#include "ui/WaveformStyleEditor.h"
#include "ui/Theme.h"

#include <cmath>
#include <numbers>

namespace sonarc {

namespace {

constexpr std::uint32_t kTestRate = 48000;
using Shape = WaveformStyle::Shape;

const char* const kShapeNames[] = { "Classic", "Mirrored", "Oscilloscope", "Vertical bars", "Filled", "Circular" };
const char* const kColourNames[] = { "Spectral bands (bass / mid / high colours)", "Channel colours (left / right)",
                                     "Spectrum gradient", "Accent colour" };

} // namespace

WaveformStyleEditor::WaveformStyleEditor(WaveformPresetLibrary& presetLibrary, Host& editorHost,
                                         const WaveformStyle& current, const juce::String& currentPreset)
    : library(presetLibrary), host(editorHost), working(current), presetName(currentPreset)
{
    if (const auto* p = library.find(presetName))
        saved = p->style;
    else
    {
        presetName = waveformPresets::defaultPreset().name;
        saved = waveformPresets::defaultPreset().style;
    }

    // ---- Presets
    presetBox.setTooltip("Switch preset (favourites first)");
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id > 0 && id <= (int) presetIds.size())
            selectPreset(presetIds[(size_t) id - 1]);
    };
    addAndMakeVisible(presetBox);

    favouriteButton.setTooltip("Show this preset at the top of the list and in the right-click menu");
    favouriteButton.onClick = [this]
    {
        library.setFavourite(presetName, favouriteButton.getToggleState());
        refreshPresetBox();
    };
    addAndMakeVisible(favouriteButton);

    for (auto* b : { &saveButton, &saveAsButton, &renameButton, &duplicateButton, &deleteButton, &importButton,
                     &exportButton, &defaultButton, &coloursButton, &resetAllButton })
        addAndMakeVisible(*b);

    saveButton.setTooltip("Save changes into this preset (built-ins: Save As instead)");
    saveButton.onClick = [this] { save(); };
    saveAsButton.setTooltip("Save as a new preset");
    saveAsButton.onClick = [this] { saveAs(); };
    renameButton.onClick = [this] { renamePreset(); };
    duplicateButton.onClick = [this] { duplicatePreset(); };
    deleteButton.onClick = [this] { deletePreset(); };
    importButton.setTooltip("Add presets from a .json file");
    importButton.onClick = [this] { importPresets(); };
    exportButton.setTooltip("Save presets to a .json file to share");
    exportButton.onClick = [this] { exportPresets(); };
    defaultButton.setTooltip("Go back to the default waveform (" + waveformPresets::defaultPreset().name + ")");
    defaultButton.onClick = [this] { selectPreset(waveformPresets::defaultPreset().name); };

    // ---- Preview
    previewLabel.setText("Live preview", juce::dontSendNotification);
    previewLabel.setFont(theme::font(12.5f, true));
    addAndMakeVisible(previewLabel);
    previewSource.addItem("Live audio (test signal when silent)", 1);
    previewSource.addItem("Live audio only", 2);
    previewSource.addItem("Test signal (simulated)", 3);
    previewSource.setSelectedId(1, juce::dontSendNotification);
    previewSource.onChange = [this] { updatePreviewSource(); };
    addAndMakeVisible(previewSource);
    addAndMakeVisible(preview);

    // ---- Style
    shapeLabel.setText("Style", juce::dontSendNotification);
    shapeLabel.setFont(theme::font(12.5f, true));
    addAndMakeVisible(shapeLabel);
    for (int i = 0; i < 6; ++i)
        shapeBox.addItem(kShapeNames[i], i + 1);
    shapeBox.onChange = [this]
    {
        working.shape = (Shape) (shapeBox.getSelectedId() - 1);
        edited();
        layoutRows();
    };
    addAndMakeVisible(shapeBox);

    coloursButton.setTooltip("Open the colour theme editor (the waveform uses the theme's colours)");
    coloursButton.onClick = [this] { host.openColourEditor(); };
    resetAllButton.setTooltip("Undo all changes since this preset was last saved");
    resetAllButton.onClick = [this]
    {
        working = saved;
        refreshControls();
        edited();
    };

    settingsHeading.setText("Settings for this style", juce::dontSendNotification);
    settingsHeading.setFont(theme::font(11.0f, true));
    settingsHeading.setColour(juce::Label::textColourId, theme::textDim);
    addAndMakeVisible(settingsHeading);

    addRow(Setting::Colour, "Colours");
    addRow(Setting::Amplitude, "Amplitude");
    addRow(Setting::Window, "Time shown (speed)");
    addRow(Setting::LineThickness, "Line thickness");
    addRow(Setting::Glow, "Glow");
    addRow(Setting::Smoothing, "Smoothing");
    addRow(Setting::FillOpacity, "Fill opacity");
    addRow(Setting::StereoSeparation, "Stereo separation");
    addRow(Setting::AnimationSpeed, "Animation speed");
    addRow(Setting::BarCount, "Bars");
    addRow(Setting::PeakHold, "Peak hold");
    addRow(Setting::Direction, "Direction");

    testBuilder.prepare(kTestRate);
    refreshPresetBox();
    refreshControls();
    updatePreviewSource();
    setSize(preferredWidth, preferredHeight);
    startTimerHz(60);
}

