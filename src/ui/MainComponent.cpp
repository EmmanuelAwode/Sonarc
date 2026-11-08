#include "ui/MainComponent.h"
#include "ui/ColourSettingsPanel.h"
#include "ui/MainWindow.h"
#include "ui/SonarcLookAndFeel.h"
#include "ui/Theme.h"

namespace sonarc {

namespace {

constexpr int kControlBarHeight = 38;
constexpr int kStatusBarHeight = 20;

using ViewMode = MainComponent::ViewMode;

struct ViewInfo
{
    const char* key;   // stored in settings
    const char* label; // shown in the View box and menu
    bool spectrum, waveform, spectrogram;
};

// Indexed by ViewMode.
constexpr ViewInfo kViews[] = {
    { "spectrum",             "Spectrum",                 true,  false, false },
    { "waveform",             "Waveform",                 false, true,  false },
    { "spectrogram",          "Spectrogram",              false, false, true  },
    { "both",                 "Spectrum + Wave",          true,  true,  false },
    { "spectrum+spectrogram", "Spectrum + Spectrogram",   true,  false, true  },
    { "all",                  "Wave + Spectrum + Sgram",  true,  true,  true  },
};
static_assert(std::size(kViews) == (size_t) ViewMode::Count);

const ViewInfo& viewInfo(ViewMode mode) { return kViews[(size_t) mode]; }

struct PaletteInfo
{
    const char* key;
    const char* label;
    SpectrogramView::Palette palette;
};

constexpr PaletteInfo kPalettes[] = {
    { "theme",     "Theme colours",                SpectrogramView::Palette::Theme },
    { "sonarc",    "Sonarc (blue - cyan - amber)", SpectrogramView::Palette::Sonarc },
    { "ember",     "Ember (purple - orange)",      SpectrogramView::Palette::Ember },
    { "ice",       "Ice (blue - white)",           SpectrogramView::Palette::Ice },
    { "grayscale", "Grayscale",                    SpectrogramView::Palette::Grayscale },
};


juce::String encodingName(const StreamFormat& format)
{
    switch (format.encoding)
    {
        case SampleEncoding::Float32:     return "32-bit float";
        case SampleEncoding::Int16:       return "16-bit";
        case SampleEncoding::Int24Packed: return "24-bit";
        case SampleEncoding::Int32:       return juce::String(format.validBits) + "-bit";
        case SampleEncoding::Unknown:     break;
    }
    return "unknown format";
}

} // namespace

MainComponent::MainComponent(AudioEngine& audioEngine, AppSettings& appSettings)
    : engine(audioEngine), settings(appSettings)
{
    setOpaque(true);
    setWantsKeyboardFocus(true);

    // Device picker
    deviceBox.setTooltip("Audio source. Playback devices are captured via WASAPI loopback (what you hear).");
    deviceBox.onChange = [this] { openSelectedDevice(); };
    addAndMakeVisible(deviceBox);

    // FFT size
    for (int size : { 1024, 2048, 4096, 8192 })
        fftBox.addItem("FFT " + juce::String(size), size);
    fftBox.setSelectedId(settings.fftSize(), juce::dontSendNotification);
    if (fftBox.getSelectedId() == 0)
        fftBox.setSelectedId(4096, juce::dontSendNotification);
    fftBox.setTooltip("FFT size: larger = finer low-frequency detail, slower response");
    fftBox.onChange = [this]
    {
        engine.setFftSize(fftBox.getSelectedId());
        settings.setFftSize(fftBox.getSelectedId());
    };
    addAndMakeVisible(fftBox);

    setupSlider(smoothingSlider, 0.0, 0.95, 0.01, settings.spectrumSmoothing(),
                [](double v) { return "Smooth " + juce::String(v, 2); });
    smoothingSlider.setTooltip("Time averaging of the spectrum");
    smoothingSlider.onValueChange = [this]
    {
        engine.setSpectrumSmoothing((float) smoothingSlider.getValue());
        settings.setSpectrumSmoothing((float) smoothingSlider.getValue());
    };

    setupSlider(decaySlider, 6.0, 240.0, 1.0, settings.spectrumDecay(),
                [](double v) { return "Decay " + juce::String(juce::roundToInt(v)) + " dB/s"; });
    decaySlider.setSkewFactorFromMidPoint(60.0);
    decaySlider.setTooltip("Maximum fall speed of the spectrum curve");
    decaySlider.onValueChange = [this]
    {
        engine.setSpectrumDecay((float) decaySlider.getValue());
        settings.setSpectrumDecay((float) decaySlider.getValue());
    };

    setupSlider(tiltSlider, 0.0, 6.0, 0.5, settings.spectrumTilt(),
                [](double v) { return "Tilt " + juce::String(v, 1) + " dB/oct"; });
    tiltSlider.setTooltip("Display slope around 1 kHz (4.5 dB/oct makes typical mixes look flat). Readouts stay unweighted.");
    tiltSlider.onValueChange = [this]
    {
        spectrumView.setTilt((float) tiltSlider.getValue());
        spectrogramView.setTilt((float) tiltSlider.getValue());
        settings.setSpectrumTilt((float) tiltSlider.getValue());
    };

    peakHoldButton.setToggleState(settings.peakHoldEnabled(), juce::dontSendNotification);
    peakHoldButton.onClick = [this]
    {
        const bool on = peakHoldButton.getToggleState();
        engine.setPeakHoldSeconds(on ? 1.5f : 0.0f);
        spectrumView.setShowPeakHold(on);
        settings.setPeakHoldEnabled(on);
    };
    addAndMakeVisible(peakHoldButton);

    pinButton.setTooltip("Keep Sonarc above all other windows (T)");
    pinButton.onClick = [this]
    {
        if (auto* w = window())
            w->setPinnedOnTop(pinButton.getToggleState());
    };
    addAndMakeVisible(pinButton);

    overlayButton.setTooltip("Borderless overlay: drag anywhere to move, double-click or Esc to exit (O)");
    overlayButton.onClick = [this]
    {
        if (auto* w = window())
            w->setOverlayMode(true);
    };
    addAndMakeVisible(overlayButton);

    stickButton.setTooltip("Dock to the top of the screen as a toolbar that other windows can't cover (S)");
    stickButton.onClick = [this]
    {
        if (auto* w = window())
            w->setStickEdge(w->isStuck() ? MainWindow::StickEdge::None : MainWindow::StickEdge::Top);
    };
    addAndMakeVisible(stickButton);

    // View selector
    for (size_t i = 0; i < std::size(kViews); ++i)
        viewBox.addItem(kViews[i].label, (int) i + 1);
    viewBox.setTooltip("What to show (V cycles)");
    viewBox.onChange = [this] { setViewMode((ViewMode) (viewBox.getSelectedId() - 1)); };
    addAndMakeVisible(viewBox);

    // Waveform: the selected preset and the current (possibly edited) style.
    {
        WaveformStyle current;
        juce::String presetName = settings.waveformPresetName();
        juce::String error;
        if (waveformPresets::styleFromVar(settings.waveformCurrentStyle(), current, error))
        {
            // Restored exactly as it was left (including unsaved edits).
        }
        else if (const auto* named = presetLibrary.find(presetName))
        {
            current = named->style;
        }
        else
        {
            // First run with presets: carry over the earlier waveform settings.
            const bool classic = settings.waveformStyle() == "classic";
            const auto* base = presetLibrary.find(classic ? "Classic" : waveformPresets::defaultPreset().name);
            presetName = base->name;
            current = base->style;
            const auto stereo = settings.waveformStereoMode();
            current.stereoSeparation = stereo == "mid" ? 0.0f : stereo == "overlaid" ? 0.15f : 1.0f;
            current.windowSeconds = juce::jlimit(WaveformStyle::windowRange.min, WaveformStyle::windowRange.max, settings.waveformWindowSeconds());
            current.lineThickness = juce::jlimit(WaveformStyle::lineThicknessRange.min, WaveformStyle::lineThicknessRange.max, settings.waveformThickness());
        }
        if (presetLibrary.find(presetName) == nullptr)
            presetName = waveformPresets::defaultPreset().name;
        applyWaveformStyle(current, presetName, false);
    }
    waveformView.onWindowChanged = [this](float) { applyWaveformStyle(waveformView.style(), waveformPresetName, false); };
    addChildComponent(waveformView);

    // BPM and key modules
    showTempo = settings.showTempoModule();
    showKey = settings.showKeyModule();
    tempoRangeMinimum = settings.tempoRangeMinimum();
    engine.setTempoFoldRange(tempoRangeMinimum);
    tempoView.setBeatPulse(settings.beatPulse());
    tempoView.onRangeShift = [this](float factor) { setTempoRange(tempoRangeMinimum * factor); };
    tempoView.onContextMenu = [this] { showContextMenu(); };
    keyView.onContextMenu = [this] { showContextMenu(); };
    addChildComponent(tempoView);
    addChildComponent(keyView);
    updateModules();

    // Spectrogram
    spectrogramView.setSpeed(settings.spectrogramSpeed());
    spectrogramView.setRangeDb(settings.spectrogramRangeDb());
    spectrogramView.setFftSize(settings.spectrogramFftSize());
    engine.setSpectrogramFftSize(settings.spectrogramFftSize());
    for (const auto& p : kPalettes)
        if (settings.spectrogramPalette() == p.key)
            spectrogramView.setPalette(p.palette);
    addChildComponent(spectrogramView);

    // Receive the visualisers' mouse events too, for dragging and the context menu.
    spectrumView.addMouseListener(this, false);
    waveformView.addMouseListener(this, false);
    spectrogramView.addMouseListener(this, false);

    statusLabel.setFont(theme::font(11.5f));
    statusLabel.setColour(juce::Label::textColourId, theme::textDim);
    statusLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(statusLabel);

    meterView.onResetRequested = [this] { engine.resetPeaks(); };
    addAndMakeVisible(spectrumView);
    addAndMakeVisible(meterView);

    // Push saved settings into the engine and views.
    engine.setFftSize(fftBox.getSelectedId());
    engine.setSpectrumSmoothing((float) smoothingSlider.getValue());
    engine.setSpectrumDecay((float) decaySlider.getValue());
    engine.setPeakHoldSeconds(peakHoldButton.getToggleState() ? 1.5f : 0.0f);
    spectrumView.setTilt((float) tiltSlider.getValue());
    spectrogramView.setTilt((float) tiltSlider.getValue());
    spectrumView.setShowPeakHold(peakHoldButton.getToggleState());

    auto initialView = ViewMode::Spectrum;
    for (size_t i = 0; i < std::size(kViews); ++i)
        if (settings.viewMode() == kViews[i].key)
            initialView = (ViewMode) i;
    setViewMode(initialView);

    const auto saved = settings.deviceSelection();
    refreshDeviceList();
    engine.open(saved);
    refreshDeviceList(); // reflect the opened selection in the combo box
    updateStatus();

    startTimerHz(4);
}

MainComponent::~MainComponent()
{
    stopTimer();
}

void MainComponent::setupSlider(juce::Slider& slider, double min, double max, double step, double value,
                                std::function<juce::String(double)> format)
{
    slider.setSliderStyle(juce::Slider::LinearBar);
    slider.setTextBoxStyle(juce::Slider::TextBoxLeft, true, 0, 0); // read-only text drawn over the bar
    slider.setRange(min, max, step);
    slider.textFromValueFunction = std::move(format);
    slider.setValue(value, juce::dontSendNotification);
    slider.setDoubleClickReturnValue(true, value);
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour(juce::Slider::trackColourId, juce::Colours::transparentBlack);
    slider.updateText();
    addAndMakeVisible(slider);
}

void MainComponent::refreshDeviceList()
{
    knownDevices = engine.listDevices();
    rebuildDeviceBox();
}

// Apps start and stop playing all the time, so the list is re-checked every few
// seconds and rebuilt only when it changed (and never while it is open).
void MainComponent::refreshDeviceListIfChanged()
{
    if (deviceBox.isPopupActive())
        return;

    auto devices = engine.listDevices();
    if (devices != knownDevices)
    {
        knownDevices = std::move(devices);
        rebuildDeviceBox();
    }
}

void MainComponent::rebuildDeviceBox()
{
    const auto& devices = knownDevices;
    const auto& current = engine.selection();

    deviceChoices.clear();
    deviceBox.clear(juce::dontSendNotification);

    int selectedId = 0;
    auto addChoice = [&](const juce::String& label, DeviceSelection selection)
    {
        deviceChoices.push_back(std::move(selection));
        const int id = (int) deviceChoices.size();
        deviceBox.addItem(label, id);
        if (deviceChoices.back() == current)
            selectedId = id;
    };

    auto addSection = [&](DeviceKind kind, const juce::String& heading, const juce::String& defaultLabel)
    {
        deviceBox.addSectionHeading(heading);
        addChoice(defaultLabel, { kind, {} });
        for (const auto& device : devices)
            if (device.kind == kind)
                addChoice(juce::String::fromUTF8(device.name.c_str()) + (device.isDefault ? "  (default)" : ""),
                          { kind, device.id });
    };

    addSection(DeviceKind::PlaybackLoopback, "Playback - loopback (what you hear)", "System default output");
    addSection(DeviceKind::RecordingInput, "Recording inputs", "System default input");

    // One entry per program that has an audio session; captures only that program.
    deviceBox.addSectionHeading("Applications (only that app's audio)");
    bool currentAppListed = false;
    for (const auto& device : devices)
    {
        if (device.kind != DeviceKind::Application)
            continue;

        DeviceSelection selection { DeviceKind::Application, {}, device.processName, device.processId };
        currentAppListed = currentAppListed || selection == current;
        addChoice(juce::String::fromUTF8(device.name.c_str()) + (device.isPlaying ? "  - playing" : ""), selection);
    }

    // Keep a saved app selectable while it isn't running or hasn't played anything yet.
    if (current.kind == DeviceKind::Application && ! currentAppListed && ! current.processName.empty())
        addChoice(juce::String(current.processName.c_str()) + "  (no audio yet)", current);

    if (selectedId != 0)
        deviceBox.setSelectedId(selectedId, juce::dontSendNotification);
    else
        deviceBox.setText("Saved device (disconnected)", juce::dontSendNotification);
}

void MainComponent::openSelectedDevice()
{
    const int id = deviceBox.getSelectedId();
    if (id <= 0 || id > (int) deviceChoices.size())
        return;

    const auto selection = deviceChoices[(size_t) id - 1];
    engine.open(selection);
    settings.setDeviceSelection(selection);
    updateStatus();
}

void MainComponent::updateStatus()
{
    const auto status = engine.status();
    const auto deviceName = juce::String::fromUTF8(status.deviceName.c_str());
    const auto kind = engine.selection().kind;
    const char* captureType = kind == DeviceKind::PlaybackLoopback ? " loopback"
                            : kind == DeviceKind::Application      ? " per-app capture"
                                                                   : " input";

    juce::String text;
    juce::Colour colour = theme::textDim;

    switch (status.state)
    {
        case CaptureState::Running:
        {
            const auto& f = status.format;
            text << deviceName << "   |   " << juce::String(f.sampleRate / 1000.0, 1) << " kHz   |   "
                 << (int) f.channels << " ch   |   " << encodingName(f) << "   |   "
                 << engine.backendName() << captureType;
            if (f.channels > 2)
                text << "   |   showing front L/R";
            if (snapshot.droppedFrames > 0)
                text << "   |   dropped " << juce::String((juce::int64) snapshot.droppedFrames) << " frames";
            break;
        }
        case CaptureState::DeviceLost:
            text << (deviceName.isNotEmpty() ? deviceName + ": " : juce::String())
                 << juce::String::fromUTF8(status.message.c_str())
                 << (kind == DeviceKind::Application ? "  - will reconnect when it runs again"
                                                     : "  - waiting for the device to come back");
            colour = theme::warn;
            break;
        case CaptureState::Failed:
            text << (deviceName.isNotEmpty() ? deviceName + ": " : juce::String())
                 << juce::String::fromUTF8(status.message.c_str());
            colour = theme::hot;
            break;
        case CaptureState::Stopped:
            text = "Stopped";
            break;
    }

    statusLabel.setText(text, juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, colour);

    if (status.state != CaptureState::Running && lastState == CaptureState::Running)
    {
        spectrumView.clearSpectrum();
        meterView.clearLevels();
        waveformView.clear();
        spectrogramView.clear();
        tempoView.clearReading();
        keyView.clearReading();
    }
    lastState = status.state;
}

void MainComponent::timerCallback()
{
    if (engine.service())
        refreshDeviceList();
    else if (++timerTicks % 12 == 0) // every 3 s
        refreshDeviceListIfChanged();

    updateStatus();
    drainWaveform(); // VBlank stops while minimised; don't let the peak ring fill with stale audio
    settings.saveIfNeeded();
}

void MainComponent::onVBlank()
{
    if (lastState != CaptureState::Running)
        return;

    // VBlank follows the monitor (often 120-240 Hz); cap the redraw rate.
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now - lastFrameMs < 1000.0 / kMaxFramesPerSecond - 2.0)
        return;
    lastFrameMs = now;

