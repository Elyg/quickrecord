#include "app/Updater.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

namespace qr
{

namespace
{
constexpr auto kLatestUrl = "https://api.github.com/repos/Elyg/quickrecord/releases/latest";

// macOS: runs after the app has quit ($1 its pid, $2 the downloaded dmg, $3 the app bundle).
// The new app is copied next to the old one first and only then swapped in, so a failed copy
// leaves the old app as it was.
constexpr auto kMacSwap = R"SH(
pid=$1; dmg=$2; app=$3
while kill -0 "$pid" 2>/dev/null; do sleep 0.2; done
mnt=$(mktemp -d "${TMPDIR:-/tmp}/quickrecord-update.XXXXXX") || exit 1
if hdiutil attach -nobrowse -noautoopen -readonly -mountpoint "$mnt" "$dmg" >/dev/null; then
    new=$(ls -d "$mnt"/*.app 2>/dev/null | head -n 1)
    if [ -n "$new" ] && rm -rf "$app.update" && ditto "$new" "$app.update"; then
        rm -rf "$app.old"
        if mv "$app" "$app.old"; then
            if mv "$app.update" "$app"; then rm -rf "$app.old"; else mv "$app.old" "$app"; fi
        fi
    fi
    rm -rf "$app.update"
    hdiutil detach "$mnt" -quiet || hdiutil detach "$mnt" -force -quiet
fi
rmdir "$mnt" 2>/dev/null
rm -f "$dmg"
xattr -dr com.apple.quarantine "$app" 2>/dev/null
open "$app"
)SH";

QNetworkRequest request(const QUrl& url, const QString& version)
{
    QNetworkRequest r(url);
    // GitHub's API wants a User-Agent; nothing else identifies the user.
    r.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("QuickRecord/") + version);
    r.setTransferTimeout(30000);
    return r;
}
} // namespace

Updater::Updater(QString currentVersion, QObject* parent)
    : QObject(parent)
    , m_current(std::move(currentVersion))
    , m_network(new QNetworkAccessManager(this))
{
#ifdef Q_OS_WIN
    if(QFile::exists(QCoreApplication::applicationDirPath() + "/Uninstall.exe"))
        m_mode = Mode::WindowsInstaller;
#elif defined(Q_OS_MACOS)
    // Contents/MacOS/QuickRecord → QuickRecord.app. Its folder must take the new app: not the
    // case when it runs from the dmg, or from a read-only "translocated" copy macOS makes of a
    // quarantined app that was never moved.
    QDir bundle(QCoreApplication::applicationDirPath());
    if(bundle.cdUp() && bundle.cdUp() && bundle.dirName().endsWith(".app") &&
       QFileInfo(QFileInfo(bundle.absolutePath()).absolutePath()).isWritable())
    {
        m_mode = Mode::MacApp;
        m_appBundle = bundle.absolutePath();
    }
#elif defined(Q_OS_LINUX)
    // The AppImage replaces itself, so its folder must take a new file (QSaveFile writes one
    // next to it, then renames it over the old).
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    if(!appImage.isEmpty() && QFileInfo(appImage).isFile() && QFileInfo(QFileInfo(appImage).absolutePath()).isWritable())
    {
        m_mode = Mode::AppImage;
        m_appImage = appImage;
    }
#endif
}

Updater::~Updater()
{
    if(m_reply)
    {
        m_reply->disconnect(this); // abort() finishes it right away; nothing to report now
        m_reply->abort();
    }
}

void Updater::check()
{
    if(m_reply)
        return;
    QNetworkRequest r = request(QUrl(kLatestUrl), m_current);
    r.setRawHeader("Accept", "application/vnd.github+json");
    m_reply = m_network->get(r);
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = m_reply] {
        reply->deleteLater();
        m_reply = nullptr; // done: install() may start right away
        Result result;
        if(reply->error() != QNetworkReply::NoError)
        {
            result.message = errorText(reply);
            emit checked(result);
            return;
        }
        m_release = parseRelease(reply->readAll());
        if(!m_release)
        {
            result.message = "GitHub's answer wasn't a release.";
            emit checked(result);
            return;
        }
        result.latest = m_release->version;
        if(!isReleaseVersion(m_current))
            result.status = Status::DevBuild;
        else if(!isNewer(m_release->version, m_current))
            result.status = Status::UpToDate;
        else
        {
            result.status = Status::Available;
            if(m_mode == Mode::AppImage)
                m_asset = findAsset(*m_release, Package::AppImage);
            else if(m_mode == Mode::WindowsInstaller)
                m_asset = findAsset(*m_release, Package::WindowsSetup);
            else if(m_mode == Mode::MacApp)
                m_asset = findAsset(*m_release, Package::MacDmg);
            result.canInstall = m_asset.has_value();
        }
        emit checked(result);
    });
}

void Updater::install()
{
    if(m_reply || !m_asset)
        return;
    const QString target =
        m_mode == Mode::AppImage
            ? m_appImage
            : QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/" + m_asset->name;
    m_file = std::make_unique<QSaveFile>(target);
    if(!m_file->open(QIODevice::WriteOnly))
    {
        emit failed("Can't write " + QDir::toNativeSeparators(target) + ": " + m_file->errorString());
        m_file.reset();
        return;
    }
    m_downloaded = m_mode == Mode::AppImage ? QString() : target;

    m_reply = m_network->get(request(m_asset->url, m_current)); // follows GitHub's redirect
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        if(m_file && m_reply)
            m_file->write(m_reply->readAll());
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if(total <= 0)
            total = m_asset->size;
        if(total > 0)
            emit progress(int(received * 100 / total));
    });
    connect(m_reply, &QNetworkReply::finished, this, &Updater::downloadFinished);
}

void Updater::downloadFinished()
{
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();
    std::unique_ptr<QSaveFile> file = std::move(m_file);
    if(reply->error() != QNetworkReply::NoError)
    {
        file->cancelWriting();
        emit failed(errorText(reply));
        return;
    }
    file->write(reply->readAll());
    if(m_asset->size > 0 && file->size() != m_asset->size)
    {
        file->cancelWriting();
        emit failed(QString("The download is incomplete (%1 of %2 bytes).").arg(file->size()).arg(m_asset->size));
        return;
    }
    if(!file->commit())
    {
        emit failed("Can't save the update: " + file->errorString());
        return;
    }
    if(m_mode == Mode::AppImage)
        QFile::setPermissions(m_appImage, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup |
                                              QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);
    emit installed();
}

bool Updater::finish()
{
    if(m_mode == Mode::AppImage)
    {
        // The new AppImage can only take over once this instance has let go of its socket.
        const QString wait = QStringLiteral("while kill -0 \"$1\" 2>/dev/null; do sleep 0.2; done; exec \"$2\"");
        QProcess::startDetached("/bin/sh", {"-c", wait, "quickrecord-update",
                                            QString::number(QCoreApplication::applicationPid()), m_appImage});
        return true;
    }
    if(m_mode == Mode::MacApp)
    {
        if(QProcess::startDetached("/bin/sh", {"-c", QString::fromUtf8(kMacSwap), "quickrecord-update",
                                               QString::number(QCoreApplication::applicationPid()), m_downloaded,
                                               m_appBundle}))
            return true;
        emit failed("Couldn't start the update.");
        return false;
    }
    if(m_mode == Mode::WindowsInstaller)
    {
        // It quits this app itself (QuickRecord.exe --quit), installs and starts the new one.
        if(!QProcess::startDetached(m_downloaded, {"/S", "/RELAUNCH"}))
            emit failed("Couldn't start the installer: " + QDir::toNativeSeparators(m_downloaded));
    }
    return false;
}

void Updater::openReleasePage() const
{
    QDesktopServices::openUrl(m_release ? m_release->page
                                        : QUrl("https://github.com/Elyg/quickrecord/releases/latest"));
}

QString Updater::errorText(QNetworkReply* reply) const
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if(status == 403 || status == 429)
        return "GitHub is limiting requests right now; try again in a while.";
    if(status == 404)
        return "No release found on GitHub.";
    if(reply->error() == QNetworkReply::OperationCanceledError)
        return "Cancelled.";
    return "Couldn't reach GitHub: " + reply->errorString();
}

} // namespace qr
