#include "dsp/SpectrogramAnalyzer.h"
#include "dsp/Decibels.h"

#include <algorithm>
#include <cmath>

namespace sonarc {

float SpectrogramAnalyzer::bandFrequency(float band) noexcept
{
    const float position = (band + 0.5f) / static_cast<float>(kSpectrogramBands);
    return kSpectrogramMinHz * std::pow(kSpectrogramMaxHz / kSpectrogramMinHz, position);
}

void SpectrogramAnalyzer::prepare(std::uint32_t sampleRate, int fftSize)
{
    spectrum.prepare(fftSize);
    const auto n = static_cast<std::size_t>(fftSize);
    mask = n - 1;
    history.assign(n, 0.0f);
    binDb.assign(n / 2 + 1, kSilenceDb);
    writePosition = 0;
    framesSinceRow = 0;

    // Where a band spans whole bins (high frequencies) take the loudest so narrow
    // peaks survive; where bins are wider than a band (low end) interpolate.
    const float binsPerHz = static_cast<float>(fftSize) / static_cast<float>(sampleRate);
    const int lastBin = static_cast<int>(n / 2);
    bands.resize(kSpectrogramBands);

    for (int b = 0; b < kSpectrogramBands; ++b)
    {
        const float low = bandFrequency(static_cast<float>(b) - 0.5f) * binsPerHz;
        const float high = bandFrequency(static_cast<float>(b) + 0.5f) * binsPerHz;
        auto& band = bands[static_cast<std::size_t>(b)];
        band.centreBin = std::clamp(bandFrequency(static_cast<float>(b)) * binsPerHz, 0.0f, static_cast<float>(lastBin));

        if (high - low >= 1.0f)
        {
            band.firstBin = std::clamp(static_cast<int>(std::ceil(low)), 0, lastBin);
            band.lastBin = std::clamp(static_cast<int>(std::floor(high)), band.firstBin, lastBin);
        }
        else
        {
            band.firstBin = band.lastBin = -1;
        }
    }

    row.db.fill(kSilenceDb);
}

void SpectrogramAnalyzer::reset() noexcept
{
    std::fill(history.begin(), history.end(), 0.0f);
    framesSinceRow = 0;
}

void SpectrogramAnalyzer::computeRow() noexcept
{
    spectrum.compute(history.data(), writePosition, binDb.data()); // writePosition = oldest sample

    const int lastBin = static_cast<int>(binDb.size()) - 1;
    for (std::size_t b = 0; b < bands.size(); ++b)
    {
        const auto& band = bands[b];
        if (band.firstBin >= 0)
        {
            float level = binDb[static_cast<std::size_t>(band.firstBin)];
            for (int k = band.firstBin + 1; k <= band.lastBin; ++k)
                level = std::max(level, binDb[static_cast<std::size_t>(k)]);
            row.db[b] = level;
        }
        else
        {
            const int i = std::min(static_cast<int>(band.centreBin), lastBin - 1);
            const float t = band.centreBin - static_cast<float>(i);
            row.db[b] = binDb[static_cast<std::size_t>(i)] + t * (binDb[static_cast<std::size_t>(i + 1)] - binDb[static_cast<std::size_t>(i)]);
        }
    }
}

} // namespace sonarc
