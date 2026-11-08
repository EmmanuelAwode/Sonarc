#ifndef NOMINMAX
 #define NOMINMAX
#endif

#include <windows.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <avrt.h>
#include <tlhelp32.h>
#include <wrl/client.h>

#include "audio/WasapiCaptureBackend.h"
#include "audio/SampleConversion.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cwctype>
#include <future>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace sonarc {

namespace {

using Microsoft::WRL::ComPtr;

// PKEY_Device_FriendlyName, defined locally to avoid needing INITGUID tricks.
constexpr PROPERTYKEY kDeviceFriendlyName = {
    { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14
};

// 200 ms of capture buffer: the visualiser doesn't need low latency from WASAPI
// itself (we poll every 5 ms), and a large buffer means we never lose data if
// the capture thread is briefly descheduled.
constexpr REFERENCE_TIME kBufferDuration100ns = 2'000'000;
constexpr DWORD kPollIntervalMs = 5;

// While nothing is playing, loopback capture delivers no packets at all. After
// this much silence we feed equivalent zero frames so meters fall naturally.
constexpr double kLoopbackIdleSeconds = 0.03;

struct ComInit
{
    explicit ComInit(DWORD model) : hr(CoInitializeEx(nullptr, model)) {}
    ~ComInit() { if (SUCCEEDED(hr)) CoUninitialize(); }

    // RPC_E_CHANGED_MODE means COM is already initialised in another model; it's still usable.
    bool usable() const noexcept { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }

    ComInit(const ComInit&) = delete;
    ComInit& operator=(const ComInit&) = delete;

    HRESULT hr;
};

struct CoTaskMemDeleter
{
    void operator()(void* p) const noexcept { CoTaskMemFree(p); }
};

// Owns a Win32 HANDLE.
struct UniqueHandle
{
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE h) : handle(h) {}
    ~UniqueHandle() { reset(); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    void reset(HANDLE h = nullptr)
    {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
        handle = h;
    }

    HANDLE get() const noexcept { return handle; }

    HANDLE handle = nullptr;
};

std::string toUtf8(const wchar_t* text)
{
    if (text == nullptr)
        return {};

    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1)
        return {};

    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);
    result.resize(static_cast<std::size_t>(length - 1));
    return result;
}

std::wstring toLower(std::wstring text)
{
    for (auto& c : text)
        c = static_cast<wchar_t>(std::towlower(c));
    return text;
}

std::string describeHresult(const char* operation, HRESULT hr)
{
    const char* reason = nullptr;
    switch (hr)
    {
        case AUDCLNT_E_DEVICE_INVALIDATED:     reason = "the device was removed, disabled or its format changed"; break;
        case AUDCLNT_E_DEVICE_IN_USE:          reason = "the device is in exclusive use by another application"; break;
        case AUDCLNT_E_UNSUPPORTED_FORMAT:     reason = "the device format is not supported"; break;
        case AUDCLNT_E_SERVICE_NOT_RUNNING:    reason = "the Windows Audio service is not running"; break;
        case AUDCLNT_E_RESOURCES_INVALIDATED:  reason = "the device resources were invalidated"; break;
        case E_ACCESSDENIED:                   reason = "access denied - check Settings > Privacy > Microphone"; break;
        case E_NOTFOUND:                       reason = "the device was not found"; break;
        case E_NOTIMPL:                        reason = "not supported by this version of Windows"; break;
        default: break;
    }

    char buffer[256];
    if (reason != nullptr)
        std::snprintf(buffer, sizeof buffer, "%s failed: %s", operation, reason);
    else
        std::snprintf(buffer, sizeof buffer, "%s failed (HRESULT 0x%08lX)", operation, static_cast<unsigned long>(hr));
    return buffer;
}

std::wstring endpointId(IMMDevice* device)
{
    std::wstring result;
    LPWSTR id = nullptr;
    if (SUCCEEDED(device->GetId(&id)) && id != nullptr)
    {
        result = id;
        CoTaskMemFree(id);
    }
    return result;
}

std::string friendlyName(IMMDevice* device)
{
    std::string name = "Unknown device";
    ComPtr<IPropertyStore> properties;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &properties)))
        return name;

    PROPVARIANT value;
    PropVariantInit(&value);
    if (SUCCEEDED(properties->GetValue(kDeviceFriendlyName, &value)) && value.vt == VT_LPWSTR && value.pwszVal != nullptr)
        name = toUtf8(value.pwszVal);
    PropVariantClear(&value);
    return name;
}

