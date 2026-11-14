#pragma once

#include "dsp/EffectChain.h"
#include "plugin/Parameters.h"
#include "plugin/SignalFeed.h"

#include <juce_audio_processors/juce_audio_processors.h>

class DriftwaveProcessor final : public juce::AudioProcessor {
public:
    DriftwaveProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    // Presets arrive in Phase 4; until then the host sees a single program.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    /** Highest output sample since the editor last asked. Written by the audio
        thread, read (and cleared) by the editor's meter. */
    float takePeak(int channel) noexcept { return peak[channel].exchange(0.0f, std::memory_order_relaxed); }

    /** How far the slowed output is behind the input, for the editor. */
    float speedLagSeconds() const noexcept { return speedLag.load(std::memory_order_relaxed); }
    /** Ask the audio thread to jump back to the live input (the editor's button). */
    void requestSpeedResync() noexcept { resyncRequested.store(true, std::memory_order_relaxed); }

    /** Audio for the displays: what comes in and what goes out. Only filled while an editor is open. */
    driftwave::SignalFeed inputFeed, outputFeed;
    void setDisplaysActive(bool active) noexcept { displaysActive.store(active, std::memory_order_relaxed); }
    /** Loudest reverb sample (as mixed in) and deepest clip-guard reduction since last asked. */
    float takeWetPeak() noexcept { return wetPeak.exchange(0.0f, std::memory_order_relaxed); }
    float takeClipReductionDb() noexcept { return clipReduction.exchange(0.0f, std::memory_order_relaxed); }

private:
    driftwave::ParameterReader reader;
    driftwave::EffectChain chain;
    std::atomic<float> peak[2] { 0.0f, 0.0f };
    std::atomic<float> speedLag { 0.0f };
    std::atomic<float> wetPeak { 0.0f }, clipReduction { 0.0f };
    std::atomic<bool> displaysActive { false };
    std::atomic<bool> resyncRequested { false };
    bool wasPlaying = false;
    juce::int64 expectedTime = -1; // where the playhead should be next block if it does not jump

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DriftwaveProcessor)
};
