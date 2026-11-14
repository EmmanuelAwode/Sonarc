#include <juce_gui_extra/juce_gui_extra.h>
#include "plugin/PluginEditor.h"

namespace driftwave {

namespace {
constexpr const char* kEditorWidthProperty = "editorWidth";
constexpr const char* kVisualiserProperty = "visualiser";
constexpr const char* kAccentProperty = "accentColour";
constexpr float kMeterFloorDb = -48.0f;
// Layout grid (px at the design size).
constexpr int kHeaderHeight = 38, kDisplayHeight = 124, kTitleHeight = 30, kPad = 14;
constexpr int kSpeedWidth = 250, kReverbWidth = 320, kBassWidth = 196;
}

// ---------------------------------------------------------------------------

Knob::Knob(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& title, Size knobSize)
    : size(knobSize)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, kValueHeight);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    // Holding Shift slows the drag right down for fine adjustment.
    slider.setVelocityModeParameters(0.3, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
    addAndMakeVisible(slider);

    label.setText(title, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(uiFont(type::label));
    label.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(label);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, paramId, slider);
    // Hover and the drawn position are animated; see timerCallback.
    slider.addMouseListener(this, false);
    slider.onValueChange = [this] { startTimerHz(60); };
    shown = float(slider.valueToProportionOfLength(slider.getValue()));
    slider.getProperties().set("shown", shown);
    slider.getProperties().set("hover", 0.0f);
    if (const auto* param = state.getParameter(paramId)) {
        slider.setDoubleClickReturnValue(true, param->convertFrom0to1(param->getDefaultValue()));
        slider.setTooltip(param->getName(64) + ". Shift-drag for fine steps, double-click to reset, click the value to type.");
    }
}

void Knob::timerCallback()
{
    // Under the hand the ring follows at once. A change from anywhere else (a preset,
    // automation, a typed value) glides there in about a tenth of a second.
    const auto target = float(slider.valueToProportionOfLength(slider.getValue()));
    shown = slider.isMouseButtonDown() ? target : shown + (target - shown) * 0.35f;
    if (std::abs(target - shown) < 0.0015f)
        shown = target;

    const float hoverTarget = slider.isMouseOverOrDragging() ? 1.0f : 0.0f;
    hover += (hoverTarget - hover) * 0.3f;
    if (std::abs(hoverTarget - hover) < 0.02f)
        hover = hoverTarget;

    slider.getProperties().set("shown", shown);
    slider.getProperties().set("hover", hover);
    slider.repaint();
    if (shown == target && hover == hoverTarget)
        stopTimer();
}

void Knob::resized()
{
    label.setBounds(0, 0, getWidth(), kLabelHeight);
    slider.setBounds(0, kLabelHeight, getWidth(), getHeight() - kLabelHeight);
}

// ---------------------------------------------------------------------------

OutputMeter::OutputMeter(DriftwaveProcessor& p, const Palette& pal) : processor(p), palette(pal)
{
}

void OutputMeter::updateTimer()
{
    if (isShowing())
        startTimerHz(30);
    else
        stopTimer();
}

void OutputMeter::timerCallback()
{
    for (int ch = 0; ch < 2; ++ch) {
        const float peak = processor.takePeak(ch);
        const float db = juce::Decibels::gainToDecibels(peak, -100.0f);
        levelDb[ch] = juce::jmax(db, levelDb[ch] - 1.2f);   // fall at about 36 dB/s
        holdDb[ch] = juce::jmax(db, holdDb[ch] - 0.25f);
        if (peak >= 1.0f)
            clipped[ch] = true;
    }
    // Clip-guard reduction: jumps to the deepest value, then recovers at 12 dB/s.
    reductionDb = juce::jmin(processor.takeClipReductionDb(), reductionDb + 0.4f, 0.0f);
    repaint();
}

