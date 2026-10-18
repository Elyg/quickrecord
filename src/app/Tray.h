#pragma once

#include "app/Recorder.h"

#include <QObject>

class QAction;
class QMenu;
class QSystemTrayIcon;

namespace qr
{

class Tray : public QObject
{
    Q_OBJECT

public:
    explicit Tray(QObject* parent = nullptr);
    ~Tray() override;

    void setState(Recorder::State state);
    void setHotkeyText(const QString& text);
    void setScreenshotHotkeyText(const QString& text);
    void setLaserHotkeyText(const QString& text);
    void setLaser(bool on);
    void setLastRegionAvailable(bool available);
    void setExporting(bool busy);
    void message(const QString& title, const QString& text, bool error = false);

signals:
    void recordTriggered(); // start a selection, or stop when recording
    void recordLastTriggered();
    void screenshotTriggered();
    void pauseTriggered();
    void cancelTriggered();
    void laserTriggered();
    void historyTriggered();
    void settingsTriggered();
    void quitTriggered();

private:
    QSystemTrayIcon* m_icon;
    QMenu* m_menu;
    QMenu* m_dockMenu = nullptr; // macOS: the same actions on the Dock icon
    QAction* m_record;
    QAction* m_recordLast;
    QAction* m_screenshot;
    QAction* m_pause;
    QAction* m_cancel;
    QAction* m_laser;
    QString m_hotkey;
    QString m_shotHotkey;
    QString m_laserHotkey;
    Recorder::State m_state = Recorder::State::Idle;
    bool m_lastAvailable = false;
};

} // namespace qr
