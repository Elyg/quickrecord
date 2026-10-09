#pragma once

#include "core/FfmpegArgs.h"

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QTimer>

class QProcess;

namespace qr
{

// Drives ffmpeg x11grab. Pause is implemented as segments: pausing finishes the current
// segment, resuming starts a new one, and stopping concatenates them into the master MP4.
class Recorder : public QObject
{
    Q_OBJECT

public:
    enum class State
    {
        Idle,
        Countdown,
        Recording,
        Paused,
        Finalizing,
    };

    struct Options
    {
        QString ffmpeg;
        CaptureParams capture; // output is ignored; segments are managed here
        QString mastersDir;
        int countdownSeconds = 0;
    };

    explicit Recorder(QObject* parent = nullptr);
    ~Recorder() override;

    bool start(const Options& opts);
    void togglePause();
    void stop();
    void cancel();

    State state() const { return m_state; }
    qint64 elapsedMs() const;

signals:
    void stateChanged(qr::Recorder::State state);
    void countdownTick(int remaining); // 0 = countdown over, capture about to start
    void elapsedChanged(qint64 ms);
    void finished(QString id, QString masterPath, double durationSec, bool hasAudio);
    void failed(QString message);

private:
    void setState(State s);
    void startSegment();
    void stopSegment();
    void onSegmentFinished(int exitCode);
    void finalize();
    void fail(const QString& message);
    void cleanup();

    Options m_opts;
    State m_state = State::Idle;
    QString m_id;
    QString m_workDir;
    QStringList m_segments;
    QProcess* m_proc = nullptr;
    QProcess* m_finalizer = nullptr;
    bool m_stopping = false;
    bool m_resumeRequested = false;
    QByteArray m_stderr;
    QElapsedTimer m_segmentTimer;
    qint64 m_accumulatedMs = 0;
    QTimer m_tick;
    QTimer m_countdownTimer;
    int m_countdown = 0;
};

} // namespace qr