void OutputMeter::paint(juce::Graphics& g)
{
    // A scale on the left, a thin reduction bar, two level bars, a clip lamp over each.
    auto area = getLocalBounds().toFloat();
    const float barWidth = 6.0f, gap = 2.0f;
    auto bars = area.removeFromRight(barWidth * 2.0f + gap);
    area.removeFromRight(4.0f);
    auto reduction = area.removeFromRight(3.0f);
    auto lamps = bars.removeFromTop(4.0f);
    bars.removeFromTop(3.0f);
    bars.removeFromBottom(7.0f); // room for the lowest scale number
    reduction = reduction.withTop(bars.getY()).withBottom(bars.getBottom());

    const auto toY = [&](float db) {
        const float t = juce::jlimit(0.0f, 1.0f, (db - kMeterFloorDb) / -kMeterFloorDb);
        return bars.getBottom() - t * bars.getHeight();
    };

    g.setFont(uiFont(type::small));
    for (const int mark : { 0, -12, -24, -36, -48 }) {
        const float y = toY(float(mark));
        g.setColour(palette.border);
        g.fillRect(reduction.getX() - 5.0f, std::floor(y), 3.0f, 1.0f);
        g.setColour(palette.textDim);
        g.drawText(juce::String(std::abs(mark)), juce::Rectangle<float>(area.getX(), y - 7.0f, area.getWidth() - 8.0f, 14.0f),
                   juce::Justification::centredRight, false);
    }

    // What the clip guard is taking off, growing downwards from the top (to the same dB scale).
    g.setColour(palette.background);
    g.fillRect(reduction);
    if (reductionDb < -0.05f) {
        g.setColour(palette.clip);
        g.fillRect(reduction.withBottom(juce::jmin(reduction.getBottom(), toY(reductionDb))));
    }

    for (int ch = 0; ch < 2; ++ch) {
        const float x = bars.getX() + float(ch) * (barWidth + gap);
        g.setColour(clipped[ch] ? palette.clip : palette.border);
        g.fillRoundedRectangle(x, lamps.getY(), barWidth, lamps.getHeight(), 1.0f);

        const auto column = juce::Rectangle<float>(x, bars.getY(), barWidth, bars.getHeight());
        g.setColour(palette.background);
        g.fillRect(column);

        const float top = toY(levelDb[ch]);
        if (top < column.getBottom()) {
            // Cool grey that warms into amber over the top 12 dB.
            juce::ColourGradient fill(palette.accent, 0.0f, toY(0.0f), palette.signal.withAlpha(0.75f), 0.0f, column.getBottom(), false);
            fill.addColour(double((toY(-12.0f) - toY(0.0f)) / (column.getBottom() - toY(0.0f))), palette.signal);
            g.setGradientFill(fill);
            g.fillRect(column.withTop(top));
        }
        if (holdDb[ch] > kMeterFloorDb) {
            g.setColour(palette.text);
            g.fillRect(column.getX(), std::floor(toY(holdDb[ch])), column.getWidth(), 1.0f);
        }
    }
}

// ---------------------------------------------------------------------------

LagReadout::LagReadout(DriftwaveProcessor& p, const Palette& pal) : processor(p), palette(pal)
{
    setInterceptsMouseClicks(false, false);
    startTimerHz(10);
}

void LagReadout::timerCallback()
{
    const float lag = processor.speedLagSeconds();
    // Faster than 100% always runs in sync, whatever the switch says.
    const bool sync = processor.apvts.getRawParameterValue(id::speedOn)->load() > 0.5f
                   && (processor.apvts.getRawParameterValue(id::speedSync)->load() > 0.5f
                       || processor.apvts.getRawParameterValue(id::speed)->load() > 100.0f);
    if (std::abs(lag - shownLag) >= 0.05f || (lag == 0.0f) != (shownLag == 0.0f) || sync != shownSync) {
        shownLag = lag;
        shownSync = sync;
        repaint();
    }
}

void LagReadout::paint(juce::Graphics& g)
{
    const bool live = shownLag < 0.05f;
    g.setFont(uiFont(type::label));
    g.setColour(!live ? palette.accent : palette.textDim);
    g.drawText(!live ? juce::String(shownLag, 1) + " s behind" : shownSync ? juce::String("In sync") : juce::String("Live"),
               getLocalBounds(), juce::Justification::centredLeft);
}

// ---------------------------------------------------------------------------

