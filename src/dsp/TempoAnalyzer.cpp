#include "dsp/TempoAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sonarc {

//==============================================================================
// TempoTracker

void TempoTracker::reset() noexcept
{
    historyCount = historyHead = 0;
    hasShown = false;
    shown = pendingBpm = 0.0f;
    pendingVotes = invalidRun = 0;
    rawConfidenceAverage = combinedConfidence = 0.0f;
}

bool TempoTracker::close(float a, float b) noexcept
{
    return b > 0.0f && std::abs(a - b) / b < 0.03f;
}

void TempoTracker::update(bool rawValid, float rawBpm, float rawConfidence) noexcept
{
    constexpr float kMinimumRawConfidence = 0.15f;

    if (! rawValid || rawConfidence < kMinimumRawConfidence)
    {
        rawConfidenceAverage *= 0.8f;
        combinedConfidence *= 0.8f;
        if (++invalidRun >= 8) // ~4 s without a usable estimate
            reset();
        return;
    }

    invalidRun = 0;
    history[(size_t) historyHead] = rawBpm;
    historyHead = (historyHead + 1) % kHistory;
    historyCount = std::min(historyCount + 1, kHistory);
    rawConfidenceAverage += 0.3f * (rawConfidence - rawConfidenceAverage);

    auto agreeingWith = [this](float value)
    {
        int agree = 0;
        for (int i = 0; i < historyCount; ++i)
            agree += close(history[(size_t) i], value) ? 1 : 0;
        return agree;
    };

    if (! hasShown)
    {
        // Need three consistent estimates (~1.5 s) before showing anything.
        if (agreeingWith(rawBpm) >= 3)
        {
            shown = rawBpm;
            hasShown = true;
        }
    }
    else if (close(rawBpm, shown))
    {
        shown += 0.2f * (rawBpm - shown);
        pendingVotes = 0;
    }
    else
    {
        if (pendingVotes > 0 && close(rawBpm, pendingBpm))
        {
            ++pendingVotes;
            pendingBpm += 0.5f * (rawBpm - pendingBpm);
        }
        else
        {
            pendingVotes = 1;
            pendingBpm = rawBpm;
        }

        if (pendingVotes >= 6) // the new tempo held for ~3 s
        {
            shown = pendingBpm;
            pendingVotes = 0;
        }
    }

    const float stability = hasShown && historyCount > 0 ? (float) agreeingWith(shown) / (float) historyCount : 0.0f;
    combinedConfidence = hasShown ? std::clamp(0.5f * rawConfidenceAverage + 0.5f * stability, 0.0f, 1.0f) : 0.0f;
}

//==============================================================================
// TempoAnalyzer

void TempoAnalyzer::prepare(std::uint32_t sampleRate)
{
    rate = sampleRate;

    // ~10.7 ms hops at any sample rate (512 at 44.1/48 kHz, 1024 at 96 kHz...).
    hop = 1;
    while ((double) (hop * 2) <= sampleRate / 93.75 + 0.5)
        hop *= 2;
    fftSize = hop * 2;
    mask = fftSize - 1;
    framesPerSecond = (double) sampleRate / (double) hop;

    fft = std::make_unique<RealFft>((int) fftSize);
    history.assign(fftSize, 0.0f);
    frame.assign(fftSize, 0.0f);
    window.resize(fftSize);
    double windowSum = 0.0;
    for (std::size_t i = 0; i < fftSize; ++i)
    {
        window[i] = (float) (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * (double) i / (double) fftSize));
        windowSum += window[i];
    }
    // Calibrate so the compression below behaves the same at every FFT size.
    for (auto& w : window)
        w = (float) (w * 2.0 / windowSum);

    bins.assign(fftSize / 2 + 1, {});
    previousLogMagnitude.assign(fftSize / 2 + 1, 0.0f);

    onsetCapacity = (std::size_t) std::ceil(8.0 * framesPerSecond); // 8 s of onset strength
    onset.assign(onsetCapacity, 0.0f);
    energy.assign(onsetCapacity, 0.0f);
    envelope.assign(onsetCapacity, 0.0f);
    smoothed.assign(onsetCapacity, 0.0f);
    correlation.assign(onsetCapacity, 0.0f);
    hopsPerEstimate = std::max<std::size_t>(1, (std::size_t) std::lround(framesPerSecond * 0.5));

    reset();
}