    if (engine.readSnapshot(snapshot) && ! frozen)
    {
        spectrumView.setSpectrum(snapshot);
        meterView.setLevels(snapshot.left, snapshot.right);
        if (showTempo)
            tempoView.setReading(snapshot.tempo, snapshot.analysisTimeMs);
        if (showKey)
            keyView.setReading(snapshot.key);
    }
    if (showTempo)
        tempoView.tick(); // beat pulse

    drainWaveform();
}

void MainComponent::drainWaveform()
{
    waveformView.setSampleRate(snapshot.sampleRate);
    auto* editor = styleEditor();
    while (const auto n = engine.readWaveform(waveformScratch.data(), waveformScratch.size()))
    {
        if (! frozen)
            waveformView.append(waveformScratch.data(), n);
        if (editor != nullptr)
            editor->feedLive(waveformScratch.data(), n, snapshot.sampleRate);
    }

    if (snapshot.sampleRate > 0)
        spectrogramView.setRowRate((double) snapshot.sampleRate / (double) SpectrogramAnalyzer::kHopFrames);
    while (const auto n = engine.readSpectrogram(spectrogramScratch.data(), spectrogramScratch.size()))
        if (! frozen)
            spectrogramView.appendRows(spectrogramScratch.data(), n);
}

void MainComponent::setViewMode(ViewMode mode)
{
    viewMode = mode;
    viewBox.setSelectedId((int) mode + 1, juce::dontSendNotification);
    settings.setViewMode(viewInfo(mode).key);
    engine.setSpectrogramEnabled(viewInfo(mode).spectrogram); // only analyse what is shown
    resized();
}

