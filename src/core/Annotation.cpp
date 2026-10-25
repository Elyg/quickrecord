#include "core/Annotation.h"

#include <QFont>
#include <QFontMetrics>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>

namespace qr
{

namespace
{
struct Name
{
    AnnotationTool tool;
    const char* name;
};
constexpr Name kNames[] = {
    {AnnotationTool::Pen, "pen"},       {AnnotationTool::Line, "line"},         {AnnotationTool::Arrow, "arrow"},
    {AnnotationTool::Rect, "rect"},     {AnnotationTool::Ellipse, "ellipse"},   {AnnotationTool::Marker, "marker"},
    {AnnotationTool::Pixelate, "pixelate"}, {AnnotationTool::Text, "text"},
};

QPainterPath stroke(const QList<QPointF>& points)
{
    QPainterPath path;
    if(points.isEmpty())
        return path;
    path.moveTo(points.first());
    for(qsizetype i = 1; i < points.size(); ++i)
        path.lineTo(points[i]);
    if(points.size() == 1)
        path.lineTo(points.first() + QPointF(0.01, 0)); // a click still leaves a dot
    return path;
}

void drawArrow(QPainter& p, const Annotation& a)
{
    const QPointF from = a.points[0], to = a.points[1];
    const QLineF line(from, to);
    if(line.length() < 1)
        return;
    // Head scales with the stroke but never swallows a short arrow.
    const qreal head = std::min(std::max(10.0, a.width * 4.0), line.length() * 0.6);
    const qreal angle = qDegreesToRadians(line.angle());
    auto wing = [&](qreal deg) {
        const qreal r = angle + qDegreesToRadians(deg);
        return to + QPointF(-std::cos(r) * head, std::sin(r) * head);
    };
    QPolygonF tip;
    tip << to << wing(28) << wing(-28);
    // Stop the shaft inside the head so its square end doesn't poke out of the point.
    const QPointF shaftEnd = to + (from - to) * (head * 0.6 / line.length());
    p.setPen(QPen(a.color, a.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(from, shaftEnd);
    p.setPen(QPen(a.color, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(a.color);
    p.drawPolygon(tip);
    p.setBrush(Qt::NoBrush);
}

void pixelate(QPainter& p, QImage& canvas, const Annotation& a)
{
    const QRect r = QRectF(a.points[0], a.points[1]).normalized().toAlignedRect().intersected(canvas.rect());
    if(r.isEmpty())
        return;
    const int block = pixelateBlock(a.width);
    const QImage region = canvas.copy(r);
    const QImage small = region.scaled(std::max(1, r.width() / block), std::max(1, r.height() / block),
                                       Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    p.drawImage(r, small.scaled(r.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation));
}

QFont textFont(const Annotation& a)
{
    QFont font;
    font.setFamilies({"DejaVu Sans", "Noto Sans", "Sans Serif"});
    font.setBold(true);
    font.setPixelSize(annotationFontPixels(a.width));
    return font;
}

qreal distanceToSegment(QPointF p, QPointF a, QPointF b)
{
    const QPointF ab = b - a;
    const qreal len2 = QPointF::dotProduct(ab, ab);
    const qreal t = len2 > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / len2, 0.0, 1.0) : 0.0;
    const QPointF d = p - (a + ab * t);
    return std::hypot(d.x(), d.y());
}

qreal distanceToPolyline(QPointF p, const QList<QPointF>& points)
{
    if(points.size() == 1)
        return distanceToSegment(p, points[0], points[0]);
    qreal best = std::numeric_limits<qreal>::max();
    for(qsizetype i = 1; i < points.size(); ++i)
        best = std::min(best, distanceToSegment(p, points[i - 1], points[i]));
    return best;
}

bool isTwoPoint(AnnotationTool tool)
{
    return tool != AnnotationTool::Pen && tool != AnnotationTool::Marker && tool != AnnotationTool::Text;
}

// Stroke thickness as painted.
qreal paintedWidth(const Annotation& a)
{
    return a.tool == AnnotationTool::Marker ? a.width * 4.0 : a.width;
}

qreal widestLine(const QFontMetricsF& fm, const QStringList& lines)
{
    qreal w = 0;
    for(const QString& line : lines)
        w = std::max(w, fm.horizontalAdvance(line));
    return w;
}

void drawText(QPainter& p, const Annotation& a)
{
    const QFont font = textFont(a);
    const QFontMetricsF fm(font);
    QPainterPath path;
    const QStringList lines = a.text.split('\n');
    const qreal box = widestLine(fm, lines);
    for(qsizetype i = 0; i < lines.size(); ++i)
    {
        // Anchored on the right or in the centre, lines line up on that side like a caption's.
        const qreal spare = box - fm.horizontalAdvance(lines[i]);
        qreal x = 0;
        if(a.anchor == TextPosition::TopRight || a.anchor == TextPosition::BottomRight)
            x = spare;
        else if(a.anchor == TextPosition::TopCenter || a.anchor == TextPosition::BottomCenter)
            x = spare / 2;
        path.addText(a.points[0] + QPointF(x, fm.ascent() + i * fm.lineSpacing()), font, lines[i]);
    }
    // Dark outline under the fill keeps any colour readable on any background.
    p.setPen(QPen(QColor(0, 0, 0, 170), std::max(2.0, font.pixelSize() / 8.0), Qt::SolidLine, Qt::RoundCap,
                  Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.setPen(Qt::NoPen);
    p.setBrush(a.color);
    p.drawPath(path);
    p.setBrush(Qt::NoBrush);
}
} // namespace

QString toString(AnnotationTool tool)
{
    for(const Name& n : kNames)
        if(n.tool == tool)
            return n.name;
    return "pen";
}

AnnotationTool annotationToolFromString(const QString& s)
{
    for(const Name& n : kNames)
        if(s == QLatin1String(n.name))
            return n.tool;
    return AnnotationTool::Pen;
}

int annotationFontPixels(int width)
{
    return std::max(10, width * 6);
}

int pixelateBlock(int width)
{
    return std::max(6, width * 3);
}

QRectF annotationBounds(const Annotation& a)
{
    if(a.points.isEmpty() || (isTwoPoint(a.tool) && a.points.size() < 2))
        return {};
    if(a.tool == AnnotationTool::Text)
    {
        const QFontMetricsF fm(textFont(a));
        const QStringList lines = a.text.split('\n');
        return QRectF(a.points[0], QSizeF(widestLine(fm, lines), lines.size() * fm.lineSpacing()));
    }
    QRectF r;
    if(isTwoPoint(a.tool))
        r = QRectF(a.points[0], a.points[1]).normalized();
    else
    {
        qreal x0 = a.points[0].x(), y0 = a.points[0].y(), x1 = x0, y1 = y0;
        for(const QPointF& pt : a.points)
        {
            x0 = std::min(x0, pt.x()), y0 = std::min(y0, pt.y());
            x1 = std::max(x1, pt.x()), y1 = std::max(y1, pt.y());
        }
        r = QRectF(QPointF(x0, y0), QPointF(x1, y1));
    }
    qreal pad = paintedWidth(a) / 2;
    if(a.tool == AnnotationTool::Arrow)
        pad = std::max(pad, std::max(10.0, a.width * 4.0)); // the head sticks out sideways
    if(a.tool == AnnotationTool::Pixelate)
        pad = 0;
    return r.adjusted(-pad, -pad, pad, pad);
}

bool annotationHit(const Annotation& a, QPointF pos, qreal tolerance)
{
    const QRectF bounds = annotationBounds(a);
    if(bounds.isNull() || !bounds.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(pos))
        return false;
    const qreal reach = paintedWidth(a) / 2 + tolerance;
    switch(a.tool)
    {
        case AnnotationTool::Pen:
        case AnnotationTool::Marker: return distanceToPolyline(pos, a.points) <= reach;
        case AnnotationTool::Line: return distanceToSegment(pos, a.points[0], a.points[1]) <= reach;
        case AnnotationTool::Arrow:
            return distanceToSegment(pos, a.points[0], a.points[1]) <= std::max(reach, a.width * 2.0 + tolerance);
        case AnnotationTool::Ellipse:
        {
            const QRectF r = QRectF(a.points[0], a.points[1]).normalized().adjusted(-reach, -reach, reach, reach);
            if(r.width() <= 0 || r.height() <= 0)
                return false;
            const QPointF d = pos - r.center();
            const qreal nx = d.x() / (r.width() / 2), ny = d.y() / (r.height() / 2);
            return nx * nx + ny * ny <= 1.0;
        }
        case AnnotationTool::Rect:
        case AnnotationTool::Pixelate:
        case AnnotationTool::Text: return true; // inside the (padded) bounds
    }
    return false;
}

int annotationAt(const QList<Annotation>& annotations, QPointF pos, qreal tolerance)
{
    for(qsizetype i = annotations.size() - 1; i >= 0; --i)
        if(annotationHit(annotations[i], pos, tolerance))
            return int(i);
    return -1;
}

Annotation translated(Annotation a, QPointF delta)
{
    for(QPointF& pt : a.points)
        pt += delta;
    a.anchor.reset();
    return a;
}

QList<Annotation> placeAnchored(QList<Annotation> annotations, const QRect& frame)
{
    for(Annotation& a : annotations)
    {
        if(a.tool != AnnotationTool::Text || !a.anchor || a.points.isEmpty())
            continue;
        const QSizeF size = annotationBounds(a).size();
        const QSize box(int(std::ceil(size.width())), int(std::ceil(size.height())));
        const int margin = std::max(2, annotationFontPixels(a.width) * 60 / 100);
        a.points[0] = placeText(*a.anchor, frame.size(), box, margin).translated(frame.topLeft()).topLeft();
    }
    return annotations;
}

QJsonObject toJson(const Annotation& a)
{
    QJsonArray points;
    for(const QPointF& pt : a.points)
        points << pt.x() << pt.y();
    QJsonObject o{
        {"tool", toString(a.tool)},
        {"color", a.color.name(QColor::HexArgb)},
        {"width", a.width},
        {"points", points},
    };
    if(!a.text.isEmpty())
        o["text"] = a.text;
    if(a.anchor)
        o["anchor"] = toString(*a.anchor);
    return o;
}

Annotation annotationFromJson(const QJsonObject& o)
{
    Annotation a;
    a.tool = annotationToolFromString(o["tool"].toString());
    a.color = QColor(o["color"].toString("#ffff3b3f"));
    a.width = o["width"].toInt(a.width);
    const QJsonArray points = o["points"].toArray();
    for(qsizetype i = 0; i + 1 < points.size(); i += 2)
        a.points << QPointF(points[i].toDouble(), points[i + 1].toDouble());
    a.text = o["text"].toString();
    if(o.contains("anchor"))
        a.anchor = textPositionFromString(o["anchor"].toString());
    return a;
}

QImage applyAnnotations(const QImage& image, const QList<Annotation>& annotations)
{
    if(annotations.isEmpty() || image.isNull())
        return image;
    QImage out = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    for(const Annotation& a : annotations)
    {
        if(a.points.isEmpty() || (isTwoPoint(a.tool) && a.points.size() < 2) || a.width <= 0)
            continue;
        const QPen pen(a.color, a.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        switch(a.tool)
        {
            case AnnotationTool::Pen:
                p.setPen(pen);
                p.drawPath(stroke(a.points));
                break;
            case AnnotationTool::Marker:
            {
                QColor c = a.color;
                c.setAlphaF(0.4 * c.alphaF());
                p.setPen(QPen(c, a.width * 4, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
                p.drawPath(stroke(a.points));
                break;
            }
            case AnnotationTool::Line:
                p.setPen(pen);
                p.drawLine(a.points[0], a.points[1]);
                break;
            case AnnotationTool::Arrow: drawArrow(p, a); break;
            case AnnotationTool::Rect:
                p.setPen(QPen(a.color, a.width, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
                p.drawRect(QRectF(a.points[0], a.points[1]).normalized());
                break;
            case AnnotationTool::Ellipse:
                p.setPen(pen);
                p.drawEllipse(QRectF(a.points[0], a.points[1]).normalized());
                break;
            case AnnotationTool::Pixelate: pixelate(p, out, a); break;
            case AnnotationTool::Text: drawText(p, a); break;
        }
    }
    p.end();
    return out;
}

} // namespace qr
