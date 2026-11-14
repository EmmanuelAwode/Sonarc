#pragma once

#include "dsp/BassBoost.h"
#include "dsp/ConvReverb.h"
#include "dsp/OutputStage.h"
#include "dsp/TapeSpeed.h"

namespace driftwave {

/** Everything the chain needs for one block, as plain values. The plugin fills
    this from its atomics at the top of every audio callback. */
struct ChainParams {
    bool speedOn = false;
    float speed = 0.8f;          // 0.5..1.5, tape style: pitch follows speed
    bool speedSync = false;       // keep the pitch change but stay in time with the host

    bool reverbOn = true;
    float reverbMix = 0.4f;      // 0..1, equal-power dry/wet
    ConvReverb::Params reverb;

    bool bassOn = false;
    float bassGainDb = 6.0f;     // 0..18
    float bassFreqHz = 90.0f;    // 40..200
    float bassDrive = 0.0f;      // 0..1

    float outputGainDb = 0.0f;   // -24..+12
    bool clipOn = true;
    float ceilingDb = -1.0f;     // -12..0
};

/** The signal path, in the same order as the Driftwave app's chain:

      input -> tape speed -> bass shelf (+ drive) -> reverb (dry + wet) -> output gain -> soft ceiling

    Zero latency (a slowed signal falls behind the input, but that is the effect
    itself, not latency to compensate). Real-time safe: prepare() allocates, process() does not. */
class EffectChain {
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void setParams(const ChainParams& p) noexcept;
    void process(float* left, float* right, int numSamples) noexcept;

    /** Crossfade the slowed signal back to the live input (e.g. when playback restarts). */
    void resyncSpeed() noexcept { tape.resync(); }
    double speedLagSeconds() const noexcept { return tape.lagSeconds(); }

    /** Measurements of the block just processed, for displays. */
    float wetPeak() const noexcept { return lastWetPeak; }             // reverb level as mixed in
    float clipReductionDb() const noexcept { return lastReductionDb; } // 0 or negative

private:
    static constexpr int kChunk = 256;

    ChainParams params;
    TapeSpeed tape;
    BassBoost bass;
    ConvReverb reverb;
    OutputStage output;
    Smoother drySm, wetSm;
    float dryTarget = 1.0f, wetTarget = 0.0f;
    bool needsSnap = true;
    bool reverbCleared = true;
    float lastWetPeak = 0.0f, lastReductionDb = 0.0f;
};

} // namespace driftwave
