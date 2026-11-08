#include "ui/Theme.h"

namespace sonarc::theme {

namespace {

using C = juce::Colour;

Colours make(C backgroundColour, C panelColour, C controlsColour, C gridColour, C labelsColour, C textColour, C accentColour,
             std::array<juce::uint32, 6> spectrum, C peakHoldColour,
             C low, C mid, C high, C left, C right,
             std::array<juce::uint32, 5> meter, float fill = 0.55f, float glowAmount = 1.0f)
{
    Colours c;
    c.background = backgroundColour;
    c.panel = panelColour;
    c.controls = controlsColour;
    c.grid = gridColour;
    c.labels = labelsColour;
    c.text = textColour;
    c.accent = accentColour;
    for (size_t i = 0; i < spectrum.size(); ++i)
        c.spectrum[i] = C(spectrum[i]);
    c.spectrumFill = fill;
    c.glow = glowAmount;
    c.peakHold = peakHoldColour;
    c.waveLow = low;
    c.waveMid = mid;
    c.waveHigh = high;
    c.waveLeft = left;
    c.waveRight = right;
    for (size_t i = 0; i < meter.size(); ++i)
        c.meter[i] = C(meter[i]);
    return c;
}

const char* const kSpectrumKeys[] = { "spectrum20", "spectrum90", "spectrum400", "spectrum2k", "spectrum6k", "spectrum20k" };
const char* const kMeterKeys[] = { "meterFloor", "meterLow", "meterMid", "meterHigh", "meterClip" };

} // namespace

const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list {
        { "Sonarc", make(C(0xff0b0d10), C(0xff12151a), C(0xff1a1e25), C(0x14ffffff), C(0xff6b7484), C(0xffc5ccd6), C(0xff3ad0e0),
                         { 0xff6b4bff, 0xff2f7bff, 0xff22d3ee, 0xff3ee6a8, 0xffffd166, 0xffff7a59 }, C(0x59eef2f6),
                         C(0xffff3d6b), C(0xffffb82e), C(0xff59e6ff), C(0xff3ad0e0), C(0xffffb648),
                         { 0xff1f7fd8, 0xff3ad0e0, 0xff48d597, 0xffffb648, 0xffff5a5f }) },

        { "Neon Night", make(C(0xff07060d), C(0xff0e0b18), C(0xff1a1528), C(0x18ffffff), C(0xff7a6f99), C(0xffd9d2f2), C(0xffff3df2),
                             { 0xff7b2cff, 0xffb42cff, 0xffff2ccf, 0xffff4f8b, 0xffffb02e, 0xfffff35c }, C(0x59ffe6ff),
                             C(0xffff2cc4), C(0xff9d5bff), C(0xff2ef2ff), C(0xffff3df2), C(0xff2ef2ff),
                             { 0xff6a2cff, 0xffc02cff, 0xffff2ccf, 0xffffb02e, 0xffff3b3b }, 0.6f, 1.4f) },

        { "Sunset", make(C(0xff0f0a0a), C(0xff171010), C(0xff241919), C(0x16ffe8d6), C(0xff8f7a72), C(0xfff0ddd2), C(0xffff8a3d),
                         { 0xff5a1e6e, 0xffa12a6b, 0xffe0455a, 0xffff7a3d, 0xffffb547, 0xffffe28a }, C(0x59fff0e0),
                         C(0xffe0245a), C(0xffff8a3d), C(0xffffe28a), C(0xffff8a3d), C(0xffe0455a),
                         { 0xff5a1e6e, 0xffe0455a, 0xffff7a3d, 0xffffb547, 0xffff3b3b }) },

        { "Arctic", make(C(0xff070b10), C(0xff0d141c), C(0xff16212c), C(0x16d6ecff), C(0xff6f8496), C(0xffd6e6f2), C(0xff7fd8ff),
                         { 0xff1e3a8a, 0xff2563eb, 0xff38bdf8, 0xff7dd3fc, 0xffbae6fd, 0xfff0f9ff }, C(0x59f0f9ff),
                         C(0xff2563eb), C(0xff38bdf8), C(0xffe0f7ff), C(0xff7fd8ff), C(0xffc7d2fe),
                         { 0xff1e3a8a, 0xff2563eb, 0xff38bdf8, 0xffbae6fd, 0xffff5a5f }) },

        { "Forest", make(C(0xff070c09), C(0xff0e1610), C(0xff17231a), C(0x16e2ffe6), C(0xff6f8a76), C(0xffd5ead9), C(0xff5ee08a),
                         { 0xff14532d, 0xff15803d, 0xff22c55e, 0xffa3e635, 0xfffacc15, 0xfffde68a }, C(0x59f0fff2),
                         C(0xff16a34a), C(0xffa3e635), C(0xfffde047), C(0xff5ee08a), C(0xfffacc15),
                         { 0xff14532d, 0xff22c55e, 0xffa3e635, 0xfffacc15, 0xffff5a5f }) },

        { "Studio Mono", make(C(0xff0a0a0a), C(0xff121212), C(0xff1e1e1e), C(0x16ffffff), C(0xff7a7a7a), C(0xffdcdcdc), C(0xffffffff),
                              { 0xff4a4a4a, 0xff6e6e6e, 0xff9a9a9a, 0xffc4c4c4, 0xffe4e4e4, 0xffffffff }, C(0x59ffffff),
                              C(0xff8a8a8a), C(0xffc8c8c8), C(0xffffffff), C(0xffe0e0e0), C(0xff9a9a9a),
                              { 0xff5a5a5a, 0xff8a8a8a, 0xffc4c4c4, 0xffe8e8e8, 0xffff5a5f }, 0.45f, 0.6f) },
    };
    return list;
}

