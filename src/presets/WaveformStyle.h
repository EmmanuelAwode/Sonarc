#pragma once

// How the waveform looks. Plain data (no JUCE) so presets can store it and
// tests can check it. Colours are *not* stored here: `colour` only says which
// colours of the active theme to use, so presets never override the user's
// colour preferences.
namespace sonarc {

struct WaveformStyle
{
    enum class Shape { Classic, Mirrored, Oscilloscope, Bars, Filled, Circular };
    enum class ColourSource { SpectralBands, Channels, SpectrumGradient, Accent };
    enum class Direction { NewestRight, NewestLeft };

    Shape shape = Shape::Mirrored;
    ColourSource colour = ColourSource::SpectralBands;
    float lineThickness = 1.5f;    // pixels
    float glow = 1.0f;             // 0 = none, 1 = normal, 2 = strong (also scaled by the theme's glow)
    float amplitude = 1.0f;        // display gain
    float smoothing = 0.3f;        // 0 = raw, 1 = very smooth
    float windowSeconds = 4.0f;    // time shown across the view ("scrolling speed")
    float animationSpeed = 1.0f;   // bar/peak fall speed, circle rotation
    float stereoSeparation = 0.0f; // 0 = mono (mid), 1 = left and right fully apart
    bool peakHold = false;
    float fillOpacity = 0.75f;
    Direction direction = Direction::NewestRight;
    int barCount = 64;

    bool operator==(const WaveformStyle&) const = default;

    // Each adjustable setting, and which shapes use it (the editor only shows those).
    enum class Setting
    {
        LineThickness, Glow, Amplitude, Smoothing, Window, AnimationSpeed,
        StereoSeparation, PeakHold, FillOpacity, Direction, BarCount, Colour
    };

    static constexpr bool uses(Shape shape, Setting setting) noexcept
    {
        switch (setting)
        {
            case Setting::Glow:
            case Setting::Amplitude:
            case Setting::Window:
            case Setting::StereoSeparation:
            case Setting::Colour:
                return true;
            case Setting::LineThickness:
                return shape != Shape::Bars;
            case Setting::Smoothing:
                return shape == Shape::Oscilloscope || shape == Shape::Bars || shape == Shape::Filled || shape == Shape::Circular;
            case Setting::AnimationSpeed:
                return shape == Shape::Bars || shape == Shape::Circular;
            case Setting::PeakHold:
                return shape == Shape::Bars || shape == Shape::Circular;
            case Setting::FillOpacity:
                return shape == Shape::Mirrored || shape == Shape::Bars || shape == Shape::Filled || shape == Shape::Circular;
            case Setting::Direction:
                return shape != Shape::Circular;
            case Setting::BarCount:
                return shape == Shape::Bars;
        }
        return false;
    }

    // Valid ranges (the editor's sliders and preset validation both use these).
    struct Range { float min, max; };
    static constexpr Range lineThicknessRange { 0.5f, 8.0f };
    static constexpr Range glowRange { 0.0f, 2.0f };
    static constexpr Range amplitudeRange { 0.25f, 4.0f };
    static constexpr Range smoothingRange { 0.0f, 1.0f };
    static constexpr Range windowRange { 0.02f, 10.0f };
    static constexpr Range animationSpeedRange { 0.0f, 3.0f };
    static constexpr Range stereoSeparationRange { 0.0f, 1.0f };
    static constexpr Range fillOpacityRange { 0.0f, 1.0f };
    static constexpr int minBarCount = 8, maxBarCount = 256;
};

} // namespace sonarc
