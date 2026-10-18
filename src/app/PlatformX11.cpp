#include "app/Platform.h"

#include "app/X11.h"

#include <QGuiApplication>
#include <QScreen>

namespace qr::platform
{

QString unsupportedReason()
{
    if(QGuiApplication::platformName() != "xcb" || qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland")
        return "QuickRecord currently supports X11 sessions only.\n"
               "Log into an X11 session (e.g. Cinnamon on Xorg) to record.";
    return {};
}

void init() {}

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
    return x11::rootGeometry();
}

QImage grab(const QRect& physical)
{
    return x11::grabRoot(physical);
}

QString screenAccessHint()
{
    return {};
}

QList<QRect> visibleWindowRects()
{
    return x11::visibleWindowRects();
}

void setCaptureSource(CaptureParams& params, const QRect& physical)
{
    params.source = CaptureSource::X11Grab;
    params.display = x11::displayName();
    params.physicalRect = physical;
}

void keepOnTop(QWidget*, const QRect&) {}

void activateApp() {}

} // namespace qr::platform
