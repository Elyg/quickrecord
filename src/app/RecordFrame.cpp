#include "app/RecordFrame.h"

#include <QPainter>
#include <QRegion>
#include <algorithm>

namespace qr
{

RecordFrame::RecordFrame(const QRect& logicalRegion)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint |
                           Qt::Tool | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    m_region = logicalRegion;
    setGeometry(logicalRegion.adjusted(-kBorder, -kBorder, kBorder, kBorder));
    updateMask();
}

QRect RecordFrame::regionLocal() const
{
    return QRect(mapFromGlobal(m_region.topLeft()), m_region.size());
}

QRegion RecordFrame::ring() const
{
    // The 2px gap absorbs logical→physical rounding at fractional scale factors.
    return QRegion(rect()).subtracted(QRegion(regionLocal().adjusted(-2, -2, 2, 2)));
}

void RecordFrame::setPaused(bool paused)
{
    m_paused = paused;
    update();
}

void RecordFrame::setCountdown(int seconds)
{
    m_countdown = seconds;
    updateMask();
    update();
}

void RecordFrame::resizeEvent(QResizeEvent*)
{
    updateMask();
}

void RecordFrame::moveEvent(QMoveEvent*)
{
    updateMask();
}

void RecordFrame::updateMask()
{
    if(m_countdown > 0)
    {
        clearMask();
        return;
    }
    // Only the ring is part of the window. With nothing left of it (pushed entirely over
    // the region), the window stays unmasked but paints nothing.
    const QRegion r = ring();
    if(r.isEmpty())
        clearMask();
    else
        setMask(r);
}

void RecordFrame::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    const QRect region = regionLocal();
    const QColor color = m_paused ? QColor("#f5a524") : QColor("#ff3b3f");
    QPen pen(color, 3, Qt::DashLine);
    pen.setDashPattern({4, 3});
    p.save();
    p.setClipRegion(ring());
    p.setPen(pen);
    p.drawRect(QRectF(region.adjusted(-kBorder, -kBorder, kBorder, kBorder)).adjusted(1.5, 1.5, -1.5, -1.5));
    p.restore();

    if(m_countdown > 0)
    {
        const int d = std::min({160, region.width(), region.height()});
        QRect circle(0, 0, d, d);
        circle.moveCenter(region.center());
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(20, 20, 24, 200));
        p.drawEllipse(circle);
        QFont f = font();
        f.setPixelSize(std::max(12, d / 2));
        f.setBold(true);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(circle, Qt::AlignCenter, QString::number(m_countdown));
    }
}

} // namespace qr
