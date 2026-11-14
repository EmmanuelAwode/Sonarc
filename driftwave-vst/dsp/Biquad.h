#pragma once

#include <algorithm>
#include <cmath>

namespace driftwave {

/** Normalised biquad coefficients (a0 = 1). Formulas: RBJ Audio EQ Cookbook. */
struct BiquadCoeffs {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
};

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kButterworthQ = 0.70710678118654752440;

inline double clampFrequency(double sampleRate, double hz) noexcept
{
    return std::clamp(hz, 5.0, sampleRate * 0.49);
}

inline BiquadCoeffs makeLowPass(double sampleRate, double hz, double q = kButterworthQ) noexcept
{
    const double w = 2.0 * kPi * clampFrequency(sampleRate, hz) / sampleRate;
    const double c = std::cos(w), alpha = std::sin(w) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    return { float((1.0 - c) * 0.5 / a0), float((1.0 - c) / a0), float((1.0 - c) * 0.5 / a0),
             float(-2.0 * c / a0), float((1.0 - alpha) / a0) };
}

inline BiquadCoeffs makeHighPass(double sampleRate, double hz, double q = kButterworthQ) noexcept
{
    const double w = 2.0 * kPi * clampFrequency(sampleRate, hz) / sampleRate;
    const double c = std::cos(w), alpha = std::sin(w) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    return { float((1.0 + c) * 0.5 / a0), float(-(1.0 + c) / a0), float((1.0 + c) * 0.5 / a0),
             float(-2.0 * c / a0), float((1.0 - alpha) / a0) };
}

/** Low shelf. q = 1/sqrt(2) is the cookbook's "shelf slope S = 1", which is what
    the Web Audio lowshelf in the Driftwave app uses. */
inline BiquadCoeffs makeLowShelf(double sampleRate, double hz, double gainDb, double q = kButterworthQ) noexcept
{
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w = 2.0 * kPi * clampFrequency(sampleRate, hz) / sampleRate;
    const double c = std::cos(w), alpha = std::sin(w) / (2.0 * q);
    const double k = 2.0 * std::sqrt(A) * alpha;
    const double a0 = (A + 1.0) + (A - 1.0) * c + k;
    return { float(A * ((A + 1.0) - (A - 1.0) * c + k) / a0),
             float(2.0 * A * ((A - 1.0) - (A + 1.0) * c) / a0),
             float(A * ((A + 1.0) - (A - 1.0) * c - k) / a0),
             float(-2.0 * ((A - 1.0) + (A + 1.0) * c) / a0),
             float(((A + 1.0) + (A - 1.0) * c - k) / a0) };
}

/** Transposed direct form II: the form that copes best with coefficients changing while running. */
struct BiquadState {
    float s1 = 0.0f, s2 = 0.0f;

    void reset() noexcept { s1 = s2 = 0.0f; }

    float process(const BiquadCoeffs& c, float x) noexcept
    {
        const float y = c.b0 * x + s1;
        s1 = c.b1 * x - c.a1 * y + s2;
        s2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

} // namespace driftwave
