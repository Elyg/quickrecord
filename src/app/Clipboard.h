#pragma once

#include <QString>
#include <QStringList>

namespace qr
{

// Puts a file on the clipboard the way file managers do (text/uri-list +
// x-special/gnome-copied-files), plus the raw bytes for GIFs and PNGs so chat apps that
// accept pasted images get the image (or the animation) itself.
void copyFileToClipboard(const QString& path);
// Several files, as a file manager copies them (one: as copyFileToClipboard()).
void copyFilesToClipboard(const QStringList& paths);

} // namespace qr
