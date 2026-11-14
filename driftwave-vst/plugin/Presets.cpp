#include "plugin/Presets.h"

#include <vector>

namespace driftwave {

namespace {

constexpr const char* kPresetTag = "DriftwavePreset";
constexpr const char* kNameProperty = "presetName";

struct PresetValue {
    const char* paramId;
    float value;
};

struct FactoryPreset {
    const char* name;
    std::vector<PresetValue> values; // anything not listed is the parameter's default
};

// Sync is a choice about how the plugin is being used (in time with the song, or a true
// slow-down that drifts), not part of a sound: presets neither store it nor change it.
bool belongsInPresets(const juce::String& paramId) { return paramId != id::speedSync; }

// Values are in the units the knobs show. "Slowed + Reverb" and "Sped Up" are the
// reference settings (80% with 40% reverb at 3 s; 120% with no reverb); the rest are variations.
const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = {
        { "Default", {} },
        { "Slowed + Reverb", { { id::speedOn, 1 }, { id::speed, 80 }, { id::reverbMix, 40 }, { id::reverbDecay, 3.0f } } },
        { "Sped Up", { { id::speedOn, 1 }, { id::speed, 120 }, { id::reverbOn, 0 } } },
        { "Slowed Only", { { id::speedOn, 1 }, { id::speed, 80 }, { id::reverbOn, 0 } } },
        { "Reverb Only", { { id::reverbMix, 40 }, { id::reverbDecay, 3.0f } } },
        { "Deep Slowed",
          { { id::speedOn, 1 }, { id::speed, 70 }, { id::reverbMix, 45 }, { id::reverbDecay, 4.0f }, { id::reverbHighCut, 9000 },
            { id::bassOn, 1 }, { id::bassGain, 4 }, { id::bassFreq, 80 } } },
        { "Dreamy",
          { { id::speedOn, 1 }, { id::speed, 85 }, { id::reverbMix, 55 }, { id::reverbDecay, 5.0f }, { id::reverbLowCut, 250 },
            { id::reverbHighCut, 8000 } } },
        { "Night Drive",
          { { id::speedOn, 1 }, { id::speed, 88 }, { id::reverbMix, 30 }, { id::reverbDecay, 2.2f }, { id::reverbLowCut, 150 },
            { id::bassOn, 1 }, { id::bassGain, 5 }, { id::bassFreq, 90 }, { id::bassDrive, 15 } } },
        { "Heavy Bass",
          { { id::speedOn, 1 }, { id::speed, 80 }, { id::reverbMix, 25 }, { id::reverbDecay, 2.5f }, { id::reverbLowCut, 200 },
            { id::bassOn, 1 }, { id::bassGain, 10 }, { id::bassFreq, 70 }, { id::bassDrive, 35 }, { id::outputGain, -3 } } },
        { "Light Reverb", { { id::reverbMix, 20 }, { id::reverbDecay, 1.5f }, { id::reverbLowCut, 200 } } },
        { "Nightcore", { { id::speedOn, 1 }, { id::speed, 130 }, { id::reverbOn, 0 } } },
    };
    return presets;
}

} // namespace

PresetManager::PresetManager(juce::AudioProcessorValueTreeState& s, juce::File f) : state(s), folder(std::move(f)) {}

juce::File PresetManager::defaultFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Driftwave FX").getChildFile("Presets");
}

juce::String PresetManager::cleanName(const juce::String& name)
{
    return juce::File::createLegalFileName(name.trim()).trim().substring(0, 60).trim();
}

juce::Array<PresetInfo> PresetManager::list() const
{
    juce::Array<PresetInfo> result;
    for (const auto& preset : factoryPresets())
        result.add({ preset.name, true, {} });

    auto files = folder.findChildFiles(juce::File::findFiles, false, juce::String("*") + kExtension);
    files.sort();
    for (const auto& file : files)
        result.add({ file.getFileNameWithoutExtension(), false, file });
    return result;
}

void PresetManager::apply(const juce::StringPairArray& values, const juce::String& name)
{
    for (auto* base : state.processor.getParameters()) {
        auto* param = dynamic_cast<juce::RangedAudioParameter*>(base);
        if (param == nullptr)
            continue;
        const auto paramId = param->getParameterID();
        if (state.getParameter(paramId) != param) // the host's own bypass parameter is not ours to set
            continue;
        if (!belongsInPresets(paramId))
            continue;

        float normalised = param->getDefaultValue();
        if (values.containsKey(paramId)) {
            const auto& range = param->getNormalisableRange();
            const float value = values[paramId].getFloatValue();
            if (std::isfinite(value))
                normalised = range.convertTo0to1(range.snapToLegalValue(juce::jlimit(range.start, range.end, value)));
        }
        if (std::abs(param->getValue() - normalised) > 1.0e-6f) {
            param->beginChangeGesture();
            param->setValueNotifyingHost(normalised);
            param->endChangeGesture();
        }
    }
    state.state.setProperty(kNameProperty, name, nullptr);
}

bool PresetManager::load(const PresetInfo& preset)
{
    juce::StringPairArray values;

    if (preset.factory) {
        const FactoryPreset* found = nullptr;
        for (const auto& candidate : factoryPresets())
            if (preset.name == candidate.name)
                found = &candidate;
        if (found == nullptr)
            return false;
        for (const auto& [paramId, value] : found->values)
            values.set(paramId, juce::String(value));
    } else {
        // A user file is untrusted input: only our own tag, only known parameters, only finite numbers in range.
        const auto xml = juce::parseXML(preset.file);
        if (xml == nullptr || !xml->hasTagName(kPresetTag))
            return false;
        for (const auto* child : xml->getChildWithTagNameIterator("Param"))
            values.set(child->getStringAttribute("id"), child->getStringAttribute("value"));
    }

    apply(values, preset.name);
    return true;
}

PresetInfo PresetManager::saveUser(const juce::String& rawName)
{
    const auto name = cleanName(rawName);
    if (name.isEmpty() || !folder.createDirectory())
        return {};

    juce::XmlElement xml(kPresetTag);
    xml.setAttribute("version", 1);
    xml.setAttribute("name", name);
    for (auto* base : state.processor.getParameters()) {
        auto* param = dynamic_cast<juce::RangedAudioParameter*>(base);
        if (param == nullptr || state.getParameter(param->getParameterID()) != param || !belongsInPresets(param->getParameterID()))
            continue;
        auto* child = xml.createNewChildElement("Param");
        child->setAttribute("id", param->getParameterID());
        child->setAttribute("value", double(param->convertFrom0to1(param->getValue())));
    }

    const auto file = folder.getChildFile(name + kExtension);
    if (!xml.writeTo(file))
        return {};
    state.state.setProperty(kNameProperty, name, nullptr);
    return { name, false, file };
}

bool PresetManager::removeUser(const PresetInfo& preset)
{
    if (preset.factory || !preset.file.existsAsFile() || !preset.file.isAChildOf(folder))
        return false;
    return preset.file.moveToTrash();
}

juce::String PresetManager::currentName() const
{
    return state.state.getProperty(kNameProperty, factoryPresets().front().name).toString();
}

} // namespace driftwave
