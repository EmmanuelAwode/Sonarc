#pragma once

#include "audio/AudioTypes.h"

#include <functional>
#include <vector>

namespace sonarc {

// A source of live audio. WASAPI is the only implementation today; the engine
// only talks to this interface so ASIO or a DAW-plugin bridge can be added
// later without touching the DSP or UI.
//
// Backends only ever *read* audio. They must never change the device's format,
// volume or routing.
class CaptureBackend
{
public:
    virtual ~CaptureBackend() = default;

    virtual const char* backendName() const noexcept = 0;

    // Active devices that this backend can capture from.
    virtual std::vector<AudioDeviceInfo> enumerateDevices() = 0;

    // Opens the device and starts delivering frames to `sink` on a backend-owned
    // thread. Blocks until the stream is running or has failed (see status()).
    virtual bool start(const DeviceSelection& selection, FrameSink& sink) = 0;

    // Stops capture and joins the capture thread. Safe to call when not running.
    virtual void stop() = 0;

    virtual CaptureStatus status() const = 0;

    // `callback` runs on an arbitrary system thread whenever devices are added,
    // removed, enabled/disabled or the default device changes. Keep it trivial.
    virtual void setDeviceChangeCallback(std::function<void()> callback) = 0;
};

} // namespace sonarc
