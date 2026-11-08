#include "TestHarness.h"
#include "presets/WaveformPresets.h"

using namespace sonarc;
namespace wp = waveformPresets;

namespace {

juce::String exportOne(const WaveformStyle& style, const juce::String& name = "Test")
{
    return wp::exportJson({ { name, style, false, false } });
}

juce::String withStyleField(const juce::String& key, const juce::var& value)
{
    auto style = wp::styleToVar(WaveformStyle {});
    style.getDynamicObject()->setProperty(key, value);
    auto* entry = new juce::DynamicObject();
    entry->setProperty("name", "Edited");
    entry->setProperty("style", style);
    auto* root = new juce::DynamicObject();
    root->setProperty("format", "sonarc-waveform-preset");
    root->setProperty("version", 1);
    root->setProperty("presets", juce::Array<juce::var> { juce::var(entry) });
    return juce::JSON::toString(juce::var(root));
}

struct TempLibrary
{
    juce::TemporaryFile temp { ".json" };
    juce::File file() const { return temp.getFile(); }
};

} // namespace

TEST_CASE("Presets: built-ins are complete, unique and start with the default")
{
    const auto& list = wp::builtIns();
    CHECK(list.size() == 7);
    CHECK(wp::defaultPreset().name == "Spectral Mirror");
    for (const char* expected : { "Classic", "Neon Oscilloscope", "Minimal", "Bass Pulse", "Retro Bars", "Circular Glow" })
    {
        bool found = false;
        for (const auto& p : list)
            found = found || p.name == expected;
        CHECK(found);
    }
    for (size_t i = 0; i < list.size(); ++i)
    {
        CHECK(list[i].builtIn);
        for (size_t j = i + 1; j < list.size(); ++j)
            CHECK(list[i].name != list[j].name);
    }
}

TEST_CASE("Presets: every built-in survives export and import unchanged")
{
    const auto result = wp::importJson(wp::exportJson(wp::builtIns()));
    CHECK(result.errors.isEmpty());
    CHECK(result.presets.size() == wp::builtIns().size());
    for (size_t i = 0; i < result.presets.size() && i < wp::builtIns().size(); ++i)
    {
        CHECK(result.presets[i].name == wp::builtIns()[i].name);
        CHECK(result.presets[i].style == wp::builtIns()[i].style);
        CHECK(! result.presets[i].builtIn); // imported copies are never built-in
    }
}

TEST_CASE("Presets: malformed files are rejected with a reason")
{
    CHECK(wp::importJson("this is not json {").presets.empty());
    CHECK(! wp::importJson("this is not json {").errors.isEmpty());
    CHECK(wp::importJson("{\"hello\": 1}").errors[0].contains("Not a Sonarc"));
    CHECK(wp::importJson("{\"format\": \"sonarc-waveform-preset\", \"presets\": []}").errors[0].contains("version"));

    const auto newer = wp::importJson("{\"format\": \"sonarc-waveform-preset\", \"version\": 7, \"presets\": []}");
    CHECK(newer.presets.empty());
    CHECK(newer.errors[0].contains("newer version"));
}

TEST_CASE("Presets: invalid settings are rejected, not clamped")
{
    auto rejects = [](const juce::String& json, const char* reasonPart)
    {
        const auto result = wp::importJson(json);
        const bool ok = result.presets.empty() && result.errors.size() == 1 && result.errors[0].contains(reasonPart);
        if (! ok)
            std::printf("    got: %s\n", result.errors.joinIntoString(" | ").toRawUTF8());
        return ok;
    };

    CHECK(rejects(withStyleField("shape", "spiral"), "unsupported shape"));
    CHECK(rejects(withStyleField("glow", 9.0), "glow"));
    CHECK(rejects(withStyleField("amplitude", "loud"), "amplitude"));
    CHECK(rejects(withStyleField("peakHold", 1), "peakHold"));
    CHECK(rejects(withStyleField("barCount", 12.5), "barCount"));
    CHECK(rejects(withStyleField("direction", "up"), "direction"));
    CHECK(rejects(withStyleField("colour", "rainbow"), "colour"));
}