WaveformStyleEditor::~WaveformStyleEditor()
{
    stopTimer();
}

//==============================================================================
// Settings rows

void WaveformStyleEditor::addRow(Setting setting, const juce::String& text)
{
    Row row;
    row.setting = setting;
    row.label = std::make_unique<juce::Label>(juce::String(), text);
    row.label->setFont(theme::font(12.5f));
    addChildComponent(*row.label);

    if (setting == Setting::PeakHold)
    {
        row.toggle = std::make_unique<juce::ToggleButton>("Show peak caps");
        row.toggle->onClick = [this, setting]
        {
            for (auto& r : rows)
                if (r.setting == setting)
                    working.peakHold = r.toggle->getToggleState();
            edited();
        };
        addChildComponent(*row.toggle);
    }
    else if (setting == Setting::Direction || setting == Setting::Colour)
    {
        row.choice = std::make_unique<juce::ComboBox>();
        if (setting == Setting::Direction)
        {
            row.choice->addItem("Newest on the right (scrolls left)", 1);
            row.choice->addItem("Newest on the left (scrolls right)", 2);
        }
        else
        {
            for (int i = 0; i < 4; ++i)
                row.choice->addItem(kColourNames[i], i + 1);
        }
        row.choice->onChange = [this, setting]
        {
            for (auto& r : rows)
                if (r.setting == setting)
                {
                    if (setting == Setting::Direction)
                        working.direction = (WaveformStyle::Direction) (r.choice->getSelectedId() - 1);
                    else
                        working.colour = (WaveformStyle::ColourSource) (r.choice->getSelectedId() - 1);
                }
            edited();
        };
        addChildComponent(*row.choice);
    }
    else
    {
        row.slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
        row.slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 20);
        WaveformStyle::Range range { 0.0f, 1.0f };
        double step = 0.01;
        switch (setting)
        {
            case Setting::LineThickness:    range = WaveformStyle::lineThicknessRange; step = 0.1; break;
            case Setting::Glow:             range = WaveformStyle::glowRange; break;
            case Setting::Amplitude:        range = WaveformStyle::amplitudeRange; break;
            case Setting::Smoothing:        range = WaveformStyle::smoothingRange; break;
            case Setting::Window:           range = WaveformStyle::windowRange; step = 0.01; break;
            case Setting::AnimationSpeed:   range = WaveformStyle::animationSpeedRange; break;
            case Setting::StereoSeparation: range = WaveformStyle::stereoSeparationRange; break;
            case Setting::FillOpacity:      range = WaveformStyle::fillOpacityRange; break;
            case Setting::BarCount:         range = { (float) WaveformStyle::minBarCount, (float) WaveformStyle::maxBarCount }; step = 1.0; break;
            case Setting::PeakHold:
            case Setting::Direction:
            case Setting::Colour:           break;
        }
        row.slider->setRange(range.min, range.max, step);
        if (setting == Setting::Window)
        {
            row.slider->setSkewFactorFromMidPoint(1.0);
            row.slider->setTextValueSuffix(" s");
        }
        if (setting == Setting::Amplitude)
            row.slider->setSkewFactorFromMidPoint(1.0);
        row.slider->onValueChange = [this, setting]
        {
            for (auto& r : rows)
                if (r.setting == setting)
                    setValue(working, setting, (float) r.slider->getValue());
            edited();
        };
        addChildComponent(*row.slider);
    }
    row.reset = std::make_unique<juce::TextButton>("Reset");
    row.reset->setTooltip("Back to the value saved in this preset");
    row.reset->onClick = [this, setting]
    {
        switch (setting)
        {
            case Setting::PeakHold:  working.peakHold = saved.peakHold; break;
            case Setting::Direction: working.direction = saved.direction; break;
            case Setting::Colour:    working.colour = saved.colour; break;
            default:                 setValue(working, setting, valueOf(saved, setting)); break;
        }
        refreshControls();
        edited();
    };
    addChildComponent(*row.reset);

    rows.push_back(std::move(row));
}

