#include "dsp/MagnitudeSpectrum.h"
#include "dsp/Decibels.h"

#include <cmath>
#include <numbers>

namespace sonarc {

void MagnitudeSpectrum::prepare(int fftSize)
{
    n = fftSize;
    mask = static_cast<std::size_t>(fftSize - 1);
    fft = std::make_unique<RealFft>(fftSize);

    const auto size = static_cast<std::size_t>(fftSize);
    frame.assign(size, 0.0f);

    // Periodic Hann window; its coherent gain (mean value) is 0.5.
    window.resize(size);
    double windowSum = 0.0;
    for (std::size_t i = 0; i < size; ++i)
    {
        window[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(size)));
        windowSum += window[i];
    }

    // One-sided amplitude scaling: a sine of amplitude A appears as A at its bin.
    binScale = static_cast<float>(2.0 / windowSum);
    bins.assign(size / 2 + 1, {});
}

void MagnitudeSpectrum::compute(const float* history, std::size_t oldest, float* dbOut) noexcept
{
    if (n == 0)
        return;

    const auto size = static_cast<std::size_t>(n);
    for (std::size_t i = 0; i < size; ++i)
        frame[i] = history[(oldest + i) & mask] * window[i];

    fft->forward(frame.data(), bins.data());

    const auto binsUsed = bins.size();
    for (std::size_t k = 0; k < binsUsed; ++k)
    {
        // DC and Nyquist have no mirrored negative-frequency half, so they get half the scale.
        const float scale = (k == 0 || k == binsUsed - 1) ? binScale * 0.5f : binScale;
        dbOut[k] = gainToDb(std::abs(bins[k]) * scale);
    }
}

} // namespace sonarc
