#pragma once

#include "audio/CaptureBackend.h"
#include "dsp/AnalysisSnapshot.h"
#include "dsp/SpectrogramAnalyzer.h"
#include "dsp/WaveformBuilder.h"
#include "util/SpscRingBuffer.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace sonarc {

// Owns the capture backend and the analysis thread.
//
//   capture thread --(lock-free ring)--> analysis thread --(snapshot)--> UI thread
//
// The capture thread only converts samples and pushes them into the ring.
// FFTs and meter ballistics run on the analysis thread, and the UI copies the
// latest finished result whenever it draws a frame.
//
// All public methods are for the UI (message) thread.
class AudioEngine final : private FrameSink
{
public:
    explicit AudioEngine(std::unique_ptr<CaptureBackend> captureBackend);
    ~AudioEngine() override;

    std::vector<AudioDeviceInfo> listDevices();
    const char* backendName() const noexcept { return backend->backendName(); }

    bool open(const DeviceSelection& selection);
    void close();
    const DeviceSelection& selection() const noexcept { return currentSelection; }
    CaptureStatus status() const { return backend->status(); }

    // Call a few times per second. Handles device loss and default-device
    // changes. Returns true if the system's device list changed since last call.
    bool service();
    void setAutoReconnect(bool shouldReconnect) noexcept { autoReconnect = shouldReconnect; }

    void setFftSize(int size) noexcept;
    void setSpectrumSmoothing(float amount) noexcept { spectrumSmoothing = amount; }
    void setSpectrumDecay(float dbPerSecond) noexcept { spectrumDecay = dbPerSecond; }
    void setPeakHoldSeconds(float seconds) noexcept { peakHoldSeconds = seconds; }
    void resetPeaks() noexcept { resetPeaksRequested = true; }

    // Copies the newest analysis into `destination` if it is newer than what
    // `destination` already holds. Returns true if it copied.
    bool readSnapshot(AnalysisSnapshot& destination);

    // Pops waveform peak buckets produced since the last call (UI thread only).
    // Each bucket covers WaveformBuilder::kFramesPerBucket frames at the
    // snapshot's sample rate.
    std::size_t readWaveform(WaveBucket* destination, std::size_t maxCount) noexcept
    {
        return waveformRing.pop(destination, maxCount);
    }

    // Spectrogram rows, one per SpectrogramAnalyzer::kHopFrames frames. Only
    // computed while enabled, so a hidden spectrogram costs nothing.
    void setSpectrogramEnabled(bool enabled) noexcept { spectrogramEnabled = enabled; }
    void setSpectrogramFftSize(int size) noexcept;
    std::size_t readSpectrogram(SpectrogramRow* destination, std::size_t maxCount) noexcept
    {
        return spectrogramRing.pop(destination, maxCount);
    }

private:
    void writeFrames(const StereoFrame* frames, std::size_t count) noexcept override;
    bool restartStream();
    void analysisLoop();

    std::unique_ptr<CaptureBackend> backend;
    DeviceSelection currentSelection;
    bool isOpen = false;
    bool autoReconnect = true;
    std::chrono::steady_clock::time_point nextReconnectAttempt;
    std::atomic<bool> devicesChanged { false };

    // capture -> analysis
    SpscRingBuffer<StereoFrame> ring { 1 << 17 };
    std::atomic<std::uint64_t> droppedFrames { 0 };
    std::atomic<std::uint32_t> streamSampleRate { 0 };
    std::atomic<std::uint64_t> streamGeneration { 0 };
    std::mutex wakeLock;
    std::condition_variable wakeAnalysis;

    // UI -> analysis
    std::atomic<int> fftSize { 4096 };
    std::atomic<float> spectrumSmoothing { 0.6f };
    std::atomic<float> spectrumDecay { 60.0f };
    std::atomic<float> peakHoldSeconds { 1.5f };
    std::atomic<bool> resetPeaksRequested { false };
    std::atomic<bool> spectrogramEnabled { false };
    std::atomic<int> spectrogramFftSize { 2048 };

    // analysis -> UI
    std::mutex snapshotLock;
    AnalysisSnapshot published;
    SpscRingBuffer<WaveBucket> waveformRing { 1 << 17 }; // ~22 s of peaks at 48 kHz
    SpscRingBuffer<SpectrogramRow> spectrogramRing { 1024 }; // ~5 s of rows at 48 kHz

    std::atomic<bool> quit { false };
    std::thread analysisThread;
};

} // namespace sonarc
