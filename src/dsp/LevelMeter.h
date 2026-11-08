#pragma once

#include "audio/AudioTypes.h"
#include "dsp/Decibels.h"

#include <cstdint>

namespace sonarc {

struct LevelReading
{
    float peakDb = kSilenceDb; // sample peak with release ballistics (dBFS)
    float holdDb = kSilenceDb; // peak-hold marker (dBFS)
    float rmsDb = kSilenceDb;  // exponential RMS, sine = -3.01 dBFS at full scale
    float maxDb = kSilenceDb;  // highest sample peak since the last reset (dBFS)
    bool clipped = false;      // a sample reached or exceeded 0 dBFS since the last reset
};

struct LevelMeterSettings
{
    float releaseDbPerSecond = 24.0f;
    float peakHoldSeconds = 1.5f;
    float rmsTimeConstantSeconds = 0.3f;
};

// Stereo sample-peak + RMS meter.
// Note: this is *sample* peak. Inter-sample (true) peak per ITU-R BS.1770
// requires oversampling and is planned for the loudness module.
class StereoLevelMeter
{
public:
    void prepare(std::uint32_t sampleRate);
    void setSettings(const LevelMeterSettings& newSettings);

    void process(const StereoFrame* frames, std::size_t count) noexcept;
    void resetMax() noexcept;

    LevelReading left() const noexcept { return reading(channels[0]); }
    LevelReading right() const noexcept { return reading(channels[1]); }

private:
    struct Channel
    {
        float peakDb = kSilenceDb;
        float holdDb = kSilenceDb;
        float holdRemaining = 0.0f;
        double meanSquare = 0.0;
        float maxDb = kSilenceDb;
        bool clipped = false;
    };

    static LevelReading reading(const Channel& c) noexcept;
    void updateBallistics(Channel& c, float blockPeak, float dt) noexcept;

    std::uint32_t rate = 0;
    LevelMeterSettings settings;
    double rmsCoefficient = 0.0;
    Channel channels[2];
};

} // namespace sonarc
