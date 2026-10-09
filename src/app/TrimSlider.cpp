#include "app/TrimSlider.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace qr
{

namespace
{
constexpr qint64 kMinRangeMs = 100;
constexpr double kHandleW = 10.0;
const QColor kAccent("#ff3b3f");
} // namespace

TrimSlider::TrimSlider(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(36);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
}

void TrimSlider::setDuration(qint64 ms)
{
    m_duration = std::max<qint64>(0, ms);
    m_start = 0;
    m_end = m_duration;
    update();
    emit rangeChanged(m_start, m_end);
}

void TrimSlider::setRange(qint64 start, qint64 end)
{
    if(m_duration <= 2 * kMinRangeMs)
        return;
    m_start = std::clamp(start, qint64(0), m_duration - kMinRangeMs);
    m_end = std::clamp(end, m_start + kMinRangeMs, m_duration);
    update();
    emit rangeChanged(m_start, m_end);
}

void TrimSlider::setPosition(qint64 ms)
{
    m_pos = ms;
    update();
}

QRectF TrimSlider::track() const
{
    return QRectF(kHandleW, 8, width() - 2 * kHandleW, height() - 16);
}

double TrimSlider::xFor(qint64 ms) const
{
    const QRectF t = track();
    return m_duration > 0 ? t.left() + t.width() * double(ms) / double(m_duration) : t.left();
}

qint64 TrimSlider::msFor(double x) const
{
    const QRectF t = track();
    const double f = std::clamp((x - t.left()) / std::max(1.0, t.width()), 0.0, 1.0);
    return qint64(std::llround(f * double(m_duration)));
}

void TrimSlider::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF t = track();
    const QColor base = palette().color(QPalette::Mid);

    p.setPen(Qt::NoPen);
    p.setBrush(base);
    p.drawRoundedRect(t, 4, 4);

    const double xs = xFor(m_start), xe = xFor(m_end);
    QColor sel = kAccent;
    sel.setAlpha(70);
    p.setBrush(sel);
    p.drawRect(QRectF(xs, t.top(), xe - xs, t.height()));

    p.setBrush(kAccent);
    p.drawRoundedRect(QRectF(xs - kHandleW, t.top() - 4, kHandleW, t.height() + 8), 3, 3);
    p.drawRoundedRect(QRectF(xe, t.top() - 4, kHandleW, t.height() + 8), 3, 3);

    const double xp = xFor(m_pos);
    p.setPen(QPen(palette().color(QPalette::WindowText), 2));
    p.drawLine(QPointF(xp, 2), QPointF(xp, height() - 2));
}

void TrimSlider::mousePressEvent(QMouseEvent* e)
{
    if(e->button() != Qt::LeftButton || m_duration <= 0)
        return;
    const double x = e->position().x();
    const double ds = std::abs(x - (xFor(m_start) - kHandleW / 2));
    const double de = std::abs(x - (xFor(m_end) + kHandleW / 2));
    if(ds <= kHandleW && ds <= de)
        m_drag = Drag::Start;
    else if(de <= kHandleW)
        m_drag = Drag::End;
    else
    {
        m_drag = Drag::Seek;
        emit seekRequested(std::clamp(msFor(x), m_start, m_end));
    }
}

void TrimSlider::mouseMoveEvent(QMouseEvent* e)
{
    if(m_duration <= 2 * kMinRangeMs)
        return;
    const double x = e->position().x();
    switch(m_drag)
    {
        case Drag::Start:
            m_start = std::clamp(msFor(x + kHandleW / 2), qint64(0), m_end - kMinRangeMs);
            update();
            emit rangeChanged(m_start, m_end);
            emit seekRequested(m_start);
            break;
        case Drag::End:
            m_end = std::clamp(msFor(x - kHandleW / 2), m_start + kMinRangeMs, m_duration);
            update();
            emit rangeChanged(m_start, m_end);
            emit seekRequested(m_end);
            break;
        case Drag::Seek: emit seekRequested(std::clamp(msFor(x), m_start, m_end)); break;
        case Drag::None: break;
    }
}

void TrimSlider::mouseReleaseEvent(QMouseEvent*)
{
    m_drag = Drag::None;
}

} // namespace qr