void MainComponent::showColourPanel()
{
    if (colourDialog != nullptr)
    {
        colourDialog->toFront(true);
        return;
    }

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new ColourSettingsPanel(theme::current(), settings.themePreset(),
                                                     [this](const theme::Colours& colours, const juce::String& preset)
                                                     {
                                                         applyTheme(colours, preset);
                                                     }));
    options.dialogTitle = "Sonarc - Colours & theme";
    options.dialogBackgroundColour = theme::background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = this;

    colourDialog = options.launchAsync();
    if (colourDialog != nullptr)
    {
        // Sonarc is often pinned on top; the dialog must not open behind it.
        colourDialog->setAlwaysOnTop(true);
        colourDialog->toFront(true);
        if (auto* panel = dynamic_cast<ColourSettingsPanel*>(colourDialog->getContentComponent()))
            panel->focusPresets();
    }
}

void MainComponent::applyTheme(const theme::Colours& colours, const juce::String& presetName)
{
    theme::apply(colours);
    settings.setTheme(theme::toVar(colours));
    settings.setThemePreset(presetName);

    if (auto* sonarcLookAndFeel = dynamic_cast<SonarcLookAndFeel*>(&getLookAndFeel()))
        sonarcLookAndFeel->refreshColours();

    spectrumView.themeChanged();
    waveformView.themeChanged();
    spectrogramView.themeChanged();
    meterView.themeChanged();
    tempoView.themeChanged();
    keyView.themeChanged();
    if (auto* editor = styleEditor())
        editor->themeChanged();
    if (styleDialog != nullptr)
        styleDialog->setBackgroundColour(theme::background);

    if (auto* w = window())
    {
        w->setBackgroundColour(theme::background);
        w->sendLookAndFeelChange();
    }
    if (colourDialog != nullptr)
        colourDialog->setBackgroundColour(theme::background);
    updateStatus();
    repaint();
}

