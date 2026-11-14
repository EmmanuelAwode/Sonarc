// Renders the plugin interface to a PNG without a DAW.
// Usage: driftwave_ui_snapshot <out.png> [scale] [visualiser] [preset name] [style 0-3] [accent rrggbb]

#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstdio>
#include <functional>

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::printf("usage: driftwave_ui_snapshot <out.png> [scale]\n");
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI gui;
    DriftwaveProcessor processor;
    processor.setRateAndBufferSizeDetails(48000.0, 512); // a host always does this before preparing
    processor.prepareToPlay(48000.0, 512);
    if (argc > 3 && juce::String(argv[3]) == "visualiser")
        processor.apvts.state.setProperty("visualiser", true, nullptr);
    if (argc > 6)
        processor.apvts.state.setProperty("accentColour", juce::String("ff") + juce::String(argv[6]), nullptr);
    if (argc > 5)
        processor.apvts.state.setProperty("visualiserStyle", juce::String(argv[5]).getIntValue(), nullptr);
    if (argc > 4) {
        driftwave::PresetManager manager(processor.apvts, driftwave::PresetManager::defaultFolder());
        for (const auto& preset : manager.list())
            if (preset.name == juce::String(argv[4]))
                manager.load(preset);
    }
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());

    // The displays normally run from a timer while the window is on screen. Here there
    // is no window, so find them and drive them by hand while audio goes through.
    driftwave::DisplayBand* displays = nullptr;
    std::function<void(juce::Component&)> find = [&](juce::Component& c) {
        if (auto* band = dynamic_cast<driftwave::DisplayBand*>(&c))
            displays = band;
        for (auto* child : c.getChildren())
            find(*child);
    };
    find(*editor);
    processor.setDisplaysActive(true);

    // Something music-like so the displays have a shape to show: a few decaying notes over a low tone, plus a little noise.
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    juce::Random random(7);
    double time = 0.0;
    for (int block = 0; block < 450; ++block) {
        for (int i = 0; i < 512; ++i, time += 1.0 / 48000.0) {
            const double beat = std::fmod(time, 0.5);
            const double pluck = std::exp(-beat * 9.0);
            const double note = 220.0 * std::pow(2.0, double(int(time * 2.0) % 5) * 3.0 / 12.0);
            const double sample = 0.30 * pluck * std::sin(juce::MathConstants<double>::twoPi * note * time)
                                + 0.16 * pluck * std::sin(juce::MathConstants<double>::twoPi * note * 2.01 * time)
                                + 0.22 * std::exp(-beat * 14.0) * std::sin(juce::MathConstants<double>::twoPi * 55.0 * time)
                                + 0.02 * pluck * (random.nextDouble() - 0.5);
            buffer.setSample(0, i, float(sample));
            buffer.setSample(1, i, float(sample * 0.9));
        }
        processor.processBlock(buffer, midi);
        if (displays != nullptr && block % 3 == 2)
            displays->refreshNow();
    }

    const float scale = argc > 2 ? juce::String(argv[2]).getFloatValue() : 2.0f;
    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, scale);
    const juce::File out { juce::String(juce::CharPointer_UTF8(argv[1])) };
    out.deleteFile();
    juce::FileOutputStream stream(out);
    const bool ok = stream.openedOk() && juce::PNGImageFormat().writeImageToStream(image, stream);
    std::printf(ok ? "wrote %s\n" : "could not write %s\n", argv[1]);
    return ok ? 0 : 1;
}
