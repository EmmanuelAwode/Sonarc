#include "ui/MainWindow.h"
#include "ui/MainComponent.h"
#include "ui/Theme.h"

namespace sonarc {

MainWindow::MainWindow(AudioEngine& engine, AppSettings& appSettings)
    : DocumentWindow("Sonarc", theme::background, DocumentWindow::allButtons),
      settings(appSettings)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new MainComponent(engine, settings), true);
    setResizable(true, false);
    setResizeLimits(240, 90, 10000, 10000);
    centreWithSize(1100, 320);

    opacity = settings.windowOpacity(); // read before setOverlayMode() re-applies it
    setOverlayMode(settings.overlayMode());
    setPinnedOnTop(settings.alwaysOnTop());
    setWindowOpacity(opacity);

    // Restore last, so the saved bounds win over the defaults above.
    if (const auto state = settings.windowState(); state.isNotEmpty())
        restoreWindowStateFromString(state);

    setVisible(true);
    getContentComponent()->grabKeyboardFocus();
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

// Shortcuts work even when no control has keyboard focus.
bool MainWindow::keyPressed(const juce::KeyPress& key)
{
    if (auto* main = dynamic_cast<MainComponent*>(getContentComponent()))
        return main->keyPressed(key);
    return DocumentWindow::keyPressed(key);
}

void MainWindow::setOverlayMode(bool shouldBeOverlay)
{
    if (shouldBeOverlay == overlayMode && isOnDesktop())
        return;

    // Keep the meters exactly where they are on screen while the frame changes.
    const auto* content = getContentComponent();
    const auto contentOnScreen = content != nullptr && isOnDesktop() ? content->getScreenBounds()
                                                                     : juce::Rectangle<int>();

    overlayMode = shouldBeOverlay;
    setUsingNativeTitleBar(! overlayMode);
    setTitleBarHeight(overlayMode ? 0 : 26);

    if (! contentOnScreen.isEmpty())
        setBounds(getContentComponentBorder().addedTo(contentOnScreen));

    if (auto* main = dynamic_cast<MainComponent*>(getContentComponent()))
        main->overlayModeChanged(overlayMode);

    // Re-apply: recreating the native window resets these.
    setWindowOpacity(opacity);
    settings.setOverlayMode(overlayMode);
}

void MainWindow::setPinnedOnTop(bool shouldBeOnTop)
{
    setAlwaysOnTop(shouldBeOnTop);
    settings.setAlwaysOnTop(shouldBeOnTop);

    if (auto* main = dynamic_cast<MainComponent*>(getContentComponent()))
        main->windowStateChanged();
}

void MainWindow::setWindowOpacity(float newOpacity)
{
    opacity = juce::jlimit(0.2f, 1.0f, newOpacity);
    setAlpha(opacity);
    settings.setWindowOpacity(opacity);
}

void MainWindow::beginDrag(const juce::MouseEvent& e)
{
    dragger.startDraggingComponent(this, e.getEventRelativeTo(this));
}

void MainWindow::continueDrag(const juce::MouseEvent& e)
{
    dragger.dragComponent(this, e.getEventRelativeTo(this), nullptr);
}

} // namespace sonarc