WaveformStyleEditor* MainComponent::styleEditor() const
{
    return styleDialog != nullptr ? dynamic_cast<WaveformStyleEditor*>(styleDialog->getContentComponent()) : nullptr;
}

void MainComponent::showStyleEditor()
{
    if (styleDialog != nullptr)
    {
        styleDialog->toFront(true);
        return;
    }

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new WaveformStyleEditor(presetLibrary, *this, waveformStyle, waveformPresetName));
    options.dialogTitle = "Sonarc - Waveform Style Editor";
    options.dialogBackgroundColour = theme::background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = this;

    styleDialog = options.launchAsync();
    if (styleDialog != nullptr)
    {
        styleDialog->setAlwaysOnTop(true); // Sonarc is often pinned on top
        styleDialog->toFront(true);
    }
}

void MainComponent::applyWaveformStyle(const WaveformStyle& style, const juce::String& presetName, bool fromEditor)
{
    waveformStyle = style;
    waveformPresetName = presetName;
    waveformView.setStyle(style); // switching never touches audio capture
    settings.setWaveformPresetName(presetName);
    settings.setWaveformCurrentStyle(waveformPresets::styleToVar(style));

    if (! fromEditor)
        if (auto* editor = styleEditor())
            editor->externalChange(style, presetName);
}