PresetBar::PresetBar(DriftwaveProcessor& p) : manager(p.apvts)
{
    for (auto* c : std::initializer_list<juce::Component*> { &box, &previous, &next, &save, &remove })
        addAndMakeVisible(c);

    box.setTextWhenNothingSelected("Presets");
    box.setTooltip("Presets: the built-in ones, then your own");
    box.onChange = [this] {
        const int index = box.getSelectedId() - 1;
        if (index >= 0 && index < presets.size() && presets[index].name != shownName && manager.load(presets[index]))
            refresh();
    };
    previous.setTooltip("Previous preset");
    next.setTooltip("Next preset");
    previous.onClick = [this] { step(-1); };
    next.onClick = [this] { step(1); };
    save.setTooltip("Save the current settings as your own preset");
    save.onClick = [this] { askForNameAndSave(); };
    remove.setTooltip("Delete this preset of yours (it goes to the recycle bin)");
    remove.onClick = [this] {
        const int index = box.getSelectedId() - 1;
        if (index >= 0 && index < presets.size() && manager.removeUser(presets[index]))
            refresh();
    };

    refresh();
    startTimerHz(2); // picks up a project being loaded, or preset files changed outside the plugin
}

void PresetBar::resized()
{
    auto area = getLocalBounds();
    remove.setBounds(area.removeFromRight(52));
    area.removeFromRight(4);
    save.setBounds(area.removeFromRight(46));
    area.removeFromRight(12);
    previous.setBounds(area.removeFromLeft(24));
    area.removeFromLeft(4);
    next.setBounds(area.removeFromRight(24));
    area.removeFromRight(4);
    box.setBounds(area);
}

void PresetBar::timerCallback()
{
    if (box.isPopupActive())
        return;
    const auto latest = manager.list();
    bool changed = manager.currentName() != shownName || latest.size() != presets.size();
    for (int i = 0; !changed && i < latest.size(); ++i)
        changed = latest[i].name != presets[i].name;
    if (changed)
        refresh();
}

void PresetBar::refresh()
{
    presets = manager.list();
    shownName = manager.currentName();

    box.clear(juce::dontSendNotification);
    int selected = 0;
    bool selectedIsUser = false, headingAdded = false;
    box.addSectionHeading("Built in");
    for (int i = 0; i < presets.size(); ++i) {
        if (!presets[i].factory && !headingAdded) {
            box.addSeparator();
            box.addSectionHeading("Yours");
            headingAdded = true;
        }
        box.addItem(presets[i].name, i + 1);
        // A preset of yours wins over a built-in one of the same name: it was saved later.
        if (presets[i].name == shownName && (selected == 0 || !presets[i].factory)) {
            selected = i + 1;
            selectedIsUser = !presets[i].factory;
        }
    }
    if (selected != 0)
        box.setSelectedId(selected, juce::dontSendNotification);
    else
        box.setText(shownName, juce::dontSendNotification);
    remove.setEnabled(selectedIsUser);
}

void PresetBar::step(int direction)
{
    if (presets.isEmpty())
        return;
    const int current = box.getSelectedId() - 1; // -1 when nothing in the list is selected
    const int target = current < 0 ? 0 : (current + direction + presets.size()) % presets.size();
    if (manager.load(presets[target]))
        refresh();
}

void PresetBar::askForNameAndSave()
{
    // Suggest the current name when it is one of the user's own, so saving again updates it.
    const int index = box.getSelectedId() - 1;
    const bool own = index >= 0 && index < presets.size() && !presets[index].factory;

    auto* window = new juce::AlertWindow("Save preset", "Name for this preset:", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", own ? presets[index].name : juce::String("My preset"));
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<PresetBar>(this), window](int result) {
                                if (result != 1 || safe == nullptr)
                                    return;
                                const auto saved = safe->manager.saveUser(window->getTextEditorContents("name"));
                                if (saved.name.isEmpty())
                                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Save preset",
                                                                           "The preset could not be saved. Check the name and that "
                                                                               + PresetManager::defaultFolder().getFullPathName()
                                                                               + " can be written to.",
                                                                           "OK", safe.getComponent());
                                safe->refresh();
                            }),
                            true);
}

