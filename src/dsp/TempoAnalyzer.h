#pragma once

#include "audio/AudioTypes.h"
#include "dsp/Fft.h"

#include <array>
#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

namespace sonarc {

// Smooths raw tempo estimates (one every ~0.5 s) into a stable reading.
// A new tempo only replaces the shown one after it has been seen
// consistently for ~3 s; the reading goes back to "unknown" after ~4 s of
// unreliable estimates.
class TempoTracker
{
public:
    void reset() noexcept;
    void update(bool rawValid, float rawBpm, float rawConfidence) noexcept;

    bool valid() const noexcept { return hasShown; }
    float bpm() const noexcept { return shown; }
    float confidence() const noexcept { return combinedConfidence; } // 0..1

    // Re-express everything in a new octave range (e.g. after switching 70 <-> 140).
    template <typename Fold>
    void refold(Fold&& fold) noexcept
    {
        for (int i = 0; i < historyCount; ++i)
            history[(size_t) i] = fold(history[(size_t) i]);
        if (hasShown)
            shown = fold(shown);
        if (pendingVotes > 0)
            pendingBpm = fold(pendingBpm);
    }

private:
    static constexpr int kHistory = 8;
    static bool close(float a, float b) noexcept;

    std::array<float, kHistory> history {};
    int historyCount = 0, historyHead = 0;
    float shown = 0.0f;
    bool hasShown = false;
    float pendingBpm = 0.0f;
    int pendingVotes = 0;
    int invalidRun = 0;
    float rawConfidenceAverage = 0.0f;
    float combinedConfidence = 0.0f;
};

struct TempoEstimate
{
    bool valid = false;
    float bpm = 0.0f;
    float confidence = 0.0f; // 0..1: periodicity strength combined with stability over time
    float beatPhase = 0.0f;  // 0 at a beat, rising to 1 just before the next
};

// Real-time tempo detection:
//  1. onset strength = spectral flux of log-compressed magnitudes (one value per hop, ~94 per second)
//  2. every 0.5 s: autocorrelation of the last 8 s of onset strength over 50-240 BPM,
//     a comb over the first harmonic, and a mild preference for tempos near 120 BPM
//  3. octave folding into the chosen range (so 70 and 140 aren't flip-flopped), then TempoTracker
//  4. beat phase from aligning a pulse train with the onset strength
// Silent passages report no tempo instead of guessing.
class TempoAnalyzer
{
public:
    void prepare(std::uint32_t sampleRate);
    void reset() noexcept;

    // Results are folded into [minimumBpm, 2 * minimumBpm). 0 = no folding.
    // Changing it re-folds the current reading straight away.
    void setFoldRange(float minimumBpm) noexcept
    {
        if (minimumBpm == foldMinimum)
            return;
        foldMinimum = minimumBpm;
        tracker.refold([this](float bpm) { return fold(bpm); });
        result.bpm = tracker.bpm();
    }

    void process(const StereoFrame* frames, std::size_t count) noexcept;

    const TempoEstimate& latest() const noexcept { return result; }
    double onsetFramesPerSecond() const noexcept { return framesPerSecond; }

    // The most recent unsmoothed estimate (for tests).
    float rawBpm() const noexcept { return lastRawBpm; }
    bool rawValid() const noexcept { return lastRawValid; }

private:
    void computeOnset() noexcept;
    void estimate() noexcept;
    float fold(float bpm) const noexcept;

    std::uint32_t rate = 0;
    std::size_t fftSize = 0, hop = 0, mask = 0;
    double framesPerSecond = 0.0;
    float foldMinimum = 85.0f;

    std::unique_ptr<RealFft> fft;
    std::vector<float> history, window, frame, previousLogMagnitude;
    std::vector<std::complex<float>> bins;
    std::size_t writePosition = 0, framesSinceHop = 0;
    double hopEnergy = 0.0;

    // Onset strength and hop energy, as rings of `onsetCapacity` values.
    std::vector<float> onset, energy;
    std::size_t onsetCapacity = 0, onsetWrite = 0, onsetCount = 0;
    std::size_t hopsSinceEstimate = 0, hopsPerEstimate = 1;

    // Work buffers for estimate(), allocated in prepare().
    std::vector<float> envelope, smoothed, correlation;

    TempoTracker tracker;
    TempoEstimate result;
    float lastRawBpm = 0.0f;
    bool lastRawValid = false;
    double phase = 0.0; // fraction of a beat since the last beat
};

// Tap tempo: the median-guarded average of the last few tap intervals.
class TapTempo
{
public:
    void tap(double seconds) noexcept;
    void reset() noexcept { count = 0; }

    bool hasTempo() const noexcept { return count >= 3; } // at least two intervals
    float bpm() const noexcept;
    int tapCount() const noexcept { return count; }

private:
    static constexpr int kMaxTaps = 8;
    std::array<double, kMaxTaps> taps {};
    int count = 0;
};

} // namespace sonarc
