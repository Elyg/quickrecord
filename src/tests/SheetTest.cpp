#include "core/Sheet.h"
#include "core/SlotText.h"

#include "tests/GuiApp.h"

#include <QJsonDocument>
#include <QPainter>
#include <gtest/gtest.h>

using namespace qr;

namespace
{
std::optional<QString> known(const QString& key)
{
    if(key == "n")
        return "3";
    if(key == "name")
        return "qs-1";
    return std::nullopt;
}

bool anyOpaque(const QImage& img, const QRect& area)
{
    for(int y = area.top(); y <= area.bottom(); ++y)
        for(int x = area.left(); x <= area.right(); ++x)
            if(qAlpha(img.pixel(x, y)) > 0)
                return true;
    return false;
}
} // namespace

TEST(SlotText, PlaceholdersResolveAndUnknownOnesStay)
{
    EXPECT_EQ(resolvePlaceholders("#{n} {name}", known), "#3 qs-1");
    EXPECT_EQ(resolvePlaceholders("{n}{n}", known), "33");
    EXPECT_EQ(resolvePlaceholders("{nope} {n} {", known), "{nope} 3 {");
    EXPECT_EQ(resolvePlaceholders("plain", known), "plain");
}

TEST(SlotText, AppendedStacksLinesPerSlot)
{
    SlotText every, mine;
    every[TextPosition::TopLeft] = "{n}";
    every[TextPosition::BottomRight] = "x";
    mine[TextPosition::TopLeft] = "login page";
    mine[TextPosition::TopRight] = "only here";
    const SlotText both = appended(every, mine);
    EXPECT_EQ(both[TextPosition::TopLeft], "{n}\nlogin page");
    EXPECT_EQ(both[TextPosition::TopRight], "only here");
    EXPECT_EQ(both[TextPosition::BottomRight], "x");
    EXPECT_TRUE(both[TextPosition::BottomLeft].isEmpty());
    EXPECT_TRUE(SlotText{}.isEmpty());
    EXPECT_FALSE(both.isEmpty());
}

TEST(SlotText, RowHeightGrowsWithLines)
{
    guiApp();
    TextOverlay style;
    SlotText t;
    EXPECT_EQ(slotRowHeight(t, true, style), 0);
    t[TextPosition::TopCenter] = "one";
    const int one = slotRowHeight(t, true, style);
    EXPECT_GT(one, style.fontSize);
    EXPECT_EQ(slotRowHeight(t, false, style), 0);
    t[TextPosition::TopRight] = "one\ntwo";
    EXPECT_GT(slotRowHeight(t, true, style), one);
}

