#pragma once

#include "dsp/WaveformBuilder.h"
#include "presets/WaveformStyle.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace sonarc {

// The waveform module. Draws the real signal (min/max peaks, RMS, band energy
// and averaged sample values per 8-frame bucket) in one of six shapes:
//  - Classic: per-channel min/max trace
//  - Mirrored: symmetric about the centre, RMS core inside the peak body, glow
//  - Oscilloscope: a continuous line of the signal, triggered on rising zero crossings
//  - Bars: vertical bars, each the peak of one slice of the time window
//  - Filled: solid min/max shape with an outline
//  - Circular: time wrapped around a circle, radius following the level
// Colours come from the active theme (see WaveformStyle::ColourSource).
class WaveformView final : public juce::Component
{
public:
    WaveformView();

    // Clears the history if the rate changes.
    void setSampleRate(std::uint32_t rate);
    void append(const WaveBucket* buckets, std::size_t count);
    void clear();

    void setStyle(const WaveformStyle& newStyle);
    const WaveformStyle& style() const noexcept { return currentStyle; }
    float windowSeconds() const noexcept { return currentStyle.windowSeconds; }

    // Clean mode (overlay / stick / hidden controls) drops labels for a stream-ready look.
    void setCleanMode(bool clean);

    // The active theme changed: rebuild cached images and redraw.
    void themeChanged();

    // Text shown in the corner, e.g. "TEST SIGNAL (simulated)" in the style editor's preview.
    void setBadge(const juce::String& text);

    // Mouse wheel zooms the time window; this reports the new window.
    std::function<void(float)> onWindowChanged;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    using Shape = WaveformStyle::Shape;

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
    float gain(float value) const noexcept { return juce::jlimit(-1.0f, 1.0f, value * currentStyle.amplitude); }
    double bucketsInWindow() const noexcept;
    float effectiveGlow() const noexcept;

    juce::Colour bandColour(const WaveBucket&) const noexcept;
    juce::Colour sourceColour(const WaveBucket&, int channel, float level) const noexcept;
    juce::Colour gradientAt(float t) const noexcept;
    juce::ColourGradient horizontalGradient(juce::Rectangle<float> area, const std::vector<WaveBucket>& columns, float alpha) const;
    void invalidateSpectral() noexcept { spectralDirty = true; }

    void paintClassic(juce::Graphics&, juce::Rectangle<float> area);
    void addSpan(Lane& lane, float x, float width, const WaveBucket& bucket);
    void drawEnvelope(juce::Graphics&, const Lane& lane, float pixelsPerBucket, juce::Colour colour);

    void paintMirrored(juce::Graphics&, juce::Rectangle<float> area, float pixelScale);
    void renderMirroredColumn(int x, std::uint64_t column);

    void paintOscilloscope(juce::Graphics&, juce::Rectangle<float> area);
    void paintBars(juce::Graphics&, juce::Rectangle<float> area, float dt);
    void paintFilled(juce::Graphics&, juce::Rectangle<float> area);
    void paintCircular(juce::Graphics&, juce::Rectangle<float> area, float dt);
    void setFill(juce::Graphics&, const juce::FillType&);
    void strokeWithGlow(juce::Graphics&, const juce::Path&, float thickness);
    void fillSlices(int count, std::vector<WaveBucket>& out) const; // `count` equal slices of the window, oldest first
    void smoothLevels(std::vector<float>& shown, const std::vector<float>& target, std::vector<float>& peaks, float dt) const;

    void renderBackground(float pixelScale);

    std::uint32_t sampleRate = 0;
    std::vector<WaveBucket> history; // ring of the last maxWindowSeconds
    std::uint64_t total = 0;         // buckets ever appended; newest is total - 1

    WaveformStyle currentStyle;
    bool cleanMode = false;
    juce::String badge;

    juce::RectangleList<float> spansA, spansB, clips;
    juce::Path envelope, shapePath, outlinePath;
    juce::FillType currentFill; // last fill set by setFill(), used by strokeWithGlow()
    juce::Image background;
    float backgroundScale = 0.0f;

    // Mirrored: a ring image, one physical-pixel column per slice of time.
    juce::Image spectralImage;
    bool spectralDirty = true;
    float spectralScale = 0.0f;
    std::uint64_t spectralPerColumn = 1;
    std::uint64_t nextSpectralColumn = 0;
    int spectralWriteX = 0;
    std::vector<float> columnPixels; // premultiplied RGBA for one column while compositing lanes

    // Bars / circle: shown levels with temporal smoothing and peak caps, per lane.
    std::vector<float> levelsA, levelsB, peaksA, peaksB, targetA, targetB;
    std::vector<WaveBucket> slices;
    float rotation = 0.0f;
    double lastPaintMs = 0.0;
};

} // namespace sonarc
