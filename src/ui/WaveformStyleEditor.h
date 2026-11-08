#pragma once

#include "dsp/WaveformBuilder.h"
#include "presets/WaveformPresets.h"
#include "visualizers/WaveformView.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>
#include <vector>

namespace sonarc {

// Design waveform looks and manage presets, with a live preview. Every change
// is applied to the main waveform straight away through the Host.
class WaveformStyleEditor final : public juce::Component,
                                  private juce::Timer
{
public:
    struct Host
    {
        virtual ~Host() = default;
        // `modified` = the style differs from what is saved in `presetName`.
        virtual void waveformStyleChanged(const WaveformStyle&, const juce::String& presetName, bool modified) = 0;
        virtual void openColourEditor() = 0;
    };

    WaveformStyleEditor(WaveformPresetLibrary& library, Host& host,
                        const WaveformStyle& current, const juce::String& presetName);
    ~WaveformStyleEditor() override;

    // Live audio for the preview (called by the main window as it receives it).
    void feedLive(const WaveBucket* buckets, std::size_t count, std::uint32_t sampleRate);

    // The style was changed outside the editor (mouse-wheel zoom, preset menu).
    void externalChange(const WaveformStyle& style, const juce::String& presetName);

    void themeChanged();

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int preferredWidth = 860;
    static constexpr int preferredHeight = 660;

private:
    using Setting = WaveformStyle::Setting;

    struct Row
    {
        Setting setting;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ToggleButton> toggle;
        std::unique_ptr<juce::ComboBox> choice;
        std::unique_ptr<juce::TextButton> reset;
    };

    void timerCallback() override;

    void addRow(Setting, const juce::String& label);
    float valueOf(const WaveformStyle&, Setting) const;
    void setValue(WaveformStyle&, Setting, float) const;
    void layoutRows();
    void refreshControls();
    void edited();
    bool isModified() const;
    const WaveformPreset* currentPreset() const;

    void refreshPresetBox();
    void selectPreset(const juce::String& name);
    void save();
    void saveAs();
    void renamePreset();
    void duplicatePreset();
    void deletePreset();
    void importPresets();
    void exportPresets();

    void askForName(const juce::String& title, const juce::String& initial, std::function<void(juce::String)> onOk);
    void showMessage(const juce::String& title, const juce::String& message);

    void generateTestSignal(double seconds);
    void updatePreviewSource();

    WaveformPresetLibrary& library;
    Host& host;
    WaveformStyle working, saved;
    juce::String presetName;

    juce::ComboBox presetBox;
    std::vector<juce::String> presetIds; // ComboBox item id N -> presetIds[N - 1]
    juce::ToggleButton favouriteButton { "Favourite" };
    juce::TextButton saveButton { "Save" }, saveAsButton { "Save As..." }, renameButton { "Rename..." },
        duplicateButton { "Duplicate" }, deleteButton { "Delete" }, importButton { "Import..." },
        exportButton { "Export..." }, defaultButton { "Restore default" };

    WaveformView preview;
    juce::Label previewLabel;
    juce::ComboBox previewSource;

    juce::Label shapeLabel;
    juce::ComboBox shapeBox;
    juce::TextButton coloursButton { "Edit colours..." }, resetAllButton { "Reset style" };
    juce::Label settingsHeading;
    std::vector<Row> rows;
    juce::Rectangle<int> rowsArea;

    std::unique_ptr<juce::FileChooser> fileChooser;

    // Preview feed.
    enum class Source { Auto, Live, Test };
    bool showingTest = false;
    double lastLiveSoundMs = 0.0;
    std::uint32_t liveRate = 0;
    WaveformBuilder testBuilder;
    std::vector<StereoFrame> testFrames;
    std::vector<WaveBucket> testBuckets;
    double testTime = 0.0;
    double lastTimerMs = 0.0;
};

} // namespace sonarc