// ---------------------------------------------------------------------------

EditorContent::EditorContent(DriftwaveProcessor& p, const Palette& pal)
    : palette(pal),
      processor(p),
      presetBar(p),
      stage(p, pal),
      displays(p, pal),
      speed(p.apvts, id::speed, "Speed", Knob::large),
      lag(p, pal),
      mix(p.apvts, id::reverbMix, "Mix", Knob::large),
      decay(p.apvts, id::reverbDecay, "Decay"),
      lowCut(p.apvts, id::reverbLowCut, "Low cut"),
      highCut(p.apvts, id::reverbHighCut, "High cut"),
      bassGain(p.apvts, id::bassGain, "Boost"),
      bassFreq(p.apvts, id::bassFreq, "Freq"),
      bassDrive(p.apvts, id::bassDrive, "Drive"),
      outGain(p.apvts, id::outputGain, "Gain"),
      ceiling(p.apvts, id::ceiling, "Ceiling"),
      meter(p, pal)
{
    // The large visualiser: opened from the header, remembered with the project.
    addChildComponent(stage);
    stage.setVisible(bool(p.apvts.state.getProperty(kVisualiserProperty, false)));
    displays.setStage(&stage);
    addAndMakeVisible(stageButton);
    stageButton.setClickingTogglesState(true);
    stageButton.setToggleState(stage.isVisible(), juce::dontSendNotification);
    stageButton.setTooltip("Show or hide the large visualiser (for recording videos)");
    stageButton.onClick = [this] {
        stage.setVisible(stageButton.getToggleState());
        processor.apvts.state.setProperty(kVisualiserProperty, stage.isVisible(), nullptr);
        setSize(kWidth, designHeight());
        resized();
        repaint();
        if (onLayoutChange)
            onLayoutChange();
    };

    addAndMakeVisible(colourButton);
    colourButton.setTooltip("Change the interface colour");
    colourButton.onClick = [this] { showColourMenu(); };

    for (auto* c : std::initializer_list<juce::Component*> { &presetBar, &displays, &speedOn, &speedSync, &speed, &lag, &catchUp, &reverbOn, &bassOn,
                                                             &clipOn, &mix, &decay, &lowCut, &highCut, &bassGain, &bassFreq, &bassDrive,
                                                             &outGain, &ceiling, &meter })
        addAndMakeVisible(c);

    speedOnAtt = std::make_unique<ButtonAttachment>(p.apvts, id::speedOn, speedOn);
    speedSyncAtt = std::make_unique<ButtonAttachment>(p.apvts, id::speedSync, speedSync);
    reverbOnAtt = std::make_unique<ButtonAttachment>(p.apvts, id::reverbOn, reverbOn);
    bassOnAtt = std::make_unique<ButtonAttachment>(p.apvts, id::bassOn, bassOn);
    clipOnAtt = std::make_unique<ButtonAttachment>(p.apvts, id::clipOn, clipOn);

    // The three section switches are drawn as headings.
    for (auto* b : { &speedOn, &reverbOn, &bassOn })
        b->getProperties().set("heading", true);
    speedOn.setTooltip("Speed section on / off");
    reverbOn.setTooltip("Reverb section on / off");
    bassOn.setTooltip("Bass section on / off");
    speedSync.setTooltip("Change the pitch but stay in time with the song. Off: a true slow-down that drifts behind. Presets leave this switch alone. Always used above 100%.");
    clipOn.setTooltip("Keeps the output under the ceiling");
    meter.setTooltip("Output level. Click to clear the clip lamps.");

    // Runs for clicks and for host automation alike, always on the message thread.
    for (auto* b : { &speedOn, &speedSync, &reverbOn, &bassOn, &clipOn })
        b->onStateChange = [this] { updateEnabledLook(); };

    // Jumps the slowed signal back to what FL Studio is playing right now.
    catchUp.setTooltip("Jump back to the live input");
    catchUp.onClick = [&p] { p.requestSpeedResync(); };
    displays.setSectionWidths(kSpeedWidth, kReverbWidth);
    updateEnabledLook();
    fadesReady = true; // from here on, changes fade instead of jumping

    setSize(kWidth, designHeight());
}

