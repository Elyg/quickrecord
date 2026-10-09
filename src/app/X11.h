#pragma once

#include <QImage>
#include <QList>
#include <QRect>
#include <QString>

typedef struct xcb_connection_t xcb_connection_t;

namespace qr::x11
{

xcb_connection_t* connection();
uint32_t rootWindow();

// The X display ffmpeg should grab from (e.g. ":0").
QString displayName();

// Size of the root window in physical pixels (x11grab can't grab outside it).
QRect rootGeometry();

// Pixels of the root window inside `physical` (clipped to the root), without the mouse
// cursor. Null image on failure.
QImage grabRoot(const QRect& physical);

// Visible top-level windows on the current desktop, top-most first, as physical
// rectangles including WM decorations and excluding GTK client-side shadows.
QList<QRect> visibleWindowRects();

} // namespace qr::x11
