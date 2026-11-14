#include "dsp/EffectChain.h"

#include <algorithm>
#include <cmath>

namespace driftwave {

void EffectChain::prepare(double sampleRate)
{
    tape.prepare(sampleRate);
    bass.prepare(sampleRate);
    reverb.prepare(sampleRate);
    output.prepare(sampleRate);
    drySm.setTime(0.02f, float(sampleRate));
    wetSm.setTime(0.02f, float(sampleRate));
    setParams(params);
    reset();
}

void EffectChain::reset() noexcept
{
    tape.reset();
    bass.reset();
    reverb.reset();
    output.reset();
    reverbCleared = true;
    needsSnap = true;
}

void EffectChain::setParams(const ChainParams& p) noexcept
{
    params = p;

    // A switched-off section glides to its neutral setting instead of being cut out.
    tape.setTarget(p.speedOn, p.speed, p.speedSync);
    bass.setTargets(p.bassOn ? p.bassGainDb : 0.0f, p.bassFreqHz, p.bassOn ? p.bassDrive : 0.0f);
    reverb.setParams(p.reverb);
    output.setTargets(p.outputGainDb, p.clipOn, p.ceilingDb);

    const float mix = p.reverbOn ? std::clamp(p.reverbMix, 0.0f, 1.0f) : 0.0f;
    dryTarget = std::cos(mix * float(kPi) * 0.5f);
    wetTarget = std::sin(mix * float(kPi) * 0.5f);
}

void EffectChain::process(float* left, float* right, int numSamples) noexcept
{
    if (needsSnap) {
        drySm.snap(dryTarget);
        wetSm.snap(wetTarget);
        needsSnap = false;
    }

    tape.process(left, right, numSamples);
    bass.process(left, right, numSamples);

    float wetL[kChunk], wetR[kChunk];
    float wetPeakNow = 0.0f;
    for (int pos = 0; pos < numSamples; pos += kChunk) {
        const int count = std::min(kChunk, numSamples - pos);
        float* l = left + pos;
        float* r = right + pos;

        // With the wet signal fully faded out the reverb is skipped. Its buffers are
        // cleared once so a stale tail can't reappear when it is switched back on.
        const bool idle = wetTarget == 0.0f && wetSm.current() == 0.0f;
        if (idle) {
            if (!reverbCleared) {
                reverb.reset();
                reverbCleared = true;
            }
            std::fill(wetL, wetL + count, 0.0f);
            std::fill(wetR, wetR + count, 0.0f);
        } else {
            reverbCleared = false;
            reverb.process(l, r, wetL, wetR, count);
        }

        for (int i = 0; i < count; ++i) {
            const float dry = drySm.next(dryTarget);
            const float wet = wetSm.next(wetTarget);
            l[i] = l[i] * dry + wetL[i] * wet;
            r[i] = r[i] * dry + wetR[i] * wet;
            wetPeakNow = std::max(wetPeakNow, std::abs(wetL[i] * wet));
        }
    }
    lastWetPeak = wetPeakNow;

    // How much the clip guard takes off the loudest sample of this block.
    float before = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        before = std::max({ before, std::abs(left[i]), std::abs(right[i]) });

    output.process(left, right, numSamples);

    float after = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        after = std::max({ after, std::abs(left[i]), std::abs(right[i]) });
    const float expected = before * dbToGain(params.outputGainDb);
    lastReductionDb = params.clipOn && expected > 1.0e-4f && after < expected
                        ? std::max(-24.0f, 20.0f * std::log10(std::max(after, 1.0e-6f) / expected))
                        : 0.0f;
}

} // namespace driftwave
