#pragma once

#include "audio/AudioEngine.h"
#include "settings/AppSettings.h"
#include "visualizers/SpectrogramView.h"
#include "visualizers/SpectrumView.h"
#include "visualizers/StereoPeakMeterView.h"
#include "visualizers/WaveformView.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace sonarc {

class MainWindow;

// A compact control strip, the spectrum and/or waveform, a stereo peak meter
// A compact control strip, any combination of spectrum, waveform and
// spectrogram, a stereo peak meter and a status line. Keys: H controls,
// T pin on top, O overlay, S stick to screen edge, F freeze, V cycle view.
// Right-click for all options.
class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    MainComponent(AudioEngine& audioEngine, AppSettings& appSettings);
    ~MainComponent() override;

    enum class ViewMode { Spectrum, Waveform, Spectrogram, SpectrumWave, SpectrumSpectrogram, All, Count };

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    // Called by MainWindow when its frame / pin state changes.
    void overlayModeChanged(bool isOverlay);
    void windowStateChanged();

private:
    void timerCallback() override;
    void onVBlank();

    MainWindow* window() const;
    void showContextMenu();
    void toggleControls();
    void setViewMode(ViewMode);
    void setFrozen(bool);
    void drainWaveform();

    void refreshDeviceList();
    void refreshDeviceListIfChanged();
    void rebuildDeviceBox();
    void openSelectedDevice();
    void updateStatus();
    void setupSlider(juce::Slider&, double min, double max, double step, double value,
                     std::function<juce::String(double)> format);

    AudioEngine& engine;
    AppSettings& settings;

    juce::ComboBox deviceBox;
    juce::ComboBox fftBox;
    juce::ComboBox viewBox;
    juce::Slider smoothingSlider, decaySlider, tiltSlider;
    juce::ToggleButton peakHoldButton { "Peak hold" };
    juce::ToggleButton pinButton { "On top" };
    juce::TextButton overlayButton { "Overlay" };
    juce::TextButton stickButton { "Stick" };
    juce::Label statusLabel;
    SpectrumView spectrumView;
    StereoPeakMeterView meterView;
    WaveformView waveformView;
    SpectrogramView spectrogramView;
    juce::TooltipWindow tooltips { this, 600 };

    std::vector<DeviceSelection> deviceChoices; // ComboBox item id N maps to deviceChoices[N - 1]
    std::vector<AudioDeviceInfo> knownDevices;
    int timerTicks = 0;
    AnalysisSnapshot snapshot;
    CaptureState lastState = CaptureState::Stopped;
    bool controlsVisible = true;
    ViewMode viewMode = ViewMode::Spectrum;
    bool frozen = false;
    std::vector<WaveBucket> waveformScratch = std::vector<WaveBucket>(1 << 14);
    std::vector<SpectrogramRow> spectrogramScratch = std::vector<SpectrogramRow>(64);
    static constexpr double kMaxFramesPerSecond = 60.0;
    double lastFrameMs = 0.0;

    juce::VBlankAttachment vblank { this, [this] { onVBlank(); } };
};

} // namespace sonarc
