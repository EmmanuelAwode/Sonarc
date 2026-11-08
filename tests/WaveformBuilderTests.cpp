#include "TestHarness.h"
#include "dsp/WaveformBuilder.h"

#include <numbers>

using namespace sonarc;

TEST_CASE("Waveform buckets hold the exact min and max of each run")
{
    WaveformBuilder builder;
    constexpr auto n = WaveformBuilder::kFramesPerBucket;

    std::vector<StereoFrame> frames(n * 2);
    for (std::size_t i = 0; i < frames.size(); ++i)
        frames[i] = { static_cast<float>(i) * 0.1f, -static_cast<float>(i) * 0.1f };
    frames[3].left = 0.95f; // single-sample peak must survive

    std::vector<WaveBucket> out(WaveformBuilder::maxBucketsFor(frames.size()));
    const auto produced = builder.process(frames.data(), frames.size(), out.data());

    CHECK(produced == 2);
    CHECK_NEAR(out[0].maxLeft, 0.95, 1e-6);
    CHECK_NEAR(out[0].minLeft, 0.0, 1e-6);
    CHECK_NEAR(out[0].minRight, -0.7, 1e-6);
    CHECK_NEAR(out[1].minLeft, 0.8, 1e-6);
    CHECK_NEAR(out[1].maxLeft, 1.5, 1e-6);
}

TEST_CASE("Waveform mid is computed per sample, not from channel extremes")
{
    WaveformBuilder builder;
    // L and R in opposite polarity: mid is exactly zero although each channel swings fully.
    std::vector<StereoFrame> frames(WaveformBuilder::kFramesPerBucket);
    for (std::size_t i = 0; i < frames.size(); ++i)
    {
        const float s = (i % 2 == 0) ? 1.0f : -1.0f;
        frames[i] = { s, -s };
    }

    WaveBucket out[2];
    CHECK(builder.process(frames.data(), frames.size(), out) == 1);
    CHECK_NEAR(out[0].maxLeft, 1.0, 1e-6);
    CHECK_NEAR(out[0].minLeft, -1.0, 1e-6);
    CHECK_NEAR(out[0].maxMid, 0.0, 1e-6);
    CHECK_NEAR(out[0].minMid, 0.0, 1e-6);
}

TEST_CASE("Waveform partial buckets carry over between calls")
{
    WaveformBuilder builder;
    const StereoFrame frame { 0.5f, 0.25f };
    WaveBucket out[2];

    std::size_t produced = 0;
    for (std::size_t i = 0; i < WaveformBuilder::kFramesPerBucket - 1; ++i)
        produced += builder.process(&frame, 1, out);
    CHECK(produced == 0);

    const StereoFrame last { -0.5f, 0.75f };
    CHECK(builder.process(&last, 1, out) == 1);
    CHECK_NEAR(out[0].minLeft, -0.5, 1e-6);
    CHECK_NEAR(out[0].maxLeft, 0.5, 1e-6);
    CHECK_NEAR(out[0].maxRight, 0.75, 1e-6);
}

TEST_CASE("Waveform of a full-scale sine spans -1 to +1")
{
    WaveformBuilder builder;
    constexpr std::size_t frames = 48000;
    std::vector<StereoFrame> audio(frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        const auto s = static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(i) / 48000.0));
        audio[i] = { s, 0.5f * s };
    }

    std::vector<WaveBucket> out(WaveformBuilder::maxBucketsFor(frames));
    const auto produced = builder.process(audio.data(), frames, out.data());
    CHECK(produced == frames / WaveformBuilder::kFramesPerBucket);

    float lo = 0, hi = 0, hiRight = 0;
    for (std::size_t i = 0; i < produced; ++i)
    {
        lo = std::min(lo, out[i].minLeft);
        hi = std::max(hi, out[i].maxLeft);
        hiRight = std::max(hiRight, out[i].maxRight);
    }
    CHECK_NEAR(hi, 1.0, 1e-4);
    CHECK_NEAR(lo, -1.0, 1e-4);
    CHECK_NEAR(hiRight, 0.5, 1e-4);
}
