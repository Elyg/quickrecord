#include "core/SlotText.h"

#include <QFontMetrics>
#include <QPainter>
#include <QRegularExpression>
#include <algorithm>

namespace qr
{

bool SlotText::isEmpty() const
{
    return std::all_of(corners.begin(), corners.end(), [](const QString& s) { return s.trimmed().isEmpty(); });
}

SlotText appended(const SlotText& a, const SlotText& b)
{
    SlotText out;
    for(size_t i = 0; i < out.corners.size(); ++i)
    {
        const QString first = a.corners[i].trimmed(), second = b.corners[i].trimmed();
        out.corners[i] = first.isEmpty() ? second : (second.isEmpty() ? first : first + "\n" + second);
    }
    return out;
}

QString resolvePlaceholders(const QString& text, const std::function<std::optional<QString>(const QString&)>& lookup)
{
    static const QRegularExpression re(QStringLiteral("\\{(\\w+)\\}"));
    QString out;
    qsizetype last = 0;
    for(auto it = re.globalMatch(text); it.hasNext();)
    {
        const QRegularExpressionMatch m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        const std::optional<QString> value = lookup(m.captured(1));
        out += value ? *value : m.captured(0);
        last = m.capturedEnd();
    }
    return out + text.mid(last);
}

namespace
{
constexpr TextPosition kTop[] = {TextPosition::TopLeft, TextPosition::TopCenter, TextPosition::TopRight};
constexpr TextPosition kBottom[] = {TextPosition::BottomLeft, TextPosition::BottomCenter, TextPosition::BottomRight};

// Width of `text`'s box in `style` (0 when empty).
int boxWidth(const QFontMetrics& fm, const QString& text, int pad)
{
    int w = 0;
    for(const QString& line : text.trimmed().split('\n'))
        w = std::max(w, fm.horizontalAdvance(line));
    return text.trimmed().isEmpty() ? 0 : w + 2 * pad;
}
} // namespace

int slotRowHeight(const SlotText& text, bool top, const TextOverlay& style)
{
    const QFontMetrics fm(captionFont(style.fontSize));
    int h = 0;
    for(TextPosition p : top ? kTop : kBottom)
    {
        const QString t = text[p].trimmed();
        if(!t.isEmpty())
            h = std::max(h, int(fm.boundingRect(QRect(0, 0, 1 << 20, 1 << 20), Qt::AlignLeft, t).height()));
    }
    return h > 0 ? h + 2 * captionPadding(style) + 2 * captionMargin(style) : 0;
}

void renderSlots(QPainter& p, const QRect& frame, const SlotText& text, const TextOverlay& style)
{
    if(text.isEmpty() || frame.isEmpty())
        return;
    const QFontMetrics fm(captionFont(style.fontSize));
    const int pad = captionPadding(style);
    const int margin = captionMargin(style);
    const int inner = frame.width() - 2 * margin;
    for(const auto& row : {kTop, kBottom})
    {
        const QString left = text[row[0]], centre = text[row[1]], right = text[row[2]];
        const bool both = !left.trimmed().isEmpty() && !right.trimmed().isEmpty();
        const int cornerMax = both ? (inner - margin) / 2 : inner;
        const QRect l = drawCaption(p, frame, left, row[0], style, cornerMax);
        const QRect r = drawCaption(p, frame, right, row[2], style, cornerMax);
        if(centre.trimmed().isEmpty())
            continue;
        // Centred, so it may use twice the room between the middle and the wider corner.
        const int side = std::max(l.isNull() ? 0 : l.width(), r.isNull() ? 0 : r.width());
        const int room = side > 0 ? inner - 2 * (side + margin) : inner;
        if(room >= std::min(boxWidth(fm, centre, pad), fm.horizontalAdvance(QStringLiteral("W…")) + 2 * pad))
            drawCaption(p, frame, centre, row[1], style, room);
    }
}

} // namespace qr
