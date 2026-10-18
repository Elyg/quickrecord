#include "app/Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace qr
{

namespace
{
QString dataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

#ifdef Q_OS_MACOS
constexpr auto kLaunchAgentLabel = "com.etitas.quickrecord";

QString autostartPath()
{
    return QDir::homePath() + "/Library/LaunchAgents/" + kLaunchAgentLabel + ".plist";
}
#else
QString autostartPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/autostart/quickrecord.desktop";
}
#endif

// Explicit path so it doesn't depend on Qt's organization-name defaults.
QString configPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/quickrecord/quickrecord.conf";
}
} // namespace

Settings Settings::load()
{
    QSettings s(configPath(), QSettings::IniFormat);
    Settings out;
    out.theme = s.value("ui/theme", out.theme).toString();
    out.historyTileWidth = s.value("ui/historyTileWidth", out.historyTileWidth).toInt();
    out.textScale = s.value("ui/textScale", out.textScale).toInt();
    out.hotkey = s.value("record/hotkey", out.hotkey).toString();
    out.fps = s.value("record/fps", out.fps).toInt();
    out.drawMouse = s.value("record/drawMouse", out.drawMouse).toBool();
    out.recordMic = s.value("record/mic", out.recordMic).toBool();
    out.countdown = s.value("record/countdown", out.countdown).toBool();
    out.recordInput = s.value("record/input", out.recordInput).toBool();
    out.inputKeys = s.value("input/keys", out.inputKeys).toBool();
    out.inputClicks = s.value("input/clicks", out.inputClicks).toBool();
    out.inputPosition = s.value("input/position", out.inputPosition).toString();
    out.lastRegion = s.value("record/lastRegion").toRect();
    out.screenshotHotkey = s.value("screenshot/hotkey", out.screenshotHotkey).toString();
    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    out.screenshotDir = s.value("screenshot/dir", pictures + "/quickrecord").toString();

    const QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    out.exportDir = s.value("export/dir", videos + "/quickrecord").toString();
    out.lastFormat = s.value("export/lastFormat", out.lastFormat).toString();
    out.gifFps = s.value("export/gifFps", out.gifFps).toInt();
    out.gifMaxWidth = s.value("export/gifMaxWidth", out.gifMaxWidth).toInt();
    out.gifQuality = s.value("export/gifQuality", out.gifQuality).toInt();
    out.mp4Fps = s.value("export/mp4Fps", out.mp4Fps).toInt();
    out.mp4MaxWidth = s.value("export/mp4MaxWidth", out.mp4MaxWidth).toInt();
    out.mp4Audio = s.value("export/mp4Audio", out.mp4Audio).toBool();
    out.textPosition = s.value("text/position", out.textPosition).toString();
    out.textSize = s.value("text/size", out.textSize).toInt();
    out.textColor = s.value("text/color", out.textColor).toString();
    out.textBackground = s.value("text/background", out.textBackground).toBool();
    out.drawColor = s.value("draw/color", out.drawColor).toString();
    out.drawWidth = s.value("draw/width", out.drawWidth).toInt();
    return out;
}

void Settings::save() const
{
    QSettings s(configPath(), QSettings::IniFormat);
    s.setValue("ui/theme", theme);
    s.setValue("ui/historyTileWidth", historyTileWidth);
    s.setValue("ui/textScale", textScale);
    s.setValue("record/hotkey", hotkey);
    s.setValue("record/fps", fps);
    s.setValue("record/drawMouse", drawMouse);
    s.setValue("record/mic", recordMic);
    s.setValue("record/countdown", countdown);
    s.setValue("record/input", recordInput);
    s.setValue("input/keys", inputKeys);
    s.setValue("input/clicks", inputClicks);
    s.setValue("input/position", inputPosition);
    s.setValue("record/lastRegion", lastRegion);
    s.setValue("screenshot/hotkey", screenshotHotkey);
    s.setValue("screenshot/dir", screenshotDir);
    s.setValue("export/dir", exportDir);
    s.setValue("export/lastFormat", lastFormat);
    s.setValue("export/gifFps", gifFps);
    s.setValue("export/gifMaxWidth", gifMaxWidth);
    s.setValue("export/gifQuality", gifQuality);
    s.setValue("export/mp4Fps", mp4Fps);
    s.setValue("export/mp4MaxWidth", mp4MaxWidth);
    s.setValue("export/mp4Audio", mp4Audio);
    s.setValue("text/position", textPosition);
    s.setValue("text/size", textSize);
    s.setValue("text/color", textColor);
    s.setValue("text/background", textBackground);
    s.setValue("draw/color", drawColor);
    s.setValue("draw/width", drawWidth);
}

QString Settings::mastersDir()
{
    return dataDir() + "/masters";
}

QString Settings::shotsDir()
{
    return dataDir() + "/screenshots";
}

QString Settings::thumbsDir()
{
    return dataDir() + "/thumbs";
}

QString Settings::indexPath()
{
    return dataDir() + "/index.json";
}

QString Settings::cacheDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
}

bool Settings::autostartEnabled()
{
    return QFile::exists(autostartPath());
}

bool Settings::setAutostart(bool enabled)
{
    const QString path = autostartPath();
    if(!enabled)
        return !QFile::exists(path) || QFile::remove(path);

    // Inside an AppImage, applicationFilePath() is the bundled loader; $APPIMAGE is the real file.
    QString exec = qEnvironmentVariable("APPIMAGE");
    if(exec.isEmpty())
        exec = QCoreApplication::applicationFilePath();

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if(!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
#ifdef Q_OS_MACOS
    // A per-user LaunchAgent; launchd picks it up at the next login.
    f.write(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                           "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                           "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                           "<plist version=\"1.0\">\n<dict>\n"
                           "  <key>Label</key><string>%1</string>\n"
                           "  <key>ProgramArguments</key><array><string>%2</string></array>\n"
                           "  <key>RunAtLoad</key><true/>\n"
                           "  <key>ProcessType</key><string>Interactive</string>\n"
                           "</dict>\n</plist>\n")
                .arg(kLaunchAgentLabel, exec.toHtmlEscaped())
                .toUtf8());
#else
    f.write(QStringLiteral("[Desktop Entry]\n"
                           "Type=Application\n"
                           "Name=QuickRecord\n"
                           "Comment=Screen region to GIF/MP4 recorder\n"
                           "Exec=\"%1\"\n"
                           "Icon=quickrecord\n"
                           "X-GNOME-Autostart-enabled=true\n")
                .arg(exec)
                .toUtf8());
#endif
    return true;
}

} // namespace qr
