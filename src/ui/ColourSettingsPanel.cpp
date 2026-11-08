#include "ui/ColourSettingsPanel.h"

namespace sonarc {

namespace {

constexpr int kCustomPresetId = 1000;

void drawChecker(juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour(juce::Colour(0xff8a8a8a));
    g.fillRect(area);
    g.setColour(juce::Colour(0xff5a5a5a));
    for (float y = area.getY(); y < area.getBottom(); y += 4.0f)
        for (float x = area.getX() + (int ((y - area.getY()) / 4.0f) % 2 == 0 ? 0.0f : 4.0f); x < area.getRight(); x += 8.0f)
            g.fillRect(juce::Rectangle<float>(x, y, 4.0f, 4.0f).getIntersection(area));
}

} // namespace

//==============================================================================
// One editable colour: label on the left, colour chip on the right.
class ColourSettingsPanel::Swatch final : public juce::Component
{
public:
    Swatch(juce::String labelText, Accessor accessor, ColourSettingsPanel& panel)
        : label(std::move(labelText)), access(std::move(accessor)), owner(panel)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    juce::Colour colour() const { return access(owner.working); }
    void setColour(juce::Colour c) { access(owner.working) = c; repaint(); }

    void setSelected(bool shouldBeSelected)
    {
        selectedState = shouldBeSelected;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(2.0f, 1.0f);
        if (selectedState || isMouseOver())
        {
            g.setColour(theme::raised.brighter(selectedState ? 0.12f : 0.04f));
            g.fillRoundedRectangle(bounds, 4.0f);
        }
        if (selectedState)
        {
            g.setColour(theme::accent);
            g.drawRoundedRectangle(bounds, 4.0f, 1.2f);
        }

        const auto chip = bounds.removeFromRight(54.0f).reduced(6.0f, 5.0f);
        drawChecker(g, chip);
        g.setColour(colour());
        g.fillRect(chip);
        g.setColour(juce::Colours::white.withAlpha(0.25f));
        g.drawRect(chip, 1.0f);

        g.setColour(theme::text);
        g.setFont(theme::font(13.0f));
        g.drawText(label, bounds.reduced(8.0f, 0.0f), juce::Justification::centredLeft, true);
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseUp(const juce::MouseEvent&) override { owner.select(this); }

    const juce::String label;

private:
    Accessor access;
    ColourSettingsPanel& owner;
    bool selectedState = false;
};

//==============================================================================
// The scrolling list: section headings and swatches.
class ColourSettingsPanel::SwatchList final : public juce::Component
{
public:
    void addHeading(const juce::String& text) { rows.push_back({ text, nullptr }); }

    Swatch* addSwatch(std::unique_ptr<Swatch> swatch)
    {
        auto* raw = swatch.get();
        addAndMakeVisible(*raw);
        rows.push_back({ {}, raw });
        owned.push_back(std::move(swatch));
        return raw;
    }

    int idealHeight() const
    {
        int h = 4;
        for (const auto& row : rows)
            h += row.swatch == nullptr ? kHeadingHeight : kRowHeight;
        return h;
    }

    void resized() override
    {
        int y = 4;
        for (const auto& row : rows)
        {
            if (row.swatch == nullptr)
            {
                y += kHeadingHeight;
                continue;
            }
            row.swatch->setBounds(0, y, getWidth(), kRowHeight);
            y += kRowHeight;
        }
    }

    void paint(juce::Graphics& g) override
    {
        int y = 4;
        g.setFont(theme::font(11.0f, true));
        for (const auto& row : rows)
        {
            if (row.swatch != nullptr)
            {
                y += kRowHeight;
                continue;
            }
            g.setColour(theme::textDim);
            g.drawText(row.heading.toUpperCase(), juce::Rectangle<int>(10, y + 8, getWidth() - 20, kHeadingHeight - 8),
                       juce::Justification::centredLeft, false);
            y += kHeadingHeight;
        }
    }

    void refreshAll()
    {
        for (auto& swatch : owned)
            swatch->repaint();
    }

    Swatch* first() const { return owned.empty() ? nullptr : owned.front().get(); }

private:
    static constexpr int kRowHeight = 28;
    static constexpr int kHeadingHeight = 30;

    struct Row
    {
        juce::String heading;
        Swatch* swatch;
    };

