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

    repaint();
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
    if (fftSize > 0 && sampleRate > 0)
    {
        const auto info = "FFT " + juce::String(fftSize) + "  |  " + juce::String((double) sampleRate / fftSize, 1)
                        + " Hz/bin  |  tilt " + juce::String(tiltDbPerOctave, 1) + " dB/oct";
        g.drawText(info, area.reduced(6.0f, 2.0f), juce::Justification::topRight, false);
    }
}

juce::Rectangle<float> SpectrumView::plotArea() const noexcept
{
    return getLocalBounds().toFloat().reduced(1.0f, 4.0f).withTrimmedBottom(14.0f);
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
        g.setColour(db == 0.0f ? theme::gridStrong : theme::grid);
        g.drawHorizontalLine(juce::roundToInt(y), area.getX(), area.getRight());
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
        g.setColour(mark.major ? theme::gridStrong : theme::grid);
        g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());

        if (*mark.label != 0)
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

    g.setGradientFill(juce::ColourGradient(theme::accent.withAlpha(0.42f), 0.0f, area.getY(),
                                           theme::accentDeep.withAlpha(0.04f), 0.0f, bottom, false));
    g.fillPath(fillPath);

    g.setColour(theme::accent);
    g.strokePath(linePath, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));

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
