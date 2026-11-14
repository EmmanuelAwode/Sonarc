#pragma once

#include "dsp/Fft.h"
#include "plugin/LookAndFeel.h"
#include "plugin/PluginProcessor.h"

#include <vector>

namespace driftwave {

/** Base for the three displays: a dark well with a small caption. */
class DisplayWell : public juce::Component {
public:
    DisplayWell(DriftwaveProcessor& p, const Palette& pal) : processor(p), palette(pal) { setInterceptsMouseClicks(false, false); }

    /** Called 30 times a second (60 for the large visualiser) while the interface is on screen. */
    virtual void advance() = 0;

protected:
    juce::Rectangle<float> paintWell(juce::Graphics&) const;
    void caption(juce::Graphics&, juce::Rectangle<float> area, const juce::String& left, const juce::String& right,
                 juce::Colour rightColour) const;
    float param(const char* paramId) const { return processor.apvts.getRawParameterValue(paramId)->load(); }

    DriftwaveProcessor& processor;
    const Palette& palette;
};

/** Speed: the output waveform scrolling past, the pitch change, and how far behind the song it is. */
class ScopeDisplay final : public DisplayWell {
public:
    ScopeDisplay(DriftwaveProcessor&, const Palette&);
    void advance() override;
    void paint(juce::Graphics&) override;
    /** New output samples from the shared feed (the band reads the feed once and hands the block to both users). */
    void feed(const float* samples, int count, double sampleRate);

private:
    static constexpr int kColumns = 220;
    static constexpr double kSecondsShown = 4.0;
    float low[kColumns] {}, high[kColumns] {};
    int head = 0;
    float accLow = 0.0f, accHigh = 0.0f;
    double accCount = 0.0;
    float lag = 0.0f;
};

/** Reverb: the shape of the tail the controls ask for, lit by the reverb actually sounding. */
class TailDisplay final : public DisplayWell {
public:
    TailDisplay(DriftwaveProcessor&, const Palette&);
    void advance() override;
    void paint(juce::Graphics&) override;

private:
    float level = 0.0f;                    // smoothed measured reverb level, 0..1
    float shownDecay = 3.0f, shownMix = 0.4f;
};

/** Bass and output: spectrum of the input and the output, with the bass shelf's response on top. */
class SpectrumDisplay final : public DisplayWell {
public:
    SpectrumDisplay(DriftwaveProcessor&, const Palette&);
    void advance() override;
    void paint(juce::Graphics&) override;
    void feed(const float* input, int inputCount, const float* output, int outputCount);

private:
    static constexpr int kFftSize = 2048, kPoints = 96;
    static constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f, kTopDb = 6.0f, kBottomDb = -84.0f;

    void analyse(const std::vector<float>& signal, float* smoothed);
    float xFor(float hz, juce::Rectangle<float> area) const;
    juce::Path curve(const float* values, juce::Rectangle<float> area) const;

    Fft fft;
    std::vector<float> window, inRing, outRing, re, im;
    float inDb[kPoints], outDb[kPoints];
    float shownBass = 0.0f, shownFreq = 90.0f, shownDrive = 0.0f;
    bool haveData = false;
};

/** The large visualiser, made for recording videos. Four styles (click it to change):
    bars, mirrored bars, a circle, and ridges flowing back in time. In all of them the
    spectrum is the plugin's output, the blue-grey trail lingers for as long as the reverb
    does, and the waveform moves at the chosen speed. Everything drawn comes from the
    audio or from a control. Runs at 60 frames a second. */
class StageDisplay final : public DisplayWell {
public:
    StageDisplay(DriftwaveProcessor&, const Palette&);
    void advance() override;
    void paint(juce::Graphics&) override;
    void feed(const float* samples, int count, double sampleRate);
    void mouseDown(const juce::MouseEvent&) override;

    enum Style { barsStyle, mirrorStyle, circleStyle, ridgesStyle, styleCount };

private:
    static constexpr int kFftSize = 2048, kBars = 64, kColumns = 480, kRidges = 18;
    static constexpr float kMinHz = 30.0f, kMaxHz = 16000.0f, kRangeDb = 54.0f;

    void paintRibbon(juce::Graphics&, juce::Rectangle<float> area, float strength) const;
    void paintBars(juce::Graphics&, juce::Rectangle<float> area, bool mirrored) const;
    void paintCircle(juce::Graphics&, juce::Rectangle<float> area) const;
    void paintRidges(juce::Graphics&, juce::Rectangle<float> area) const;
    juce::Colour barColour(int bar) const;
    float trailStrength() const;

    int style = barsStyle;
    float ridges[kRidges][kBars] {}; // earlier spectra, newest first
    float ridgeClock = 0.0f, bassLevel = 0.0f;

    Fft fft;
    std::vector<float> window, ring, re, im;
    float bars[kBars] {}, trail[kBars] {};
    float low[kColumns] {}, high[kColumns] {};
    int head = 0;
    float accLow = 0.0f, accHigh = 0.0f;
    double accCount = 0.0;
    bool haveData = false;
};

/** The row of displays above the controls. Owns the one timer that drives them,
    which stops while the interface is not on screen. */
class DisplayBand final : public juce::Component, private juce::Timer {
public:
    DisplayBand(DriftwaveProcessor&, const Palette&);
    ~DisplayBand() override;
    void resized() override;
    void visibilityChanged() override { updateTimer(); }
    void parentHierarchyChanged() override { updateTimer(); }

    void setSectionWidths(int speedWidth, int reverbWidth) { speedW = speedWidth; reverbW = reverbWidth; resized(); }
    /** One display update, outside the timer (for rendering the interface without a host). */
    void refreshNow() { oddFrame = false; timerCallback(); }
    /** The large visualiser lives elsewhere in the window but is fed and timed from here. */
    void setStage(StageDisplay* display) { stage = display; }

private:
    void timerCallback() override;
    void updateTimer();

    DriftwaveProcessor& processor;
    ScopeDisplay scope;
    TailDisplay tail;
    SpectrumDisplay spectrum;
    StageDisplay* stage = nullptr;
    bool oddFrame = false;
    std::vector<float> inBlock, outBlock;
    int speedW = 250, reverbW = 320;
};

} // namespace driftwave
