#pragma once

#include "dsp/LevelMeter.h"

#include <cstdint>
#include <vector>

namespace sonarc {

struct TempoReading
{
    bool valid = false;
    float bpm = 0.0f;
    float confidence = 0.0f; // 0..1
    float beatPhase = 0.0f;  // at `analysisTimeMs`; 0 = on a beat
};

struct KeyReading
{
    bool valid = false;
    int key = -1;            // see keys:: in KeyDetector.h
    float confidence = 0.0f; // 0..1
};

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

    TempoReading tempo;                // only updated while tempo detection is enabled
    KeyReading key;                    // only updated while key detection is enabled
    double analysisTimeMs = 0.0;       // steady_clock time this snapshot was produced
};

} // namespace sonarc
