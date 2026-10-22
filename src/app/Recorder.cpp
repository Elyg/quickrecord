#include "app/Recorder.h"

#include "app/Tools.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <spdlog/spdlog.h>

namespace qr
{

namespace
{
// Give the compositor time to fade out the overlay before the first frame is grabbed.
constexpr int kStartDelayMs = 200;
constexpr qint64 kMinSegmentBytes = 1024;
// How long a change on screen takes to reach the recording. Keys and clicks are shifted by it
// so they line up with what the video shows (avfoundation, measured ~100–135 ms; x11grab
// reads the screen directly).
#ifdef Q_OS_MACOS
constexpr qint64 kCaptureLatencyMs = 120;
#else
constexpr qint64 kCaptureLatencyMs = 0;
#endif

QString tail(const QByteArray& bytes, int lines = 6)
{
    QStringList all = QString::fromUtf8(bytes).trimmed().split('\n');
    return all.mid(std::max<qsizetype>(0, all.size() - lines)).join('\n');
}
} // namespace

Recorder::Recorder(QObject* parent)
    : QObject(parent)
{
    m_clock.start();
    m_tick.setInterval(250);
    connect(&m_tick, &QTimer::timeout, this, [this] { emit elapsedChanged(elapsedMs()); });
    m_countdownTimer.setInterval(1000);
    connect(&m_countdownTimer, &QTimer::timeout, this, [this] {
        --m_countdown;
        emit countdownTick(m_countdown);
        if(m_countdown <= 0)
        {
            m_countdownTimer.stop();
            QTimer::singleShot(kStartDelayMs, this, [this] {
                if(m_state == State::Countdown)
                    startSegment();
            });
        }
    });
}

Recorder::~Recorder()
{
    cancel();
}

bool Recorder::start(const Options& opts)
{
    if(m_state != State::Idle)
        return false;
    m_opts = opts;
    m_segments.clear();
    m_accumulatedMs = 0;
    m_resumeRequested = false;
    m_stderr.clear();
    m_captured.clear();
    m_inputEvents.clear();

    QDir().mkpath(opts.outputDir);
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    m_id = "qr-" + stamp;
    for(int n = 2; QFileInfo::exists(opts.outputDir + "/" + m_id + ".mp4"); ++n)
        m_id = QStringLiteral("qr-%1-%2").arg(stamp).arg(n);
    m_workDir = opts.workRoot + "/work-" + m_id;
    if(!QDir().mkpath(m_workDir))
    {
        emit failed("Cannot create " + m_workDir);
        return false;
    }

    setState(State::Countdown);
    m_countdown = opts.countdownSeconds;
    if(m_countdown > 0)
    {
        emit countdownTick(m_countdown);
        m_countdownTimer.start();
    }
    else
    {
        QTimer::singleShot(kStartDelayMs, this, [this] {
            if(m_state == State::Countdown)
                startSegment();
        });
    }
    return true;
}

void Recorder::startSegment()
{
    CaptureParams p = m_opts.capture;
    p.output = QStringLiteral("%1/seg%2.mkv").arg(m_workDir).arg(m_segments.size());
    m_segments << p.output;

    m_proc = new QProcess(this);
    m_proc->setProcessEnvironment(Tools::processEnv());
    connect(m_proc, &QProcess::readyReadStandardError, this, [this, proc = m_proc] {
        m_stderr += proc->readAllStandardError();
        if(m_stderr.size() > 64 * 1024)
            m_stderr = m_stderr.right(32 * 1024);
    });
    m_segmentLaunch = m_clock.elapsed();
    m_segmentStart = -1;
    m_stdout.clear();
    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this, proc = m_proc] { readProgress(proc); });
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) { onSegmentFinished(code); });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if(err == QProcess::FailedToStart)
            fail("Could not start ffmpeg: " + m_opts.ffmpeg);
    });

    const QStringList args = captureArgs(p);
    spdlog::info("capture: {} {}", m_opts.ffmpeg.toStdString(), args.join(' ').toStdString());
    m_proc->start(m_opts.ffmpeg, args);
    m_segmentTimer.start();
    m_tick.start();
    setState(State::Recording);
}

void Recorder::stopSegment()
{
    if(!m_proc || m_stopping)
        return;
    m_stopping = true;
    m_accumulatedMs += m_segmentTimer.elapsed();
    // Its start is when the first frame was captured: "now − video so far", earliest seen.
    const qint64 start = m_segmentStart >= 0 ? m_segmentStart : m_segmentLaunch;
    m_captured.append({start, std::max<qint64>(0, m_clock.elapsed() - start)});
    // "q" makes ffmpeg flush and close the file cleanly; escalate if it hangs.
    m_proc->write("q");
    QProcess* proc = m_proc;
    QTimer::singleShot(4000, proc, [proc] { proc->terminate(); });
    QTimer::singleShot(8000, proc, [proc] { proc->kill(); });
}

void Recorder::onSegmentFinished(int exitCode)
{
    QProcess* proc = m_proc;
    m_proc = nullptr;
    const bool expected = m_stopping;
    m_stopping = false;
    if(proc)
        proc->deleteLater();

    if(!expected)
    {
        fail(QStringLiteral("ffmpeg stopped unexpectedly (exit %1):\n%2").arg(exitCode).arg(tail(m_stderr)));
        return;
    }
    if(m_state == State::Finalizing)
        finalize();
    else if(m_state == State::Paused && m_resumeRequested)
    {
        m_resumeRequested = false;
        startSegment();
    }
}

