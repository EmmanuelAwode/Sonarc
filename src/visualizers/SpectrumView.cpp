#include "visualizers/SpectrumView.h"
#include "dsp/Decibels.h"
#include "ui/Theme.h"

#include <cmath>

namespace sonarc {

SpectrumView::SpectrumView()
{
    setOpaque(true);
}

void SpectrumView::setSpectrum(const AnalysisSnapshot& snapshot)
{
    const bool layoutChanged = snapshot.sampleRate != sampleRate || snapshot.fftSize != fftSize;
    if (! layoutChanged && snapshot.spectrumDb == spectrum && snapshot.spectrumPeakDb == peaks)
        return; // e.g. silence that has fully decayed: nothing to redraw

    sampleRate = snapshot.sampleRate;
    fftSize = snapshot.fftSize;
    spectrum = snapshot.spectrumDb;
    peaks = snapshot.spectrumPeakDb;

    if (layoutChanged)
    {
        rebuildColumns();
        background = {};
    }

    // ~8 updates per second: fast enough to follow, slow enough to read.
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now - lastPeakTextMs > 125.0)
    {
        lastPeakTextMs = now;
        updatePeakReadout();
    }

    repaint();
}

void SpectrumView::setCleanMode(bool clean)
{
    if (clean == cleanMode)
        return;
    cleanMode = clean;
    rebuildColumns();
    background = {};
    repaint();
}

// Loudest spectral peak with parabolic interpolation, as a level, frequency and musical note.
void SpectrumView::updatePeakReadout()
{
    peakText.clear();
    if (spectrum.size() < 3 || sampleRate == 0 || fftSize == 0)
        return;

    const double hzPerBin = (double) sampleRate / fftSize;
    const int first = juce::jmax(1, (int) std::ceil(minHz / hzPerBin));
    const int last = juce::jmin((int) spectrum.size() - 2, (int) std::floor(maxHz / hzPerBin));
    if (first >= last)
        return;

    int best = first;
    for (int k = first + 1; k <= last; ++k)
        if (spectrum[(size_t) k] > spectrum[(size_t) best])
            best = k;

    const float level = spectrum[(size_t) best];
    if (level < -80.0f)
        return;

    const double a = spectrum[(size_t) best - 1], b = level, c = spectrum[(size_t) best + 1];
    const double denominator = a - 2.0 * b + c;
    const double offset = std::abs(denominator) > 1.0e-9 ? juce::jlimit(-0.5, 0.5, 0.5 * (a - c) / denominator) : 0.0;
    const double hz = (best + offset) * hzPerBin;

    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double midi = 69.0 + 12.0 * std::log2(hz / 440.0);
    const int note = (int) std::lround(midi);
    const int cents = (int) std::lround((midi - note) * 100.0);
    const int octave = note / 12 - 1;

    const auto frequency = hz >= 1000.0 ? juce::String(hz / 1000.0, 2) + " kHz" : juce::String(hz, 1) + " Hz";
    peakText << juce::String(level, 1) << " dB   " << frequency << "   " << names[((note % 12) + 12) % 12] << octave
             << " " << (cents >= 0 ? "+" : "") << cents << "c";
}

// The spectrum's colour runs with frequency: violet bass through blue, cyan
// and mint to gold and coral highs.
juce::ColourGradient SpectrumView::frequencyGradient(float alpha, float brighten) const
{
    struct Stop { float hz; juce::uint32 argb; };
    static constexpr Stop stops[] = { { 20.0f, 0xff6b4bff }, { 90.0f, 0xff2f7bff }, { 400.0f, 0xff22d3ee },
                                      { 2000.0f, 0xff3ee6a8 }, { 6000.0f, 0xffffd166 }, { 20000.0f, 0xffff7a59 } };

    auto colourOf = [&](const Stop& s) { return juce::Colour(s.argb).brighter(brighten).withAlpha(alpha); };
    const float x0 = xForFrequency(minHz), x1 = xForFrequency(maxHz);
    juce::ColourGradient gradient(colourOf(stops[0]), x0, 0.0f, colourOf(stops[std::size(stops) - 1]), x1, 0.0f, false);
    for (size_t i = 1; i + 1 < std::size(stops); ++i)
        gradient.addColour(std::log(stops[i].hz / minHz) / std::log(maxHz / minHz), colourOf(stops[i]));
    return gradient;
}

