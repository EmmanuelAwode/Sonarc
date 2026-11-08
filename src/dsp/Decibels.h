#pragma once

#include <algorithm>
#include <cmath>

namespace sonarc {

// Lowest level any meter reports. Anything quieter is shown as "silence".
inline constexpr float kSilenceDb = -150.0f;

inline float gainToDb(float gain) noexcept
{
    return gain > 0.0f ? std::max(kSilenceDb, 20.0f * std::log10(gain)) : kSilenceDb;
}

inline float powerToDb(double power) noexcept
{
    return power > 0.0 ? std::max(kSilenceDb, static_cast<float>(10.0 * std::log10(power))) : kSilenceDb;
}

} // namespace sonarc