    std::vector<Row> rows;
    std::vector<std::unique_ptr<Swatch>> owned;
};

//==============================================================================
ColourSettingsPanel::ColourSettingsPanel(const theme::Colours& initial, const juce::String& initialPreset,
                                         ChangeCallback callback)
    : working(initial), presetName(initialPreset), onChange(std::move(callback))
{
    basePreset = presetName;
    if (basePreset == "Custom" || basePreset.isEmpty())
        basePreset = theme::presets().front().name;

    // Presets
    presetLabel.setText("Theme", juce::dontSendNotification);
    presetLabel.setFont(theme::font(13.0f, true));
    addAndMakeVisible(presetLabel);

    const auto& presetList = theme::presets();
    for (size_t i = 0; i < presetList.size(); ++i)
        presetBox.addItem(presetList[i].name, (int) i + 1);
    presetBox.addSeparator();
    presetBox.addItem("Custom", kCustomPresetId);
    presetBox.setItemEnabled(kCustomPresetId, false);
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id > 0 && id != kCustomPresetId)
            loadPreset(theme::presets()[(size_t) id - 1].name);
    };
    addAndMakeVisible(presetBox);

    resetButton.setTooltip("Throw away your changes and go back to the selected preset");
    resetButton.onClick = [this] { loadPreset(basePreset); };
    addAndMakeVisible(resetButton);

    // Swatches
    list = std::make_unique<SwatchList>();
    auto add = [this](const juce::String& label, Accessor accessor)
    {
        list->addSwatch(std::make_unique<Swatch>(label, std::move(accessor), *this));
    };
    using Colours = theme::Colours;

    list->addHeading("Window & panels");
    add("Background", [](Colours& c) -> juce::Colour& { return c.background; });
    add("Panels", [](Colours& c) -> juce::Colour& { return c.panel; });
    add("Buttons & meter tracks", [](Colours& c) -> juce::Colour& { return c.controls; });
    add("Grid lines", [](Colours& c) -> juce::Colour& { return c.grid; });
    add("Labels", [](Colours& c) -> juce::Colour& { return c.labels; });
    add("Text", [](Colours& c) -> juce::Colour& { return c.text; });
    add("Accent (toggles, highlights)", [](Colours& c) -> juce::Colour& { return c.accent; });

    list->addHeading("Spectrum (colour by frequency)");
    const char* spectrumNames[] = { "20 Hz - sub", "90 Hz - bass", "400 Hz - low mids", "2 kHz - mids", "6 kHz - presence", "20 kHz - air" };
    for (size_t i = 0; i < 6; ++i)
        add(spectrumNames[i], [i](Colours& c) -> juce::Colour& { return c.spectrum[i]; });
    add("Peak-hold line", [](Colours& c) -> juce::Colour& { return c.peakHold; });

    list->addHeading("Waveform - spectral style");
    add("Bass (below 250 Hz)", [](Colours& c) -> juce::Colour& { return c.waveLow; });
    add("Mids (250 Hz - 2.5 kHz)", [](Colours& c) -> juce::Colour& { return c.waveMid; });
    add("Highs (above 2.5 kHz)", [](Colours& c) -> juce::Colour& { return c.waveHigh; });

    list->addHeading("Waveform - classic style");
    add("Left channel", [](Colours& c) -> juce::Colour& { return c.waveLeft; });
    add("Right channel", [](Colours& c) -> juce::Colour& { return c.waveRight; });

    list->addHeading("Level meters (bottom to top)");
    const char* meterNames[] = { "Quiet (-60 dB)", "-30 dB", "-12 dB", "-6 dB", "0 dB / clip" };
    for (size_t i = 0; i < 5; ++i)
        add(meterNames[i], [i](Colours& c) -> juce::Colour& { return c.meter[i]; });

    viewport.setViewedComponent(list.get(), false);
    viewport.setScrollBarsShown(true, false);
    viewport.setScrollBarThickness(8);
    addAndMakeVisible(viewport);

    // Picker
    editingLabel.setFont(theme::font(13.0f, true));
    editingLabel.setColour(juce::Label::textColourId, theme::textBright);
    addAndMakeVisible(editingLabel);

    selector.setColour(juce::ColourSelector::backgroundColourId, theme::panel);
    selector.setColour(juce::ColourSelector::labelTextColourId, theme::text);
    selector.addChangeListener(this);
    addAndMakeVisible(selector);

