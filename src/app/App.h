#pragma once

#include "app/GlobalHotkey.h"
#include "app/InputMonitor.h"
#include "app/Recorder.h"
#include "app/RegionOverlay.h"
#include "app/Settings.h"
#include "app/Tray.h"
#include "core/HistoryStore.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QFileSystemWatcher>
#include <QRect>
#include <QTimer>

namespace qr
{

class HistoryWindow;
class RecordBar;
class RecordFrame;
class SettingsDialog;

// Tray daemon: hotkey → overlay → recorder → editor (or screenshot → screenshot editor),
// plus history and settings.
class App : public QObject
{
    Q_OBJECT

public:
    App();
    ~App() override;

    // Commands from the CLI / a second instance: toggle, last, screenshot, history, settings, quit.
    void handleCommand(const QString& command);

private:
    void toggle();
    void screenshot();
    void startSelection(CapturePurpose purpose);
    void recordLastRegion();
    void onRegionSelected(QRect physical, CapturePurpose purpose);
    void takeScreenshot(QRect physical);
    void captureScreenshot(const QRect& physical);
    void onStateChanged(Recorder::State state);
    void onRecordingFinished(const QString& id, const QString& master, double duration, bool hasAudio);
    void onRecordingFailed(const QString& message);
    void teardownRecordingUi();
    void placeBar(const QRect& regionLogical, const QRect& screenLogical);

    void showHistory();
    void showSettings();
    void openEditor(const QString& id);
    void confirmDelete(const QString& id, QWidget* parent);
    void deleteRecording(const QString& id);
    void saveStore();
    void refreshHistory();
    void applyHotkeys();
    void bindHotkey(GlobalHotkey& hotkey, const QString& sequence, const QString& title,
                    void (Tray::*setText)(const QString&));
    void quit();
    void watchFolders();
    void pruneMissing();

    Settings m_settings;
    HistoryStore m_store;
    Recorder m_recorder;
    GlobalHotkey m_hotkey;
    GlobalHotkey m_shotHotkey;
    InputMonitor m_input; // keys & clicks, only while recording with Settings::recordInput
    Tray m_tray;

    QPointer<RegionOverlay> m_overlay;
    RecordFrame* m_frame = nullptr;
    RecordBar* m_bar = nullptr;
    QRect m_recordRect; // physical
    QPointer<HistoryWindow> m_history;
    QPointer<SettingsDialog> m_settingsDialog;
    QHash<QString, QPointer<QWidget>> m_editors; // EditorWindow / ScreenshotEditor by entry id
    QFileSystemWatcher m_watcher; // folders holding history files, so deleting one updates History
    QTimer m_pruneTimer;
};

} // namespace qr
