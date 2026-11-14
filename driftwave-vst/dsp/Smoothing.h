#pragma once

#include <cmath>

namespace driftwave {

/** How many samples share one set of filter coefficients / control values. */
inline constexpr int kControlInterval = 16;

inline float dbToGain(float db) noexcept { return std::pow(10.0f, db * 0.05f); }

/** One-pole parameter smoother. Call next() once per tick of whatever rate it was set up for. */
class Smoother {
public:
    /** @param seconds  time constant (63% of the way to a new target)
        @param tickRate how often next() is called, in Hz */
    void setTime(float seconds, float tickRate) noexcept
    {
        coeff = seconds > 0.0f ? std::exp(-1.0f / (seconds * tickRate)) : 0.0f;
    }

    void snap(float v) noexcept { value = v; }
    float current() const noexcept { return value; }

    float next(float target) noexcept
    {
        value = target + coeff * (value - target);
        // Land exactly on the target so the tail of the ramp never sits in denormals.
        if (std::abs(value - target) < 1.0e-7f)
            value = target;
        return value;
    }

private:
    float coeff = 0.0f;
    float value = 0.0f;
};

} // namespace driftwave
