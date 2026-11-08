#include "TestHarness.h"
#include "dsp/TempoAnalyzer.h"

#include <numbers>
#include <random>

using namespace sonarc;

namespace {

constexpr std::uint32_t kRate = 48000;

// A simple drum loop: kick on every beat, snare-ish noise burst on 2 and 4, closed hats on 8ths.
std::vector<StereoFrame> drumLoop(double bpm, double seconds, double startOffsetSeconds = 0.0)
{
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    const auto frames = (std::size_t) (seconds * kRate);
    std::vector<StereoFrame> out(frames);
    const double beat = 60.0 / bpm;

    for (std::size_t i = 0; i < frames; ++i)
    {
        const double t = (double) i / kRate + startOffsetSeconds;
        const double inBeat = std::fmod(t, beat);
        const int beatIndex = (int) (t / beat);
        const double inEighth = std::fmod(t, beat / 2.0);

        const double kickHz = 50.0 + 90.0 * std::exp(-inBeat * 30.0);
        float s = 0.6f * (float) (std::sin(2.0 * std::numbers::pi * kickHz * inBeat) * std::exp(-inBeat * 12.0));
        if (beatIndex % 2 == 1)
            s += 0.3f * noise(rng) * (float) std::exp(-inBeat * 25.0);
        s += 0.08f * noise(rng) * (float) std::exp(-inEighth * 80.0);
        out[i] = { s, s };
    }
    return out;
}

void feed(TempoAnalyzer& analyzer, const std::vector<StereoFrame>& audio)
{
    for (std::size_t i = 0; i < audio.size(); i += 480)
        analyzer.process(audio.data() + i, std::min<std::size_t>(480, audio.size() - i));
}

TempoEstimate analyse(double bpm, double seconds, float foldMinimum = 85.0f)
{
    TempoAnalyzer analyzer;
    analyzer.prepare(kRate);
    analyzer.setFoldRange(foldMinimum);
    feed(analyzer, drumLoop(bpm, seconds));
    return analyzer.latest();
}

} // namespace

TEST_CASE("Tempo: drum loops at known tempos are detected within 1 BPM")
{
    for (double bpm : { 90.0, 120.0, 128.0, 140.0 })
    {
        const auto result = analyse(bpm, 14.0);
        CHECK(result.valid);
        CHECK_NEAR(result.bpm, bpm, 1.0);
        CHECK(result.confidence > 0.5f);
    }
}

TEST_CASE("Tempo: drums with sustained chords (the on-screen test track) read the right tempo")
{
    for (double bpm : { 124.0, 128.0, 95.0 })
    {
        auto audio = drumLoop(bpm, 16.0);
        const double barSeconds = 4.0 * 60.0 / bpm;
        const int roots[] = { 66, 59, 61, 66 }; // F#m Bm C# F#m, one per bar
        for (std::size_t i = 0; i < audio.size(); ++i)
        {
            const double t = (double) i / kRate;
            const int root = roots[(int) (t / barSeconds) % 4];
            double pad = 0.0;
            for (int n : { root, root + 3, root + 7, root - 12 })
                for (int h = 1; h <= 3; ++h)
                    pad += std::sin(2.0 * std::numbers::pi * 440.0 * std::pow(2.0, (n - 69) / 12.0) * h * t) / (h * 1.6);
            const auto v = (float) (0.035 * pad);
            audio[i].left += v;
            audio[i].right += v;
        }

        TempoAnalyzer analyzer;
        analyzer.prepare(kRate);
        feed(analyzer, audio);
        if (std::abs(analyzer.latest().bpm - bpm) > 1.0)
            std::printf("    %.0f BPM track read as %.1f (raw %.1f)\n", bpm, analyzer.latest().bpm, analyzer.rawBpm());
        CHECK_NEAR(analyzer.latest().bpm, bpm, 1.0);
    }
}

TEST_CASE("Tempo: sweep 80-175 BPM reads every tempo (folded into 85-170)")
{
    int wrong = 0;
    for (double bpm = 80.0; bpm <= 175.0; bpm += 5.0)
    {
        const double expected = bpm < 85.0 ? bpm * 2.0 : bpm >= 170.0 ? bpm / 2.0 : bpm;
        const auto result = analyse(bpm, 14.0);
        if (! result.valid || std::abs(result.bpm - expected) > 1.0)
        {
            std::printf("    %.0f BPM read as %s%.1f (expected %.1f)\n", bpm, result.valid ? "" : "invalid ", result.bpm, expected);
            ++wrong;
        }
    }
    CHECK(wrong == 0);
}

TEST_CASE("Tempo: 174 BPM folds to 87 in the default 85-170 range, and reads 174 in 100-200")
{
    CHECK_NEAR(analyse(174.0, 14.0).bpm, 87.0, 1.0);
    CHECK_NEAR(analyse(174.0, 14.0, 100.0f).bpm, 174.0, 1.5);
}

