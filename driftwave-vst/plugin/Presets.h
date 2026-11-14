#pragma once

#include "plugin/Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace driftwave {

struct PresetInfo {
    juce::String name;
    bool factory = false;
    juce::File file; // user presets only
};

/** Factory presets (built in) and user presets (one small XML file each, in
    Documents/Driftwave FX/Presets, so they can be copied, shared and backed up).

    A preset holds every parameter. Loading goes through the host, so it can be
    undone there and shows up in automation recording like any knob movement.
    Message thread only. */
class PresetManager {
public:
    explicit PresetManager(juce::AudioProcessorValueTreeState& state, juce::File folder = defaultFolder());

    static juce::File defaultFolder();
    static constexpr const char* kExtension = ".dwpreset";

    /** Factory presets in their fixed order, then user presets by name. */
    juce::Array<PresetInfo> list() const;

    bool load(const PresetInfo& preset);
    /** Saves the current settings as a user preset, replacing one of the same name.
        Returns the saved preset, or one with an empty name if it could not be written. */
    PresetInfo saveUser(const juce::String& name);
    /** Moves a user preset's file to the recycle bin. */
    bool removeUser(const PresetInfo& preset);

    /** The preset last loaded or saved; kept in the plugin state, so it comes back with the project. */
    juce::String currentName() const;

    /** A name that is safe to use as a file name (may come back empty). */
    static juce::String cleanName(const juce::String& name);

private:
    void apply(const juce::StringPairArray& values, const juce::String& name);

    juce::AudioProcessorValueTreeState& state;
    juce::File folder;
};

} // namespace driftwave
