#include "app/Tools.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <spdlog/spdlog.h>
#ifdef Q_OS_WIN
#include <QAudioDevice>
#include <QMediaDevices>
#endif

namespace qr
{

namespace
{
#ifdef Q_OS_MACOS
constexpr auto kScreenDevice = "avfoundation";
constexpr auto kInstallHint = "Install ffmpeg (e.g. `brew install ffmpeg` or `nix profile install nixpkgs#ffmpeg`).";
#elif defined(Q_OS_WIN)
constexpr auto kScreenDevice = "gdigrab";
constexpr auto kInstallHint = "It belongs next to QuickRecord.exe: unpack the whole zip, or `winget install ffmpeg`.";
#else
constexpr auto kScreenDevice = "x11grab";
constexpr auto kInstallHint = "Install ffmpeg (e.g. `sudo apt install ffmpeg`).";
#endif

// What a tool is called on disk.
QString exeName(const QString& tool)
{
#ifdef Q_OS_WIN
    return tool + ".exe";
#else
    return tool;
#endif
}

struct Caps
{
    bool screen = false; // kScreenDevice
    bool pulse = false;
    bool alsa = false;
    bool dshow = false;
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
    // Long enough for Windows Defender to scan a 165 MB ffmpeg.exe on its first run: the probes
    // only fail on a timeout, and what they find is kept until the app restarts.
    constexpr int kTimeoutMs = 30000;
    QProcess p;
    p.setProcessEnvironment(Tools::processEnv());
    p.start(program, args);
    if(!p.waitForFinished(kTimeoutMs))
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
        c.screen |= cols[1] == kScreenDevice;
        c.pulse |= cols[1] == "pulse";
        c.alsa |= cols[1] == "alsa";
        c.dshow |= cols[1] == "dshow";
    }
    c.x264 = hasRow(run(ffmpeg, {"-hide_banner", "-encoders"}), "libx264");
    spdlog::info("ffmpeg candidate {}: {}={} pulse={} alsa={} dshow={} libx264={}", ffmpeg.toStdString(),
                 kScreenDevice, c.screen, c.pulse, c.alsa, c.dshow, c.x264);
    return *cache.insert(ffmpeg, c);
}

QStringList searchPath()
{
    QStringList dirs = qEnvironmentVariable("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
#ifdef Q_OS_MACOS
    // Apps opened from Finder get only the system PATH: also look where Homebrew and nix
    // (user profile, nix-darwin, the default profile) install tools.
    const QString home = QDir::homePath();
    for(const QString& dir : {QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin"),
                              home + "/.nix-profile/bin", "/etc/profiles/per-user/" + qEnvironmentVariable("USER") + "/bin",
                              QStringLiteral("/run/current-system/sw/bin"),
                              QStringLiteral("/nix/var/nix/profiles/default/bin")})
        if(!dirs.contains(dir))
            dirs << dir;
#endif
    return dirs;
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
    add(bundledDir() + "/" + exeName("ffmpeg"));
    for(const QString& dir : searchPath())
        add(dir + "/" + exeName("ffmpeg"));
    return out;
}
} // namespace

Tools::Capture Tools::capture(bool wantMic, QString* error)
{
    const QStringList candidates = ffmpegCandidates();
    if(candidates.isEmpty())
    {
        *error = QStringLiteral("ffmpeg was not found. ") + kInstallHint;
        return {};
    }

    if(wantMic)
    {
#ifdef Q_OS_MACOS
        // avfoundation captures the microphone itself.
        for(const QString& f : candidates)
            if(const Caps& c = probe(f); c.screen && c.x264)
                return {f, AudioInput::AVFoundation, {}};
#endif
#ifdef Q_OS_WIN
        const QString systemMic = QMediaDevices::defaultAudioInput().description();
        for(const QString& f : candidates)
            if(const Caps& c = probe(f); c.screen && c.x264 && c.dshow)
                if(const QString device = pickDshowAudioDevice(parseDshowAudioDevices(run(f, dshowListArgs())), systemMic);
                   !device.isEmpty())
                    return {f, AudioInput::DirectShow, {}, device};
#endif
        for(const QString& f : candidates)
            if(const Caps& c = probe(f); c.screen && c.x264 && c.pulse)
                return {f, AudioInput::Pulse, {}};
        for(const QString& f : candidates)
            if(const Caps& c = probe(f); c.screen && c.x264 && c.alsa)
                return {f, AudioInput::Alsa,
                        "No ffmpeg with PulseAudio input found; recording the mic through ALSA instead. "
                        "Install your distro's ffmpeg if the mic stays silent."};
    }
    for(const QString& f : candidates)
        if(const Caps& c = probe(f); c.screen && c.x264)
            return {f, AudioInput::None,
                    wantMic ? "No ffmpeg with microphone input found; recording without audio." : QString()};

    *error = QStringLiteral("None of the ffmpeg builds found supports screen capture (%1) with libx264:\n")
                 .arg(kScreenDevice) +
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
    const QString bundled = bundledDir() + "/" + exeName("gifski");
    if(QFileInfo(bundled).isExecutable())
        return bundled;
    for(const QString& dir : searchPath())
        if(QFileInfo(dir + "/" + exeName("gifski")).isExecutable())
            return dir + "/" + exeName("gifski");
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