void EditorContent::applyAccent(juce::Colour colour, bool remember)
{
    colour = usableAccent(colour);
    processor.apvts.state.setProperty(kAccentProperty, colour.toString(), nullptr); // saved with the project
    if (remember)
        colours.setLastUsed(colour);
    if (onAccentChange)
        onAccentChange(colour);
}

void EditorContent::showColourMenu()
{
    // Built-in colours, then the user's own, each written in its colour; then the tools.
    const auto saved = colours.saved();
    const auto& builtIn = builtInColours();
    const auto current = palette.accent;
    juce::PopupMenu menu, removeMenu;
    int itemId = 1;
    for (const auto& colour : builtIn)
        menu.addColouredItem(itemId++, colour.name, colour.accent, true, colour.accent == current);
    if (!saved.empty())
        menu.addSeparator();
    for (const auto& colour : saved) {
        removeMenu.addColouredItem(1000 + itemId, colour.name, colour.accent);
        menu.addColouredItem(itemId++, colour.name, colour.accent, true, colour.accent == current);
    }
    menu.addSeparator();
    menu.addItem(900, "Custom colour...");
    menu.addItem(901, "Save this colour...");
    if (!saved.empty())
        menu.addSubMenu("Remove a saved colour", removeMenu);

    juce::Component::SafePointer<EditorContent> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(colourButton).withParentComponent(getTopLevelComponent()),
                       [safe, saved, builtIn](int chosen) {
                           if (safe == nullptr || chosen == 0)
                               return;
                           const int builtInCount = int(builtIn.size()), savedCount = int(saved.size());
                           if (chosen == 900)
                               safe->showColourPicker();
                           else if (chosen == 901)
                               safe->askForNameAndSaveColour();
                           else if (chosen > 1000 && chosen - 1001 - builtInCount < savedCount && chosen - 1001 - builtInCount >= 0)
                               safe->colours.remove(saved[size_t(chosen - 1001 - builtInCount)].name);
                           else if (chosen <= builtInCount)
                               safe->applyAccent(builtIn[size_t(chosen - 1)].accent, true);
                           else if (chosen - 1 - builtInCount < savedCount)
                               safe->applyAccent(saved[size_t(chosen - 1 - builtInCount)].accent, true);
                       });
}

namespace {
/** The colour picker in its pop-up: the interface follows it live, and the choice is
    written to the settings file once, when the pop-up closes. */
class AccentPicker final : public juce::Component, private juce::ChangeListener {
public:
    AccentPicker(juce::Colour start, std::function<void(juce::Colour, bool)> onChange) : change(std::move(onChange))
    {
        selector.setCurrentColour(start, juce::dontSendNotification);
        selector.addChangeListener(this);
        addAndMakeVisible(selector);
        setSize(260, 280);
    }
    ~AccentPicker() override
    {
        selector.removeChangeListener(this);
        if (changed)
            change(selector.getCurrentColour(), true);
    }
    void resized() override { selector.setBounds(getLocalBounds()); }

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        changed = true;
        change(selector.getCurrentColour(), false);
    }
    juce::ColourSelector selector { juce::ColourSelector::showColourAtTop | juce::ColourSelector::editableColour
                                    | juce::ColourSelector::showColourspace };
    std::function<void(juce::Colour, bool)> change;
    bool changed = false;
};
} // namespace

void EditorContent::showColourPicker()
{
    juce::Component::SafePointer<EditorContent> safe(this);
    auto picker = std::make_unique<AccentPicker>(palette.accent, [safe](juce::Colour colour, bool remember) {
        if (safe != nullptr)
            safe->applyAccent(colour, remember);
    });
    auto* top = getTopLevelComponent();
    juce::CallOutBox::launchAsynchronously(std::move(picker), top->getLocalArea(&colourButton, colourButton.getLocalBounds()), top);
}

