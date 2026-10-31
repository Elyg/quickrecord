#include "app/Platform.h"

#include <QGuiApplication>
#include <QScreen>
#include <QWidget>

#include <qt_windows.h>

#include <cwchar>
#include <dwmapi.h>

// Qt makes the process per-monitor DPI aware, and so is ffmpeg.exe (its manifest): everything
// Win32 says here, and what gdigrab takes, is in native pixels of the virtual desktop.
namespace qr::platform
{

namespace
{
// EnumWindows also lists the desktop and the taskbar, which aren't windows to a user.
bool isShellWindow(HWND hwnd)
{
    wchar_t name[32] = {};
    GetClassNameW(hwnd, name, 32);
    for(const wchar_t* shell : {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"})
        if(wcscmp(name, shell) == 0)
            return true;
    return false;
}

BOOL CALLBACK addWindow(HWND hwnd, LPARAM rects)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if(pid == GetCurrentProcessId() || !IsWindowVisible(hwnd) || IsIconic(hwnd) || isShellWindow(hwnd))
        return TRUE;
    if(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
        return TRUE;
    // Store apps, and windows on another virtual desktop, count as visible while cloaked.
    DWORD cloaked = 0;
    if(SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return TRUE;
    // Not GetWindowRect: that includes the invisible resize borders.
    RECT r;
    if(FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
        return TRUE;
    if(r.right - r.left > 1 && r.bottom - r.top > 1)
        reinterpret_cast<QList<QRect>*>(rects)->append(QRect(r.left, r.top, r.right - r.left, r.bottom - r.top));
    return TRUE;
}
} // namespace

QString unsupportedReason()
{
    return {};
}

void init() {}

// A second start reaches the running instance through its pipe (main.cpp).
void onReopen(std::function<void()>) {}

QList<ScreenInfo> screens()
{
    QList<ScreenInfo> out;
    for(QScreen* s : QGuiApplication::screens())
        out << ScreenInfo{s->geometry(), s->devicePixelRatio()};
    return out;
}

QRect captureBounds(const QRect&)
{
    return QRect(GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                 GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
}

QImage grab(const QRect& physical)
{
    HDC screen = GetDC(nullptr);
    if(!screen)
        return {};
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, physical.width(), physical.height());
    QImage image;
    if(memory && bitmap)
    {
        HGDIOBJ previous = SelectObject(memory, bitmap);
        // CAPTUREBLT: layered windows (ours, and many others) are left out without it.
        const bool ok = BitBlt(memory, 0, 0, physical.width(), physical.height(), screen, physical.x(), physical.y(),
                               SRCCOPY | CAPTUREBLT);
        SelectObject(memory, previous);
        if(ok)
            image = QImage::fromHBITMAP(bitmap);
    }
    if(bitmap)
        DeleteObject(bitmap);
    if(memory)
        DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return image.isNull() ? image : image.convertToFormat(QImage::Format_RGB32);
}

QString screenAccessHint()
{
    return {};
}

QList<QRect> visibleWindowRects()
{
    // EnumWindows goes from the top of the Z order down.
    QList<QRect> out;
    EnumWindows(addWindow, reinterpret_cast<LPARAM>(&out));
    return out;
}

void setCaptureSource(CaptureParams& params, const QRect& physical)
{
    params.source = CaptureSource::GdiGrab;
    params.display = "desktop";
    params.physicalRect = physical;
}

void keepOnTop(QWidget* widget, const QRect&)
{
    // The stays-on-top hint is set at creation; this also puts it above what is topmost by now.
    SetWindowPos(reinterpret_cast<HWND>(widget->winId()), HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// Nothing to do: a hotkey or a tray click lets this process take the foreground, and so does
// the `quickrecord --toggle` that hands it over (main.cpp).
void activateApp() {}

} // namespace qr::platform
