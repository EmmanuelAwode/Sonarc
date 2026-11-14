#include "plugin/Displays.h"

#include "dsp/Biquad.h"

#include <complex>

namespace driftwave {

namespace {
juce::String hertzText(float hz)
{
    return hz >= 1000.0f ? juce::String(hz / 1000.0f, 1) + " kHz" : juce::String(juce::roundToInt(hz)) + " Hz";
}
}

// ---------------------------------------------------------------------------

juce::Rectangle<float> DisplayWell::paintWell(juce::Graphics& g) const
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(palette.background);
    g.fillRoundedRectangle(bounds, 4.0f);
    // A hairline that is darker at the top than the bottom reads as a recess.
    g.setGradientFill(juce::ColourGradient(palette.background.darker(0.6f), 0.0f, bounds.getY(), palette.border, 0.0f, bounds.getBottom(), false));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    return bounds.reduced(9.0f, 6.0f);
}

void DisplayWell::caption(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& left, const juce::String& right,
                          juce::Colour rightColour) const
{
    const auto row = area.withHeight(14.0f);
    g.setFont(uiFont(type::small));
    g.setColour(palette.textDim);
    g.drawText(left, row, juce::Justification::centredLeft, false);
    g.setColour(rightColour);
    g.drawText(right, row, juce::Justification::centredRight, false);
}

// ---------------------------------------------------------------------------

ScopeDisplay::ScopeDisplay(DriftwaveProcessor& p, const Palette& pal) : DisplayWell(p, pal) {}

void ScopeDisplay::feed(const float* samples, int count, double sampleRate)
{
    const double perColumn = juce::jmax(1.0, sampleRate * kSecondsShown / double(kColumns));
    for (int i = 0; i < count; ++i) {
        accLow = juce::jmin(accLow, samples[i]);
        accHigh = juce::jmax(accHigh, samples[i]);
        if (++accCount >= perColumn) {
            low[head] = accLow;
            high[head] = accHigh;
            head = (head + 1) % kColumns;
            accLow = accHigh = 0.0f;
            accCount -= perColumn;
        }
    }
}

void ScopeDisplay::advance()
{
    lag = processor.speedLagSeconds();
    repaint();
}

void ScopeDisplay::paint(juce::Graphics& g)
{
    auto area = paintWell(g);
    const bool on = param(id::speedOn) > 0.5f;
    const float speed = param(id::speed);
    const bool synced = on && (param(id::speedSync) > 0.5f || speed > 100.0f);
    const bool behind = lag >= 0.05f;

    // Left: what the speed does to the pitch. Right: where the output is relative to the song.
    juce::String left = "Off";
    if (on) {
        const float semitones = 12.0f * std::log2(juce::jmax(speed, 1.0f) / 100.0f);
        left = std::abs(semitones) < 0.005f ? juce::String("No pitch change")
                                            : juce::String(semitones > 0.0f ? "+" : "") + juce::String(semitones, 2) + " st"
                                                  + (speed < 100.0f ? "  slower" : "  faster");
    }
    caption(g, area, left, behind ? juce::String(lag, 1) + " s behind the song" : synced ? juce::String("In sync") : juce::String("Live"),
            behind ? palette.accent : palette.textDim);
    area.removeFromTop(16.0f);

    const float mid = area.getCentreY(), half = area.getHeight() * 0.5f;
    g.setColour(palette.border.withAlpha(0.7f));
    g.fillRect(area.getX(), std::floor(mid), area.getWidth(), 1.0f);

    // The last few seconds of output, newest on the right.
    juce::Path top, bottom;
    const float step = area.getWidth() / float(kColumns - 1);
    for (int i = 0; i < kColumns; ++i) {
        const int column = (head + i) % kColumns;
        const float x = area.getX() + float(i) * step;
        const float yTop = mid - juce::jlimit(0.0f, 1.0f, high[column]) * half - 0.5f;
        const float yBottom = mid - juce::jlimit(-1.0f, 0.0f, low[column]) * half + 0.5f;
        if (i == 0) {
            top.startNewSubPath(x, yTop);
            bottom.startNewSubPath(x, yBottom);
        } else {
            top.lineTo(x, yTop);
            bottom.lineTo(x, yBottom);
        }
    }
    juce::Path shape = top;
    for (int i = kColumns - 1; i >= 0; --i) {
        const int column = (head + i) % kColumns;
        shape.lineTo(area.getX() + float(i) * step, mid - juce::jlimit(-1.0f, 0.0f, low[column]) * half + 0.5f);
    }
    shape.closeSubPath();
    // Brighter towards "now".
    g.setGradientFill(juce::ColourGradient(palette.accent.withAlpha(0.25f), area.getX(), 0.0f, palette.accent.withAlpha(0.9f), area.getRight(), 0.0f, false));
    g.fillPath(shape);
}

// ---------------------------------------------------------------------------

TailDisplay::TailDisplay(DriftwaveProcessor& p, const Palette& pal) : DisplayWell(p, pal)
{
    shownDecay = param(id::reverbDecay);
    shownMix = param(id::reverbMix) * 0.01f;
}