void MainComponent::waveformStyleChanged(const WaveformStyle& style, const juce::String& presetName, bool)
{
    applyWaveformStyle(style, presetName, true);
}

void MainComponent::updateModules()
{
    // Detection only runs while its module is shown.
    engine.setTempoEnabled(showTempo);
    engine.setKeyEnabled(showKey);
    settings.setShowTempoModule(showTempo);
    settings.setShowKeyModule(showKey);
    if (! showTempo)
        tempoView.clearReading();
    if (! showKey)
        keyView.clearReading();
    resized();
}

void MainComponent::setTempoRange(float minimumBpm)
{
    tempoRangeMinimum = juce::jlimit(40.0f, 160.0f, minimumBpm);
    engine.setTempoFoldRange(tempoRangeMinimum);
    settings.setTempoRangeMinimum(tempoRangeMinimum);
}

void MainComponent::setFrozen(bool shouldFreeze)
{
    frozen = shouldFreeze;
    repaint();
}

void MainComponent::paintOverChildren(juce::Graphics& g)
{
    if (! frozen)
        return;

    auto target = spectrumView.getBounds();
    if (waveformView.isVisible())
        target = waveformView.getBounds();
    else if (spectrogramView.isVisible() && ! spectrumView.isVisible())
        target = spectrogramView.getBounds();
    const auto badge = juce::Rectangle<float>(0, 0, 118, 20).withCentre({ (float) target.getCentreX(), (float) target.getY() + 16.0f });
    g.setColour(theme::raised.withAlpha(0.92f));
    g.fillRoundedRectangle(badge, 4.0f);
    g.setColour(theme::warn);
    g.setFont(theme::font(11.5f, true));
    g.drawText("FROZEN  (F)", badge, juce::Justification::centred, false);
}

