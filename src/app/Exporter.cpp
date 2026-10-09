#include "app/Exporter.h"

#include "app/Tools.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <spdlog/spdlog.h>

namespace qr
{

Exporter::Exporter(QObject* parent)
    : QObject(parent)
{
}

Exporter::~Exporter()
{
    cancel();
}

void Exporter::start(Format format, const ExportParams& params)
{
    cancel();
    m_failed = false;
    m_errors.clear();
    m_stderrLine.clear();
    m_output = params.output;
    m_duration = std::max(0.05, params.end - params.start);

    const QFileInfo out(params.output);
    QDir().mkpath(out.absolutePath());
    // Keep the real extension last: ffmpeg picks the muxer from it.
    m_tmpOutput = out.absolutePath() + "/." + out.completeBaseName() + ".part." + out.suffix();
    ExportParams p = params;
    p.output = m_tmpOutput;

    const QString ffmpeg = Tools::exportFfmpeg();
    if(ffmpeg.isEmpty())
    {
        emit finished(false, m_output, "No ffmpeg with libx264 found.");
        return;
    }
    const QString gifski = format == Format::Gif ? Tools::gifski() : QString();

    m_ffmpeg = new QProcess(this);
    m_ffmpeg->setProcessEnvironment(Tools::processEnv());
    connect(m_ffmpeg, &QProcess::readyReadStandardError, this, &Exporter::onStderr);
    connect(m_ffmpeg, &QProcess::finished, this, &Exporter::onProcessDone);
    connect(m_ffmpeg, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if(e == QProcess::FailedToStart)
            finish(false, "Could not start ffmpeg.");
    });

    QStringList ffArgs;
    if(format == Format::Mp4)
        ffArgs = mp4ExportArgs(p);
    else if(gifski.isEmpty())
        ffArgs = paletteGifArgs(p);
    else
    {
        ffArgs = gifskiFeederArgs(p);
        m_gifski = new QProcess(this);
        m_gifski->setProcessEnvironment(Tools::processEnv());
        m_ffmpeg->setStandardOutputProcess(m_gifski);
        connect(m_gifski, &QProcess::finished, this, &Exporter::onProcessDone);
        connect(m_gifski, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if(e == QProcess::FailedToStart)
                finish(false, "Could not start gifski.");
        });
        const QStringList gArgs = gifskiArgs(p);
        spdlog::info("export: {} {}", gifski.toStdString(), gArgs.join(' ').toStdString());
        m_gifski->start(gifski, gArgs);
        ++m_running;
    }
    spdlog::info("export: {} {}", ffmpeg.toStdString(), ffArgs.join(' ').toStdString());
    m_ffmpeg->start(ffmpeg, ffArgs);
    ++m_running;
    emit progress(0.0);
}

void Exporter::onStderr()
{
    if(!m_ffmpeg)
        return;
    m_stderrLine += m_ffmpeg->readAllStandardError();
    qsizetype nl;
    while((nl = m_stderrLine.indexOf('\n')) >= 0)
    {
        const QString line = QString::fromUtf8(m_stderrLine.left(nl)).trimmed();
        m_stderrLine.remove(0, nl + 1);
        const double t = parseProgressSeconds(line);
        if(t >= 0)
            emit progress(std::clamp(t / m_duration, 0.0, 1.0));
        else if(!line.isEmpty() && !QRegularExpression("^[a-z_0-9]+=").match(line).hasMatch())
        {
            m_errors << line;
            if(m_errors.size() > 12)
                m_errors.removeFirst();
        }
    }
}

void Exporter::onProcessDone()
{
    auto* proc = qobject_cast<QProcess*>(sender());
    if(proc && (proc->exitStatus() != QProcess::NormalExit || proc->exitCode() != 0))
    {
        m_failed = true;
        if(proc == m_gifski)
            m_errors << QString::fromUtf8(m_gifski->readAllStandardError()).trimmed();
    }
    if(--m_running > 0)
        return;
    if(m_failed || !QFileInfo::exists(m_tmpOutput))
    {
        finish(false, m_errors.isEmpty() ? "Export failed." : m_errors.join('\n'));
        return;
    }
    QFile::remove(m_output);
    if(!QFile::rename(m_tmpOutput, m_output))
    {
        finish(false, "Could not write " + m_output);
        return;
    }
    finish(true, {});
}

void Exporter::finish(bool ok, const QString& error)
{
    for(QProcess* p : {m_ffmpeg, m_gifski})
    {
        if(!p)
            continue;
        p->disconnect(this);
        if(p->state() != QProcess::NotRunning)
        {
            p->kill();
            p->waitForFinished(2000);
        }
        p->deleteLater();
    }
    m_ffmpeg = nullptr;
    m_gifski = nullptr;
    m_running = 0;
    if(!ok)
        QFile::remove(m_tmpOutput);
    if(ok)
        emit progress(1.0);
    emit finished(ok, m_output, error);
}

void Exporter::cancel()
{
    if(!m_ffmpeg)
        return;
    for(QProcess* p : {m_ffmpeg, m_gifski})
    {
        if(!p)
            continue;
        p->disconnect(this);
        p->kill();
        p->waitForFinished(2000);
        p->deleteLater();
    }
    m_ffmpeg = nullptr;
    m_gifski = nullptr;
    m_running = 0;
    QFile::remove(m_tmpOutput);
}

} // namespace qr
