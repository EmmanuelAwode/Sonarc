#include "dsp/ConvReverb.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace driftwave {

namespace {
constexpr double kLn1000 = 6.907755278982137; // 60 dB as a natural-log amplitude ratio
constexpr double kNoiseVariance = 1.0 / 3.0;  // of noise spread evenly over -1..1
}

// ---------------------------------------------------------------------------

void ConvReverb::Stage::prepare(int blockSize, int numPartitions)
{
    block = blockSize;
    bins = blockSize + 1;
    partitions = numPartitions;
    fft.prepare(2 * blockSize);
    for (int ch = 0; ch < 2; ++ch) {
        kernelRe[ch].assign(size_t(partitions * bins), 0.0f);
        kernelIm[ch].assign(size_t(partitions * bins), 0.0f);
        lineRe[ch].assign(size_t(partitions * bins), 0.0f);
        lineIm[ch].assign(size_t(partitions * bins), 0.0f);
        frame[ch].assign(size_t(2 * block), 0.0f);
        accRe[ch].assign(size_t(bins), 0.0f);
        accIm[ch].assign(size_t(bins), 0.0f);
    }
    workRe.assign(size_t(2 * block), 0.0f);
    workIm.assign(size_t(2 * block), 0.0f);
    reset();
}

void ConvReverb::Stage::setKernel(int channel, const float* impulse, int length)
{
    for (int k = 0; k < partitions; ++k) {
        std::fill(workRe.begin(), workRe.end(), 0.0f);
        std::fill(workIm.begin(), workIm.end(), 0.0f);
        const int from = k * block;
        const int count = std::clamp(length - from, 0, block);
        std::copy(impulse + from, impulse + from + count, workRe.begin());
        fft.forward(workRe.data(), workIm.data());
        std::copy(workRe.begin(), workRe.begin() + bins, kernelRe[channel].begin() + k * bins);
        std::copy(workIm.begin(), workIm.begin() + bins, kernelIm[channel].begin() + k * bins);
    }
}

void ConvReverb::Stage::reset() noexcept
{
    // The stored spectra are not cleared (they are large): `filled` keeps the
    // convolution away from anything stored before this point.
    newest = 0;
    filled = 0;
    for (int ch = 0; ch < 2; ++ch)
        std::fill(frame[ch].begin(), frame[ch].end(), 0.0f);
}

void ConvReverb::Stage::shiftIn(int channel, const float* input) noexcept
{
    auto& f = frame[channel];
    std::memcpy(f.data(), f.data() + block, size_t(block) * sizeof(float));
    std::memcpy(f.data() + block, input, size_t(block) * sizeof(float));
}

void ConvReverb::Stage::transform(int channel, double rate, bool advance) noexcept
{
    if (advance) {
        newest = newest + 1 == partitions ? 0 : newest + 1;
        filled = std::min(filled + 1, partitions);
    }

    // Exponential weighting, rising through the frame. Together with the falling
    // weight on the output and the per-partition gains this multiplies the stored
    // impulse response by exp(-rate * n), exactly, without touching the kernel.
    const float* f = frame[channel].data();
    double w = std::exp(-rate * double(block));
    const double up = std::exp(rate);
    for (int n = 0; n < 2 * block; ++n) {
        workRe[size_t(n)] = float(double(f[n]) * w);
        w *= up;
    }
    std::fill(workIm.begin(), workIm.end(), 0.0f);
    fft.forward(workRe.data(), workIm.data());
    std::copy(workRe.begin(), workRe.begin() + bins, lineRe[channel].begin() + newest * bins);
    std::copy(workIm.begin(), workIm.begin() + bins, lineIm[channel].begin() + newest * bins);
}

void ConvReverb::Stage::clearAccumulator() noexcept
{
    for (int ch = 0; ch < 2; ++ch) {
        std::fill(accRe[ch].begin(), accRe[ch].end(), 0.0f);
        std::fill(accIm[ch].begin(), accIm[ch].end(), 0.0f);
    }
}

void ConvReverb::Stage::accumulate(int from, int to, double rate) noexcept
{
    to = std::min(to, filled);
    for (int k = from; k < to; ++k) {
        int slot = newest - k;
        if (slot < 0)
            slot += partitions;
        const auto gain = float(std::exp(-rate * double(block) * double(k)));
        for (int ch = 0; ch < 2; ++ch) {
            const float* xr = lineRe[ch].data() + slot * bins;
            const float* xi = lineIm[ch].data() + slot * bins;
            const float* hr = kernelRe[ch].data() + k * bins;
            const float* hi = kernelIm[ch].data() + k * bins;
            float* ar = accRe[ch].data();
            float* ai = accIm[ch].data();
            for (int b = 0; b < bins; ++b) {
                ar[b] += gain * (xr[b] * hr[b] - xi[b] * hi[b]);
                ai[b] += gain * (xr[b] * hi[b] + xi[b] * hr[b]);
            }
        }
    }
}