void TailDisplay::advance()
{
    // Measured reverb level, -60..0 dB onto 0..1, with a quick rise and a slow fall.
    const float db = juce::Decibels::gainToDecibels(processor.takeWetPeak(), -100.0f);
    const float target = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
    level = target > level ? level + (target - level) * 0.6f : juce::jmax(target, level - 0.035f);

    // The drawn shape eases towards the controls, so preset changes glide.
    shownDecay += (param(id::reverbDecay) - shownDecay) * 0.35f;
    shownMix += (param(id::reverbMix) * 0.01f - shownMix) * 0.35f;
    repaint();
}

void TailDisplay::paint(juce::Graphics& g)
{
    auto area = paintWell(g);
    const bool on = param(id::reverbOn) > 0.5f;
    const float lowCut = param(id::reverbLowCut), highCut = param(id::reverbHighCut);
    const bool filtered = lowCut > 20.5f || highCut < 19990.0f;

    caption(g, area, filtered ? hertzText(lowCut) + " - " + hertzText(highCut) : juce::String("Full range"),
            juce::String(shownDecay, 1) + " s", on ? palette.text : palette.textDim);
    area.removeFromTop(16.0f);
    auto axis = area.removeFromBottom(11.0f);

    // Time runs left to right, one mark per second.
    const float secondsShown = 5.5f;
    const auto xFor = [&](float seconds) { return area.getX() + area.getWidth() * seconds / secondsShown; };
    g.setFont(uiFont(9.5f));
    for (int second = 1; second <= 5; ++second) {
        const float x = std::floor(xFor(float(second)));
        g.setColour(palette.border.withAlpha(0.6f));
        g.fillRect(x, area.getY(), 1.0f, area.getHeight());
        g.setColour(palette.textDim.withAlpha(0.8f));
        g.drawText(juce::String(second) + "s", juce::Rectangle<float>(x - 12.0f, axis.getY(), 24.0f, axis.getHeight()), juce::Justification::centred, false);
    }

    const float mid = area.getCentreY(), half = area.getHeight() * 0.5f - 1.0f;
    g.setColour(palette.border.withAlpha(0.7f));
    g.fillRect(area.getX(), std::floor(mid), area.getWidth(), 1.0f);

    // The tail the controls describe: starts 10 ms in, falls 60 dB in the decay time,
    // as tall as the mix lets it be. (Height follows level with a display curve, like a meter.)
    const float wetGain = std::sin(juce::jlimit(0.0f, 1.0f, shownMix) * juce::MathConstants<float>::halfPi);
    const auto shape = [&](float scale) {
        juce::Path upper, path;
        const int steps = 64;
        std::vector<juce::Point<float>> points;
        for (int i = 0; i <= steps; ++i) {
            const float t = 0.010f + shownDecay * float(i) / float(steps);
            const float height = std::pow(std::pow(10.0f, -3.0f * (t - 0.010f) / shownDecay), 0.35f) * wetGain * scale * half;
            points.push_back({ xFor(t), height });
        }
        path.startNewSubPath(points.front().x, mid);
        for (const auto& p : points) path.lineTo(p.x, mid - p.y);
        for (auto it = points.rbegin(); it != points.rend(); ++it) path.lineTo(it->x, mid + it->y);
        path.closeSubPath();
        return path;
    };

    const juce::Colour tint = on ? palette.cool : palette.textDim;
    const auto outline = shape(1.0f);
    if (on) {
        // Lit by the reverb that is actually sounding.
        g.setGradientFill(juce::ColourGradient(tint.withAlpha(0.10f + 0.45f * level), xFor(0.0f), 0.0f,
                                               tint.withAlpha(0.03f + 0.10f * level), xFor(shownDecay), 0.0f, false));
        g.fillPath(outline);
        if (filtered) {
            // The share of the audible range (in octaves) that the two cuts leave in the tail.
            const float share = juce::jlimit(0.05f, 1.0f, std::log(highCut / lowCut) / std::log(1000.0f));
            g.setColour(tint.withAlpha(0.18f + 0.25f * level));
            g.fillPath(shape(share));
        }
    }
    g.setColour(tint.withAlpha(on ? 0.9f : 0.45f));
    g.strokePath(outline, juce::PathStrokeType(1.2f));
}

// ---------------------------------------------------------------------------

SpectrumDisplay::SpectrumDisplay(DriftwaveProcessor& p, const Palette& pal) : DisplayWell(p, pal)
{
    fft.prepare(kFftSize);
    window.resize(size_t(kFftSize));
    for (int i = 0; i < kFftSize; ++i)
        window[size_t(i)] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * float(i) / float(kFftSize));
    inRing.assign(size_t(kFftSize), 0.0f);
    outRing.assign(size_t(kFftSize), 0.0f);
    re.assign(size_t(kFftSize), 0.0f);
    im.assign(size_t(kFftSize), 0.0f);
    std::fill(std::begin(inDb), std::end(inDb), kBottomDb);
    std::fill(std::begin(outDb), std::end(outDb), kBottomDb);
    shownBass = param(id::bassOn) > 0.5f ? param(id::bassGain) : 0.0f;
    shownFreq = param(id::bassFreq);
    shownDrive = param(id::bassOn) > 0.5f ? param(id::bassDrive) * 0.01f : 0.0f;
}