    // Strength sliders
    auto setupSlider = [this](juce::Slider& slider, juce::Label& label, const juce::String& text, double max)
    {
        label.setText(text, juce::dontSendNotification);
        label.setFont(theme::font(12.5f));
        addAndMakeVisible(label);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 20);
        slider.setRange(0.0, max, 0.01);
        addAndMakeVisible(slider);
    };
    setupSlider(fillSlider, fillLabel, "Spectrum fill", 1.0);
    setupSlider(glowSlider, glowLabel, "Glow", 2.0);
    fillSlider.onValueChange = [this] { working.spectrumFill = (float) fillSlider.getValue(); edited(); };
    glowSlider.onValueChange = [this] { working.glow = (float) glowSlider.getValue(); edited(); };

    refreshFromWorking();
    select(list->first());
    setSize(preferredWidth, preferredHeight);
}

ColourSettingsPanel::~ColourSettingsPanel()
{
    selector.removeChangeListener(this);
}

void ColourSettingsPanel::paint(juce::Graphics& g)
{
    g.fillAll(theme::background);
    g.setColour(theme::panel);
    g.fillRoundedRectangle(viewport.getBounds().toFloat().expanded(4.0f), 6.0f);
    g.fillRoundedRectangle(selector.getBounds().toFloat().expanded(4.0f).withTop((float) editingLabel.getY() - 4.0f), 6.0f);
}

void ColourSettingsPanel::resized()
{
    auto area = getLocalBounds().reduced(14);

    auto top = area.removeFromTop(30);
    presetLabel.setBounds(top.removeFromLeft(56));
    resetButton.setBounds(top.removeFromRight(130));
    top.removeFromRight(10);
    presetBox.setBounds(top.removeFromLeft(260));
    area.removeFromTop(14);

    auto bottom = area.removeFromBottom(60);
    auto fillRow = bottom.removeFromTop(28);
    fillLabel.setBounds(fillRow.removeFromLeft(110));
    fillSlider.setBounds(fillRow);
    bottom.removeFromTop(4);
    glowLabel.setBounds(bottom.removeFromLeft(110));
    glowSlider.setBounds(bottom);
    area.removeFromBottom(10);

    auto left = area.removeFromLeft(area.getWidth() / 2 - 8).reduced(4);
    area.removeFromLeft(16);
    viewport.setBounds(left);
    list->setBounds(0, 0, left.getWidth() - viewport.getScrollBarThickness(), list->idealHeight());

    auto right = area.reduced(4);
    editingLabel.setBounds(right.removeFromTop(24));
    selector.setBounds(right);
}

void ColourSettingsPanel::select(Swatch* swatch)
{
    if (selected != nullptr)
        selected->setSelected(false);
    selected = swatch;
    if (selected == nullptr)
        return;

    selected->setSelected(true);
    editingLabel.setText("Editing: " + selected->label, juce::dontSendNotification);
    selector.setCurrentColour(selected->colour(), juce::dontSendNotification);
}

void ColourSettingsPanel::changeListenerCallback(juce::ChangeBroadcaster*)
{
    if (selected == nullptr)
        return;

    const auto colour = selector.getCurrentColour();
    if (colour == selected->colour())
        return;

    selected->setColour(colour);
    edited();
}

void ColourSettingsPanel::edited()
{
    presetName = "Custom";
    presetBox.setSelectedId(kCustomPresetId, juce::dontSendNotification);
    if (onChange)
        onChange(working, presetName);
}

void ColourSettingsPanel::loadPreset(const juce::String& name)
{
    working = theme::presetNamed(name);
    presetName = basePreset = name;
    refreshFromWorking();
    if (selected != nullptr)
        selector.setCurrentColour(selected->colour(), juce::dontSendNotification);
    if (onChange)
        onChange(working, presetName);
}

void ColourSettingsPanel::refreshFromWorking()
{
    const auto& presetList = theme::presets();
    int id = kCustomPresetId;
    for (size_t i = 0; i < presetList.size(); ++i)
        if (presetList[i].name == presetName)
            id = (int) i + 1;
    presetBox.setSelectedId(id, juce::dontSendNotification);

    fillSlider.setValue(working.spectrumFill, juce::dontSendNotification);
    glowSlider.setValue(working.glow, juce::dontSendNotification);
    list->refreshAll();
    repaint();
}

} // namespace sonarc
