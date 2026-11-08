#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Sonarc's palette: near-black graphite with a cool cyan accent that warms
// through amber to coral as levels approach full scale.
namespace sonarc::theme {

inline const juce::Colour background { 0xff0b0d10 };
inline const juce::Colour panel      { 0xff12151a };
inline const juce::Colour raised     { 0xff1a1e25 };
inline const juce::Colour outline    { 0xff262b34 };
inline const juce::Colour grid       { 0x14ffffff };
inline const juce::Colour gridStrong { 0x24ffffff };
inline const juce::Colour textDim    { 0xff6b7484 };
inline const juce::Colour text       { 0xffc5ccd6 };
inline const juce::Colour textBright { 0xffeef2f6 };

inline const juce::Colour accent     { 0xff3ad0e0 }; // cyan
inline const juce::Colour accentDeep { 0xff1f7fd8 }; // blue
inline const juce::Colour warn       { 0xffffb648 }; // amber
inline const juce::Colour hot        { 0xffff5a5f }; // coral red
inline const juce::Colour ok         { 0xff48d597 }; // green

inline juce::Font font(float height, bool bold = false)
{
    auto options = juce::FontOptions(height);
    if (bold)
        options = options.withStyle("Bold");
    return juce::Font(options);
}

} // namespace sonarc::theme
