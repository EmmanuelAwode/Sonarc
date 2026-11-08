#pragma once

#include "audio/AudioTypes.h"
#include "dsp/MagnitudeSpectrum.h"

#include <array>
#include <cstdint>
#include <vector>

namespace sonarc {

inline constexpr int kSpectrogramBands = 512;
inline constexpr float kSpectrogramMinHz = 20.0f;
inline constexpr float kSpectrogramMaxHz = 20000.0f;

// One moment of the spectrogram: level in dBFS for kSpectrogramBands bands,
// log-spaced from 20 Hz (band 0) to 20 kHz (last band).
struct SpectrogramRow
{
    std::array<float, kSpectrogramBands> db {};
};

// Short-time spectrum of the mid signal at a fixed hop, resampled onto log bands.
// Same calibration as the spectrum analyser (full-scale sine = 0 dBFS).
class SpectrogramAnalyzer
{
public:
    static constexpr std::size_t kHopFrames = 256; // one row per 256 frames (~5.3 ms at 48 kHz)

    // Centre frequency of a (fractional) band index.
    static float bandFrequency(float band) noexcept;

    void prepare(std::uint32_t sampleRate, int fftSize);
    void reset() noexcept; // forget past audio (e.g. after being disabled)

    int fftSize() const noexcept { return spectrum.size(); }

    // Calls onRow(const SpectrogramRow&) for every completed hop. Never allocates.
    template <typename OnRow>
    void process(const StereoFrame* frames, std::size_t count, OnRow&& onRow) noexcept
    {
        if (history.empty())
            return;

        for (std::size_t i = 0; i < count; ++i)
        {
            history[writePosition] = 0.5f * (frames[i].left + frames[i].right);
            writePosition = (writePosition + 1) & mask;

            if (++framesSinceRow == kHopFrames)
            {
                framesSinceRow = 0;
                computeRow();
                onRow(row);
            }
        }
    }

private:
    struct Band
    {
        int firstBin, lastBin; // range to take the maximum over, or -1 to interpolate
        float centreBin;
    };

    void computeRow() noexcept;

    MagnitudeSpectrum spectrum;
    std::vector<float> history, binDb;
    std::size_t mask = 0, writePosition = 0, framesSinceRow = 0;
    std::vector<Band> bands;
    SpectrogramRow row;
};

} // namespace sonarc
