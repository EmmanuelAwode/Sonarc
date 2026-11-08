#pragma once

#include "dsp/WaveformBuilder.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace sonarc {

// Scrolling waveform: newest audio at the right edge. Each pixel column shows
// the true min/max of the samples it covers, so peaks are never lost when
// zoomed out. Mouse wheel zooms the time window.
class WaveformView final : public juce::Component
{
public:
    enum class StereoMode { Stacked, Overlaid, Mid };

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

    void setThickness(float pixels);
    float thickness() const noexcept { return minSpanHeight; }

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
    void addSpan(Lane& lane, float x, float width, const WaveBucket& bucket);
    void drawEnvelope(juce::Graphics&, const Lane& lane, float pixelsPerBucket, juce::Colour colour);
    float yFor(const Lane& lane, float value) const noexcept;
    void renderBackground(float pixelScale);

    std::uint32_t sampleRate = 0;
    std::vector<WaveBucket> history; // ring of the last maxWindowSeconds
    std::uint64_t total = 0;         // buckets ever appended; newest is total - 1

    float window = 2.0f;
    StereoMode mode = StereoMode::Stacked;
    float minSpanHeight = 1.5f;

    juce::RectangleList<float> spansA, spansB, clips;
    juce::Path envelope;
    juce::Image background;
    float backgroundScale = 0.0f;
};

} // namespace sonarc