void TempoAnalyzer::reset() noexcept
{
    std::fill(history.begin(), history.end(), 0.0f);
    std::fill(previousLogMagnitude.begin(), previousLogMagnitude.end(), 0.0f);
    writePosition = framesSinceHop = 0;
    hopEnergy = 0.0;
    onsetWrite = onsetCount = hopsSinceEstimate = 0;
    tracker.reset();
    result = {};
    lastRawBpm = 0.0f;
    lastRawValid = false;
    phase = 0.0;
}

void TempoAnalyzer::process(const StereoFrame* frames, std::size_t count) noexcept
{
    if (fft == nullptr)
        return;

    for (std::size_t i = 0; i < count; ++i)
    {
        const float m = 0.5f * (frames[i].left + frames[i].right);
        history[writePosition] = m;
        writePosition = (writePosition + 1) & mask;
        hopEnergy += (double) m * m;

        if (++framesSinceHop == hop)
        {
            framesSinceHop = 0;
            computeOnset();

            // Advance the beat phase every hop so the beat animation runs smoothly between estimates.
            if (result.valid && result.bpm > 0.0f)
            {
                phase += result.bpm / 60.0 / framesPerSecond;
                phase -= std::floor(phase);
                result.beatPhase = (float) phase;
            }

            if (++hopsSinceEstimate >= hopsPerEstimate)
            {
                hopsSinceEstimate = 0;
                estimate();
            }
        }
    }
}

void TempoAnalyzer::computeOnset() noexcept
{
    for (std::size_t i = 0; i < fftSize; ++i)
        frame[i] = history[(writePosition + i) & mask] * window[i]; // oldest first

    fft->forward(frame.data(), bins.data());

    // Spectral flux: how much louder each bin got, on a compressed scale.
    double flux = 0.0;
    for (std::size_t k = 1; k < bins.size(); ++k)
    {
        const float compressed = std::log1p(100.0f * std::abs(bins[k]));
        flux += std::max(0.0f, compressed - previousLogMagnitude[k]);
        previousLogMagnitude[k] = compressed;
    }

    onset[onsetWrite] = (float) flux;
    energy[onsetWrite] = (float) (hopEnergy / (double) hop);
    hopEnergy = 0.0;
    onsetWrite = (onsetWrite + 1) % onsetCapacity;
    onsetCount = std::min(onsetCount + 1, onsetCapacity);
}

float TempoAnalyzer::fold(float bpm) const noexcept
{
    if (foldMinimum <= 0.0f || bpm <= 0.0f)
        return bpm;

    // The working range starts 2 % below the chosen minimum, so a song right at
    // the minimum (85 BPM in "85-170") can't flip to double on tiny estimation
    // differences. A song at exactly the maximum (170) still reads as half (85).
    const float low = foldMinimum * 0.98f;
    while (bpm < low)
        bpm *= 2.0f;
    while (bpm >= low * 2.0f)
        bpm *= 0.5f;
    return bpm;
}