void SpectrumView::clearSpectrum()
{
    if (spectrum.empty())
        return;

    spectrum.clear();
    peaks.clear();
    repaint();
}

void SpectrumView::setTilt(float dbPerOctave)
{
    tiltDbPerOctave = dbPerOctave;
    rebuildColumns();
    background = {};
    repaint();
}

void SpectrumView::setShowPeakHold(bool shouldShow)
{
    showPeakHold = shouldShow;
    repaint();
}

void SpectrumView::resized()
{
    rebuildColumns();
    background = {};
}

void SpectrumView::renderBackground(float pixelScale)
{
    background = juce::Image(juce::Image::RGB,
                             juce::jmax(1, juce::roundToInt((float) getWidth() * pixelScale)),
                             juce::jmax(1, juce::roundToInt((float) getHeight() * pixelScale)),
                             false);
    backgroundScale = pixelScale;

    juce::Graphics g(background);
    g.addTransform(juce::AffineTransform::scale(pixelScale));
    g.fillAll(theme::panel);

    const auto area = plotArea();
    drawGrid(g, area);

    g.setColour(theme::textDim);
    g.setFont(theme::font(10.5f));
    if (fftSize > 0 && sampleRate > 0 && ! cleanMode)
    {
        const auto info = "FFT " + juce::String(fftSize) + "  |  " + juce::String((double) sampleRate / fftSize, 1)
                        + " Hz/bin  |  tilt " + juce::String(tiltDbPerOctave, 1) + " dB/oct";
        g.drawText(info, area.reduced(6.0f, 2.0f), juce::Justification::topRight, false);
    }
}

juce::Rectangle<float> SpectrumView::plotArea() const noexcept
{
    const auto bounds = getLocalBounds().toFloat();
    return cleanMode ? bounds.reduced(1.0f, 2.0f) : bounds.reduced(1.0f, 4.0f).withTrimmedBottom(14.0f);
}

float SpectrumView::xForFrequency(float hz) const noexcept
{
    const auto area = plotArea();
    return area.getX() + area.getWidth() * std::log(hz / minHz) / std::log(maxHz / minHz);
}

float SpectrumView::yForDb(float db) const noexcept
{
    const auto area = plotArea();
    const float t = (juce::jlimit(minDb, maxDb, db) - minDb) / (maxDb - minDb);
    return area.getBottom() - t * area.getHeight();
}

// One column per pixel. Where a pixel spans several FFT bins we show the
// loudest (so narrow peaks are never lost); where bins are wider than a pixel
// (low frequencies) we interpolate between them.
void SpectrumView::rebuildColumns()
{
    columns.clear();
    const auto area = plotArea();
    const int width = juce::roundToInt(area.getWidth());
    if (width <= 1 || sampleRate == 0 || fftSize == 0)
        return;

    const float hzPerBin = (float) sampleRate / (float) fftSize;
    const float ratio = maxHz / minHz;
    auto frequencyAt = [&](float x) { return minHz * std::pow(ratio, x / (float) width); };

    columns.reserve((size_t) width + 1);
    for (int x = 0; x <= width; ++x)
    {
        Column c;
        c.frequency = frequencyAt((float) x);
        c.binLow = frequencyAt((float) x - 0.5f) / hzPerBin;
        c.binCentre = c.frequency / hzPerBin;
        c.binHigh = frequencyAt((float) x + 0.5f) / hzPerBin;
        c.tiltDb = tiltDbPerOctave * std::log2(c.frequency / 1000.0f);
        columns.push_back(c);
    }
}

