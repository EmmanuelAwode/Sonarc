#include "ui/SonarcLookAndFeel.h"
#include "ui/Theme.h"

namespace sonarc {

SonarcLookAndFeel::SonarcLookAndFeel()
    : LookAndFeel_V4(LookAndFeel_V4::ColourScheme(theme::background, // window background
                                                   theme::raised,     // widget background
                                                   theme::panel,      // menu background
                                                   theme::outline,    // outline
                                                   theme::text,       // default text
                                                   theme::raised,     // default fill
                                                   theme::background, // highlighted text
                                                   theme::accent,     // highlighted fill
                                                   theme::text))      // menu text
{
    setColour(juce::ComboBox::backgroundColourId, theme::raised);
    setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::textColourId, theme::text);
    setColour(juce::ComboBox::arrowColourId, theme::textDim);
    setColour(juce::PopupMenu::backgroundColourId, theme::panel);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, theme::raised);
    setColour(juce::PopupMenu::highlightedTextColourId, theme::textBright);
    setColour(juce::PopupMenu::headerTextColourId, theme::textDim);
    setColour(juce::Label::textColourId, theme::text);
    setColour(juce::Slider::textBoxTextColourId, theme::text);
    setColour(juce::TooltipWindow::backgroundColourId, theme::raised);
    setColour(juce::TooltipWindow::textColourId, theme::text);
    setColour(juce::TooltipWindow::outlineColourId, theme::outline);
}

juce::Font SonarcLookAndFeel::getComboBoxFont(juce::ComboBox&) { return theme::font(13.0f); }
juce::Font SonarcLookAndFeel::getPopupMenuFont() { return theme::font(14.0f); }
juce::Font SonarcLookAndFeel::getLabelFont(juce::Label& label) { return label.getFont(); }

void SonarcLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                     int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height);
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId).brighter(box.isMouseOver(true) ? 0.08f : 0.0f));
    g.fillRoundedRectangle(bounds, 5.0f);

    // Small chevron on the right.
    const float cx = (float) width - 12.0f;
    const float cy = (float) height * 0.5f;
    juce::Path chevron;
    chevron.startNewSubPath(cx - 3.5f, cy - 1.5f);
    chevron.lineTo(cx, cy + 2.0f);
    chevron.lineTo(cx + 3.5f, cy - 1.5f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.strokePath(chevron, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void SonarcLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float, float,
                                         juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearBar)
    {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, 0.0f, 0.0f, style, slider);
        return;
    }

    // A compact "value bar": rounded track filled up to the current value, label drawn by the text box.
    const auto bounds = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height);
    g.setColour(theme::raised.brighter(slider.isMouseOverOrDragging() ? 0.08f : 0.0f));
    g.fillRoundedRectangle(bounds, 5.0f);

    auto fill = bounds.withRight(juce::jlimit(bounds.getX(), bounds.getRight(), sliderPos));
    g.saveState();
    g.reduceClipRegion(fill.toNearestInt());
    g.setColour(theme::accent.withAlpha(0.22f));
    g.fillRoundedRectangle(bounds, 5.0f);
    g.restoreState();
}

void SonarcLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    const auto bounds = button.getLocalBounds().toFloat();
    const bool on = button.getToggleState();

    g.setColour(theme::raised.brighter(highlighted ? 0.08f : 0.0f));
    g.fillRoundedRectangle(bounds, 5.0f);

    const auto dot = juce::Rectangle<float>(8.0f, 8.0f).withCentre({ bounds.getX() + 12.0f, bounds.getCentreY() });
    g.setColour(on ? theme::accent : theme::outline.brighter(0.3f));
    g.fillEllipse(dot);

    g.setColour(on ? theme::text : theme::textDim);
    g.setFont(theme::font(13.0f));
    g.drawText(button.getButtonText(), bounds.withTrimmedLeft(22.0f), juce::Justification::centredLeft, true);
}

} // namespace sonarc
