#pragma once

#include <QRect>
#include <QString>

namespace qr
{

// User preferences, persisted in ~/.config/quickrecord/quickrecord.conf.
struct Settings
{
    // Recording
    QString hotkey = "Meta+Shift+R";
    int fps = 30;
    bool drawMouse = true;
    bool recordMic = false;
    bool countdown = false;
    QRect lastRegion; // physical pixels of the last recorded/screenshotted area; null if none yet

    // Screenshots
    QString screenshotHotkey = "Meta+Shift+S";
    QString screenshotDir; // where saved screenshots go

    // Export defaults (the editor remembers the last values used)
    QString exportDir;
    QString lastFormat = "gif"; // "gif" | "mp4"
    int gifFps = 15;
    int gifMaxWidth = 800;
    int gifQuality = 90;
    int mp4Fps = 0;      // 0 = source rate
    int mp4MaxWidth = 0; // 0 = source width
    bool mp4Audio = true;

    // Caption style in the editor (the text itself is per recording)
    QString textPosition = "bottom-right";
    int textSize = 28;
    QString textColor = "#ffffff";
    bool textBackground = true;

    static Settings load();
    void save() const;

    static QString mastersDir(); // ~/.local/share/quickrecord/masters
    static QString shotsDir();   // ~/.local/share/quickrecord/screenshots (originals)
    static QString thumbsDir();  // ~/.local/share/quickrecord/thumbs
    static QString indexPath();  // ~/.local/share/quickrecord/index.json
    static QString cacheDir();   // ~/.cache/quickrecord (clipboard-only exports)

    static bool autostartEnabled();
    static bool setAutostart(bool enabled);
};

} // namespace qr
