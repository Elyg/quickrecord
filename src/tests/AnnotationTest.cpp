#include "core/Annotation.h"
#include "tests/GuiApp.h"

#include <QJsonObject>
#include <gtest/gtest.h>

using namespace qr;

namespace
{
QImage canvas(QColor color = Qt::white)
{
    QImage img(200, 100, QImage::Format_RGB32);
    img.fill(color);
    return img;
}

Annotation make(AnnotationTool tool, QList<QPointF> points, QColor color = Qt::red, int width = 4)
{
    Annotation a;
    a.tool = tool;
    a.points = std::move(points);
    a.color = color;
    a.width = width;
    return a;
}
} // namespace

TEST(Annotation, ToolNamesRoundTrip)
{
    for(AnnotationTool t : {AnnotationTool::Pen, AnnotationTool::Line, AnnotationTool::Arrow, AnnotationTool::Rect,
                            AnnotationTool::Ellipse, AnnotationTool::Marker, AnnotationTool::Pixelate,
                            AnnotationTool::Text})
        EXPECT_EQ(annotationToolFromString(toString(t)), t);
    EXPECT_EQ(annotationToolFromString("bogus"), AnnotationTool::Pen);
}

TEST(Annotation, JsonRoundTrip)
{
    Annotation a = make(AnnotationTool::Pen, {QPointF(1, 2), QPointF(3.5, 4.25), QPointF(5, 6)}, QColor(1, 2, 3, 4), 9);
    EXPECT_EQ(annotationFromJson(toJson(a)), a);
    Annotation t = make(AnnotationTool::Text, {QPointF(7, 8)});
    t.text = "two\nlines";
    EXPECT_EQ(annotationFromJson(toJson(t)), t);
}

TEST(Annotation, NothingToDrawLeavesImageAlone)
{
    const QImage img = canvas();
    EXPECT_EQ(applyAnnotations(img, {}), img);
    // Incomplete shapes are skipped rather than drawn wrongly.
    const QImage out = applyAnnotations(img, {make(AnnotationTool::Rect, {QPointF(10, 10)})});
    EXPECT_EQ(out.convertToFormat(QImage::Format_RGB32), img);
}

TEST(Annotation, RectIsOutlined)
{
    const QImage out = applyAnnotations(canvas(), {make(AnnotationTool::Rect, {QPointF(20, 20), QPointF(120, 80)})});
    EXPECT_EQ(out.pixelColor(20, 50), QColor(Qt::red)); // on the left edge
    EXPECT_EQ(out.pixelColor(70, 50), QColor(Qt::white)); // inside stays clear
    EXPECT_EQ(out.size(), QSize(200, 100));
}

TEST(Annotation, PenDrawsAlongTheStroke)
{
    const QImage out =
        applyAnnotations(canvas(), {make(AnnotationTool::Pen, {QPointF(10, 50), QPointF(100, 50), QPointF(190, 50)})});
    EXPECT_EQ(out.pixelColor(150, 50), QColor(Qt::red));
    EXPECT_EQ(out.pixelColor(150, 20), QColor(Qt::white));
}

TEST(Annotation, MarkerIsSeeThrough)
{
    const QImage out =
        applyAnnotations(canvas(), {make(AnnotationTool::Marker, {QPointF(10, 50), QPointF(190, 50)}, Qt::blue)});
    const QColor c = out.pixelColor(100, 50);
    EXPECT_GT(c.red(), 100); // white still shows through
    EXPECT_LT(c.red(), 255);
    EXPECT_EQ(c.blue(), 255);
}

TEST(Annotation, PixelateFlattensDetail)
{
    // Alternating 1px columns average out to a flat grey inside the area only.
    QImage img = canvas();
    for(int x = 0; x < img.width(); x += 2)
        for(int y = 0; y < img.height(); ++y)
            img.setPixelColor(x, y, Qt::black);
    const QImage out = applyAnnotations(img, {make(AnnotationTool::Pixelate, {QPointF(0, 0), QPointF(96, 96)})});
    const QColor a = out.pixelColor(40, 40), b = out.pixelColor(41, 40);
    EXPECT_NEAR(a.red(), b.red(), 2);
    EXPECT_GT(a.red(), 60);
    EXPECT_LT(a.red(), 200);
    EXPECT_EQ(out.pixelColor(150, 40), QColor(Qt::black)); // outside untouched
}

