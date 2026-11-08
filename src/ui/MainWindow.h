#pragma once

#include "audio/AudioEngine.h"
#include "settings/AppSettings.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace sonarc {

// The top-level window. Besides the normal framed window it supports:
//  - always on top ("pin"), so it stays above a video, browser or DAW window
//  - overlay mode: no title bar or frame; drag anywhere to move, edges resize
//  - window opacity
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(AudioEngine& engine, AppSettings& settings);

    void closeButtonPressed() override;
    bool keyPressed(const juce::KeyPress&) override;

    bool isOverlayMode() const noexcept { return overlayMode; }
    void setOverlayMode(bool shouldBeOverlay);

    void setPinnedOnTop(bool shouldBeOnTop);
    bool isPinnedOnTop() const { return isAlwaysOnTop(); }

    float windowOpacity() const noexcept { return opacity; }
    void setWindowOpacity(float newOpacity);

    // Called by MainComponent while overlay mode is on and the user drags the meters.
    void beginDrag(const juce::MouseEvent&);
    void continueDrag(const juce::MouseEvent&);

private:
    AppSettings& settings;
    bool overlayMode = false;
    float opacity = 1.0f;
    juce::ComponentDragger dragger;
};

} // namespace sonarc