MainWindow* MainComponent::window() const
{
    return findParentComponentOfClass<MainWindow>();
}

void MainComponent::toggleControls()
{
    controlsVisible = ! controlsVisible;
    resized();
    repaint();
}

void MainComponent::overlayModeChanged(bool isOverlay)
{
    // Overlay mode is meant to be a clean view; controls can still be shown with H.
    controlsVisible = ! isOverlay;
    resized();
    repaint();
    windowStateChanged();
}

void MainComponent::windowStateChanged()
{
    if (auto* w = window())
    {
        pinButton.setToggleState(w->isPinnedOnTop(), juce::dontSendNotification);
        stickButton.setToggleState(w->isStuck(), juce::dontSendNotification);
    }
}

void MainComponent::showContextMenu()
{
    auto* w = window();
    if (w == nullptr)
        return;

    juce::PopupMenu opacityMenu;
    for (int percent : { 100, 90, 75, 60, 45, 30 })
        opacityMenu.addItem(juce::String(percent) + "%", true, juce::roundToInt(w->windowOpacity() * 100.0f) == percent,
                            [w, percent] { w->setWindowOpacity((float) percent / 100.0f); });

    juce::PopupMenu viewMenu;
    for (size_t i = 0; i < std::size(kViews); ++i)
        viewMenu.addItem(kViews[i].label, true, viewMode == (ViewMode) i, [this, i] { setViewMode((ViewMode) i); });

    juce::PopupMenu sgramMenu;
    for (float speed : { 15.0f, 30.0f, 60.0f, 120.0f, 240.0f })
        sgramMenu.addItem("Speed " + juce::String(juce::roundToInt(speed)) + " px/s", true,
                          std::abs(spectrogramView.speed() - speed) < 0.5f, [this, speed]
                          {
                              spectrogramView.setSpeed(speed);
                              settings.setSpectrogramSpeed(speed);
                          });
    sgramMenu.addSeparator();
    for (int size : { 1024, 2048, 4096, 8192 })
        sgramMenu.addItem("Resolution: FFT " + juce::String(size), true, settings.spectrogramFftSize() == size, [this, size]
                          {
                              engine.setSpectrogramFftSize(size);
                              spectrogramView.setFftSize(size);
                              settings.setSpectrogramFftSize(size);
                          });
    sgramMenu.addSeparator();
    for (const auto& p : kPalettes)
        sgramMenu.addItem(p.label, true, spectrogramView.palette() == p.palette, [this, p]
                          {
                              spectrogramView.setPalette(p.palette);
                              settings.setSpectrogramPalette(p.key);
                          });
    sgramMenu.addSeparator();
    for (float rangeDb : { 60.0f, 90.0f, 120.0f })
        sgramMenu.addItem("Range " + juce::String(juce::roundToInt(rangeDb)) + " dB", true,
                          std::abs(spectrogramView.rangeDb() - rangeDb) < 0.5f, [this, rangeDb]
                          {
                              spectrogramView.setRangeDb(rangeDb);
                              settings.setSpectrogramRangeDb(rangeDb);
                          });

    juce::PopupMenu waveMenu;
    {
        const auto all = presetLibrary.all();
        auto addPreset = [&](const WaveformPreset& p)
        {
            waveMenu.addItem((p.favourite ? "* " : "") + p.name, true, p.name == waveformPresetName,
                             [this, name = p.name, style = p.style] { applyWaveformStyle(style, name, false); });
        };
        bool anyFavourite = false;
        for (const auto& p : all)
            anyFavourite = anyFavourite || p.favourite;
        if (anyFavourite)
        {
            waveMenu.addSectionHeader("Favourites");
            for (const auto& p : all)
                if (p.favourite)
                    addPreset(p);
        }
        waveMenu.addSectionHeader("Presets");
        for (const auto& p : all)
            if (! p.favourite)
                addPreset(p);
        waveMenu.addSeparator();
        waveMenu.addItem("Waveform Style Editor...\tW", [this] { showStyleEditor(); });
        waveMenu.addItem("Restore default waveform", [this]
        {
            const auto& d = waveformPresets::defaultPreset();
            applyWaveformStyle(d.style, d.name, false);
        });
    }

    juce::PopupMenu modulesMenu;
    modulesMenu.addItem("BPM", true, showTempo, [this] { showTempo = ! showTempo; updateModules(); });
    modulesMenu.addItem("Key", true, showKey, [this] { showKey = ! showKey; updateModules(); });
    modulesMenu.addItem("Beat pulse animation", showTempo, settings.beatPulse(), [this]
    {
        settings.setBeatPulse(! settings.beatPulse());
        tempoView.setBeatPulse(settings.beatPulse());
    });
    modulesMenu.addSeparator();
    modulesMenu.addSectionHeader("BPM range (half / double time)");
    for (float minimum : { 60.0f, 70.0f, 85.0f, 100.0f })
        modulesMenu.addItem(juce::String(juce::roundToInt(minimum)) + " - " + juce::String(juce::roundToInt(minimum * 2.0f)) + " BPM",
                            showTempo, std::abs(tempoRangeMinimum - minimum) < 0.5f, [this, minimum] { setTempoRange(minimum); });
    modulesMenu.addItem("Tap tempo\tB", showTempo, false, [this] { tempoView.tap(); });

    juce::PopupMenu themeMenu;
    for (const auto& preset : theme::presets())
        themeMenu.addItem(preset.name, true, settings.themePreset() == preset.name,
                          [this, name = preset.name] { applyTheme(theme::presetNamed(name), name); });
    themeMenu.addSeparator();
    themeMenu.addItem("Customise colours...\tC", [this] { showColourPanel(); });

    juce::PopupMenu menu;
    menu.addSubMenu("View", viewMenu);
    menu.addSubMenu("Waveform", waveMenu);
    menu.addSubMenu("Spectrogram", sgramMenu);
    menu.addSubMenu("Modules (BPM / Key)", modulesMenu);
    menu.addSubMenu("Theme", themeMenu);
    menu.addItem("Colours & theme...\tC", [this] { showColourPanel(); });
    menu.addItem("Freeze display\tF", true, frozen, [this] { setFrozen(! frozen); });
    menu.addSeparator();
    juce::PopupMenu stickMenu;
    using Edge = MainWindow::StickEdge;
    stickMenu.addItem("Off", true, w->stickEdge() == Edge::None, [w] { w->setStickEdge(Edge::None); });
    stickMenu.addItem("Top of screen\tS", true, w->stickEdge() == Edge::Top, [w] { w->setStickEdge(Edge::Top); });
    stickMenu.addItem("Bottom of screen", true, w->stickEdge() == Edge::Bottom, [w] { w->setStickEdge(Edge::Bottom); });
    stickMenu.addSeparator();
    for (int height : { 60, 80, 110, 140, 180, 240 })
        stickMenu.addItem("Bar height " + juce::String(height) + " px", true, w->stickHeight() == height,
                          [w, height] { w->setStickHeight(height); });
    menu.addSubMenu("Stick to screen edge", stickMenu);
    menu.addItem("Always on top\tT", true, w->isPinnedOnTop(), [w] { w->setPinnedOnTop(! w->isPinnedOnTop()); });
    menu.addItem("Overlay mode (no frame)\tO", true, w->isOverlayMode(), [w] { w->setOverlayMode(! w->isOverlayMode()); });
    menu.addItem("Show controls\tH", true, controlsVisible, [this] { toggleControls(); });
    menu.addSubMenu("Opacity", opacityMenu);
    menu.addSeparator();
    menu.addItem("Reset peaks", [this] { engine.resetPeaks(); });
    menu.addSeparator();
    menu.addItem("Quit Sonarc", [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });

    menu.showMenuAsync(juce::PopupMenu::Options());
}