void SpectrumDisplay::feed(const float* input, int inputCount, const float* output, int outputCount)
{
    const auto append = [](std::vector<float>& ring, const float* samples, int count) {
        const int n = int(ring.size());
        if (count >= n) {
            std::copy(samples + count - n, samples + count, ring.begin());
        } else if (count > 0) {
            std::copy(ring.begin() + count, ring.end(), ring.begin());
            std::copy(samples, samples + count, ring.end() - count);
        }
    };
    append(inRing, input, inputCount);
    append(outRing, output, outputCount);
    haveData = haveData || inputCount > 0 || outputCount > 0;
}

void SpectrumDisplay::analyse(const std::vector<float>& signal, float* smoothed)
{
    for (int i = 0; i < kFftSize; ++i) {
        re[size_t(i)] = signal[size_t(i)] * window[size_t(i)];
        im[size_t(i)] = 0.0f;
    }
    fft.forward(re.data(), im.data());

    const auto sampleRate = float(juce::jmax(8000.0, processor.getSampleRate()));
    const float binHz = sampleRate / float(kFftSize);
    const auto power = [&](int bin) {
        bin = juce::jlimit(1, kFftSize / 2 - 1, bin);
        return re[size_t(bin)] * re[size_t(bin)] + im[size_t(bin)] * im[size_t(bin)];
    };
    const float scale = 4.0f / float(kFftSize); // undoes the FFT size and the window's level

    for (int k = 0; k < kPoints; ++k) {
        const float lowHz = kMinHz * std::pow(kMaxHz / kMinHz, float(k) / float(kPoints));
        const float highHz = kMinHz * std::pow(kMaxHz / kMinHz, float(k + 1) / float(kPoints));
        const float lowBin = lowHz / binHz, highBin = highHz / binHz;
        float p;
        if (int(highBin) > int(lowBin)) { // several bins fall in this slice: take the strongest
            p = 0.0f;
            for (int bin = int(lowBin) + 1; bin <= int(highBin); ++bin)
                p = juce::jmax(p, power(bin));
        } else { // slices narrower than a bin: read between the two nearest
            const float centre = 0.5f * (lowBin + highBin);
            const int below = int(centre);
            const float t = centre - float(below);
            p = power(below) * (1.0f - t) + power(below + 1) * t;
        }
        // Tilted by 3 dB per octave around 1 kHz, the usual way to make music read level instead of falling to the right.
        const float centreHz = std::sqrt(lowHz * highHz);
        const float db = 10.0f * std::log10(p * scale * scale + 1.0e-12f) + 3.0f * std::log2(centreHz / 1000.0f);
        smoothed[k] = db > smoothed[k] ? smoothed[k] + (db - smoothed[k]) * 0.6f : juce::jmax(db, smoothed[k] - 2.0f);
        smoothed[k] = juce::jmax(smoothed[k], kBottomDb);
    }
}

void SpectrumDisplay::advance()
{
    if (haveData) {
        analyse(inRing, inDb);
        analyse(outRing, outDb);
    }
    const bool bassOn = param(id::bassOn) > 0.5f;
    shownBass += ((bassOn ? param(id::bassGain) : 0.0f) - shownBass) * 0.35f;
    shownFreq += (param(id::bassFreq) - shownFreq) * 0.35f;
    shownDrive += ((bassOn ? param(id::bassDrive) * 0.01f : 0.0f) - shownDrive) * 0.35f;
    repaint();
}

float SpectrumDisplay::xFor(float hz, juce::Rectangle<float> area) const
{
    return area.getX() + area.getWidth() * std::log(juce::jlimit(kMinHz, kMaxHz, hz) / kMinHz) / std::log(kMaxHz / kMinHz);
}

juce::Path SpectrumDisplay::curve(const float* values, juce::Rectangle<float> area) const
{
    juce::Path path;
    for (int k = 0; k < kPoints; ++k) {
        const float x = area.getX() + area.getWidth() * (float(k) + 0.5f) / float(kPoints);
        const float y = area.getY() + area.getHeight() * juce::jlimit(0.0f, 1.0f, (kTopDb - values[k]) / (kTopDb - kBottomDb));
        if (k == 0)
            path.startNewSubPath(area.getX(), y);
        path.lineTo(x, y);
        if (k == kPoints - 1)
            path.lineTo(area.getRight(), y);
    }
    return path.createPathWithRoundedCorners(3.0f);
}