Colours presetNamed(const juce::String& name)
{
    for (const auto& preset : presets())
        if (preset.name == name)
            return preset.colours;
    return presets().front().colours;
}

Colours current()
{
    Colours c;
    c.background = background;
    c.panel = panel;
    c.controls = raised;
    c.grid = grid;
    c.labels = textDim;
    c.text = text;
    c.accent = accent;
    c.spectrum = spectrumStops;
    c.spectrumFill = spectrumFill;
    c.glow = glow;
    c.peakHold = peakHold;
    c.waveLow = waveLow;
    c.waveMid = waveMid;
    c.waveHigh = waveHigh;
    c.waveLeft = waveLeft;
    c.waveRight = waveRight;
    c.meter = meterStops;
    return c;
}

void apply(const Colours& c)
{
    background = c.background;
    panel = c.panel;
    raised = c.controls;
    outline = c.controls.brighter(0.18f);
    grid = c.grid;
    gridStrong = c.grid.withMultipliedAlpha(1.8f);
    textDim = c.labels;
    text = c.text;
    textBright = c.text.brighter(0.35f);
    accent = c.accent;
    accentDeep = c.accent.darker(0.6f);
    spectrumStops = c.spectrum;
    spectrumFill = juce::jlimit(0.0f, 1.0f, c.spectrumFill);
    glow = juce::jlimit(0.0f, 2.0f, c.glow);
    peakHold = c.peakHold;
    waveLow = c.waveLow;
    waveMid = c.waveMid;
    waveHigh = c.waveHigh;
    waveLeft = c.waveLeft;
    waveRight = c.waveRight;
    meterStops = c.meter;
}

juce::var toVar(const Colours& c)
{
    auto* object = new juce::DynamicObject();
    auto put = [object](const char* key, juce::Colour colour) { object->setProperty(key, colour.toString()); };

    put("background", c.background);
    put("panel", c.panel);
    put("controls", c.controls);
    put("grid", c.grid);
    put("labels", c.labels);
    put("text", c.text);
    put("accent", c.accent);
    for (size_t i = 0; i < c.spectrum.size(); ++i)
        put(kSpectrumKeys[i], c.spectrum[i]);
    object->setProperty("spectrumFill", c.spectrumFill);
    object->setProperty("glow", c.glow);
    put("peakHold", c.peakHold);
    put("waveLow", c.waveLow);
    put("waveMid", c.waveMid);
    put("waveHigh", c.waveHigh);
    put("waveLeft", c.waveLeft);
    put("waveRight", c.waveRight);
    for (size_t i = 0; i < c.meter.size(); ++i)
        put(kMeterKeys[i], c.meter[i]);
    return juce::var(object);
}

Colours fromVar(const juce::var& value, const Colours& fallback)
{
    Colours c = fallback;
    auto* object = value.getDynamicObject();
    if (object == nullptr)
        return c;

    auto read = [object](const char* key, juce::Colour& target)
    {
        if (object->hasProperty(key))
            target = juce::Colour::fromString(object->getProperty(key).toString());
    };

    read("background", c.background);
    read("panel", c.panel);
    read("controls", c.controls);
    read("grid", c.grid);
    read("labels", c.labels);
    read("text", c.text);
    read("accent", c.accent);
    for (size_t i = 0; i < c.spectrum.size(); ++i)
        read(kSpectrumKeys[i], c.spectrum[i]);
    if (object->hasProperty("spectrumFill"))
        c.spectrumFill = (float) (double) object->getProperty("spectrumFill");
    if (object->hasProperty("glow"))
        c.glow = (float) (double) object->getProperty("glow");
    read("peakHold", c.peakHold);
    read("waveLow", c.waveLow);
    read("waveMid", c.waveMid);
    read("waveHigh", c.waveHigh);
    read("waveLeft", c.waveLeft);
    read("waveRight", c.waveRight);
    for (size_t i = 0; i < c.meter.size(); ++i)
        read(kMeterKeys[i], c.meter[i]);
    return c;
}

} // namespace sonarc::theme
