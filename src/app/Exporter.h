#pragma once

#include "core/FfmpegArgs.h"

#include <QObject>

class QProcess;

namespace qr
{

// Renders a trimmed/scaled GIF or MP4 from a master. GIFs go through
// `ffmpeg → y4m → gifski` when gifski is available, else ffmpeg's palettegen.
// Writes to a temp file next to the output and renames on success.
class Exporter : public QObject
{
    Q_OBJECT

public:
    enum class Format
    {
        Gif,
        Mp4,
    };

    explicit Exporter(QObject* parent = nullptr);
    ~Exporter() override;

    bool isRunning() const { return m_ffmpeg != nullptr; }
    void start(Format format, const ExportParams& params);
    void cancel();

signals:
    void progress(double fraction);
    void finished(bool ok, QString output, QString error);

private:
    void onStderr();
    void onProcessDone();
    void finish(bool ok, const QString& error);

    QProcess* m_ffmpeg = nullptr;
    QProcess* m_gifski = nullptr; // optional second stage
    int m_running = 0;
    bool m_failed = false;
    QString m_output;
    QString m_tmpOutput;
    double m_duration = 0;
    QByteArray m_stderrLine;
    QStringList m_errors;
};

} // namespace qr
