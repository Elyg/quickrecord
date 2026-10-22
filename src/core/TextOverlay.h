#pragma once

#include <QColor>
#include <QFont>
#include <QImage>
#include <QRect>
#include <QString>

class QPainter;

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

// The caption's look, shared with the contact sheet's corner text.
QFont captionFont(int pixelSize);
int captionPadding(const TextOverlay& style); // text to box edge
int captionMargin(const TextOverlay& style);  // box to frame edge

// Draws `text` at `position` inside `frame`, styled like `style` (its own text and position are
// not used); lines wider than `maxWidth` (when > 0, box included) are elided. Returns the box,
// or a null rect when there is nothing to draw.
QRect drawCaption(QPainter& p, const QRect& frame, const QString& text, TextPosition position,
                  const TextOverlay& style, int maxWidth = 0);

} // namespace qr