float WaveformStyleEditor::valueOf(const WaveformStyle& s, Setting setting) const
{
    switch (setting)
    {
        case Setting::LineThickness:    return s.lineThickness;
        case Setting::Glow:             return s.glow;
        case Setting::Amplitude:        return s.amplitude;
        case Setting::Smoothing:        return s.smoothing;
        case Setting::Window:           return s.windowSeconds;
        case Setting::AnimationSpeed:   return s.animationSpeed;
        case Setting::StereoSeparation: return s.stereoSeparation;
        case Setting::FillOpacity:      return s.fillOpacity;
        case Setting::BarCount:         return (float) s.barCount;
        case Setting::PeakHold:         return s.peakHold ? 1.0f : 0.0f;
        case Setting::Direction:        return (float) s.direction;
        case Setting::Colour:           return (float) s.colour;
    }
    return 0.0f;
}

void WaveformStyleEditor::setValue(WaveformStyle& s, Setting setting, float value) const
{
    switch (setting)
    {
        case Setting::LineThickness:    s.lineThickness = value; break;
        case Setting::Glow:             s.glow = value; break;
        case Setting::Amplitude:        s.amplitude = value; break;
        case Setting::Smoothing:        s.smoothing = value; break;
        case Setting::Window:           s.windowSeconds = value; break;
        case Setting::AnimationSpeed:   s.animationSpeed = value; break;
        case Setting::StereoSeparation: s.stereoSeparation = value; break;
        case Setting::FillOpacity:      s.fillOpacity = value; break;
        case Setting::BarCount:         s.barCount = juce::roundToInt(value); break;
        case Setting::PeakHold:         s.peakHold = value > 0.5f; break;
        case Setting::Direction:        s.direction = (WaveformStyle::Direction) juce::roundToInt(value); break;
        case Setting::Colour:           s.colour = (WaveformStyle::ColourSource) juce::roundToInt(value); break;
    }
}

void WaveformStyleEditor::refreshControls()
{
    shapeBox.setSelectedId((int) working.shape + 1, juce::dontSendNotification);
    for (auto& r : rows)
    {
        if (r.slider != nullptr)
            r.slider->setValue(valueOf(working, r.setting), juce::dontSendNotification);
        if (r.toggle != nullptr)
            r.toggle->setToggleState(working.peakHold, juce::dontSendNotification);
        if (r.choice != nullptr)
            r.choice->setSelectedId(juce::roundToInt(valueOf(working, r.setting)) + 1, juce::dontSendNotification);
    }
    preview.setStyle(working);
    layoutRows();
}

