#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QUrl>

#include <optional>

namespace qr
{

// A release as GitHub's releases/latest API describes it.
struct ReleaseAsset
{
    QString name;
    QUrl url; // browser_download_url
    qint64 size = 0;
};

struct Release
{
    QString version; // tag_name without the "v": "0.0.13"
    QUrl page;       // html_url
    QList<ReleaseAsset> assets;
};

// Null if the JSON isn't a release (an error message, a rate limit, …).
std::optional<Release> parseRelease(const QByteArray& json);

// Whether `latest` is a newer X.Y.Z than `current`. False when either isn't one (e.g. "dev").
bool isNewer(const QString& latest, const QString& current);
// Whether `version` is an X.Y.Z release version (not "dev").
bool isReleaseVersion(const QString& version);

// The packages that can update themselves in place.
enum class Package
{
    AppImage,     // QuickRecord-<v>-x86_64.AppImage
    WindowsSetup, // QuickRecord-<v>-windows-x64-setup.exe
    MacDmg,       // QuickRecord-<v>-macos-arm64.dmg
};

std::optional<ReleaseAsset> findAsset(const Release& release, Package package);

} // namespace qr
