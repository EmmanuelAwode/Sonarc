#include "TestHarness.h"
#include "audio/SampleConversion.h"

using namespace sonarc;

TEST_CASE("Float32 stereo passes through")
{
    const float samples[] = { 0.25f, -0.5f, 1.0f, -1.0f };
    StreamFormat format { 48000, 2, 32, 8, SampleEncoding::Float32 };
    StereoFrame out[2];
    convertToStereo(reinterpret_cast<const std::uint8_t*>(samples), 2, format, out);

    CHECK(out[0].left == 0.25f && out[0].right == -0.5f);
    CHECK(out[1].left == 1.0f && out[1].right == -1.0f);
}

TEST_CASE("Int16 converts to [-1, 1)")
{
    const std::int16_t samples[] = { 16384, -32768, 32767, 0 };
    StreamFormat format { 44100, 2, 16, 4, SampleEncoding::Int16 };
    StereoFrame out[2];
    convertToStereo(reinterpret_cast<const std::uint8_t*>(samples), 2, format, out);

    CHECK_NEAR(out[0].left, 0.5, 1e-6);
    CHECK_NEAR(out[0].right, -1.0, 1e-6);
    CHECK_NEAR(out[1].left, 32767.0 / 32768.0, 1e-6);
    CHECK_NEAR(out[1].right, 0.0, 1e-9);
}

TEST_CASE("Packed 24-bit keeps the sign")
{
    // 0x400000 = +0.5, 0xC00000 = -0.5 (little endian)
    const std::uint8_t bytes[] = { 0x00, 0x00, 0x40, 0x00, 0x00, 0xC0 };
    StreamFormat format { 96000, 2, 24, 6, SampleEncoding::Int24Packed };
    StereoFrame out[1];
    convertToStereo(bytes, 1, format, out);

    CHECK_NEAR(out[0].left, 0.5, 1e-7);
    CHECK_NEAR(out[0].right, -0.5, 1e-7);
}

TEST_CASE("Int32 (24 valid bits in 32-bit container)")
{
    const std::int32_t samples[] = { 0x40000000, -0x40000000 };
    StreamFormat format { 48000, 2, 24, 8, SampleEncoding::Int32 };
    StereoFrame out[1];
    convertToStereo(reinterpret_cast<const std::uint8_t*>(samples), 1, format, out);

    CHECK_NEAR(out[0].left, 0.5, 1e-7);
    CHECK_NEAR(out[0].right, -0.5, 1e-7);
}

TEST_CASE("Mono is duplicated to both channels")
{
    const float samples[] = { 0.1f, 0.2f, 0.3f };
    StreamFormat format { 48000, 1, 32, 4, SampleEncoding::Float32 };
    StereoFrame out[3];
    convertToStereo(reinterpret_cast<const std::uint8_t*>(samples), 3, format, out);

    for (int i = 0; i < 3; ++i)
        CHECK(out[i].left == samples[i] && out[i].right == samples[i]);
}

TEST_CASE("Surround keeps only front left / right")
{
    // 6 channels: FL FR C LFE SL SR
    const float samples[] = { 0.1f, 0.2f, 0.9f, 0.9f, 0.9f, 0.9f,
                              0.3f, 0.4f, 0.9f, 0.9f, 0.9f, 0.9f };
    StreamFormat format { 48000, 6, 32, 24, SampleEncoding::Float32 };
    StereoFrame out[2];
    convertToStereo(reinterpret_cast<const std::uint8_t*>(samples), 2, format, out);

    CHECK(out[0].left == 0.1f && out[0].right == 0.2f);
    CHECK(out[1].left == 0.3f && out[1].right == 0.4f);
}
