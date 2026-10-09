#include "app/Tray.h"

#include "app/Icons.h"

#include <QMenu>
#include <QSystemTrayIcon>

namespace qr
{

Tray::Tray(QObject* parent)
    : QObject(parent)
    , m_icon(new QSystemTrayIcon(this))
    , m_menu(new QMenu)
{
    m_record = m_menu->addAction("Record…", this, &Tray::recordTriggered);
    m_recordLast = m_menu->addAction("Record last area", this, &Tray::recordLastTriggered);
    m_screenshot = m_menu->addAction("Screenshot…", this, &Tray::screenshotTriggered);
    m_pause = m_menu->addAction("Pause", this, &Tray::pauseTriggered);
    m_cancel = m_menu->addAction("Cancel recording", this, &Tray::cancelTriggered);
    m_menu->addSeparator();
    m_menu->addAction("History…", this, &Tray::historyTriggered);
    m_menu->addAction("Settings…", this, &Tray::settingsTriggered);
    m_menu->addSeparator();
    m_menu->addAction("Quit", this, &Tray::quitTriggered);

    m_icon->setContextMenu(m_menu);
    connect(m_icon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if(r == QSystemTrayIcon::Trigger)
            emit recordTriggered();
        else if(r == QSystemTrayIcon::MiddleClick)
            emit historyTriggered();
    });
    setState(Recorder::State::Idle);
    m_icon->show();
}

Tray::~Tray()
{
    delete m_menu;
}

void Tray::setHotkeyText(const QString& text)
{
    m_hotkey = text;
    setState(m_state);
}

void Tray::setScreenshotHotkeyText(const QString& text)
{
    m_shotHotkey = text;
    setState(m_state);
}

void Tray::setLastRegionAvailable(bool available)
{
    m_lastAvailable = available;
    setState(m_state);
}

void Tray::setState(Recorder::State state)
{
    m_state = state;
    const QString hk = m_hotkey.isEmpty() ? QString() : "\t" + m_hotkey;
    using S = Recorder::State;
    switch(state)
    {
        case S::Idle:
            m_icon->setIcon(icons::trayIdle());
            m_icon->setToolTip("QuickRecord — click to record a region");
            m_record->setText("Record region…" + hk);
            break;
        case S::Countdown:
        case S::Recording:
            m_icon->setIcon(icons::trayRecording());
            m_icon->setToolTip("QuickRecord — recording (click to stop)");
            m_record->setText("Stop recording" + hk);
            break;
        case S::Paused:
            m_icon->setIcon(icons::trayPaused());
            m_icon->setToolTip("QuickRecord — paused (click to stop)");
            m_record->setText("Stop recording" + hk);
            break;
        case S::Finalizing:
            m_icon->setIcon(icons::trayBusy());
            m_icon->setToolTip("QuickRecord — finishing…");
            m_record->setText("Finishing…");
            break;
    }
    m_record->setEnabled(state != S::Finalizing);
    m_recordLast->setVisible(state == S::Idle && m_lastAvailable);
    m_screenshot->setText("Screenshot…" + (m_shotHotkey.isEmpty() ? QString() : "\t" + m_shotHotkey));
    m_screenshot->setVisible(state == S::Idle);
    m_pause->setVisible(state == S::Recording || state == S::Paused);
    m_pause->setText(state == S::Paused ? "Resume" : "Pause");
    m_cancel->setVisible(state != S::Idle && state != S::Finalizing);
}

void Tray::setExporting(bool busy)
{
    if(m_state == Recorder::State::Idle)
        m_icon->setIcon(busy ? icons::trayBusy() : icons::trayIdle());
}

void Tray::message(const QString& title, const QString& text, bool error)
{
    m_icon->showMessage(title, text, error ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information,
                        error ? 8000 : 3000);
}

} // namespace qr