void EditorContent::askForNameAndSaveColour()
{
    auto* window = new juce::AlertWindow("Save colour", "Name for this colour:", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", {});
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<EditorContent> safe(this);
    const auto colour = palette.accent;
    window->enterModalState(true, juce::ModalCallbackFunction::create([safe, window, colour](int result) {
                                if (result == 1 && safe != nullptr && !safe->colours.save(window->getTextEditorContents("name"), colour))
                                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Save colour",
                                                                           "The colour could not be saved. Check the name and that "
                                                                               + ColourStore::defaultFile().getFullPathName() + " can be written to.",
                                                                           "OK", safe.getComponent());
                            }),
                            true);
}

void EditorContent::setFade(juce::Component& component, bool lamp, float target)
{
    for (auto& fade : fades) {
        if (fade.component == &component && fade.lamp == lamp) {
            fade.target = target;
            return;
        }
    }
    fades.push_back({ &component, lamp, target, target });
}

void EditorContent::updateEnabledLook()
{
    // Switch lamps fade on and off; a bypassed section dims but stays editable.
    for (auto* b : { &speedOn, &speedSync, &reverbOn, &bassOn, &clipOn })
        setFade(*b, true, b->getToggleState() ? 1.0f : 0.0f);
    const auto dim = [this](bool on, std::initializer_list<juce::Component*> parts) {
        for (auto* c : parts) setFade(*c, false, on ? 1.0f : 0.35f);
    };
    dim(speedOn.getToggleState(), { &speed, &speedSync, &lag });
    // In sync mode there is never anything to catch up with.
    dim(speedOn.getToggleState() && !speedSync.getToggleState(), { &catchUp });
    dim(reverbOn.getToggleState(), { &mix, &decay, &lowCut, &highCut });
    dim(bassOn.getToggleState(), { &bassGain, &bassFreq, &bassDrive });
    dim(clipOn.getToggleState(), { &ceiling });

    if (fadesReady) {
        startTimerHz(60);
    } else {
        for (auto& fade : fades) fade.current = fade.target - 1.0f; // forces the first tick to apply every value
        timerCallback();
    }
}

void EditorContent::timerCallback()
{
    bool moving = false;
    for (auto& fade : fades) {
        if (fade.current == fade.target)
            continue;
        const float next = fadesReady ? fade.current + (fade.target - fade.current) * 0.3f : fade.target;
        fade.current = std::abs(fade.target - next) < 0.01f ? fade.target : next;
        moving = moving || fade.current != fade.target;
        if (fade.lamp) {
            fade.component->getProperties().set("lit", fade.current);
            fade.component->repaint();
        } else {
            fade.component->setAlpha(fade.current);
        }
    }
    if (!moving)
        stopTimer();
}

