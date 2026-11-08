#include "dsp/KeyDetector.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sonarc {

namespace keys {

namespace {
    // Index = tonic pitch class (C = 0).
    const char* const kMajorNames[] = { "C major", "Db major", "D major", "Eb major", "E major", "F major",
                                        "F# major", "G major", "Ab major", "A major", "Bb major", "B major" };
    const char* const kMinorNames[] = { "C minor", "C# minor", "D minor", "Eb minor", "E minor", "F minor",
                                        "F# minor", "G minor", "G# minor", "A minor", "Bb minor", "B minor" };
    const char* const kMajorShort[] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    const char* const kMinorShort[] = { "Cm", "C#m", "Dm", "Ebm", "Em", "Fm", "F#m", "Gm", "G#m", "Am", "Bbm", "Bm" };
    const char* const kMajorCamelot[] = { "8B", "3B", "10B", "5B", "12B", "7B", "2B", "9B", "4B", "11B", "6B", "1B" };
    const char* const kMinorCamelot[] = { "5A", "12A", "7A", "2A", "9A", "4A", "11A", "6A", "1A", "8A", "3A", "10A" };
}

const char* name(int key) noexcept
{
    return key < 0 || key > 23 ? "--" : key < 12 ? kMajorNames[key] : kMinorNames[key - 12];
}

const char* shortName(int key) noexcept
{
    return key < 0 || key > 23 ? "--" : key < 12 ? kMajorShort[key] : kMinorShort[key - 12];
}

const char* camelot(int key) noexcept
{
    return key < 0 || key > 23 ? "--" : key < 12 ? kMajorCamelot[key] : kMinorCamelot[key - 12];
}

} // namespace keys

