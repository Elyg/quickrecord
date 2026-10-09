#pragma once

#include <QColor>
#include <QImage>
#include <QRect>
#include <QString>

namespace qr
{

enum class TextPosition
{
    TopLeft,
    TopCenter,
    TopRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
};

// A caption burned into exports. Rendered by Qt into a transparent frame-sized image,
// which the editor previews and ffmpeg composites with `overlay` — so the preview and
// the export are pixel-identical and no ffmpeg font support (drawtext) is needed.
struct TextOverlay
{
    QString text;
    TextPosition position = TextPosition::BottomRight;
    int fontSize = 28; // pixels of the recording (scales with the export)
    QColor color = Qt::white;
    bool background = true;

    bool isEmpty() const { return text.trimmed().isEmpty(); }
    bool operator==(const TextOverlay&) const = default;
};

QString toString(TextPosition p);           // "bottom-right"
TextPosition textPositionFromString(const QString& s);

// Where a box of `box` size goes inside `frame`, `margin` pixels from the edges.
QRect placeText(TextPosition position, QSize frame, QSize box, int margin);

// Transparent ARGB image of `frame` size with the caption drawn on it.
QImage renderTextOverlay(const TextOverlay& overlay, QSize frame);

} // namespace qr