void TempoAnalyzer::estimate() noexcept
{
    const auto length = onsetCount;
    lastRawValid = false;

    const double fps = framesPerSecond;
    bool rawValid = length >= (std::size_t) (4.0 * fps); // need ~4 s of history

    // Oldest first.
    const auto recentFrames = std::min(length, (std::size_t) (1.5 * fps));
    double recentEnergy = 0.0;
    for (std::size_t n = 0; n < length; ++n)
    {
        const auto index = (onsetWrite + onsetCapacity - length + n) % onsetCapacity;
        envelope[n] = onset[index];
        if (n >= length - recentFrames)
            recentEnergy += energy[index];
    }
    recentEnergy /= (double) std::max<std::size_t>(1, recentFrames);

    // The last 1.5 s are silent (below -60 dBFS RMS): no tempo, and forget the old one now.
    if (recentEnergy < 1.0e-6)
    {
        tracker.reset();
        result = {};
        lastRawValid = false;
        lastRawBpm = 0.0f;
        return;
    }

    float rawBpm = 0.0f, rawConfidence = 0.0f;
    if (rawValid)
    {
        // Keep only what rises above the local average (the onsets), then remove the mean.
        constexpr int half = 8;
        double running = 0.0;
        for (std::size_t n = 0; n < length; ++n)
        {
            const auto from = n >= (std::size_t) half ? n - half : 0;
            const auto to = std::min(length - 1, n + half);
            running = 0.0;
            for (auto j = from; j <= to; ++j)
                running += envelope[j];
            smoothed[n] = std::max(0.0f, envelope[n] - (float) (running / (double) (to - from + 1)));
        }
        double mean = 0.0;
        for (std::size_t n = 0; n < length; ++n)
            mean += smoothed[n];
        mean /= (double) length;
        double r0 = 0.0;
        for (std::size_t n = 0; n < length; ++n)
        {
            smoothed[n] -= (float) mean;
            r0 += (double) smoothed[n] * smoothed[n];
        }

        if (r0 <= 1.0e-12)
            rawValid = false;

        if (rawValid)
        {
            // Normalised, unbiased autocorrelation. Lags beyond 60 % of the history rest
            // on too few samples (and the unbiasing inflates them), so stop there.
            const auto maxLag = std::min(length - 1, (std::size_t) (0.6 * (double) length));
            for (std::size_t lag = 1; lag <= maxLag; ++lag)
            {
                double sum = 0.0;
                for (std::size_t n = lag; n < length; ++n)
                    sum += (double) smoothed[n] * smoothed[n - lag];
                correlation[lag] = (float) (sum / r0 * (double) length / (double) (length - lag));
            }

            // The period rarely falls on a whole number of frames, so read each
            // correlation as the best value within one lag of where it should be.
            auto peakNear = [&](double lag)
            {
                if (lag + 1.0 > (double) maxLag)
                    return 0.0; // not enough history yet to judge this far out
                const auto from = (std::size_t) std::max(1.0, std::floor(lag - 1.0));
                const auto to = (std::size_t) std::min((double) maxLag, std::ceil(lag + 1.0));
                float best = correlation[from];
                for (auto k = from + 1; k <= to; ++k)
                    best = std::max(best, correlation[k]);
                return (double) best;
            };

            // Candidate beats: every period inside the chosen tempo range (one octave),
            // in quarter-frame steps. Each is scored by how strongly the onsets repeat at
            // *every* multiple of it (1, 2, 3, 4... beats, as far back as the history
            // allows), plus a little for activity at the half beat (8th notes). A real
            // beat lines up with all of them, even when the pattern itself only repeats
            // every 2 beats or every bar; a 3/4- or 1.5-beat pulse (3-3-2 rhythms,
            // dotted patterns) only lines up with some. A mild preference for tempos
            // near 120 BPM breaks near-ties.
            const double low = foldMinimum > 0.0f ? foldMinimum * 0.98 : 83.3;
            const double shortest = 60.0 * fps / (2.0 * low);
            const double longest = 60.0 * fps / low;

            double best = -1.0e9, bestPeriod = shortest, bestComb = 0.0;
            for (double period = shortest; period <= longest; period += 0.25)
            {
                double sum = 0.0, weight = 0.0;
                for (int k = 1; k * period + 1.0 <= (double) maxLag; ++k)
                {
                    const double w = k <= 4 ? 1.0 : 0.5; // the first bar matters most
                    sum += w * peakNear(k * period);
                    weight += w;
                }
                if (weight <= 0.0)
                    continue;

                const double comb = sum / weight;
                const double bpm = 60.0 * fps / period;
                const double prior = std::exp(-0.5 * std::pow(std::log2(bpm / 120.0) / 1.5, 2.0));
                const double score = (comb + 0.25 * peakNear(0.5 * period)) * prior;
                if (score > best)
                {
                    best = score;
                    bestPeriod = period;
                    bestComb = comb;
                }
            }

            // Refine on the strongest of the first few multiples (the beat itself may
            // repeat only weakly), with parabolic interpolation around its peak.
            int bestMultiple = 1;
            double strongest = -1.0e9;
            for (int k = 1; k <= 4 && k * bestPeriod + 1.0 <= (double) maxLag; ++k)
                if (const double r = peakNear(k * bestPeriod); r > strongest)
                {
                    strongest = r;
                    bestMultiple = k;
                }

            auto peak = (std::size_t) std::lround(bestMultiple * bestPeriod);
            for (const auto neighbour : { peak - 1, peak + 1 })
                if (neighbour >= 1 && neighbour < maxLag && correlation[neighbour] > correlation[peak])
                    peak = neighbour;

            double refinedLag = (double) peak;
            if (peak > 1 && peak < maxLag)
            {
                const double a = correlation[peak - 1], b = correlation[peak], c = correlation[peak + 1];
                const double denominator = a - 2.0 * b + c;
                if (std::abs(denominator) > 1.0e-9)
                    refinedLag += std::clamp(0.5 * (a - c) / denominator, -0.5, 0.5);
            }

            // How clearly periodic the onsets are. For random onsets the correlation at any
            // lag is about +/-1/sqrt(length) (~0.04 over 8 s), and averaging over the
            // multiples shrinks that further; a real beat correlates far more strongly.
            rawConfidence = (float) std::clamp((bestComb - 0.2) / 0.35, 0.0, 1.0);
            rawBpm = fold((float) (60.0 * fps * bestMultiple / refinedLag));
            rawValid = rawConfidence > 0.0f && std::isfinite(rawBpm);

            // Beat phase: the offset of a pulse train (at the folded tempo) that best lines up with the onsets.
            if (rawValid)
            {
                const double period = 60.0 * fps / rawBpm;
                const int pulses = std::max(1, (int) (4.0 * fps / period)); // last ~4 s
                double bestSum = -1.0e9, bestOffset = 0.0;
                for (double offset = 0.0; offset < period; offset += 0.25)
                {
                    double sum = 0.0;
                    for (int k = 0; k < pulses; ++k)
                    {
                        const double position = (double) (length - 1) - offset - k * period;
                        if (position < 1.0)
                            break;
                        const auto i = (std::size_t) position;
                        const double t = position - (double) i;
                        sum += smoothed[i] + t * (smoothed[i + 1 < length ? i + 1 : i] - smoothed[i]);
                    }
                    if (sum > bestSum)
                    {
                        bestSum = sum;
                        bestOffset = offset;
                    }
                }
                phase = bestOffset / period; // fraction of a beat since the most recent beat
            }
        }
    }

    lastRawValid = rawValid;
    lastRawBpm = rawBpm;
    tracker.update(rawValid, rawBpm, rawConfidence);

    result.valid = tracker.valid();
    result.bpm = tracker.bpm();
    result.confidence = tracker.confidence();
    result.beatPhase = (float) phase;
}