bool WaveformStyleEditor::isModified() const
{
    return ! (working == saved);
}

const WaveformPreset* WaveformStyleEditor::currentPreset() const
{
    return library.find(presetName);
}

void WaveformStyleEditor::edited()
{
    preview.setStyle(working);
    host.waveformStyleChanged(working, presetName, isModified());
    refreshPresetBox();
    for (auto& r : rows)
    {
        const bool differs = r.setting == Setting::PeakHold    ? working.peakHold != saved.peakHold
                           : r.setting == Setting::Direction ? working.direction != saved.direction
                           : r.setting == Setting::Colour    ? working.colour != saved.colour
                                                             : std::abs(valueOf(working, r.setting) - valueOf(saved, r.setting)) > 1.0e-4f;
        r.reset->setEnabled(differs);
    }
}

void WaveformStyleEditor::externalChange(const WaveformStyle& style, const juce::String& name)
{
    if (name != presetName)
    {
        presetName = name;
        if (const auto* p = currentPreset())
            saved = p->style;
    }
    working = style;
    refreshControls();
    refreshPresetBox();
}

void WaveformStyleEditor::themeChanged()
{
    preview.themeChanged();
    repaint();
}

//==============================================================================
// Presets

void WaveformStyleEditor::refreshPresetBox()
{
    presetBox.clear(juce::dontSendNotification);
    presetIds.clear();

    const auto list = library.all();
    auto add = [this](const WaveformPreset& p)
    {
        presetIds.push_back(p.name);
        presetBox.addItem((p.favourite ? "* " : "") + p.name, (int) presetIds.size());
    };

    bool anyFavourite = false;
    for (const auto& p : list)
        anyFavourite = anyFavourite || p.favourite;
    if (anyFavourite)
    {
        presetBox.addSectionHeading("Favourites");
        for (const auto& p : list)
            if (p.favourite)
                add(p);
    }
    presetBox.addSectionHeading("Built-in");
    for (const auto& p : list)
        if (p.builtIn && ! p.favourite)
            add(p);
    bool anyUser = false;
    for (const auto& p : list)
        anyUser = anyUser || (! p.builtIn && ! p.favourite);
    if (anyUser)
    {
        presetBox.addSectionHeading("My presets");
        for (const auto& p : list)
            if (! p.builtIn && ! p.favourite)
                add(p);
    }

    for (size_t i = 0; i < presetIds.size(); ++i)
        if (presetIds[i] == presetName)
            presetBox.setSelectedId((int) i + 1, juce::dontSendNotification);
    if (isModified())
        presetBox.setText(presetName + "  (edited)", juce::dontSendNotification);

    const auto* preset = currentPreset();
    const bool builtIn = preset != nullptr && preset->builtIn;
    favouriteButton.setToggleState(preset != nullptr && preset->favourite, juce::dontSendNotification);
    saveButton.setEnabled(! builtIn && isModified());
    renameButton.setEnabled(! builtIn);
    deleteButton.setEnabled(! builtIn);
    saveButton.setTooltip(builtIn ? "Built-in presets can't be changed - use Save As" : "Save changes into this preset");
}

void WaveformStyleEditor::selectPreset(const juce::String& name)
{
    const auto* preset = library.find(name);
    if (preset == nullptr)
        return;
    presetName = name;
    saved = working = preset->style;
    refreshControls();
    edited();
}

void WaveformStyleEditor::save()
{
    juce::String error;
    if (! library.save(presetName, working, error))
    {
        showMessage("Can't save", error);
        return;
    }
    saved = working;
    edited();
}

void WaveformStyleEditor::saveAs()
{
    askForName("Save waveform preset", library.uniqueName(presetName), [this](juce::String name)
    {
        name = name.trim();
        const auto* existing = library.find(name);
        if (existing != nullptr && existing->builtIn)
        {
            showMessage("Can't save", "\"" + name + "\" is a built-in preset. Please choose another name.");
            return;
        }
        juce::String error;
        if (! library.save(name, working, error))
        {
            showMessage("Can't save", error);
            return;
        }
        presetName = name;
        saved = working;
        edited();
    });
}

