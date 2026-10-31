#include "app/Thumbnailer.h"

#include "app/Settings.h"
#include "app/Tools.h"
#include "core/FfmpegArgs.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <algorithm>
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

QString sheetFramePath(const HistoryEntry& entry)
{
    return Settings::thumbsDir() + "/" + entry.id + ".frame.png";
}

QString sheetImagePath(const HistoryEntry& entry)
{
    if(entry.kind == EntryKind::Recording)
        return sheetFramePath(entry);
    const QString edited = editedFor(entry, "png");
    return edited.isEmpty() ? entry.master : edited;
}

QString sheetVideoPath(const HistoryEntry& entry)
{
    if(const QString mp4 = editedFor(entry, "mp4"); !mp4.isEmpty())
        return mp4;
    const QString edited = latestEdited(entry);
    return edited.isEmpty() ? entry.master : edited;
}

SheetVideoParams sheetVideoParams(const SheetState& state, const QList<SheetTile>& tiles,
                                  const QList<const HistoryEntry*>& entries, const QString& name, const QDateTime& created,
                                  const QString& layers, QSize* size)
{
    SheetLayout layout;
    const QImage background = renderSheet(state, tiles, name, created, &layout, SheetLayer::Background);
    const QImage labels = renderSheet(state, tiles, name, created, nullptr, SheetLayer::Labels);
    if(size)
        *size = QSize((layout.size().width() + 1) & ~1, (layout.size().height() + 1) & ~1); // what yuv420p allows

    SheetVideoParams p;
    p.color = state.background.name();
    p.background = layers + "-background.png";
    p.labels = layers + "-labels.png";
    for(int i = 0; i < tiles.size(); ++i)
        if(entries[i] && !tiles[i].image.isNull())
        {
            p.tiles << SheetVideoTile{sheetVideoPath(*entries[i]), sheetImageRect(layout.tileRect(i), tiles[i].image.size())};
            p.duration = std::max(p.duration, playLength(*entries[i]));
        }
    if(!background.save(p.background) || !labels.save(p.labels))
        p.tiles.clear();
    return p;
}

void makeSheetFrames(QList<HistoryEntry> entries, QObject* context, std::function<void()> done)
{
    entries.removeIf([](const HistoryEntry& e) {
        if(e.kind != EntryKind::Recording)
            return true;
        const QFileInfo frame(sheetFramePath(e));
        return frame.exists() && frame.lastModified() >= QFileInfo(sheetVideoPath(e)).lastModified();
    });
    const QString ffmpeg = entries.isEmpty() ? QString() : Tools::exportFfmpeg();
    if(ffmpeg.isEmpty())
    {
        done();
        return;
    }

    const HistoryEntry entry = entries.takeFirst();
    QDir().mkpath(QFileInfo(sheetFramePath(entry)).absolutePath());
    auto* proc = new QProcess(context);
    proc->setProcessEnvironment(Tools::processEnv());
    auto next = [proc, entries, context, done] {
        proc->deleteLater();
        makeSheetFrames(entries, context, done);
    };
    QObject::connect(proc, &QProcess::finished, context, [proc, next](int code, QProcess::ExitStatus st) {
        if(st != QProcess::NormalExit || code != 0)
            spdlog::warn("sheet frame failed: {}", proc->readAllStandardError().toStdString());
        next();
    });
    QObject::connect(proc, &QProcess::errorOccurred, context, [next](QProcess::ProcessError err) {
        if(err == QProcess::FailedToStart)
            next(); // other errors are followed by finished()
    });
    proc->start(ffmpeg, frameArgs(sheetVideoPath(entry), playLength(entry) / 2.0, sheetFramePath(entry)));
}

void probeVideo(const QString& input, QObject* context, std::function<void(std::optional<VideoInfo>)> done)
{
    const QString ffmpeg = Tools::exportFfmpeg();
    if(ffmpeg.isEmpty())
    {
        done(std::nullopt);
        return;
    }

    auto* proc = new QProcess(context);
    proc->setProcessEnvironment(Tools::processEnv());
    // The exit code says nothing: without an output file ffmpeg always "fails".
    QObject::connect(proc, &QProcess::finished, context, [proc, done](int, QProcess::ExitStatus) {
        const QString printout = QString::fromUtf8(proc->readAllStandardError());
        proc->deleteLater();
        done(parseProbe(printout));
    });
    QObject::connect(proc, &QProcess::errorOccurred, context, [proc, done](QProcess::ProcessError err) {
        if(err != QProcess::FailedToStart)
            return; // other errors are followed by finished()
        proc->deleteLater();
        done(std::nullopt);
    });
    proc->start(ffmpeg, probeArgs(input));
}

} // namespace qr
