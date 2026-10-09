#pragma once

#include <QRect>
#include <QString>
#include <QStringList>

namespace qr
{

// --- capture --------------------------------------------------------------

enum class AudioInput
{
    None,
    Pulse,
    Alsa,
};

struct CaptureParams
{
    QString display = ":0";
    QRect physicalRect;  // already even-sized
    int fps = 30;
    bool drawMouse = true;
    AudioInput audio = AudioInput::None;
    QString audioDevice = "default";
    QString output;      // segment file (.mkv)
};

// ffmpeg x11grab → near-lossless H.264 segment. Stopped by writing "q" to stdin.
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

// Parses "out_time_us=12345" style lines from `-progress`; returns seconds or -1.
double parseProgressSeconds(const QString& line);

} // namespace qr
