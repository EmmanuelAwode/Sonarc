// DSP unit tests. No test framework: each CHECK prints on failure and the
// process exits non-zero if anything failed, which is all CTest needs.

#include "dsp/EffectChain.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace driftwave;

namespace {

int failures = 0;
int checks = 0;

#define CHECK(cond, ...)                                             \
    do {                                                             \
        ++checks;                                                    \
        if (!(cond)) {                                               \
            ++failures;                                              \
            std::printf("  FAIL %s:%d  %s\n       ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                                \
            std::printf("\n");                                       \
        }                                                            \
    } while (false)

struct Rng {
    uint32_t state = 0x1234abcdu;
    float next() // -1..1
    {
        state = state * 1664525u + 1013904223u;
        return float(state >> 8) / 8388608.0f - 1.0f;
    }
};

double toDb(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }

/** Amplitude of one frequency in a signal (Goertzel-style correlation). */
double toneAmplitude(const std::vector<float>& x, size_t from, double hz, double sr)
{
    double re = 0.0, im = 0.0;
    for (size_t i = from; i < x.size(); ++i) {
        const double ph = 2.0 * kPi * hz * double(i) / sr;
        re += x[i] * std::cos(ph);
        im += x[i] * std::sin(ph);
    }
    return 2.0 * std::sqrt(re * re + im * im) / double(x.size() - from);
}

void processInBlocks(EffectChain& chain, std::vector<float>& l, std::vector<float>& r, int block = 480)
{
    for (size_t pos = 0; pos < l.size(); pos += size_t(block)) {
        const int n = int(std::min(size_t(block), l.size() - pos));
        chain.process(l.data() + pos, r.data() + pos, n);
    }
}

ChainParams neutralParams()
{
    ChainParams p;
    p.reverbOn = false;
    p.bassOn = false;
    p.clipOn = false;
    p.outputGainDb = 0.0f;
    return p;
}

// ---------------------------------------------------------------------------

void testSilenceAndPassThrough()
{
    std::printf("silence / pass-through\n");
    EffectChain chain;
    chain.prepare(48000.0);

    ChainParams all;
    all.reverbOn = true;
    all.bassOn = true;
    all.bassDrive = 1.0f;
    chain.setParams(all);
    std::vector<float> l(48000, 0.0f), r(48000, 0.0f);
    processInBlocks(chain, l, r);
    float peak = 0.0f;
    for (size_t i = 0; i < l.size(); ++i) peak = std::max({ peak, std::abs(l[i]), std::abs(r[i]) });
    CHECK(peak == 0.0f, "silence in must give silence out, peak %g", double(peak));

    // Everything off: output must equal input, with no delay.
    chain.reset();
    chain.setParams(neutralParams());
    Rng rng;
    std::vector<float> inL(4800), inR(4800);
    for (size_t i = 0; i < inL.size(); ++i) { inL[i] = 0.5f * rng.next(); inR[i] = 0.5f * rng.next(); }
    auto outL = inL, outR = inR;
    processInBlocks(chain, outL, outR, 64);
    float maxErr = 0.0f;
    for (size_t i = 0; i < inL.size(); ++i)
        maxErr = std::max({ maxErr, std::abs(outL[i] - inL[i]), std::abs(outR[i] - inR[i]) });
    CHECK(maxErr < 1.0e-5f, "bypassed chain should be transparent, max error %g", double(maxErr));
}

void testBassShelf()
{
    std::printf("bass shelf response\n");
    const double sr = 48000.0;
    for (float gainDb : { 6.0f, 12.0f, 18.0f }) {
        for (double hz : { 20.0, 5000.0 }) {
            EffectChain chain;
            chain.prepare(sr);
            auto p = neutralParams();
            p.bassOn = true;
            p.bassGainDb = gainDb;
            p.bassFreqHz = 150.0f;
            chain.setParams(p);
            std::vector<float> l(48000), r(48000);
            for (size_t i = 0; i < l.size(); ++i)
                l[i] = r[i] = 0.05f * float(std::sin(2.0 * kPi * hz * double(i) / sr));
            processInBlocks(chain, l, r);
            const double db = toDb(toneAmplitude(l, 24000, hz, sr) / 0.05);
            const double expected = hz < 100.0 ? gainDb : 0.0;
            CHECK(std::abs(db - expected) < 1.0, "%.0f Hz with +%.0f dB shelf: got %.2f dB, expected %.2f",
                  hz, double(gainDb), db, expected);
        }
    }
}

void testBassDrive()
{
    std::printf("bass drive harmonics\n");
    const double sr = 48000.0, hz = 60.0;
    double third[2] = {};
    for (int pass = 0; pass < 2; ++pass) {
        EffectChain chain;
        chain.prepare(sr);
        auto p = neutralParams();
        p.bassOn = true;
        p.bassGainDb = 0.0f;
        p.bassFreqHz = 120.0f;
        p.bassDrive = pass == 0 ? 0.0f : 1.0f;
        chain.setParams(p);
        std::vector<float> l(48000), r(48000);
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = r[i] = 0.8f * float(std::sin(2.0 * kPi * hz * double(i) / sr));
        processInBlocks(chain, l, r);
        third[pass] = toneAmplitude(l, 24000, hz * 3.0, sr);
    }
    CHECK(third[0] < 1.0e-4, "no drive should add no harmonics, 3rd = %g", third[0]);
    CHECK(third[1] > 0.01, "full drive should add a 3rd harmonic, 3rd = %g", third[1]);
}

void testOutputStage()
{
    std::printf("output gain and ceiling\n");
    // Gain accuracy.
    {
        EffectChain chain;
        chain.prepare(44100.0);
        auto p = neutralParams();
        p.outputGainDb = -6.0f;
        chain.setParams(p);
        std::vector<float> l(2000, 0.5f), r(2000, 0.5f);
        processInBlocks(chain, l, r);
        CHECK(std::abs(toDb(l.back() / 0.5) + 6.0) < 0.01, "-6 dB gain gave %.3f dB", toDb(l.back() / 0.5));
    }
    // The ceiling is never exceeded, and quiet signals are untouched.
    for (float ceilingDb : { -12.0f, -1.0f, 0.0f }) {
        EffectChain chain;
        chain.prepare(48000.0);
        auto p = neutralParams();
        p.clipOn = true;
        p.ceilingDb = ceilingDb;
        p.outputGainDb = 12.0f;
        chain.setParams(p);
        Rng rng;
        std::vector<float> l(20000), r(20000);
        for (size_t i = 0; i < l.size(); ++i) { l[i] = rng.next(); r[i] = rng.next(); }
        processInBlocks(chain, l, r);
        float peak = 0.0f;
        for (size_t i = 0; i < l.size(); ++i) peak = std::max({ peak, std::abs(l[i]), std::abs(r[i]) });
        const float ceiling = dbToGain(ceilingDb);
        CHECK(peak <= ceiling * 1.00001f, "ceiling %.1f dB exceeded: peak %.4f > %.4f", double(ceilingDb), double(peak), double(ceiling));
        CHECK(peak > ceiling * 0.9f, "clipper should use the headroom, peak %.4f", double(peak));
    }
    CHECK(softCeiling(0.3f, 0.891f) == 0.3f, "below the knee the curve must be the identity");
    CHECK(softCeiling(-5.0f, 0.5f) >= -0.5f, "negative side must respect the ceiling too");
}

/** Feeds an impulse into the reverb at sample `at` and returns the wet response. */
void reverbImpulse(double sr, const ConvReverb::Params& p, double seconds, std::vector<float>& wl, std::vector<float>& wr,
                   size_t at = 0, int block = 480)
{
    ConvReverb rv;
    rv.prepare(sr);
    rv.setParams(p);
    const size_t n = size_t(seconds * sr);
    std::vector<float> inL(n, 0.0f), inR(n, 0.0f);
    inL[at] = inR[at] = 1.0f;
    wl.assign(n, 0.0f);
    wr.assign(n, 0.0f);
    for (size_t pos = 0; pos < n; pos += size_t(block)) {
        const int count = int(std::min(size_t(block), n - pos));
        rv.process(inL.data() + pos, inR.data() + pos, wl.data() + pos, wr.data() + pos, count);
    }
}

/** RT60 from the slope of the backward-integrated energy between -5 and -35 dB. */
double measureRt60(const std::vector<float>& h, double sr)
{
    std::vector<double> edc(h.size() + 1, 0.0);
    for (size_t i = h.size(); i-- > 0;) edc[i] = edc[i + 1] + double(h[i]) * h[i];
    size_t t5 = 0, t35 = 0;
    for (size_t i = 0; i < h.size(); ++i) {
        const double db = 10.0 * std::log10(edc[i] / edc[0] + 1e-30);
        if (t5 == 0 && db <= -5.0) t5 = i;
        if (t35 == 0 && db <= -35.0) { t35 = i; break; }
    }
    return 2.0 * double(t35 - t5) / sr;
}

void testReverbMatchesReference()
{
    std::printf("reverb matches the reference: noise burst, 10 ms gap, 3 s decay, level\n");
    for (double sr : { 44100.0, 48000.0 }) {
        std::vector<float> wl, wr;
        reverbImpulse(sr, ConvReverb::Params {}, 7.0, wl, wr);
        const auto gap = size_t(std::lround(0.010 * sr));

        // Nothing before the 10 ms gap, sound straight after it.
        double before = 0.0, justAfter = 0.0;
        for (size_t i = 0; i < gap; ++i) before = std::max(before, double(std::abs(wl[i])) + double(std::abs(wr[i])));
        for (size_t i = gap; i < gap + 64; ++i) justAfter += double(wl[i]) * wl[i];
        CHECK(before < 1e-6, "%.0f Hz: reverb before the 10 ms gap (peak %g)", sr, before);
        CHECK(justAfter > 1e-6, "%.0f Hz: reverb should start right after the gap (energy %g)", sr, justAfter);

        const double rtL = measureRt60(wl, sr), rtR = measureRt60(wr, sr);
        CHECK(std::abs(rtL - 3.0) < 0.1 && std::abs(rtR - 3.0) < 0.1, "%.0f Hz: decay time %.3f / %.3f s, expected 3.0", sr, rtL, rtR);

        double eL = 0.0, eR = 0.0, cross = 0.0;
        for (size_t i = 0; i < wl.size(); ++i) { eL += double(wl[i]) * wl[i]; eR += double(wr[i]) * wr[i]; cross += double(wl[i]) * wr[i]; }
        CHECK(std::abs(toDb(std::sqrt(eL / ConvReverb::kWetEnergy))) < 0.3 && std::abs(toDb(std::sqrt(eR / ConvReverb::kWetEnergy))) < 0.3,
              "%.0f Hz: wet level %.2f / %.2f dB vs reference", sr, toDb(std::sqrt(eL / ConvReverb::kWetEnergy)), toDb(std::sqrt(eR / ConvReverb::kWetEnergy)));
        CHECK(std::abs(cross) / std::sqrt(eL * eR) < 0.05, "%.0f Hz: left and right should be unrelated noise (correlation %.3f)", sr, cross / std::sqrt(eL * eR));

        // White: no filtering, so each octave-wide slice of the spectrum holds its share of the energy.
        for (double hz : { 100.0, 1000.0, 8000.0 }) {
            BiquadState a, b;
            const auto lp = makeLowPass(sr, hz * 1.4142), hp = makeHighPass(sr, hz / 1.4142);
            double band = 0.0;
            for (float v : wl) { const float y = a.process(lp, b.process(hp, v)); band += double(y) * y; }
            const double share = band / eL, expected = (hz * 1.4142 - hz / 1.4142) / (sr / 2.0);
            CHECK(std::abs(toDb(std::sqrt(share / expected))) < 2.0, "%.0f Hz: band at %.0f Hz holds %.2f dB vs flat", sr, hz, toDb(std::sqrt(share / expected)));
        }
    }
}

void testReverbIsATrueConvolution()
{
    std::printf("reverb is an exact, time-invariant convolution\n");
    const double sr = 48000.0;
    // The response must not depend on where the impulse falls in the internal blocks,
    // or on how the host slices its buffers. Any mistake in the partitioning shows up here.
    std::vector<float> refL, refR, wl, wr;
    reverbImpulse(sr, ConvReverb::Params {}, 2.5, refL, refR, 0, 480);
    for (size_t at : { size_t(1), size_t(127), size_t(128), size_t(5000), size_t(8191), size_t(8192), size_t(20001) }) {
        reverbImpulse(sr, ConvReverb::Params {}, 2.5, wl, wr, at, at % 2 == 0 ? 64 : 777);
        double worst = 0.0, peak = 0.0;
        for (size_t i = 0; i + at < wl.size(); ++i) {
            worst = std::max(worst, double(std::abs(wl[i + at] - refL[i])) + double(std::abs(wr[i + at] - refR[i])));
            peak = std::max(peak, double(std::abs(refL[i])));
        }
        CHECK(worst < peak * 2e-3, "impulse at sample %d: response differs by %g (peak %g)", int(at), worst, peak);
    }

    // The decay control: measured decay time follows it, and the level stays the same.
    for (float decay : { 0.5f, 1.5f, 5.0f }) {
        ConvReverb::Params p;
        p.decaySeconds = decay;
        reverbImpulse(sr, p, double(decay) * 1.6 + 1.0, wl, wr);
        const double rt = measureRt60(wl, sr);
        double e = 0.0;
        for (float v : wl) e += double(v) * v;
        const double levelDb = toDb(std::sqrt(e / ConvReverb::kWetEnergy));
        std::printf("  decay %.1f s: measured %.3f s, level %+.2f dB\n", double(decay), rt, levelDb);
        CHECK(std::abs(rt - double(decay)) < double(decay) * 0.05, "decay %.1f s measured as %.3f s", double(decay), rt);
        CHECK(std::abs(levelDb) < 0.5, "decay %.1f s: wet level %.2f dB vs reference", double(decay), levelDb);
    }

    // Tone controls only act when moved off their end stops.
    ConvReverb::Params cut;
    cut.lowCutHz = 300.0f;
    reverbImpulse(sr, cut, 2.5, wl, wr);
    BiquadState a;
    const auto lp = makeLowPass(sr, 100.0);
    double lowCutBand = 0.0, lowPlain = 0.0;
    for (float v : wl) { const float y = a.process(lp, v); lowCutBand += double(y) * y; }
    a.reset();
    for (float v : refL) { const float y = a.process(lp, v); lowPlain += double(y) * y; }
    CHECK(toDb(std::sqrt(lowCutBand / lowPlain)) < -15.0, "low cut at 300 Hz leaves %.1f dB below 100 Hz", toDb(std::sqrt(lowCutBand / lowPlain)));
}

void testNoClicksOnParameterJumps()
{
    std::printf("parameter jumps are smoothed\n");
    EffectChain chain;
    chain.prepare(48000.0);
    auto p = neutralParams();
    chain.setParams(p);
    std::vector<float> l(4800, 0.5f), r(4800, 0.5f);
    processInBlocks(chain, l, r);

    // Jump every "instant" control at once on a DC signal: any step would show up directly.
    p.outputGainDb = -24.0f;
    p.clipOn = true;
    p.ceilingDb = -12.0f;
    p.reverbOn = true;
    p.reverbMix = 1.0f;
    chain.setParams(p);
    std::vector<float> l2(9600, 0.5f), r2(9600, 0.5f);
    processInBlocks(chain, l2, r2);
    float maxStep = std::abs(l2[0] - l.back());
    for (size_t i = 1; i < l2.size(); ++i) maxStep = std::max(maxStep, std::abs(l2[i] - l2[i - 1]));
    CHECK(maxStep < 0.02f, "largest sample-to-sample step after a jump was %g", double(maxStep));

    // Bass gain jump on a low sine: compare against the steepest slope the settled signal has.
    EffectChain bassChain;
    bassChain.prepare(48000.0);
    auto b = neutralParams();
    b.bassOn = true;
    b.bassGainDb = 0.0f;
    bassChain.setParams(b);
    std::vector<float> s(48000), s2(48000);
    for (size_t i = 0; i < s.size(); ++i) s[i] = s2[i] = 0.1f * float(std::sin(2.0 * kPi * 50.0 * double(i) / 48000.0));
    bassChain.process(s.data(), s2.data(), 24000);
    b.bassGainDb = 18.0f;
    bassChain.setParams(b);
    bassChain.process(s.data() + 24000, s2.data() + 24000, 24000);
    float step = 0.0f;
    for (size_t i = 24000; i < s.size(); ++i) step = std::max(step, std::abs(s[i] - s[i - 1]));
    const float settledSlope = 0.1f * 7.95f * float(2.0 * kPi * 50.0 / 48000.0); // +18 dB sine
    CHECK(step < settledSlope * 1.5f, "bass gain jump produced a step of %g (settled slope %g)", double(step), double(settledSlope));
}

void testAutomationTorture()
{
    std::printf("random automation stays finite and under the ceiling\n");
    for (double sr : { 44100.0, 96000.0 }) {
        EffectChain chain;
        chain.prepare(sr);
        Rng rng, prng;
        prng.state = 99u;
        auto u = [&] { return 0.5f * (prng.next() + 1.0f); };
        float peak = 0.0f;
        bool finite = true;
        std::vector<float> l(1024), r(1024);
        for (int block = 0; block < 600; ++block) {
            ChainParams p;
            p.reverbOn = u() > 0.2f;
            p.reverbMix = u();
            p.reverb.decaySeconds = 0.5f + 4.5f * u();
            p.reverb.lowCutHz = 20.0f + 980.0f * u();
            p.reverb.highCutHz = 1000.0f + 19000.0f * u();
            p.bassOn = u() > 0.3f;
            p.bassGainDb = 18.0f * u();
            p.bassFreqHz = 40.0f + 160.0f * u();
            p.bassDrive = u();
            p.outputGainDb = -24.0f + 36.0f * u();
            p.clipOn = true;
            p.ceilingDb = -1.0f;
            chain.setParams(p);
            const int n = 1 + int(u() * 1023.0f); // hosts send odd block sizes
            for (int i = 0; i < n; ++i) { l[size_t(i)] = rng.next(); r[size_t(i)] = rng.next(); }
            chain.process(l.data(), r.data(), n);
            for (int i = 0; i < n; ++i) {
                finite = finite && std::isfinite(l[size_t(i)]) && std::isfinite(r[size_t(i)]);
                peak = std::max({ peak, std::abs(l[size_t(i)]), std::abs(r[size_t(i)]) });
            }
        }
        CHECK(finite, "output contained NaN or infinity at %.0f Hz", sr);
        CHECK(peak <= dbToGain(-1.0f) * 1.00001f, "ceiling exceeded under automation: %.4f", double(peak));
    }
}

void testReverbStaysStable()
{
    std::printf("reverb stays stable while the decay is moved, and dies away\n");
    const double sr = 48000.0;
    ConvReverb rv;
    rv.prepare(sr);
    Rng rng, prng;
    prng.state = 7u;
    std::vector<float> inL(480), inR(480), wl(480), wr(480);
    double first = 0.0, last = 0.0, peak = 0.0;
    const int blocks = int(12.0 * sr / 480.0);
    for (int b = 0; b < blocks; ++b) {
        const bool feeding = b < 300; // 3 s of noise with the decay knob being thrown around, then let it ring at 3 s
        ConvReverb::Params p;
        p.decaySeconds = feeding ? 0.5f + 2.25f * (prng.next() + 1.0f) : 3.0f;
        rv.setParams(p);
        for (size_t i = 0; i < 480; ++i) { inL[i] = feeding ? rng.next() : 0.0f; inR[i] = feeding ? rng.next() : 0.0f; }
        rv.process(inL.data(), inR.data(), wl.data(), wr.data(), 480);
        double e = 0.0;
        for (size_t i = 0; i < 480; ++i) { e += double(wl[i]) * wl[i]; peak = std::max(peak, double(std::abs(wl[i]))); }
        if (b >= 400 && b < 500) first += e;
        if (b >= blocks - 100) last += e;
    }
    CHECK(std::isfinite(peak) && peak < 20.0, "reverb output peak %g", peak);
    // The two windows are 7 s apart: at RT60 = 3 s that is 140 dB, i.e. down to nothing.
    const double dropDb = 10.0 * std::log10(first / std::max(last, 1e-30));
    CHECK(dropDb > 100.0, "tail dropped %.1f dB over 7 s", dropDb);

    // A reset really does empty it.
    rv.reset();
    std::fill(inL.begin(), inL.end(), 0.0f);
    std::fill(inR.begin(), inR.end(), 0.0f);
    double leftover = 0.0;
    for (int b = 0; b < 200; ++b) {
        rv.process(inL.data(), inR.data(), wl.data(), wr.data(), 480);
        for (float v : wl) leftover = std::max(leftover, double(std::abs(v)));
    }
    CHECK(leftover == 0.0, "after a reset the reverb must be silent (peak %g)", leftover);
}

// ---------------------------------------------------------------------------

void testTapeSpeed()
{
    const double sr = 48000.0;
    const size_t n = size_t(sr * 4);

    // Off: bit-exact, no delay.
    {
        TapeSpeed tape;
        tape.prepare(sr);
        tape.setTarget(false, 0.5f);
        Rng rng;
        std::vector<float> l(4800), r(4800), refL, refR;
        for (size_t i = 0; i < l.size(); ++i) { l[i] = rng.next(); r[i] = rng.next(); }
        refL = l; refR = r;
        tape.process(l.data(), r.data(), int(l.size()));
        CHECK(l == refL && r == refR, "tape speed off must not change the signal");
        CHECK(tape.lagSeconds() == 0.0, "lag %g", tape.lagSeconds());
    }

    // 0.8x: a 1 kHz tone comes out at 800 Hz, and the output falls 0.2 s behind per second.
    {
        TapeSpeed tape;
        tape.prepare(sr);
        tape.setTarget(true, 0.8f);
        std::vector<float> l(n), r(n);
        for (size_t i = 0; i < n; ++i) l[i] = r[i] = 0.5f * float(std::sin(2.0 * kPi * 1000.0 * double(i) / sr));
        for (size_t pos = 0; pos < n; pos += 512)
            tape.process(l.data() + pos, r.data() + pos, int(std::min<size_t>(512, n - pos)));
        const double at800 = toneAmplitude(l, n / 2, 800.0, sr);
        const double at1000 = toneAmplitude(l, n / 2, 1000.0, sr);
        CHECK(std::abs(toDb(at800 / 0.5)) < 0.3, "800 Hz level %.2f dB", toDb(at800 / 0.5));
        CHECK(toDb(at1000 / 0.5) < -50.0, "1 kHz residue %.1f dB", toDb(at1000 / 0.5));
        CHECK(std::abs(tape.lagSeconds() - 0.8) < 0.01, "lag after 4 s at 0.8x: %.3f s, expected 0.8", tape.lagSeconds());
        float peak = 0.0f;
        for (float v : l) peak = std::max(peak, std::abs(v));
        CHECK(peak < 0.52f, "slowed tone peak %g", peak);
    }

    // The speed glides: switching on mid-tone makes no step larger than the tone's own slope allows.
    // Switching off crossfades back to live, after which the output is the input again.
    {
        TapeSpeed tape;
        tape.prepare(sr);
        tape.setTarget(false, 0.5f);
        std::vector<float> in(n), l(n), r(n);
        for (size_t i = 0; i < n; ++i) in[i] = 0.5f * float(std::sin(2.0 * kPi * 220.0 * double(i) / sr));
        l = in; r = in;
        const double maxSlope = 0.5 * 2.0 * kPi * 220.0 / sr;
        double worstStep = 0.0;
        float prev = 0.0f;
        for (size_t pos = 0; pos < n; pos += 480) {
            const double t = double(pos) / sr;
            tape.setTarget(t >= 0.5 && t < 2.5, 0.5f);
            if (pos == size_t(sr * 1.5))
                tape.resync();
            tape.process(l.data() + pos, r.data() + pos, 480);
            for (size_t i = pos; i < pos + 480; ++i) { worstStep = std::max(worstStep, double(std::abs(l[i] - prev))); prev = l[i]; }
        }
        CHECK(worstStep < maxSlope * 2.5, "largest step %g vs tone slope %g", worstStep, maxSlope);
        CHECK(tape.lagSeconds() == 0.0, "lag after switching off: %g", tape.lagSeconds());
        bool same = true;
        for (size_t i = size_t(sr * 3); i < n; ++i) same = same && l[i] == in[i];
        CHECK(same, "after switching off the output must be the live input again");
    }

    // The slowed signal plays out after the input stops, then the lag returns to zero by itself.
    {
        TapeSpeed tape;
        tape.prepare(sr);
        tape.setTarget(true, 0.5f);
        Rng rng;
        std::vector<float> l(480), r(480);
        double lateEnergy = 0.0, endEnergy = 0.0;
        for (int b = 0; b < 500; ++b) { // 1 s of noise, then 4 s of silence
            for (size_t i = 0; i < 480; ++i) { l[i] = b < 100 ? rng.next() * 0.5f : 0.0f; r[i] = l[i]; }
            tape.process(l.data(), r.data(), 480);
            double e = 0.0;
            for (float v : l) e += double(v) * v;
            if (b >= 150 && b < 190) lateEnergy += e;
            if (b >= 450) endEnergy += e;
        }
        CHECK(lateEnergy > 1.0, "slowed audio should still be playing 0.5-0.9 s after the input stops (energy %g)", lateEnergy);
        CHECK(endEnergy < 1e-6, "output should be silent once the buffer has played out (energy %g)", endEnergy);
        CHECK(tape.lagSeconds() == 0.0, "lag should return to zero in silence, is %g s", tape.lagSeconds());
    }

    // Sync mode: the pitch still drops, but the output stays in time with the input.
    {
        TapeSpeed tape;
        tape.prepare(sr);
        tape.setTarget(true, 0.8f, true);
        std::vector<float> l(n), r(n);
        for (size_t i = 0; i < n; ++i) l[i] = r[i] = 0.5f * float(std::sin(2.0 * kPi * 1000.0 * double(i) / sr));
        for (size_t pos = 0; pos < n; pos += 512)
            tape.process(l.data() + pos, r.data() + pos, int(std::min<size_t>(512, n - pos)));
        const double at800 = toneAmplitude(l, n / 2, 800.0, sr);
        const double at1000 = toneAmplitude(l, n / 2, 1000.0, sr);
        CHECK(toDb(at800 / 0.5) > -6.0, "sync: 800 Hz level %.2f dB", toDb(at800 / 0.5));
        CHECK(toDb(at1000 / at800) < -20.0, "sync: 1 kHz is %.1f dB relative to 800 Hz", toDb(at1000 / at800));
        CHECK(tape.lagSeconds() == 0.0, "sync: lag %g", tape.lagSeconds());
        float peak = 0.0f;
        for (float v : l) peak = std::max(peak, std::abs(v));
        CHECK(peak < 0.75f, "sync: tone peak %g", peak);
    }

    // Resampling accuracy in tape mode: high notes keep their level, and the
    // interpolation leaves no audible image of the original behind.
    {
        for (double hz : { 5000.0, 15000.0, 18000.0, 20000.0 }) {
            TapeSpeed tape;
            tape.prepare(sr);
            tape.setTarget(true, 0.8f);
            std::vector<float> l(n), r(n);
            for (size_t i = 0; i < n; ++i) l[i] = r[i] = 0.5f * float(std::sin(2.0 * kPi * hz * double(i) / sr));
            for (size_t pos = 0; pos < n; pos += 512)
                tape.process(l.data() + pos, r.data() + pos, int(std::min<size_t>(512, n - pos)));
            const double level = toDb(toneAmplitude(l, n / 2, hz * 0.8, sr) / 0.5);
            const double image = toDb(toneAmplitude(l, n / 2, (sr - hz) * 0.8, sr) / 0.5);
            CHECK(std::abs(level) < 0.1, "%.0f Hz slowed to 80%%: level %.2f dB", hz, level);
            CHECK(image < -70.0, "%.0f Hz slowed to 80%%: image at %.0f Hz is %.1f dB", hz, (sr - hz) * 0.8, image);
        }
    }

    // Sync mode must not flutter: a low note that does not fit the grain length
    // keeps a steady level. (Unaligned grains cancel each other and dip by 10 dB or more.)
    for (float speedValue : { 0.8f, 0.7f, 1.25f }) {
        for (double hz : { 41.0, 55.0, 73.0, 137.0, 311.0 }) {
            TapeSpeed tape;
            tape.prepare(sr);
            tape.setTarget(true, speedValue, true);
            std::vector<float> l(n), r(n);
            for (size_t i = 0; i < n; ++i) l[i] = r[i] = 0.5f * float(std::sin(2.0 * kPi * hz * double(i) / sr));
            for (size_t pos = 0; pos < n; pos += 512)
                tape.process(l.data() + pos, r.data() + pos, int(std::min<size_t>(512, n - pos)));
            const size_t window = size_t(4.0 * sr / (hz * double(speedValue))); // four periods of the shifted note
            double lowest = 1e9, highest = 0.0;
            for (size_t start = n / 2; start + window < n; start += window / 4) {
                double e = 0.0;
                for (size_t i = start; i < start + window; ++i) e += double(l[i]) * l[i];
                const double rms = std::sqrt(e / double(window));
                lowest = std::min(lowest, rms);
                highest = std::max(highest, rms);
            }
            const double rippleDb = toDb(highest / lowest);
            const double levelDb = toDb(highest / (0.5 / std::sqrt(2.0)));
            CHECK(rippleDb < 1.5, "sync %.0f%%, %.0f Hz: level wobbles by %.2f dB", double(speedValue) * 100.0, hz, rippleDb);
            CHECK(std::abs(levelDb) < 1.0, "sync %.0f%%, %.0f Hz: level %.2f dB", double(speedValue) * 100.0, hz, levelDb);
        }
    }

    // Sync mode leaves the source alone between splices: for most of the time the output
    // is one plain resampled copy, so a steady tone keeps its level to a fraction of a dB
    // and a single click comes out as a single click, not a doubled or smeared one.
    {
        TapeSpeed tape;
        tape.prepare(sr);
        tape.setTarget(true, 0.8f, true);
        std::vector<float> l(n, 0.0f), r(n, 0.0f);
        const size_t clicks = 40;
        for (size_t c = 0; c < clicks; ++c) l[size_t(sr) + c * 3301] = r[size_t(sr) + c * 3301] = 0.8f;
        for (size_t pos = 0; pos < n; pos += 512)
            tape.process(l.data() + pos, r.data() + pos, int(std::min<size_t>(512, n - pos)));
        // Count the clicks that came out essentially whole. (One that lands on a splice is shared between two heads.)
        size_t whole = 0;
        double energyIn = double(clicks) * 0.8 * 0.8, energyOut = 0.0;
        for (size_t c = 0; c < clicks; ++c) {
            const size_t from = size_t(sr) + c * 3301;
            double e = 0.0;
            for (size_t i = from; i < from + 3301 && i < n; ++i) e += double(l[i]) * l[i];
            energyOut += e;
            // A click read at 80% speed is spread over 1 / 0.8 as many samples, less the 5% of the band the interpolator leaves out.
            const double expected = 0.8 * 0.8 * 0.95 / 0.8;
            if (e > expected * 0.8 && e < expected * 1.2)
                ++whole;
        }
        CHECK(whole == clicks, "sync: %d of %d clicks came through whole", int(whole), int(clicks));
        CHECK(energyOut < energyIn * 0.95 / 0.8 * 1.05, "sync: clicks must not be doubled (energy out %g, in %g)", energyOut, energyIn);
    }

    // Sync mode and drums: every hit comes out once, whole. A splice must not land on a hit
    // (slower than live that would skip its attack, faster than live it would play it twice).
    for (float speedValue : { 0.8f, 0.7f, 0.9f, 1.2f, 1.5f }) {
        TapeSpeed tape;
        tape.prepare(sr);
        tape.setTarget(true, speedValue, true);
        Rng rng;
        std::vector<float> l(n, 0.0f), r(n, 0.0f);
        const size_t hits = 20, spacing = 6593, first = size_t(sr);
        std::vector<double> energyIn(hits, 0.0);
        for (size_t h = 0; h < hits; ++h)
            for (size_t i = 0; i < 1500; ++i) {
                const float v = 0.8f * rng.next() * float(std::exp(-double(i) / (0.006 * sr)));
                l[first + h * spacing + i] = r[first + h * spacing + i] = v;
                energyIn[h] += double(v) * v;
            }
        for (size_t pos = 0; pos < n; pos += 512)
            tape.process(l.data() + pos, r.data() + pos, int(std::min<size_t>(512, n - pos)));
        size_t whole = 0;
        double worst = 1.0;
        for (size_t h = 0; h < hits; ++h) {
            double e = 0.0;
            for (size_t i = first + h * spacing; i < first + (h + 1) * spacing; ++i) e += double(l[i]) * l[i];
            // The same samples spread over more (or less) time; faster than live the top of the band is filtered off.
            const double band = speedValue > 1.25f ? 0.61 : speedValue > 1.0f ? 0.74 : 0.95;
            const double ratio = e / (energyIn[h] * band / double(speedValue));
            if (std::abs(toDb(ratio)) > std::abs(toDb(worst))) worst = ratio;
            if (ratio > 0.8 && ratio < 1.2) ++whole;
        }
        CHECK(whole == hits, "sync %.0f%%: %d of %d drum hits came through whole (worst %.1f dB)", double(speedValue) * 100.0, int(whole),
              int(hits), toDb(worst) * 0.5);
    }

    // The same with a loud bass note held underneath, as in a full mix: the hits still come
    // through whole (compared with the same hits on their own), and the bass keeps its level.
    for (float speedValue : { 0.8f, 1.2f }) {
        const size_t hits = 20, spacing = 6593, first = size_t(sr);
        std::vector<float> out[2];
        for (int withBass = 0; withBass < 2; ++withBass) {
            TapeSpeed tape;
            tape.prepare(sr);
            tape.setTarget(true, speedValue, true);
            Rng rng;
            std::vector<float> l(n, 0.0f), r(n, 0.0f);
            if (withBass)
                for (size_t i = 0; i < n; ++i) l[i] = r[i] = 0.3f * float(std::sin(2.0 * kPi * 55.0 * double(i) / sr));
            for (size_t h = 0; h < hits; ++h)
                for (size_t i = 0; i < 1500; ++i) {
                    const float v = 0.5f * rng.next() * float(std::exp(-double(i) / (0.006 * sr)));
                    l[first + h * spacing + i] += v;
                    r[first + h * spacing + i] += v;
                }
            for (size_t pos = 0; pos < n; pos += 512)
                tape.process(l.data() + pos, r.data() + pos, int(std::min<size_t>(512, n - pos)));
            out[withBass] = l;
        }
        // The sample-to-sample difference hears the hits and all but ignores the bass.
        size_t whole = 0;
        double worst = 1.0;
        for (size_t h = 0; h < hits; ++h) {
            double e[2] { 0.0, 0.0 };
            for (int k = 0; k < 2; ++k)
                for (size_t i = first + h * spacing; i < first + (h + 1) * spacing; ++i) {
                    const double d = double(out[k][i]) - out[k][i - 1];
                    e[k] += d * d;
                }
            const double ratio = e[1] / e[0];
            if (std::abs(toDb(ratio)) > std::abs(toDb(worst))) worst = ratio;
            if (ratio > 0.75 && ratio < 1.25) ++whole;
        }
        CHECK(whole == hits, "sync %.0f%% over bass: %d of %d drum hits came through whole (worst %.1f dB)", double(speedValue) * 100.0,
              int(whole), int(hits), toDb(worst) * 0.5);
        // The bass, in stretches of eight periods: a splice that put the note out of step would
        // show as a stretch where it all but disappears. (The hits themselves move the reading by a dB or two.)
        const double bassHz = 55.0 * double(speedValue);
        const size_t stretch = size_t(8.0 * sr / bassHz);
        double lowest = 1e9;
        for (size_t start = first; start + stretch < first + hits * spacing; start += stretch) {
            double re = 0.0, im = 0.0;
            for (size_t i = start; i < start + stretch; ++i) {
                const double phase = 2.0 * kPi * bassHz * double(i) / sr;
                re += out[1][i] * std::cos(phase);
                im += out[1][i] * std::sin(phase);
            }
            lowest = std::min(lowest, toDb(2.0 * std::sqrt(re * re + im * im) / double(stretch) / 0.3));
        }
        CHECK(lowest > -3.5, "sync %.0f%% over drums: the bass dips to %.1f dB", double(speedValue) * 100.0, lowest);
    }

    // Faster than live must not alias: 20 kHz at 150% would land on 30 kHz, which
    // does not exist at 48 kHz and would fold back to 18 kHz. It has to be removed instead.
    {
        TapeSpeed tape;
        tape.prepare(sr);
        tape.setTarget(true, 1.5f, true);
        std::vector<float> l(n), r(n);
        for (size_t i = 0; i < n; ++i) l[i] = r[i] = 0.5f * float(std::sin(2.0 * kPi * 20000.0 * double(i) / sr));
        for (size_t pos = 0; pos < n; pos += 512)
            tape.process(l.data() + pos, r.data() + pos, int(std::min<size_t>(512, n - pos)));
        double e = 0.0;
        for (size_t i = n / 2; i < n; ++i) e += double(l[i]) * l[i];
        const double db = toDb(std::sqrt(e / double(n / 2)) / (0.5 / std::sqrt(2.0)));
        CHECK(db < -55.0, "20 kHz at 150%%: %.1f dB of aliasing left", db);
        // ...while a note that does fit keeps its level.
        TapeSpeed tape2;
        tape2.prepare(sr);
        tape2.setTarget(true, 1.5f, true);
        for (size_t i = 0; i < n; ++i) l[i] = r[i] = 0.5f * float(std::sin(2.0 * kPi * 4000.0 * double(i) / sr));
        for (size_t pos = 0; pos < n; pos += 512)
            tape2.process(l.data() + pos, r.data() + pos, int(std::min<size_t>(512, n - pos)));
        e = 0.0;
        for (size_t i = n / 2; i < n; ++i) e += double(l[i]) * l[i];
        const double keep = toDb(std::sqrt(e / double(n / 2)) / (0.5 / std::sqrt(2.0)));
        CHECK(std::abs(keep) < 1.5, "4 kHz at 150%%: level %.2f dB", keep);
    }

    // Faster than live: 1 kHz at 125% comes out at 1250 Hz with no drift, and it does so
    // with the sync switch off too, because speeding up is only possible in sync.
    for (bool syncSwitch : { true, false }) {
        TapeSpeed tape;
        tape.prepare(sr);
        tape.setTarget(true, 1.25f, syncSwitch);
        std::vector<float> l(n), r(n);
        for (size_t i = 0; i < n; ++i) l[i] = r[i] = 0.5f * float(std::sin(2.0 * kPi * 1000.0 * double(i) / sr));
        for (size_t pos = 0; pos < n; pos += 512)
            tape.process(l.data() + pos, r.data() + pos, int(std::min<size_t>(512, n - pos)));
        const double at1250 = toneAmplitude(l, n / 2, 1250.0, sr);
        const double at1000 = toneAmplitude(l, n / 2, 1000.0, sr);
        CHECK(toDb(at1250 / 0.5) > -6.0, "speed-up: 1250 Hz level %.2f dB", toDb(at1250 / 0.5));
        CHECK(toDb(at1000 / at1250) < -20.0, "speed-up: 1 kHz is %.1f dB relative to 1250 Hz", toDb(at1000 / at1250));
        CHECK(tape.lagSeconds() == 0.0, "speed-up: lag %g", tape.lagSeconds());
        bool finite = true;
        float peak = 0.0f;
        for (float v : l) { finite = finite && std::isfinite(v); peak = std::max(peak, std::abs(v)); }
        CHECK(finite && peak < 0.75f, "speed-up: tone peak %g", peak);
    }

    // Tape mode, knob swept from above 100% to below: the lag never goes negative.
    {
        TapeSpeed tape;
        tape.prepare(sr);
        std::vector<float> l(480, 0.25f), r(480, 0.25f);
        bool ok = true;
        for (int b = 0; b < 300; ++b) {
            tape.setTarget(true, b < 100 ? 1.5f : 0.9f, false);
            std::fill(l.begin(), l.end(), 0.25f);
            std::fill(r.begin(), r.end(), 0.25f);
            tape.process(l.data(), r.data(), 480);
            ok = ok && tape.lagSeconds() >= 0.0;
            for (float v : l) ok = ok && std::abs(v - 0.25f) < 1e-4f; // a constant stays constant
        }
        CHECK(ok, "sweeping from 150%% to 90%% must keep the lag non-negative and the signal intact");
        CHECK(tape.lagSeconds() > 0.1, "after the sweep the tape should be falling behind (lag %g)", tape.lagSeconds());
    }

    // Sync mode: a burst 3 s in comes out within one grain window, not 0.6 s late.
    {
        TapeSpeed tape;
        tape.prepare(sr);
        tape.setTarget(true, 0.8f, true);
        Rng rng;
        std::vector<float> l(n, 0.0f), r(n, 0.0f);
        const size_t burst = size_t(sr * 3);
        for (size_t i = burst; i < burst + 480; ++i) l[i] = r[i] = rng.next() * 0.5f;
        for (size_t pos = 0; pos < n; pos += 480)
            tape.process(l.data() + pos, r.data() + pos, 480);
        double before = 0.0, inWindow = 0.0, after = 0.0;
        const size_t windowEnd = burst + 480 + size_t(sr * 0.08);
        for (size_t i = 0; i < n; ++i) {
            const double e = double(l[i]) * l[i];
            (i < burst ? before : i < windowEnd ? inWindow : after) += e;
        }
        CHECK(before == 0.0, "sync: output before the burst (energy %g)", before);
        CHECK(inWindow > 1.0, "sync: the burst should come out straight away (energy %g)", inWindow);
        CHECK(after < inWindow * 0.01, "sync: nothing should come out late (energy %g vs %g)", after, inWindow);
    }

    // Sync mode at 100% speed is the plain signal; switching between the modes makes no click.
    {
        TapeSpeed tape;
        tape.prepare(sr);
        tape.setTarget(true, 1.0f, true);
        Rng rng;
        std::vector<float> l(4800), r(4800), ref;
        for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = rng.next();
        ref = l;
        tape.process(l.data(), r.data(), int(l.size()));
        CHECK(l == ref, "sync at 100%% speed must not change the signal");

        std::vector<float> tl(n), tr(n);
        for (size_t i = 0; i < n; ++i) tl[i] = tr[i] = 0.5f * float(std::sin(2.0 * kPi * 220.0 * double(i) / sr));
        const double maxSlope = 0.5 * 2.0 * kPi * 220.0 / sr;
        double worstStep = 0.0;
        float prev = 0.0f;
        tape.reset();
        for (size_t pos = 0; pos < n; pos += 480) {
            const double t = double(pos) / sr;
            tape.setTarget(t >= 0.25 && t < 3.5, 0.7f, (t >= 1.0 && t < 2.0) || t >= 3.0);
            tape.process(tl.data() + pos, tr.data() + pos, 480);
            for (size_t i = pos; i < pos + 480; ++i) { worstStep = std::max(worstStep, double(std::abs(tl[i] - prev))); prev = tl[i]; }
        }
        CHECK(worstStep < maxSlope * 3.0, "mode switching: largest step %g vs tone slope %g", worstStep, maxSlope);
        CHECK(tape.lagSeconds() == 0.0, "mode switching: lag at the end %g", tape.lagSeconds());
    }

    // Random speed automation through the whole chain at 44.1 kHz never misbehaves.
    {
        EffectChain chain;
        chain.prepare(44100.0);
        Rng rng;
        ChainParams p;
        std::vector<float> l(700), r(700);
        bool finite = true;
        float peak = 0.0f;
        for (int b = 0; b < 600; ++b) {
            p.speedOn = (b / 37) % 3 != 0;
            p.speed = 1.0f + 0.5f * rng.next();
            p.speedSync = (b / 53) % 2 == 1;
            if (b % 91 == 0) chain.resyncSpeed();
            if (b == 300) chain.reset();
            const int count = 1 + int((rng.next() * 0.5f + 0.5f) * 699.0f);
            for (int i = 0; i < count; ++i) { l[size_t(i)] = rng.next() * 0.9f; r[size_t(i)] = rng.next() * 0.9f; }
            chain.setParams(p);
            chain.process(l.data(), r.data(), count);
            for (int i = 0; i < count; ++i) {
                finite = finite && std::isfinite(l[size_t(i)]) && std::isfinite(r[size_t(i)]);
                peak = std::max({ peak, std::abs(l[size_t(i)]), std::abs(r[size_t(i)]) });
            }
        }
        CHECK(finite, "speed automation produced a non-finite sample");
        CHECK(peak <= dbToGain(-1.0f) + 1e-4f, "peak %g exceeds the ceiling", peak);
    }
}

} // namespace

int main()
{
    testSilenceAndPassThrough();
    testBassShelf();
    testBassDrive();
    testOutputStage();
    testReverbMatchesReference();
    testReverbIsATrueConvolution();
    testNoClicksOnParameterJumps();
    testAutomationTorture();
    testReverbStaysStable();
    testTapeSpeed();

    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
