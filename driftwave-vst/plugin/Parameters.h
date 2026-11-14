#pragma once

#include "dsp/EffectChain.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace driftwave {

/** Parameter IDs. These are stored in FL Studio projects and automation clips:
    never rename or remove one. New parameters get a higher version hint. */
namespace id {
inline constexpr const char* reverbOn = "reverbOn";
inline constexpr const char* reverbMix = "reverbMix";
inline constexpr const char* reverbDecay = "reverbDecay";
inline constexpr const char* reverbLowCut = "reverbLowCut";
inline constexpr const char* reverbHighCut = "reverbHighCut";
inline constexpr const char* bassOn = "bassOn";
inline constexpr const char* bassGain = "bassGain";
inline constexpr const char* bassFreq = "bassFreq";
inline constexpr const char* bassDrive = "bassDrive";
inline constexpr const char* outputGain = "outputGain";
inline constexpr const char* clipOn = "clipOn";
inline constexpr const char* ceiling = "ceiling";
inline constexpr const char* speedOn = "speedOn";
inline constexpr const char* speed = "speed";
inline constexpr const char* speedSync = "speedSync";
} // namespace id

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Lock-free view of the parameters for the audio thread. */
class ParameterReader {
public:
    explicit ParameterReader(juce::AudioProcessorValueTreeState& state);
    ChainParams read() const noexcept;

private:
    std::atomic<float>* reverbOn;
    std::atomic<float>* reverbMix;
    std::atomic<float>* reverbDecay;
    std::atomic<float>* reverbLowCut;
    std::atomic<float>* reverbHighCut;
    std::atomic<float>* bassOn;
    std::atomic<float>* bassGain;
    std::atomic<float>* bassFreq;
    std::atomic<float>* bassDrive;
    std::atomic<float>* outputGain;
    std::atomic<float>* clipOn;
    std::atomic<float>* ceiling;
    std::atomic<float>* speedOn;
    std::atomic<float>* speed;
    std::atomic<float>* speedSync;
};

} // namespace driftwave
