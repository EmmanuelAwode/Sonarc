#pragma once

#include <functional>

namespace sonarc::desktop {

// Small Win32 helpers for the main window. `nativeWindow` is the HWND from
// juce::ComponentPeer::getNativeHandle(); kept as void* so <windows.h> stays
// out of the UI code.

// Puts the window back at the top of the always-on-top band without activating it.
void bringToTopmost(void* nativeWindow);

// True if a window belonging to another process is covering the middle of ours.
bool isCoveredByAnotherApp(void* nativeWindow);

// A Windows "application desktop toolbar": the window is docked to a screen
// edge across the full monitor width, and Windows reserves that strip so
// maximised windows sit beside it instead of under it (like the taskbar).
class AppBar
{
public:
    enum class Edge { Top, Bottom };

    // Physical-pixel rectangle the window must occupy.
    using PlaceCallback = std::function<void(int left, int top, int right, int bottom)>;

    AppBar() = default;
    ~AppBar();
    AppBar(const AppBar&) = delete;
    AppBar& operator=(const AppBar&) = delete;

    // Registers and positions the bar; `thickness` is in physical pixels.
    bool dock(void* nativeWindow, Edge edge, int thickness, PlaceCallback onPlace);
    void undock();
    bool isDocked() const noexcept { return window != nullptr; }

    // While a fullscreen app (game, video) is open on this monitor the bar
    // steps out of the way and must not be forced back on top.
    bool isFullscreenAppActive() const noexcept { return fullscreenApp; }

    void setThickness(int thickness);

    // Internal: called from the window subclass for app-bar notifications.
    void handleNotification(unsigned long long code, long long data);
    void reposition();

private:
    void* window = nullptr;
    Edge edge = Edge::Top;
    int thickness = 100;
    bool fullscreenApp = false;
    PlaceCallback place;
};

} // namespace sonarc::desktop