void ConvReverb::Stage::finish(int channel, float* output, double rate, float gain) noexcept
{
    const float* ar = accRe[channel].data();
    const float* ai = accIm[channel].data();
    const int n = 2 * block;
    for (int b = 0; b < bins; ++b) {
        workRe[size_t(b)] = ar[b];
        workIm[size_t(b)] = ai[b];
    }
    for (int b = 1; b < block; ++b) { // the mirrored half of a real signal's spectrum
        workRe[size_t(n - b)] = ar[b];
        workIm[size_t(n - b)] = -ai[b];
    }
    fft.inverse(workRe.data(), workIm.data());

    // The second half of the frame is the valid part of the result.
    double w = double(gain);
    const double down = std::exp(-rate);
    for (int i = 0; i < block; ++i) {
        output[i] = float(double(workRe[size_t(block + i)]) * w);
        w *= down;
    }
}

// ---------------------------------------------------------------------------

void ConvReverb::prepare(double sr)
{
    sampleRate = sr;
    baseRate = kLn1000 / (double(kMaxDecay) * sr);
    preSamples = std::max(0, int(std::lround(kPreDelaySeconds * sr)) - kFine);
    coarseOffset = 2 * kCoarse - kFine;

    // The stored impulse response: silence for the pre-delay (less the fine stage's
    // own delay), then noise dying away at the slowest rate on offer.
    const int length = preSamples + int(kKernelSeconds * sr);
    const int finePartitions = coarseOffset / kFine;
    const int coarsePartitions = std::max(1, (length - coarseOffset + kCoarse - 1) / kCoarse);
    fine.prepare(kFine, finePartitions);
    coarse.prepare(kCoarse, coarsePartitions);

    std::vector<float> impulse(size_t(std::max(length, coarseOffset + 1)), 0.0f);
    for (int ch = 0; ch < 2; ++ch) {
        uint32_t state = ch == 0 ? 0x9e3779b9u : 0x7f4a7c15u; // fixed seeds: renders are repeatable
        std::fill(impulse.begin(), impulse.end(), 0.0f);
        double envelope = 1.0;
        const double down = std::exp(-baseRate);
        for (int n = preSamples; n < length; ++n) {
            state = state * 1664525u + 1013904223u;
            const double noise = double(state >> 8) / 8388608.0 - 1.0;
            impulse[size_t(n)] = float(noise * envelope);
            envelope *= down;
        }
        fine.setKernel(ch, impulse.data(), std::min(length, coarseOffset));
        coarse.setKernel(ch, impulse.data() + coarseOffset, std::max(0, length - coarseOffset));
    }

    for (int ch = 0; ch < 2; ++ch) {
        fineIn[ch].assign(size_t(kFine), 0.0f);
        fineOut[ch].assign(size_t(kFine), 0.0f);
        coarseIn[ch].assign(size_t(kCoarse), 0.0f);
        coarseOutNow[ch].assign(size_t(kCoarse), 0.0f);
        coarseOutNext[ch].assign(size_t(kCoarse), 0.0f);
    }

    // The coarse stage has one coarse block of time to do its work: two transforms,
    // the partitions in equal slices, two inverse transforms.
    coarsePerStep = (coarsePartitions + (kSubBlocks - 4) - 1) / (kSubBlocks - 4);
    coarseSteps = 4 + (coarsePartitions + coarsePerStep - 1) / coarsePerStep;

    const float tickRate = float(sr) / float(kFine);
    decaySm.setTime(0.08f, tickRate);
    lowCutSm.setTime(0.05f, tickRate);
    highCutSm.setTime(0.05f, tickRate);
    lastLowCut = lastHighCut = -1.0f;
    reset();
}

void ConvReverb::reset() noexcept
{
    fine.reset();
    coarse.reset();
    for (int ch = 0; ch < 2; ++ch) {
        std::fill(fineIn[ch].begin(), fineIn[ch].end(), 0.0f);
        std::fill(fineOut[ch].begin(), fineOut[ch].end(), 0.0f);
        std::fill(coarseIn[ch].begin(), coarseIn[ch].end(), 0.0f);
        std::fill(coarseOutNow[ch].begin(), coarseOutNow[ch].end(), 0.0f);
        std::fill(coarseOutNext[ch].begin(), coarseOutNext[ch].end(), 0.0f);
        lowCutStateA[ch].reset();
        lowCutStateB[ch].reset();
        highCutState[ch].reset();
    }
    pos = subBlock = 0;
    coarseBusy = false;
    coarseStep = 0;
    needsSnap = true;
}

