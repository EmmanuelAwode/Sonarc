#pragma once

#include "audio/AudioEngine.h"
#include "settings/AppSettings.h"
#include "ui/WindowsDesktop.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace sonarc {

// The top-level window. Besides the normal framed window it supports:
//  - always on top ("pin"), with a watchdog that puts it back if another app covers it
//  - overlay mode: no title bar or frame; drag anywhere to move, edges resize
//  - stick mode: docked full-width to the top or bottom screen edge as a
//    toolbar; Windows reserves the strip so other windows don't cover it
//  - window opacity
class MainWindow final : public juce::DocumentWindow,
                         private juce::Timer
{
public:
    enum class StickEdge { None, Top, Bottom };

    MainWindow(AudioEngine& engine, AppSettings& settings);
    ~MainWindow() override;

    void closeButtonPressed() override;
    bool keyPressed(const juce::KeyPress&) override;

    bool isOverlayMode() const noexcept { return overlayMode; }
    void setOverlayMode(bool shouldBeOverlay);

    void setPinnedOnTop(bool shouldBeOnTop);
    bool isPinnedOnTop() const { return pinned; }

    StickEdge stickEdge() const noexcept { return stuckTo; }
    bool isStuck() const noexcept { return stuckTo != StickEdge::None; }
    void setStickEdge(StickEdge edge);
    int stickHeight() const noexcept { return barHeight; }
    void setStickHeight(int logicalPixels);

    float windowOpacity() const noexcept { return opacity; }
    void setWindowOpacity(float newOpacity);

    // Bounds to remember for next launch (the free-floating position, even while stuck).
    juce::String windowStateForSaving();

    // Called by MainComponent while overlay mode is on and the user drags the meters.
    void beginDrag(const juce::MouseEvent&);
    void continueDrag(const juce::MouseEvent&);

private:
    void timerCallback() override;
    void dock();
    void notifyContent();

    AppSettings& settings;
    bool overlayMode = false;
    bool pinned = false;
    float opacity = 1.0f;
    juce::ComponentDragger dragger;

    StickEdge stuckTo = StickEdge::None;
    int barHeight = 110;
    juce::Rectangle<int> boundsBeforeStick;
    bool overlayBeforeStick = false;
    desktop::AppBar appBar;
};

} // namespace sonarc
