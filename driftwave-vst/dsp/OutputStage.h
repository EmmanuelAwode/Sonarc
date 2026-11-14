#pragma once

#include "dsp/Smoothing.h"

namespace driftwave {

/** Soft ceiling: identity up to 85% of the ceiling, then a tanh knee that never
    exceeds it. Same curve as the Driftwave app's export limiter safety stage. */
inline float softCeiling(float x, float ceiling) noexcept
{
    const float a = std::abs(x);
    const float knee = ceiling * 0.85f;
    if (a <= knee)
        return x;
    const float span = ceiling - knee;
    const float y = knee + span * std::tanh((a - knee) / span);
    return x < 0.0f ? -y : y;
}

/** Output gain followed by optional clipping protection. */
class OutputStage {
public:
    void prepare(double sampleRate) noexcept
    {
        const float rate = float(sampleRate);
        gainSm.setTime(0.02f, rate);
        ceilingSm.setTime(0.02f, rate);
        clipSm.setTime(0.01f, rate);
        needsSnap = true;
    }

    void reset() noexcept { needsSnap = true; }

    void setTargets(float gainDb, bool clipOn, float ceilingDb) noexcept
    {
        targetGain = dbToGain(gainDb);
        targetCeiling = dbToGain(ceilingDb);
        targetClip = clipOn ? 1.0f : 0.0f;
    }

    void process(float* left, float* right, int numSamples) noexcept
    {
        if (needsSnap) {
            gainSm.snap(targetGain);
            ceilingSm.snap(targetCeiling);
            clipSm.snap(targetClip);
            needsSnap = false;
        }

        for (int i = 0; i < numSamples; ++i) {
            const float g = gainSm.next(targetGain);
            const float c = ceilingSm.next(targetCeiling);
            const float blend = clipSm.next(targetClip);
            float l = left[i] * g, r = right[i] * g;
            if (blend > 0.0f) {
                // Crossfading in/out of the clipper keeps the on/off switch click-free.
                l += blend * (softCeiling(l, c) - l);
                r += blend * (softCeiling(r, c) - r);
            }
            left[i] = l;
            right[i] = r;
        }
    }

private:
    Smoother gainSm, ceilingSm, clipSm;
    float targetGain = 1.0f, targetCeiling = 1.0f, targetClip = 1.0f;
    bool needsSnap = true;
};

} // namespace driftwave
