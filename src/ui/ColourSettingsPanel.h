#pragma once

#include "ui/Theme.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>
#include <vector>

namespace sonarc {

// "Colours & theme" editor: pick a preset, then customise any colour with the
// built-in picker. Every change is reported straight away so the meters
// update live behind the dialog.
class ColourSettingsPanel final : public juce::Component,
                                  private juce::ChangeListener
{
public:
    // presetName is the preset the colours came from, or "Custom" once edited.
    using ChangeCallback = std::function<void(const theme::Colours&, const juce::String& presetName)>;

    ColourSettingsPanel(const theme::Colours& initial, const juce::String& presetName, ChangeCallback onChange);
    ~ColourSettingsPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Gives the preset box keyboard focus, so up/down arrows flip through presets.
    void focusPresets() { presetBox.grabKeyboardFocus(); }

    static constexpr int preferredWidth = 700;
    static constexpr int preferredHeight = 580;

private:
    using Accessor = std::function<juce::Colour&(theme::Colours&)>;
    class Swatch;
    class SwatchList;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void select(Swatch*);
    void edited();
    void loadPreset(const juce::String& name);
    void refreshFromWorking();

    theme::Colours working;
    juce::String presetName, basePreset;
    ChangeCallback onChange;

    juce::Label presetLabel;
    juce::ComboBox presetBox;
    juce::TextButton resetButton { "Reset to preset" };

    juce::Viewport viewport;
    std::unique_ptr<SwatchList> list;
    Swatch* selected = nullptr;

    juce::Label editingLabel;
    juce::ColourSelector selector { juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders
                                    | juce::ColourSelector::showColourspace | juce::ColourSelector::showAlphaChannel };

    juce::Label fillLabel, glowLabel;
    juce::Slider fillSlider, glowSlider;
};

} // namespace sonarc
