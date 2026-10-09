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
    setGeometry(logicalRegion.adjusted(-kBorder, -kBorder, kBorder, kBorder));
    updateMask();
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

void RecordFrame::updateMask()
{
    if(m_countdown > 0)
    {
        clearMask();
        return;
    }
    // Only the outer ring is part of the window. Leave a 2px gap towards the capture
    // area to absorb logical→physical rounding at fractional scale factors.
    const QRect outer = rect();
    const QRect inner = outer.adjusted(kBorder, kBorder, -kBorder, -kBorder);
    setMask(QRegion(outer).subtracted(QRegion(inner.adjusted(-2, -2, 2, 2))));
}

void RecordFrame::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    const QColor color = m_paused ? QColor("#f5a524") : QColor("#ff3b3f");
    QPen pen(color, 3, Qt::DashLine);
    pen.setDashPattern({4, 3});
    p.setPen(pen);
    p.drawRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));

    if(m_countdown > 0)
    {
        const int d = std::min({160, width() - 2 * kBorder, height() - 2 * kBorder});
        QRect circle(0, 0, d, d);
        circle.moveCenter(rect().center());
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
