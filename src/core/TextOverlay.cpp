#include "core/TextOverlay.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace qr
{

namespace
{
struct Name
{
    TextPosition position;
    const char* name;
};
constexpr Name kNames[] = {
    {TextPosition::TopLeft, "top-left"},       {TextPosition::TopCenter, "top-center"},
    {TextPosition::TopRight, "top-right"},     {TextPosition::BottomLeft, "bottom-left"},
    {TextPosition::BottomCenter, "bottom-center"}, {TextPosition::BottomRight, "bottom-right"},
};
} // namespace

QString toString(TextPosition p)
{
    for(const Name& n : kNames)
        if(n.position == p)
            return n.name;
    return "bottom-right";
}

TextPosition textPositionFromString(const QString& s)
{
    for(const Name& n : kNames)
        if(s == QLatin1String(n.name))
            return n.position;
    return TextPosition::BottomRight;
}

QRect placeText(TextPosition position, QSize frame, QSize box, int margin)
{
    int x = margin;
    int y = margin;
    switch(position)
    {
        case TextPosition::TopLeft:
        case TextPosition::BottomLeft: x = margin; break;
        case TextPosition::TopCenter:
        case TextPosition::BottomCenter: x = (frame.width() - box.width()) / 2; break;
        case TextPosition::TopRight:
        case TextPosition::BottomRight: x = frame.width() - box.width() - margin; break;
    }
    switch(position)
    {
        case TextPosition::TopLeft:
        case TextPosition::TopCenter:
        case TextPosition::TopRight: y = margin; break;
        default: y = frame.height() - box.height() - margin; break;
    }
    return {QPoint(x, y), box};
}

QImage renderTextOverlay(const TextOverlay& overlay, QSize frame)
{
    QImage img(frame, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    if(overlay.isEmpty() || frame.isEmpty())
        return img;

    QFont font;
    font.setFamilies({"DejaVu Sans", "Noto Sans", "Sans Serif"});
    font.setBold(true);
    font.setPixelSize(std::max(4, overlay.fontSize));
    const QFontMetrics fm(font);
    const QString text = overlay.text.trimmed();

    const int pad = overlay.background ? std::max(2, overlay.fontSize * 35 / 100) : 0;
    const int margin = std::max(2, overlay.fontSize * 60 / 100);
    const QRect textBounds = fm.boundingRect(QRect(0, 0, frame.width(), frame.height()), Qt::AlignLeft, text);
    const QSize box = textBounds.size() + QSize(2 * pad, 2 * pad);
    const QRect boxRect = placeText(overlay.position, frame, box, margin);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(font);
    const Qt::Alignment align = Qt::AlignVCenter | (overlay.position == TextPosition::TopLeft ||
                                                            overlay.position == TextPosition::BottomLeft
                                                        ? Qt::AlignLeft
                                                        : (overlay.position == TextPosition::TopCenter ||
                                                                   overlay.position == TextPosition::BottomCenter
                                                               ? Qt::AlignHCenter
                                                               : Qt::AlignRight));
    const QRect textRect = boxRect.adjusted(pad, pad, -pad, -pad);
    if(overlay.background)
    {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 150));
        p.drawRoundedRect(boxRect, pad * 0.6, pad * 0.6);
    }
    else
    {
        // No box: a soft dark outline keeps light text readable on light content.
        QPainterPath path;
        const QStringList lines = text.split('\n');
        for(int i = 0; i < lines.size(); ++i)
        {
            const int w = fm.horizontalAdvance(lines[i]);
            int x = textRect.left();
            if(align & Qt::AlignHCenter)
                x = textRect.left() + (textRect.width() - w) / 2;
            else if(align & Qt::AlignRight)
                x = textRect.right() + 1 - w;
            path.addText(x, textRect.top() + fm.ascent() + i * fm.lineSpacing(), font, lines[i]);
        }
        p.setPen(QPen(QColor(0, 0, 0, 170), std::max(2.0, overlay.fontSize / 8.0), Qt::SolidLine, Qt::RoundCap,
                      Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
    p.setPen(overlay.color);
    p.drawText(textRect, align, text);
    return img;
}

} // namespace qr