TEST_CASE("Tempo: half/double time follows the chosen range (70 vs 140)")
{
    CHECK_NEAR(analyse(70.0, 14.0, 60.0f).bpm, 70.0, 1.0);
    CHECK_NEAR(analyse(70.0, 14.0, 100.0f).bpm, 140.0, 1.5);
}

TEST_CASE("Tempo: switching the range re-reads the current tempo immediately (x2 / half)")
{
    TempoAnalyzer analyzer;
    analyzer.prepare(kRate);
    analyzer.setFoldRange(60.0f);
    feed(analyzer, drumLoop(70.0, 12.0));
    CHECK_NEAR(analyzer.latest().bpm, 70.0, 1.0);
    analyzer.setFoldRange(120.0f); // "x2"
    CHECK_NEAR(analyzer.latest().bpm, 140.0, 2.0);
    CHECK(analyzer.latest().valid);
}

TEST_CASE("Tempo: silence and noise give no tempo")
{
    TempoAnalyzer analyzer;
    analyzer.prepare(kRate);
    feed(analyzer, std::vector<StereoFrame>(kRate * 10));
    CHECK(! analyzer.latest().valid);

    std::mt19937 rng(1);
    std::uniform_real_distribution<float> noise(-0.3f, 0.3f);
    std::vector<StereoFrame> white(kRate * 12);
    for (auto& f : white)
        f = { noise(rng), noise(rng) };
    TempoAnalyzer noisy;
    noisy.prepare(kRate);
    feed(noisy, white);
    CHECK(! noisy.latest().valid);
}

TEST_CASE("Tempo: goes back to unknown after the music stops")
{
    TempoAnalyzer analyzer;
    analyzer.prepare(kRate);
    feed(analyzer, drumLoop(124.0, 12.0));
    CHECK(analyzer.latest().valid);
    feed(analyzer, std::vector<StereoFrame>(kRate * 10));
    CHECK(! analyzer.latest().valid);
}

TEST_CASE("Tempo: follows a tempo change after it settles")
{
    TempoAnalyzer analyzer;
    analyzer.prepare(kRate);
    feed(analyzer, drumLoop(120.0, 12.0));
    CHECK_NEAR(analyzer.latest().bpm, 120.0, 1.0);
    feed(analyzer, drumLoop(150.0, 14.0));
    CHECK_NEAR(analyzer.latest().bpm, 150.0, 1.5);
}

TEST_CASE("Tempo: beat phase lines up with the kicks")
{
    // 120 BPM, ending a quarter of a beat (125 ms) after a kick.
    TempoAnalyzer analyzer;
    analyzer.prepare(kRate);
    feed(analyzer, drumLoop(120.0, 12.125));
    const float phase = analyzer.latest().beatPhase;
    // Onsets are seen ~10-20 ms late (hop and window), i.e. a few hundredths of a beat.
    CHECK(phase > 0.12f && phase < 0.32f);
}

TEST_CASE("Tempo tracker: holds the shown tempo until a new one is consistent for ~3 s")
{
    TempoTracker tracker;
    for (int i = 0; i < 10; ++i)
        tracker.update(true, 120.0f, 0.8f);
    CHECK(tracker.valid());
    CHECK_NEAR(tracker.bpm(), 120.0, 0.1);

    for (int i = 0; i < 3; ++i)
        tracker.update(true, 132.0f, 0.8f);
    CHECK_NEAR(tracker.bpm(), 120.0, 0.1); // a brief disagreement doesn't move it

    for (int i = 0; i < 4; ++i)
        tracker.update(true, 132.0f, 0.8f);
    CHECK_NEAR(tracker.bpm(), 132.0, 0.5);

    for (int i = 0; i < 8; ++i)
        tracker.update(false, 0.0f, 0.0f);
    CHECK(! tracker.valid());
}

TEST_CASE("Tap tempo: steady taps, a mistimed tap, and a pause resetting it")
{
    TapTempo tap;
    for (int i = 0; i < 6; ++i)
        tap.tap(1.0 + i * 0.5);
    CHECK(tap.hasTempo());
    CHECK_NEAR(tap.bpm(), 120.0, 0.01);

    TapTempo sloppy;
    const double times[] = { 0.0, 0.5, 1.0, 1.62, 2.0, 2.5, 3.0 }; // one late tap
    for (double t : times)
        sloppy.tap(t);
    CHECK_NEAR(sloppy.bpm(), 120.0, 3.0);

    TapTempo paused;
    paused.tap(0.0);
    paused.tap(0.4);
    paused.tap(3.0); // > 2 s later: starts again
    CHECK(! paused.hasTempo());
}
