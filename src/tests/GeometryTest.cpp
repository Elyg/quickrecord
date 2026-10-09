#include "core/Geometry.h"

#include <gtest/gtest.h>

using namespace qr;

TEST(Geometry, IdentityAtScaleOne)
{
    const ScreenInfo s{QRect(0, 0, 1920, 1080), 1.0};
    EXPECT_EQ(toPhysical(QRect(10, 20, 300, 200), s), QRect(10, 20, 300, 200));
    EXPECT_EQ(toLogical(QRect(10, 20, 300, 200), s), QRect(10, 20, 300, 200));
}

TEST(Geometry, ScalesRelativeToScreenOrigin)
{
    // Second monitor at native x=1920, 2x scale: logical width 1280 for a 2560 panel.
    const ScreenInfo s{QRect(1920, 0, 1280, 720), 2.0};
    EXPECT_EQ(toPhysical(QRect(1920, 0, 100, 50), s), QRect(1920, 0, 200, 100));
    EXPECT_EQ(toPhysical(QRect(2020, 10, 100, 50), s), QRect(2120, 20, 200, 100));
    EXPECT_EQ(toLogical(QRect(2120, 20, 200, 100), s), QRect(2020, 10, 100, 50));
    EXPECT_EQ(physicalGeometry(s), QRect(1920, 0, 2560, 1440));
}

TEST(Geometry, ScreenLookupPrefersContainingThenNearest)
{
    const QList<ScreenInfo> screens = {{QRect(0, 0, 1920, 1080), 1.0}, {QRect(1920, 0, 1280, 720), 2.0}};
    EXPECT_EQ(screenAtLogical(screens, QPoint(100, 100)), 0);
    EXPECT_EQ(screenAtLogical(screens, QPoint(2000, 100)), 1);
    EXPECT_EQ(screenAtLogical(screens, QPoint(3300, 100)), 1);
    EXPECT_EQ(screenAtPhysical(screens, QPoint(4000, 1000)), 1); // inside the 2560x1440 native area
    EXPECT_EQ(screenAtLogical({}, QPoint(0, 0)), -1);
}

TEST(Geometry, EvenSize)
{
    EXPECT_EQ(evenSize(QRect(5, 7, 301, 201)), QRect(5, 7, 300, 200));
    EXPECT_EQ(evenSize(QRect(0, 0, 1, 1)), QRect(0, 0, 2, 2));
}

TEST(Geometry, SnapCropRect)
{
    const QSize img(602, 402);
    EXPECT_EQ(snapCropRect(QRectF(11.2, 21.7, 301, 151), img), QRect(10, 22, 300, 150));
    // clamped inside the frame, keeping the size
    EXPECT_EQ(snapCropRect(QRectF(500, 350, 200, 100), img), QRect(402, 302, 200, 100));
    EXPECT_EQ(snapCropRect(QRectF(-30, -30, 100, 100), img), QRect(0, 0, 100, 100));
    // minimum size and never bigger than the frame
    EXPECT_EQ(snapCropRect(QRectF(0, 0, 3, 3), img), QRect(0, 0, 16, 16));
    EXPECT_EQ(snapCropRect(QRectF(0, 0, 5000, 5000), img), QRect(0, 0, 602, 402));
}

TEST(Geometry, SnapCropRectUnaligned)
{
    // Still images: odd sizes and positions are fine, and the whole odd-sized frame fits.
    const QSize img(601, 401);
    EXPECT_EQ(snapCropRect(QRectF(11.2, 21.7, 301, 151), img, 4, 1), QRect(11, 22, 301, 151));
    EXPECT_EQ(snapCropRect(QRectF(0, 0, 5000, 5000), img, 4, 1), QRect(0, 0, 601, 401));
    EXPECT_EQ(snapCropRect(QRectF(590, 390, 2, 2), img, 4, 1), QRect(590, 390, 4, 4));
}

TEST(Geometry, DragRect)
{
    const QRect bounds(0, 0, 1000, 800);
    const QRect r(100, 100, 200, 150);
    EXPECT_EQ(dragRect(r, EdgeRight | EdgeBottom, QPoint(50, -20), bounds, 8), QRect(100, 100, 250, 130));
    EXPECT_EQ(dragRect(r, EdgeLeft, QPoint(-30, 99), bounds, 8), QRect(70, 100, 230, 150));
    // an edge stops minSize before the opposite one, and at the bounds
    EXPECT_EQ(dragRect(r, EdgeLeft, QPoint(500, 0), bounds, 8), QRect(292, 100, 8, 150));
    EXPECT_EQ(dragRect(r, EdgeTop, QPoint(0, -500), bounds, 8), QRect(100, 0, 200, 250));
    EXPECT_EQ(dragRect(r, EdgeRight, QPoint(5000, 0), bounds, 8), QRect(100, 100, 900, 150));
    // moving keeps the size and stays inside
    EXPECT_EQ(dragRect(r, EdgeMove, QPoint(40, 10), bounds, 8), QRect(140, 110, 200, 150));
    EXPECT_EQ(dragRect(r, EdgeMove, QPoint(5000, -5000), bounds, 8), QRect(800, 0, 200, 150));
    // negative-origin bounds (monitor left of the primary)
    EXPECT_EQ(dragRect(QRect(-50, 0, 40, 40), EdgeMove, QPoint(-5000, 0), QRect(-1920, 0, 3840, 1080), 8),
              QRect(-1920, 0, 40, 40));
}

TEST(Geometry, RectFromPointsNormalises)
{
    EXPECT_EQ(rectFromPoints(QPoint(50, 60), QPoint(10, 20)), QRect(QPoint(10, 20), QPoint(50, 60)));
}
