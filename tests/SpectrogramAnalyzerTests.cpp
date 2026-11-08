#include "TestHarness.h"
#include "dsp/SpectrogramAnalyzer.h"

#include <algorithm>
#include <numbers>

using namespace sonarc;

namespace {

constexpr std::uint32_t kRate = 48000;

std::vector<StereoFrame> sine(double hz, float amplitude, std::size_t frames)
{
    std::vector<StereoFrame> out(frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        const auto s = amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kRate));
        out[i] = { s, s };
    }
    return out;
}

// Runs audio through the analyser and returns the last row.
SpectrogramRow lastRow(int fftSize, const std::vector<StereoFrame>& audio, std::size_t* rowCount = nullptr)
{
    SpectrogramAnalyzer analyzer;
    analyzer.prepare(kRate, fftSize);
    SpectrogramRow last;
    std::size_t rows = 0;
    analyzer.process(audio.data(), audio.size(), [&](const SpectrogramRow& row) { last = row; ++rows; });
    if (rowCount != nullptr)
        *rowCount = rows;
    return last;
}

int loudestBand(const SpectrogramRow& row)
{
    return static_cast<int>(std::max_element(row.db.begin(), row.db.end()) - row.db.begin());
}

} // namespace

TEST_CASE("Spectrogram band frequencies span 20 Hz to 20 kHz logarithmically")
{
    CHECK_NEAR(SpectrogramAnalyzer::bandFrequency(-0.5f), 20.0, 1e-3);
    CHECK_NEAR(SpectrogramAnalyzer::bandFrequency(kSpectrogramBands - 0.5f), 20000.0, 0.05);
    // The geometric middle of 20 Hz-20 kHz is ~632 Hz.
    CHECK_NEAR(SpectrogramAnalyzer::bandFrequency(kSpectrogramBands / 2.0f - 0.5f), 632.46, 0.05);
}

TEST_CASE("Spectrogram emits one row per hop")
{
    std::size_t rows = 0;
    lastRow(2048, std::vector<StereoFrame>(SpectrogramAnalyzer::kHopFrames * 37 + 100), &rows);
    CHECK(rows == 37);
}

TEST_CASE("Spectrogram puts a tone in the right band at the right level")
{
    for (double hz : { 100.0, 1000.0, 5000.0, 12000.0 })
    {
        const auto row = lastRow(4096, sine(hz, 1.0f, 16384));
        const int band = loudestBand(row);
        const double bandHz = SpectrogramAnalyzer::bandFrequency(static_cast<float>(band));

        // As close as the FFT allows: within half a bin (11.7 Hz bins at 4096 / 48 kHz)
        // or one band (~1.4 % of frequency), whichever is wider. Level within Hann scalloping of 0 dBFS.
        const double halfBinHz = 0.5 * kRate / 4096.0;
        const double bandWidthHz = hz * (std::pow(1000.0, 1.0 / kSpectrogramBands) - 1.0);
        CHECK(std::abs(bandHz - hz) <= std::max(halfBinHz, bandWidthHz) + bandWidthHz);
        CHECK(row.db[static_cast<std::size_t>(band)] <= 0.05f);
        CHECK(row.db[static_cast<std::size_t>(band)] >= -1.5f);
    }
}

TEST_CASE("Spectrogram half amplitude reads 6 dB lower")
{
    const auto full = lastRow(4096, sine(1000.0, 1.0f, 16384));
    const auto half = lastRow(4096, sine(1000.0, 0.5f, 16384));
    const auto band = static_cast<std::size_t>(loudestBand(full));
    CHECK_NEAR(full.db[band] - half.db[band], 6.02, 0.05);
}

TEST_CASE("Spectrogram of silence is at the floor")
{
    const auto row = lastRow(2048, std::vector<StereoFrame>(8192));
    for (float v : row.db)
        CHECK(v <= -140.0f);
}
