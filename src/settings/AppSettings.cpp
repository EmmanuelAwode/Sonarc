#include "settings/AppSettings.h"

namespace sonarc {

namespace keys {
    static const juce::Identifier deviceKind { "deviceKind" };
    static const juce::Identifier deviceId { "deviceId" };
    static const juce::Identifier processName { "appProcessName" };
    static const juce::Identifier fftSize { "fftSize" };
    static const juce::Identifier smoothing { "spectrumSmoothing" };
    static const juce::Identifier decay { "spectrumDecayDbPerSecond" };
    static const juce::Identifier tilt { "spectrumTiltDbPerOctave" };
    static const juce::Identifier peakHold { "peakHold" };
    static const juce::Identifier windowState { "windowState" };
    static const juce::Identifier alwaysOnTop { "alwaysOnTop" };
    static const juce::Identifier overlayMode { "overlayMode" };
    static const juce::Identifier windowOpacity { "windowOpacity" };
    static const juce::Identifier viewMode { "viewMode" };
    static const juce::Identifier waveformWindow { "waveformWindowSeconds" };
    static const juce::Identifier waveformStereo { "waveformStereoMode" };
    static const juce::Identifier waveformThickness { "waveformThickness" };
}

AppSettings::AppSettings(juce::File settingsFile) : file(std::move(settingsFile))
{
    const auto parsed = juce::JSON::parse(file.loadFileAsString());
    if (auto* object = parsed.getDynamicObject())
        root = object;
    else
        root = new juce::DynamicObject();
}

juce::File AppSettings::defaultFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Sonarc")
        .getChildFile("settings.json");
}

juce::var AppSettings::get(const juce::Identifier& key, const juce::var& fallback) const
{
    return root->hasProperty(key) ? root->getProperty(key) : fallback;
}

void AppSettings::set(const juce::Identifier& key, const juce::var& value)
{
    if (root->getProperty(key) != value)
    {
        root->setProperty(key, value);
        dirty = true;
    }
}

DeviceSelection AppSettings::deviceSelection() const
{
    DeviceSelection selection;
    const auto kind = get(keys::deviceKind, "playback").toString();
    selection.kind = kind == "input" ? DeviceKind::RecordingInput
                   : kind == "app"   ? DeviceKind::Application
                                     : DeviceKind::PlaybackLoopback;
    selection.deviceId = get(keys::deviceId, "").toString().toWideCharPointer();
    selection.processName = get(keys::processName, "").toString().toLowerCase().toWideCharPointer();
    return selection;
}

void AppSettings::setDeviceSelection(const DeviceSelection& selection)
{
    set(keys::deviceKind, selection.kind == DeviceKind::RecordingInput ? "input"
                        : selection.kind == DeviceKind::Application    ? "app"
                                                                       : "playback");
    set(keys::deviceId, juce::String(selection.deviceId.c_str()));
    set(keys::processName, juce::String(selection.processName.c_str())); // the PID is not kept: it changes every launch
}

int AppSettings::fftSize() const { return get(keys::fftSize, 4096); }
void AppSettings::setFftSize(int size) { set(keys::fftSize, size); }

float AppSettings::spectrumSmoothing() const { return (float) (double) get(keys::smoothing, 0.6); }
void AppSettings::setSpectrumSmoothing(float value) { set(keys::smoothing, value); }

float AppSettings::spectrumDecay() const { return (float) (double) get(keys::decay, 60.0); }
void AppSettings::setSpectrumDecay(float dbPerSecond) { set(keys::decay, dbPerSecond); }

float AppSettings::spectrumTilt() const { return (float) (double) get(keys::tilt, 4.5); }
void AppSettings::setSpectrumTilt(float dbPerOctave) { set(keys::tilt, dbPerOctave); }

bool AppSettings::peakHoldEnabled() const { return get(keys::peakHold, true); }
void AppSettings::setPeakHoldEnabled(bool enabled) { set(keys::peakHold, enabled); }

bool AppSettings::alwaysOnTop() const { return get(keys::alwaysOnTop, false); }
void AppSettings::setAlwaysOnTop(bool onTop) { set(keys::alwaysOnTop, onTop); }

bool AppSettings::overlayMode() const { return get(keys::overlayMode, false); }
void AppSettings::setOverlayMode(bool overlay) { set(keys::overlayMode, overlay); }

float AppSettings::windowOpacity() const { return (float) (double) get(keys::windowOpacity, 1.0); }
void AppSettings::setWindowOpacity(float opacity) { set(keys::windowOpacity, opacity); }

juce::String AppSettings::viewMode() const { return get(keys::viewMode, "spectrum").toString(); }
void AppSettings::setViewMode(const juce::String& mode) { set(keys::viewMode, mode); }

float AppSettings::waveformWindowSeconds() const { return (float) (double) get(keys::waveformWindow, 2.0); }
void AppSettings::setWaveformWindowSeconds(float seconds) { set(keys::waveformWindow, seconds); }

juce::String AppSettings::waveformStereoMode() const { return get(keys::waveformStereo, "stacked").toString(); }
void AppSettings::setWaveformStereoMode(const juce::String& mode) { set(keys::waveformStereo, mode); }

float AppSettings::waveformThickness() const { return (float) (double) get(keys::waveformThickness, 1.5); }
void AppSettings::setWaveformThickness(float pixels) { set(keys::waveformThickness, pixels); }

juce::String AppSettings::windowState() const { return get(keys::windowState, "").toString(); }
void AppSettings::setWindowState(const juce::String& state) { set(keys::windowState, state); }

void AppSettings::saveIfNeeded()
{
    if (dirty)
        save();
}

void AppSettings::save()
{
    file.getParentDirectory().createDirectory();
    if (file.replaceWithText(juce::JSON::toString(juce::var(root.get()))))
        dirty = false;
}

} // namespace sonarc
