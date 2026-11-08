#pragma once

#include "audio/AudioTypes.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace sonarc {

// One short run of samples, summarised for drawing:
//  - min / max per channel and for the mid signal (L + R) / 2, so no peak is
//    ever skipped at any zoom level
//  - mean square per channel (for the RMS "core" of the waveform)
//  - mean square of the mid signal's lows / mids / highs (for colouring the
//    waveform by frequency content)
struct WaveBucket
{
    float minLeft = 0.0f, maxLeft = 0.0f;
    float minRight = 0.0f, maxRight = 0.0f;
    float minMid = 0.0f, maxMid = 0.0f;
    float msLeft = 0.0f, msRight = 0.0f, msMid = 0.0f;
    float msLow = 0.0f, msMidBand = 0.0f, msHigh = 0.0f;
};

// Turns a stream of frames into WaveBuckets of kFramesPerBucket frames each.
// Partial buckets carry over between calls. Never allocates.
class WaveformBuilder
{
public:
    static constexpr std::size_t kFramesPerBucket = 8;

    // Band split points for the colour information.
    static constexpr double kLowCrossoverHz = 250.0;
    static constexpr double kHighCrossoverHz = 2500.0;

    static constexpr std::size_t maxBucketsFor(std::size_t frameCount) noexcept
    {
        return frameCount / kFramesPerBucket + 1;
    }

    void prepare(std::uint32_t sampleRate) noexcept
    {
        auto coefficient = [sampleRate](double hz)
        {
            return sampleRate > 0 ? static_cast<float>(1.0 - std::exp(-2.0 * std::numbers::pi * hz / sampleRate)) : 1.0f;
        };
        lowCoefficient = coefficient(kLowCrossoverHz);
        highCoefficient = coefficient(kHighCrossoverHz);
        reset();
    }

    void reset() noexcept
    {
        framesInCurrent = 0;
        low1 = low2 = high1 = high2 = 0.0f;
    }

    // Writes completed buckets to `out` (room for maxBucketsFor(count) needed) and returns how many.
    std::size_t process(const StereoFrame* frames, std::size_t count, WaveBucket* out) noexcept
    {
        std::size_t produced = 0;

        for (std::size_t i = 0; i < count; ++i)
        {
            const float l = frames[i].left;
            const float r = frames[i].right;
            const float m = 0.5f * (l + r);

            // Two cascaded one-pole low-passes per split (12 dB/octave).
            low1 += lowCoefficient * (m - low1);
            low2 += lowCoefficient * (low1 - low2);
            high1 += highCoefficient * (m - high1);
            high2 += highCoefficient * (high1 - high2);
            const float lowBand = low2;
            const float midBand = high2 - low2;
            const float highBand = m - high2;

            if (framesInCurrent == 0)
            {
                current = { l, l, r, r, m, m };
                sumL = sumR = sumM = sumLow = sumMidBand = sumHigh = 0.0f;
            }
            else
            {
                current.minLeft = std::min(current.minLeft, l);
                current.maxLeft = std::max(current.maxLeft, l);
                current.minRight = std::min(current.minRight, r);
                current.maxRight = std::max(current.maxRight, r);
                current.minMid = std::min(current.minMid, m);
                current.maxMid = std::max(current.maxMid, m);
            }

            sumL += l * l;
            sumR += r * r;
            sumM += m * m;
            sumLow += lowBand * lowBand;
            sumMidBand += midBand * midBand;
            sumHigh += highBand * highBand;

            if (++framesInCurrent == kFramesPerBucket)
            {
                constexpr float scale = 1.0f / static_cast<float>(kFramesPerBucket);
                current.msLeft = sumL * scale;
                current.msRight = sumR * scale;
                current.msMid = sumM * scale;
                current.msLow = sumLow * scale;
                current.msMidBand = sumMidBand * scale;
                current.msHigh = sumHigh * scale;
                out[produced++] = current;
                framesInCurrent = 0;
            }
        }

        return produced;
    }

private:
    WaveBucket current;
    std::size_t framesInCurrent = 0;
    float sumL = 0, sumR = 0, sumM = 0, sumLow = 0, sumMidBand = 0, sumHigh = 0;
    float lowCoefficient = 1.0f, highCoefficient = 1.0f;
    float low1 = 0, low2 = 0, high1 = 0, high2 = 0;
};

} // namespace sonarc
