#ifndef NOMINMAX
 #define NOMINMAX
#endif

#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>

#include "ui/WindowsDesktop.h"

namespace sonarc::desktop {

namespace {

constexpr UINT kAppBarMessage = WM_APP + 0x5a;
constexpr UINT_PTR kSubclassId = 0x50a4c;

HWND toHwnd(void* w) { return static_cast<HWND>(w); }

LRESULT CALLBACK appBarSubclass(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR refData)
{
    auto* bar = reinterpret_cast<AppBar*>(refData);

    switch (message)
    {
        case kAppBarMessage:
            bar->handleNotification(wParam, lParam);
            return 0;

        case WM_ACTIVATE:
        case WM_WINDOWPOSCHANGED:
        {
            // Required by the app-bar protocol so the shell keeps its z-order bookkeeping right.
            APPBARDATA data { sizeof(APPBARDATA) };
            data.hWnd = hwnd;
            SHAppBarMessage(message == WM_ACTIVATE ? ABM_ACTIVATE : ABM_WINDOWPOSCHANGED, &data);
            break;
        }

        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, appBarSubclass, kSubclassId);
            break;

        default:
            break;
    }

    return DefSubclassProc(hwnd, message, wParam, lParam);
}

} // namespace

void bringToTopmost(void* nativeWindow)
{
    if (nativeWindow != nullptr)
        SetWindowPos(toHwnd(nativeWindow), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

bool isCoveredByAnotherApp(void* nativeWindow)
{
    const auto hwnd = toHwnd(nativeWindow);
    RECT r;
    if (hwnd == nullptr || ! IsWindowVisible(hwnd) || IsIconic(hwnd) || ! GetWindowRect(hwnd, &r))
        return false;

    // Probe a few points; ignore our own windows (menus, tooltips).
    const DWORD ourProcess = GetCurrentProcessId();
    const LONG xs[] = { r.left + (r.right - r.left) / 4, (r.left + r.right) / 2, r.right - (r.right - r.left) / 4 };
    const LONG y = (r.top + r.bottom) / 2;

    for (const auto x : xs)
    {
        const HWND top = GetAncestor(WindowFromPoint({ x, y }), GA_ROOT);
        if (top == nullptr || top == hwnd)
            continue;

        DWORD process = 0;
        GetWindowThreadProcessId(top, &process);
        if (process != ourProcess)
            return true;
    }
    return false;
}

//==============================================================================
AppBar::~AppBar()
{
    undock();
}

bool AppBar::dock(void* nativeWindow, Edge newEdge, int newThickness, PlaceCallback onPlace)
{
    undock();

    APPBARDATA data { sizeof(APPBARDATA) };
    data.hWnd = toHwnd(nativeWindow);
    data.uCallbackMessage = kAppBarMessage;
    if (! SHAppBarMessage(ABM_NEW, &data))
        return false;

    if (! SetWindowSubclass(data.hWnd, appBarSubclass, kSubclassId, reinterpret_cast<DWORD_PTR>(this)))
    {
        SHAppBarMessage(ABM_REMOVE, &data);
        return false;
    }

    window = nativeWindow;
    edge = newEdge;
    thickness = newThickness;
    fullscreenApp = false;
    place = std::move(onPlace);
    reposition();
    return true;
}

void AppBar::undock()
{
    if (window == nullptr)
        return;

    APPBARDATA data { sizeof(APPBARDATA) };
    data.hWnd = toHwnd(window);
    SHAppBarMessage(ABM_REMOVE, &data);
    RemoveWindowSubclass(data.hWnd, appBarSubclass, kSubclassId);

    window = nullptr;
    place = nullptr;
    fullscreenApp = false;
}

void AppBar::setThickness(int newThickness)
{
    thickness = newThickness;
    if (window != nullptr)
        reposition();
}

void AppBar::reposition()
{
    if (window == nullptr)
        return;

    const auto hwnd = toHwnd(window);
    MONITORINFO monitor { sizeof(MONITORINFO) };
    if (! GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
        return;

    APPBARDATA data { sizeof(APPBARDATA) };
    data.hWnd = hwnd;
    data.uEdge = edge == Edge::Top ? ABE_TOP : ABE_BOTTOM;
    data.rc = monitor.rcMonitor;
    if (edge == Edge::Top)
        data.rc.bottom = data.rc.top + thickness;
    else
        data.rc.top = data.rc.bottom - thickness;

    // Let the shell move us clear of other bars (e.g. a taskbar on the same edge)...
    SHAppBarMessage(ABM_QUERYPOS, &data);
    if (edge == Edge::Top)
        data.rc.bottom = data.rc.top + thickness;
    else
        data.rc.top = data.rc.bottom - thickness;

    // ...then reserve the strip.
    SHAppBarMessage(ABM_SETPOS, &data);

    if (place)
        place(data.rc.left, data.rc.top, data.rc.right, data.rc.bottom);
}

void AppBar::handleNotification(unsigned long long code, long long data)
{
    switch (code)
    {
        case ABN_POSCHANGED: // taskbar moved, resolution changed, another bar appeared...
            reposition();
            break;

        case ABN_FULLSCREENAPP:
            // Step out of the way of fullscreen games and videos, come back afterwards.
            fullscreenApp = data != 0;
            SetWindowPos(toHwnd(window), fullscreenApp ? HWND_NOTOPMOST : HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            break;

        default:
            break;
    }
}

} // namespace sonarc::desktop
