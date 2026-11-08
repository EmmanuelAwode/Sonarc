#pragma once

#include <complex>
#include <cstdint>
#include <vector>

namespace sonarc {

// Radix-2 FFT for real input. All buffers are allocated in the constructor;
// forward() never allocates.
class RealFft
{
public:
    // `size` must be a power of two >= 2.
    explicit RealFft(int size);

    int size() const noexcept { return n; }

    // Writes bins 0..N/2 (N/2 + 1 values) of the DFT of `input` (N samples).
    // Uses the unnormalised convention X[k] = sum x[n] e^(-2*pi*i*k*n/N).
    void forward(const float* input, std::complex<float>* output) noexcept;

    static bool isPowerOfTwo(int value) noexcept { return value >= 2 && (value & (value - 1)) == 0; }

private:
    int n;
    std::vector<std::complex<float>> twiddles;
    std::vector<std::uint32_t> bitReversed;
    std::vector<std::complex<float>> work;
};

} // namespace sonarc
