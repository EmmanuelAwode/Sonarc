#pragma once

#include "dsp/Fft.h"

#include <complex>
#include <cstddef>
#include <memory>
#include <vector>

namespace sonarc {

// Hann-windowed FFT -> level per bin in dBFS, calibrated so a full-scale sine
// centred on a bin reads 0 dBFS (a sine between bins reads up to 1.42 dB low:
// Hann scalloping loss). Shared by the spectrum analyser and the spectrogram.
class MagnitudeSpectrum
{
public:
    // `fftSize` must be a power of two. Allocates; everything else doesn't.
    void prepare(int fftSize);

    int size() const noexcept { return n; }
    int binCount() const noexcept { return n / 2 + 1; }

    // `history` is a ring of exactly size() samples and `oldest` the index of its
    // oldest sample. Writes binCount() values to `dbOut`.
    void compute(const float* history, std::size_t oldest, float* dbOut) noexcept;

private:
    int n = 0;
    std::size_t mask = 0;
    std::unique_ptr<RealFft> fft;
    std::vector<float> window, frame;
    std::vector<std::complex<float>> bins;
    float binScale = 0.0f;
};

} // namespace sonarc
