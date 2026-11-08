#pragma once

#include "dsp/AnalysisSnapshot.h"
#include "dsp/TempoAnalyzer.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace sonarc {

// The BPM module: detected tempo with a confidence bar and an optional beat
// pulse, plus TAP (tap tempo), LOCK, and 1/2 / x2 to pick the tempo octave.
// Shows "--" whenever the detector has no reliable tempo.
class TempoView final : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    TempoView();

    void setReading(const TempoReading& reading, double analysisTimeMs);
    void clearReading();
    void tick(); // once per frame: animates the beat pulse

    void setBeatPulse(bool enabled);
    void setCleanMode(bool clean);
    void themeChanged() { repaint(); }

    // Tap tempo (also bound to the B key). Three taps set and lock the tempo.
    void tap();

    // Detected tempos are folded into [minimum, 2 * minimum); 1/2 and x2 move that range.
    std::function<void(float factor)> onRangeShift;
    std::function<void()> onContextMenu;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

    static constexpr int preferredWidth = 150;

private:
    bool hasTempo() const noexcept;
    float shownBpm() const noexcept;
    float beatPhaseNow() const noexcept;
    void setLocked(bool shouldLock);
    void styleButton(juce::TextButton&, const juce::String& tip);

    TempoReading reading;
    double readingTimeMs = 0.0;

    bool locked = false;
    float lockedBpm = 0.0f;
    bool lockedFromTap = false;
    TapTempo tapTempo;
    double lastTapMs = 0.0;

    bool beatPulse = true;
    bool cleanMode = false;
    float pulse = 0.0f;

    juce::TextButton tapButton { "TAP" }, lockButton { "LOCK" }, halfButton { "1/2" }, doubleButton { "x2" };
};

} // namespace sonarc
