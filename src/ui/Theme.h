#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

// Sonarc's colours. These are the *active* theme: read them while painting.
// They are only changed (on the UI thread) through theme::apply(), after
// which MainComponent tells every view to redraw its caches.
namespace sonarc::theme {

// ---- Window, panels and controls ------------------------------------------------
inline juce::Colour background { 0xff0b0d10 };
inline juce::Colour panel      { 0xff12151a };
inline juce::Colour raised     { 0xff1a1e25 }; // controls, meter tracks
inline juce::Colour outline    { 0xff262b34 };
inline juce::Colour grid       { 0x14ffffff };
inline juce::Colour gridStrong { 0x24ffffff };
inline juce::Colour textDim    { 0xff6b7484 }; // axis labels
inline juce::Colour text       { 0xffc5ccd6 };
inline juce::Colour textBright { 0xffeef2f6 };
inline juce::Colour accent     { 0xff3ad0e0 }; // toggles, highlights, classic mid trace
inline juce::Colour accentDeep { 0xff1f7fd8 };

// ---- Fixed status colours (not themed: they carry meaning) -------------------------
inline const juce::Colour warn { 0xffffb648 };
inline const juce::Colour hot  { 0xffff5a5f };
inline const juce::Colour ok   { 0xff48d597 };

// ---- Visualisers -------------------------------------------------------------------
// Spectrum colour by frequency at 20 Hz, 90 Hz, 400 Hz, 2 kHz, 6 kHz and 20 kHz.
inline constexpr std::array<float, 6> kSpectrumStopHz { 20.0f, 90.0f, 400.0f, 2000.0f, 6000.0f, 20000.0f };
inline std::array<juce::Colour, 6> spectrumStops { juce::Colour(0xff6b4bff), juce::Colour(0xff2f7bff), juce::Colour(0xff22d3ee),
                                                   juce::Colour(0xff3ee6a8), juce::Colour(0xffffd166), juce::Colour(0xffff7a59) };
inline float spectrumFill = 0.55f; // opacity of the area under the curve
inline float glow = 1.0f;          // 0 = no glow, 1 = default, 2 = strong
inline juce::Colour peakHold { 0x59eef2f6 };

// Waveform: spectral style colours by band, classic style colours by channel.
inline juce::Colour waveLow   { 0xffff3d6b };
inline juce::Colour waveMid   { 0xffffb82e };
inline juce::Colour waveHigh  { 0xff59e6ff };
inline juce::Colour waveLeft  { 0xff3ad0e0 };
inline juce::Colour waveRight { 0xffffb648 };

// Meters, bottom to top: floor, -30 dB, -12 dB, -6 dB, 0 dB.
inline std::array<juce::Colour, 5> meterStops { juce::Colour(0xff1f7fd8), juce::Colour(0xff3ad0e0), juce::Colour(0xff48d597),
                                                juce::Colour(0xffffb648), juce::Colour(0xffff5a5f) };

//==============================================================================
// Everything a user can customise, as one value (for presets and settings).
struct Colours
{
    juce::Colour background, panel, controls, grid, labels, text, accent;
    std::array<juce::Colour, 6> spectrum;
    float spectrumFill = 0.55f, glow = 1.0f;
    juce::Colour peakHold;
    juce::Colour waveLow, waveMid, waveHigh, waveLeft, waveRight;
    std::array<juce::Colour, 5> meter;

    bool operator==(const Colours&) const = default;
};

struct Preset
{
    juce::String name;
    Colours colours;
};

const std::vector<Preset>& presets();      // the first one is the default
Colours presetNamed(const juce::String& name); // falls back to the default

Colours current();
void apply(const Colours&);

juce::var toVar(const Colours&);
Colours fromVar(const juce::var&, const Colours& fallback);

inline juce::Font font(float height, bool bold = false)
{
    auto options = juce::FontOptions(height);
    if (bold)
        options = options.withStyle("Bold");
    return juce::Font(options);
}

} // namespace sonarc::theme