void ConvReverb::setParams(const Params& p) noexcept
{
    target.decaySeconds = std::clamp(p.decaySeconds, kMinDecay, kMaxDecay);
    target.lowCutHz = std::clamp(p.lowCutHz, 20.0f, 2000.0f);
    target.highCutHz = std::clamp(p.highCutHz, 500.0f, 20000.0f);
}

double ConvReverb::outputGain(double extraRate) const noexcept
{
    // Energy of the weighted kernel for unit noise, in closed form; scaled so the
    // wet path always has the reference energy, whatever the decay.
    const double energy = kNoiseVariance * std::exp(-2.0 * extraRate * double(preSamples))
                        / (1.0 - std::exp(-2.0 * (baseRate + extraRate)));
    return std::sqrt(kWetEnergy / energy);
}

void ConvReverb::runFine() noexcept
{
    // Control values move once per fine block.
    const float decay = decaySm.next(target.decaySeconds);
    rate = std::max(0.0, kLn1000 / (double(decay) * sampleRate) - baseRate);

    const float lowCut = lowCutSm.next(target.lowCutHz);
    lowCutOn = lowCut > 20.5f;
    if (lowCutOn && lowCut != lastLowCut) {
        lowCutA = makeHighPass(sampleRate, lowCut, 0.5412); // two sections: 24 dB per octave
        lowCutB = makeHighPass(sampleRate, lowCut, 1.3066);
        lastLowCut = lowCut;
    }
    const float highCutHz = highCutSm.next(target.highCutHz);
    highCutOn = highCutHz < 19990.0f;
    if (highCutOn && highCutHz != lastHighCut) {
        highCut = makeLowPass(sampleRate, highCutHz);
        lastHighCut = highCutHz;
    }

    const auto gain = float(outputGain(rate));
    for (int ch = 0; ch < 2; ++ch) {
        fine.shiftIn(ch, fineIn[ch].data());
        fine.transform(ch, rate, ch == 0);
    }
    fine.clearAccumulator();
    fine.accumulate(0, fine.partitions, rate);
    for (int ch = 0; ch < 2; ++ch)
        fine.finish(ch, fineOut[ch].data(), rate, gain);
}

void ConvReverb::runCoarseStep() noexcept
{
    if (!coarseBusy)
        return;

    const int step = coarseStep++;
    if (step == 0) {
        coarse.transform(0, coarseRate, true);
        coarse.clearAccumulator();
    } else if (step == 1) {
        coarse.transform(1, coarseRate, false);
    } else if (step < coarseSteps - 2) {
        const int from = (step - 2) * coarsePerStep;
        coarse.accumulate(from, std::min(from + coarsePerStep, coarse.partitions), coarseRate);
    } else {
        // The coarse stage's part of the impulse response starts coarseOffset samples in.
        const auto gain = float(outputGain(coarseRate) * std::exp(-coarseRate * double(coarseOffset)));
        const int ch = step - (coarseSteps - 2);
        coarse.finish(ch, coarseOutNext[ch].data(), coarseRate, gain);
        if (ch == 1)
            coarseBusy = false;
    }
}

void ConvReverb::process(const float* inL, const float* inR, float* wetL, float* wetR, int numSamples) noexcept
{
    if (needsSnap) {
        decaySm.snap(target.decaySeconds);
        lowCutSm.snap(target.lowCutHz);
        highCutSm.snap(target.highCutHz);
        needsSnap = false;
    }

    const float* in[2] = { inL, inR };
    float* wet[2] = { wetL, wetR };

    for (int i = 0; i < numSamples; ++i) {
        const int coarsePos = subBlock * kFine + pos;
        for (int ch = 0; ch < 2; ++ch) {
            fineIn[ch][size_t(pos)] = in[ch][i];
            float x = fineOut[ch][size_t(pos)] + coarseOutNow[ch][size_t(coarsePos)];
            if (lowCutOn)
                x = lowCutStateB[ch].process(lowCutB, lowCutStateA[ch].process(lowCutA, x));
            if (highCutOn)
                x = highCutState[ch].process(highCut, x);
            wet[ch][i] = x;
        }

        if (++pos < kFine)
            continue;
        pos = 0;

        runFine();
        for (int ch = 0; ch < 2; ++ch)
            std::memcpy(coarseIn[ch].data() + subBlock * kFine, fineIn[ch].data(), size_t(kFine) * sizeof(float));

        if (++subBlock == kSubBlocks) {
            subBlock = 0;
            // The job started one coarse block ago has finished: its result plays now,
            // and the block just completed becomes the next job.
            for (int ch = 0; ch < 2; ++ch) {
                std::swap(coarseOutNow[ch], coarseOutNext[ch]);
                coarse.shiftIn(ch, coarseIn[ch].data());
            }
            coarseRate = rate;
            coarseStep = 0;
            coarseBusy = true;
        }
        runCoarseStep();
    }
}

} // namespace driftwave