float SpectrumView::levelAt(const std::vector<float>& values, const Column& c) const noexcept
{
    const int last = (int) values.size() - 1;
    if (last < 1)
        return kSilenceDb;

    if (c.binHigh - c.binLow >= 1.0f)
    {
        const int first = juce::jlimit(0, last, (int) std::ceil(c.binLow));
        const int end = juce::jlimit(first, last, (int) std::floor(c.binHigh));
        float level = values[(size_t) first];
        for (int k = first + 1; k <= end; ++k)
            level = std::max(level, values[(size_t) k]);
        return level;
    }

    // Catmull-Rom between neighbouring bins for a smooth low end.
    const float position = juce::jlimit(0.0f, (float) last, c.binCentre);
    const int i = juce::jmin((int) position, last - 1);
    const float t = position - (float) i;
    auto at = [&](int k) { return values[(size_t) juce::jlimit(0, last, k)]; };
    const float p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
    const float value = 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t
                                + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t * t * t);
    return std::max(kSilenceDb, value);
}

void SpectrumView::drawGrid(juce::Graphics& g, juce::Rectangle<float> area) const
{
    g.setFont(theme::font(10.5f));

    for (float db = 0.0f; db >= minDb; db -= 12.0f)
    {
        const float y = yForDb(db);
        g.setColour((db == 0.0f ? theme::gridStrong : theme::grid).withMultipliedAlpha(cleanMode ? 0.5f : 1.0f));
        g.drawHorizontalLine(juce::roundToInt(y), area.getX(), area.getRight());
        if (cleanMode)
            continue;
        g.setColour(theme::textDim.withAlpha(0.8f));
        g.drawText(juce::String((int) db), juce::Rectangle<float>(area.getX() + 4.0f, y + 1.0f, 40.0f, 12.0f),
                   juce::Justification::topLeft, false);
    }

    struct Mark { float hz; const char* label; bool major; };
    static constexpr Mark marks[] = {
        { 20, "20", true },  { 30, "", false },  { 40, "", false },   { 50, "50", false },  { 60, "", false },
        { 70, "", false },   { 80, "", false },  { 90, "", false },   { 100, "100", true }, { 200, "200", false },
        { 300, "", false },  { 400, "", false }, { 500, "500", false },{ 600, "", false },  { 700, "", false },
        { 800, "", false },  { 900, "", false }, { 1000, "1k", true }, { 2000, "2k", false },{ 3000, "", false },
        { 4000, "", false }, { 5000, "5k", false }, { 6000, "", false }, { 7000, "", false }, { 8000, "", false },
        { 9000, "", false }, { 10000, "10k", true }, { 20000, "20k", true },
    };

    for (const auto& mark : marks)
    {
        const float x = xForFrequency(mark.hz);
        if (cleanMode && ! mark.major)
            continue;
        g.setColour((mark.major ? theme::gridStrong : theme::grid).withMultipliedAlpha(cleanMode ? 0.5f : 1.0f));
        g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());

        if (*mark.label != 0 && ! cleanMode)
        {
            g.setColour(theme::textDim);
            const auto labelArea = juce::Rectangle<float>(x - 20.0f, area.getBottom() + 1.0f, 40.0f, 13.0f);
            const auto justification = mark.hz <= minHz ? juce::Justification::centredLeft
                                     : mark.hz >= maxHz ? juce::Justification::centredRight
                                                        : juce::Justification::centred;
            const auto clamped = labelArea.withX(juce::jlimit(area.getX(), area.getRight() - 40.0f,
                                                              mark.hz <= minHz ? x : mark.hz >= maxHz ? x - 40.0f : x - 20.0f));
            g.drawText(mark.label, clamped, justification, false);
        }
    }
}

