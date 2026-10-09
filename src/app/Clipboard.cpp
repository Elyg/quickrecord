#include "app/Clipboard.h"

#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QUrl>

namespace qr
{

void copyFileToClipboard(const QString& path)
{
    const QUrl url = QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath());
    auto* mime = new QMimeData;
    mime->setUrls({url});
    mime->setData("x-special/gnome-copied-files", "copy\n" + url.toEncoded());

    constexpr qint64 kMaxInlineBytes = 32 * 1024 * 1024;
    const QString suffix = QFileInfo(path).suffix().toLower();
    if((suffix == "gif" || suffix == "png") && QFileInfo(path).size() <= kMaxInlineBytes)
    {
        QFile f(path);
        if(f.open(QIODevice::ReadOnly))
            mime->setData("image/" + suffix, f.readAll());
    }
    QGuiApplication::clipboard()->setMimeData(mime);
}

} // namespace qr
