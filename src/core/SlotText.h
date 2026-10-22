#pragma once

#include "core/TextOverlay.h"

#include <QRect>
#include <QString>
#include <array>
#include <functional>
#include <optional>

class QPainter;

namespace qr
{

// Text in the six corners of a frame (the contact sheet's labels): top-left, top-centre,
// top-right, bottom-left, bottom-centre, bottom-right, in TextPosition order. A slot may hold
// several lines; they stack away from its edge.
struct SlotText
{
    std::array<QString, 6> corners;

    QString& operator[](TextPosition p) { return corners[size_t(p)]; }
    const QString& operator[](TextPosition p) const { return corners[size_t(p)]; }
    bool isEmpty() const;
    bool operator==(const SlotText&) const = default;
};

// Slot by slot, `a`'s text with `b`'s appended as further lines.
SlotText appended(const SlotText& a, const SlotText& b);

// Replaces each "{name}" in `text` with lookup(name); names it has no value for stay as typed.
QString resolvePlaceholders(const QString& text, const std::function<std::optional<QString>(const QString&)>& lookup);

// Height of the band the top (or bottom) row of corners needs in `style`, margins included;
// 0 when that row is empty.
int slotRowHeight(const SlotText& text, bool top, const TextOverlay& style);

// Draws every slot in its corner of `frame` in `style` (as drawCaption()). The corners win:
// a centre slot gets only the width the corners beside it leave, and is elided to it.
void renderSlots(QPainter& p, const QRect& frame, const SlotText& text, const TextOverlay& style);

} // namespace qr
