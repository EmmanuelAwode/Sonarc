#include "dsp/Fft.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace sonarc {

RealFft::RealFft(int size) : n(size)
{
    if (! isPowerOfTwo(size))
        throw std::invalid_argument("FFT size must be a power of two");

    twiddles.resize(static_cast<std::size_t>(n / 2));
    for (int k = 0; k < n / 2; ++k)
    {
        const double angle = -2.0 * std::numbers::pi * k / n;
        twiddles[static_cast<std::size_t>(k)] = { static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)) };
    }

    int bits = 0;
    while ((1 << bits) < n)
        ++bits;

    bitReversed.resize(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        std::uint32_t reversed = 0;
        for (int b = 0; b < bits; ++b)
            if ((i >> b) & 1)
                reversed |= 1u << (bits - 1 - b);
        bitReversed[static_cast<std::size_t>(i)] = reversed;
    }

    work.resize(static_cast<std::size_t>(n));
}

void RealFft::forward(const float* input, std::complex<float>* output) noexcept
{
    for (int i = 0; i < n; ++i)
        work[bitReversed[static_cast<std::size_t>(i)]] = { input[i], 0.0f };

    // Iterative Cooley-Tukey butterflies.
    for (int length = 2; length <= n; length <<= 1)
    {
        const int half = length / 2;
        const int twiddleStep = n / length;

        for (int start = 0; start < n; start += length)
        {
            for (int j = 0; j < half; ++j)
            {
                const auto w = twiddles[static_cast<std::size_t>(j * twiddleStep)];
                auto& a = work[static_cast<std::size_t>(start + j)];
                auto& b = work[static_cast<std::size_t>(start + j + half)];
                const auto t = b * w;
                b = a - t;
                a = a + t;
            }
        }
    }

    for (int k = 0; k <= n / 2; ++k)
        output[k] = work[static_cast<std::size_t>(k)];
}

} // namespace sonarc
