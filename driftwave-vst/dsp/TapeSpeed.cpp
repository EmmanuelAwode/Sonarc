#include "dsp/TapeSpeed.h"

#include "dsp/Biquad.h"

#include <algorithm>
#include <cmath>

namespace driftwave {

namespace {
constexpr float kSilence = 1.0e-5f;       // -100 dBFS
constexpr float kSpeedGlideSeconds = 0.12f; // a little inertia, like a tape transport
constexpr double kFadeSeconds = 0.03;

// Interpolation filter cutoffs, relative to Nyquist. The first leaves room for the
// filter's transition band below Nyquist; the others are for reading faster than
// live, where everything above Nyquist / speed would alias.
constexpr double kCutoffs[3] = { 0.95, 0.74, 0.61 };
constexpr double kKaiserBeta = 8.6; // about 85 dB of stop-band attenuation

double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 40; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < sum * 1e-14)
            break;
    }
    return sum;
}
} // namespace

void TapeSpeed::prepare(double sr)
{
    sampleRate = sr;
    maxLag = std::floor(kMaxLagSeconds * sr);
    size = int64_t(maxLag) + 2 * kTaps; // room for the interpolator's neighbours
    for (auto& b : buffer)
        b.assign(size_t(size), 0.0f);
    fadeLength = std::max(1, int(kFadeSeconds * sr));
    speedSm.setTime(kSpeedGlideSeconds, float(sr));
    syncSm.setTime(0.02f, float(sr));
    syncBase = std::max(kSyncBaseSeconds * sr, double(kHalfTaps + 8)); // the sinc always has its neighbours
    syncSpan = kSyncSpanSeconds * sr;
    syncLimit = kSyncLimitSeconds * sr;
    spliceLength = std::max(8, int(kSyncSpliceSeconds * sr));
    minJump = 0.004 * sr;   // a splice has to be worth making
    minRange = 0.012 * sr;  // and needs some choice of where to land, or it cannot line up
    onsetPre = 0.003 * sr;  // a hit starts a little before it is noticed
    onsetBody = 0.012 * sr; // and its attack lasts about this long
    fastRelease = float(std::exp(-1.0 / (0.030 * sr)));
    slowRate = float(1.0 - std::exp(-1.0 / (0.060 * sr)));
    onsetHoldLength = int(0.030 * sr);
    spliceRetry = std::max(1, int(0.003 * sr));

    // Row p holds the taps for a read position p / kPhases of the way from one
    // sample to the next; tap t weights the sample (t - (kHalfTaps - 1)) newer.
    const double i0Beta = besselI0(kKaiserBeta);
    for (int table = 0; table < kTables; ++table) {
        auto& taps = sincTables[table];
        taps.assign(size_t((kPhases + 1) * kTaps), 0.0f);
        const double cutoff = kCutoffs[table];
        for (int p = 0; p <= kPhases; ++p) {
            double row[kTaps];
            double sum = 0.0;
            for (int t = 0; t < kTaps; ++t) {
                const double x = double(t - (kHalfTaps - 1)) - double(p) / kPhases;
                const double w = x / double(kHalfTaps);
                const double window = std::abs(w) < 1.0 ? besselI0(kKaiserBeta * std::sqrt(1.0 - w * w)) / i0Beta : 0.0;
                const double arg = kPi * cutoff * x;
                row[t] = (std::abs(arg) < 1e-9 ? 1.0 : std::sin(arg) / arg) * cutoff * window;
                sum += row[t];
            }
            for (int t = 0; t < kTaps; ++t) // unity gain at DC for every phase
                taps[size_t(p * kTaps + t)] = float(row[t] / sum);
        }
    }
    sinc = sincTables[0].data();

    const int refLength = int(kSyncRefSeconds * sr) / kSearchStep;
    searchRef.assign(size_t(refLength), 0.0f);
    searchArea.assign(size_t(refLength + int(kSyncLimitSeconds * sr) / kSearchStep + 16), 0.0f);

    reset();
}

