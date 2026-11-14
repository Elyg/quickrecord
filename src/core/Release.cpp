#include "core/Release.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QVersionNumber>

namespace qr
{

std::optional<Release> parseRelease(const QByteArray& json)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    Release r;
    r.version = o.value("tag_name").toString();
    if(r.version.startsWith('v'))
        r.version.remove(0, 1);
    r.page = QUrl(o.value("html_url").toString());
    if(!isReleaseVersion(r.version) || !r.page.isValid() || r.page.isEmpty())
        return std::nullopt;
    for(const QJsonValue& v : o.value("assets").toArray())
    {
        const QJsonObject a = v.toObject();
        ReleaseAsset asset;
        asset.name = a.value("name").toString();
        asset.url = QUrl(a.value("browser_download_url").toString());
        asset.size = qint64(a.value("size").toDouble());
        if(!asset.name.isEmpty() && asset.url.isValid() && !asset.url.isEmpty())
            r.assets << asset;
    }
    return r;
}

bool isReleaseVersion(const QString& version)
{
    static const QRegularExpression re(QStringLiteral("^\\d+\\.\\d+\\.\\d+$"));
    return re.match(version).hasMatch();
}

bool isNewer(const QString& latest, const QString& current)
{
    if(!isReleaseVersion(latest) || !isReleaseVersion(current))
        return false;
    return QVersionNumber::fromString(latest) > QVersionNumber::fromString(current);
}

std::optional<ReleaseAsset> findAsset(const Release& release, Package package)
{
    QString suffix;
    switch(package)
    {
        case Package::AppImage: suffix = QStringLiteral("-x86_64.AppImage"); break;
        case Package::WindowsSetup: suffix = QStringLiteral("-windows-x64-setup.exe"); break;
        case Package::MacDmg: suffix = QStringLiteral("-macos-arm64.dmg"); break;
    }
    for(const ReleaseAsset& a : release.assets)
        if(a.name.startsWith(QStringLiteral("QuickRecord-")) && a.name.endsWith(suffix))
            return a;
    return std::nullopt;
}

} // namespace qr