void SpectrumDisplay::paint(juce::Graphics& g)
{
    auto area = paintWell(g);

    // Caption with a small key.
    caption(g, area, "Spectrum", "", palette.textDim);
    {
        g.setFont(uiFont(type::small));
        float x = area.getRight();
        const auto key = [&](const juce::String& name, float textWidth, juce::Colour colour) {
            x -= textWidth;
            g.setColour(palette.textDim);
            g.drawText(name, juce::Rectangle<float>(x, area.getY(), textWidth, 14.0f), juce::Justification::centredRight, false);
            x -= 15.0f;
            g.setColour(colour);
            g.fillRect(x, area.getY() + 7.0f, 10.0f, 1.5f);
            x -= 12.0f;
        };
        key("Out", 18.0f, palette.accent);
        key("In", 11.0f, palette.signal.withAlpha(0.7f));
    }
    area.removeFromTop(16.0f);
    auto axis = area.removeFromBottom(11.0f);

    // Frequency grid, logarithmic.
    g.setFont(uiFont(9.5f));
    for (const auto& [hz, name] : { std::pair<float, const char*> { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } }) {
        const float x = std::floor(xFor(hz, area));
        g.setColour(palette.border.withAlpha(0.6f));
        g.fillRect(x, area.getY(), 1.0f, area.getHeight());
        g.setColour(palette.textDim.withAlpha(0.8f));
        g.drawText(name, juce::Rectangle<float>(x - 14.0f, axis.getY(), 28.0f, axis.getHeight()), juce::Justification::centred, false);
    }

    g.saveState();
    g.reduceClipRegion(area.toNearestInt());

    // The band the bass drive works on (below 1.5x the bass frequency), shaded by how much drive there is.
    if (shownDrive > 0.01f) {
        g.setColour(palette.accent.withAlpha(0.14f * shownDrive));
        g.fillRect(area.withRight(xFor(shownFreq * 1.5f, area)));
    }

    // The range the reverb is allowed to work in, as a bar along the top.
    if (param(id::reverbOn) > 0.5f) {
        const float from = xFor(param(id::reverbLowCut), area), to = xFor(param(id::reverbHighCut), area);
        g.setColour(palette.cool.withAlpha(0.75f));
        g.fillRect(from, area.getY(), juce::jmax(2.0f, to - from), 2.0f);
    }

    // Input as a thin grey line, output as the filled amber curve.
    g.setColour(palette.signal.withAlpha(0.5f));
    g.strokePath(curve(inDb, area), juce::PathStrokeType(1.0f));

    const auto outPath = curve(outDb, area);
    juce::Path fill = outPath;
    fill.lineTo(area.getRight(), area.getBottom());
    fill.lineTo(area.getX(), area.getBottom());
    fill.closeSubPath();
    g.setGradientFill(juce::ColourGradient(palette.accent.withAlpha(0.32f), 0.0f, area.getY(), palette.accent.withAlpha(0.02f), 0.0f, area.getBottom(), false));
    g.fillPath(fill);
    g.setColour(palette.accent);
    g.strokePath(outPath, juce::PathStrokeType(1.4f));

    // The bass shelf's own response, computed from the same filter the audio goes through.
    if (shownBass > 0.05f) {
        const double sampleRate = juce::jmax(8000.0, processor.getSampleRate());
        const auto c = makeLowShelf(sampleRate, shownFreq, shownBass);
        const float zero = area.getY() + area.getHeight() * 0.72f, perDb = area.getHeight() * 0.55f / 18.0f;
        juce::Path response;
        for (int i = 0; i <= 64; ++i) {
            const float hz = kMinHz * std::pow(kMaxHz / kMinHz, float(i) / 64.0f);
            const auto z = std::polar(1.0, -2.0 * kPi * double(hz) / sampleRate);
            const auto h = (double(c.b0) + double(c.b1) * z + double(c.b2) * z * z) / (1.0 + double(c.a1) * z + double(c.a2) * z * z);
            const float y = zero - float(20.0 * std::log10(std::abs(h) + 1e-9)) * perDb;
            if (i == 0)
                response.startNewSubPath(xFor(hz, area), y);
            else
                response.lineTo(xFor(hz, area), y);
        }
        g.setColour(palette.text.withAlpha(0.8f));
        g.strokePath(response, juce::PathStrokeType(1.2f));
        g.setFont(uiFont(9.5f));
        g.drawText("+" + juce::String(shownBass, 1) + " dB", juce::Rectangle<float>(area.getX() + 2.0f, zero - shownBass * perDb - 13.0f, 60.0f, 12.0f),
                   juce::Justification::centredLeft, false);
    }
    g.restoreState();
}

// ---------------------------------------------------------------------------