StreamFormat describeFormat(const WAVEFORMATEX& wfx)
{
    StreamFormat format;
    format.sampleRate = wfx.nSamplesPerSec;
    format.channels = wfx.nChannels;
    format.bytesPerFrame = wfx.nBlockAlign;
    format.validBits = wfx.wBitsPerSample;

    WORD tag = wfx.wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE && wfx.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
    {
        const auto& ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(wfx);
        // Sub-format GUIDs for wave formats are {0000XXXX-0000-0010-8000-00AA00389B71} with XXXX = format tag.
        if (ext.SubFormat.Data2 == 0x0000 && ext.SubFormat.Data3 == 0x0010)
            tag = static_cast<WORD>(ext.SubFormat.Data1);
        if (ext.Samples.wValidBitsPerSample != 0)
            format.validBits = ext.Samples.wValidBitsPerSample;
    }

    const WORD containerBits = wfx.wBitsPerSample;
    if (tag == WAVE_FORMAT_IEEE_FLOAT && containerBits == 32)
        format.encoding = SampleEncoding::Float32;
    else if (tag == WAVE_FORMAT_PCM && containerBits == 16)
        format.encoding = SampleEncoding::Int16;
    else if (tag == WAVE_FORMAT_PCM && containerBits == 24)
        format.encoding = SampleEncoding::Int24Packed;
    else if (tag == WAVE_FORMAT_PCM && containerBits == 32)
        format.encoding = SampleEncoding::Int32;

    return format;
}

//==============================================================================
// Processes and application names

struct ProcessEntry
{
    DWORD parent = 0;
    std::wstring exe; // lower case, e.g. "chrome.exe"
};

std::unordered_map<DWORD, ProcessEntry> snapshotProcesses()
{
    std::unordered_map<DWORD, ProcessEntry> processes;
    const UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.get() == INVALID_HANDLE_VALUE)
        return processes;

    PROCESSENTRY32W entry {};
    entry.dwSize = sizeof entry;
    for (BOOL ok = Process32FirstW(snapshot.get(), &entry); ok; ok = Process32NextW(snapshot.get(), &entry))
        processes[entry.th32ProcessID] = { entry.th32ParentProcessID, toLower(entry.szExeFile) };

    return processes;
}

// Browsers and Electron apps play audio from a child process. Walk up to the
// top-most ancestor with the same executable so we capture the whole app.
DWORD appRootProcess(DWORD pid, const std::unordered_map<DWORD, ProcessEntry>& processes)
{
    for (int depth = 0; depth < 64; ++depth)
    {
        const auto self = processes.find(pid);
        if (self == processes.end())
            break;

        const auto parent = processes.find(self->second.parent);
        if (parent == processes.end() || parent->first == pid || parent->second.exe != self->second.exe)
            break;

        pid = parent->first;
    }
    return pid;
}

std::wstring processImagePath(DWORD pid)
{
    const UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (process.get() == nullptr)
        return {};

    wchar_t path[MAX_PATH * 2];
    DWORD length = static_cast<DWORD>(std::size(path));
    return QueryFullProcessImageNameW(process.get(), 0, path, &length) ? std::wstring(path, length) : std::wstring();
}

// "Google Chrome" rather than "chrome.exe", from the executable's version info.
std::string applicationDisplayName(DWORD pid, const std::wstring& exe)
{
    const auto path = processImagePath(pid);
    if (! path.empty())
    {
        DWORD ignored = 0;
        const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
        if (size > 0)
        {
            std::vector<BYTE> data(size);
            struct Translation { WORD language, codePage; }* translations = nullptr;
            UINT translationBytes = 0;

            if (GetFileVersionInfoW(path.c_str(), 0, size, data.data())
                && VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &translationBytes)
                && translationBytes >= sizeof(Translation))
            {
                wchar_t query[64];
                std::swprintf(query, std::size(query), L"\\StringFileInfo\\%04x%04x\\FileDescription",
                              translations[0].language, translations[0].codePage);

                wchar_t* description = nullptr;
                UINT descriptionLength = 0;
                if (VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&description), &descriptionLength)
                    && descriptionLength > 1 && description != nullptr)
                    return toUtf8(description);
            }
        }
    }

    auto name = exe.size() > 4 && exe.ends_with(L".exe") ? exe.substr(0, exe.size() - 4) : exe;
    if (! name.empty())
        name[0] = static_cast<wchar_t>(std::towupper(name[0]));
    return toUtf8(name.c_str());
}