TEST(Annotation, ArrowAndTextDrawSomething)
{
    const QImage img = canvas();
    const QImage arrow = applyAnnotations(img, {make(AnnotationTool::Arrow, {QPointF(10, 50), QPointF(180, 50)})});
    EXPECT_EQ(arrow.pixelColor(100, 50), QColor(Qt::red));
    EXPECT_EQ(arrow.pixelColor(176, 50), QColor(Qt::red)); // inside the head

    guiApp();
    Annotation t = make(AnnotationTool::Text, {QPointF(5, 5)});
    t.text = "Hi";
    const QImage text = applyAnnotations(img, {t});
    EXPECT_NE(text.convertToFormat(QImage::Format_RGB32), img);
}

TEST(Annotation, HitTesting)
{
    const Annotation line = make(AnnotationTool::Line, {QPointF(10, 10), QPointF(110, 10)}, Qt::red, 4);
    EXPECT_TRUE(annotationHit(line, QPointF(60, 13), 2));   // within half width + tolerance
    EXPECT_FALSE(annotationHit(line, QPointF(60, 20), 2));
    EXPECT_FALSE(annotationHit(line, QPointF(130, 10), 2)); // past the end

    const Annotation box = make(AnnotationTool::Rect, {QPointF(100, 100), QPointF(20, 40)});
    EXPECT_TRUE(annotationHit(box, QPointF(60, 70), 2)); // inside counts
    EXPECT_FALSE(annotationHit(box, QPointF(150, 70), 2));

    const Annotation ellipse = make(AnnotationTool::Ellipse, {QPointF(0, 0), QPointF(100, 50)});
    EXPECT_TRUE(annotationHit(ellipse, QPointF(50, 25), 2));
    EXPECT_FALSE(annotationHit(ellipse, QPointF(3, 3), 1)); // bounding-box corner, outside the curve

    const Annotation pen = make(AnnotationTool::Pen, {QPointF(0, 0), QPointF(50, 50), QPointF(100, 0)});
    EXPECT_TRUE(annotationHit(pen, QPointF(75, 26), 2));
    EXPECT_FALSE(annotationHit(pen, QPointF(50, 10), 2)); // in the V, away from the stroke
}

TEST(Annotation, TopMostWinsAndMoving)
{
    const QList<Annotation> list = {make(AnnotationTool::Rect, {QPointF(0, 0), QPointF(100, 100)}),
                                    make(AnnotationTool::Rect, {QPointF(50, 50), QPointF(150, 150)})};
    EXPECT_EQ(annotationAt(list, QPointF(75, 75), 2), 1);
    EXPECT_EQ(annotationAt(list, QPointF(25, 25), 2), 0);
    EXPECT_EQ(annotationAt(list, QPointF(190, 190), 2), -1);

    const Annotation moved = translated(list[0], QPointF(10, -5));
    EXPECT_EQ(moved.points, (QList<QPointF>{QPointF(10, -5), QPointF(110, 95)}));
    EXPECT_EQ(annotationBounds(moved), annotationBounds(list[0]).translated(10, -5));
}

TEST(Annotation, TextBoundsFollowTheText)
{
    guiApp();
    Annotation t = make(AnnotationTool::Text, {QPointF(10, 20)});
    t.text = "short";
    const QRectF one = annotationBounds(t);
    t.text = "a much longer line\nand two";
    const QRectF two = annotationBounds(t);
    EXPECT_EQ(one.topLeft(), QPointF(10, 20));
    EXPECT_GT(two.width(), one.width());
    EXPECT_GT(two.height(), one.height());
}

TEST(Annotation, AnchoredTextSitsInItsCorner)
{
    guiApp();
    Annotation t = make(AnnotationTool::Text, {QPointF(0, 0)});
    t.text = "corner\nand two";
    t.anchor = TextPosition::BottomRight;
    EXPECT_EQ(annotationFromJson(toJson(t)), t);

    const QRect frame(100, 50, 400, 300); // e.g. a crop
    const QRectF placed = annotationBounds(placeAnchored({t}, frame)[0]);
    EXPECT_TRUE(QRectF(frame).contains(placed));
    EXPECT_NEAR(frame.right() + 1 - placed.right(), frame.bottom() + 1 - placed.bottom(), 1.0); // same margin
    EXPECT_LT(frame.right() + 1 - placed.right(), 40);

    t.anchor = TextPosition::TopLeft;
    EXPECT_GT(annotationBounds(placeAnchored({t}, frame)[0]).left(), frame.left());

    // Free text stays put, and moving anchored text frees it.
    const Annotation free = make(AnnotationTool::Text, {QPointF(7, 8)});
    EXPECT_EQ(placeAnchored({free}, frame)[0], free);
    EXPECT_FALSE(translated(t, QPointF(1, 1)).anchor.has_value());
}
