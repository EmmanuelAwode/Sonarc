#include "dsp/SpectrumAnalyzer.h"
#include "dsp/Decibels.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sonarc {

void SpectrumAnalyzer::prepare(std::uint32_t sampleRate, int fftSize)
{
    rate = sampleRate;
    size = fftSize;
    mask = static_cast<std::size_t>(fftSize - 1);
    fft = std::make_unique<RealFft>(fftSize);

    const auto n = static_cast<std::size_t>(fftSize);
    history.assign(n, 0.0f);
    writePosition = 0;
    frame.assign(n, 0.0f);

    // Periodic Hann window; its coherent gain (mean value) is 0.5.
    window.resize(n);
    double windowSum = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        window[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n)));
        windowSum += window[i];
    }

    // One-sided amplitude scaling: a sine of amplitude A appears as A at its bin.
    binScale = static_cast<float>(2.0 / windowSum);

    const auto binsNeeded = n / 2 + 1;
    bins.assign(binsNeeded, {});
    rawDb.assign(binsNeeded, kSilenceDb);
    averagedDb.assign(binsNeeded, kSilenceDb);
    shownDb.assign(binsNeeded, kSilenceDb);
    peakDb.assign(binsNeeded, kSilenceDb);
    peakHoldRemaining.assign(binsNeeded, 0.0f);
}

void SpectrumAnalyzer::pushSamples(const StereoFrame* frames, std::size_t count) noexcept
{
    if (size == 0)
        return;

    for (std::size_t i = 0; i < count; ++i)
    {
        history[writePosition] = 0.5f * (frames[i].left + frames[i].right);
        writePosition = (writePosition + 1) & mask;
    }
}

void SpectrumAnalyzer::analyze(double elapsedSeconds) noexcept
{
    if (size == 0)
        return;

    const auto n = static_cast<std::size_t>(size);
    for (std::size_t i = 0; i < n; ++i)
        frame[i] = history[(writePosition + i) & mask] * window[i]; // oldest sample first

    fft->forward(frame.data(), bins.data());

    const auto binsUsed = bins.size();
    for (std::size_t k = 0; k < binsUsed; ++k)
    {
        // DC and Nyquist have no mirrored negative-frequency half, so they get half the scale.
        const float scale = (k == 0 || k == binsUsed - 1) ? binScale * 0.5f : binScale;
        rawDb[k] = gainToDb(std::abs(bins[k]) * scale);
    }

    const float dt = static_cast<float>(std::max(0.0, elapsedSeconds));
    // Frame-rate independent exponential averaging: `smoothing` is the per-1/60 s retention.
    const float retain = ballistics.smoothing > 0.0f ? std::pow(std::clamp(ballistics.smoothing, 0.0f, 0.999f), dt * 60.0f) : 0.0f;
    const float maxFall = ballistics.decayDbPerSecond * dt;
    const float peakFall = ballistics.peakDecayDbPerSecond * dt;

    for (std::size_t k = 0; k < binsUsed; ++k)
    {
        averagedDb[k] = rawDb[k] + (averagedDb[k] - rawDb[k]) * retain;
        shownDb[k] = std::max(averagedDb[k], shownDb[k] - maxFall);

        if (ballistics.peakHoldSeconds <= 0.0f)
        {
            peakDb[k] = shownDb[k];
        }
        else if (shownDb[k] >= peakDb[k])
        {
            peakDb[k] = shownDb[k];
            peakHoldRemaining[k] = ballistics.peakHoldSeconds;
        }
        else if (peakHoldRemaining[k] > 0.0f)
        {
            peakHoldRemaining[k] -= dt;
        }
        else
        {
            peakDb[k] = std::max(shownDb[k], peakDb[k] - peakFall);
        }
    }
}

void SpectrumAnalyzer::resetPeakHold() noexcept
{
    std::copy(shownDb.begin(), shownDb.end(), peakDb.begin());
    std::fill(peakHoldRemaining.begin(), peakHoldRemaining.end(), 0.0f);
}

} // namespace sonarc
