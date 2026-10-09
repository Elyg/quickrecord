#pragma once

#include <QList>
#include <QPoint>
#include <QRect>

namespace qr
{

// A screen as Qt reports it: logical geometry plus its device pixel ratio.
// On X11, Qt keeps a screen's top-left in native pixels and scales only the size,
// so native = (logical - origin) * dpr + origin, with origin = geometry.topLeft().
struct ScreenInfo
{
    QRect geometry;
    qreal dpr = 1.0;
};

QRect toPhysical(const QRect& logical, const ScreenInfo& screen);
QRect toLogical(const QRect& physical, const ScreenInfo& screen);
QRect physicalGeometry(const ScreenInfo& screen);

// Index of the screen containing the point, or the nearest one; -1 if empty.
int screenAtLogical(const QList<ScreenInfo>& screens, QPoint logical);
int screenAtPhysical(const QList<ScreenInfo>& screens, QPoint physical);

// x264/yuv420p need even dimensions: shrink to even, min 2x2, keep top-left.
QRect evenSize(const QRect& r);

// Normalised rect between two drag points (inclusive of both corners).
QRect rectFromPoints(QPoint a, QPoint b);

// Which parts of a rectangle a drag grabs.
enum RectEdge
{
    EdgeLeft = 1,
    EdgeRight = 2,
    EdgeTop = 4,
    EdgeBottom = 8,
    EdgeMove = 16, // the whole rectangle
};

// `start` with the grabbed `edges` dragged by `delta`: kept inside `bounds`, never smaller
// than `minSize` (an edge stops instead of crossing the opposite one), moved without resizing.
QRect dragRect(const QRect& start, int edges, QPoint delta, const QRect& bounds, int minSize);

// Crop rectangle: clamped inside `bounds`, at least `minSize` square, and with x/y/width/
// height multiples of `align` (2 for video: yuv420p chroma alignment; 1 for still images).
QRect snapCropRect(const QRectF& r, QSize bounds, int minSize = 16, int align = 2);

} // namespace qr
