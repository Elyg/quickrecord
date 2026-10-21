#pragma once

#include "core/TextOverlay.h"

#include <QImage>
#include <QList>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <functional>

namespace qr
{

// Keystrokes and mouse clicks captured during a recording (opt-in), stored next to the
// master and drawn on top of it by the editor and exports: keys as chips in a corner,
// clicks as a ring where the mouse was pressed.
struct InputEvent
{
    enum class Kind
    {
        Key,
        Click,
    };
    Kind kind = Kind::Key;
    double time = 0;    // seconds into the recording (while capturing: wall-clock ms, see toRecordingTime)
    QString label;      // key: "⌘⇧R", "↩", or the typed text ("a", "A", " ")
    bool typed = false; // key that types text: runs of these merge into one chip
    QPoint pos;         // click: recording pixels

    bool operator==(const InputEvent&) const = default;
};

// Where the log of the master at `masterPath` lives ("x.mp4" → "x.input.json").
QString inputLogPath(const QString& masterPath);
bool saveInputLog(const QString& path, const QList<InputEvent>& events);
QList<InputEvent> loadInputLog(const QString& path); // empty when missing or unreadable

// One stretch of capture between pauses, on the same clock as the events being mapped.
struct CaptureSegment
{
    qint64 startMs = 0;
    qint64 lengthMs = 0;
};

// Events timestamped in wall-clock ms → seconds into the recording, which is the segments
// played back to back. Events outside every segment (paused, countdown) are dropped.
QList<InputEvent> toRecordingTime(const QList<InputEvent>& events, const QList<CaptureSegment>& segments);

// What is drawn at one moment.
struct InputFrame
{
    QStringList keys;                       // oldest first
    QList<std::pair<QPoint, double>> rings; // click position, animation progress 0 → 1

    bool isEmpty() const { return keys.isEmpty() && rings.isEmpty(); }
    bool operator==(const InputFrame&) const = default;
};

InputFrame inputFrameAt(const QList<InputEvent>& events, double t);

// Moments in [start, end) where the drawing may change, starting with `start`; clicks add
// `ringFps` steps for the ring's animation. Each frame holds until the next time (or `end`).
QList<double> inputFrameTimes(const QList<InputEvent>& events, double start, double end, int ringFps = 15);

// Transparent ARGB image of `frame` size: key chips at `corner`, rings at their positions
// minus `origin` (the crop's top-left), all sized relative to the frame.
QImage renderInputOverlay(const InputFrame& input, QSize frame, TextPosition corner, QPoint origin = {});

// ffmpeg concat-demuxer script showing `files` for `durations` seconds each (same length).
QString overlaySequenceList(const QStringList& files, const QList<double>& durations);

// The overlay of an export as images: at each of `times` (see inputFrameTimes) `compose` draws
// `frameAt` that moment into a PNG in `dir` (emptied first), a frame unchanged from the one before
// just holding it longer, the last until `end`. Returns the concat script listing them, or "" with
// `error` set.
QString writeOverlaySequence(const QList<double>& times, double end, const QString& dir,
                             const std::function<InputFrame(double)>& frameAt,
                             const std::function<QImage(const InputFrame&)>& compose, QString* error = nullptr);

} // namespace qr