void WaveformStyleEditor::renamePreset()
{
    askForName("Rename preset", presetName, [this](juce::String name)
    {
        juce::String error;
        if (! library.rename(presetName, name, error))
        {
            showMessage("Can't rename", error);
            return;
        }
        presetName = name.trim();
        edited();
    });
}

void WaveformStyleEditor::duplicatePreset()
{
    const auto copy = library.duplicate(presetName);
    if (copy.isEmpty())
        return;
    // The copy holds the saved preset; carry over any unsaved edits as edits of the copy.
    const auto edits = working;
    selectPreset(copy);
    working = edits;
    refreshControls();
    edited();
}

void WaveformStyleEditor::deletePreset()
{
    const auto name = presetName;
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                     .withTitle("Delete preset")
                                     .withMessage("Delete \"" + name + "\"? This can't be undone.")
                                     .withButton("Delete")
                                     .withButton("Cancel")
                                     .withAssociatedComponent(this),
                                 [this, name](int result)
                                 {
                                     if (result != 1)
                                         return;
                                     juce::String error;
                                     if (! library.remove(name, error))
                                         showMessage("Can't delete", error);
                                     else
                                         selectPreset(waveformPresets::defaultPreset().name);
                                 });
}

void WaveformStyleEditor::importPresets()
{
    fileChooser = std::make_unique<juce::FileChooser>("Import waveform presets", juce::File(), "*.json", true, false, this);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& chooser)
                             {
                                 const auto file = chooser.getResult();
                                 if (file == juce::File())
                                     return;
                                 if (file.getSize() > 1024 * 1024)
                                 {
                                     showMessage("Import failed", "That file is too large to be a preset file.");
                                     return;
                                 }

                                 const auto result = waveformPresets::importJson(file.loadFileAsString());
                                 const auto names = library.add(result.presets);
                                 juce::String message;
                                 if (! names.isEmpty())
                                     message << "Imported: " << names.joinIntoString(", ") << "\n";
                                 if (! result.errors.isEmpty())
                                     message << (names.isEmpty() ? "Nothing was imported.\n" : "\nSkipped:\n")
                                             << result.errors.joinIntoString("\n");
                                 showMessage(names.isEmpty() ? "Import failed" : "Import", message.trim());
                                 if (! names.isEmpty())
                                     selectPreset(names[0]);
                             });
}

void WaveformStyleEditor::exportPresets()
{
    juce::PopupMenu menu;
    menu.addItem("Export this preset...", [this]
    {
        auto preset = currentPreset() != nullptr ? *currentPreset() : WaveformPreset { presetName, working };
        preset.style = working; // include unsaved edits
        const auto text = waveformPresets::exportJson({ preset });
        fileChooser = std::make_unique<juce::FileChooser>("Export waveform preset",
                                                          juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                                              .getChildFile(juce::File::createLegalFileName(preset.name) + ".json"),
                                                          "*.json", true, false, this);
        fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                 [this, text](const juce::FileChooser& chooser)
                                 {
                                     const auto file = chooser.getResult();
                                     if (file != juce::File() && ! file.withFileExtension("json").replaceWithText(text))
                                         showMessage("Export failed", "Couldn't write " + file.getFullPathName());
                                 });
    });
    menu.addItem("Export all my presets...", [this]
    {
        std::vector<WaveformPreset> mine;
        for (const auto& p : library.all())
            if (! p.builtIn)
                mine.push_back(p);
        if (mine.empty())
        {
            showMessage("Nothing to export", "You haven't saved any presets of your own yet.");
            return;
        }
        const auto text = waveformPresets::exportJson(mine);
        fileChooser = std::make_unique<juce::FileChooser>("Export all my waveform presets",
                                                          juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                                              .getChildFile("Sonarc waveform presets.json"),
                                                          "*.json", true, false, this);
        fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                 [this, text](const juce::FileChooser& chooser)
                                 {
                                     const auto file = chooser.getResult();
                                     if (file != juce::File() && ! file.withFileExtension("json").replaceWithText(text))
                                         showMessage("Export failed", "Couldn't write " + file.getFullPathName());
                                 });
    });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&exportButton));
}

