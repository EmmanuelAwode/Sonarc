#pragma once

#include "dsp/AnalysisSnapshot.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace sonarc {

// The Key module: detected key (e.g. "F# minor"), its Camelot code, a
// confidence bar and LOCK. Shows "--" while there isn't enough tonal
// material (silence, drums only, very short passages).
class KeyView final : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    KeyView();

    void setReading(const KeyReading& reading);
    void clearReading();
    void setCleanMode(bool clean);
    void themeChanged() { repaint(); }

    std::function<void()> onContextMenu;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

    static constexpr int preferredWidth = 132;

private:
    KeyReading reading;
    bool locked = false;
    int lockedKey = -1;
    bool cleanMode = false;
    juce::TextButton lockButton { "LOCK" };
};

} // namespace sonarc
