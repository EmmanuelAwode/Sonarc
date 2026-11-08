#pragma once

#include "audio/AudioTypes.h"
#include "dsp/MagnitudeSpectrum.h"

#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

namespace sonarc {

struct SpectrumBallistics
{
    float smoothing = 0.6f;             // 0 = none, towards 1 = heavy time averaging
    float decayDbPerSecond = 60.0f;     // maximum fall speed of the displayed curve
    float peakHoldSeconds = 1.5f;       // 0 disables peak hold
    float peakDecayDbPerSecond = 15.0f; // fall speed of the peak line after the hold time
};

// FFT spectrum of the mid signal (L + R) / 2.
//
// Levels are calibrated so a full-scale sine centred on a bin reads 0 dBFS.
// A Hann window is used, so a sine that falls between two bins can read up to
// 1.42 dB low (scalloping loss); that is normal for analysers of this kind.
class SpectrumAnalyzer
{
public:
    void prepare(std::uint32_t sampleRate, int fftSize);
    void setBallistics(const SpectrumBallistics& newBallistics) noexcept { ballistics = newBallistics; }

    // Appends audio to the analysis history. Never allocates.
    void pushSamples(const StereoFrame* frames, std::size_t count) noexcept;

    // Analyses the most recent fftSize() samples. `elapsedSeconds` is how much
    // audio time has passed since the previous call; it drives the ballistics.
    void analyze(double elapsedSeconds) noexcept;

    void resetPeakHold() noexcept;

    int fftSize() const noexcept { return size; }
    std::uint32_t sampleRate() const noexcept { return rate; }
    int binCount() const noexcept { return size / 2 + 1; }

    const std::vector<float>& instantaneousDb() const noexcept { return rawDb; }
    const std::vector<float>& displayDb() const noexcept { return shownDb; }
    const std::vector<float>& peakHoldDb() const noexcept { return peakDb; }

private:
    std::uint32_t rate = 0;
    int size = 0;
    std::size_t mask = 0;
    SpectrumBallistics ballistics;

    MagnitudeSpectrum magnitude;
    std::vector<float> history;
    std::size_t writePosition = 0;

    std::vector<float> rawDb, averagedDb, shownDb, peakDb, peakHoldRemaining;
};

} // namespace sonarc
