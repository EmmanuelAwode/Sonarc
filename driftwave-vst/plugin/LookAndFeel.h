#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace driftwave {

/** The whole interface draws from these few colours. The accent is used
    sparingly: values that are set, switches that are on, and warnings. */
struct Palette {
    juce::Colour background { 0xff17191c }; // window, wells
    juce::Colour surface { 0xff22252a };    // the control strip
    juce::Colour elevated { 0xff2b2f35 };   // knobs, buttons
    juce::Colour border { 0xff383d44 };     // dividers, outlines, empty tracks
    juce::Colour text { 0xffe5e7eb };
    juce::Colour textDim { 0xff8b929c };
    juce::Colour accent { 0xffd4a65a };     // muted amber by default; the user can change it
    juce::Colour cool { 0xff8fb3c4 };       // the accent's quiet companion, used for the reverb's displays
    juce::Colour signal { 0xffb5bec9 };     // meters
    juce::Colour clip { 0xffc8553d };
};

/** Text sizes used across the interface (px). */
namespace type {
inline constexpr float name = 15.0f, section = 12.0f, label = 11.0f, value = 13.0f, small = 10.5f;
}

juce::Font uiFont(float height, bool bold = false);

class DriftwaveLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    DriftwaveLookAndFeel();

    Palette palette;
    /** Changes the accent colour (and its companion) everywhere. Components need a
        sendLookAndFeelChange() and a repaint afterwards. */
    void setAccent(juce::Colour accent);

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                          float startAngle, float endAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;

    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return uiFont(type::label); }

    void drawComboBox(juce::Graphics&, int width, int height, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override { return uiFont(type::section); }
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override { return uiFont(type::section); }

private:
    void applyColours();
};

} // namespace driftwave
