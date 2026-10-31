#pragma once

#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <optional>

namespace qr
{

// --- capture --------------------------------------------------------------

enum class CaptureSource
{
    X11Grab,      // Linux
    AVFoundation, // macOS: one whole display, cropped by a filter
};

enum class AudioInput
{
    None,
    Pulse,
    Alsa,
    AVFoundation, // the default microphone, through the screen's avfoundation input
};

struct CaptureParams
{
    CaptureSource source = CaptureSource::X11Grab;
    QString display = ":0"; // X display, or the avfoundation screen ("Capture screen 0")
    QRect physicalRect;     // already even-sized; x11grab: root window pixels,
                            // avfoundation: pixels relative to that display's top-left
    int fps = 30;
    bool drawMouse = true;
    AudioInput audio = AudioInput::None;
    QString audioDevice = "default";
    QString output;      // segment file (.mkv)
};

// ffmpeg x11grab / avfoundation → near-lossless H.264 segment. Stopped by writing "q" to stdin.
// Reports progress ("out_time_us=…") on stdout, which dates the start of the capture.
QStringList captureArgs(const CaptureParams& p);

// Contents of an ffmpeg concat-demuxer list file for the given segments.
QString concatList(const QStringList& segments);
// Joins segments (via the list file) into a faststart MP4 without re-encoding.
QStringList concatArgs(const QString& listFile, const QString& output);
// Single segment → faststart MP4 without re-encoding.
QStringList remuxArgs(const QString& input, const QString& output);

// --- export ---------------------------------------------------------------

struct ExportParams
{
    QString input;
    double start = 0.0; // seconds
    double end = 0.0;   // seconds, > start
    int fps = 15;       // 0 = keep source rate
    int sourceWidth = 0;
    QRect crop;         // in source pixels (even-aligned); empty = whole frame
    int maxWidth = 0;   // 0 = keep source width
    int gifQuality = 90;
    bool keepAudio = false;
    QString overlayImage; // transparent PNG of the (cropped) frame size, composited on every frame
    // Instead of overlayImage: an ffconcat script of such PNGs with durations (keys and clicks),
    // timed from the trim start.
    QString overlaySequence;
    QString output;
};

// Output width after applying maxWidth (even, never upscaled).
int outputWidth(int sourceWidth, int maxWidth);
// Width the export starts from: the crop's when cropping, else the source's.
int croppedWidth(const ExportParams& p);
// "crop=…,fps=15,scale=800:-2:flags=lanczos" (parts omitted when not needed; may be empty).
QString videoFilter(const ExportParams& p);

QStringList mp4ExportArgs(const ExportParams& p);
// ffmpeg half of the gifski pipeline: decodes + filters, writes y4m to stdout.
QStringList gifskiFeederArgs(const ExportParams& p);
// gifski reading y4m from stdin.
QStringList gifskiArgs(const ExportParams& p);
// Single-process fallback when gifski is unavailable (palettegen/paletteuse).
QStringList paletteGifArgs(const ExportParams& p);

// Sprite sheet of `frames` evenly spaced thumbnails, each `cellWidth` wide, in one row.
QStringList spriteArgs(const QString& input, double durationSec, int frames, int cellWidth,
                       const QString& output);

// A contact sheet of recordings: they all start together, and one that ends holds its last frame
// until the longest has finished.
struct SheetVideoTile
{
    QString input;
    QRect rect; // where it plays on the sheet
};

struct SheetVideoParams
{
    QList<SheetVideoTile> tiles;
    QString background; // PNG of SheetLayer::Background
    QString labels;     // PNG of SheetLayer::Labels
    QString color = "#1e1f22"; // fills the pixel that evens out an odd sheet size
    double duration = 0.0;     // of the longest
    int fps = 30;
    QString preset = "medium"; // x264: a throwaway preview trades quality for speed
    int crf = 20;
    QString output;
};

QStringList sheetVideoArgs(const SheetVideoParams& p);

// The frame of `input` at `seconds` as an image.
QStringList frameArgs(const QString& input, double seconds, const QString& output);

// Parses "out_time_us=12345" style lines from `-progress`; returns seconds or -1.
double parseProgressSeconds(const QString& line);

// --- probing --------------------------------------------------------------

struct VideoInfo
{
    double duration = 0.0; // seconds
    QSize size;
    bool hasAudio = false;
};

// Makes ffmpeg print what it knows about `input` to stderr and stop (it exits non-zero: no output).
QStringList probeArgs(const QString& input);
// Reads that printout. nullopt when it isn't a whole video, e.g. a file still being copied.
std::optional<VideoInfo> parseProbe(const QString& ffmpegStderr);

} // namespace qr
