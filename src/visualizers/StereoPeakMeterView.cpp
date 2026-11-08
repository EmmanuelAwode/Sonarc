#include "visualizers/StereoPeakMeterView.h"
#include "ui/Theme.h"

namespace sonarc {

StereoPeakMeterView::StereoPeakMeterView()
{
    setOpaque(true);
    setTooltip("Sample peak / RMS (dBFS). Click to reset max and clip.");
}

void StereoPeakMeterView::setLevels(const LevelReading& left, const LevelReading& right)
{
    auto same = [](const LevelReading& a, const LevelReading& b)
    {
        return a.peakDb == b.peakDb && a.holdDb == b.holdDb && a.rmsDb == b.rmsDb && a.maxDb == b.maxDb && a.clipped == b.clipped;
    };
    if (same(left, levels[0]) && same(right, levels[1]))
        return;

    levels[0] = left;
    levels[1] = right;
    repaint();
}

void StereoPeakMeterView::clearLevels()
{
    levels[0] = {};
    levels[1] = {};
    repaint();
}

void StereoPeakMeterView::mouseDown(const juce::MouseEvent&)
{
    if (onResetRequested)
        onResetRequested();
}

float StereoPeakMeterView::yForDb(float db, juce::Rectangle<float> bar) const noexcept
{
    const float t = (juce::jlimit(floorDb, 0.0f, db) - floorDb) / -floorDb;
    return bar.getBottom() - t * bar.getHeight();
}


void StereoPeakMeterView::themeChanged()
{
    background = {};
    repaint();
}

void StereoPeakMeterView::resized()
{
    background = {};
}

StereoPeakMeterView::Layout StereoPeakMeterView::layout() const noexcept
{
    Layout l;
    const bool compact = isCompact();
    auto area = getLocalBounds().toFloat().reduced(6.0f, compact ? 3.0f : 6.0f);
    l.header = area.removeFromTop(getHeight() < 80 ? 0.0f : (compact ? 15.0f : 18.0f));
    l.footer = area.removeFromBottom(compact ? 0.0f : 14.0f);
    area.removeFromTop(compact ? 3.0f : 4.0f);

    const float barWidth = juce::jmin(16.0f, (area.getWidth() - 30.0f) / 2.0f);
    l.leftBar = area.withWidth(barWidth);
    l.rightBar = area.withX(area.getRight() - barWidth).withWidth(barWidth);
    l.scale = area.withLeft(l.leftBar.getRight()).withRight(l.rightBar.getX());
    return l;
}

void StereoPeakMeterView::renderBackground(float pixelScale)
{
    background = juce::Image(juce::Image::RGB,
                             juce::jmax(1, juce::roundToInt((float) getWidth() * pixelScale)),
                             juce::jmax(1, juce::roundToInt((float) getHeight() * pixelScale)),
                             false);
    backgroundScale = pixelScale;

    juce::Graphics g(background);
    g.addTransform(juce::AffineTransform::scale(pixelScale));
    g.fillAll(theme::panel);

    const auto l = layout();
    g.setColour(theme::raised);
    g.fillRoundedRectangle(l.leftBar, 2.0f);
    g.fillRoundedRectangle(l.rightBar, 2.0f);

    // Gaps between LED segments, every 3 px.
    segmentGaps.clear();
    for (const auto& bar : { l.leftBar, l.rightBar })
        for (float y = bar.getBottom() - 3.0f; y > bar.getY(); y -= 3.0f)
            segmentGaps.addWithoutMerging({ bar.getX(), y, bar.getWidth(), 1.0f });

    // Scale between the bars (tick marks only when compact).
    g.setColour(theme::textDim);
    g.setFont(theme::font(9.5f));
    for (float db : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f })
    {
        if (isCompact())
        {
            const float y = yForDb(db, l.leftBar);
            g.setColour(theme::textDim.withAlpha(0.6f));
            g.fillRect(juce::Rectangle<float>(l.scale.getCentreX() - 3.0f, y, 6.0f, 1.0f));
            continue;
        }

        const float y = yForDb(db, l.leftBar);
        g.drawText(juce::String((int) -db), juce::Rectangle<float>(l.scale.getX(), y - 6.0f, l.scale.getWidth(), 12.0f),
                   juce::Justification::centred, false);
    }

