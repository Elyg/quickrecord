#include "app/Icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace qr::icons
{

namespace
{
// Rounded dark tile with viewfinder corners and a centre dot — same motif as icons/quickrecord.svg.
QIcon draw(const QColor& dot, bool ring)
{
    QIcon icon;
    for(int size : {16, 22, 24, 32, 48, 64, 128})
    {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal s = size / 256.0;
        p.scale(s, s);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#20232a"));
        p.drawRoundedRect(QRectF(16, 16, 224, 224), 52, 52);

        QPen corner(QColor("#e8e8e8"), 18, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(corner);
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        path.moveTo(60, 92), path.lineTo(60, 60), path.lineTo(92, 60);
        path.moveTo(164, 60), path.lineTo(196, 60), path.lineTo(196, 92);
        path.moveTo(196, 164), path.lineTo(196, 196), path.lineTo(164, 196);
        path.moveTo(92, 196), path.lineTo(60, 196), path.lineTo(60, 164);
        p.drawPath(path);

        if(ring)
        {
            p.setPen(QPen(dot, 18));
            p.drawEllipse(QPointF(128, 128), 38, 38);
        }
        else
        {
            p.setPen(Qt::NoPen);
            p.setBrush(dot);
            p.drawEllipse(QPointF(128, 128), 44, 44);
        }
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}
} // namespace

QIcon app()
{
    return draw(QColor("#e5484d"), false);
}

QIcon trayIdle()
{
    return draw(QColor("#b8bcc4"), true);
}

QIcon trayRecording()
{
    return draw(QColor("#ff3b3f"), false);
}

QIcon trayPaused()
{
    return draw(QColor("#f5a524"), false);
}

QIcon trayBusy()
{
    return draw(QColor("#3e9bff"), true);
}

QIcon swatch(const QColor& color)
{
    QPixmap pm(36, 36);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(128, 128, 128, 160), 2));
    p.setBrush(color);
    p.drawRoundedRect(QRectF(1, 1, 34, 34), 8, 8);
    p.end();
    return QIcon(pm);
}

} // namespace qr::icons
