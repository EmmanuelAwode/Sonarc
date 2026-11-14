#include "plugin/LookAndFeel.h"

#include "plugin/Theme.h"

namespace driftwave {

juce::Font uiFont(float height, bool bold)
{
    // Segoe UI ships with every supported Windows and has even-width digits, so values do not jitter.
    return juce::Font(juce::FontOptions("Segoe UI", height, bold ? juce::Font::bold : juce::Font::plain));
}

DriftwaveLookAndFeel::DriftwaveLookAndFeel()
{
    applyColours();
}

void DriftwaveLookAndFeel::setAccent(juce::Colour accent)
{
    palette.accent = usableAccent(accent);
    palette.cool = companionFor(palette.accent);
    applyColours();
}

void DriftwaveLookAndFeel::applyColours()
{
    setColour(juce::ResizableWindow::backgroundColourId, palette.background);
    setColour(juce::Label::textColourId, palette.textDim);
    setColour(juce::Label::backgroundWhenEditingColourId, palette.background);
    setColour(juce::Label::textWhenEditingColourId, palette.text);
    setColour(juce::Label::outlineWhenEditingColourId, palette.accent);

    setColour(juce::Slider::textBoxTextColourId, palette.text);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, palette.accent.withAlpha(0.35f));

    setColour(juce::TextButton::buttonColourId, palette.elevated);
    setColour(juce::TextButton::buttonOnColourId, palette.elevated);
    setColour(juce::TextButton::textColourOffId, palette.text);
    setColour(juce::TextButton::textColourOnId, palette.text);

    setColour(juce::ComboBox::backgroundColourId, palette.background);
    setColour(juce::ComboBox::outlineColourId, palette.border);
    setColour(juce::ComboBox::textColourId, palette.text);
    setColour(juce::ComboBox::arrowColourId, palette.textDim);

    setColour(juce::PopupMenu::backgroundColourId, palette.surface);
    setColour(juce::PopupMenu::textColourId, palette.text);
    setColour(juce::PopupMenu::headerTextColourId, palette.textDim);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, palette.elevated);
    setColour(juce::PopupMenu::highlightedTextColourId, palette.accent);

    setColour(juce::AlertWindow::backgroundColourId, palette.surface);
    setColour(juce::AlertWindow::textColourId, palette.text);
    setColour(juce::AlertWindow::outlineColourId, palette.border);
    setColour(juce::TextEditor::backgroundColourId, palette.background);
    setColour(juce::TextEditor::textColourId, palette.text);
    setColour(juce::TextEditor::outlineColourId, palette.border);
    setColour(juce::TextEditor::focusedOutlineColourId, palette.accent);
    setColour(juce::TextEditor::highlightColourId, palette.accent.withAlpha(0.35f));
    setColour(juce::TextEditor::highlightedTextColourId, palette.text);
    setColour(juce::CaretComponent::caretColourId, palette.text);

    setColour(juce::TooltipWindow::backgroundColourId, palette.elevated);
    setColour(juce::TooltipWindow::textColourId, palette.text);
    setColour(juce::TooltipWindow::outlineColourId, palette.border);
}

void DriftwaveLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                            float startAngle, float endAngle, juce::Slider& slider)
{
    // A softly lit disc with a pointer, inside a ring that fills with the accent up to the value.
    // The Knob component animates two properties: where the ring is drawn (so preset
    // changes glide) and how far the pointer is hovered over.
    const auto& props = slider.getProperties();
    const float shown = props.contains("shown") ? float(props["shown"]) : sliderPos;
    const float hover = props.contains("hover") ? float(props["hover"]) : (slider.isMouseOverOrDragging() ? 1.0f : 0.0f);
    const bool dragging = slider.isMouseButtonDown();

    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const float size = juce::jmin(bounds.getWidth(), bounds.getHeight()) - 2.0f;
    const auto centre = bounds.getCentre();
    const float ringWidth = size > 60.0f ? 3.0f : 2.5f;
    const float ringRadius = size * 0.5f - ringWidth * 0.5f;
    const float discRadius = ringRadius - ringWidth * 0.5f - 4.0f;
    const float angle = startAngle + shown * (endAngle - startAngle);
    const juce::PathStrokeType stroke(ringWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path ring;
    ring.addCentredArc(centre.x, centre.y, ringRadius, ringRadius, 0.0f, startAngle, endAngle, true);
    g.setColour(palette.background.interpolatedWith(palette.border, 0.55f));
    g.strokePath(ring, stroke);

    if (shown > 0.002f) {
        juce::Path value;
        value.addCentredArc(centre.x, centre.y, ringRadius, ringRadius, 0.0f, startAngle, angle, true);
        g.setColour(palette.accent.interpolatedWith(juce::Colours::white, 0.12f * hover));
        g.strokePath(value, stroke);
    }

    // The disc: a soft shadow underneath, lit from above, with a fine bright rim at the top edge.
    const auto disc = juce::Rectangle<float>(discRadius * 2.0f, discRadius * 2.0f).withCentre(centre);
    for (int i = 3; i >= 1; --i) {
        g.setColour(juce::Colours::black.withAlpha(0.10f));
        g.fillEllipse(disc.translated(0.0f, 1.0f + float(i) * 0.8f).expanded(float(i) * 0.6f));
    }
    const auto face = palette.elevated.brighter(0.05f * hover + (dragging ? 0.04f : 0.0f));
    g.setGradientFill(juce::ColourGradient(face.brighter(0.14f), centre.x, disc.getY(), face.darker(0.22f), centre.x, disc.getBottom(), false));
    g.fillEllipse(disc);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.20f + 0.10f * hover), centre.x, disc.getY(),
                                           juce::Colours::black.withAlpha(0.25f), centre.x, disc.getBottom(), false));
    g.drawEllipse(disc.reduced(0.5f), 1.0f);

    // The pointer turns amber while the knob is being turned.
    const auto tip = centre.getPointOnCircumference(discRadius - 3.5f, angle);
    const auto tail = centre.getPointOnCircumference(discRadius * 0.42f, angle);
    juce::Path pointer;
    pointer.startNewSubPath(tail);
    pointer.lineTo(tip);
    g.setColour(dragging ? palette.accent : palette.text);
    g.strokePath(pointer, juce::PathStrokeType(size > 60.0f ? 2.4f : 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Label* DriftwaveLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(uiFont(type::value));
    label->setJustificationType(juce::Justification::centred);
    label->setColour(juce::Label::textColourId, palette.text);
    label->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour(juce::TextEditor::backgroundColourId, palette.background);
    label->setColour(juce::TextEditor::outlineColourId, palette.accent);
    label->setColour(juce::TextEditor::focusedOutlineColourId, palette.accent);
    return label;
}

void DriftwaveLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    // A small square lamp and a text label: lit means on. "lit" is animated by the editor.
    const auto bounds = button.getLocalBounds().toFloat();
    const auto& props = button.getProperties();
    const float lit = props.contains("lit") ? float(props["lit"]) : (button.getToggleState() ? 1.0f : 0.0f);
    const bool heading = props["heading"];

    const float side = 9.0f;
    const auto lamp = juce::Rectangle<float>(bounds.getX() + 1.0f, std::floor(bounds.getCentreY() - side * 0.5f) + 0.5f, side, side);
    g.setColour(palette.background);
    g.fillRoundedRectangle(lamp, 2.0f);
    g.setColour(palette.accent.withAlpha(lit));
    g.fillRoundedRectangle(lamp, 2.0f);
    g.setColour((highlighted ? palette.textDim : palette.border.brighter(0.25f)).withAlpha(1.0f - lit));
    g.drawRoundedRectangle(lamp, 2.0f, 1.0f);

    g.setColour(palette.textDim.interpolatedWith(palette.text, lit));
    g.setFont(uiFont(heading ? type::section : type::label, heading));
    g.drawText(button.getButtonText(), bounds.withTrimmedLeft(side + 9.0f), juce::Justification::centredLeft, false);
}

void DriftwaveLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool down)
{
    // Slightly raised; pressing it pushes it in.
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const bool enabled = button.isEnabled();
    const auto face = down ? palette.background : highlighted && enabled ? palette.elevated.brighter(0.08f) : palette.elevated;
    if (down)
        g.setColour(face);
    else
        g.setGradientFill(juce::ColourGradient(face.brighter(0.06f), 0.0f, bounds.getY(), face.darker(0.10f), 0.0f, bounds.getBottom(), false));
    g.fillRoundedRectangle(bounds, 3.0f);
    // A latched button (one that stays on) is outlined in the accent while it is on.
    g.setColour(button.getToggleState() ? palette.accent : highlighted && enabled ? palette.textDim : palette.border);
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
}

void DriftwaveLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<int>(width, height).toFloat().reduced(0.5f);
    g.setColour(palette.background);
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(box.isMouseOver(true) || box.isPopupActive() ? palette.textDim : palette.border);
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);

    // A small chevron on the right.
    const float cx = float(width) - 13.0f, cy = float(height) * 0.5f;
    juce::Path chevron;
    chevron.startNewSubPath(cx - 3.5f, cy - 1.5f);
    chevron.lineTo(cx, cy + 2.0f);
    chevron.lineTo(cx + 3.5f, cy - 1.5f);
    g.setColour(palette.textDim);
    g.strokePath(chevron, juce::PathStrokeType(1.2f));
}

void DriftwaveLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(8, 0, box.getWidth() - 30, box.getHeight());
    label.setFont(getComboBoxFont(box));
}

} // namespace driftwave
