#include "plugin/Parameters.h"

namespace driftwave {

namespace {

using FloatParam = juce::AudioParameterFloat;
using BoolParam = juce::AudioParameterBool;
using Range = juce::NormalisableRange<float>;
using Attributes = juce::AudioParameterFloatAttributes;

constexpr int kVersion1 = 1; // parameters that shipped in 0.1.0
constexpr int kVersion2 = 2; // added in 0.2.0

Range skewed(float min, float max, float step, float centre)
{
    Range r(min, max, step);
    r.setSkewForCentre(centre);
    return r;
}

Attributes withUnit(const char* unit, int decimals)
{
    const juce::String suffix(unit);
    return Attributes()
        .withStringFromValueFunction([suffix, decimals](float v, int) { return juce::String(v, decimals) + suffix; })
        .withValueFromStringFunction([](const juce::String& s) { return s.getFloatValue(); });
}

Attributes hertz()
{
    return Attributes()
        .withStringFromValueFunction([](float v, int) {
            return v >= 1000.0f ? juce::String(v / 1000.0f, 1) + " kHz" : juce::String(juce::roundToInt(v)) + " Hz";
        })
        .withValueFromStringFunction([](const juce::String& s) {
            const float v = s.getFloatValue();
            return s.containsIgnoreCase("k") ? v * 1000.0f : v;
        });
}

std::unique_ptr<FloatParam> makeFloat(const char* paramId, const char* name, Range range, float def, Attributes attr)
{
    return std::make_unique<FloatParam>(juce::ParameterID { paramId, kVersion1 }, name, range, def, attr);
}

std::unique_ptr<BoolParam> makeBool(const char* paramId, const char* name, bool def)
{
    return std::make_unique<BoolParam>(juce::ParameterID { paramId, kVersion1 }, name, def);
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // The reverb's defaults are the reference "slowed + reverb" sound: 40% mix,
    // 3 seconds, no filtering. (Size, pre-delay, damping and width existed up to
    // 0.3.0 and are gone: the convolution reverb has no use for them.)
    layout.add(makeBool(id::reverbOn, "Reverb On", true));
    layout.add(makeFloat(id::reverbMix, "Reverb Mix", Range(0.0f, 100.0f, 0.1f), 40.0f, withUnit("%", 0)));
    layout.add(makeFloat(id::reverbDecay, "Reverb Decay", skewed(0.5f, 5.0f, 0.01f, 3.0f), 3.0f, withUnit(" s", 1)));
    layout.add(makeFloat(id::reverbLowCut, "Reverb Low Cut", skewed(20.0f, 1000.0f, 1.0f, 200.0f), 20.0f, hertz()));
    layout.add(makeFloat(id::reverbHighCut, "Reverb High Cut", skewed(1000.0f, 20000.0f, 1.0f, 6000.0f), 20000.0f, hertz()));

    layout.add(makeBool(id::bassOn, "Bass On", false));
    layout.add(makeFloat(id::bassGain, "Bass Gain", Range(0.0f, 18.0f, 0.1f), 6.0f, withUnit(" dB", 1)));
    layout.add(makeFloat(id::bassFreq, "Bass Frequency", Range(40.0f, 200.0f, 1.0f), 90.0f, hertz()));
    layout.add(makeFloat(id::bassDrive, "Bass Drive", Range(0.0f, 100.0f, 0.1f), 0.0f, withUnit("%", 0)));

    layout.add(makeFloat(id::outputGain, "Output Gain", Range(-24.0f, 12.0f, 0.1f), 0.0f, withUnit(" dB", 1)));
    layout.add(makeBool(id::clipOn, "Clip Guard", true));
    layout.add(makeFloat(id::ceiling, "Ceiling", Range(-12.0f, 0.0f, 0.1f), -1.0f, withUnit(" dB", 1)));

    // Added in 0.2.0. Appended, with a higher version hint, so existing projects and automation keep working.
    layout.add(std::make_unique<BoolParam>(juce::ParameterID { id::speedOn, kVersion2 }, "Speed On", false));
    layout.add(std::make_unique<FloatParam>(juce::ParameterID { id::speed, kVersion2 }, "Speed", Range(50.0f, 150.0f, 0.1f), 80.0f,
                                            withUnit("%", 0)));
    layout.add(std::make_unique<BoolParam>(juce::ParameterID { id::speedSync, kVersion2 }, "Speed Sync", false));

    return layout;
}

ParameterReader::ParameterReader(juce::AudioProcessorValueTreeState& state)
    : reverbOn(state.getRawParameterValue(id::reverbOn)),
      reverbMix(state.getRawParameterValue(id::reverbMix)),
      reverbDecay(state.getRawParameterValue(id::reverbDecay)),
      reverbLowCut(state.getRawParameterValue(id::reverbLowCut)),
      reverbHighCut(state.getRawParameterValue(id::reverbHighCut)),
      bassOn(state.getRawParameterValue(id::bassOn)),
      bassGain(state.getRawParameterValue(id::bassGain)),
      bassFreq(state.getRawParameterValue(id::bassFreq)),
      bassDrive(state.getRawParameterValue(id::bassDrive)),
      outputGain(state.getRawParameterValue(id::outputGain)),
      clipOn(state.getRawParameterValue(id::clipOn)),
      ceiling(state.getRawParameterValue(id::ceiling)),
      speedOn(state.getRawParameterValue(id::speedOn)),
      speed(state.getRawParameterValue(id::speed)),
      speedSync(state.getRawParameterValue(id::speedSync))
{
}

ChainParams ParameterReader::read() const noexcept
{
    const auto get = [](const std::atomic<float>* p) { return p->load(std::memory_order_relaxed); };

    ChainParams p;
    p.reverbOn = get(reverbOn) > 0.5f;
    p.reverbMix = get(reverbMix) * 0.01f;
    p.reverb.decaySeconds = get(reverbDecay);
    p.reverb.lowCutHz = get(reverbLowCut);
    p.reverb.highCutHz = get(reverbHighCut);
    p.bassOn = get(bassOn) > 0.5f;
    p.bassGainDb = get(bassGain);
    p.bassFreqHz = get(bassFreq);
    p.bassDrive = get(bassDrive) * 0.01f;
    p.outputGainDb = get(outputGain);
    p.clipOn = get(clipOn) > 0.5f;
    p.ceilingDb = get(ceiling);
    p.speedOn = get(speedOn) > 0.5f;
    p.speed = get(speed) * 0.01f;
    p.speedSync = get(speedSync) > 0.5f;
    return p;
}

} // namespace driftwave
