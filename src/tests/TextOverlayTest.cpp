#include "core/TextOverlay.h"

#include "tests/GuiApp.h"

#include <QGuiApplication>
#include <gtest/gtest.h>

using namespace qr;

TEST(TextOverlay, PlacementInAllSixPositions)
{
    const QSize frame(800, 600), box(200, 50);
    EXPECT_EQ(placeText(TextPosition::TopLeft, frame, box, 10), QRect(10, 10, 200, 50));
    EXPECT_EQ(placeText(TextPosition::TopCenter, frame, box, 10), QRect(300, 10, 200, 50));
    EXPECT_EQ(placeText(TextPosition::TopRight, frame, box, 10), QRect(590, 10, 200, 50));
    EXPECT_EQ(placeText(TextPosition::BottomLeft, frame, box, 10), QRect(10, 540, 200, 50));
    EXPECT_EQ(placeText(TextPosition::BottomCenter, frame, box, 10), QRect(300, 540, 200, 50));
    EXPECT_EQ(placeText(TextPosition::BottomRight, frame, box, 10), QRect(590, 540, 200, 50));
}

TEST(TextOverlay, PositionNamesRoundTrip)
{
    for(TextPosition p : {TextPosition::TopLeft, TextPosition::TopCenter, TextPosition::TopRight,
                          TextPosition::BottomLeft, TextPosition::BottomCenter, TextPosition::BottomRight})
        EXPECT_EQ(textPositionFromString(toString(p)), p);
    EXPECT_EQ(textPositionFromString("nonsense"), TextPosition::BottomRight);
}

TEST(TextOverlay, EmptyWhenBlank)
{
    TextOverlay o;
    o.text = "  ";
    EXPECT_TRUE(o.isEmpty());
    o.text = "hi";
    EXPECT_FALSE(o.isEmpty());
}

namespace
{
bool anyOpaque(const QImage& img, const QRect& area)
{
    for(int y = area.top(); y <= area.bottom(); ++y)
        for(int x = area.left(); x <= area.right(); ++x)
            if(qAlpha(img.pixel(x, y)) > 0)
                return true;
    return false;
}
} // namespace

TEST(TextOverlay, RendersOnlyInTheChosenCorner)
{
    guiApp();
    TextOverlay o;
    o.text = "Hello";
    o.fontSize = 24;
    o.position = TextPosition::BottomRight;
    const QImage img = renderTextOverlay(o, QSize(400, 300));
    ASSERT_EQ(img.size(), QSize(400, 300));
    EXPECT_TRUE(anyOpaque(img, QRect(200, 150, 200, 150)));
    EXPECT_FALSE(anyOpaque(img, QRect(0, 0, 200, 150)));

    o.position = TextPosition::TopLeft;
    o.background = false;
    const QImage top = renderTextOverlay(o, QSize(400, 300));
    EXPECT_TRUE(anyOpaque(top, QRect(0, 0, 200, 150)));
    EXPECT_FALSE(anyOpaque(top, QRect(200, 150, 200, 150)));
}

TEST(TextOverlay, EmptyTextIsFullyTransparent)
{
    guiApp();
    const QImage img = renderTextOverlay(TextOverlay{}, QSize(64, 48));
    EXPECT_FALSE(anyOpaque(img, img.rect()));
}
