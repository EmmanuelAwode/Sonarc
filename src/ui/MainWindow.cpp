#include "ui/MainWindow.h"
#include "ui/MainComponent.h"
#include "ui/Theme.h"

namespace sonarc {

namespace {

MainWindow::StickEdge edgeFromSetting(const juce::String& value)
{
    return value == "top" ? MainWindow::StickEdge::Top
         : value == "bottom" ? MainWindow::StickEdge::Bottom
                             : MainWindow::StickEdge::None;
}

const char* settingFromEdge(MainWindow::StickEdge edge)
{
    switch (edge)
    {
        case MainWindow::StickEdge::Top:    return "top";
        case MainWindow::StickEdge::Bottom: return "bottom";
        case MainWindow::StickEdge::None:   break;
    }
    return "off";
}

} // namespace

MainWindow::MainWindow(AudioEngine& engine, AppSettings& appSettings)
    : DocumentWindow("Sonarc", theme::background, DocumentWindow::allButtons),
      settings(appSettings)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new MainComponent(engine, settings), true);
    setResizable(true, false);
    setResizeLimits(240, 48, 10000, 10000);
    centreWithSize(1100, 320);

    opacity = settings.windowOpacity(); // read before setOverlayMode() re-applies it
    barHeight = settings.stickHeight();
    setOverlayMode(settings.overlayMode());
    setPinnedOnTop(settings.alwaysOnTop());
    setWindowOpacity(opacity);

    // Restore last, so the saved bounds win over the defaults above.
    if (const auto state = settings.windowState(); state.isNotEmpty())
        restoreWindowStateFromString(state);

    setVisible(true);
    getContentComponent()->grabKeyboardFocus();

    setStickEdge(edgeFromSetting(settings.stickMode()));
    startTimer(750);
}

MainWindow::~MainWindow()
{
    stopTimer();
    appBar.undock(); // while our window still exists
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

void MainWindow::notifyContent()
{
    if (auto* main = dynamic_cast<MainComponent*>(getContentComponent()))
        main->windowStateChanged();
}

void MainWindow::setOverlayMode(bool shouldBeOverlay)
{
    // Leaving the frameless look while docked means leaving stick mode.
    if (! shouldBeOverlay && isStuck())
    {
        setStickEdge(StickEdge::None);
        return;
    }

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
    setAlwaysOnTop(pinned || isStuck());
    if (! isStuck()) // stick mode forces overlay; remember the user's own choice
        settings.setOverlayMode(overlayMode);
}

void MainWindow::setPinnedOnTop(bool shouldBeOnTop)
{
    pinned = shouldBeOnTop;
    appBar.setStayOverFullscreen(pinned);
    setAlwaysOnTop(pinned || isStuck());
    if (pinned && getPeer() != nullptr)
        desktop::bringToTopmost(getPeer()->getNativeHandle());
    settings.setAlwaysOnTop(pinned);
    notifyContent();
}

void MainWindow::setStickEdge(StickEdge edge)
{
    if (edge == stuckTo)
        return;

    appBar.undock();

    if (edge == StickEdge::None)
    {
        stuckTo = StickEdge::None;
        setResizable(true, false);
        setOverlayMode(overlayBeforeStick);
        if (! boundsBeforeStick.isEmpty())
            setBounds(boundsBeforeStick);
        setAlwaysOnTop(pinned);
    }
    else
    {
        if (! isStuck())
        {
            boundsBeforeStick = getBounds();
            overlayBeforeStick = overlayMode;
        }

        stuckTo = edge;
        setOverlayMode(true);     // may recreate the native window, so do it before docking
        setResizable(false, false);
        setAlwaysOnTop(true);
        dock();
    }

    settings.setStickMode(settingFromEdge(stuckTo));
    notifyContent();
}

void MainWindow::setStickHeight(int logicalPixels)
{
    barHeight = juce::jlimit(48, 400, logicalPixels);
    settings.setStickHeight(barHeight);
    if (isStuck())
        dock();
}

void MainWindow::dock()
{
    auto* peer = getPeer();
    if (peer == nullptr)
        return;

    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = displays.getDisplayForRect(getScreenBounds());
    const double scale = display != nullptr ? display->scale : 1.0;

    appBar.dock(peer->getNativeHandle(),
                stuckTo == StickEdge::Bottom ? desktop::AppBar::Edge::Bottom : desktop::AppBar::Edge::Top,
                juce::roundToInt(barHeight * scale),
                [this](int left, int top, int right, int bottom)
                {
                    const auto physical = juce::Rectangle<int>(left, top, right - left, bottom - top);
                    setBounds(juce::Desktop::getInstance().getDisplays().physicalToLogical(physical));
                });
}

void MainWindow::timerCallback()
{
    // Watchdog: if another app has covered us while pinned or docked, step back on top.
    // Skipped while one of our own menus is open, and while a fullscreen app owns the
    // screen unless the user pinned Sonarc on top (e.g. a bar over a fullscreen video).
    if (! (pinned || isStuck()) || (appBar.isFullscreenAppActive() && ! pinned)
        || juce::Component::getCurrentlyModalComponent() != nullptr)
        return;

    if (auto* peer = getPeer(); peer != nullptr && desktop::isCoveredByAnotherApp(peer->getNativeHandle()))
        desktop::bringToTopmost(peer->getNativeHandle());
}

void MainWindow::setWindowOpacity(float newOpacity)
{
    opacity = juce::jlimit(0.2f, 1.0f, newOpacity);
    setAlpha(opacity);
    settings.setWindowOpacity(opacity);
}

juce::String MainWindow::windowStateForSaving()
{
    return isStuck() && ! boundsBeforeStick.isEmpty() ? boundsBeforeStick.toString() : getWindowStateAsString();
}

void MainWindow::beginDrag(const juce::MouseEvent& e)
{
    if (! isStuck())
        dragger.startDraggingComponent(this, e.getEventRelativeTo(this));
}

void MainWindow::continueDrag(const juce::MouseEvent& e)
{
    if (! isStuck())
        dragger.dragComponent(this, e.getEventRelativeTo(this), nullptr);
}

} // namespace sonarc
