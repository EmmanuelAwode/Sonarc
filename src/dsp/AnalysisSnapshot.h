#pragma once

#include "dsp/LevelMeter.h"

#include <cstdint>
#include <vector>

namespace sonarc {

// Everything the UI needs to draw one frame. Produced by the analysis thread,
// copied out by the UI thread.
struct AnalysisSnapshot
{
    std::uint64_t sequence = 0;        // increases with every published analysis
    std::uint32_t sampleRate = 0;
    int fftSize = 0;
    std::vector<float> spectrumDb;     // fftSize/2 + 1 bins, smoothed, dBFS
    std::vector<float> spectrumPeakDb; // per-bin peak hold, dBFS
    LevelReading left, right;
    std::uint64_t droppedFrames = 0;   // frames lost because analysis fell behind
};

} // namespace sonarc