void WaveformStyleEditor::askForName(const juce::String& title, const juce::String& initial, std::function<void(juce::String)> onOk)
{
    auto* window = new juce::AlertWindow(title, "Preset name:", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", initial);
    window->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->setAlwaysOnTop(true);
    window->enterModalState(true, juce::ModalCallbackFunction::create([window, onOk](int result)
    {
        if (result == 1)
            onOk(window->getTextEditorContents("name"));
    }), true);
}

void WaveformStyleEditor::showMessage(const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::InfoIcon)
                                     .withTitle(title)
                                     .withMessage(message)
                                     .withButton("OK")
                                     .withAssociatedComponent(this),
                                 nullptr);
}

//==============================================================================
// Preview

void WaveformStyleEditor::feedLive(const WaveBucket* buckets, std::size_t count, std::uint32_t sampleRate)
{
    liveRate = sampleRate;
    for (std::size_t i = 0; i < count; ++i)
        if (juce::jmax(std::abs(buckets[i].minMid), std::abs(buckets[i].maxMid)) > 0.001f) // above -60 dBFS
        {
            lastLiveSoundMs = juce::Time::getMillisecondCounterHiRes();
            break;
        }

    if (! showingTest)
    {
        preview.setSampleRate(sampleRate);
        preview.append(buckets, count);
    }
}

void WaveformStyleEditor::updatePreviewSource()
{
    const auto source = (Source) (previewSource.getSelectedId() - 1);
    const bool liveIsSounding = liveRate > 0 && juce::Time::getMillisecondCounterHiRes() - lastLiveSoundMs < 1500.0;
    const bool test = source == Source::Test || (source == Source::Auto && ! liveIsSounding);

    if (test != showingTest)
    {
        showingTest = test;
        preview.setSampleRate(0); // drop history from the other source
        preview.setSampleRate(test ? kTestRate : liveRate);
        testBuilder.reset();
    }
    preview.setBadge(showingTest ? "TEST SIGNAL (simulated)" : (liveRate == 0 ? "NO AUDIO" : "LIVE AUDIO"));
}

// A short synthetic groove (kick, bass, chord pad, hats) so styles can be
// judged when nothing is playing. Always labelled as simulated in the preview.
void WaveformStyleEditor::generateTestSignal(double seconds)
{
    const auto frames = (std::size_t) (juce::jlimit(0.0, 0.25, seconds) * kTestRate);
    if (frames == 0)
        return;

    testFrames.resize(frames);
    const double beat = 60.0 / 120.0;
    for (std::size_t i = 0; i < frames; ++i)
    {
        const double t = testTime + (double) i / kTestRate;
        const double inBeat = std::fmod(t, beat);
        const double kickHz = 50.0 + 90.0 * std::exp(-inBeat * 30.0);
        double s = 0.55 * std::sin(2.0 * std::numbers::pi * kickHz * inBeat) * std::exp(-inBeat * 9.0);

        const double root = std::fmod(t, 8.0) < 4.0 ? 110.0 : 98.0;
        s += 0.12 * std::sin(2.0 * std::numbers::pi * root * 0.5 * t) * (0.6 + 0.4 * std::exp(-inBeat * 4.0));
        for (double ratio : { 1.0, 1.26, 1.5 })
            s += 0.035 * std::sin(2.0 * std::numbers::pi * root * 2.0 * ratio * t);

        const double inEighth = std::fmod(t, beat / 2.0);
        const double noise = std::sin(t * 12345.678 + std::sin(t * 9876.5) * 1000.0); // deterministic hiss
        s += 0.1 * noise * std::exp(-inEighth * 70.0);

        const float pan = (float) (0.2 * std::sin(t * 1.3));
        testFrames[i] = { (float) s * (1.0f - pan), (float) s * (1.0f + pan) };
    }
    testTime += (double) frames / kTestRate;

    testBuckets.resize(WaveformBuilder::maxBucketsFor(frames));
    const auto produced = testBuilder.process(testFrames.data(), frames, testBuckets.data());
    preview.append(testBuckets.data(), produced);
}

void WaveformStyleEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double elapsed = lastTimerMs > 0.0 ? (now - lastTimerMs) / 1000.0 : 0.0;
    lastTimerMs = now;

    updatePreviewSource();
    if (showingTest)
        generateTestSignal(elapsed);
}

//==============================================================================
// Layout

void WaveformStyleEditor::layoutRows()
{
    const auto area = rowsArea; // what resized() left below the style row
    const int columnWidth = (area.getWidth() - 20) / 2;
    int index = 0;
    for (auto& r : rows)
    {
        const bool relevant = WaveformStyle::uses(working.shape, r.setting);
        for (juce::Component* c : { (juce::Component*) r.label.get(), (juce::Component*) r.slider.get(),
                                    (juce::Component*) r.toggle.get(), (juce::Component*) r.choice.get(),
                                    (juce::Component*) r.reset.get() })
            if (c != nullptr)
                c->setVisible(relevant);
        if (! relevant)
            continue;

        const int column = index % 2;
        const int line = index / 2;
        auto cell = juce::Rectangle<int>(area.getX() + column * (columnWidth + 20), area.getY() + line * 30, columnWidth, 26);
        r.label->setBounds(cell.removeFromLeft(124));
        r.reset->setBounds(cell.removeFromRight(52));
        cell.removeFromRight(6);
        if (r.slider != nullptr)
            r.slider->setBounds(cell);
        if (r.toggle != nullptr)
            r.toggle->setBounds(cell);
        if (r.choice != nullptr)
            r.choice->setBounds(cell);
        ++index;
    }
}

void WaveformStyleEditor::resized()
{
    auto area = getLocalBounds().reduced(16);

    // Preset row 1: picker + favourite + save buttons.
    auto row = area.removeFromTop(30);
    presetBox.setBounds(row.removeFromLeft(280));
    row.removeFromLeft(10);
    favouriteButton.setBounds(row.removeFromLeft(100));
    for (auto* b : { &saveButton, &saveAsButton, &renameButton })
    {
        b->setBounds(row.removeFromLeft(96).reduced(2, 0));
    }
    area.removeFromTop(6);

    // Preset row 2: management.
    row = area.removeFromTop(30);
    for (auto* b : { &duplicateButton, &deleteButton, &importButton, &exportButton })
        b->setBounds(row.removeFromLeft(110).reduced(2, 0));
    defaultButton.setBounds(row.removeFromRight(140).reduced(2, 0));
    area.removeFromTop(12);

    // Preview.
    row = area.removeFromTop(26);
    previewLabel.setBounds(row.removeFromLeft(120));
    previewSource.setBounds(row.removeFromRight(300));
    preview.setBounds(area.removeFromTop(220));
    area.removeFromTop(14);

    // Style row.
    row = area.removeFromTop(30);
    shapeLabel.setBounds(row.removeFromLeft(56));
    shapeBox.setBounds(row.removeFromLeft(220));
    resetAllButton.setBounds(row.removeFromRight(120).reduced(2, 0));
    coloursButton.setBounds(row.removeFromRight(140).reduced(2, 0));
    area.removeFromTop(14);
    settingsHeading.setBounds(area.removeFromTop(22));

    rowsArea = area;
    layoutRows();
}

void WaveformStyleEditor::paint(juce::Graphics& g)
{
    g.fillAll(theme::background);
}

} // namespace sonarc
