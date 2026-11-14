#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace driftwave {

/** A named accent colour for the interface. */
struct ColourTheme {
    juce::String name;
    juce::Colour accent;
};

/** The colours that come with the plugin; the first is the default. */
const std::vector<ColourTheme>& builtInColours();

/** Makes any colour usable as an accent: opaque, and bright enough to read on the dark background. */
juce::Colour usableAccent(juce::Colour colour);

/** The quiet second colour that goes with an accent (used for the reverb's displays). */
juce::Colour companionFor(juce::Colour accent);

/** Reads a colour written by Colour::toString(); anything unreadable gives the fallback. */
juce::Colour colourFromText(const juce::String& text, juce::Colour fallback);

/** The user's own saved colours and the colour used last, kept in one small file so
    they are there in every project. The file is treated as untrusted input. */
class ColourStore {
public:
    explicit ColourStore(juce::File file) : file(std::move(file)) {}
    static juce::File defaultFile();

    std::vector<ColourTheme> saved() const;
    bool save(const juce::String& name, juce::Colour colour);
    bool remove(const juce::String& name);

    /** The colour new instances of the plugin start with. */
    juce::Colour lastUsed(juce::Colour fallback) const;
    void setLastUsed(juce::Colour colour);

    static juce::String cleanName(const juce::String& name);

private:
    bool write(const std::vector<ColourTheme>& colours, juce::Colour last) const;
    juce::File file;
};

} // namespace driftwave
