#include "core/Geometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace qr
{

QRect toPhysical(const QRect& logical, const ScreenInfo& screen)
{
    const QPoint origin = screen.geometry.topLeft();
    const QPointF topLeft = QPointF(logical.topLeft() - origin) * screen.dpr + QPointF(origin);
    const int w = int(std::lround(logical.width() * screen.dpr));
    const int h = int(std::lround(logical.height() * screen.dpr));
    return {int(std::lround(topLeft.x())), int(std::lround(topLeft.y())), w, h};
}

QRect toLogical(const QRect& physical, const ScreenInfo& screen)
{
    const QPoint origin = screen.geometry.topLeft();
    const QPointF topLeft = QPointF(physical.topLeft() - origin) / screen.dpr + QPointF(origin);
    const int w = int(std::lround(physical.width() / screen.dpr));
    const int h = int(std::lround(physical.height() / screen.dpr));
    return {int(std::lround(topLeft.x())), int(std::lround(topLeft.y())), w, h};
}

QRect physicalGeometry(const ScreenInfo& screen)
{
    const QRect& g = screen.geometry;
    return {g.topLeft(),
            QSize(int(std::lround(g.width() * screen.dpr)), int(std::lround(g.height() * screen.dpr)))};
}

namespace
{
template <typename RectOf>
int screenAt(const QList<ScreenInfo>& screens, QPoint p, RectOf rectOf)
{
    int best = -1;
    qint64 bestDist = std::numeric_limits<qint64>::max();
    for(int i = 0; i < screens.size(); ++i)
    {
        const QRect r = rectOf(screens[i]);
        if(r.contains(p))
            return i;
        const qint64 dx = p.x() < r.left() ? r.left() - p.x() : (p.x() > r.right() ? p.x() - r.right() : 0);
        const qint64 dy = p.y() < r.top() ? r.top() - p.y() : (p.y() > r.bottom() ? p.y() - r.bottom() : 0);
        const qint64 d = dx * dx + dy * dy;
        if(d < bestDist)
        {
            bestDist = d;
            best = i;
        }
    }
    return best;
}
} // namespace

int screenAtLogical(const QList<ScreenInfo>& screens, QPoint logical)
{
    return screenAt(screens, logical, [](const ScreenInfo& s) { return s.geometry; });
}

int screenAtPhysical(const QList<ScreenInfo>& screens, QPoint physical)
{
    return screenAt(screens, physical, [](const ScreenInfo& s) { return physicalGeometry(s); });
}

QRect evenSize(const QRect& r)
{
    const int w = std::max(2, r.width() & ~1);
    const int h = std::max(2, r.height() & ~1);
    return {r.topLeft(), QSize(w, h)};
}

QRect rectFromPoints(QPoint a, QPoint b)
{
    // Not QRect(a, b).normalized(): for reversed corners that shrinks the rect by a pixel.
    return QRect(QPoint(std::min(a.x(), b.x()), std::min(a.y(), b.y())),
                 QPoint(std::max(a.x(), b.x()), std::max(a.y(), b.y())));
}

namespace
{
// std::clamp, but well-defined when lo > hi (lo wins).
int clampTo(int v, int lo, int hi)
{
    return std::max(lo, std::min(v, hi));
}
} // namespace

QRect dragRect(const QRect& start, int edges, QPoint delta, const QRect& bounds, int minSize)
{
    // Exclusive right/bottom, so widths are plain differences.
    const int bl = bounds.left(), bt = bounds.top();
    const int br = bl + bounds.width(), bb = bt + bounds.height();
    int l = start.left(), t = start.top();
    int r = l + start.width(), b = t + start.height();
    if(edges & EdgeMove)
        return start.translated(clampTo(delta.x(), bl - l, br - r), clampTo(delta.y(), bt - t, bb - b));
    if(edges & EdgeLeft)
        l = clampTo(l + delta.x(), bl, r - minSize);
    if(edges & EdgeRight)
        r = clampTo(r + delta.x(), l + minSize, br);
    if(edges & EdgeTop)
        t = clampTo(t + delta.y(), bt, b - minSize);
    if(edges & EdgeBottom)
        b = clampTo(b + delta.y(), t + minSize, bb);
    return {l, t, r - l, b - t};
}

QRect snapCropRect(const QRectF& r, QSize bounds, int minSize, int align)
{
    align = std::max(1, align);
    const auto down = [align](long v) { return int(v - v % align); };
    const int maxW = down(bounds.width());
    const int maxH = down(bounds.height());
    minSize = std::min({down(minSize), maxW, maxH});
    int w = std::clamp(down(std::lround(r.width())), minSize, maxW);
    int h = std::clamp(down(std::lround(r.height())), minSize, maxH);
    int x = std::clamp(down(std::lround(r.left())), 0, down(maxW - w));
    int y = std::clamp(down(std::lround(r.top())), 0, down(maxH - h));
    return {x, y, w, h};
}

} // namespace qr
