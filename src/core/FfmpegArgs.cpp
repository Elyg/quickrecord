#include "core/FfmpegArgs.h"

#include <algorithm>

namespace qr
{

namespace
{
QString num(double v)
{
    return QString::number(v, 'f', 3);
}

// Shared head for the non-interactive (export/thumbnail) invocations.
QStringList batchHead()
{
    return {"-hide_banner", "-loglevel", "error", "-nostdin", "-nostats", "-progress", "pipe:2", "-y"};
}

QString cropFilter(const ExportParams& p)
{
    if(p.crop.isEmpty())
        return {};
    return QStringLiteral("crop=%1:%2:%3:%4").arg(p.crop.width()).arg(p.crop.height()).arg(p.crop.x()).arg(p.crop.y());
}

// fps + scale (everything after the crop).
QString scaleChain(const ExportParams& p)
{
    QStringList parts;
    if(p.fps > 0)
        parts << QStringLiteral("fps=%1").arg(p.fps);
    const int src = croppedWidth(p);
    const int w = outputWidth(src, p.maxWidth);
    if(src > 0 && w != src)
        parts << QStringLiteral("scale=%1:-2:flags=lanczos").arg(w);
    return parts.join(',');
}

QStringList trimmedInput(const ExportParams& p)
{
    const double duration = std::max(0.05, p.end - p.start);
    QStringList args = {"-ss", num(p.start), "-i", p.input};
    if(!p.overlaySequence.isEmpty())
        args << "-f" << "concat" << "-safe" << "0" << "-i" << p.overlaySequence;
    else if(!p.overlayImage.isEmpty())
        args << "-i" << p.overlayImage; // a still image: overlay repeats it for every frame
    // After all inputs, so it limits the output rather than the overlay input.
    args << "-t" << num(duration);
    return args;
}

// Video filtering: a plain -vf chain, or a filtergraph compositing the overlay image
// first (at source resolution, so it scales together with the video). `tail` is
// appended to the chain (e.g. the palette split for GIFs).
QStringList videoArgs(const ExportParams& p, const QString& tail, bool mapAudio)
{
    if(p.overlayImage.isEmpty() && p.overlaySequence.isEmpty())
    {
        QString chain = videoFilter(p);
        if(!tail.isEmpty())
            chain = chain.isEmpty() ? tail : chain + "," + tail;
        return chain.isEmpty() ? QStringList() : QStringList{"-vf", chain};
    }

    // crop → overlay (the caption is laid out on the cropped frame) → fps/scale → tail
    QString chain = scaleChain(p);
    if(!tail.isEmpty())
        chain = chain.isEmpty() ? tail : chain + "," + tail;
    const QString crop = cropFilter(p);
    QString graph = crop.isEmpty() ? QStringLiteral("[0:v]") : "[0:v]" + crop + "[c];[c]";
    graph += "[1:v]overlay=0:0:format=auto";
    if(!chain.isEmpty())
        graph += "," + chain;
    graph += "[v]";
    QStringList args = {"-filter_complex", graph, "-map", "[v]"};
    if(mapAudio)
        args << "-map" << "0:a?";
    return args;
}

int gifFps(const ExportParams& p)
{
    return p.fps > 0 ? p.fps : 30;
}
} // namespace

QStringList captureArgs(const CaptureParams& p)
{
    const QRect& r = p.physicalRect;
    QStringList args = {"-hide_banner", "-loglevel", "error", "-progress", "pipe:1", "-y", "-thread_queue_size", "1024"};

    if(p.source == CaptureSource::AVFoundation)
    {
        // The mic comes in through the same input ("<screen>:<audio device>").
        const QString audio = p.audio == AudioInput::AVFoundation ? p.audioDevice : QStringLiteral("none");
        args << "-f" << "avfoundation" << "-framerate" << QString::number(p.fps) << "-capture_cursor"
             << (p.drawMouse ? "1" : "0") << "-pixel_format" << "nv12" << "-i" << p.display + ":" + audio;
        args << "-vf"
             << QStringLiteral("crop=%1:%2:%3:%4").arg(r.width()).arg(r.height()).arg(r.x()).arg(r.y());
    }
    else
    {
        args << "-f" << "x11grab" << "-framerate" << QString::number(p.fps) << "-video_size"
             << QStringLiteral("%1x%2").arg(r.width()).arg(r.height()) << "-draw_mouse" << (p.drawMouse ? "1" : "0")
             << "-show_region" << "0" << "-i" << QStringLiteral("%1+%2,%3").arg(p.display).arg(r.x()).arg(r.y());

        if(p.audio == AudioInput::Pulse || p.audio == AudioInput::Alsa)
        {
            args << "-thread_queue_size" << "1024" << "-f" << (p.audio == AudioInput::Pulse ? "pulse" : "alsa")
                 << "-i" << p.audioDevice;
        }
    }

    // zerolatency: no frames held in the encoder, so the progress reports keep up with capture.
    args << "-c:v" << "libx264" << "-preset" << "ultrafast" << "-tune" << "zerolatency" << "-crf" << "18" << "-pix_fmt"
         << "yuv420p";
    if(p.audio != AudioInput::None)
        args << "-c:a" << "aac" << "-b:a" << "160k";
    args << p.output;
    return args;
}

QString concatList(const QStringList& segments)
{
    QString out;
    for(QString s : segments)
    {
        s.replace("'", "'\\''");
        out += QStringLiteral("file '%1'\n").arg(s);
    }
    return out;
}

QStringList concatArgs(const QString& listFile, const QString& output)
{
    return {"-hide_banner", "-loglevel", "error", "-nostdin", "-y", "-f", "concat", "-safe", "0", "-i", listFile,
            "-c", "copy", "-movflags", "+faststart", output};
}

QStringList remuxArgs(const QString& input, const QString& output)
{
    return {"-hide_banner", "-loglevel", "error", "-nostdin", "-y", "-i", input,
            "-c", "copy", "-movflags", "+faststart", output};
}

int outputWidth(int sourceWidth, int maxWidth)
{
    int w = sourceWidth;
    if(maxWidth > 0 && maxWidth < sourceWidth)
        w = maxWidth;
    return std::max(2, w & ~1);
}

int croppedWidth(const ExportParams& p)
{
    return p.crop.isEmpty() ? p.sourceWidth : p.crop.width();
}

QString videoFilter(const ExportParams& p)
{
    QStringList parts;
    if(const QString crop = cropFilter(p); !crop.isEmpty())
        parts << crop;
    if(const QString chain = scaleChain(p); !chain.isEmpty())
        parts << chain;
    return parts.join(',');
}

QStringList mp4ExportArgs(const ExportParams& p)
{
    QStringList args = batchHead() + trimmedInput(p) + videoArgs(p, {}, p.keepAudio);
    args << "-c:v" << "libx264" << "-preset" << "medium" << "-crf" << "23" << "-pix_fmt" << "yuv420p";
    if(p.keepAudio)
        args << "-c:a" << "aac" << "-b:a" << "128k";
    else
        args << "-an";
    args << "-movflags" << "+faststart" << p.output;
    return args;
}

QStringList gifskiFeederArgs(const ExportParams& p)
{
    ExportParams q = p;
    q.fps = gifFps(p);
    QStringList args = batchHead() + trimmedInput(q) + videoArgs(q, {}, false);
    args << "-an" << "-pix_fmt" << "yuv420p" << "-f" << "yuv4mpegpipe" << "-";
    return args;
}

QStringList gifskiArgs(const ExportParams& p)
{
    // gifski caps output at ~800px unless told otherwise, so always pass the width.
    return {"--quiet",
            "--quality",
            QString::number(std::clamp(p.gifQuality, 1, 100)),
            "--fps",
            QString::number(gifFps(p)),
            "--width",
            QString::number(outputWidth(croppedWidth(p), p.maxWidth)),
            "-o",
            p.output,
            "-"};
}

QStringList paletteGifArgs(const ExportParams& p)
{
    ExportParams q = p;
    q.fps = gifFps(p);
    const QString palette =
        "split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle";
    return batchHead() + trimmedInput(q) + videoArgs(q, palette, false) + QStringList{"-loop", "0", p.output};
}

QStringList spriteArgs(const QString& input, double durationSec, int frames, int cellWidth, const QString& output)
{
    const double rate = durationSec > 0.1 ? frames / durationSec : 10.0;
    const QString vf = QStringLiteral("fps=%1,scale=%2:-2,tile=%3x1").arg(QString::number(rate, 'f', 4)).arg(cellWidth).arg(frames);
    return {"-hide_banner", "-loglevel", "error", "-nostdin", "-y", "-i", input,
            "-vf", vf, "-frames:v", "1", "-update", "1", output};
}

double parseProgressSeconds(const QString& line)
{
    for(const char* key : {"out_time_us=", "out_time_ms="})
    {
        if(line.startsWith(QLatin1String(key)))
        {
            bool ok = false;
            const qint64 us = line.mid(int(qstrlen(key))).trimmed().toLongLong(&ok);
            return ok ? us / 1e6 : -1.0;
        }
    }
    return -1.0;
}

} // namespace qr