void TapeSpeed::reset() noexcept
{
    // The buffer itself is not cleared (it is large): `written` keeps the read
    // heads away from anything recorded before this point.
    writeIndex = 0;
    written = 0;
    lag = fadeLag = 0.0;
    syncRunning = false;
    spliceLeft = 0;
    spliceWait = 0;
    clock = 0;
    for (auto& onset : onsets)
        onset = -(int64_t(1) << 40);
    nextOnset = 0;
    envFast = envSlow = 0.0f;
    onsetHold = 0;
    fadeLeft = 0;
    silentRun = 0;
    resyncPending = false;
    needsSnap = true;
}

void TapeSpeed::setTarget(bool isOn, float speed, bool isSync) noexcept
{
    on = isOn;
    sync = isOn && (isSync || speed > 1.0f);
    targetSpeed = isOn ? std::clamp(speed, kMinSpeed, kMaxSpeed) : 1.0f;
}

float TapeSpeed::at(int channel, int64_t samplesBack) const noexcept
{
    // Nothing newer than the sample just written exists yet, and nothing older
    // than the last reset: hold the nearest valid sample instead.
    int64_t i = writeIndex - std::clamp<int64_t>(samplesBack, 0, written);
    if (i < 0)
        i += size;
    return buffer[channel][size_t(i)];
}

