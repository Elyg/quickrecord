#pragma once

#include "core/FfmpegArgs.h"
#include "core/Geometry.h"

#include <QImage>
#include <QList>
#include <QRect>
#include <QString>
#include <functional>

class QWidget;

// What differs between X11 (PlatformX11.cpp) and macOS (PlatformMac.mm). Rectangles are
// "physical" as defined in core/Geometry.h, using the ScreenInfo list from screens().
namespace qr::platform
{

// Call once the QApplication exists. Non-empty: why we can't run here (e.g. Wayland).
QString unsupportedReason();
// Process setup after the QApplication exists (macOS: ask for screen access).
void init();
// Calls `callback` when the app is opened again while running (macOS: a click on the Dock
// icon, or launching it from Finder/Spotlight). Never called on X11.
void onReopen(std::function<void()> callback);

QList<ScreenInfo> screens();

// Area a capture of `physical` can come from: the X root window, or on macOS the display
// under the centre of `physical` (avfoundation records one display at a time).
QRect captureBounds(const QRect& physical);

// Pixels inside `physical` (within captureBounds), without the mouse cursor. Null on failure.
QImage grab(const QRect& physical);
// Hint for when grab() or recording sees nothing, e.g. a missing permission. May be empty.
QString screenAccessHint();

// Visible top-level windows, top-most first, as physical rectangles.
QList<QRect> visibleWindowRects();

// Points `params` (source, display, physicalRect) at `physical` (within captureBounds).
void setCaptureSource(CaptureParams& params, const QRect& physical);

// Keeps a shown frameless helper (picker, record frame/bar) above everything, including the
// menu bar, on every Space, and visible while other apps are active. A non-empty `geometry`
// (global logical) is applied as is: Qt and AppKit otherwise squeeze new windows into the
// area below the menu bar. No-op on X11, where the window flags do all of this.
void keepOnTop(QWidget* widget, const QRect& geometry = {});
// Brings our app to the front so its newest window gets the keyboard. No-op on X11.
void activateApp();

} // namespace qr::platform