TEST(SlotText, RendersInItsCorner)
{
    guiApp();
    QImage img(400, 300, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    SlotText t;
    t[TextPosition::BottomRight] = "Hello";
    {
        QPainter p(&img);
        renderSlots(p, QRect(0, 0, 400, 300), t, TextOverlay{});
    }
    EXPECT_TRUE(anyOpaque(img, QRect(200, 150, 200, 150)));
    EXPECT_FALSE(anyOpaque(img, QRect(0, 0, 200, 150)));
}

TEST(Sheet, AutoColumnsComesClosestTo16By10)
{
    const QSize wide(1600, 1000); // 16:10 cells
    EXPECT_EQ(autoColumns(1, wide, 0, 0), 1);
    EXPECT_EQ(autoColumns(4, wide, 0, 0), 2); // 2×2
    EXPECT_EQ(autoColumns(9, wide, 0, 0), 3); // 3×3
    const QSize tall(500, 1000);
    EXPECT_EQ(autoColumns(4, tall, 0, 0), 4); // tall tiles: one row
    EXPECT_EQ(autoColumns(0, wide, 0, 0), 1);
}

TEST(Sheet, LayoutUsesTheLargestImageAsTheCell)
{
    const SheetLayout l = layoutSheet({QSize(100, 50), QSize(200, 100), QSize(), QSize(150, 150)}, 2, 10, 0, 0);
    EXPECT_EQ(l.count, 4);
    EXPECT_EQ(l.columns, 2);
    EXPECT_EQ(l.rows, 2);
    EXPECT_EQ(l.cell, QSize(150, 150)); // the most pixels, not the widest
}

TEST(Sheet, TileRectsAndHitTesting)
{
    const SheetLayout l = layoutSheet({QSize(100, 50), QSize(100, 50), QSize(100, 50)}, 2, 10, 40, 0);
    EXPECT_EQ(l.cell, QSize(100, 50));
    EXPECT_EQ(l.size(), QSize(10 + 100 + 10 + 100 + 10, 40 + 50 + 10 + 50 + 10));
    EXPECT_EQ(l.tileRect(0), QRect(10, 40, 100, 50));
    EXPECT_EQ(l.tileRect(1), QRect(120, 40, 100, 50));
    EXPECT_EQ(l.tileRect(2), QRect(10, 100, 100, 50));
    EXPECT_EQ(l.tileRect(3), QRect());
    EXPECT_EQ(l.tileAt(QPoint(15, 45)), 0);
    EXPECT_EQ(l.tileAt(QPoint(219, 89)), 1);
    EXPECT_EQ(l.tileAt(QPoint(115, 45)), -1); // the gap
    EXPECT_EQ(l.tileAt(QPoint(150, 120)), -1); // no fourth tile
    EXPECT_EQ(l.headerRect(), QRect(0, 0, 230, 40));
    EXPECT_EQ(l.footerRect().height(), 0);
}

TEST(Sheet, ExplicitColumnsAreClamped)
{
    EXPECT_EQ(layoutSheet({QSize(10, 10), QSize(10, 10)}, 5, 0, 0, 0).columns, 2);
    EXPECT_EQ(layoutSheet({QSize(10, 10), QSize(10, 10)}, 1, 0, 0, 0).rows, 2);
}

TEST(Sheet, RenderSizesAndDrawsMissingTiles)
{
    guiApp();
    SheetState s;
    s.members = {"a", "b"};
    s.columns = 2;
    s.gap = 8;
    s.background = Qt::black;
    QImage red(60, 40, QImage::Format_RGB32);
    red.fill(Qt::red);
    SheetLayout layout;
    const QImage sheet = renderSheet(s, {SheetTile{red, "a", {}, red.size()}, SheetTile{}}, "sheet", {}, &layout);
    EXPECT_EQ(sheet.size(), QSize(8 + 60 + 8 + 60 + 8, 8 + 40 + 8));
    EXPECT_EQ(layout.cell, QSize(60, 40));
    EXPECT_EQ(sheet.pixelColor(layout.tileRect(0).center()), QColor(Qt::red));
    EXPECT_NE(sheet.pixelColor(layout.tileRect(1).topLeft()), QColor(Qt::black)); // the "missing" fill
    EXPECT_EQ(sheet.pixelColor(2, 2), QColor(Qt::black));

    s.sheetText[TextPosition::BottomCenter] = "{n} shots";
    const QImage withFooter = renderSheet(s, {SheetTile{red, "a", {}, red.size()}, SheetTile{}}, "sheet", {});
    EXPECT_GT(withFooter.height(), sheet.height()); // the footer is a band, not over the tiles
}

TEST(Sheet, JsonRoundTrip)
{
    SheetState s;
    s.members = {"qs-2", "qs-1"};
    s.everyTile[TextPosition::TopLeft] = "{n}";
    s.perTile["qs-1"][TextPosition::BottomLeft] = "here";
    s.perTile["qs-2"] = SlotText{}; // empty ones are not stored
    const SheetState back = sheetFromJson(QJsonDocument::fromJson(QJsonDocument(toJson(s)).toJson()).object());
    EXPECT_EQ(back.members, s.members);
    EXPECT_EQ(back.everyTile, s.everyTile);
    EXPECT_EQ(back.perTile.size(), 1);
    EXPECT_EQ(back.perTile.value("qs-1"), s.perTile.value("qs-1"));
}

TEST(Sheet, ScaledSheetScalesGapAndText)
{
    SheetState s;
    s.gap = 20;
    s.style.fontSize = 40;
    const SheetState half = scaledSheet(s, 0.5);
    EXPECT_EQ(half.gap, 10);
    EXPECT_EQ(half.style.fontSize, 20);
}
