#include "TestHarness.h"
#include "dsp/LevelMeter.h"

#include <numbers>

using namespace sonarc;

namespace {

constexpr std::uint32_t kRate = 48000;

std::vector<StereoFrame> sine(double hz, float amplitude, std::size_t frames)
{
    std::vector<StereoFrame> out(frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        const auto s = amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kRate));
        out[i] = { s, s };
    }
    return out;
}

void feed(StereoLevelMeter& meter, const std::vector<StereoFrame>& audio, std::size_t blockSize = 480)
{
    for (std::size_t i = 0; i < audio.size(); i += blockSize)
        meter.process(audio.data() + i, std::min(blockSize, audio.size() - i));
}

} // namespace

TEST_CASE("Sine at 0.5 amplitude: peak -6.02 dBFS, RMS -9.03 dBFS")
{
    StereoLevelMeter meter;
    meter.prepare(kRate);
    feed(meter, sine(1000.0, 0.5f, kRate * 3)); // 1 kHz divides 48 kHz, so the exact peak is sampled

    CHECK_NEAR(meter.left().peakDb, -6.02, 0.02);
    CHECK_NEAR(meter.right().peakDb, -6.02, 0.02);
    CHECK_NEAR(meter.left().rmsDb, -9.03, 0.1);
    CHECK(! meter.left().clipped);
}

TEST_CASE("Square wave: peak equals RMS")
{
    StereoLevelMeter meter;
    meter.prepare(kRate);

    std::vector<StereoFrame> square(kRate * 3);
    for (std::size_t i = 0; i < square.size(); ++i)
    {
        const float v = (i / 24) % 2 == 0 ? 0.25f : -0.25f;
        square[i] = { v, v };
    }
    feed(meter, square);

    CHECK_NEAR(meter.left().peakDb, -12.04, 0.02);
    CHECK_NEAR(meter.left().rmsDb, -12.04, 0.05);
}

TEST_CASE("Peak falls at the release rate after the signal stops")
{
    StereoLevelMeter meter;
    meter.prepare(kRate);
    meter.setSettings({ 20.0f, 0.0f, 0.3f });

    feed(meter, sine(1000.0, 1.0f, kRate / 10));
    CHECK_NEAR(meter.left().peakDb, 0.0, 0.02);

    feed(meter, std::vector<StereoFrame>(kRate)); // 1 s of silence
    CHECK_NEAR(meter.left().peakDb, -20.0, 0.05);
    CHECK_NEAR(meter.left().maxDb, 0.0, 0.02); // max is not affected by release
}

TEST_CASE("Peak hold stays put for the hold time")
{
    StereoLevelMeter meter;
    meter.prepare(kRate);
    meter.setSettings({ 20.0f, 1.0f, 0.3f });

    feed(meter, sine(1000.0, 0.5f, kRate / 10));
    feed(meter, std::vector<StereoFrame>(kRate / 2)); // 0.5 s
    CHECK_NEAR(meter.left().holdDb, -6.02, 0.02);

    feed(meter, std::vector<StereoFrame>(kRate)); // past the hold time
    CHECK(meter.left().holdDb < -7.0f);
}

TEST_CASE("Clip flag triggers at 0 dBFS and resets")
{
    StereoLevelMeter meter;
    meter.prepare(kRate);

    std::vector<StereoFrame> audio(480, StereoFrame { 0.99f, 0.0f });
    feed(meter, audio);
    CHECK(! meter.left().clipped);

    audio[100] = { 1.0f, 0.0f };
    feed(meter, audio);
    CHECK(meter.left().clipped);
    CHECK(! meter.right().clipped);

    meter.resetMax();
    CHECK(! meter.left().clipped);
}

TEST_CASE("Channels are metered independently")
{
    StereoLevelMeter meter;
    meter.prepare(kRate);

    std::vector<StereoFrame> audio(kRate);
    for (std::size_t i = 0; i < audio.size(); ++i)
        audio[i] = { 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(i) / kRate)), 0.0f };
    feed(meter, audio);

    CHECK_NEAR(meter.left().peakDb, -6.02, 0.02);
    CHECK(meter.right().peakDb <= kSilenceDb);
}
