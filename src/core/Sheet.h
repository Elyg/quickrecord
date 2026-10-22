#pragma once

#include "core/SlotText.h"
#include "core/TextOverlay.h"

#include <QColor>
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QRect>
#include <QSize>
#include <QStringList>

namespace qr
{

// A contact sheet of screenshots: what it shows and how, kept with its history entry so it can
// be reopened and changed.
struct SheetState
{
    QStringList members; // screenshot ids, in sheet order
    int columns = 0;     // 0 = auto (see autoColumns)
    int gap = 16;        // pixels between tiles and around them
    QColor background = QColor("#1e1f22");
    SlotText sheetText;               // header / footer bands, never over the images
    SlotText everyTile;               // drawn on every tile
    QHash<QString, SlotText> perTile; // appended on that member's tile
    TextOverlay style;                // size, colour, background box (its text and position unused)

    bool operator==(const SheetState&) const = default;
};

// A new sheet of `members`: numbered tiles, text and gap sized for cells `cellHeight` pixels tall.
SheetState newSheet(const QStringList& members, int cellHeight);

QJsonObject toJson(const SheetState& s);
SheetState sheetFromJson(const QJsonObject& o);

// Where everything goes on the sheet: same-size cells in a grid, filled left to right then
// down, header and footer bands above and below them. Pure arithmetic.
struct SheetLayout
{
    int count = 0;
    int columns = 1;
    int rows = 0;
    QSize cell;
    int gap = 0;
    int header = 0; // band heights; 0 = none (the gap instead)
    int footer = 0;

    QSize size() const;
    QRect tileRect(int i) const;
    int tileAt(QPoint p) const; // -1 between or outside the tiles
    QRect headerRect() const;
    QRect footerRect() const;
    bool operator==(const SheetLayout&) const = default;
};

// The column count whose sheet comes closest to 16:10, so a sheet shows the most image on a
// typical screen.
int autoColumns(int count, QSize cell, int gap, int bands);

// `sizes` are the members' images (invalid = missing). Each cell is the size of the largest
// image; `columns` 0 = autoColumns().
SheetLayout layoutSheet(const QList<QSize>& sizes, int columns, int gap, int header, int footer);

// One member as drawn: its image (null = missing) and what its placeholders show.
struct SheetTile
{
    QImage image;
    QString name;      // {name}
    QDateTime created; // {date} {time}
    QSize size;        // {size}: the original's pixels
};

// The sheet itself. `name` and `created` fill the sheet bands' placeholders ({n} there is the
// number of tiles). Images larger than their cell are scaled down to fit, smaller ones stay
// pixel for pixel; each is centred in its cell.
QImage renderSheet(const SheetState& state, const QList<SheetTile>& tiles, const QString& name,
                   const QDateTime& created, SheetLayout* layout = nullptr);

// `state` with its gap and text size multiplied by `factor` (for a preview of scaled images).
SheetState scaledSheet(SheetState state, double factor);

} // namespace qr
