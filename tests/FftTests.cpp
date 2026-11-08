#include "TestHarness.h"
#include "dsp/Fft.h"

#include <complex>
#include <numbers>
#include <random>

using sonarc::RealFft;

namespace {

std::vector<std::complex<double>> naiveDft(const std::vector<float>& input)
{
    const auto n = input.size();
    std::vector<std::complex<double>> out(n / 2 + 1);
    for (std::size_t k = 0; k <= n / 2; ++k)
        for (std::size_t i = 0; i < n; ++i)
            out[k] += static_cast<double>(input[i]) * std::polar(1.0, -2.0 * std::numbers::pi * static_cast<double>(k * i) / static_cast<double>(n));
    return out;
}

} // namespace

TEST_CASE("FFT matches a naive DFT on random input")
{
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    for (int size : { 2, 8, 64, 1024 })
    {
        std::vector<float> input(static_cast<std::size_t>(size));
        for (auto& x : input)
            x = dist(rng);

        RealFft fft(size);
        std::vector<std::complex<float>> output(static_cast<std::size_t>(size / 2 + 1));
        fft.forward(input.data(), output.data());

        const auto reference = naiveDft(input);
        double maxError = 0.0;
        for (std::size_t k = 0; k < reference.size(); ++k)
            maxError = std::max(maxError, std::abs(std::complex<double>(output[k]) - reference[k]));

        CHECK(maxError < 1e-3 * size);
    }
}

TEST_CASE("FFT of an impulse is flat")
{
    RealFft fft(256);
    std::vector<float> input(256, 0.0f);
    input[0] = 1.0f;
    std::vector<std::complex<float>> output(129);
    fft.forward(input.data(), output.data());

    for (const auto& bin : output)
        CHECK_NEAR(std::abs(bin), 1.0, 1e-5);
}

TEST_CASE("FFT rejects non power-of-two sizes")
{
    bool threw = false;
    try { RealFft fft(1000); }
    catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}