void Recorder::readProgress(QProcess* proc)
{
    m_stdout += proc->readAllStandardOutput();
    qsizetype nl;
    while((nl = m_stdout.indexOf('\n')) >= 0)
    {
        const double s = parseProgressSeconds(QString::fromUtf8(m_stdout.left(nl)));
        m_stdout.remove(0, nl + 1);
        if(s <= 0 || m_stopping) // after "q" the reports include the flush
            continue;
        const qint64 start = m_clock.elapsed() - qint64(s * 1000);
        m_segmentStart = m_segmentStart < 0 ? start : std::min(m_segmentStart, start);
    }
}

void Recorder::togglePause()
{
    if(m_state == State::Recording)
    {
        setState(State::Paused);
        m_tick.stop();
        stopSegment();
        emit elapsedChanged(elapsedMs());
    }
    else if(m_state == State::Paused)
    {
        if(m_proc) // previous segment still flushing
            m_resumeRequested = !m_resumeRequested;
        else
            startSegment();
    }
}

void Recorder::stop()
{
    switch(m_state)
    {
        case State::Countdown: cancel(); break;
        case State::Recording:
            setState(State::Finalizing);
            m_tick.stop();
            stopSegment();
            break;
        case State::Paused:
            setState(State::Finalizing);
            m_resumeRequested = false;
            if(!m_proc)
                finalize();
            break;
        default: break;
    }
}

void Recorder::cancel()
{
    if(m_state == State::Idle)
        return;
    m_countdownTimer.stop();
    m_tick.stop();
    for(QProcess* p : {m_proc, m_finalizer})
    {
        if(!p)
            continue;
        p->disconnect(this);
        p->kill();
        p->waitForFinished(2000);
        p->deleteLater();
    }
    m_proc = nullptr;
    m_finalizer = nullptr;
    m_stopping = false;
    m_inputEvents.clear();
    cleanup();
    setState(State::Idle);
}

void Recorder::finalize()
{
    QStringList segments;
    for(const QString& s : m_segments)
        if(QFileInfo(s).size() >= kMinSegmentBytes)
            segments << s;
    if(segments.isEmpty())
    {
        fail("Nothing was recorded.\n" + tail(m_stderr));
        return;
    }

    // Assembled out of sight, then moved into the recordings folder whole.
    const QString assembled = m_workDir + "/" + m_id + ".mp4";
    const QString master = m_opts.outputDir + "/" + m_id + ".mp4";
    QStringList args;
    if(segments.size() == 1)
        args = remuxArgs(segments.first(), assembled);
    else
    {
        const QString list = m_workDir + "/segments.txt";
        QFile f(list);
        if(!f.open(QIODevice::WriteOnly) || f.write(concatList(segments).toUtf8()) < 0)
        {
            fail("Cannot write " + list);
            return;
        }
        f.close();
        args = concatArgs(list, assembled);
    }

    m_finalizer = new QProcess(this);
    m_finalizer->setProcessEnvironment(Tools::processEnv());
    connect(m_finalizer, &QProcess::finished, this, [this, assembled, master](int code, QProcess::ExitStatus st) {
        const QByteArray err = m_finalizer->readAllStandardError();
        m_finalizer->deleteLater();
        m_finalizer = nullptr;
        if(st != QProcess::NormalExit || code != 0 || !QFileInfo::exists(assembled))
        {
            fail("Could not assemble the recording:\n" + tail(err));
            return;
        }
        // A rename across volumes (cache on another disk) fails: copy instead.
        if(!QFile::rename(assembled, master) && !QFile::copy(assembled, master))
        {
            fail("Could not write " + master);
            return;
        }
        const double seconds = m_accumulatedMs / 1000.0;
        const bool hasAudio = m_opts.capture.audio != AudioInput::None;
        const QString id = m_id;
        cleanup();
        setState(State::Idle);
        emit finished(id, master, seconds, hasAudio);
    });
    m_finalizer->start(m_opts.ffmpeg, args);
}

void Recorder::fail(const QString& message)
{
    spdlog::error("recording failed: {}", message.toStdString());
    cancel();
    emit failed(message);
}

void Recorder::cleanup()
{
    if(!m_workDir.isEmpty())
        QDir(m_workDir).removeRecursively();
    m_workDir.clear();
}

void Recorder::addInputEvent(InputEvent event)
{
    if(m_state != State::Recording && m_state != State::Paused && m_state != State::Countdown)
        return;
    event.time = double(m_clock.elapsed() + kCaptureLatencyMs);
    m_inputEvents << event;
}

QList<InputEvent> Recorder::takeInputEvents()
{
    const QList<InputEvent> out = toRecordingTime(m_inputEvents, m_captured);
    m_inputEvents.clear();
    return out;
}

qint64 Recorder::elapsedMs() const
{
    return m_accumulatedMs + ((m_state == State::Recording && m_proc && !m_stopping) ? m_segmentTimer.elapsed() : 0);
}

void Recorder::setState(State s)
{
    if(m_state == s)
        return;
    m_state = s;
    emit stateChanged(s);
}

} // namespace qr