namespace {

// Krumhansl & Kessler (1982) probe-tone key profiles, tonic first.
constexpr double kMajorProfile[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
constexpr double kMinorProfile[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

constexpr double kMemorySeconds = 20.0;
constexpr double kMinimumEvidenceSeconds = 3.0; // of clearly tonal audio, before any key is shown

double correlate(const std::array<double, 12>& chroma, const double* profile, int tonic) noexcept
{
    double meanC = 0.0, meanP = 0.0;
    for (int i = 0; i < 12; ++i)
    {
        meanC += chroma[(size_t) i];
        meanP += profile[i];
    }
    meanC /= 12.0;
    meanP /= 12.0;

    double sxy = 0.0, sxx = 0.0, syy = 0.0;
    for (int i = 0; i < 12; ++i)
    {
        const double x = chroma[(size_t) ((tonic + i) % 12)] - meanC; // chroma relative to the tonic
        const double y = profile[i] - meanP;
        sxy += x * y;
        sxx += x * x;
        syy += y * y;
    }
    return sxx > 0.0 && syy > 0.0 ? sxy / std::sqrt(sxx * syy) : 0.0;
}

} // namespace

void KeyDetector::prepare(std::uint32_t sampleRate)
{
    rate = sampleRate;

    // ~6 Hz frequency resolution: 8192 points at 44.1/48 kHz.
    fftSize = 1;
    while ((double) fftSize < sampleRate * 0.15)
        fftSize *= 2;
    hop = fftSize / 2;
    mask = fftSize - 1;
    hopSeconds = (double) hop / sampleRate;
    decay = std::exp(-hopSeconds / kMemorySeconds);
    hopsPerEstimate = std::max<std::size_t>(1, (std::size_t) std::lround(0.5 / hopSeconds));

    fft = std::make_unique<RealFft>((int) fftSize);
    history.assign(fftSize, 0.0f);
    frame.assign(fftSize, 0.0f);
    window.resize(fftSize);
    for (std::size_t i = 0; i < fftSize; ++i)
        window[i] = (float) (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * (double) i / (double) fftSize));
    bins.assign(fftSize / 2 + 1, {});
    magnitude.assign(fftSize / 2 + 1, 0.0f);

    const double hzPerBin = (double) sampleRate / (double) fftSize;
    firstBin = std::max<std::size_t>(2, (std::size_t) std::floor(110.0 / hzPerBin));
    lastBin = std::min(bins.size() - 2, (std::size_t) std::ceil(4000.0 / hzPerBin));

    reset();
}

void KeyDetector::reset() noexcept
{
    std::fill(history.begin(), history.end(), 0.0f);
    writePosition = framesSinceHop = hopsSinceEstimate = 0;
    accumulated.fill(0.0);
    evidence = 0.0;
    shownKey = candidateKey = lastRawKey = keys::kNone;
    candidateVotes = invalidRun = 0;
    result = {};
}

void KeyDetector::process(const StereoFrame* frames, std::size_t count) noexcept
{
    if (fft == nullptr)
        return;

    for (std::size_t i = 0; i < count; ++i)
    {
        history[writePosition] = 0.5f * (frames[i].left + frames[i].right);
        writePosition = (writePosition + 1) & mask;

        if (++framesSinceHop == hop)
        {
            framesSinceHop = 0;
            analyseFrame();
            if (++hopsSinceEstimate >= hopsPerEstimate)
            {
                hopsSinceEstimate = 0;
                estimate();
            }
        }
    }
}

void KeyDetector::analyseFrame() noexcept
{
    double energy = 0.0;
    for (std::size_t i = 0; i < fftSize; ++i)
    {
        const float x = history[(writePosition + i) & mask];
        energy += (double) x * x;
        frame[i] = x * window[i];
    }
    energy /= (double) fftSize;

    // Memory fades whether or not this frame contributes.
    for (auto& c : accumulated)
        c *= decay;
    evidence *= decay;

    if (energy < 1.0e-6) // below -60 dBFS: silence carries no key information
        return;

    fft->forward(frame.data(), bins.data());
    for (std::size_t k = firstBin - 1; k <= lastBin + 1; ++k)
        magnitude[k] = std::abs(bins[k]);

    // Spectral flatness in the analysed range: near 1 for noise (hats, cymbals), near 0 for tones.
    double logSum = 0.0, linearSum = 0.0, peakMagnitude = 0.0;
    for (std::size_t k = firstBin; k <= lastBin; ++k)
    {
        const double power = (double) magnitude[k] * magnitude[k] + 1.0e-20;
        logSum += std::log(power);
        linearSum += power;
        peakMagnitude = std::max(peakMagnitude, (double) magnitude[k]);
    }
    const double bandBins = (double) (lastBin - firstBin + 1);
    const double flatness = std::exp(logSum / bandBins) / (linearSum / bandBins);

    // Fold spectral peaks into pitch classes.
    std::array<double, 12> chroma {};
    const double hzPerBin = (double) rate / (double) fftSize;
    for (std::size_t k = firstBin; k <= lastBin; ++k)
    {
        const float m = magnitude[k];
        if (m < magnitude[k - 1] || m <= magnitude[k + 1] || m < 0.02 * peakMagnitude)
            continue;

        const double a = magnitude[k - 1], b = m, c = magnitude[k + 1];
        const double denominator = a - 2.0 * b + c;
        const double offset = std::abs(denominator) > 1.0e-12 ? std::clamp(0.5 * (a - c) / denominator, -0.5, 0.5) : 0.0;
        const double hz = ((double) k + offset) * hzPerBin;

        const double midi = 69.0 + 12.0 * std::log2(hz / 440.0);
        const double nearest = std::round(midi);
        const double cents = midi - nearest; // -0.5..0.5 semitones
        const double tuning = std::pow(std::cos(std::numbers::pi * cents), 2.0);
        const int pitchClass = (((int) nearest % 12) + 12) % 12;
        chroma[(size_t) pitchClass] += m * tuning;
    }

    double total = 0.0, maximum = 0.0;
    for (const auto c : chroma)
    {
        total += c;
        maximum = std::max(maximum, c);
    }
    if (total <= 0.0)
        return;

    // How much this frame should count: tonal (low flatness) and focused on a few pitch classes.
    const double tonality = std::clamp((0.35 - flatness) / 0.35, 0.0, 1.0);
    const double focus = std::clamp((maximum / (total / 12.0) - 1.5) / 2.0, 0.0, 1.0);
    const double weight = tonality * focus;
    if (weight <= 0.0)
        return;

    for (size_t i = 0; i < 12; ++i)
        accumulated[i] += weight * chroma[i] / total;
    evidence += weight;
}

void KeyDetector::estimate() noexcept
{
    int rawKey = keys::kNone;
    float rawConfidence = 0.0f;

    if (evidence * hopSeconds >= kMinimumEvidenceSeconds)
    {
        std::array<double, 24> scores {};
        for (int tonic = 0; tonic < 12; ++tonic)
        {
            scores[(size_t) tonic] = correlate(accumulated, kMajorProfile, tonic);
            scores[(size_t) tonic + 12] = correlate(accumulated, kMinorProfile, tonic);
        }

        int best = 0;
        for (int k = 1; k < 24; ++k)
            if (scores[(size_t) k] > scores[(size_t) best])
                best = k;

        double second = -1.0, secondUnrelated = -1.0;
        for (int k = 0; k < 24; ++k)
        {
            if (k == best)
                continue;
            second = std::max(second, scores[(size_t) k]);
            if (k != keys::relative(best))
                secondUnrelated = std::max(secondUnrelated, scores[(size_t) k]);
        }

        // Confidence: a clear lead over other keys (the relative major/minor shares every
        // note, so it is judged separately), a good absolute fit, and enough evidence.
        const double r = scores[(size_t) best];
        const double clarity = std::clamp((r - secondUnrelated) / 0.15, 0.0, 1.0);
        const double modeClarity = std::clamp((r - second) / 0.06, 0.0, 1.0);
        const double fit = std::clamp((r - 0.4) / 0.4, 0.0, 1.0);
        const double amount = std::clamp(evidence * hopSeconds / 10.0, 0.0, 1.0);
        rawConfidence = (float) ((0.6 * clarity + 0.4 * modeClarity) * fit * (0.5 + 0.5 * amount));

        if (rawConfidence >= 0.12f)
            rawKey = best;
    }

    lastRawKey = rawKey;

    // Hysteresis.
    if (rawKey == keys::kNone)
    {
        candidateVotes = 0;
        if (++invalidRun >= 10) // ~5 s without a usable estimate
            shownKey = keys::kNone;
    }
    else
    {
        invalidRun = 0;
        if (rawKey == shownKey)
        {
            candidateVotes = 0;
        }
        else
        {
            if (rawKey == candidateKey)
                ++candidateVotes;
            else
            {
                candidateKey = rawKey;
                candidateVotes = 1;
            }

            // First key after ~1.5 s of agreement; changing key needs ~4 s.
            if (candidateVotes >= (shownKey == keys::kNone ? 3 : 8))
            {
                shownKey = rawKey;
                candidateVotes = 0;
            }
        }
    }

    result.valid = shownKey != keys::kNone;
    result.key = shownKey;
    const float target = result.valid && rawKey == shownKey ? rawConfidence : 0.5f * result.confidence;
    result.confidence = result.valid ? result.confidence + 0.3f * (target - result.confidence) : 0.0f;
}

} // namespace sonarc
