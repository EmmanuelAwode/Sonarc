#pragma once

#include <cmath>
#include <utility>
#include <vector>

namespace driftwave {

/** Radix-2 complex FFT on separate real and imaginary arrays. prepare() allocates
    its tables; the transforms do not allocate. */
class Fft {
public:
    void prepare(int fftSize)
    {
        n = fftSize;
        reversed.assign(size_t(n), 0);
        int bits = 0;
        while ((1 << bits) < n)
            ++bits;
        for (int i = 0; i < n; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b))
                    r |= 1 << (bits - 1 - b);
            reversed[size_t(i)] = r;
        }
        cosTable.assign(size_t(n / 2), 0.0f);
        sinTable.assign(size_t(n / 2), 0.0f);
        for (int i = 0; i < n / 2; ++i) {
            const double angle = -2.0 * 3.14159265358979323846 * double(i) / double(n);
            cosTable[size_t(i)] = float(std::cos(angle));
            sinTable[size_t(i)] = float(std::sin(angle));
        }
    }

    int size() const noexcept { return n; }

    void forward(float* re, float* im) const noexcept
    {
        for (int i = 0; i < n; ++i) {
            const int j = reversed[size_t(i)];
            if (j > i) {
                std::swap(re[i], re[j]);
                std::swap(im[i], im[j]);
            }
        }
        for (int half = 1; half < n; half *= 2) {
            const int step = n / (half * 2);
            for (int start = 0; start < n; start += half * 2) {
                for (int k = 0; k < half; ++k) {
                    const float wr = cosTable[size_t(k * step)], wi = sinTable[size_t(k * step)];
                    const int a = start + k, b = a + half;
                    const float tr = re[b] * wr - im[b] * wi;
                    const float ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
            }
        }
    }

    /** Includes the 1/N scaling. */
    void inverse(float* re, float* im) const noexcept
    {
        forward(im, re); // swapping the parts turns the forward transform into the inverse
        const float scale = 1.0f / float(n);
        for (int i = 0; i < n; ++i) {
            re[i] *= scale;
            im[i] *= scale;
        }
    }

private:
    int n = 0;
    std::vector<int> reversed;
    std::vector<float> cosTable, sinTable;
};

} // namespace driftwave
