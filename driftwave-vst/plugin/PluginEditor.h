#pragma once

#include "plugin/Displays.h"
#include "plugin/LookAndFeel.h"
#include "plugin/PluginProcessor.h"
#include "plugin/Presets.h"
#include "plugin/Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace driftwave {

/** A rotary control: its name above, the knob, and its value underneath.
    Drag to change, Shift-drag for fine steps, double-click to reset, click the value to type one. */
class Knob final : public juce::Component, private juce::Timer {
public:
    enum Size { small, large };
    static constexpr int kLabelHeight = 14, kValueHeight = 18, kSmallDisc = 44, kLargeDisc = 76;
    static constexpr int heightFor(Size size) { return kLabelHeight + (size == large ? kLargeDisc : kSmallDisc) + kValueHeight; }

    Knob(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& title, Size size = small);
    void resized() override;
    void mouseEnter(const juce::MouseEvent&) override { startTimerHz(60); }
    void mouseExit(const juce::MouseEvent&) override { startTimerHz(60); }

private:
    void timerCallback() override;

    Size size;
    float shown = 0.0f, hover = 0.0f;
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** Stereo output peak meter with a clip light. */
class OutputMeter final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer {
public:
    OutputMeter(DriftwaveProcessor& processor, const Palette& palette);
    void paint(juce::Graphics&) override;
    void visibilityChanged() override { updateTimer(); }
    void parentHierarchyChanged() override { updateTimer(); }
    void mouseDown(const juce::MouseEvent&) override { clipped[0] = clipped[1] = false; }

private:
    void timerCallback() override;
    void updateTimer();

    DriftwaveProcessor& processor;
    const Palette& palette;
    float reductionDb = 0.0f; // what the clip guard is taking off, 0 or negative
    float levelDb[2] { -100.0f, -100.0f };
    float holdDb[2] { -100.0f, -100.0f };
    bool clipped[2] { false, false };
};

/** Shows how far the slowed signal is behind the live input. */
class LagReadout final : public juce::Component, private juce::Timer {
public:
    LagReadout(DriftwaveProcessor& processor, const Palette& palette);
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    DriftwaveProcessor& processor;
    const Palette& palette;
    float shownLag = -1.0f;
    bool shownSync = false;
};

/** Preset picker for the header: a list of factory and user presets, previous / next,
    save the current settings under a name, and delete a user preset. */
class PresetBar final : public juce::Component, private juce::Timer {
public:
    explicit PresetBar(DriftwaveProcessor& processor);
    void resized() override;

private:
    void timerCallback() override;
    void refresh();
    void step(int direction);
    void askForNameAndSave();

    PresetManager manager;
    juce::Array<PresetInfo> presets;
    juce::String shownName;
    juce::ComboBox box;
    juce::TextButton previous { "<" }, next { ">" }, save { "Save" }, remove { "Delete" };
};

/** The whole interface at its design size; the editor scales it to the window. */
class EditorContent final : public juce::Component, private juce::Timer {
public:
    static constexpr int kWidth = 960, kHeight = 328, kStageHeight = 270;

    EditorContent(DriftwaveProcessor& processor, const Palette& palette);
    /** The height of the design: taller while the large visualiser is open. */
    int designHeight() const { return kHeight + stageOffset(); }
    /** Called after the visualiser is opened or closed, so the window can follow. */
    std::function<void()> onLayoutChange;
    /** Called when the user picks another accent colour, so the window can restyle itself. */
    std::function<void(juce::Colour)> onAccentChange;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void updateEnabledLook();
    void timerCallback() override;
    int stageOffset() const { return stage.isVisible() ? kStageHeight : 0; }
    void showColourMenu();
    void showColourPicker();
    void askForNameAndSaveColour();
    /** `remember` also makes it the colour new instances start with. */
    void applyAccent(juce::Colour colour, bool remember);

    /** One thing that fades between two states: a switch's lamp, or a bypassed group dimming. */
    struct Fade {
        juce::Component* component;
        bool lamp;
        float current, target;
    };
    std::vector<Fade> fades;
    void setFade(juce::Component& component, bool lamp, float target);
    bool fadesReady = false;

    const Palette& palette;
    juce::Rectangle<int> speedSection, reverbSection, bassSection, outputSection;

    juce::ToggleButton speedOn { "Speed" }, speedSync { "Sync to song" }, reverbOn { "Reverb" }, bassOn { "Bass" }, clipOn { "Clip guard" };
    std::unique_ptr<ButtonAttachment> speedOnAtt, speedSyncAtt, reverbOnAtt, bassOnAtt, clipOnAtt;

    DriftwaveProcessor& processor;
    PresetBar presetBar;
    juce::TextButton stageButton { "Visualiser" }, colourButton { "Colour" };
    ColourStore colours { ColourStore::defaultFile() };
    StageDisplay stage;
    DisplayBand displays;
    Knob speed;
    LagReadout lag;
    juce::TextButton catchUp { "Catch up" };
    Knob mix, decay, lowCut, highCut;
    Knob bassGain, bassFreq, bassDrive;
    Knob outGain, ceiling;
    OutputMeter meter;
};

} // namespace driftwave

class DriftwaveEditor final : public juce::AudioProcessorEditor {
public:
    explicit DriftwaveEditor(DriftwaveProcessor&);
    ~DriftwaveEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void applyLayout(int width);
    void setAccent(juce::Colour);

    DriftwaveProcessor& owner;
    driftwave::DriftwaveLookAndFeel lookAndFeel;
    driftwave::EditorContent content;
    juce::TooltipWindow tooltips { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DriftwaveEditor)
};