StageDisplay::StageDisplay(DriftwaveProcessor& p, const Palette& pal) : DisplayWell(p, pal)
{
    fft.prepare(kFftSize);
    window.resize(size_t(kFftSize));
    for (int i = 0; i < kFftSize; ++i)
        window[size_t(i)] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * float(i) / float(kFftSize));
    ring.assign(size_t(kFftSize), 0.0f);
    re.assign(size_t(kFftSize), 0.0f);
    im.assign(size_t(kFftSize), 0.0f);

    style = juce::jlimit(0, int(styleCount) - 1, int(processor.apvts.state.getProperty("visualiserStyle", 0)));
    setInterceptsMouseClicks(true, false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void StageDisplay::mouseDown(const juce::MouseEvent&)
{
    style = (style + 1) % int(styleCount);
    processor.apvts.state.setProperty("visualiserStyle", style, nullptr); // saved with the project
    repaint();
}

void StageDisplay::feed(const float* samples, int count, double sampleRate)
{
    const int n = int(ring.size());
    if (count >= n) {
        std::copy(samples + count - n, samples + count, ring.begin());
    } else if (count > 0) {
        std::copy(ring.begin() + count, ring.end(), ring.begin());
        std::copy(samples, samples + count, ring.end() - count);
    }
    haveData = haveData || count > 0;

    // The ribbon shows six seconds at 100%. Slowed down, the same width holds less music,
    // so it visibly moves slower; sped up, faster.
    const float speed = param(id::speedOn) > 0.5f ? juce::jlimit(50.0f, 150.0f, param(id::speed)) * 0.01f : 1.0f;
    const double perColumn = juce::jmax(1.0, sampleRate * 6.0 / double(kColumns) / double(speed));
    for (int i = 0; i < count; ++i) {
        accLow = juce::jmin(accLow, samples[i]);
        accHigh = juce::jmax(accHigh, samples[i]);
        if (++accCount >= perColumn) {
            low[head] = accLow;
            high[head] = accHigh;
            head = (head + 1) % kColumns;
            accLow = accHigh = 0.0f;
            accCount -= perColumn;
        }
    }
}

void StageDisplay::advance()
{
    if (haveData) {
        for (int i = 0; i < kFftSize; ++i) {
            re[size_t(i)] = ring[size_t(i)] * window[size_t(i)];
            im[size_t(i)] = 0.0f;
        }
        fft.forward(re.data(), im.data());

        const auto sampleRate = float(juce::jmax(8000.0, processor.getSampleRate()));
        const float binHz = sampleRate / float(kFftSize), scale = 4.0f / float(kFftSize);
        const auto power = [&](int bin) {
            bin = juce::jlimit(1, kFftSize / 2 - 1, bin);
            return re[size_t(bin)] * re[size_t(bin)] + im[size_t(bin)] * im[size_t(bin)];
        };
        // The trail falls at the reverb's own rate: 60 dB in the decay time.
        const bool reverbOn = param(id::reverbOn) > 0.5f;
        const float trailFall = 60.0f / kRangeDb / (juce::jmax(0.5f, param(id::reverbDecay)) * 60.0f); // per frame, at 60 a second

        for (int k = 0; k < kBars; ++k) {
            const float lowHz = kMinHz * std::pow(kMaxHz / kMinHz, float(k) / float(kBars));
            const float highHz = kMinHz * std::pow(kMaxHz / kMinHz, float(k + 1) / float(kBars));
            const float lowBin = lowHz / binHz, highBin = highHz / binHz;
            float p;
            if (int(highBin) > int(lowBin)) {
                p = 0.0f;
                for (int bin = int(lowBin) + 1; bin <= int(highBin); ++bin)
                    p = juce::jmax(p, power(bin));
            } else {
                const float centre = 0.5f * (lowBin + highBin);
                const int below = int(centre);
                const float t = centre - float(below);
                p = power(below) * (1.0f - t) + power(below + 1) * t;
            }
            // Tilted by 3 dB per octave so music fills the width instead of leaning left.
            const float db = 10.0f * std::log10(p * scale * scale + 1.0e-12f) + 3.0f * std::log2(std::sqrt(lowHz * highHz) / 1000.0f);
            const float target = juce::jlimit(0.0f, 1.0f, (db + kRangeDb + 6.0f) / kRangeDb);
            bars[k] = target > bars[k] ? bars[k] + (target - bars[k]) * 0.45f : juce::jmax(target, bars[k] - 0.022f);
            trail[k] = reverbOn ? juce::jmax(bars[k], trail[k] - trailFall) : bars[k];
        }

        // How hard the low end is hitting (below about 150 Hz), for the circle's pulse.
        float lows = 0.0f;
        const int lowBars = kBars / 4;
        for (int k = 0; k < lowBars; ++k)
            lows = juce::jmax(lows, bars[k]);
        bassLevel += (lows - bassLevel) * (lows > bassLevel ? 0.5f : 0.12f);

        // The ridges are the spectrum at earlier moments; together they reach back as far as
        // the reverb rings (one second with the reverb off).
        const float reach = reverbOn ? juce::jmax(0.5f, param(id::reverbDecay)) : 1.0f;
        ridgeClock += float(kRidges) / (reach * 60.0f);
        if (ridgeClock >= 1.0f) {
            ridgeClock -= 1.0f;
            for (int r = kRidges - 1; r > 0; --r)
                std::copy(std::begin(ridges[r - 1]), std::end(ridges[r - 1]), std::begin(ridges[r]));
            std::copy(std::begin(bars), std::end(bars), std::begin(ridges[0]));
        }
    }
    repaint();
}

juce::Colour StageDisplay::barColour(int bar) const
{
    // Bars the bass boost is lifting are drawn brighter, by how much it lifts them.
    const float lift = param(id::bassOn) > 0.5f ? juce::jlimit(0.0f, 1.0f, param(id::bassGain) / 12.0f) : 0.0f;
    const float centreHz = kMinHz * std::pow(kMaxHz / kMinHz, (float(bar) + 0.5f) / float(kBars));
    return palette.accent.interpolatedWith(juce::Colours::white, centreHz < param(id::bassFreq) * 1.5f ? 0.45f * lift : 0.0f);
}

float StageDisplay::trailStrength() const
{
    if (param(id::reverbOn) < 0.5f)
        return 0.0f;
    return std::sin(juce::jlimit(0.0f, 1.0f, param(id::reverbMix) * 0.01f) * juce::MathConstants<float>::halfPi);
}

void StageDisplay::paintRibbon(juce::Graphics& g, juce::Rectangle<float> ribbon, float strength) const
{
    // The output waveform, newest on the right.
    const float mid = ribbon.getCentreY(), half = ribbon.getHeight() * 0.5f;
    g.setColour(palette.border.withAlpha(0.7f));
    g.fillRect(ribbon.getX(), std::floor(mid), ribbon.getWidth(), 1.0f);
    const float step = ribbon.getWidth() / float(kColumns - 1);
    juce::Path shape;
    for (int i = 0; i < kColumns; ++i) {
        const float y = mid - juce::jlimit(0.0f, 1.0f, high[(head + i) % kColumns]) * half - 0.5f;
        if (i == 0)
            shape.startNewSubPath(ribbon.getX(), y);
        else
            shape.lineTo(ribbon.getX() + float(i) * step, y);
    }
    for (int i = kColumns - 1; i >= 0; --i)
        shape.lineTo(ribbon.getX() + float(i) * step, mid - juce::jlimit(-1.0f, 0.0f, low[(head + i) % kColumns]) * half + 0.5f);
    shape.closeSubPath();
    g.setGradientFill(juce::ColourGradient(palette.accent.withAlpha(0.12f * strength), ribbon.getX(), 0.0f,
                                           palette.accent.withAlpha(0.75f * strength), ribbon.getRight(), 0.0f, false));
    g.fillPath(shape);
}

void StageDisplay::paintBars(juce::Graphics& g, juce::Rectangle<float> area, bool mirrored) const
{
    // Standing on a line with a short reflection under it, or (mirrored) growing both ways from the middle.
    const float floorY = std::floor(mirrored ? area.getCentreY() : area.getY() + area.getHeight() * 0.78f);
    const float tall = floorY - area.getY(), deep = area.getBottom() - floorY;
    const float slot = area.getWidth() / float(kBars), barWidth = juce::jmax(2.0f, slot - 3.0f);
    const auto xFor = [&](int k) { return area.getX() + (float(k) + 0.5f) * slot; };

    // The reverb's trail: where the bars have been, fading at the rate the tail does.
    const float strength = trailStrength();
    if (strength > 0.005f) {
        for (int side = 0; side < (mirrored ? 2 : 1); ++side) {
            const float sign = side == 0 ? -1.0f : 1.0f;
            juce::Path edge;
            edge.startNewSubPath(area.getX(), floorY + sign * trail[0] * tall);
            for (int k = 0; k < kBars; ++k)
                edge.lineTo(xFor(k), floorY + sign * trail[k] * tall);
            edge.lineTo(area.getRight(), floorY + sign * trail[kBars - 1] * tall);
            edge = edge.createPathWithRoundedCorners(slot);
            juce::Path fill = edge;
            fill.lineTo(area.getRight(), floorY);
            fill.lineTo(area.getX(), floorY);
            fill.closeSubPath();
            g.setGradientFill(juce::ColourGradient(palette.cool.withAlpha(0.30f * strength), 0.0f, floorY + sign * tall, palette.cool.withAlpha(0.03f), 0.0f, floorY, false));
            g.fillPath(fill);
            g.setColour(palette.cool.withAlpha(0.25f + 0.55f * strength));
            g.strokePath(edge, juce::PathStrokeType(1.2f));
        }
    }

    for (int k = 0; k < kBars; ++k) {
        const float height = juce::jmax(1.0f, bars[k] * tall);
        const float x = std::floor(xFor(k) - barWidth * 0.5f);
        const auto colour = barColour(k);
        const float corner = juce::jmin(1.5f, barWidth * 0.5f);
        if (mirrored) {
            g.setGradientFill(juce::ColourGradient(colour, 0.0f, floorY - height, colour.withAlpha(0.55f), 0.0f, floorY, false));
            g.fillRoundedRectangle(x, floorY - height, barWidth, height, corner);
            g.setGradientFill(juce::ColourGradient(colour.withAlpha(0.55f), 0.0f, floorY, colour, 0.0f, floorY + height, false));
            g.fillRoundedRectangle(x, floorY, barWidth, height, corner);
        } else {
            g.setGradientFill(juce::ColourGradient(colour, 0.0f, floorY - height, colour.withAlpha(0.55f), 0.0f, floorY, false));
            g.fillRoundedRectangle(x, floorY - height, barWidth, height, corner);
            const float reflection = juce::jmin(deep, height * 0.35f);
            g.setGradientFill(juce::ColourGradient(colour.withAlpha(0.22f), 0.0f, floorY + 2.0f, colour.withAlpha(0.0f), 0.0f, floorY + 2.0f + reflection, false));
            g.fillRect(x, floorY + 2.0f, barWidth, reflection);
        }
    }
    g.setColour(mirrored ? palette.background : palette.border);
    g.fillRect(area.getX(), floorY - (mirrored ? 0.5f : 0.0f), area.getWidth(), 1.0f);
}

void StageDisplay::paintCircle(juce::Graphics& g, juce::Rectangle<float> area) const
{
    // The waveform runs across the whole width behind a ring of spectrum spokes. The ring
    // swells with the low end; the trail is the outline around the spokes.
    paintRibbon(g, area.withSizeKeepingCentre(area.getWidth(), area.getHeight() * 0.42f), 0.7f);

    const auto centre = area.getCentre();
    const float reach = area.getHeight() * 0.5f;
    const float radius = reach * (0.36f + 0.07f * bassLevel), longest = reach - radius - 2.0f;
    const auto pointAt = [&](int k, bool left, float distance) {
        // Low notes at the top, high at the bottom, the same on both sides.
        const float angle = juce::MathConstants<float>::pi * (float(k) + 0.5f) / float(kBars);
        return centre.getPointOnCircumference(distance, left ? -angle : angle);
    };

    g.setColour(palette.background);
    g.fillEllipse(juce::Rectangle<float>(2.0f * (radius + longest + 4.0f), 2.0f * (radius + longest + 4.0f)).withCentre(centre));

    const float strength = trailStrength();
    if (strength > 0.005f) {
        juce::Path edge;
        for (int k = 0; k < kBars; ++k) {
            const auto p = pointAt(k, false, radius + 2.0f + trail[k] * longest);
            if (k == 0)
                edge.startNewSubPath(p);
            else
                edge.lineTo(p);
        }
        for (int k = kBars - 1; k >= 0; --k)
            edge.lineTo(pointAt(k, true, radius + 2.0f + trail[k] * longest));
        edge.closeSubPath();
        g.setColour(palette.cool.withAlpha(0.12f * strength));
        g.fillPath(edge);
        g.setColour(palette.cool.withAlpha(0.25f + 0.55f * strength));
        g.strokePath(edge, juce::PathStrokeType(1.2f));
    }

    const float spoke = juce::jlimit(1.5f, 4.0f, juce::MathConstants<float>::pi * radius / float(kBars) * 0.7f);
    for (int k = 0; k < kBars; ++k) {
        g.setColour(barColour(k));
        for (const bool left : { false, true }) {
            juce::Path line;
            line.startNewSubPath(pointAt(k, left, radius + 2.0f));
            line.lineTo(pointAt(k, left, radius + 2.0f + juce::jmax(1.0f, bars[k] * longest)));
            g.strokePath(line, juce::PathStrokeType(spoke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // The disc in the middle, lit a little by the low end.
    const auto disc = juce::Rectangle<float>(2.0f * radius - 4.0f, 2.0f * radius - 4.0f).withCentre(centre);
    g.setColour(palette.surface.interpolatedWith(palette.accent, 0.10f + 0.25f * bassLevel));
    g.fillEllipse(disc);
    g.setColour(palette.accent.withAlpha(0.5f + 0.5f * bassLevel));
    g.drawEllipse(disc, 1.2f);
}

void StageDisplay::paintRidges(juce::Graphics& g, juce::Rectangle<float> area) const
{
    // The spectrum now at the front, and at earlier moments behind it: each line further back
    // is older, smaller and cooler. With the reverb on they reach back as far as it rings.
    const float cool = trailStrength();
    const float rise = area.getHeight() * 0.52f / float(kRidges), tall = area.getHeight() * 0.46f;
    for (int r = kRidges - 1; r >= -1; --r) {
        const float* values = r < 0 ? bars : ridges[r];
        const float depth = float(r + 1) / float(kRidges); // 0 at the front
        const float baseline = area.getBottom() - 2.0f - float(r + 1) * rise;
        const float inset = area.getWidth() * 0.16f * depth;
        const float left = area.getX() + inset, width = area.getWidth() - 2.0f * inset;
        const float height = tall * (1.0f - 0.45f * depth);

        juce::Path edge;
        edge.startNewSubPath(left, baseline - values[0] * height);
        for (int k = 0; k < kBars; ++k)
            edge.lineTo(left + width * (float(k) + 0.5f) / float(kBars), baseline - values[k] * height);
        edge.lineTo(left + width, baseline - values[kBars - 1] * height);
        edge = edge.createPathWithRoundedCorners(width / float(kBars));
        juce::Path fill = edge;
        fill.lineTo(left + width, baseline + 1.0f);
        fill.lineTo(left, baseline + 1.0f);
        fill.closeSubPath();
        g.setColour(palette.background); // hides the lines behind, like a hillside
        g.fillPath(fill);
        const auto colour = palette.accent.interpolatedWith(palette.cool, juce::jlimit(0.0f, 1.0f, depth * 1.4f) * cool);
        if (r < 0) {
            g.setGradientFill(juce::ColourGradient(colour.withAlpha(0.30f), 0.0f, baseline - height, colour.withAlpha(0.02f), 0.0f, baseline, false));
            g.fillPath(fill);
        }
        g.setColour(colour.withAlpha(r < 0 ? 1.0f : 0.75f * (1.0f - depth) + 0.08f));
        g.strokePath(edge, juce::PathStrokeType(r < 0 ? 1.6f : 1.1f));
    }
}

void StageDisplay::paint(juce::Graphics& g)
{
    auto area = paintWell(g).reduced(6.0f, 4.0f);

    const bool speedOn = param(id::speedOn) > 0.5f, reverbOn = param(id::reverbOn) > 0.5f, bassOn = param(id::bassOn) > 0.5f;
    const float speed = param(id::speed), mix = param(id::reverbMix) * 0.01f;

    // Top row: the preset's name, the style, and what is switched on.
    {
        const auto row = area.removeFromTop(22.0f);
        g.setColour(palette.text);
        g.setFont(uiFont(type::name, true));
        g.drawText(processor.apvts.state.getProperty("presetName", "Default").toString(), row, juce::Justification::centredLeft, true);

        juce::StringArray parts;
        if (speedOn && std::abs(speed - 100.0f) > 0.05f) {
            const float semitones = 12.0f * std::log2(speed / 100.0f);
            parts.add(juce::String(speed < 100.0f ? "Slowed " : "Sped up ") + juce::String(juce::roundToInt(speed)) + "%  ("
                      + (semitones > 0.0f ? "+" : "") + juce::String(semitones, 1) + " st)");
        }
        if (reverbOn && mix > 0.005f)
            parts.add("Reverb " + juce::String(juce::roundToInt(mix * 100.0f)) + "%, " + juce::String(param(id::reverbDecay), 1) + " s");
        if (bassOn && param(id::bassGain) > 0.05f)
            parts.add("Bass +" + juce::String(param(id::bassGain), 1) + " dB");
        g.setColour(palette.textDim);
        g.setFont(uiFont(type::section));
        g.drawText(parts.isEmpty() ? juce::String("No effects on") : parts.joinIntoString("     "), row, juce::Justification::centredRight, true);

        static const char* const names[] = { "Bars", "Mirror", "Circle", "Ridges" };
        g.setColour(palette.textDim.withAlpha(0.7f));
        g.setFont(uiFont(type::small));
        g.drawText(juce::String(names[style]) + "  -  click to change", row, juce::Justification::centred, false);
    }
    area.removeFromTop(6.0f);

    if (style == circleStyle) {
        paintCircle(g, area);
        return;
    }
    auto ribbon = area.removeFromBottom(juce::jmin(54.0f, area.getHeight() * 0.26f));
    area.removeFromBottom(8.0f);
    paintRibbon(g, ribbon, 1.0f);
    if (style == ridgesStyle)
        paintRidges(g, area);
    else
        paintBars(g, area, style == mirrorStyle);
}

// ---------------------------------------------------------------------------

DisplayBand::DisplayBand(DriftwaveProcessor& p, const Palette& pal)
    : processor(p), scope(p, pal), tail(p, pal), spectrum(p, pal), inBlock(size_t(1 << 14)), outBlock(size_t(1 << 14))
{
    addAndMakeVisible(scope);
    addAndMakeVisible(tail);
    addAndMakeVisible(spectrum);
}

DisplayBand::~DisplayBand()
{
    processor.setDisplaysActive(false);
}

void DisplayBand::updateTimer()
{
    // Nothing is measured, copied or drawn while the interface is not on screen.
    const bool showing = isShowing();
    processor.setDisplaysActive(showing);
    if (showing)
        startTimerHz(60);
    else
        stopTimer();
}

void DisplayBand::resized()
{
    const int pad = 10, gap = 5, top = 8, bottom = 6;
    const int h = getHeight() - top - bottom;
    scope.setBounds(pad, top, speedW - pad - gap, h);
    tail.setBounds(speedW + gap, top, reverbW - 2 * gap, h);
    spectrum.setBounds(speedW + reverbW + gap, top, getWidth() - speedW - reverbW - gap - pad, h);
}

void DisplayBand::timerCallback()
{
    const int inputCount = processor.inputFeed.pull(inBlock.data(), int(inBlock.size()));
    const int outputCount = processor.outputFeed.pull(outBlock.data(), int(outBlock.size()));
    scope.feed(outBlock.data(), outputCount, processor.getSampleRate());
    spectrum.feed(inBlock.data(), inputCount, outBlock.data(), outputCount);
    // The timer runs 60 times a second for the large visualiser; the three small
    // displays keep their 30, which is plenty at their size and halves their cost.
    if ((oddFrame = !oddFrame)) {
        scope.advance();
        tail.advance();
        spectrum.advance();
    }
    if (stage != nullptr && stage->isVisible()) {
        stage->feed(outBlock.data(), outputCount, processor.getSampleRate());
        stage->advance();
    }
}

} // namespace driftwave
