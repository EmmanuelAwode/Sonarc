#include "visualizers/KeyView.h"
#include "dsp/KeyDetector.h"
#include "ui/Theme.h"

namespace sonarc {

namespace {

// Camelot wheel colours: one hue per number, so compatible keys look related.
juce::Colour camelotColour(int key)
{
    const juce::String code = keys::camelot(key);
    const int number = code.getIntValue();
    if (number < 1)
        return theme::raised;
    return juce::Colour::fromHSV((float) ((number - 1) % 12) / 12.0f * 0.92f + 0.02f, 0.62f, 0.92f, 1.0f);
}

} // namespace

KeyView::KeyView()
{
    setOpaque(true);
    setTooltip("Detected musical key with its Camelot code. LOCK holds it. Right-click for options.");

    lockButton.setTooltip("Hold the current key");
    lockButton.setWantsKeyboardFocus(false);
    lockButton.onClick = [this]
    {
        if (locked)
        {
            locked = false;
        }
        else if (reading.valid)
        {
            locked = true;
            lockedKey = reading.key;
        }
        repaint();
    };
    addAndMakeVisible(lockButton);
}

void KeyView::setReading(const KeyReading& newReading)
{
    const bool changed = newReading.valid != reading.valid || newReading.key != reading.key
                      || std::abs(newReading.confidence - reading.confidence) > 0.02f;
    reading = newReading;
    if (changed)
        repaint();
}

void KeyView::clearReading()
{
    reading = {};
    repaint();
}

void KeyView::setCleanMode(bool clean)
{
    cleanMode = clean;
    repaint();
}

void KeyView::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onContextMenu)
        onContextMenu();
}

void KeyView::resized()
{
    auto area = getLocalBounds().reduced(5, 4);
    const int buttonHeight = juce::jlimit(14, 18, getHeight() / 6);
    lockButton.setBounds(area.removeFromBottom(buttonHeight).removeFromRight(48));
}

void KeyView::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel);

    const int key = locked ? lockedKey : (reading.valid ? reading.key : -1);
    const bool known = key >= 0;
    const bool compact = getHeight() < 90;

    auto area = getLocalBounds().toFloat().reduced(6.0f, 4.0f);
    auto bottom = area.removeFromBottom((float) lockButton.getHeight());
    area.removeFromBottom(3.0f);

    auto header = area.removeFromTop(compact ? 11.0f : 14.0f);
    g.setFont(theme::font(compact ? 9.5f : 10.5f, true));
    g.setColour(theme::textDim);
    g.drawText(locked ? "KEY  LOCKED" : "KEY", header, juce::Justification::centredLeft, false);

    // Camelot pill on the right of the main line, key name on the left.
    const float lineHeight = juce::jlimit(14.0f, 30.0f, area.getHeight() - (compact ? 4.0f : 8.0f));
    auto line = area.removeFromTop(lineHeight);
    const float pillWidth = compact ? 34.0f : 40.0f;
    auto pill = line.removeFromRight(pillWidth).withSizeKeepingCentre(pillWidth, juce::jmin(lineHeight, 22.0f));
    g.setColour(known ? camelotColour(key) : theme::raised);
    g.fillRoundedRectangle(pill, 5.0f);
    g.setColour(known ? juce::Colours::black.withAlpha(0.85f) : theme::textDim);
    g.setFont(theme::font(juce::jmin(13.0f, pill.getHeight() * 0.7f), true));
    g.drawText(known ? keys::camelot(key) : "--", pill, juce::Justification::centred, false);

    g.setColour(locked ? theme::accent : (known ? theme::textBright : theme::textDim));
    const float nameSize = juce::jmin(lineHeight * 0.78f, 20.0f);
    g.setFont(theme::font(nameSize, true));
    const juce::String name = known ? (line.getWidth() > 86.0f ? keys::name(key) : keys::shortName(key)) : "--";
    g.drawFittedText(name, line.reduced(0.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

    // Confidence bar next to the LOCK button.
    auto bar = bottom.withTrimmedRight((float) lockButton.getWidth() + 6.0f).withSizeKeepingCentre(bottom.getWidth() - (float) lockButton.getWidth() - 6.0f, compact ? 4.0f : 6.0f);
    bar.setX(bottom.getX());
    const float confidence = reading.valid ? reading.confidence : 0.0f;
    g.setColour(theme::raised);
    g.fillRoundedRectangle(bar, bar.getHeight() * 0.5f);
    const auto colour = confidence >= 0.6f ? theme::ok : confidence >= 0.35f ? theme::warn : theme::hot;
    g.setColour(locked ? colour.withMultipliedAlpha(0.4f) : colour);
    g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, confidence)), bar.getHeight() * 0.5f);

    lockButton.setToggleState(locked, juce::dontSendNotification);
}

} // namespace sonarc
