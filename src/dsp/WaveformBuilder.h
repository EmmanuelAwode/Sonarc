#pragma once

#include "audio/AudioTypes.h"

#include <algorithm>
#include <cstddef>

namespace sonarc {

// Minimum and maximum sample value over a short run of samples, per channel
// and for the mid signal (L + R) / 2. Drawing these instead of decimated
// samples means no peak is ever skipped, at any zoom level.
struct WaveBucket
{
    float minLeft = 0.0f, maxLeft = 0.0f;
    float minRight = 0.0f, maxRight = 0.0f;
    float minMid = 0.0f, maxMid = 0.0f;
};

// Turns a stream of frames into WaveBuckets of kFramesPerBucket frames each.
// Partial buckets carry over between calls. Never allocates.
class WaveformBuilder
{
public:
    static constexpr std::size_t kFramesPerBucket = 8;

    static constexpr std::size_t maxBucketsFor(std::size_t frameCount) noexcept
    {
        return frameCount / kFramesPerBucket + 1;
    }

    void reset() noexcept { framesInCurrent = 0; }

    // Writes completed buckets to `out` (room for maxBucketsFor(count) needed) and returns how many.
    std::size_t process(const StereoFrame* frames, std::size_t count, WaveBucket* out) noexcept
    {
        std::size_t produced = 0;

        for (std::size_t i = 0; i < count; ++i)
        {
            const float l = frames[i].left;
            const float r = frames[i].right;
            const float m = 0.5f * (l + r);

            if (framesInCurrent == 0)
            {
                current = { l, l, r, r, m, m };
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

            if (++framesInCurrent == kFramesPerBucket)
            {
                out[produced++] = current;
                framesInCurrent = 0;
            }
        }

        return produced;
    }

private:
    WaveBucket current;
    std::size_t framesInCurrent = 0;
};

} // namespace sonarc
