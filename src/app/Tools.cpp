#include "app/Tools.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <spdlog/spdlog.h>

namespace qr
{

namespace
{
struct Caps
{
    bool x11grab = false;
    bool pulse = false;
    bool alsa = false;
    bool x264 = false;
};

QString bundledDir()
{
    const QString appDir = qEnvironmentVariable("QUICKRECORD_APPDIR");
    if(!appDir.isEmpty())
        return appDir + "/usr/bin";
    return QCoreApplication::applicationDirPath();
}

QString run(const QString& program, const QStringList& args)
{
    QProcess p;
    p.setProcessEnvironment(Tools::processEnv());
    p.start(program, args);
    if(!p.waitForFinished(5000))
    {
        p.kill();
        return {};
    }
    return QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError());
}

// Matches a `-devices`/`-encoders` table row such as " D  x11grab   X11 screen capture".
bool hasRow(const QString& table, const QString& name)
{
    for(const QString& line : table.split('\n'))
    {
        const QStringList cols = line.simplified().split(' ');
        if(cols.size() >= 2 && cols[1] == name)
            return true;
    }
    return false;
}

const Caps& probe(const QString& ffmpeg)
{
    static QHash<QString, Caps> cache;
    auto it = cache.find(ffmpeg);
    if(it != cache.end())
        return *it;

    Caps c;
    const QString devices = run(ffmpeg, {"-hide_banner", "-devices"});
    // Only demuxers ("D" in the first column) count as capture inputs.
    for(const QString& line : devices.split('\n'))
    {
        const QStringList cols = line.simplified().split(' ');
        if(cols.size() < 2 || !cols[0].contains('D'))
            continue;
        c.x11grab |= cols[1] == "x11grab";
        c.pulse |= cols[1] == "pulse";
        c.alsa |= cols[1] == "alsa";
    }
    c.x264 = hasRow(run(ffmpeg, {"-hide_banner", "-encoders"}), "libx264");
    spdlog::info("ffmpeg candidate {}: x11grab={} pulse={} alsa={} libx264={}", ffmpeg.toStdString(), c.x11grab,
                 c.pulse, c.alsa, c.x264);
    return *cache.insert(ffmpeg, c);
}

QStringList ffmpegCandidates()
{
    QStringList out;
    auto add = [&](const QString& path) {
        const QFileInfo fi(path);
        if(!fi.isFile() || !fi.isExecutable())
            return;
        const QString canonical = fi.canonicalFilePath();
        if(!out.contains(canonical))
            out << canonical;
    };
    add(bundledDir() + "/ffmpeg");
    for(const QString& dir : qEnvironmentVariable("PATH").split(':', Qt::SkipEmptyParts))
        add(dir + "/ffmpeg");
    return out;
}
} // namespace

Tools::Capture Tools::capture(bool wantMic, QString* error)
{
    const QStringList candidates = ffmpegCandidates();
    if(candidates.isEmpty())
    {
        *error = "ffmpeg was not found. Install ffmpeg (e.g. `sudo apt install ffmpeg`).";
        return {};
    }

    if(wantMic)
    {
        for(const QString& f : candidates)
            if(const Caps& c = probe(f); c.x11grab && c.x264 && c.pulse)
                return {f, AudioInput::Pulse, {}};
        for(const QString& f : candidates)
            if(const Caps& c = probe(f); c.x11grab && c.x264 && c.alsa)
                return {f, AudioInput::Alsa,
                        "No ffmpeg with PulseAudio input found; recording the mic through ALSA instead. "
                        "Install your distro's ffmpeg if the mic stays silent."};
    }
    for(const QString& f : candidates)
        if(const Caps& c = probe(f); c.x11grab && c.x264)
            return {f, AudioInput::None,
                    wantMic ? "No ffmpeg with microphone input found; recording without audio." : QString()};

    *error = "None of the ffmpeg builds found supports X11 capture (x11grab) with libx264:\n" +
             candidates.join('\n');
    return {};
}

QString Tools::exportFfmpeg()
{
    for(const QString& f : ffmpegCandidates())
        if(probe(f).x264)
            return f;
    return {};
}

QString Tools::gifski()
{
    const QString bundled = bundledDir() + "/gifski";
    if(QFileInfo(bundled).isExecutable())
        return bundled;
    for(const QString& dir : qEnvironmentVariable("PATH").split(':', Qt::SkipEmptyParts))
        if(QFileInfo(dir + "/gifski").isExecutable())
            return dir + "/gifski";
    return {};
}

QProcessEnvironment Tools::processEnv()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove("LD_LIBRARY_PATH");
    env.remove("QT_PLUGIN_PATH");
    env.remove("QML2_IMPORT_PATH");
    return env;
}

} // namespace qr