//==============================================================================
// TapTempo

void TapTempo::tap(double seconds) noexcept
{
    if (count > 0)
    {
        const double sinceLast = seconds - taps[(size_t) count - 1];
        if (sinceLast < 0.15) // bounce
            return;
        if (sinceLast > 2.0) // a pause starts a new measurement
            count = 0;
    }

    if (count == kMaxTaps)
    {
        std::rotate(taps.begin(), taps.begin() + 1, taps.end());
        --count;
    }
    taps[(size_t) count++] = seconds;
}

float TapTempo::bpm() const noexcept
{
    if (count < 2)
        return 0.0f;

    std::array<double, kMaxTaps> intervals {};
    const int n = count - 1;
    for (int i = 0; i < n; ++i)
        intervals[(size_t) i] = taps[(size_t) i + 1] - taps[(size_t) i];

    auto sorted = intervals;
    std::sort(sorted.begin(), sorted.begin() + n);
    const double median = sorted[(size_t) n / 2];

    // Average the taps that agree with the median (drops the odd mistimed tap).
    double sum = 0.0;
    int used = 0;
    for (int i = 0; i < n; ++i)
        if (std::abs(intervals[(size_t) i] - median) <= 0.2 * median)
        {
            sum += intervals[(size_t) i];
            ++used;
        }

    return used > 0 ? (float) (60.0 / (sum / used)) : 0.0f;
}

} // namespace sonarc
