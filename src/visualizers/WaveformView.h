#pragma once

#include "dsp/WaveformBuilder.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace sonarc {

// Scrolling waveform: newest audio at the right edge. Mouse wheel zooms.
//
// Styles:
//  - Spectral (default): mirrored, coloured by frequency content (lows warm
//    red, mids amber, highs cyan), with a bright RMS core inside the peak
//    envelope and a soft glow. Rendered column by column into an image, so
//    each slice of audio is only drawn once.
//  - Classic: plain min/max envelope per channel.
// Either way each pixel column covers the true min/max of its samples, so
// short peaks never disappear when zoomed out.
class WaveformView final : public juce::Component
{
public:
    enum class StereoMode { Stacked, Overlaid, Mid };
    enum class Style { Spectral, Classic };

    static constexpr float minWindowSeconds = 0.02f;
    static constexpr float maxWindowSeconds = 10.0f;

    WaveformView();

    // Clears the history if the rate changes.
    void setSampleRate(std::uint32_t rate);
    void append(const WaveBucket* buckets, std::size_t count);
    void clear();

    void setWindowSeconds(float seconds);
    float windowSeconds() const noexcept { return window; }

    void setStereoMode(StereoMode mode);
    StereoMode stereoMode() const noexcept { return mode; }

    void setStyle(Style newStyle);
    Style style() const noexcept { return currentStyle; }

    void setThickness(float pixels);
    float thickness() const noexcept { return minSpanHeight; }

    // Clean mode (overlay / stick / hidden controls) drops labels for a stream-ready look.
    void setCleanMode(bool clean);

    // The active theme changed: rebuild cached images and redraw.
    void themeChanged();

    std::function<void(float)> onWindowChanged; // after a mouse-wheel zoom

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct Lane
    {
        juce::Rectangle<float> area;
        int channel; // 0 = left, 1 = right, 2 = mid
        juce::RectangleList<float>* spans;

        // Previous column's extent, so consecutive columns always join up.
        float previousTop = 0.0f, previousBottom = 0.0f;
        bool hasPrevious = false;
    };

    juce::Rectangle<float> plotArea() const noexcept;
    std::vector<Lane> lanes();
    bool aggregate(std::uint64_t start, std::uint64_t end, WaveBucket& result) const noexcept;
    float yFor(const Lane& lane, float value) const noexcept;
    juce::Colour bandColour(const WaveBucket&) const noexcept;
    void invalidateSpectral() noexcept { spectralDirty = true; }

    void paintClassic(juce::Graphics&, juce::Rectangle<float> area);
    void addSpan(Lane& lane, float x, float width, const WaveBucket& bucket);
    void drawEnvelope(juce::Graphics&, const Lane& lane, float pixelsPerBucket, juce::Colour colour);

    void paintSpectral(juce::Graphics&, juce::Rectangle<float> area, float pixelScale);
    void renderSpectralColumn(int x, std::uint64_t column);

    void renderBackground(float pixelScale);

    std::uint32_t sampleRate = 0;
    std::vector<WaveBucket> history; // ring of the last maxWindowSeconds
    std::uint64_t total = 0;         // buckets ever appended; newest is total - 1

    float window = 2.0f;
    StereoMode mode = StereoMode::Stacked;
    Style currentStyle = Style::Spectral;
    float minSpanHeight = 1.5f;
    bool cleanMode = false;

    juce::RectangleList<float> spansA, spansB, clips;
    juce::Path envelope;
    juce::Image background;
    float backgroundScale = 0.0f;

    // Spectral style: a ring image, one physical-pixel column per slice of time.
    juce::Image spectralImage;
    bool spectralDirty = true;
    float spectralScale = 0.0f;
    std::uint64_t spectralPerColumn = 1;
    std::uint64_t nextSpectralColumn = 0;
    int spectralWriteX = 0;
};

} // namespace sonarc
