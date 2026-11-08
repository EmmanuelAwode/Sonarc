#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace sonarc {

struct StereoFrame
{
    float left = 0.0f;
    float right = 0.0f;
};

enum class DeviceKind
{
    PlaybackLoopback, // capture what a playback device is outputting
    RecordingInput,   // capture a microphone / line input
    Application       // capture only the audio one program (and its child processes) plays
};

struct DeviceSelection
{
    DeviceKind kind = DeviceKind::PlaybackLoopback;
    std::wstring deviceId;    // endpoints: empty = follow the Windows default device of this kind
    std::wstring processName; // applications: executable name, e.g. "chrome.exe" (lower case)
    std::uint32_t processId = 0; // applications: last known process; 0 = find by name

    bool followsDefault() const noexcept { return kind != DeviceKind::Application && deviceId.empty(); }

    // Applications are identified by executable, so a restarted app is still "the same" source.
    bool operator==(const DeviceSelection& other) const noexcept
    {
        if (kind != other.kind)
            return false;
        return kind == DeviceKind::Application ? processName == other.processName : deviceId == other.deviceId;
    }
};

struct AudioDeviceInfo
{
    std::wstring id;
    std::string name; // UTF-8
    DeviceKind kind = DeviceKind::PlaybackLoopback;
    bool isDefault = false;
    std::wstring processName;    // applications only
    std::uint32_t processId = 0; // applications only: root process of the app
    bool isPlaying = false;      // applications only: has an active audio session right now

    bool operator==(const AudioDeviceInfo&) const = default;
};

enum class SampleEncoding
{
    Unknown,
    Float32,
    Int16,
    Int24Packed, // 3 bytes per sample
    Int32        // also used for 24-bit samples in a 32-bit container (left-justified)
};

struct StreamFormat
{
    std::uint32_t sampleRate = 0;
    std::uint16_t channels = 0;
    std::uint16_t validBits = 0;
    std::uint16_t bytesPerFrame = 0;
    SampleEncoding encoding = SampleEncoding::Unknown;
};

enum class CaptureState
{
    Stopped,
    Running,
    DeviceLost, // device unplugged, disabled or its format changed; can be retried
    Failed      // could not open the stream for a reason a retry won't fix
};

struct CaptureStatus
{
    CaptureState state = CaptureState::Stopped;
    StreamFormat format;
    std::wstring deviceId; // the endpoint actually opened (resolved when following the default)
    std::string deviceName;
    std::string message;   // human-readable reason when DeviceLost / Failed
};

// Receives converted audio from a capture backend.
class FrameSink
{
public:
    virtual ~FrameSink() = default;

    // Called on the backend's capture thread. Implementations must not block or allocate.
    virtual void writeFrames(const StereoFrame* frames, std::size_t count) noexcept = 0;
};

} // namespace sonarc
