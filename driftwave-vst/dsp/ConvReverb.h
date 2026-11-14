#pragma once

#include "dsp/Biquad.h"
#include "dsp/Fft.h"
#include "dsp/Smoothing.h"

#include <vector>

namespace driftwave {

/** The classic "slowed + reverb" reverb: a convolution with stereo white noise
    that dies away exponentially after a 10 ms gap.

    This is the reverb the popular browser tools use (a Web Audio ConvolverNode
    fed a generated noise burst), reproduced the same way rather than imitated:
    left and right each get their own noise, there is no filtering and no
    modulation, and at 3 seconds the level and decay match the reference.

    The convolution is a real one, done in the frequency domain in two stages so
    that it costs little and never spikes: a fine stage (128-sample blocks) for
    the first third of a second, and a coarse stage (8192-sample blocks) for the
    rest, whose work is spread evenly over the time before its result is needed.
    The fine stage's 128 samples of delay are hidden inside the 10 ms gap, so
    the reverb adds no delay beyond that gap.

    Decay is adjustable without recomputing anything: the stored noise dies away
    at the slowest rate, and a faster decay is applied as an exact exponential
    weighting inside the convolution. Changing it is click-free and automatable.

    Real-time safe: prepare() allocates, process() does not. */
class ConvReverb {
public:
    static constexpr float kMinDecay = 0.5f, kMaxDecay = 5.0f, kReferenceDecay = 3.0f;
    static constexpr double kPreDelaySeconds = 0.010;
    /** Energy gain of the wet path for one channel (what a ConvolverNode's
        normalisation gives the reference burst at 44.1-48 kHz). */
    static constexpr double kWetEnergy = 0.33;

    struct Params {
        float decaySeconds = kReferenceDecay; // RT60
        float lowCutHz = 20.0f;      // at 20 Hz the low cut is off
        float highCutHz = 20000.0f;  // at 20 kHz the high cut is off
    };

    void prepare(double sampleRate);
    void reset() noexcept;
    void setParams(const Params& p) noexcept;

    /** Writes the fully wet signal. The inputs are not modified and may not alias the outputs. */
    void process(const float* inL, const float* inR, float* wetL, float* wetR, int numSamples) noexcept;

private:
    static constexpr int kFine = 128;      // fine block; also this reverb's internal delay
    static constexpr int kCoarse = 8192;   // coarse block
    static constexpr int kSubBlocks = kCoarse / kFine;
    static constexpr double kKernelSeconds = 8.0;

    /** One uniformly partitioned frequency-domain convolver. */
    struct Stage {
        int block = 0, bins = 0, partitions = 0;
        Fft fft;
        std::vector<float> kernelRe[2], kernelIm[2]; // partitions x bins
        std::vector<float> lineRe[2], lineIm[2];     // spectra of the most recent input frames
        std::vector<float> frame[2];                 // previous block, then the current one
        std::vector<float> accRe[2], accIm[2];
        std::vector<float> workRe, workIm;
        int newest = 0;                              // slot in the line holding the newest frame
        int filled = 0;                              // frames stored since the last reset

        void prepare(int blockSize, int numPartitions);
        void setKernel(int channel, const float* impulse, int length);
        void reset() noexcept;
        /** Moves one block of input into the frame. */
        void shiftIn(int channel, const float* input) noexcept;
        /** Weights the frame, transforms it and stores the spectrum. */
        void transform(int channel, double rate, bool advance) noexcept;
        void clearAccumulator() noexcept;
        void accumulate(int from, int to, double rate) noexcept;
        void finish(int channel, float* output, double rate, float gain) noexcept;
    };

    void runFine() noexcept;
    void runCoarseStep() noexcept;
    double outputGain(double rate) const noexcept;

    double sampleRate = 48000.0;
    int preSamples = 0;       // silent samples at the start of the stored kernel
    double baseRate = 0.0;    // decay per sample already in the kernel
    int coarseOffset = 0;     // kernel index where the coarse stage takes over

    Stage fine, coarse;
    std::vector<float> fineIn[2], fineOut[2];
    std::vector<float> coarseIn[2], coarseOutNow[2], coarseOutNext[2];
    int pos = 0, subBlock = 0;
    int coarseStep = 0, coarseSteps = 0, coarsePerStep = 1;
    bool coarseBusy = false;
    double coarseRate = 0.0;

    Params target;
    Smoother decaySm, lowCutSm, highCutSm; // per fine block
    double rate = 0.0;                     // extra decay per sample for the current block
    BiquadCoeffs lowCutA, lowCutB, highCut;
    BiquadState lowCutStateA[2], lowCutStateB[2], highCutState[2];
    float lastLowCut = -1.0f, lastHighCut = -1.0f;
    bool lowCutOn = false, highCutOn = false;
    bool needsSnap = true;
};

} // namespace driftwave
