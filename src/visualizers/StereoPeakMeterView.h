#pragma once

#include "dsp/LevelMeter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace sonarc {

// Vertical L/R meter: RMS as a solid bar, sample peak as a lighter bar above
// it, a peak-hold tick, and the max peak / clip state at the top.
// Clicking the meter resets max peak and clip indicators.
class StereoPeakMeterView final : public juce::Component,
                                  public juce::SettableTooltipClient
{
public:
    StereoPeakMeterView();

    void setLevels(const LevelReading& left, const LevelReading& right);
    void clearLevels();

    // The active theme changed: rebuild cached images and redraw.
    void themeChanged();

    std::function<void()> onResetRequested;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

    static constexpr int preferredWidth = 92;

private:
    struct Layout
    {
        juce::Rectangle<float> header, footer, leftBar, rightBar, scale;
    };

    Layout layout() const noexcept;
    bool isCompact() const noexcept { return getHeight() < 150; } // e.g. docked as a thin bar
    void renderBackground(float pixelScale);
    void drawChannel(juce::Graphics&, juce::Rectangle<float> bar, const LevelReading&) const;
    float yForDb(float db, juce::Rectangle<float> bar) const noexcept;

    static constexpr float floorDb = -60.0f;

    LevelReading levels[2];
    juce::Image background; // panel, tracks and scale, rendered once per size
    juce::RectangleList<float> segmentGaps; // LED-style gaps drawn over the bars
    float backgroundScale = 0.0f;
};

} // namespace sonarc
