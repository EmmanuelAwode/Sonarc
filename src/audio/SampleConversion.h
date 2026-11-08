#pragma once

#include "audio/AudioTypes.h"

#include <cstdint>
#include <cstring>

namespace sonarc {

inline std::size_t bytesPerSample(SampleEncoding encoding) noexcept
{
    switch (encoding)
    {
        case SampleEncoding::Int16:       return 2;
        case SampleEncoding::Int24Packed: return 3;
        case SampleEncoding::Float32:
        case SampleEncoding::Int32:       return 4;
        case SampleEncoding::Unknown:     break;
    }
    return 0;
}

inline float readSample(const std::uint8_t* p, SampleEncoding encoding) noexcept
{
    switch (encoding)
    {
        case SampleEncoding::Float32:
        {
            float v;
            std::memcpy(&v, p, sizeof v);
            return v;
        }
        case SampleEncoding::Int16:
        {
            std::int16_t v;
            std::memcpy(&v, p, sizeof v);
            return static_cast<float>(v) / 32768.0f;
        }
        case SampleEncoding::Int24Packed:
        {
            // Little-endian 24-bit; shift into the top of a 32-bit word so the sign is kept.
            const auto bits = (static_cast<std::uint32_t>(p[0]) << 8)
                            | (static_cast<std::uint32_t>(p[1]) << 16)
                            | (static_cast<std::uint32_t>(p[2]) << 24);
            return static_cast<float>(static_cast<std::int32_t>(bits) / 2147483648.0);
        }
        case SampleEncoding::Int32:
        {
            std::int32_t v;
            std::memcpy(&v, p, sizeof v);
            return static_cast<float>(v / 2147483648.0);
        }
        case SampleEncoding::Unknown:
            break;
    }
    return 0.0f;
}

// Converts interleaved device samples to stereo frames. Mono input is copied to
// both channels; channels beyond the first two (surround) are ignored.
inline void convertToStereo(const std::uint8_t* source,
                            std::size_t frameCount,
                            const StreamFormat& format,
                            StereoFrame* destination) noexcept
{
    const auto sampleBytes = bytesPerSample(format.encoding);
    const auto frameBytes = format.bytesPerFrame != 0 ? std::size_t { format.bytesPerFrame }
                                                      : sampleBytes * format.channels;

    for (std::size_t i = 0; i < frameCount; ++i)
    {
        const auto* frame = source + i * frameBytes;
        const float left = readSample(frame, format.encoding);
        const float right = format.channels > 1 ? readSample(frame + sampleBytes, format.encoding) : left;
        destination[i] = { left, right };
    }
}

} // namespace sonarc
