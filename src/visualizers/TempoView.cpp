#include "visualizers/TempoView.h"
#include "ui/Theme.h"

#include <chrono>
#include <cmath>

namespace sonarc {

namespace {

double steadyNowMs()
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void drawConfidence(juce::Graphics& g, juce::Rectangle<float> bar, float confidence, bool dim)
{
    g.setColour(theme::raised);
    g.fillRoundedRectangle(bar, bar.getHeight() * 0.5f);
    const auto colour = confidence >= 0.6f ? theme::ok : confidence >= 0.35f ? theme::warn : theme::hot;
    g.setColour(dim ? colour.withMultipliedAlpha(0.4f) : colour);
    g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, confidence)), bar.getHeight() * 0.5f);
}

} // namespace

TempoView::TempoView()
{
    setOpaque(true);
    setTooltip("Detected tempo. TAP to tap it in, LOCK to hold it, 1/2 and x2 pick the octave (e.g. 70 vs 140). Right-click for options.");

    styleButton(tapButton, "Tap along to set the tempo (B key)");
    tapButton.onClick = [this] { tap(); };

    styleButton(lockButton, "Hold the current tempo");
    lockButton.setClickingTogglesState(false);
    lockButton.onClick = [this] { setLocked(! locked); };

    styleButton(halfButton, "Half the tempo (read 140 as 70)");
    halfButton.onClick = [this]
    {
        if (locked)
            lockedBpm *= 0.5f;
        else if (onRangeShift)
            onRangeShift(0.5f);
        repaint();
    };

    styleButton(doubleButton, "Double the tempo (read 70 as 140)");
    doubleButton.onClick = [this]
    {
        if (locked)
            lockedBpm *= 2.0f;
        else if (onRangeShift)
            onRangeShift(2.0f);
        repaint();
    };
}

void TempoView::styleButton(juce::TextButton& button, const juce::String& tip)
{
    button.setTooltip(tip);
    button.setWantsKeyboardFocus(false);
    addAndMakeVisible(button);
}

void TempoView::setReading(const TempoReading& newReading, double analysisTimeMs)
{
    const bool changed = newReading.valid != reading.valid || std::abs(newReading.bpm - reading.bpm) > 0.04f
                      || std::abs(newReading.confidence - reading.confidence) > 0.02f;
    reading = newReading;
    readingTimeMs = analysisTimeMs;
    if (changed)
        repaint();
}

void TempoView::clearReading()
{
    reading = {};
    repaint();
}

bool TempoView::hasTempo() const noexcept
{
    return locked || reading.valid;
}

float TempoView::shownBpm() const noexcept
{
    return locked ? lockedBpm : reading.bpm;
}

float TempoView::beatPhaseNow() const noexcept
{
    const double now = steadyNowMs();

    // A tapped tempo pulses from the last tap; a detected one from the detector's beat phase.
    if (locked && lockedFromTap && lastTapMs > 0.0)
    {
        const double period = 60000.0 / lockedBpm;
        return (float) std::fmod((now - lastTapMs) / period, 1.0);
    }
    if (! reading.valid || reading.bpm <= 0.0f)
        return 1.0f;

    const double bpm = locked ? lockedBpm : reading.bpm;
    const double phase = reading.beatPhase + (now - readingTimeMs) / 60000.0 * bpm;
    return (float) (phase - std::floor(phase));
}

void TempoView::tick()
{
    if (! beatPulse || ! hasTempo())
    {
        if (pulse != 0.0f)
        {
            pulse = 0.0f;
            repaint();
        }
        return;
    }

    const float newPulse = std::exp(-beatPhaseNow() * 7.0f); // a quick flash on each beat
    if (std::abs(newPulse - pulse) > 0.02f)
    {
        pulse = newPulse;
        repaint();
    }
}

void TempoView::setBeatPulse(bool enabled)
{
    beatPulse = enabled;
    repaint();
}

void TempoView::setCleanMode(bool clean)
{
    cleanMode = clean;
    resized();
    repaint();
}

void TempoView::tap()
{
    const double now = steadyNowMs();
    tapTempo.tap(now / 1000.0);
    lastTapMs = now;
    if (tapTempo.hasTempo())
    {
        lockedBpm = tapTempo.bpm();
        locked = true;
        lockedFromTap = true;
    }
    repaint();
}

void TempoView::setLocked(bool shouldLock)
{
    if (shouldLock && ! reading.valid)
        return; // nothing to hold
    locked = shouldLock;
    lockedFromTap = false;
    if (locked)
        lockedBpm = reading.bpm;
    else
        tapTempo.reset();
    repaint();
}

void TempoView::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onContextMenu)
        onContextMenu();
}

void TempoView::resized()
{
    auto area = getLocalBounds().reduced(5, 4);
    const int buttonHeight = juce::jlimit(14, 18, getHeight() / 6);
    auto row = area.removeFromBottom(buttonHeight);
    const int w = row.getWidth() / 4;
    tapButton.setBounds(row.removeFromLeft(w).reduced(1, 0));
    lockButton.setBounds(row.removeFromLeft(w).reduced(1, 0));
    halfButton.setBounds(row.removeFromLeft(w).reduced(1, 0));
    doubleButton.setBounds(row.reduced(1, 0));
}

void TempoView::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel);

    auto area = getLocalBounds().toFloat().reduced(6.0f, 4.0f);
    area.removeFromBottom((float) tapButton.getHeight() + 3.0f);
    const bool compact = getHeight() < 90;

    // Header: label + beat dot.
    auto header = area.removeFromTop(compact ? 11.0f : 14.0f);
    g.setFont(theme::font(compact ? 9.5f : 10.5f, true));
    g.setColour(theme::textDim);
    juce::String label = "BPM";
    if (locked)
        label << (lockedFromTap ? "  TAPPED" : "  LOCKED");
    else if (tapTempo.tapCount() > 0 && ! tapTempo.hasTempo())
        label << "  TAP " << tapTempo.tapCount() << "/3";
    g.drawText(label, header, juce::Justification::centredLeft, false);

    if (beatPulse && hasTempo())
    {
        const auto dot = juce::Rectangle<float>(8.0f, 8.0f).withCentre({ header.getRight() - 5.0f, header.getCentreY() });
        g.setColour(theme::raised);
        g.fillEllipse(dot);
        g.setColour(theme::accent.withAlpha(pulse));
        g.fillEllipse(dot.expanded(1.5f * pulse));
    }

    // Value.
    auto confidenceRow = area.removeFromBottom(compact ? 4.0f : 6.0f);
    const float valueHeight = juce::jlimit(14.0f, 34.0f, area.getHeight());
    g.setFont(theme::font(valueHeight * 0.9f, true));
    g.setColour(locked ? theme::accent : (reading.valid ? theme::textBright : theme::textDim));
    const auto text = hasTempo() ? juce::String(shownBpm(), 1) : juce::String("--");
    g.drawText(text, area, juce::Justification::centredLeft, false);

    // Confidence of the detector (dimmed while a locked/tapped value is shown).
    drawConfidence(g, confidenceRow.reduced(0.0f, compact ? 0.5f : 1.0f), reading.valid ? reading.confidence : 0.0f, locked);

    lockButton.setToggleState(locked, juce::dontSendNotification);
}

} // namespace sonarc
