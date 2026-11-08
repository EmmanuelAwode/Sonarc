#pragma once

#include "dsp/SpectrogramAnalyzer.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <vector>

namespace sonarc {

// Scrolling spectrogram: time runs left to right (newest at the right edge),
// frequency bottom (20 Hz) to top (20 kHz) on a log axis, colour = level.
class SpectrogramView final : public juce::Component
{
public:
    enum class Palette { Sonarc, Ember, Ice, Grayscale };

    SpectrogramView();

    // Rows per second of incoming audio; history is cleared when it changes.
    void setRowRate(double rowsPerSecond);
    void appendRows(const SpectrogramRow* rows, std::size_t count);
    void clear();

    void setSpeed(float pixelsPerSecond);
    float speed() const noexcept { return pixelsPerSecond; }

    void setPalette(Palette);
    Palette palette() const noexcept { return currentPalette; }

    void setRangeDb(float decibels); // colour scale covers 0 dBFS down to -range
    float rangeDb() const noexcept { return range; }

    void setTilt(float dbPerOctave); // same display slope as the spectrum, pivot 1 kHz
    void setFftSize(int size);       // shown in the info text
    void setCleanMode(bool clean);   // stream look: no info text

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using Column = std::array<float, kSpectrogramBands>;

    struct PixelRow
    {
        int firstBand, lastBand; // max over this range, or -1 to interpolate at `position`
        float position;
        float tiltDb;
    };

    juce::Rectangle<int> plotArea() const noexcept;
    void pushColumn(const Column&);
    void drawColumn(int x, const Column&);
    void rebuildImage(float pixelScale);
    void rebuildPalette();
    void renderOverlay(float pixelScale);

    static constexpr std::size_t kMaxColumns = 4096;

    double rowRate = 0.0;
    float pixelsPerSecond = 60.0f;
    Palette currentPalette = Palette::Sonarc;
    float range = 90.0f;
    float tilt = 4.5f;
    int fftSize = 0;
    bool cleanMode = false;

    std::vector<Column> history; // the last kMaxColumns columns
    std::uint64_t columnCount = 0;
    Column pending {};
    bool hasPending = false;
    double columnPhase = 0.0;

    // The image is a ring: column `writeX` is the oldest, `writeX - 1` the newest.
    juce::Image image;
    int writeX = 0;
    float imageScale = 0.0f;
    std::vector<PixelRow> pixelRows;
    std::array<juce::PixelARGB, 256> colours {};

    juce::Image overlay; // axis labels and info text
    float overlayScale = 0.0f;
};

} // namespace sonarc
