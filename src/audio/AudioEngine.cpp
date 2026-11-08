#include "audio/AudioEngine.h"
#include "dsp/Fft.h"
#include "dsp/LevelMeter.h"
#include "dsp/SpectrumAnalyzer.h"

#include <algorithm>

namespace sonarc {

using namespace std::chrono_literals;

AudioEngine::AudioEngine(std::unique_ptr<CaptureBackend> captureBackend)
    : backend(std::move(captureBackend))
{
    backend->setDeviceChangeCallback([this] { devicesChanged = true; });
    analysisThread = std::thread([this] { analysisLoop(); });
}

AudioEngine::~AudioEngine()
{
    backend->setDeviceChangeCallback(nullptr);
    backend->stop();

    quit = true;
    wakeAnalysis.notify_all();
    analysisThread.join();
}

std::vector<AudioDeviceInfo> AudioEngine::listDevices()
{
    return backend->enumerateDevices();
}

bool AudioEngine::open(const DeviceSelection& selection)
{
    currentSelection = selection;
    isOpen = true;
    return restartStream();
}

void AudioEngine::close()
{
    isOpen = false;
    backend->stop();
}

bool AudioEngine::restartStream()
{
    backend->stop();
    const bool started = backend->start(currentSelection, *this);

    // Tell the analysis thread to drop anything from the previous stream and
    // re-prepare for the new sample rate.
    streamSampleRate = started ? backend->status().format.sampleRate : 0;
    streamGeneration.fetch_add(1, std::memory_order_release);
    wakeAnalysis.notify_one();

    nextReconnectAttempt = std::chrono::steady_clock::now() + 1s;
    return started;
}

bool AudioEngine::service()
{
    const bool listChanged = devicesChanged.exchange(false);
    if (! isOpen)
        return listChanged;

    const auto current = backend->status();
    bool restart = false;

    if (current.state == CaptureState::DeviceLost && autoReconnect && std::chrono::steady_clock::now() >= nextReconnectAttempt)
        restart = true;

    // When following the Windows default, switch as soon as the default moves.
    if (listChanged && currentSelection.followsDefault() && current.state == CaptureState::Running)
    {
        for (const auto& device : backend->enumerateDevices())
            if (device.kind == currentSelection.kind && device.isDefault && device.id != current.deviceId)
                restart = true;
    }

    // Per-app capture follows the app by name. Apps like browsers run several
    // processes; if the one we captured is silent while another instance of the
    // same app is playing, move to that one (checked every few seconds).
    const auto now = std::chrono::steady_clock::now();
    if (currentSelection.kind == DeviceKind::Application && current.state == CaptureState::Running && now >= nextAppCheck)
    {
        nextAppCheck = now + 3s;

        bool capturedSilent;
        {
            std::lock_guard lock(snapshotLock);
            capturedSilent = published.left.peakDb < -80.0f && published.right.peakDb < -80.0f;
        }

        if (capturedSilent)
            for (const auto& device : backend->enumerateDevices())
                if (device.kind == DeviceKind::Application && device.processName == currentSelection.processName
                    && device.isPlaying && std::to_wstring(device.processId) != current.deviceId)
                {
                    currentSelection.processId = device.processId;
                    restart = true;
                }
    }

    if (restart)
        restartStream();

    return listChanged;
}

void AudioEngine::setFftSize(int size) noexcept
{
    if (RealFft::isPowerOfTwo(size))
        fftSize = std::clamp(size, 256, 32768);
}

void AudioEngine::setSpectrogramFftSize(int size) noexcept
{
    if (RealFft::isPowerOfTwo(size))
        spectrogramFftSize = std::clamp(size, 256, 32768);
}

bool AudioEngine::readSnapshot(AnalysisSnapshot& destination)
{
    std::lock_guard lock(snapshotLock);
    if (published.sequence == destination.sequence)
        return false;

    destination = published; // vectors keep their capacity, so this rarely allocates
    return true;
}

void AudioEngine::writeFrames(const StereoFrame* frames, std::size_t count) noexcept
{
    const auto written = ring.push(frames, count);
    if (written < count)
        droppedFrames.fetch_add(count - written, std::memory_order_relaxed);

    // notify without holding the lock: never blocks the capture thread. A
    // missed wake-up only delays analysis until the wait's timeout.
    wakeAnalysis.notify_one();
}

void AudioEngine::analysisLoop()
{
    SpectrumAnalyzer spectrum;
    StereoLevelMeter meter;
    WaveformBuilder waveform;
    SpectrogramAnalyzer spectrogram;
    bool spectrogramRunning = false;
    TempoAnalyzer tempo;
    KeyDetector key;
    bool tempoRunning = false, keyRunning = false;
    std::vector<StereoFrame> block(4096);
    std::vector<WaveBucket> buckets(WaveformBuilder::maxBucketsFor(block.size()));

    std::uint64_t seenGeneration = ~std::uint64_t { 0 };
    std::uint32_t sampleRate = 0;
    std::uint64_t sequence = 0;
    AnalysisSnapshot working;

    while (! quit)
    {
        {
            std::unique_lock lock(wakeLock);
            wakeAnalysis.wait_for(lock, 20ms);
        }

        if (quit)
            break;

        const auto generation = streamGeneration.load(std::memory_order_acquire);
        if (generation != seenGeneration)
        {
            seenGeneration = generation;
            sampleRate = streamSampleRate;
            ring.discardAll();
            waveform.prepare(sampleRate);

            if (sampleRate > 0)
            {
                spectrum.prepare(sampleRate, fftSize);
                meter.prepare(sampleRate);
                spectrogram.prepare(sampleRate, spectrogramFftSize);
                tempo.prepare(sampleRate); // a new stream (device change / reconnect) starts from scratch
                key.prepare(sampleRate);
            }
        }

        if (sampleRate == 0)
            continue;

        if (fftSize != spectrum.fftSize())
            spectrum.prepare(sampleRate, fftSize);

        const bool wantSpectrogram = spectrogramEnabled;
        if (wantSpectrogram && spectrogramFftSize != spectrogram.fftSize())
            spectrogram.prepare(sampleRate, spectrogramFftSize);
        else if (wantSpectrogram && ! spectrogramRunning)
            spectrogram.reset(); // don't show audio from before it was switched off
        spectrogramRunning = wantSpectrogram;

        // Switched back on: don't mix in audio from before it was off.
        const bool wantTempo = tempoEnabled, wantKey = keyEnabled;
        if (wantTempo && ! tempoRunning)
            tempo.reset();
        if (wantKey && ! keyRunning)
            key.reset();
        tempoRunning = wantTempo;
        keyRunning = wantKey;
        tempo.setFoldRange(tempoFoldMinimum);

        spectrum.setBallistics({ spectrumSmoothing, spectrumDecay, peakHoldSeconds, 15.0f });

        if (resetPeaksRequested.exchange(false))
        {
            meter.resetMax();
            spectrum.resetPeakHold();
        }

        std::size_t framesRead = 0;
        while (const auto n = ring.pop(block.data(), block.size()))
        {
            spectrum.pushSamples(block.data(), n);
            meter.process(block.data(), n);
            // If the UI isn't draining (e.g. minimised) the ring fills and new peaks are dropped.
            waveformRing.push(buckets.data(), waveform.process(block.data(), n, buckets.data()));
            if (spectrogramRunning)
                spectrogram.process(block.data(), n, [this](const SpectrogramRow& row) { spectrogramRing.push(&row, 1); });
            if (tempoRunning)
                tempo.process(block.data(), n);
            if (keyRunning)
                key.process(block.data(), n);
            framesRead += n;
        }

        if (framesRead == 0)
            continue;

        spectrum.analyze(static_cast<double>(framesRead) / sampleRate);

        working.sequence = ++sequence;
        working.sampleRate = sampleRate;
        working.fftSize = spectrum.fftSize();
        working.spectrumDb = spectrum.displayDb();
        working.spectrumPeakDb = spectrum.peakHoldDb();
        working.left = meter.left();
        working.right = meter.right();
        working.droppedFrames = droppedFrames.load(std::memory_order_relaxed);

        const auto& t = tempo.latest();
        working.tempo = tempoRunning ? TempoReading { t.valid, t.bpm, t.confidence, t.beatPhase } : TempoReading {};
        const auto& k = key.latest();
        working.key = keyRunning ? KeyReading { k.valid, k.key, k.confidence } : KeyReading {};
        working.analysisTimeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();

        std::lock_guard lock(snapshotLock);
        std::swap(published, working);
    }
}

} // namespace sonarc
