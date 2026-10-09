#pragma once

#include <QString>

namespace qr
{

// Puts a file on the clipboard the way file managers do (text/uri-list +
// x-special/gnome-copied-files), plus the raw bytes for GIFs and PNGs so chat apps that
// accept pasted images get the image (or the animation) itself.
void copyFileToClipboard(const QString& path);

} // namespace qr
