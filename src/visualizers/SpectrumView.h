#pragma once

#include "dsp/AnalysisSnapshot.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace sonarc {

// Draws the FFT spectrum on a logarithmic 20 Hz - 20 kHz axis.
class SpectrumView final : public juce::Component
{
public:
    SpectrumView();

    void setSpectrum(const AnalysisSnapshot& snapshot);
    void clearSpectrum();

    // Display-only slope (dB/octave, pivot at 1 kHz) so a typical mix looks
    // flat. Hover readouts always report the unweighted level.
    void setTilt(float dbPerOctave);
    void setShowPeakHold(bool shouldShow);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    struct Column
    {
        float frequency;
        float binLow, binCentre, binHigh;
        float tiltDb;
    };

    void rebuildColumns();
    float levelAt(const std::vector<float>& values, const Column& column) const noexcept;
    float xForFrequency(float hz) const noexcept;
    float yForDb(float db) const noexcept;
    juce::Rectangle<float> plotArea() const noexcept;
    void drawGrid(juce::Graphics&, juce::Rectangle<float> area) const;
    void drawReadout(juce::Graphics&, juce::Rectangle<float> area) const;
    void renderBackground(float pixelScale);

    static constexpr float minHz = 20.0f, maxHz = 20000.0f;
    static constexpr float minDb = -90.0f, maxDb = 6.0f;

    std::vector<float> spectrum, peaks;
    std::uint32_t sampleRate = 0;
    int fftSize = 0;
    float tiltDbPerOctave = 4.5f;
    bool showPeakHold = true;

    std::vector<Column> columns;
    juce::Path fillPath, linePath, peakPath;

    // Grid, labels and info text only change on resize / settings changes, so
    // they are rendered once into an image instead of every frame.
    juce::Image background;
    float backgroundScale = 0.0f;

    bool hovering = false;
    float hoverX = 0.0f;
};

} // namespace sonarc
