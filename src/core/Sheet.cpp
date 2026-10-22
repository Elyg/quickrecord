#include "core/Sheet.h"

#include <QJsonArray>
#include <QLocale>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace qr
{

namespace
{
constexpr double kTargetAspect = 16.0 / 10.0;
const QSize kMissingCell(800, 500);

QJsonArray slotsToJson(const SlotText& t)
{
    QJsonArray a;
    for(const QString& s : t.corners)
        a << s;
    return a;
}

SlotText slotsFromJson(const QJsonArray& a)
{
    SlotText t;
    for(int i = 0; i < int(t.corners.size()) && i < a.size(); ++i)
        t.corners[size_t(i)] = a[i].toString();
    return t;
}
} // namespace

SheetState newSheet(const QStringList& members, int cellHeight)
{
    SheetState s;
    s.members = members;
    // Reads at a glance whatever the screenshots' resolution.
    s.style.fontSize = std::clamp(cellHeight / 28, 14, 160);
    s.gap = std::clamp(cellHeight / 40, 8, 64);
    s.everyTile[TextPosition::TopLeft] = "{n}";
    return s;
}

QJsonObject toJson(const SheetState& s)
{
    QJsonObject perTile;
    for(auto it = s.perTile.begin(); it != s.perTile.end(); ++it)
        if(!it->isEmpty())
            perTile[it.key()] = slotsToJson(*it);
    return {
        {"members", QJsonArray::fromStringList(s.members)},
        {"columns", s.columns},
        {"gap", s.gap},
        {"background", s.background.name(QColor::HexArgb)},
        {"sheetText", slotsToJson(s.sheetText)},
        {"everyTile", slotsToJson(s.everyTile)},
        {"perTile", perTile},
        {"style",
         QJsonObject{
             {"size", s.style.fontSize},
             {"color", s.style.color.name(QColor::HexArgb)},
             {"background", s.style.background},
         }},
    };
}

SheetState sheetFromJson(const QJsonObject& o)
{
    SheetState s;
    for(const auto& v : o["members"].toArray())
        s.members << v.toString();
    s.columns = o["columns"].toInt();
    s.gap = o["gap"].toInt(s.gap);
    s.background = QColor(o["background"].toString(s.background.name(QColor::HexArgb)));
    s.sheetText = slotsFromJson(o["sheetText"].toArray());
    s.everyTile = slotsFromJson(o["everyTile"].toArray());
    const QJsonObject perTile = o["perTile"].toObject();
    for(auto it = perTile.begin(); it != perTile.end(); ++it)
        s.perTile.insert(it.key(), slotsFromJson(it->toArray()));
    const QJsonObject style = o["style"].toObject();
    s.style.fontSize = style["size"].toInt(s.style.fontSize);
    s.style.color = QColor(style["color"].toString("#ffffffff"));
    s.style.background = style["background"].toBool(true);
    return s;
}

QSize SheetLayout::size() const
{
    const int w = 2 * gap + columns * cell.width() + std::max(0, columns - 1) * gap;
    const int tiles = rows * cell.height() + std::max(0, rows - 1) * gap;
    return {w, (header > 0 ? header : gap) + tiles + (footer > 0 ? footer : gap)};
}

QRect SheetLayout::tileRect(int i) const
{
    if(i < 0 || i >= count)
        return {};
    const int top = header > 0 ? header : gap;
    return {QPoint(gap + (i % columns) * (cell.width() + gap), top + (i / columns) * (cell.height() + gap)), cell};
}

int SheetLayout::tileAt(QPoint p) const
{
    for(int i = 0; i < count; ++i)
        if(tileRect(i).contains(p))
            return i;
    return -1;
}

QRect SheetLayout::headerRect() const
{
    return {0, 0, size().width(), header};
}

QRect SheetLayout::footerRect() const
{
    const QSize s = size();
    return {0, s.height() - footer, s.width(), footer};
}

int autoColumns(int count, QSize cell, int gap, int bands)
{
    int best = 1;
    double bestScore = 1e9;
    for(int c = 1; c <= std::max(1, count); ++c)
    {
        const int rows = (count + c - 1) / c;
        const double w = c * cell.width() + (c + 1) * gap;
        const double h = rows * cell.height() + (rows + 1) * gap + bands;
        const double score = std::abs(std::log((w / std::max(1.0, h)) / kTargetAspect));
        if(score < bestScore - 1e-9)
        {
            best = c;
            bestScore = score;
        }
    }
    return best;
}

SheetLayout layoutSheet(const QList<QSize>& sizes, int columns, int gap, int header, int footer)
{
    SheetLayout l;
    l.count = int(sizes.size());
    l.gap = std::max(0, gap);
    l.header = std::max(0, header);
    l.footer = std::max(0, footer);
    for(const QSize& s : sizes)
        if(s.isValid() && qint64(s.width()) * s.height() > qint64(l.cell.width()) * l.cell.height())
            l.cell = s;
    if(!l.cell.isValid() || l.cell.isEmpty())
        l.cell = kMissingCell;
    const int bands = (l.header > 0 ? l.header - l.gap : 0) + (l.footer > 0 ? l.footer - l.gap : 0);
    l.columns = columns > 0 ? std::min(columns, std::max(1, l.count)) : autoColumns(l.count, l.cell, l.gap, bands);
    l.rows = (l.count + l.columns - 1) / l.columns;
    return l;
}

QImage renderSheet(const SheetState& state, const QList<SheetTile>& tiles, const QString& name,
                   const QDateTime& created, SheetLayout* layoutOut)
{
    const QLocale loc;
    auto fields = [&loc](int n, const QString& label, const QDateTime& when, QSize size) {
        return [=, &loc](const QString& key) -> std::optional<QString> {
            if(key == "n")
                return QString::number(n);
            if(key == "name")
                return label;
            if(key == "date")
                return loc.toString(when.date(), QLocale::ShortFormat);
            if(key == "time")
                return when.time().toString("HH:mm:ss");
            if(key == "size")
                return QStringLiteral("%1×%2").arg(size.width()).arg(size.height());
            return std::nullopt;
        };
    };
    auto resolved = [](const SlotText& t, const auto& lookup) {
        SlotText out;
        for(size_t i = 0; i < t.corners.size(); ++i)
            out.corners[i] = resolvePlaceholders(t.corners[i], lookup);
        return out;
    };

    QList<QSize> sizes;
    for(const SheetTile& t : tiles)
        sizes << (t.image.isNull() ? QSize() : t.image.size());
    const SheetLayout layout = layoutSheet(sizes, state.columns, state.gap, slotRowHeight(state.sheetText, true, state.style),
                                           slotRowHeight(state.sheetText, false, state.style));
    if(layoutOut)
        *layoutOut = layout;

    QImage img(layout.size(), QImage::Format_RGB32);
    img.fill(state.background.isValid() ? state.background : QColor("#1e1f22"));
    QPainter p(&img);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    for(int i = 0; i < layout.count; ++i)
    {
        const SheetTile& tile = tiles[i];
        const QRect cell = layout.tileRect(i);
        QRect frame = cell;
        if(tile.image.isNull())
        {
            const QColor bg = state.background.isValid() ? state.background : QColor("#1e1f22");
            p.fillRect(cell, bg.lightness() > 128 ? QColor(0, 0, 0, 24) : QColor(255, 255, 255, 24));
            p.save();
            p.setFont(captionFont(std::max(state.style.fontSize, cell.height() / 12)));
            p.setPen(bg.lightness() > 128 ? QColor(0, 0, 0, 140) : QColor(255, 255, 255, 140));
            p.drawText(cell, Qt::AlignCenter, "missing");
            p.restore();
        }
        else
        {
            const QSize fitted = tile.image.size().boundedTo(cell.size()) == tile.image.size()
                                     ? tile.image.size()
                                     : tile.image.size().scaled(cell.size(), Qt::KeepAspectRatio);
            frame = QRect(QPoint(), fitted);
            frame.moveCenter(cell.center());
            p.drawImage(frame, tile.image);
        }
        const SlotText text = appended(state.everyTile, state.perTile.value(state.members.value(i)));
        renderSlots(p, frame, resolved(text, fields(i + 1, tile.name, tile.created, tile.size)), state.style);
    }

    const auto sheetFields = fields(layout.count, name, created, layout.size());
    const SlotText bands = resolved(state.sheetText, sheetFields);
    SlotText top, bottom;
    for(TextPosition pos : {TextPosition::TopLeft, TextPosition::TopCenter, TextPosition::TopRight})
        top[pos] = bands[pos];
    for(TextPosition pos : {TextPosition::BottomLeft, TextPosition::BottomCenter, TextPosition::BottomRight})
        bottom[pos] = bands[pos];
    renderSlots(p, layout.headerRect(), top, state.style);
    renderSlots(p, layout.footerRect(), bottom, state.style);
    return img;
}

SheetState scaledSheet(SheetState state, double factor)
{
    state.gap = int(std::round(state.gap * factor));
    state.style.fontSize = std::max(4, int(std::round(state.style.fontSize * factor)));
    return state;
}

} // namespace qr