void MainComponent::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showContextMenu();
        return;
    }

    if (auto* w = window(); w != nullptr && w->isOverlayMode())
        w->beginDrag(e);
}

void MainComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    if (auto* w = window(); w != nullptr && w->isOverlayMode())
        w->continueDrag(e);
}

void MainComponent::mouseDoubleClick(const juce::MouseEvent&)
{
    if (auto* w = window(); w != nullptr && w->isOverlayMode())
        w->setOverlayMode(false);
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    auto* w = window();
    const auto c = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());

    if (c == 'h')
    {
        toggleControls();
        return true;
    }
    if (c == 'c')
    {
        showColourPanel();
        return true;
    }
    if (c == 'f')
    {
        setFrozen(! frozen);
        return true;
    }
    if (c == 'w')
    {
        showStyleEditor();
        return true;
    }
    if (c == 'b' && showTempo)
    {
        tempoView.tap();
        return true;
    }
    if (c == 'v')
    {
        setViewMode((ViewMode) (((int) viewMode + 1) % (int) ViewMode::Count));
        return true;
    }
    if (c == 's' && w != nullptr)
    {
        w->setStickEdge(w->isStuck() ? MainWindow::StickEdge::None : MainWindow::StickEdge::Top);
        return true;
    }
    if (c == 't' && w != nullptr)
    {
        w->setPinnedOnTop(! w->isPinnedOnTop());
        return true;
    }
    if (c == 'o' && w != nullptr)
    {
        w->setOverlayMode(! w->isOverlayMode());
        return true;
    }
    if (key == juce::KeyPress::escapeKey && w != nullptr && w->isOverlayMode())
    {
        w->setOverlayMode(false);
        return true;
    }
    return false;
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(theme::background);

    if (controlsVisible)
    {
        g.setColour(theme::textBright);
        g.setFont(theme::font(14.0f, true));
        g.drawText("SONARC", juce::Rectangle<int>(12, 0, 70, kControlBarHeight), juce::Justification::centredLeft, false);
    }
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();

    for (auto* c : std::initializer_list<juce::Component*> { &deviceBox, &fftBox, &smoothingSlider, &decaySlider,
                                                             &tiltSlider, &peakHoldButton, &pinButton, &overlayButton, &stickButton, &viewBox,
                                                             &statusLabel })
        c->setVisible(controlsVisible);

    if (controlsVisible)
    {
        auto bar = bounds.removeFromTop(kControlBarHeight).reduced(8, 6);
        bar.removeFromLeft(74); // wordmark

        juce::FlexBox flex;
        flex.alignItems = juce::FlexBox::AlignItems::stretch;
        auto item = [](juce::Component& c, float width, float minWidth)
        {
            return juce::FlexItem(c).withWidth(width).withMinWidth(minWidth).withMargin({ 0, 6, 0, 0 });
        };
        flex.items.add(item(deviceBox, 280.0f, 120.0f).withFlex(1.0f, 1.0f).withMaxWidth(340.0f));
        flex.items.add(item(viewBox, 128.0f, 96.0f));
        flex.items.add(item(fftBox, 96.0f, 80.0f));
        flex.items.add(item(smoothingSlider, 112.0f, 90.0f));
        flex.items.add(item(decaySlider, 128.0f, 100.0f));
        flex.items.add(item(tiltSlider, 128.0f, 100.0f));
        flex.items.add(item(peakHoldButton, 96.0f, 90.0f));
        flex.items.add(item(pinButton, 84.0f, 80.0f));
        flex.items.add(item(overlayButton, 72.0f, 64.0f));
        flex.items.add(item(stickButton, 60.0f, 52.0f));
        flex.performLayout(bar);

        statusLabel.setBounds(bounds.removeFromBottom(kStatusBarHeight).reduced(6, 0));
    }

    // With the controls hidden (overlay, stick, H) the visualisers drop their labels for a clean stream look.
    spectrumView.setCleanMode(! controlsVisible);
    waveformView.setCleanMode(! controlsVisible);
    spectrogramView.setCleanMode(! controlsVisible);

    const int margin = controlsVisible ? 6 : 3;
    bounds.reduce(margin, controlsVisible ? 0 : margin);
    meterView.setBounds(bounds.removeFromRight(juce::jmin(StereoPeakMeterView::preferredWidth, bounds.getWidth() / 3)));
    bounds.removeFromRight(margin);

    // [panes] [BPM] [Key] [meters]
    keyView.setVisible(showKey);
    tempoView.setVisible(showTempo);
    tempoView.setCleanMode(! controlsVisible);
    keyView.setCleanMode(! controlsVisible);
    if (showKey)
    {
        keyView.setBounds(bounds.removeFromRight(KeyView::preferredWidth));
        bounds.removeFromRight(margin);
    }
    if (showTempo)
    {
        tempoView.setBounds(bounds.removeFromRight(TempoView::preferredWidth));
        bounds.removeFromRight(margin);
    }

    // Visible panes in display order (left to right, or top to bottom) with relative sizes.
    const auto& info = viewInfo(viewMode);
    std::vector<std::pair<juce::Component*, float>> panes;
    if (info.waveform)    panes.push_back({ &waveformView, 0.8f });
    if (info.spectrum)    panes.push_back({ &spectrumView, 1.0f });
    if (info.spectrogram) panes.push_back({ &spectrogramView, 1.0f });

    for (auto* c : std::initializer_list<juce::Component*> { &waveformView, &spectrumView, &spectrogramView })
        c->setVisible(false);

    // Side by side in a wide strip, stacked when the window is tall.
    const bool sideBySide = bounds.getWidth() > bounds.getHeight() * (int) panes.size();
    float totalWeight = 0.0f;
    for (const auto& pane : panes)
        totalWeight += pane.second;

    const int available = (sideBySide ? bounds.getWidth() : bounds.getHeight()) - margin * ((int) panes.size() - 1);
    for (size_t i = 0; i < panes.size(); ++i)
    {
        auto* component = panes[i].first;
        const bool last = i + 1 == panes.size();
        const int extent = last ? (sideBySide ? bounds.getWidth() : bounds.getHeight())
                                : (int) ((float) available * panes[i].second / totalWeight);
        component->setBounds(sideBySide ? bounds.removeFromLeft(extent) : bounds.removeFromTop(extent));
        component->setVisible(true);
        if (! last)
            sideBySide ? bounds.removeFromLeft(margin) : bounds.removeFromTop(margin);
    }
}

} // namespace sonarc