void SpectrumView::paint(juce::Graphics& g)
{
    const float pixelScale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (! background.isValid() || pixelScale != backgroundScale)
        renderBackground(pixelScale);
    g.drawImage(background, getLocalBounds().toFloat());

    const auto area = plotArea();

    if (spectrum.empty() || columns.empty())
    {
        g.setColour(theme::textDim);
        g.setFont(theme::font(13.0f));
        g.drawText("No audio stream", area, juce::Justification::centred, false);
        return;
    }

    const float left = area.getX();
    const float bottom = area.getBottom();

    linePath.clear();
    fillPath.clear();
    fillPath.startNewSubPath(left, bottom);

    for (size_t x = 0; x < columns.size(); ++x)
    {
        const auto& c = columns[x];
        const float y = yForDb(levelAt(spectrum, c) + c.tiltDb);
        const float px = left + (float) x;
        fillPath.lineTo(px, y);
        if (x == 0)
            linePath.startNewSubPath(px, y);
        else
            linePath.lineTo(px, y);
    }

    fillPath.lineTo(left + (float) (columns.size() - 1), bottom);
    fillPath.closeSubPath();

    // Fill: frequency-coloured, fading into the floor.
    g.setGradientFill(frequencyGradient(0.55f));
    g.fillPath(fillPath);
    g.setGradientFill(juce::ColourGradient(theme::panel.withAlpha(0.0f), 0.0f, area.getY(),
                                           theme::panel.withAlpha(0.92f), 0.0f, bottom, false));
    g.fillPath(fillPath);

    // Line: two soft glow passes under a bright core.
    g.setGradientFill(frequencyGradient(0.10f));
    g.strokePath(linePath, juce::PathStrokeType(8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setGradientFill(frequencyGradient(0.25f));
    g.strokePath(linePath, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setGradientFill(frequencyGradient(1.0f, 0.35f));
    g.strokePath(linePath, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (showPeakHold && ! peaks.empty())
    {
        peakPath.clear();
        for (size_t x = 0; x < columns.size(); ++x)
        {
            const auto& c = columns[x];
            const float y = yForDb(levelAt(peaks, c) + c.tiltDb);
            if (x == 0)
                peakPath.startNewSubPath(left, y);
            else
                peakPath.lineTo(left + (float) x, y);
        }
        g.setColour(theme::textBright.withAlpha(0.35f));
        g.strokePath(peakPath, juce::PathStrokeType(1.0f));
    }

    if (peakText.isNotEmpty() && area.getHeight() > 30.0f)
    {
        const auto font = theme::font(cleanMode ? 11.5f : 12.0f, true);
        g.setFont(font);
        const float textWidth = juce::GlyphArrangement::getStringWidth(font, peakText) + 14.0f;
        const auto box = juce::Rectangle<float>(area.getX() + (cleanMode ? 6.0f : 36.0f), area.getY() + 4.0f,
                                                juce::jmin(textWidth, area.getWidth() - 12.0f), 17.0f);
        g.setColour(theme::background.withAlpha(0.55f));
        g.fillRoundedRectangle(box, 4.0f);
        g.setColour(theme::textBright.withAlpha(0.92f));
        g.drawText(peakText, box.reduced(7.0f, 0.0f), juce::Justification::centredLeft, true);
    }

    if (hovering)
        drawReadout(g, area);
}

void SpectrumView::drawReadout(juce::Graphics& g, juce::Rectangle<float> area) const
{
    const int index = juce::jlimit(0, (int) columns.size() - 1, juce::roundToInt(hoverX - area.getX()));
    const auto& c = columns[(size_t) index];
    const float level = levelAt(spectrum, c);

    g.setColour(theme::textBright.withAlpha(0.25f));
    g.drawVerticalLine(juce::roundToInt(hoverX), area.getY(), area.getBottom());

    const auto hz = c.frequency >= 1000.0f ? juce::String(c.frequency / 1000.0f, 2) + " kHz"
                                           : juce::String(juce::roundToInt(c.frequency)) + " Hz";
    const auto text = hz + "   " + (level <= kSilenceDb ? juce::String("-inf") : juce::String(level, 1)) + " dBFS";

    const float boxWidth = 132.0f;
    const float boxX = hoverX + 8.0f + boxWidth > area.getRight() ? hoverX - 8.0f - boxWidth : hoverX + 8.0f;
    const auto box = juce::Rectangle<float>(boxX, area.getY() + 18.0f, boxWidth, 20.0f);
    g.setColour(theme::raised.withAlpha(0.92f));
    g.fillRoundedRectangle(box, 4.0f);
    g.setColour(theme::textBright);
    g.setFont(theme::font(11.5f));
    g.drawText(text, box, juce::Justification::centred, false);
}

void SpectrumView::mouseMove(const juce::MouseEvent& e)
{
    hovering = plotArea().contains(e.position);
    hoverX = e.position.x;
    repaint();
}

void SpectrumView::mouseExit(const juce::MouseEvent&)
{
    hovering = false;
    repaint();
}

} // namespace sonarc
