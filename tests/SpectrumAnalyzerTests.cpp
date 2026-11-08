#include "TestHarness.h"
#include "dsp/SpectrumAnalyzer.h"

#include <algorithm>
#include <numbers>

using namespace sonarc;

namespace {

constexpr std::uint32_t kRate = 48000;

std::vector<StereoFrame> sine(double hz, float leftAmplitude, float rightAmplitude, std::size_t frames)
{
    std::vector<StereoFrame> out(frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        const auto s = static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kRate));
        out[i] = { leftAmplitude * s, rightAmplitude * s };
    }
    return out;
}

SpectrumAnalyzer analyse(const std::vector<StereoFrame>& audio, int fftSize)
{
    SpectrumAnalyzer analyzer;
    analyzer.prepare(kRate, fftSize);
    analyzer.setBallistics({ 0.0f, 1000.0f, 0.0f, 1000.0f });
    analyzer.pushSamples(audio.data(), audio.size());
    analyzer.analyze(0.0);
    return analyzer;
}

std::size_t loudestBin(const std::vector<float>& db)
{
    return static_cast<std::size_t>(std::max_element(db.begin(), db.end()) - db.begin());
}

} // namespace

TEST_CASE("Full-scale bin-centred sine reads 0 dBFS at the right bin")
{
    constexpr int size = 4096;
    const double hz = 88.0 * kRate / size; // exactly bin 88 (1031.25 Hz)
    const auto analyzer = analyse(sine(hz, 1.0f, 1.0f, size * 2), size);

    const auto& db = analyzer.instantaneousDb();
    CHECK(loudestBin(db) == 88);
    CHECK_NEAR(db[88], 0.0, 0.05);
}

TEST_CASE("Half amplitude reads -6.02 dBFS")
{
    constexpr int size = 2048;
    const double hz = 100.0 * kRate / size;
    const auto analyzer = analyse(sine(hz, 0.5f, 0.5f, size), size);
    CHECK_NEAR(analyzer.instantaneousDb()[100], -6.02, 0.05);
}

TEST_CASE("Off-bin 1 kHz sine peaks at the nearest bin within Hann scalloping loss")
{
    for (int size : { 1024, 2048, 4096, 8192 })
    {
        const auto analyzer = analyse(sine(1000.0, 1.0f, 1.0f, static_cast<std::size_t>(size) * 2), size);
        const auto& db = analyzer.instantaneousDb();
        const auto peak = loudestBin(db);
        const double expectedBin = 1000.0 * size / kRate;

        CHECK(std::abs(static_cast<double>(peak) - expectedBin) <= 0.5);
        CHECK(db[peak] <= 0.01);
        CHECK(db[peak] >= -1.43);
    }
}

TEST_CASE("Hann leakage far from the tone is below -60 dB")
{
    constexpr int size = 4096;
    const auto analyzer = analyse(sine(1000.0, 1.0f, 1.0f, size), size);
    const auto& db = analyzer.instantaneousDb();
    const double toneBin = 1000.0 * size / kRate;

    float worst = -1000.0f;
    for (std::size_t k = 1; k < db.size(); ++k)
        if (std::abs(static_cast<double>(k) - toneBin) > 12.0)
            worst = std::max(worst, db[k]);

    CHECK(worst < -60.0f);
}

TEST_CASE("Spectrum analyses the mid signal: left-only sine reads -6 dB")
{
    constexpr int size = 2048;
    const double hz = 64.0 * kRate / size;
    const auto analyzer = analyse(sine(hz, 1.0f, 0.0f, size), size);
    CHECK_NEAR(analyzer.instantaneousDb()[64], -6.02, 0.05);
}

TEST_CASE("Silence reads the floor")
{
    const auto analyzer = analyse(std::vector<StereoFrame>(4096), 4096);
    for (float v : analyzer.instantaneousDb())
        CHECK(v <= -140.0f);
}

TEST_CASE("Displayed spectrum falls no faster than the decay rate")
{
    constexpr int size = 1024;
    SpectrumAnalyzer analyzer;
    analyzer.prepare(kRate, size);
    analyzer.setBallistics({ 0.0f, 30.0f, 0.0f, 30.0f });

    const double hz = 20.0 * kRate / size;
    const auto tone = sine(hz, 1.0f, 1.0f, size);
    analyzer.pushSamples(tone.data(), tone.size());
    analyzer.analyze(0.0);
    CHECK_NEAR(analyzer.displayDb()[20], 0.0, 0.05);

    // Replace the history with silence and advance 0.5 s: expect a 15 dB fall.
    const std::vector<StereoFrame> silence(size);
    analyzer.pushSamples(silence.data(), silence.size());
    analyzer.analyze(0.5);
    CHECK_NEAR(analyzer.displayDb()[20], -15.0, 0.1);
}

TEST_CASE("Peak hold keeps the maximum for the hold time")
{
    constexpr int size = 1024;
    SpectrumAnalyzer analyzer;
    analyzer.prepare(kRate, size);
    analyzer.setBallistics({ 0.0f, 1000.0f, 1.0f, 20.0f });

    const auto tone = sine(20.0 * kRate / size, 1.0f, 1.0f, size);
    const std::vector<StereoFrame> silence(size);

    analyzer.pushSamples(tone.data(), tone.size());
    analyzer.analyze(0.0);
    analyzer.pushSamples(silence.data(), silence.size());
    analyzer.analyze(0.5);
    CHECK_NEAR(analyzer.peakHoldDb()[20], 0.0, 0.05); // still held

    analyzer.analyze(0.6); // hold expires
    analyzer.analyze(0.5); // then falls at 20 dB/s
    CHECK_NEAR(analyzer.peakHoldDb()[20], -10.0, 0.1);
}
