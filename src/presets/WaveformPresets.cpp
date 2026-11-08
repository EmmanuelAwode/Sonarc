#include "presets/WaveformPresets.h"

#include <cmath>

namespace sonarc {

namespace {

using Shape = WaveformStyle::Shape;
using Colour = WaveformStyle::ColourSource;
using Direction = WaveformStyle::Direction;

const char* const kExportFormat = "sonarc-waveform-preset";
const char* const kLibraryFormat = "sonarc-waveform-presets";

struct NamedShape { const char* key; Shape value; };
constexpr NamedShape kShapes[] = { { "classic", Shape::Classic }, { "mirrored", Shape::Mirrored },
                                   { "oscilloscope", Shape::Oscilloscope }, { "bars", Shape::Bars },
                                   { "filled", Shape::Filled }, { "circular", Shape::Circular } };

struct NamedColour { const char* key; Colour value; };
constexpr NamedColour kColours[] = { { "spectral", Colour::SpectralBands }, { "channels", Colour::Channels },
                                     { "gradient", Colour::SpectrumGradient }, { "accent", Colour::Accent } };

WaveformStyle make(Shape shape, Colour colour, float thickness, float glow, float amplitude, float smoothing,
                   float window, float animation, float stereo, bool peakHold, float fill, int bars = 64)
{
    WaveformStyle s;
    s.shape = shape;
    s.colour = colour;
    s.lineThickness = thickness;
    s.glow = glow;
    s.amplitude = amplitude;
    s.smoothing = smoothing;
    s.windowSeconds = window;
    s.animationSpeed = animation;
    s.stereoSeparation = stereo;
    s.peakHold = peakHold;
    s.fillOpacity = fill;
    s.barCount = bars;
    return s;
}

} // namespace

namespace waveformPresets {

const std::vector<WaveformPreset>& builtIns()
{
    //                                                      shape                 colour                   thick glow  amp  smooth window anim stereo peak   fill  bars
    static const std::vector<WaveformPreset> presets {
        { "Spectral Mirror",   make(Shape::Mirrored,     Colour::SpectralBands,    1.5f, 1.0f, 1.0f, 0.3f, 4.0f,  1.0f, 0.0f, false, 0.78f), true },
        { "Classic",           make(Shape::Classic,      Colour::Channels,         1.5f, 0.5f, 1.0f, 0.0f, 4.0f,  1.0f, 1.0f, false, 0.75f), true },
        { "Neon Oscilloscope", make(Shape::Oscilloscope, Colour::SpectrumGradient, 2.5f, 1.8f, 1.6f, 0.35f, 0.05f, 1.0f, 0.0f, false, 0.75f), true },
        { "Minimal",           make(Shape::Filled,       Colour::Accent,           1.0f, 0.0f, 1.0f, 0.5f, 6.0f,  1.0f, 0.0f, false, 0.22f), true },
        { "Bass Pulse",        make(Shape::Bars,         Colour::SpectralBands,    1.5f, 1.2f, 1.3f, 0.6f, 1.5f,  1.4f, 0.6f, true,  0.9f, 32), true },
        { "Retro Bars",        make(Shape::Bars,         Colour::SpectrumGradient, 1.5f, 0.4f, 1.0f, 0.2f, 3.0f,  1.0f, 0.0f, true,  0.95f, 24), true },
        { "Circular Glow",     make(Shape::Circular,     Colour::SpectralBands,    2.0f, 1.6f, 1.2f, 0.5f, 2.0f,  0.6f, 0.0f, true,  0.5f), true },
    };
    return presets;
}

const WaveformPreset& defaultPreset()
{
    return builtIns().front();
}

juce::var styleToVar(const WaveformStyle& s)
{
    auto* o = new juce::DynamicObject();
    for (const auto& shape : kShapes)
        if (shape.value == s.shape)
            o->setProperty("shape", shape.key);
    for (const auto& colour : kColours)
        if (colour.value == s.colour)
            o->setProperty("colour", colour.key);
    o->setProperty("lineThickness", s.lineThickness);
    o->setProperty("glow", s.glow);
    o->setProperty("amplitude", s.amplitude);
    o->setProperty("smoothing", s.smoothing);
    o->setProperty("windowSeconds", s.windowSeconds);
    o->setProperty("animationSpeed", s.animationSpeed);
    o->setProperty("stereoSeparation", s.stereoSeparation);
    o->setProperty("peakHold", s.peakHold);
    o->setProperty("fillOpacity", s.fillOpacity);
    o->setProperty("direction", s.direction == Direction::NewestLeft ? "newestLeft" : "newestRight");
    o->setProperty("barCount", s.barCount);
    return juce::var(o);
}

bool styleFromVar(const juce::var& value, WaveformStyle& out, juce::String& error)
{
    auto* o = value.getDynamicObject();
    if (o == nullptr)
    {
        error = "style is not an object";
        return false;
    }

    WaveformStyle s; // missing optional fields keep their defaults

    // Shape: required, must be one we know.
    const auto shape = o->getProperty("shape");
    bool shapeKnown = false;
    for (const auto& candidate : kShapes)
        if (shape.isString() && shape.toString() == candidate.key)
        {
            s.shape = candidate.value;
            shapeKnown = true;
        }
    if (! shapeKnown)
    {
        error = shape.isVoid() ? "missing \"shape\"" : "unsupported shape \"" + shape.toString() + "\"";
        return false;
    }

    if (o->hasProperty("colour"))
    {
        const auto colour = o->getProperty("colour");
        bool known = false;
        for (const auto& candidate : kColours)
            if (colour.isString() && colour.toString() == candidate.key)
            {
                s.colour = candidate.value;
                known = true;
            }
        if (! known)
        {
            error = "unsupported colour source \"" + colour.toString() + "\"";
            return false;
        }
    }

    auto number = [&](const char* key, float& target, WaveformStyle::Range range) -> bool
    {
        if (! o->hasProperty(key))
            return true;
        const auto v = o->getProperty(key);
        if (! (v.isDouble() || v.isInt() || v.isInt64()) || ! std::isfinite((double) v)
            || (double) v < range.min - 1.0e-6 || (double) v > range.max + 1.0e-6)
        {
            error = juce::String("\"") + key + "\" must be a number from " + juce::String(range.min) + " to " + juce::String(range.max);
            return false;
        }
        target = (float) (double) v;
        return true;
    };

    if (! number("lineThickness", s.lineThickness, WaveformStyle::lineThicknessRange)
        || ! number("glow", s.glow, WaveformStyle::glowRange)
        || ! number("amplitude", s.amplitude, WaveformStyle::amplitudeRange)
        || ! number("smoothing", s.smoothing, WaveformStyle::smoothingRange)
        || ! number("windowSeconds", s.windowSeconds, WaveformStyle::windowRange)
        || ! number("animationSpeed", s.animationSpeed, WaveformStyle::animationSpeedRange)
        || ! number("stereoSeparation", s.stereoSeparation, WaveformStyle::stereoSeparationRange)
        || ! number("fillOpacity", s.fillOpacity, WaveformStyle::fillOpacityRange))
        return false;

    if (o->hasProperty("peakHold"))
    {
        const auto v = o->getProperty("peakHold");
        if (! v.isBool())
        {
            error = "\"peakHold\" must be true or false";
            return false;
        }
        s.peakHold = (bool) v;
    }

    if (o->hasProperty("direction"))
    {
        const auto v = o->getProperty("direction").toString();
        if (v == "newestRight")
            s.direction = Direction::NewestRight;
        else if (v == "newestLeft")
            s.direction = Direction::NewestLeft;
        else
        {
            error = "\"direction\" must be \"newestRight\" or \"newestLeft\"";
            return false;
        }
    }

    if (o->hasProperty("barCount"))
    {
        const auto v = o->getProperty("barCount");
        const double d = v.isDouble() || v.isInt() || v.isInt64() ? (double) v : -1.0;
        if (d != std::floor(d) || d < WaveformStyle::minBarCount || d > WaveformStyle::maxBarCount)
        {
            error = "\"barCount\" must be a whole number from " + juce::String(WaveformStyle::minBarCount) + " to "
                  + juce::String(WaveformStyle::maxBarCount);
            return false;
        }
        s.barCount = (int) d;
    }

    out = s;
    return true;
}

juce::String exportJson(const std::vector<WaveformPreset>& presets)
{
    juce::Array<juce::var> list;
    for (const auto& p : presets)
    {
        auto* entry = new juce::DynamicObject();
        entry->setProperty("name", p.name);
        entry->setProperty("style", styleToVar(p.style));
        list.add(juce::var(entry));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty("format", kExportFormat);
    root->setProperty("version", kFormatVersion);
    root->setProperty("presets", list);
    return juce::JSON::toString(juce::var(root));
}

ImportResult importJson(const juce::String& text)
{
    ImportResult result;

    juce::var root;
    const auto parsed = juce::JSON::parse(text, root);
    if (parsed.failed() || root.getDynamicObject() == nullptr)
    {
        result.errors.add("Not a valid JSON file" + (parsed.failed() ? ": " + parsed.getErrorMessage() : juce::String()));
        return result;
    }

    const auto format = root.getProperty("format", {}).toString();
    if (format != kExportFormat && format != kLibraryFormat)
    {
        result.errors.add("Not a Sonarc waveform preset file");
        return result;
    }

    const auto version = root.getProperty("version", {});
    if (! (version.isInt() || version.isInt64() || version.isDouble()) || (int) version < 1)
    {
        result.errors.add("The file has no valid format version");
        return result;
    }
    if ((int) version > kFormatVersion)
    {
        result.errors.add("Made by a newer version of Sonarc (format " + juce::String((int) version)
                          + "); this version reads format " + juce::String(kFormatVersion));
        return result;
    }

    const auto* list = root.getProperty("presets", {}).getArray();
    if (list == nullptr)
    {
        result.errors.add("The file contains no presets");
        return result;
    }

    for (int i = 0; i < list->size(); ++i)
    {
        const auto& entry = list->getReference(i);
        const auto name = entry.getProperty("name", {}).toString().trim();
        const auto label = name.isNotEmpty() ? "\"" + name + "\"" : "Preset " + juce::String(i + 1);

        if (! entry.getProperty("name", {}).isString() || name.isEmpty() || name.length() > kMaxNameLength)
        {
            result.errors.add(label + ": needs a name of 1 to " + juce::String(kMaxNameLength) + " characters");
            continue;
        }

        WaveformPreset preset;
        preset.name = name;
        juce::String error;
        if (! styleFromVar(entry.getProperty("style", {}), preset.style, error))
        {
            result.errors.add(label + ": " + error);
            continue;
        }
        preset.favourite = entry.getProperty("favourite", false).isBool() && (bool) entry.getProperty("favourite", false);
        result.presets.push_back(preset);
    }
    return result;
}

} // namespace waveformPresets

//==============================================================================
WaveformPresetLibrary::WaveformPresetLibrary(juce::File storage) : file(std::move(storage))
{
    load();
}

juce::File WaveformPresetLibrary::defaultFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Sonarc")
        .getChildFile("waveform-presets.json");
}

void WaveformPresetLibrary::load()
{
    user.clear();
    favouriteBuiltIns.clear();
    lastLoadError.clear();
    if (! file.existsAsFile())
        return;

    const auto text = file.loadFileAsString();
    auto imported = waveformPresets::importJson(text);
    for (auto& preset : imported.presets)
        if (! isBuiltIn(preset.name) && find(preset.name) == nullptr)
            user.push_back(preset);
    if (! imported.errors.isEmpty())
        lastLoadError = imported.errors.joinIntoString("\n");

    // Keep the parsed value alive while reading the array inside it.
    const auto root = juce::JSON::parse(text);
    const auto favourites = root.getProperty("favouriteBuiltIns", {});
    if (const auto* list = favourites.getArray())
        for (const auto& name : *list)
            if (isBuiltIn(name.toString()))
                favouriteBuiltIns.insert(name.toString());
}

void WaveformPresetLibrary::write() const
{
    juce::Array<juce::var> list;
    for (const auto& p : user)
    {
        auto* entry = new juce::DynamicObject();
        entry->setProperty("name", p.name);
        entry->setProperty("favourite", p.favourite);
        entry->setProperty("style", waveformPresets::styleToVar(p.style));
        list.add(juce::var(entry));
    }
    juce::Array<juce::var> favourites;
    for (const auto& name : favouriteBuiltIns)
        favourites.add(name);

    auto* root = new juce::DynamicObject();
    root->setProperty("format", kLibraryFormat);
    root->setProperty("version", waveformPresets::kFormatVersion);
    root->setProperty("presets", list);
    root->setProperty("favouriteBuiltIns", favourites);

    file.getParentDirectory().createDirectory();
    file.replaceWithText(juce::JSON::toString(juce::var(root)));
}

std::vector<WaveformPreset> WaveformPresetLibrary::all() const
{
    std::vector<WaveformPreset> list = waveformPresets::builtIns();
    for (auto& p : list)
        p.favourite = favouriteBuiltIns.count(p.name) > 0;
    list.insert(list.end(), user.begin(), user.end());
    return list;
}

const WaveformPreset* WaveformPresetLibrary::find(const juce::String& name) const
{
    for (const auto& p : waveformPresets::builtIns())
        if (p.name == name)
            return &p;
    for (const auto& p : user)
        if (p.name == name)
            return &p;
    return nullptr;
}

bool WaveformPresetLibrary::isBuiltIn(const juce::String& name) const
{
    for (const auto& p : waveformPresets::builtIns())
        if (p.name.equalsIgnoreCase(name))
            return true;
    return false;
}

juce::String WaveformPresetLibrary::uniqueName(const juce::String& wanted) const
{
    const auto base = wanted.trim().isEmpty() ? juce::String("My preset") : wanted.trim().substring(0, waveformPresets::kMaxNameLength - 5);
    auto taken = [this](const juce::String& n)
    {
        for (const auto& p : all())
            if (p.name.equalsIgnoreCase(n))
                return true;
        return false;
    };
    if (! taken(base))
        return base;
    for (int i = 2;; ++i)
        if (const auto candidate = base + " (" + juce::String(i) + ")"; ! taken(candidate))
            return candidate;
}

bool WaveformPresetLibrary::validName(const juce::String& name, juce::String& error)
{
    if (name.trim().isEmpty() || name.length() > waveformPresets::kMaxNameLength)
    {
        error = "A preset name needs 1 to " + juce::String(waveformPresets::kMaxNameLength) + " characters.";
        return false;
    }
    return true;
}

bool WaveformPresetLibrary::save(const juce::String& name, const WaveformStyle& style, juce::String& error)
{
    const auto trimmed = name.trim();
    if (! validName(trimmed, error))
        return false;
    if (isBuiltIn(trimmed))
    {
        error = "\"" + trimmed + "\" is a built-in preset and can't be changed. Use Save As to keep your version.";
        return false;
    }

    for (auto& p : user)
        if (p.name.equalsIgnoreCase(trimmed))
        {
            p.style = style;
            write();
            return true;
        }

    user.push_back({ trimmed, style, false, false });
    write();
    return true;
}

bool WaveformPresetLibrary::rename(const juce::String& from, const juce::String& to, juce::String& error)
{
    const auto trimmed = to.trim();
    if (isBuiltIn(from))
    {
        error = "Built-in presets can't be renamed.";
        return false;
    }
    if (! validName(trimmed, error))
        return false;
    if (! trimmed.equalsIgnoreCase(from) && (isBuiltIn(trimmed) || find(trimmed) != nullptr))
    {
        error = "There is already a preset called \"" + trimmed + "\".";
        return false;
    }
    for (auto& p : user)
        if (p.name == from)
        {
            p.name = trimmed;
            write();
            return true;
        }
    error = "Preset not found.";
    return false;
}

bool WaveformPresetLibrary::remove(const juce::String& name, juce::String& error)
{
    if (isBuiltIn(name))
    {
        error = "Built-in presets can't be deleted.";
        return false;
    }
    const auto before = user.size();
    user.erase(std::remove_if(user.begin(), user.end(), [&](const WaveformPreset& p) { return p.name == name; }), user.end());
    if (user.size() == before)
    {
        error = "Preset not found.";
        return false;
    }
    write();
    return true;
}

juce::String WaveformPresetLibrary::duplicate(const juce::String& name)
{
    const auto* source = find(name);
    if (source == nullptr)
        return {};
    WaveformPreset copy { uniqueName(name + " copy"), source->style, false, false };
    user.push_back(copy);
    write();
    return copy.name;
}

void WaveformPresetLibrary::setFavourite(const juce::String& name, bool favourite)
{
    if (isBuiltIn(name))
    {
        if (favourite)
            favouriteBuiltIns.insert(name);
        else
            favouriteBuiltIns.erase(name);
    }
    for (auto& p : user)
        if (p.name == name)
            p.favourite = favourite;
    write();
}

juce::StringArray WaveformPresetLibrary::add(const std::vector<WaveformPreset>& imported)
{
    juce::StringArray names;
    for (auto preset : imported)
    {
        preset.name = uniqueName(preset.name);
        preset.builtIn = false;
        user.push_back(preset);
        names.add(preset.name);
    }
    if (! imported.empty())
        write();
    return names;
}

} // namespace sonarc
