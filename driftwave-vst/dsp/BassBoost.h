#pragma once

#include "dsp/Biquad.h"
#include "dsp/Smoothing.h"

namespace driftwave {

/** Low-shelf bass boost with optional saturation of the low band.

    Drive isolates the band below ~1.5x the shelf frequency, runs it through a
    tanh curve normalised to unity at full scale, and adds the difference back.
    Quiet bass gets thicker (up to about +6 dB), loud bass is rounded off and
    gains odd harmonics, which is what makes it audible on small speakers.
    The harmonics of a sub-300 Hz band stay far below Nyquist, so no oversampling is needed. */
class BassBoost {
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        const float controlRate = float(sampleRate) / float(kControlInterval);
        gainSm.setTime(0.03f, controlRate);
        freqSm.setTime(0.03f, controlRate);
        driveSm.setTime(0.03f, controlRate);
        lastGain = lastFreq = -1.0f; // force new coefficients for the new sample rate
        reset();
    }

    void reset() noexcept
    {
        for (auto& s : shelfState) s.reset();
        for (auto& s : bandState) s.reset();
        counter = 0;
        needsSnap = true;
    }

    /** @param gainDb 0..18, @param hz shelf frequency, @param drive01 0..1 */
    void setTargets(float gainDb, float hz, float drive01) noexcept
    {
        targetGain = gainDb;
        targetFreq = hz;
        targetDrive = drive01;
    }

    void process(float* left, float* right, int numSamples) noexcept
    {
        if (needsSnap) {
            gainSm.snap(targetGain);
            freqSm.snap(targetFreq);
            driveSm.snap(targetDrive);
            needsSnap = false;
            counter = 0;
        }

        for (int i = 0; i < numSamples; ++i) {
            if (counter == 0)
                updateCoefficients();
            counter = (counter + 1) % kControlInterval;

            left[i] = tick(left[i], 0);
            right[i] = tick(right[i], 1);
        }
    }

private:
    static constexpr float kSatGain = 2.0f;
    static constexpr float kSatNorm = 1.0f / 0.96402758f; // 1 / tanh(2)

    void updateCoefficients() noexcept
    {
        const float g = gainSm.next(targetGain);
        const float f = freqSm.next(targetFreq);
        drive = driveSm.next(targetDrive);
        if (g != lastGain || f != lastFreq) {
            shelf = makeLowShelf(sampleRate, f, g);
            band = makeLowPass(sampleRate, f * 1.5);
            lastGain = g;
            lastFreq = f;
        }
    }

    float tick(float x, int ch) noexcept
    {
        const float y = shelfState[ch].process(shelf, x);
        // Always run the band filter so its state is valid the moment drive is turned up.
        const float low = bandState[ch].process(band, y);
        if (drive <= 0.0f)
            return y;
        return y + drive * (std::tanh(kSatGain * low) * kSatNorm - low);
    }

    double sampleRate = 48000.0;
    BiquadCoeffs shelf, band;
    BiquadState shelfState[2], bandState[2];
    Smoother gainSm, freqSm, driveSm;
    float targetGain = 0.0f, targetFreq = 90.0f, targetDrive = 0.0f;
    float lastGain = -1.0f, lastFreq = -1.0f, drive = 0.0f;
    int counter = 0;
    bool needsSnap = true;
};

} // namespace driftwave