struct AudioApplication
{
    DWORD rootPid = 0;
    std::wstring exe;
    bool playing = false;
};

// Programs that currently have an audio session on any playback device, one entry per executable.
std::vector<AudioApplication> findAudioApplications(IMMDeviceEnumerator* enumerator)
{
    const auto processes = snapshotProcesses();
    const DWORD self = GetCurrentProcessId();
    std::map<std::wstring, AudioApplication> byExe;

    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection)))
        return {};

    UINT deviceCount = 0;
    collection->GetCount(&deviceCount);

    for (UINT d = 0; d < deviceCount; ++d)
    {
        ComPtr<IMMDevice> device;
        ComPtr<IAudioSessionManager2> manager;
        ComPtr<IAudioSessionEnumerator> sessions;
        if (FAILED(collection->Item(d, &device))
            || FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                       reinterpret_cast<void**>(manager.GetAddressOf())))
            || FAILED(manager->GetSessionEnumerator(&sessions)))
            continue;

        int sessionCount = 0;
        sessions->GetCount(&sessionCount);

        for (int s = 0; s < sessionCount; ++s)
        {
            ComPtr<IAudioSessionControl> control;
            ComPtr<IAudioSessionControl2> control2;
            if (FAILED(sessions->GetSession(s, &control)) || FAILED(control.As(&control2)))
                continue;

            if (control2->IsSystemSoundsSession() == S_OK)
                continue;

            DWORD pid = 0;
            AudioSessionState state = AudioSessionStateInactive;
            if (FAILED(control2->GetProcessId(&pid)) || pid == 0 || FAILED(control->GetState(&state))
                || state == AudioSessionStateExpired)
                continue;

            const DWORD root = appRootProcess(pid, processes);
            const auto entry = processes.find(root);
            if (root == self || entry == processes.end())
                continue;

            auto& app = byExe[entry->second.exe];
            if (app.rootPid == 0 || (state == AudioSessionStateActive && ! app.playing))
            {
                app.rootPid = root;
                app.exe = entry->second.exe;
            }
            app.playing = app.playing || state == AudioSessionStateActive;
        }
    }

    std::vector<AudioApplication> result;
    for (auto& [exe, app] : byExe)
        result.push_back(std::move(app));
    return result;
}

// Resolves an application selection to a live root process: the remembered PID
// if it is still that program, otherwise any running instance (preferring one
// that is playing audio).
DWORD resolveApplicationProcess(const DeviceSelection& selection, IMMDeviceEnumerator* enumerator)
{
    const auto processes = snapshotProcesses();

    if (selection.processId != 0)
        if (const auto it = processes.find(selection.processId); it != processes.end() && it->second.exe == selection.processName)
            return appRootProcess(selection.processId, processes);

    for (const auto& app : findAudioApplications(enumerator))
        if (app.exe == selection.processName)
            return app.rootPid;

    for (const auto& [pid, entry] : processes)
        if (entry.exe == selection.processName)
            return appRootProcess(pid, processes);

    return 0;
}

//==============================================================================
// Receives endpoint notifications on a system thread and forwards them to a callback.
class DeviceNotifier final : public IMMNotificationClient
{
public:
    void setCallback(std::function<void()> newCallback)
    {
        std::lock_guard lock(callbackLock);
        callback = std::move(newCallback);
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG remaining = --refCount;
        if (remaining == 0)
            delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
    {
        if (object == nullptr)
            return E_POINTER;

        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient))
        {
            *object = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }

        *object = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { notify(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { notify(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { notify(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole role, LPCWSTR) override
    {
        if (role == eConsole) // fired once per role; one is enough
            notify();
        return S_OK;
    }

private:
    void notify()
    {
        std::lock_guard lock(callbackLock);
        if (callback)
            callback();
    }

    std::atomic<ULONG> refCount { 1 };
    std::mutex callbackLock;
    std::function<void()> callback;
};

// Completion handler for ActivateAudioInterfaceAsync. Must be agile (free-threaded).
class ActivationHandler final : public IActivateAudioInterfaceCompletionHandler, public IAgileObject
{
public:
    ActivationHandler() : done(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

    ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG remaining = --refCount;
        if (remaining == 0)
            delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
    {
        if (object == nullptr)
            return E_POINTER;

        if (riid == __uuidof(IUnknown) || riid == __uuidof(IActivateAudioInterfaceCompletionHandler))
            *object = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
        else if (riid == __uuidof(IAgileObject))
            *object = static_cast<IAgileObject*>(this);
        else
        {
            *object = nullptr;
            return E_NOINTERFACE;
        }

        AddRef();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) override
    {
        HRESULT activateResult = E_FAIL;
        ComPtr<IUnknown> activated;
        const HRESULT hr = operation->GetActivateResult(&activateResult, &activated);
        result = FAILED(hr) ? hr : activateResult;
        if (SUCCEEDED(result))
            activated.As(&client);
        SetEvent(done.get());
        return S_OK;
    }

    UniqueHandle done;
    HRESULT result = E_PENDING;
    ComPtr<IAudioClient> client;

private:
    std::atomic<ULONG> refCount { 1 };
};

// An initialised (not yet started) capture stream.
struct OpenedStream
{
    ComPtr<IAudioClient> client;
    StreamFormat format;
    std::wstring id;
    std::string name;
    bool isLoopback = false;
    UniqueHandle readyEvent;    // set when using event-driven capture
    UniqueHandle targetProcess; // application capture: signalled when the app exits
};

struct OpenFailure
{
    CaptureState state = CaptureState::Failed;
    std::string message;
    std::string deviceName;
};

} // namespace

//==============================================================================
struct WasapiCaptureBackend::Impl
{
    Impl()
    {
        stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        if (com.usable()
            && SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&notifyEnumerator))))
        {
            notifier.Attach(new DeviceNotifier());
            if (FAILED(notifyEnumerator->RegisterEndpointNotificationCallback(notifier.Get())))
                notifier.Reset();
        }
    }

    ~Impl()
    {
        stop();

        if (notifyEnumerator != nullptr && notifier != nullptr)
            notifyEnumerator->UnregisterEndpointNotificationCallback(notifier.Get());

        notifier.Reset();
        notifyEnumerator.Reset();

        if (stopEvent != nullptr)
            CloseHandle(stopEvent);
    }

    std::vector<AudioDeviceInfo> enumerateDevices() const
    {
        std::vector<AudioDeviceInfo> devices;

        ComPtr<IMMDeviceEnumerator> enumerator;
        if (! com.usable()
            || FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))))
            return devices;

