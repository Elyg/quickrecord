#include "app/LaserOverlay.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace qr
{

namespace
{
const QColor kLaser("#ff3b3f");
constexpr double kMinStep = 2.0;   // logical px; closer points are hand jitter
constexpr double kSmoothing = 0.5; // each point moves this far towards the mouse

// Curves through the midpoints between points: a smooth line with no corners.
QPainterPath smoothPath(const QList<QPointF>& points)
{
    QPainterPath path(points.first());
    if(points.size() == 1) // a click: a dot
    {
        path.lineTo(points.first() + QPointF(0.01, 0));
        return path;
    }
    for(int i = 1; i + 1 < points.size(); ++i)
        path.quadTo(points[i], (points[i] + points[i + 1]) / 2);
    path.lineTo(points.last());
    return path;
}
} // namespace

LaserOverlay::LaserOverlay(const QRect& logicalRegion, int fadeMs)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint |
                           Qt::Tool | Qt::WindowDoesNotAcceptFocus)
    , m_holdMs(std::max(0, fadeMs) * 2 / 5)
    , m_fadeMs(std::max(50, fadeMs - int(std::max(0, fadeMs) * 2 / 5)))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setCursor(Qt::CrossCursor);
    setGeometry(logicalRegion);
    m_clock.start();
    m_animation.setInterval(16);
    connect(&m_animation, &QTimer::timeout, this, &LaserOverlay::tick);
}

double LaserOverlay::opacity(const Stroke& s) const
{
    if(s.releasedMs < 0)
        return 1.0;
    const qint64 age = m_clock.elapsed() - s.releasedMs;
    return std::clamp(1.0 - double(age - m_holdMs) / double(m_fadeMs), 0.0, 1.0);
}

void LaserOverlay::tick()
{
    m_strokes.removeIf([this](const Stroke& s) { return opacity(s) <= 0.0; });
    if(m_strokes.isEmpty())
        m_animation.stop();
    update();
}

void LaserOverlay::mousePressEvent(QMouseEvent* e)
{
    if(e->button() == Qt::RightButton)
    {
        emit dismissed();
        return;
    }
    if(e->button() != Qt::LeftButton)
        return;
    m_strokes.append({{e->position()}, -1});
    m_animation.start();
    update();
}

void LaserOverlay::mouseMoveEvent(QMouseEvent* e)
{
    if(m_strokes.isEmpty() || m_strokes.last().releasedMs >= 0)
        return;
    QList<QPointF>& points = m_strokes.last().points;
    const QPointF last = points.last();
    const QPointF delta = e->position() - last;
    if(std::hypot(delta.x(), delta.y()) < kMinStep)
        return;
    points << last + delta * kSmoothing;
    update();
}

void LaserOverlay::mouseReleaseEvent(QMouseEvent* e)
{
    if(e->button() == Qt::LeftButton && !m_strokes.isEmpty() && m_strokes.last().releasedMs < 0)
        m_strokes.last().releasedMs = m_clock.elapsed();
}

void LaserOverlay::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    // Not fully transparent: macOS sends clicks on fully transparent pixels to the window below.
    // One unit of alpha is invisible in the recording.
    p.fillRect(rect(), QColor(0, 0, 0, 1));
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    for(const Stroke& s : std::as_const(m_strokes))
    {
        const double a = opacity(s);
        const QPainterPath path = smoothPath(s.points);
        // The stroke, with a thin bright centre.
        QColor core = kLaser;
        core.setAlphaF(a);
        p.setPen(QPen(core, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        p.setPen(QPen(QColor(255, 235, 235, int(200 * a)), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
    }
}

} // namespace qr
