#pragma once

#include "audio/AudioTypes.h"

#include <juce_core/juce_core.h>

namespace sonarc {

// Persistent application settings, stored as JSON in
// %APPDATA%\Sonarc\settings.json. Changes are kept in memory and written by
// saveIfNeeded() / save(), so dragging a slider doesn't hammer the disk.
class AppSettings
{
public:
    explicit AppSettings(juce::File settingsFile = defaultFile());

    static juce::File defaultFile();

    DeviceSelection deviceSelection() const;
    void setDeviceSelection(const DeviceSelection& selection);

    int fftSize() const;
    void setFftSize(int size);

    float spectrumSmoothing() const;
    void setSpectrumSmoothing(float value);

    float spectrumDecay() const;
    void setSpectrumDecay(float dbPerSecond);

    float spectrumTilt() const;
    void setSpectrumTilt(float dbPerOctave);

    bool peakHoldEnabled() const;
    void setPeakHoldEnabled(bool enabled);

    bool alwaysOnTop() const;
    void setAlwaysOnTop(bool onTop);

    bool overlayMode() const;
    void setOverlayMode(bool overlay);

    float windowOpacity() const;
    void setWindowOpacity(float opacity);

    juce::String viewMode() const; // "spectrum", "waveform" or "both"
    void setViewMode(const juce::String& mode);

    float waveformWindowSeconds() const;
    void setWaveformWindowSeconds(float seconds);

    juce::String waveformStereoMode() const; // "stacked", "overlaid" or "mid"
    void setWaveformStereoMode(const juce::String& mode);

    juce::String waveformStyle() const; // "spectral" or "classic"
    void setWaveformStyle(const juce::String& style);

    float waveformThickness() const;
    void setWaveformThickness(float pixels);

    int spectrogramFftSize() const;
    void setSpectrogramFftSize(int size);

    float spectrogramSpeed() const; // pixels per second
    void setSpectrogramSpeed(float pixelsPerSecond);

    juce::String spectrogramPalette() const; // "sonarc", "ember", "ice" or "grayscale"
    void setSpectrogramPalette(const juce::String& palette);

    float spectrogramRangeDb() const;
    void setSpectrogramRangeDb(float decibels);

    juce::String stickMode() const; // "off", "top" or "bottom"
    void setStickMode(const juce::String& mode);

    int stickHeight() const; // logical pixels
    void setStickHeight(int pixels);

    juce::var theme() const;          // JSON object of colours, or void if never customised
    void setTheme(const juce::var& colours);

    juce::String themePreset() const; // preset name, or "Custom"
    void setThemePreset(const juce::String& name);

    juce::String windowState() const;
    void setWindowState(const juce::String& state);

    void saveIfNeeded();
    void save();

private:
    juce::var get(const juce::Identifier& key, const juce::var& fallback) const;
    void set(const juce::Identifier& key, const juce::var& value);

    juce::File file;
    juce::DynamicObject::Ptr root;
    bool dirty = false;
};

} // namespace sonarc