        for (const auto& [flow, kind] : { std::pair { eRender, DeviceKind::PlaybackLoopback },
                                          std::pair { eCapture, DeviceKind::RecordingInput } })
        {
            std::wstring defaultId;
            ComPtr<IMMDevice> defaultDevice;
            if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &defaultDevice)))
                defaultId = endpointId(defaultDevice.Get());

            ComPtr<IMMDeviceCollection> collection;
            if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection)))
                continue;

            UINT count = 0;
            collection->GetCount(&count);

            for (UINT i = 0; i < count; ++i)
            {
                ComPtr<IMMDevice> device;
                if (FAILED(collection->Item(i, &device)))
                    continue;

                AudioDeviceInfo info;
                info.id = endpointId(device.Get());
                info.name = friendlyName(device.Get());
                info.kind = kind;
                info.isDefault = ! defaultId.empty() && info.id == defaultId;
                devices.push_back(std::move(info));
            }
        }

        for (const auto& app : findAudioApplications(enumerator.Get()))
        {
            AudioDeviceInfo info;
            info.kind = DeviceKind::Application;
            info.name = applicationDisplayName(app.rootPid, app.exe);
            info.processName = app.exe;
            info.processId = app.rootPid;
            info.isPlaying = app.playing;
            devices.push_back(std::move(info));
        }

        return devices;
    }

    bool start(const DeviceSelection& selection, FrameSink& sink)
    {
        stop();
        ResetEvent(stopEvent);

        std::promise<bool> started;
        auto result = started.get_future();

        worker = std::thread([this, selection, &sink, promise = std::move(started)]() mutable
        {
            captureThread(selection, sink, promise);
        });

        return result.get();
    }

    void stop()
    {
        if (worker.joinable())
        {
            SetEvent(stopEvent);
            worker.join();
        }

        std::lock_guard lock(statusLock);
        if (currentStatus.state == CaptureState::Running)
            currentStatus.state = CaptureState::Stopped;
    }

    CaptureStatus status() const
    {
        std::lock_guard lock(statusLock);
        return currentStatus;
    }

    //==========================================================================
    // A playback endpoint (loopback) or a recording endpoint, in its own mix format.
    static bool openEndpoint(const DeviceSelection& selection, IMMDeviceEnumerator* enumerator,
                             OpenedStream& stream, OpenFailure& failure)
    {
        stream.isLoopback = selection.kind == DeviceKind::PlaybackLoopback;
        const EDataFlow flow = stream.isLoopback ? eRender : eCapture;

        ComPtr<IMMDevice> device;
        HRESULT hr = selection.followsDefault() ? enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device)
                                                : enumerator->GetDevice(selection.deviceId.c_str(), &device);
        if (FAILED(hr))
        {
            failure = { CaptureState::DeviceLost, "The selected audio device is not available" };
            return false;
        }

        stream.name = friendlyName(device.Get());
        stream.id = endpointId(device.Get());

        DWORD deviceState = 0;
        if (SUCCEEDED(device->GetState(&deviceState)) && deviceState != DEVICE_STATE_ACTIVE)
        {
            failure = { CaptureState::DeviceLost, "The selected audio device is disconnected or disabled", stream.name };
            return false;
        }

        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(stream.client.GetAddressOf()));
        if (FAILED(hr))
        {
            failure = { CaptureState::DeviceLost, describeHresult("Activating the device", hr), stream.name };
            return false;
        }

        WAVEFORMATEX* rawMixFormat = nullptr;
        hr = stream.client->GetMixFormat(&rawMixFormat);
        if (FAILED(hr) || rawMixFormat == nullptr)
        {
            failure = { CaptureState::DeviceLost, describeHresult("Reading the device format", hr), stream.name };
            return false;
        }
        const std::unique_ptr<WAVEFORMATEX, CoTaskMemDeleter> mixFormat(rawMixFormat);

        stream.format = describeFormat(*mixFormat);
        if (stream.format.encoding == SampleEncoding::Unknown || stream.format.channels == 0 || stream.format.sampleRate == 0)
        {
            failure = { CaptureState::Failed, "The device uses an unsupported sample format", stream.name };
            return false;
        }

        // Shared mode + the device's own mix format: we never alter what the device is doing.
        const DWORD streamFlags = stream.isLoopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
        hr = stream.client->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, kBufferDuration100ns, 0, mixFormat.get(), nullptr);
        if (FAILED(hr))
        {
            failure = { hr == AUDCLNT_E_DEVICE_INVALIDATED ? CaptureState::DeviceLost : CaptureState::Failed,
                        describeHresult("Opening the audio stream", hr), stream.name };
            return false;
        }

        return true;
    }

    // Process loopback (Windows 10 2004+ / Windows 11): only the audio played by
    // one program and its child processes, mixed across all output devices.
    static bool openApplication(const DeviceSelection& selection, IMMDeviceEnumerator* enumerator,
                                OpenedStream& stream, OpenFailure& failure)
    {
        stream.isLoopback = true;

        const DWORD pid = resolveApplicationProcess(selection, enumerator);
        const auto fallbackName = toUtf8(selection.processName.c_str());
        if (pid == 0)
        {
            failure = { CaptureState::DeviceLost, "Waiting for the app to start", fallbackName };
            return false;
        }

        stream.name = applicationDisplayName(pid, selection.processName);
        stream.id = std::to_wstring(pid);
        stream.targetProcess.reset(OpenProcess(SYNCHRONIZE, FALSE, pid));

        AUDIOCLIENT_ACTIVATION_PARAMS params {};
        params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        params.ProcessLoopbackParams.TargetProcessId = pid;
        params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

        PROPVARIANT activationParams {};
        activationParams.vt = VT_BLOB;
        activationParams.blob.cbSize = sizeof(params);
        activationParams.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

        ComPtr<ActivationHandler> handler;
        handler.Attach(new ActivationHandler());
        ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
        HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient),
                                                 &activationParams, handler.Get(), &operation);
        if (SUCCEEDED(hr) && WaitForSingleObject(handler->done.get(), 5000) != WAIT_OBJECT_0)
            hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        if (SUCCEEDED(hr))
            hr = handler->result;
        if (FAILED(hr) || handler->client == nullptr)
        {
            failure = { CaptureState::Failed, describeHresult("Per-app capture", hr), stream.name };
            return false;
        }
        stream.client = handler->client;

        // The process-loopback client has no mix format of its own; Windows
        // converts to whatever we ask for. Match the default output's rate.
        std::uint32_t sampleRate = 48000;
        ComPtr<IMMDevice> output;
        ComPtr<IAudioClient> outputClient;
        WAVEFORMATEX* outputFormat = nullptr;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &output))
            && SUCCEEDED(output->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(outputClient.GetAddressOf())))
            && SUCCEEDED(outputClient->GetMixFormat(&outputFormat)) && outputFormat != nullptr)
        {
            sampleRate = outputFormat->nSamplesPerSec;
            CoTaskMemFree(outputFormat);
        }

        WAVEFORMATEX format {};
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = 2;
        format.nSamplesPerSec = sampleRate;
        format.wBitsPerSample = 32;
        format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        stream.format = describeFormat(format);

        constexpr DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK
                              | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        hr = stream.client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, kBufferDuration100ns, 0, &format, nullptr);
        if (FAILED(hr))
        {
            failure = { CaptureState::Failed, describeHresult("Opening the app's audio stream", hr), stream.name };
            return false;
        }

        stream.readyEvent.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        hr = stream.client->SetEventHandle(stream.readyEvent.get());
        if (FAILED(hr))
        {
            failure = { CaptureState::Failed, describeHresult("Setting up the app's audio stream", hr), stream.name };
            return false;
        }

        return true;
    }

    void captureThread(const DeviceSelection& selection, FrameSink& sink, std::promise<bool>& started)
    {
        bool startReported = false;
        auto reportStart = [&](bool ok)
        {
            if (! startReported)
            {
                startReported = true;
                started.set_value(ok);
            }
        };

        auto fail = [&](const OpenFailure& failure)
        {
            {
                std::lock_guard lock(statusLock);
                currentStatus = {};
                currentStatus.state = failure.state;
                currentStatus.deviceName = failure.deviceName;
                currentStatus.message = failure.message;
            }
            reportStart(false);
        };

        ComInit threadCom(COINIT_MULTITHREADED);
        if (! threadCom.usable())
            return fail({ CaptureState::Failed, "COM initialisation failed" });

        ComPtr<IMMDeviceEnumerator> enumerator;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
        if (FAILED(hr))
            return fail({ CaptureState::Failed, describeHresult("Creating the device enumerator", hr) });

        OpenedStream stream;
        OpenFailure failure;
        const bool opened = selection.kind == DeviceKind::Application ? openApplication(selection, enumerator.Get(), stream, failure)
                                                                      : openEndpoint(selection, enumerator.Get(), stream, failure);
        if (! opened)
            return fail(failure);

        const StreamFormat& format = stream.format;

        UINT32 bufferFrames = 0;
        if (FAILED(stream.client->GetBufferSize(&bufferFrames)))
            bufferFrames = 0;

        ComPtr<IAudioCaptureClient> capture;
        hr = stream.client->GetService(IID_PPV_ARGS(&capture));
        if (FAILED(hr))
            return fail({ CaptureState::Failed, describeHresult("Getting the capture service", hr), stream.name });

        // Allocate everything up front: the loop below must not allocate.
        std::vector<StereoFrame> converted(std::max<std::size_t>(bufferFrames, 4096));
        const std::vector<StereoFrame> silence(converted.size());

        DWORD taskIndex = 0;
        HANDLE mmcssHandle = AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex);

        hr = stream.client->Start();
        if (FAILED(hr))
        {
            if (mmcssHandle != nullptr)
                AvRevertMmThreadCharacteristics(mmcssHandle);
            return fail({ CaptureState::DeviceLost, describeHresult("Starting the audio stream", hr), stream.name });
        }

        {
            std::lock_guard lock(statusLock);
            currentStatus = {};
            currentStatus.state = CaptureState::Running;
            currentStatus.format = format;
            currentStatus.deviceId = stream.id;
            currentStatus.deviceName = stream.name;
        }
        reportStart(true);

        auto writeSilence = [&](std::size_t frames)
        {
            while (frames > 0)
            {
                const auto chunk = std::min(frames, silence.size());
                sink.writeFrames(silence.data(), chunk);
                frames -= chunk;
            }
        };

        LARGE_INTEGER qpcFrequency;
        QueryPerformanceFrequency(&qpcFrequency);
        auto nowTicks = []
        {
            LARGE_INTEGER t;
            QueryPerformanceCounter(&t);
            return t.QuadPart;
        };

        // Wake on stop, on the app exiting (per-app capture), or when data is ready / every 5 ms.
        HANDLE waitHandles[3] = { stopEvent };
        DWORD waitCount = 1;
        DWORD processExitIndex = MAXDWORD;
        if (stream.targetProcess.get() != nullptr)
        {
            processExitIndex = waitCount;
            waitHandles[waitCount++] = stream.targetProcess.get();
        }
        if (stream.readyEvent.get() != nullptr)
            waitHandles[waitCount++] = stream.readyEvent.get();

        auto lastDelivery = nowTicks();
        std::string lostMessage;

        for (;;)
        {
            const DWORD woke = WaitForMultipleObjects(waitCount, waitHandles, FALSE, kPollIntervalMs);
            if (woke == WAIT_OBJECT_0)
                break;
            if (processExitIndex != MAXDWORD && woke == WAIT_OBJECT_0 + processExitIndex)
            {
                lostMessage = "The app was closed";
                break;
            }
            if (woke == WAIT_FAILED)
            {
                lostMessage = "Audio capture wait failed";
                break;
            }

            bool delivered = false;
            UINT32 packetFrames = 0;
            hr = capture->GetNextPacketSize(&packetFrames);

            while (SUCCEEDED(hr) && packetFrames > 0)
            {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD bufferFlags = 0;
                hr = capture->GetBuffer(&data, &frames, &bufferFlags, nullptr, nullptr);
                if (FAILED(hr))
                    break;

                if (frames > 0)
                {
                    if ((bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT) != 0)
                    {
                        writeSilence(frames);
                    }
                    else
                    {
                        for (std::size_t done = 0; done < frames;)
                        {
                            const auto chunk = std::min<std::size_t>(frames - done, converted.size());
                            convertToStereo(data + done * format.bytesPerFrame, chunk, format, converted.data());
                            sink.writeFrames(converted.data(), chunk);
                            done += chunk;
                        }
                    }
                    delivered = true;
                }

                hr = capture->ReleaseBuffer(frames);
                if (FAILED(hr))
                    break;

                hr = capture->GetNextPacketSize(&packetFrames);
            }

            if (FAILED(hr))
            {
                lostMessage = describeHresult("Audio capture", hr);
                break;
            }

            const auto now = nowTicks();
            if (delivered)
            {
                lastDelivery = now;
            }
            else if (stream.isLoopback)
            {
                const double idleSeconds = static_cast<double>(now - lastDelivery) / static_cast<double>(qpcFrequency.QuadPart);
                if (idleSeconds >= kLoopbackIdleSeconds)
                {
                    writeSilence(static_cast<std::size_t>(idleSeconds * format.sampleRate));
                    lastDelivery = now;
                }
            }
        }

        stream.client->Stop();
        if (mmcssHandle != nullptr)
            AvRevertMmThreadCharacteristics(mmcssHandle);

        std::lock_guard lock(statusLock);
        if (lostMessage.empty())
        {
            currentStatus.state = CaptureState::Stopped;
        }
        else
        {
            currentStatus.state = CaptureState::DeviceLost;
            currentStatus.message = lostMessage;
        }
    }

    ComInit com { COINIT_APARTMENTTHREADED }; // the UI thread; JUCE needs it to stay single-threaded
    ComPtr<IMMDeviceEnumerator> notifyEnumerator;
    ComPtr<DeviceNotifier> notifier;
    HANDLE stopEvent = nullptr;
    std::thread worker;

    mutable std::mutex statusLock;
    CaptureStatus currentStatus;
};

//==============================================================================
WasapiCaptureBackend::WasapiCaptureBackend() : impl(std::make_unique<Impl>()) {}
WasapiCaptureBackend::~WasapiCaptureBackend() = default;

std::vector<AudioDeviceInfo> WasapiCaptureBackend::enumerateDevices() { return impl->enumerateDevices(); }
bool WasapiCaptureBackend::start(const DeviceSelection& selection, FrameSink& sink) { return impl->start(selection, sink); }
void WasapiCaptureBackend::stop() { impl->stop(); }
CaptureStatus WasapiCaptureBackend::status() const { return impl->status(); }

void WasapiCaptureBackend::setDeviceChangeCallback(std::function<void()> callback)
{
    if (impl->notifier != nullptr)
        impl->notifier->setCallback(std::move(callback));
}

} // namespace sonarc
