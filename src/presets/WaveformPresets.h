#pragma once

#include "presets/WaveformStyle.h"

#include <juce_core/juce_core.h>

#include <set>
#include <vector>

namespace sonarc {

struct WaveformPreset
{
    juce::String name;
    WaveformStyle style;
    bool builtIn = false;
    bool favourite = false;
};

namespace waveformPresets {

// Version of the JSON format. Files with a newer version are refused rather
// than half-read; older or missing optional fields fall back to defaults.
inline constexpr int kFormatVersion = 1;
inline constexpr int kMaxNameLength = 64;

// Built into the app; can't be edited, renamed or deleted. The first one is the default.
const std::vector<WaveformPreset>& builtIns();
const WaveformPreset& defaultPreset();

juce::var styleToVar(const WaveformStyle&);

// Strict: every field that is present must have the right type and be within
// range, and the shape must be known. On failure returns false and explains why.
bool styleFromVar(const juce::var&, WaveformStyle& out, juce::String& error);

// A shareable file: {"format": "sonarc-waveform-preset", "version": 1, "presets": [...]}
juce::String exportJson(const std::vector<WaveformPreset>&);

struct ImportResult
{
    std::vector<WaveformPreset> presets; // the ones that passed validation
    juce::StringArray errors;            // one line per problem (whole file or individual presets)
};
ImportResult importJson(const juce::String& text);

} // namespace waveformPresets

// The user's own presets and favourites, stored in their own file so they never
// mix with settings.json (or the colour theme).
class WaveformPresetLibrary
{
public:
    explicit WaveformPresetLibrary(juce::File storage = defaultFile());

    static juce::File defaultFile(); // %APPDATA%\Sonarc\waveform-presets.json

    // Built-ins first, then the user's presets in the order they were made.
    std::vector<WaveformPreset> all() const;
    const WaveformPreset* find(const juce::String& name) const;
    bool isBuiltIn(const juce::String& name) const;
    juce::String uniqueName(const juce::String& wanted) const;

    // All of these refuse to touch built-ins and return false (with `error` set) if they can't comply.
    bool save(const juce::String& name, const WaveformStyle& style, juce::String& error); // create or overwrite
    bool rename(const juce::String& from, const juce::String& to, juce::String& error);
    bool remove(const juce::String& name, juce::String& error);
    juce::String duplicate(const juce::String& name);  // returns the new preset's name ("" if not found)
    void setFavourite(const juce::String& name, bool favourite);
    juce::StringArray add(const std::vector<WaveformPreset>& imported); // returns the names they were stored under

    const juce::String& loadError() const noexcept { return lastLoadError; }

private:
    void load();
    void write() const;
    static bool validName(const juce::String& name, juce::String& error);

    juce::File file;
    std::vector<WaveformPreset> user;
    std::set<juce::String> favouriteBuiltIns;
    juce::String lastLoadError;
};

} // namespace sonarc
