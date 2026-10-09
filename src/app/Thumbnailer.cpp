#include "app/Thumbnailer.h"

#include "app/Tools.h"
#include "core/FfmpegArgs.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <spdlog/spdlog.h>

namespace qr
{

void makeSprite(const QString& input, double durationSec, const QString& output, QObject* context,
                std::function<void(bool)> done)
{
    const QString ffmpeg = Tools::exportFfmpeg();
    if(ffmpeg.isEmpty())
    {
        done(false);
        return;
    }
    QDir().mkpath(QFileInfo(output).absolutePath());

    auto* proc = new QProcess(context);
    proc->setProcessEnvironment(Tools::processEnv());
    QObject::connect(proc, &QProcess::finished, context, [proc, output, done](int code, QProcess::ExitStatus st) {
        const bool ok = st == QProcess::NormalExit && code == 0 && QFileInfo::exists(output);
        if(!ok)
            spdlog::warn("thumbnail failed: {}", proc->readAllStandardError().toStdString());
        proc->deleteLater();
        done(ok);
    });
    QObject::connect(proc, &QProcess::errorOccurred, context, [proc, done](QProcess::ProcessError err) {
        if(err != QProcess::FailedToStart)
            return; // other errors are followed by finished()
        proc->deleteLater();
        done(false);
    });
    proc->start(ffmpeg, spriteArgs(input, durationSec, kSpriteFrames, kSpriteCellWidth, output));
}

} // namespace qr
