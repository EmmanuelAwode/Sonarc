#include "dsp/LevelMeter.h"

#include <algorithm>
#include <cmath>

namespace sonarc {

void StereoLevelMeter::prepare(std::uint32_t sampleRate)
{
    rate = sampleRate;
    channels[0] = {};
    channels[1] = {};
    setSettings(settings);
}

void StereoLevelMeter::setSettings(const LevelMeterSettings& newSettings)
{
    settings = newSettings;
    rmsCoefficient = (rate > 0 && settings.rmsTimeConstantSeconds > 0.0f)
                         ? 1.0 - std::exp(-1.0 / (settings.rmsTimeConstantSeconds * rate))
                         : 1.0;
}

void StereoLevelMeter::process(const StereoFrame* frames, std::size_t count) noexcept
{
    if (rate == 0 || count == 0)
        return;

    float blockPeak[2] = { 0.0f, 0.0f };
    double meanSquare[2] = { channels[0].meanSquare, channels[1].meanSquare };

    for (std::size_t i = 0; i < count; ++i)
    {
        const float samples[2] = { frames[i].left, frames[i].right };
        for (int ch = 0; ch < 2; ++ch)
        {
            const float x = samples[ch];
            blockPeak[ch] = std::max(blockPeak[ch], std::abs(x));
            meanSquare[ch] += rmsCoefficient * (static_cast<double>(x) * x - meanSquare[ch]);
        }
    }

    const float dt = static_cast<float>(count) / static_cast<float>(rate);
    for (int ch = 0; ch < 2; ++ch)
    {
        channels[ch].meanSquare = meanSquare[ch];
        updateBallistics(channels[ch], blockPeak[ch], dt);
    }
}

void StereoLevelMeter::updateBallistics(Channel& c, float blockPeak, float dt) noexcept
{
    const float blockDb = gainToDb(blockPeak);
    const float fall = settings.releaseDbPerSecond * dt;

    c.peakDb = std::max(blockDb, c.peakDb - fall); // instant attack, linear-in-dB release

    if (c.peakDb >= c.holdDb)
    {
        c.holdDb = c.peakDb;
        c.holdRemaining = settings.peakHoldSeconds;
    }
    else if (c.holdRemaining > 0.0f)
    {
        c.holdRemaining -= dt;
    }
    else
    {
        c.holdDb = std::max(c.peakDb, c.holdDb - fall);
    }

    c.maxDb = std::max(c.maxDb, blockDb);
    if (blockPeak >= 1.0f)
        c.clipped = true;
}

void StereoLevelMeter::resetMax() noexcept
{
    for (auto& c : channels)
    {
        c.maxDb = c.peakDb;
        c.clipped = false;
    }
}

LevelReading StereoLevelMeter::reading(const Channel& c) noexcept
{
    return { c.peakDb, c.holdDb, powerToDb(c.meanSquare), c.maxDb, c.clipped };
}

} // namespace sonarc