TEST_CASE("Presets: a bad preset doesn't spoil the good ones in the same file")
{
    auto root = juce::JSON::parse(exportOne(WaveformStyle {}, "Good"));
    auto* bad = new juce::DynamicObject();
    bad->setProperty("name", "");
    bad->setProperty("style", wp::styleToVar(WaveformStyle {}));
    root.getProperty("presets", {}).getArray()->add(juce::var(bad));

    const auto result = wp::importJson(juce::JSON::toString(root));
    CHECK(result.presets.size() == 1);
    CHECK(result.presets[0].name == "Good");
    CHECK(result.errors.size() == 1);
}

TEST_CASE("Presets: missing optional fields fall back to defaults")
{
    const auto result = wp::importJson("{\"format\":\"sonarc-waveform-preset\",\"version\":1,"
                                       "\"presets\":[{\"name\":\"Old\",\"style\":{\"shape\":\"bars\"}}]}");
    CHECK(result.errors.isEmpty());
    CHECK(result.presets.size() == 1);
    WaveformStyle expected;
    expected.shape = WaveformStyle::Shape::Bars;
    CHECK(result.presets[0].style == expected);
}

TEST_CASE("Preset library: save, rename, duplicate, delete, favourites, and reload")
{
    TempLibrary temp;
    juce::String error;
    {
        WaveformPresetLibrary library(temp.file());
        WaveformStyle style;
        style.shape = WaveformStyle::Shape::Circular;
        style.glow = 1.7f;

        CHECK(library.save("My Circle", style, error));
        CHECK(library.find("My Circle") != nullptr);
        CHECK(! library.save("Classic", style, error)); // built-ins can't be overwritten
        CHECK(error.contains("built-in"));

        CHECK(library.rename("My Circle", "Stage Circle", error));
        CHECK(library.find("My Circle") == nullptr);
        CHECK(! library.rename("Classic", "Mine", error));
        CHECK(! library.rename("Stage Circle", "Minimal", error)); // name taken by a built-in

        const auto copy = library.duplicate("Stage Circle");
        CHECK(copy == "Stage Circle copy");
        CHECK(library.duplicate("Classic") == "Classic copy"); // built-ins can be duplicated

        CHECK(! library.remove("Spectral Mirror", error));
        CHECK(library.remove("Classic copy", error));

        library.setFavourite("Retro Bars", true);
        library.setFavourite("Stage Circle", true);
        CHECK(library.uniqueName("Stage Circle") == "Stage Circle (2)");
    }

    // Everything is persisted to disk.
    WaveformPresetLibrary reloaded(temp.file());
    CHECK(reloaded.loadError().isEmpty());
    const auto* circle = reloaded.find("Stage Circle");
    CHECK(circle != nullptr);
    if (circle != nullptr)
    {
        CHECK(circle->style.shape == WaveformStyle::Shape::Circular);
        CHECK_NEAR(circle->style.glow, 1.7, 1e-5);
        CHECK(circle->favourite);
    }
    CHECK(reloaded.find("Stage Circle copy") != nullptr);
    CHECK(reloaded.find("Classic copy") == nullptr);

    bool retroFavourite = false;
    for (const auto& p : reloaded.all())
        if (p.name == "Retro Bars")
            retroFavourite = p.favourite;
    CHECK(retroFavourite);
    CHECK(reloaded.all().size() == wp::builtIns().size() + 2);
}

TEST_CASE("Preset library: imports get unique names and never become built-ins")
{
    TempLibrary temp;
    WaveformPresetLibrary library(temp.file());
    const auto imported = wp::importJson(wp::exportJson(wp::builtIns()));
    const auto names = library.add(imported.presets);
    CHECK(names.size() == (int) wp::builtIns().size());
    CHECK(names.contains("Classic (2)"));
    juce::String error;
    CHECK(library.remove("Classic (2)", error));
    CHECK(library.find("Classic") != nullptr);
}
