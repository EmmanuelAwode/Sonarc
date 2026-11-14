// Preset tests: every factory preset loads and stays in range, user presets
// round-trip through their files, and bad files are rejected or clamped.

#include "plugin/PluginProcessor.h"
#include "plugin/Presets.h"
#include "plugin/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const juce::String& what)
{
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    if (!ok)
        ++failures;
}

float value(DriftwaveProcessor& p, const char* paramId)
{
    return p.apvts.getRawParameterValue(paramId)->load();
}

} // namespace

int main()
{
    using namespace driftwave;

    juce::ScopedJuceInitialiser_GUI gui;
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("driftwave_preset_tests_" + juce::String(juce::Random::getSystemRandom().nextInt(1000000)));
    folder.deleteRecursively();

    DriftwaveProcessor processor;
    PresetManager manager(processor.apvts, folder);

    const auto factory = manager.list();
    check(factory.size() >= 8, "there are at least 8 factory presets (got " + juce::String(factory.size()) + ")");
    check(manager.currentName() == "Default", "a new instance shows the Default preset");

    bool allLoad = true, allInRange = true;
    for (const auto& preset : factory) {
        allLoad = allLoad && preset.factory && manager.load(preset) && manager.currentName() == preset.name;
        for (auto* base : processor.getParameters())
            if (auto* param = dynamic_cast<juce::RangedAudioParameter*>(base))
                allInRange = allInRange && param->getValue() >= 0.0f && param->getValue() <= 1.0f;
        // Every preset must produce finite audio straight away.
        processor.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> buffer(2, 256);
        juce::MidiBuffer midi;
        juce::Random random(3);
        for (int block = 0; block < 40; ++block) {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    buffer.setSample(ch, i, random.nextFloat() - 0.5f);
            processor.processBlock(buffer, midi);
            allInRange = allInRange && std::isfinite(buffer.getMagnitude(0, 256)) && buffer.getMagnitude(0, 256) <= 1.0f;
        }
    }
    check(allLoad, "every factory preset loads");
    check(allInRange, "every factory preset keeps parameters and output in range");

    manager.load(factory[1]);
    check(factory[1].name == "Slowed + Reverb" && value(processor, id::speedOn) > 0.5f
              && std::abs(value(processor, id::speed) - 80.0f) < 0.01f && std::abs(value(processor, id::reverbMix) - 40.0f) < 0.01f
              && std::abs(value(processor, id::reverbDecay) - 3.0f) < 0.01f,
          "Slowed + Reverb is 80% speed with 40% reverb at 3 s");
    manager.load(factory[0]);
    check(value(processor, id::speedOn) < 0.5f && std::abs(value(processor, id::reverbDecay) - 3.0f) < 0.01f,
          "Default puts everything back");

    // User presets.
    processor.apvts.getParameter(id::reverbDecay)->setValueNotifyingHost(processor.apvts.getParameter(id::reverbDecay)->convertTo0to1(4.5f));
    processor.apvts.getParameter(id::bassOn)->setValueNotifyingHost(1.0f);
    const auto saved = manager.saveUser("  My test: preset?  ");
    check(saved.name.isNotEmpty() && saved.file.existsAsFile(), "a user preset is saved as a file ('" + saved.name + "')");
    check(!saved.name.containsAnyOf(":?"), "characters that are not allowed in file names are removed");
    check(manager.currentName() == saved.name, "the saved preset becomes the current one");
    check(manager.list().size() == factory.size() + 1 && !manager.list().getLast().factory, "it is listed after the factory presets");

    manager.load(factory[0]);
    check(manager.load(saved) && std::abs(value(processor, id::reverbDecay) - 4.5f) < 0.02f && value(processor, id::bassOn) > 0.5f,
          "loading it brings the settings back");

    // The preset name travels with the plugin state.
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    manager.load(factory[2]);
    processor.setStateInformation(state.getData(), int(state.getSize()));
    check(manager.currentName() == saved.name && std::abs(value(processor, id::reverbDecay) - 4.5f) < 0.02f,
          "the current preset name is restored with the project");

    // Bad files.
    const auto garbage = folder.getChildFile(juce::String("Garbage") + PresetManager::kExtension);
    garbage.replaceWithText("this is not a preset");
    check(!manager.load({ "Garbage", false, garbage }) && std::abs(value(processor, id::reverbDecay) - 4.5f) < 0.02f,
          "a corrupt preset file is rejected and changes nothing");

    const auto wild = folder.getChildFile(juce::String("Wild") + PresetManager::kExtension);
    wild.replaceWithText("<DriftwavePreset version=\"1\"><Param id=\"reverbDecay\" value=\"99999\"/>"
                         "<Param id=\"speed\" value=\"nan\"/><Param id=\"noSuchThing\" value=\"1\"/></DriftwavePreset>");
    check(manager.load({ "Wild", false, wild }) && std::abs(value(processor, id::reverbDecay) - 5.0f) < 0.01f
              && std::abs(value(processor, id::speed) - 80.0f) < 0.01f,
          "out-of-range values are clamped and unknown or invalid entries ignored");

    check(!manager.removeUser(factory[0]), "factory presets cannot be deleted");
    check(PresetManager::cleanName("  ..  ").isEmpty() || PresetManager::cleanName("  ..  ") == "..", "odd names do not crash");
    check(manager.saveUser("   ").name.isEmpty(), "an empty name is refused");

    // Saved interface colours live in one small file and are treated as untrusted input.
    {
        using driftwave::ColourStore;
        const auto file = folder.getChildFile("Colours.xml");
        ColourStore store(file);
        const juce::Colour amber(0xffd4a65a), teal(0xff33ccbb);
        check(store.saved().empty() && store.lastUsed(amber) == amber, "no colours file: nothing saved, the default is used");
        check(store.save("  Teal  ", teal) && store.saved().size() == 1 && store.saved()[0].name == "Teal" && store.saved()[0].accent == teal,
              "a colour is saved under its name");
        check(store.lastUsed(amber) == teal, "the colour used last is remembered");
        check(store.save("teal", amber) && store.saved().size() == 1 && store.saved()[0].accent == amber, "saving the same name again replaces it");
        check(!store.save("   ", teal), "an empty colour name is refused");
        store.setLastUsed(juce::Colour(0x00050505));
        check(store.lastUsed(amber).isOpaque() && store.lastUsed(amber).getBrightness() >= 0.49f, "a colour too dark to read is brightened");
        check(store.remove("Teal") && store.saved().empty(), "a saved colour can be removed");
        file.replaceWithText("<DriftwaveColours last=\"zzzzzzzz\"><Colour name=\"Bad\" value=\"not a colour\"/>"
                             "<Colour name=\"Good\" value=\"ff3366cc\"/><Colour name=\"\" value=\"ff3366cc\"/></DriftwaveColours>");
        check(store.saved().size() == 1 && store.saved()[0].name == "Good" && store.lastUsed(amber) == amber,
              "invalid entries in the colours file are ignored");
        file.replaceWithText("garbage");
        check(store.saved().empty() && store.lastUsed(amber) == amber, "a corrupt colours file is ignored");
    }

    folder.deleteRecursively();
    std::printf("\n%s\n", failures == 0 ? "preset tests passed" : "preset tests FAILED");
    return failures == 0 ? 0 : 1;
}
