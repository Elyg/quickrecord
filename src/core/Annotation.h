#pragma once

#include "core/TextOverlay.h"

#include <QColor>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <optional>

class QJsonObject;

namespace qr
{

enum class AnnotationTool
{
    Pen,
    Line,
    Arrow,
    Rect,
    Ellipse,
    Marker,   // wide, see-through freehand stroke
    Pixelate, // hides a rectangle behind big pixels
    Text,
};

QString toString(AnnotationTool tool); // "pen", "arrow", …
AnnotationTool annotationToolFromString(const QString& s);

// One mark drawn on a screenshot, in image pixels.
//   Pen / Marker: `points` is the stroke.
//   Line / Arrow / Rect / Ellipse / Pixelate: the two drag corners/ends, points[0] → points[1].
//   Text: points[0] is the top-left of the text. With an `anchor` it sits in that corner of the
//   frame instead (placeAnchored() keeps points[0] there) and its lines align to that side.
struct Annotation
{
    AnnotationTool tool = AnnotationTool::Pen;
    QColor color = QColor("#ff3b3f");
    int width = 4; // stroke width in image pixels; also sizes arrow heads, text and pixel blocks
    QList<QPointF> points;
    QString text;
    std::optional<TextPosition> anchor; // Text only

    bool operator==(const Annotation&) const = default;
};

// Derived sizes, shared by rendering and the editor.
int annotationFontPixels(int width); // Text tool
int pixelateBlock(int width);        // Pixelate tool

QJsonObject toJson(const Annotation& a);
Annotation annotationFromJson(const QJsonObject& o);

// Area an annotation covers, stroke width included (image pixels).
QRectF annotationBounds(const Annotation& a);

// Whether `pos` grabs the annotation: within `tolerance` of a stroke or line, or anywhere
// inside a box, ellipse, pixelated area or text.
bool annotationHit(const Annotation& a, QPointF pos, qreal tolerance);

// Index of the top-most annotation under `pos`, or -1.
int annotationAt(const QList<Annotation>& annotations, QPointF pos, qreal tolerance);

// Moved by `delta`; an anchored text is placed by hand from then on.
Annotation translated(Annotation a, QPointF delta);

// Puts each anchored text in its corner of `frame` (image pixels, e.g. the crop), as far from
// the edges as a caption of that size would be.
QList<Annotation> placeAnchored(QList<Annotation> annotations, const QRect& frame);

// `image` with the annotations painted on in order (Pixelate sees everything below it).
QImage applyAnnotations(const QImage& image, const QList<Annotation>& annotations);

} // namespace qr