void EditorContent::paint(juce::Graphics& g)
{
    // A thin header on the window colour, the displays, then one strip of controls.
    g.fillAll(palette.background);
    const int stripTop = kHeaderHeight + stageOffset() + kDisplayHeight;
    g.setColour(palette.surface);
    g.fillRect(0, kHeaderHeight, getWidth(), getHeight() - kHeaderHeight);
    // The control strip is lit very slightly from above.
    g.setGradientFill(juce::ColourGradient(palette.surface.brighter(0.035f), 0.0f, float(stripTop), palette.surface.darker(0.06f), 0.0f, float(getHeight()), false));
    g.fillRect(0, stripTop, getWidth(), getHeight() - stripTop);
    g.setColour(palette.border);
    g.fillRect(0, kHeaderHeight, getWidth(), 1);

    // Sections are separated by hairlines that fade out at both ends, not boxes.
    for (const auto& section : { reverbSection, bassSection, outputSection }) {
        const float x = float(section.getX()), top = float(stripTop + 6), bottom = float(getHeight() - 12);
        juce::ColourGradient line(palette.border.withAlpha(0.0f), x, top, palette.border.withAlpha(0.0f), x, bottom, false);
        line.addColour(0.2, palette.border);
        line.addColour(0.8, palette.border);
        g.setGradientFill(line);
        g.fillRect(x, top, 1.0f, bottom - top);
    }

    // The mark: a small wave, slowing down, in the accent colour.
    {
        juce::Path wave;
        const float x0 = float(kPad), y0 = float(kHeaderHeight) * 0.5f;
        for (int i = 0; i <= 40; ++i) {
            const float t = float(i) / 40.0f;
            const float y = y0 + std::sin(juce::MathConstants<float>::twoPi * 2.6f * std::pow(1.0f - t, 1.7f)) * 4.5f * (0.45f + 0.55f * t);
            if (i == 0)
                wave.startNewSubPath(x0, y);
            else
                wave.lineTo(x0 + t * 22.0f, y);
        }
        g.setColour(palette.accent);
        g.strokePath(wave, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour(palette.text);
    g.setFont(uiFont(type::name, true));
    g.drawText("Driftwave", kPad + 30, 0, 80, kHeaderHeight, juce::Justification::centredLeft);
    g.setColour(palette.textDim);
    g.setFont(uiFont(type::label, true));
    g.drawText("FX", kPad + 89, 1, 24, kHeaderHeight, juce::Justification::centredLeft);

    g.setColour(palette.text);
    g.setFont(uiFont(type::section, true));
    g.drawText("Output", outputSection.withHeight(kTitleHeight).withTrimmedLeft(kPad).withTrimmedTop(4), juce::Justification::centredLeft);
}

void EditorContent::resized()
{
    presetBar.setBounds(getWidth() - kPad - 400, (kHeaderHeight - 24) / 2, 400, 24);
    stageButton.setBounds(presetBar.getX() - 96, (kHeaderHeight - 24) / 2, 82, 24);
    colourButton.setBounds(stageButton.getX() - 70, (kHeaderHeight - 24) / 2, 62, 24);

    const int offset = stageOffset();
    stage.setBounds(10, kHeaderHeight + 9, getWidth() - 20, kStageHeight - 9);
    displays.setBounds(0, kHeaderHeight + 1 + offset, getWidth(), kDisplayHeight - 1);

    auto strip = getLocalBounds().withTrimmedTop(kHeaderHeight + offset + kDisplayHeight);
    speedSection = strip.removeFromLeft(kSpeedWidth);
    reverbSection = strip.removeFromLeft(kReverbWidth);
    bassSection = strip.removeFromLeft(kBassWidth);
    outputSection = strip;

    const auto title = [](juce::Rectangle<int> section) { return section.withHeight(kTitleHeight).reduced(kPad, 0).withTrimmedTop(4); };
    const auto body = [](juce::Rectangle<int> section) {
        return section.withTrimmedTop(kTitleHeight).withTrimmedBottom(8).reduced(kPad, 0);
    };
    const int largeH = Knob::heightFor(Knob::large), smallH = Knob::heightFor(Knob::small);
    const int largeW = 100, smallW = 58;
    // Small knobs sit on the same baseline as the large one next to them.
    const auto place = [](Knob& knob, int x, int bottom, int w, int h) { knob.setBounds(x, bottom - h, w, h); };

    // Speed: the knob, then its mode, state and the catch-up button in a column.
    speedOn.setBounds(title(speedSection));
    {
        auto area = body(speedSection);
        const int bottom = area.getY() + (area.getHeight() + largeH) / 2;
        place(speed, area.getX(), bottom, largeW, largeH);
        auto column = juce::Rectangle<int>(area.getX() + largeW + 14, bottom - largeH + Knob::kLabelHeight + 6, 100, 76);
        speedSync.setBounds(column.removeFromTop(18));
        column.removeFromTop(6);
        lag.setBounds(column.removeFromTop(16).withTrimmedLeft(18));
        column.removeFromTop(10);
        catchUp.setBounds(column.removeFromTop(22).withWidth(76));
    }

    // Reverb: the amount, large, then decay and the two tone controls.
    reverbOn.setBounds(title(reverbSection));
    {
        auto area = body(reverbSection);
        const int bottom = area.getY() + (area.getHeight() + largeH) / 2;
        place(mix, area.getX(), bottom, largeW, largeH);
        int x = area.getX() + largeW + 12;
        for (auto* k : { &decay, &lowCut, &highCut }) {
            place(*k, x, bottom, smallW, smallH);
            x += smallW + 4;
        }
    }

    // Bass: three small knobs.
    bassOn.setBounds(title(bassSection));
    {
        auto area = body(bassSection);
        const int bottom = area.getY() + (area.getHeight() + largeH) / 2;
        int x = area.getX();
        for (auto* k : { &bassGain, &bassFreq, &bassDrive }) {
            place(*k, x, bottom, smallW - 2, smallH);
            x += smallW - 2;
        }
    }

    // Output: gain and ceiling, the clip guard switch over them, the meter on the right.
    {
        auto area = body(outputSection);
        const int bottom = area.getY() + (area.getHeight() + largeH) / 2;
        meter.setBounds(area.removeFromRight(46).withTop(bottom - largeH).withBottom(bottom - 2));
        place(outGain, area.getX(), bottom, smallW - 2, smallH);
        place(ceiling, area.getX() + smallW - 2, bottom, smallW - 2, smallH);
        clipOn.setBounds(area.getX() + 4, bottom - largeH + 2, 110, 18);
    }
}

} // namespace driftwave

// ---------------------------------------------------------------------------

DriftwaveEditor::DriftwaveEditor(DriftwaveProcessor& p)
    : AudioProcessorEditor(&p), owner(p), content(p, lookAndFeel.palette)
{
    using driftwave::EditorContent;

    // The accent colour: the one saved with this project, else the one used last anywhere.
    {
        const auto saved = owner.apvts.state.getProperty(driftwave::kAccentProperty).toString();
        const auto fallback = driftwave::ColourStore(driftwave::ColourStore::defaultFile()).lastUsed(lookAndFeel.palette.accent);
        lookAndFeel.setAccent(driftwave::colourFromText(saved, fallback));
    }
    content.onAccentChange = [this](juce::Colour colour) { setAccent(colour); };

    // The content is added first: setting the look and feel then reaches its controls,
    // which rebuild their value boxes in our style.
    addAndMakeVisible(content);
    setLookAndFeel(&lookAndFeel);

    // Resizable at a fixed aspect ratio; the content is drawn at its design size and scaled.
    // Read the saved size first: setting the limits already triggers resized(), which stores a width.
    const int savedWidth = juce::jlimit(EditorContent::kWidth * 3 / 4, EditorContent::kWidth * 2,
                                        int(owner.apvts.state.getProperty(driftwave::kEditorWidthProperty, EditorContent::kWidth)));
    setResizable(true, true);
    applyLayout(savedWidth);
    // Opening or closing the visualiser keeps the width and changes the height.
    content.onLayoutChange = [this] { applyLayout(getWidth()); };
}

void DriftwaveEditor::setAccent(juce::Colour colour)
{
    lookAndFeel.setAccent(colour);
    sendLookAndFeelChange(); // value boxes and menus pick up the new colour
    repaint();
}

void DriftwaveEditor::applyLayout(int width)
{
    using driftwave::EditorContent;
    const int height = content.designHeight();
    const double aspect = double(EditorContent::kWidth) / double(height);
    getConstrainer()->setFixedAspectRatio(0.0); // or the old shape would bend the new limits
    setResizeLimits(EditorContent::kWidth * 3 / 4, height * 3 / 4, EditorContent::kWidth * 2, height * 2);
    getConstrainer()->setFixedAspectRatio(aspect);
    setSize(width, juce::roundToInt(double(width) / aspect));
    resized();
}

DriftwaveEditor::~DriftwaveEditor()
{
    setLookAndFeel(nullptr);
}

void DriftwaveEditor::paint(juce::Graphics& g)
{
    g.fillAll(lookAndFeel.palette.background);
}

void DriftwaveEditor::resized()
{
    const float scale = float(getWidth()) / float(driftwave::EditorContent::kWidth);
    content.setTransform(juce::AffineTransform::scale(scale));
    content.setBounds(0, 0, driftwave::EditorContent::kWidth, content.designHeight());
    // Remembered in the plugin state, so the window reopens at this size with the project.
    owner.apvts.state.setProperty(driftwave::kEditorWidthProperty, getWidth(), nullptr);
}
