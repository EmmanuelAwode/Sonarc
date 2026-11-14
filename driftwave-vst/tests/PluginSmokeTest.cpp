// Loads the built .vst3 the way a DAW does and exercises it end to end:
// discovery, bus layout, processing, state save/restore and opening the editor.
// Usage: driftwave_plugin_smoke "<path to Driftwave FX.vst3>"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const juce::String& what)
{
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    if (!ok)
        ++failures;
}

juce::AudioProcessorParameter* findParam(juce::AudioPluginInstance& plugin, const juce::String& name)
{
    for (auto* p : plugin.getParameters())
        if (p->getName(64) == name)
            return p;
    return nullptr;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::printf("usage: driftwave_plugin_smoke <path to .vst3>\n");
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI gui;
    juce::VST3PluginFormat format;

    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile(found, juce::String(juce::CharPointer_UTF8(argv[1])));
    check(found.size() == 1, "the module exposes exactly one plugin");
    if (found.isEmpty())
        return 1;

    const auto& desc = *found[0];
    check(desc.name == "Driftwave FX", "name is 'Driftwave FX' (got '" + desc.name + "')");
    check(!desc.isInstrument, "registered as an effect");

    juce::String error;
    auto plugin = format.createInstanceFromDescription(desc, 48000.0, 512, error);
    check(plugin != nullptr, "instance created " + error);
    if (plugin == nullptr)
        return 1;

    check(plugin->getTotalNumInputChannels() == 2 && plugin->getTotalNumOutputChannels() == 2, "stereo in, stereo out");
    // 15 of ours plus the Bypass parameter the VST3 wrapper adds for the host.
    check(plugin->getParameters().size() == 16, "15 parameters plus host bypass (got " + juce::String(plugin->getParameters().size()) + ")");

    // Process at two sample rates, with a stop/start in between, as a host would.
    juce::Random random(1);
    for (double sampleRate : { 48000.0, 44100.0 }) {
        plugin->setRateAndBufferSizeDetails(sampleRate, 512);
        plugin->prepareToPlay(sampleRate, 512);
        check(plugin->getLatencySamples() == 0, "reports zero latency at " + juce::String(sampleRate) + " Hz");

        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        bool finite = true;
        float peak = 0.0f;
        for (int block = 0; block < 200; ++block) {
            if (block == 100)
                plugin->reset(); // transport stopped and restarted
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buffer.setSample(ch, i, block < 150 ? random.nextFloat() * 0.5f - 0.25f : 0.0f);
            plugin->processBlock(buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    finite = finite && std::isfinite(buffer.getSample(ch, i));
            peak = juce::jmax(peak, buffer.getMagnitude(0, 512));
        }
        check(finite, "output is finite");
        check(peak > 0.01f && peak <= 1.0f, "output level is sensible (peak " + juce::String(peak, 3) + ")");
        // The last blocks had silent input: what is left must be the reverb tail.
        check(buffer.getMagnitude(0, 512) > 0.0f, "reverb tail rings after the input stops");
        plugin->releaseResources();
    }

    // Slow-down: with the reverb off, a slowed signal must still be playing after the input stops.
    auto* speedOn = findParam(*plugin, "Speed On");
    auto* speed = findParam(*plugin, "Speed");
    auto* reverbOn = findParam(*plugin, "Reverb On");
    check(speedOn != nullptr && speed != nullptr && reverbOn != nullptr, "speed parameters exist");
    if (speedOn != nullptr && speed != nullptr && reverbOn != nullptr) {
        reverbOn->setValueNotifyingHost(0.0f);
        auto* speedSync = findParam(*plugin, "Speed Sync");
        check(speedSync != nullptr && speedSync->getDefaultValue() < 0.5f, "sync is off by default");
        if (speedSync != nullptr)
            speedSync->setValueNotifyingHost(0.0f); // plain tape mode for this check
        speedOn->setValueNotifyingHost(1.0f);
        speed->setValueNotifyingHost(0.0f); // 50%
        plugin->prepareToPlay(48000.0, 512);
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        float lateLevel = 0.0f;
        bool finite = true;
        for (int block = 0; block < 300; ++block) {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buffer.setSample(ch, i, block < 200 ? random.nextFloat() * 0.5f - 0.25f : 0.0f);
            plugin->processBlock(buffer, midi);
            finite = finite && std::isfinite(buffer.getMagnitude(0, 512));
            if (block >= 250)
                lateLevel = juce::jmax(lateLevel, buffer.getMagnitude(0, 512));
        }
        check(finite, "slowed output is finite");
        check(lateLevel > 0.05f, "slowed audio keeps playing after the input stops (level " + juce::String(lateLevel, 3) + ")");
        check(plugin->getLatencySamples() == 0, "slow-down reports no latency");
        plugin->releaseResources();
        speedOn->setValueNotifyingHost(0.0f);
        reverbOn->setValueNotifyingHost(1.0f);
    }

    // State round trip.
    auto* decay = findParam(*plugin, "Reverb Decay");
    auto* bassOn = findParam(*plugin, "Bass On");
    check(decay != nullptr && bassOn != nullptr, "parameters can be found by name");
    if (decay != nullptr && bassOn != nullptr) {
        decay->setValueNotifyingHost(0.8f);
        bassOn->setValueNotifyingHost(1.0f);
        juce::MemoryBlock saved;
        plugin->getStateInformation(saved);
        check(saved.getSize() > 0, "state is saved");

        decay->setValueNotifyingHost(0.1f);
        bassOn->setValueNotifyingHost(0.0f);
        plugin->setStateInformation(saved.getData(), int(saved.getSize()));
        check(std::abs(decay->getValue() - 0.8f) < 0.01f && bassOn->getValue() > 0.5f, "state is restored");

        const char garbage[] = "this is not a plugin state";
        plugin->setStateInformation(garbage, int(sizeof(garbage)));
        check(std::abs(decay->getValue() - 0.8f) < 0.01f, "a corrupt state is ignored");
    }

    // Editor.
    if (auto* editor = plugin->createEditorIfNeeded()) {
        check(editor->getWidth() == 960 && editor->getHeight() == 328,
              "editor opens at " + juce::String(editor->getWidth()) + " x " + juce::String(editor->getHeight()));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
        delete editor;
    } else {
        check(false, "editor could not be created");
    }

    plugin.reset();
    std::printf("\n%s\n", failures == 0 ? "plugin smoke test passed" : "plugin smoke test FAILED");
    return failures == 0 ? 0 : 1;
}
