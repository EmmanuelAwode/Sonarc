#include "plugin/Theme.h"

namespace driftwave {

namespace {
constexpr const char* kTag = "DriftwaveColours";
constexpr int kMaxSaved = 60;
}

const std::vector<ColourTheme>& builtInColours()
{
    static const std::vector<ColourTheme> colours = {
        { "Amber", juce::Colour(0xffd4a65a) },
        { "Rose", juce::Colour(0xffe08a9b) },
        { "Crimson", juce::Colour(0xffe0565b) },
        { "Violet", juce::Colour(0xffa58cf0) },
        { "Ocean", juce::Colour(0xff5aa6e0) },
        { "Cyan", juce::Colour(0xff5ccbd6) },
        { "Mint", juce::Colour(0xff72cf9a) },
        { "Lime", juce::Colour(0xffb5d45a) },
        { "Silver", juce::Colour(0xffc3cad4) },
    };
    return colours;
}

juce::Colour usableAccent(juce::Colour colour)
{
    colour = colour.withAlpha(1.0f);
    return colour.getBrightness() < 0.5f ? colour.withBrightness(0.5f) : colour;
}

juce::Colour companionFor(juce::Colour accent)
{
    // Across the colour wheel, pale and a little dim, so it sits behind the accent.
    float hue = accent.getHue() + 0.45f;
    hue -= std::floor(hue);
    return juce::Colour::fromHSV(hue, accent.getSaturation() < 0.12f ? 0.0f : 0.27f, 0.77f, 1.0f);
}

juce::Colour colourFromText(const juce::String& text, juce::Colour fallback)
{
    const auto trimmed = text.trim();
    if (trimmed.length() < 6 || trimmed.length() > 8 || !trimmed.containsOnly("0123456789abcdefABCDEF"))
        return fallback;
    return usableAccent(juce::Colour::fromString(trimmed));
}

juce::File ColourStore::defaultFile()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Driftwave FX").getChildFile("Colours.xml");
}

juce::String ColourStore::cleanName(const juce::String& name)
{
    return name.removeCharacters("<>\"&\r\n\t").trim().substring(0, 40).trim();
}

std::vector<ColourTheme> ColourStore::saved() const
{
    std::vector<ColourTheme> result;
    const auto xml = juce::parseXML(file);
    if (xml == nullptr || !xml->hasTagName(kTag))
        return result;
    for (const auto* child : xml->getChildWithTagNameIterator("Colour")) {
        const auto name = cleanName(child->getStringAttribute("name"));
        const auto value = child->getStringAttribute("value");
        if (name.isEmpty() || colourFromText(value, juce::Colours::transparentBlack).isTransparent())
            continue;
        result.push_back({ name, colourFromText(value, {}) });
        if (int(result.size()) >= kMaxSaved)
            break;
    }
    return result;
}

juce::Colour ColourStore::lastUsed(juce::Colour fallback) const
{
    const auto xml = juce::parseXML(file);
    if (xml == nullptr || !xml->hasTagName(kTag))
        return fallback;
    return colourFromText(xml->getStringAttribute("last"), fallback);
}

bool ColourStore::write(const std::vector<ColourTheme>& colours, juce::Colour last) const
{
    if (!file.getParentDirectory().createDirectory())
        return false;
    juce::XmlElement xml(kTag);
    xml.setAttribute("version", 1);
    xml.setAttribute("last", usableAccent(last).toString());
    for (const auto& colour : colours) {
        auto* child = xml.createNewChildElement("Colour");
        child->setAttribute("name", colour.name);
        child->setAttribute("value", usableAccent(colour.accent).toString());
    }
    return xml.writeTo(file);
}

void ColourStore::setLastUsed(juce::Colour colour)
{
    write(saved(), colour);
}

bool ColourStore::save(const juce::String& rawName, juce::Colour colour)
{
    const auto name = cleanName(rawName);
    if (name.isEmpty())
        return false;
    auto colours = saved();
    bool replaced = false;
    for (auto& existing : colours) {
        if (existing.name.equalsIgnoreCase(name)) {
            existing.accent = colour;
            replaced = true;
        }
    }
    if (!replaced) {
        if (int(colours.size()) >= kMaxSaved)
            return false;
        colours.push_back({ name, colour });
    }
    return write(colours, colour);
}

bool ColourStore::remove(const juce::String& name)
{
    auto colours = saved();
    const auto before = colours.size();
    colours.erase(std::remove_if(colours.begin(), colours.end(), [&](const ColourTheme& c) { return c.name == name; }), colours.end());
    return colours.size() != before && write(colours, lastUsed(builtInColours().front().accent));
}

} // namespace driftwave
