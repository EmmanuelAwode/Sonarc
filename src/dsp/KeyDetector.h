#pragma once

#include "audio/AudioTypes.h"
#include "dsp/Fft.h"

#include <array>
#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

namespace sonarc {

// Keys are numbered 0-11 for C, Db, D ... B major and 12-23 for C, C# ... B minor.
namespace keys {
    inline constexpr int kNone = -1;
    const char* name(int key) noexcept;     // e.g. "F# minor"
    const char* shortName(int key) noexcept; // e.g. "F#m", "Eb"
    const char* camelot(int key) noexcept;  // e.g. "11A"
    inline constexpr bool isMinor(int key) noexcept { return key >= 12; }
    inline constexpr int relative(int key) noexcept
    {
        return key < 12 ? 12 + (key + 9) % 12   // major -> relative minor (C -> Am)
                        : (key - 12 + 3) % 12;  // minor -> relative major (Am -> C)
    }
}

struct KeyEstimate
{
    bool valid = false;
    int key = keys::kNone;
    float confidence = 0.0f; // 0..1
};

// Real-time key detection:
//  1. large-FFT spectrum every ~85 ms; spectral peaks between 110 Hz and 4 kHz are
//     folded into a 12-bin chroma (weighted by how close they are to equal-tempered pitch)
//  2. frames are weighted by how tonal they are: silent, noisy (drums, hats) or
//     unfocused frames add little or nothing, so drum-only passages can't produce a key
//  3. chroma is accumulated with a ~20 s memory and correlated with the
//     Krumhansl-Kessler key profiles for all 24 keys
//  4. a hysteresis tracker only changes the shown key after the new one has led for ~4 s
class KeyDetector
{
public:
    void prepare(std::uint32_t sampleRate);
    void reset() noexcept;

    void process(const StereoFrame* frames, std::size_t count) noexcept;

    const KeyEstimate& latest() const noexcept { return result; }

    // Diagnostics (and tests).
    int rawKey() const noexcept { return lastRawKey; }
    double evidenceSeconds() const noexcept { return evidence * hopSeconds; }
    const std::array<double, 12>& chroma() const noexcept { return accumulated; }

private:
    void analyseFrame() noexcept;
    void estimate() noexcept;

    std::uint32_t rate = 0;
    std::size_t fftSize = 0, hop = 0, mask = 0;
    double hopSeconds = 0.0;

    std::unique_ptr<RealFft> fft;
    std::vector<float> history, window, frame, magnitude;
    std::vector<std::complex<float>> bins;
    std::size_t writePosition = 0, framesSinceHop = 0, hopsSinceEstimate = 0, hopsPerEstimate = 1;
    std::size_t firstBin = 1, lastBin = 1;

    std::array<double, 12> accumulated {};
    double evidence = 0.0; // decaying sum of frame tonality weights
    double decay = 1.0;

    // Tracker state
    int shownKey = keys::kNone;
    int candidateKey = keys::kNone;
    int candidateVotes = 0;
    int invalidRun = 0;
    int lastRawKey = keys::kNone;
    KeyEstimate result;
};

} // namespace sonarc