    g.setFont(theme::font(10.5f));
    g.drawText("L", l.footer.withWidth(l.leftBar.getWidth()).withX(l.leftBar.getX()), juce::Justification::centred, false);
    g.drawText("R", l.footer.withWidth(l.rightBar.getWidth()).withX(l.rightBar.getX()), juce::Justification::centred, false);
    g.drawText("dBFS", l.footer.withLeft(l.scale.getX()).withRight(l.scale.getRight()), juce::Justification::centred, false);
}

void StereoPeakMeterView::drawChannel(juce::Graphics& g, juce::Rectangle<float> bar, const LevelReading& level) const
{
    // Colour is a function of height, so the bar warms as it rises.
    const auto& stops = theme::meterStops;
    juce::ColourGradient gradient(stops[0], 0.0f, bar.getBottom(), stops[4], 0.0f, bar.getY(), false);
    const float span = bar.getHeight();
    auto proportionFor = [&](float db) { return (bar.getBottom() - yForDb(db, bar)) / span; };
    gradient.addColour(proportionFor(-30.0f), stops[1]);
    gradient.addColour(proportionFor(-12.0f), stops[2]);
    gradient.addColour(proportionFor(-6.0f), stops[3]);

    const float peakY = yForDb(level.peakDb, bar);
    const float rmsY = yForDb(level.rmsDb, bar);

    // Soft glow behind the lit part.
    g.setGradientFill(gradient);
    g.setOpacity(juce::jmin(1.0f, 0.16f * theme::glow));
    g.fillRoundedRectangle(bar.withTop(peakY).expanded(3.0f, 2.0f), 4.0f);

    g.setOpacity(0.45f);
    g.fillRect(bar.withTop(peakY));
    g.setOpacity(1.0f);
    g.fillRect(bar.withTop(rmsY));

    if (level.holdDb > floorDb)
    {
        const float holdY = yForDb(level.holdDb, bar);
        g.setColour(level.holdDb >= 0.0f ? theme::hot : theme::textBright.withAlpha(0.85f));
        g.fillRect(bar.withTop(holdY - 1.0f).withHeight(2.0f));
    }
}

void StereoPeakMeterView::paint(juce::Graphics& g)
{
    const float pixelScale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (! background.isValid() || pixelScale != backgroundScale)
        renderBackground(pixelScale);
    g.drawImage(background, getLocalBounds().toFloat());

    const auto l = layout();
    drawChannel(g, l.leftBar, levels[0]);
    drawChannel(g, l.rightBar, levels[1]);

    g.setColour(theme::panel.withAlpha(0.75f));
    g.fillRectList(segmentGaps);

    if (l.header.isEmpty())
        return;

    // Max peak readouts, red when the channel clipped.
    auto drawMax = [&](juce::Rectangle<float> box, const LevelReading& level)
    {
        g.setColour(level.clipped ? theme::hot : theme::raised);
        g.fillRoundedRectangle(box, 3.0f);
        g.setColour(level.clipped ? theme::background : theme::text);
        g.setFont(theme::font(10.5f, true));
        const auto text = level.maxDb <= -100.0f ? juce::String("-inf") : juce::String(level.maxDb, 1);
        g.drawText(text, box, juce::Justification::centred, false);
    };

    const float boxWidth = (l.header.getWidth() - 4.0f) / 2.0f;
    drawMax(l.header.withWidth(boxWidth), levels[0]);
    drawMax(l.header.withX(l.header.getRight() - boxWidth).withWidth(boxWidth), levels[1]);
}

} // namespace sonarc