void TapeSpeed::read(double lagSamples, float& outL, float& outR) const noexcept
{
    const double whole = std::ceil(lagSamples);
    const auto back = int64_t(whole);
    const auto frac = float(whole - lagSamples); // 0..1 from sample `back` towards newer ones

    // Exactly on a sample nothing needs interpolating - unless the band is being narrowed
    // for a speed above 1, where the filter still has to run.
    if (frac == 0.0f && sinc == sincTables[0].data()) {
        outL = at(0, back);
        outR = at(1, back);
        return;
    }

    // Windowed sinc, when all of its taps exist: kHalfTaps newer samples and kHalfTaps older ones.
    if (back >= kHalfTaps && back + kHalfTaps <= written) {
        const float phase = frac * float(kPhases);
        const int row = std::min(int(phase), kPhases - 1);
        const float blend = phase - float(row);
        const float* a = sinc + row * kTaps;
        const float* b = a + kTaps;

        int64_t index = writeIndex - (back + kHalfTaps - 1); // the oldest tap
        if (index < 0)
            index += size;
        const float* bufL = buffer[0].data();
        const float* bufR = buffer[1].data();
        float sumL = 0.0f, sumR = 0.0f;
        if (index + kTaps <= size) {
            // The usual case, with the taps in one run: four partial sums per channel
            // so the compiler can keep them in vector registers.
            const float* l = bufL + index;
            const float* r = bufR + index;
            float l0 = 0.0f, l1 = 0.0f, l2 = 0.0f, l3 = 0.0f, r0 = 0.0f, r1 = 0.0f, r2 = 0.0f, r3 = 0.0f;
            for (int t = 0; t < kTaps; t += 4) {
                const float w0 = a[t] + (b[t] - a[t]) * blend, w1 = a[t + 1] + (b[t + 1] - a[t + 1]) * blend;
                const float w2 = a[t + 2] + (b[t + 2] - a[t + 2]) * blend, w3 = a[t + 3] + (b[t + 3] - a[t + 3]) * blend;
                l0 += w0 * l[t];     r0 += w0 * r[t];
                l1 += w1 * l[t + 1]; r1 += w1 * r[t + 1];
                l2 += w2 * l[t + 2]; r2 += w2 * r[t + 2];
                l3 += w3 * l[t + 3]; r3 += w3 * r[t + 3];
            }
            sumL = (l0 + l1) + (l2 + l3);
            sumR = (r0 + r1) + (r2 + r3);
        } else {
            for (int t = 0; t < kTaps; ++t) { // the taps wrap round the end of the buffer
                const float w = a[t] + (b[t] - a[t]) * blend;
                sumL += w * bufL[index];
                sumR += w * bufR[index];
                if (++index == size)
                    index = 0;
            }
        }
        outL = sumL;
        outR = sumR;
        return;
    }

    // Right next to the live input (or just after a reset) there are not enough
    // neighbours for the sinc: 4-point Hermite, which needs only two newer samples.
    float out[2];
    for (int ch = 0; ch < 2; ++ch) {
        const float xm1 = at(ch, back + 1), x0 = at(ch, back), x1 = at(ch, back - 1), x2 = at(ch, back - 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        out[ch] = ((c3 * frac + c2) * frac + c1) * frac + x0;
    }
    outL = out[0];
    outR = out[1];
}

/** Finds where between `nearest` and `furthest` (delays behind the input) a new
    read head should start so that what it plays lines up with what the head at
    `playingDelay` is playing. Runs on a mono, 4x decimated copy of the recent
    signal and then refines to the exact sample. `match` is how well the two line
    up there: 1 is a perfect fit, 0 none (or nothing to compare). */
double TapeSpeed::alignSplice(double nearest, double furthest, double playingDelay, bool preferNearest, float& match) noexcept
{
    match = 1.0f; // silence fits anywhere
    const int length = int(searchRef.size());
    const auto refBack = int64_t(playingDelay);
    const auto areaBack = int64_t(nearest);
    const double fraction = playingDelay - double(refBack);
    const int span = std::max(0, int(furthest - nearest));
    const int offsets = std::min(span / kSearchStep + 1, int(searchArea.size()) - length);
    const double fallback = double(areaBack + (preferNearest ? 0 : span)) + fraction;
    if (length < 8 || offsets < 1)
        return fallback;

    // Both heads move through the buffer at the same speed, so comparing the
    // stretch each has just passed tells how well they will overlap from here on.
    const auto block = [this](int64_t back) {
        float sum = 0.0f;
        for (int d = 0; d < kSearchStep; ++d)
            sum += at(0, back + d) + at(1, back + d);
        return sum;
    };
    float refEnergy = 0.0f;
    for (int k = 0; k < length; ++k) {
        searchRef[size_t(k)] = block(refBack + int64_t(k) * kSearchStep);
        refEnergy += searchRef[size_t(k)] * searchRef[size_t(k)];
    }
    if (refEnergy < 1e-9f)
        return fallback;
    for (int k = 0; k < offsets + length; ++k)
        searchArea[size_t(k)] = block(areaBack + int64_t(k) * kSearchStep);

    float energy = 0.0f;
    for (int k = 0; k < length; ++k)
        energy += searchArea[size_t(k)] * searchArea[size_t(k)];

    int best = preferNearest ? 0 : offsets - 1;
    float bestScore = -1.0e30f;
    match = 0.0f;
    for (int offset = 0; offset < offsets; ++offset) {
        float dot = 0.0f;
        const float* area = searchArea.data() + offset;
        for (int k = 0; k < length; ++k)
            dot += searchRef[size_t(k)] * area[k];
        // Normalised correlation, with a slight preference for the longest jump (fewer splices).
        const float away = float(preferNearest ? offset : offsets - 1 - offset) / float(offsets);
        const float fit = dot / std::sqrt(energy * refEnergy + 1e-12f);
        const float score = fit - 0.03f * away;
        if (score > bestScore) {
            bestScore = score;
            best = offset;
            match = fit;
        }
        if (offset + 1 < offsets) {
            energy += area[length] * area[length] - area[0] * area[0];
            energy = std::max(energy, 0.0f);
        }
    }

    // Refine to the exact sample around the coarse winner, so that mid and high
    // frequencies line up as well and the splice does not dull them.
    const auto mono = [this](int64_t back) { return at(0, back) + at(1, back); };
    const int fineLength = std::min(length * kSearchStep, 512);
    const int coarse = best * kSearchStep;
    int fine = coarse;
    float fineScore = -1.0e30f;
    for (int offset = std::max(0, coarse - (kSearchStep - 1)); offset <= std::min(span, coarse + (kSearchStep - 1)); ++offset) {
        float dot = 0.0f, e = 1e-12f;
        for (int k = 0; k < fineLength; ++k) {
            const float c = mono(areaBack + offset + k);
            dot += mono(refBack + k) * c;
            e += c * c;
        }
        const float score = dot / std::sqrt(e);
        if (score > fineScore) {
            fineScore = score;
            fine = offset;
        }
    }
    return double(areaBack + fine) + fraction;
}

/** Hands over to a new read head, if there is a good place for one. Slower than
    live the new head starts nearer the input and the stretch between the two is
    skipped; faster than live it starts further back and that stretch is heard
    twice. That stretch should not hold a drum hit, and the two heads should line
    up; unless `forced`, a splice that cannot have both is not made yet. */
bool TapeSpeed::trySplice(double slip, bool forced) noexcept
{
    const bool slower = slip > 0.0;
    const double current = headDelay;
    double nearest = slower ? syncBase : current + minJump;
    double furthest = slower ? current - minJump : syncBase + syncLimit;

    {
        const double openNearest = nearest, openFurthest = furthest;
        // How much of the source a head passes while the crossfade lasts.
        const double fadeTravel = double(spliceLength) * (1.0 - slip);
        for (const int64_t onset : onsets) {
            const double hit = double(clock - onset); // how far behind the input the hit is now
            if (slower) {
                // A hit the old head has not finished playing must be left for the new one, which
                // has to start early enough before it to have faded in.
                if (hit - onsetBody < current)
                    nearest = std::max(nearest, hit + onsetPre + fadeTravel);
            } else if (hit > current - onsetPre) {
                // A hit the old head has played must not be played again.
                furthest = std::min(furthest, hit - onsetBody);
            } else if (current - hit < fadeTravel + onsetPre && !forced) {
                return false; // the old head is about to play a hit: let it, at full level
            }
        }
        if (furthest - nearest < (forced ? 0.0 : minRange)) {
            if (!forced)
                return false;
            nearest = openNearest; // out of room: splice wherever lines up best
            furthest = openFurthest;
        }
    }
    furthest = std::max(furthest, nearest);

    float match = 0.0f;
    const double start = alignSplice(nearest, furthest, current, slower, match);
    // A poor fit means a held note would jump here. Look again in a moment - but only for the
    // first half of the spare room, so that the rest is still there for keeping clear of drum hits.
    const double spare = 0.5 * (syncLimit - syncSpan);
    const bool canWait = slower ? current < syncBase + syncSpan + spare : current > syncBase + spare;
    if (!forced && canWait && match < 0.5f) {
        spliceWait = spliceRetry;
        return false;
    }
    oldDelay = current;
    headDelay = start;
    spliceLeft = spliceLength;
    return true;
}

void TapeSpeed::process(float* left, float* right, int numSamples) noexcept
{
    if (size == 0)
        return;

    if (needsSnap) {
        speedSm.snap(targetSpeed);
        syncSm.snap(sync ? 1.0f : 0.0f);
        needsSnap = false;
    }

    // Fully live: pass the audio through untouched, but keep recording so a
    // speed change can start cleanly at any moment.
    if (!on && lag == 0.0 && fadeLeft == 0 && speedSm.current() == 1.0f) {
        for (int i = 0; i < numSamples; ++i) {
            buffer[0][size_t(writeIndex)] = left[i];
            buffer[1][size_t(writeIndex)] = right[i];
            if (++writeIndex == size)
                writeIndex = 0;
        }
        written = std::min<int64_t>(written + numSamples, size - 1);
        clock += numSamples;
        syncRunning = false;
        resyncPending = false;
        silentRun = 0;
        return;
    }

    // Reading faster than live needs a narrower interpolation filter so nothing aliases.
    const float speedNow = std::max(speedSm.current(), targetSpeed);
    sinc = sincTables[speedNow > 1.25f ? 2 : speedNow > 1.0005f ? 1 : 0].data();

    for (int i = 0; i < numSamples; ++i) {
        const float inL = left[i], inR = right[i];
        buffer[0][size_t(writeIndex)] = inL;
        buffer[1][size_t(writeIndex)] = inR;
        silentRun = (std::abs(inL) < kSilence && std::abs(inR) < kSilence) ? silentRun + 1 : 0;

        // Drum hits: the level jumping well above where it has been lately.
        const float level = std::abs(inL) + std::abs(inR);
        envFast = std::max(level, envFast * fastRelease);
        if (onsetHold > 0)
            --onsetHold;
        else if (envFast > 1.8f * envSlow + 1.0e-3f) {
            onsets[nextOnset] = clock;
            nextOnset = (nextOnset + 1) % kOnsets;
            onsetHold = onsetHoldLength;
        }
        envSlow += (envFast - envSlow) * slowRate;

        const double slip = 1.0 - double(speedSm.next(targetSpeed));
        // Only a switched-on tape falls behind; while switching off, the speed just glides back to 1.
        if (on && !sync)
            lag = std::max(0.0, lag + slip); // the speed may still be gliding down from above 1

        float outL, outR;
        read(lag, outL, outR);

        if (fadeLeft > 0) {
            fadeLag = std::min(fadeLag + slip, maxLag);
            float oldL, oldR;
            read(fadeLag, oldL, oldR);
            const float t = float(fadeLeft) / float(fadeLength); // weight of the old head
            outL += (oldL - outL) * t;
            outR += (oldR - outR) * t;
            --fadeLeft;
        }

        // Sync mode: one read head plays at the new speed and drifts away from (slower) or
        // towards (faster) the live input, until a new head takes over with a short crossfade.
        const float syncAmount = syncSm.next(sync ? 1.0f : 0.0f);
        if (syncAmount <= 0.0f) {
            syncRunning = false;
        } else if (slip != 0.0) {
            if (!syncRunning) {
                syncRunning = true;
                headDelay = slip > 0.0 ? syncBase : syncBase + syncSpan;
                spliceLeft = 0;
            }
            headDelay = std::clamp(headDelay + slip, double(kHalfTaps), syncBase + syncLimit + minJump);
            if (spliceLeft > 0)
                oldDelay = std::clamp(oldDelay + slip, double(kHalfTaps), syncBase + syncLimit + minJump);
            else if (slip > 0.0 ? headDelay >= syncBase + syncSpan : headDelay <= syncBase + syncLimit - syncSpan) {
                const bool forced = slip > 0.0 ? headDelay >= syncBase + syncLimit : headDelay <= syncBase;
                if (forced || --spliceWait <= 0)
                    trySplice(slip, forced);
            }

            // Near 100% speed the head hardly moves: blend to the plain signal.
            const float amount = syncAmount * float(std::min(std::abs(slip) * 50.0, 1.0));
            if (amount > 0.0f) {
                float headL, headR;
                read(headDelay, headL, headR);
                if (spliceLeft > 0) {
                    float oldL, oldR;
                    read(oldDelay, oldL, oldR);
                    float t = float(spliceLeft) / float(spliceLength); // weight of the old head
                    t = t * t * (3.0f - 2.0f * t);                       // smooth the corners
                    headL += (oldL - headL) * t;
                    headR += (oldR - headR) * t;
                }
                outL += (headL - outL) * amount;
                outR += (headR - outR) * amount;
            }
            if (spliceLeft > 0)
                --spliceLeft;
        }

        left[i] = outL;
        right[i] = outR;

        if (fadeLeft == 0) {
            if (lag == 0.0) {
                resyncPending = false;
            } else if (double(silentRun) > lag + double(kTaps)) {
                // Everything still waiting to be played is silence: catch up inaudibly.
                lag = 0.0;
                resyncPending = false;
            } else if (resyncPending || !on || sync || lag >= maxLag) {
                fadeLag = lag;
                lag = 0.0;
                fadeLeft = fadeLength;
                resyncPending = false;
            }
        }

        if (++writeIndex == size)
            writeIndex = 0;
        if (written < size - 1)
            ++written;
        ++clock;
    }
}

} // namespace driftwave
