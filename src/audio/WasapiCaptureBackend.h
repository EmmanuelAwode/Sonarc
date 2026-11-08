#pragma once

#include "audio/CaptureBackend.h"

#include <memory>

namespace sonarc {

// Shared-mode WASAPI capture.
//  - Playback devices are captured with AUDCLNT_STREAMFLAGS_LOOPBACK, i.e. we
//    receive a copy of the mix Windows is sending to that output.
//  - Recording devices are captured as ordinary inputs.
//  - Applications are captured with process loopback (Windows 10 2004+):
//    only the audio of one program and its child processes.
// Shared mode never takes the device away from other applications and never
// changes its format.
//
// Must be constructed and destroyed on the same thread (it initialises COM there).
class WasapiCaptureBackend final : public CaptureBackend
{
public:
    WasapiCaptureBackend();
    ~WasapiCaptureBackend() override;

    const char* backendName() const noexcept override { return "WASAPI"; }
    std::vector<AudioDeviceInfo> enumerateDevices() override;
    bool start(const DeviceSelection& selection, FrameSink& sink) override;
    void stop() override;
    CaptureStatus status() const override;
    void setDeviceChangeCallback(std::function<void()> callback) override;

private:
    struct Impl; // keeps <windows.h> out of this header
    std::unique_ptr<Impl> impl;
};

} // namespace sonarc
