#include "app/App.h"

#include "app/EditorWindow.h"
#include "app/HistoryWindow.h"
#include "app/RecordBar.h"
#include "app/RecordFrame.h"
#include "app/ScreenshotEditor.h"
#include "app/SettingsDialog.h"
#include "app/Theme.h"
#include "app/Thumbnailer.h"
#include "app/Platform.h"
#include "app/Tools.h"
#include "core/Geometry.h"

#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QScreen>
#include <QSet>
#include <QTimer>
#include <spdlog/spdlog.h>

namespace qr
{

namespace
{
constexpr int kMinRecordSize = 16;
constexpr int kMinShotSize = 4;
// Lets the compositor drop the picker overlay from the screen before we read the pixels.
constexpr int kGrabDelayMs = 200;
} // namespace

App::App()
    : m_settings(Settings::load())
    , m_store(Settings::indexPath())
{
    QDir().mkpath(Settings::mastersDir());
    QDir().mkpath(Settings::thumbsDir());
    QDir().mkpath(Settings::shotsDir());

    if(!m_store.load())
    {
        // Never overwrite an index we failed to read: set it aside and start fresh.
        const QString backup = Settings::indexPath() + ".corrupt-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
        QFile::rename(Settings::indexPath(), backup);
        m_tray.message("History could not be read", "The old index was moved to " + backup, true);
    }

    connect(&m_hotkey, &GlobalHotkey::activated, this, &App::toggle);
    connect(&m_shotHotkey, &GlobalHotkey::activated, this, &App::screenshot);
    connect(&m_laserHotkey, &GlobalHotkey::activated, this, &App::toggleLaser);
    connect(&m_laserEscape, &GlobalHotkey::activated, this, [this] {
        if(m_laser)
            toggleLaser();
    });
    connect(&m_tray, &Tray::laserTriggered, this, &App::toggleLaser);
    connect(&m_tray, &Tray::recordTriggered, this, &App::toggle);
    connect(&m_tray, &Tray::recordLastTriggered, this, &App::recordLastRegion);
    connect(&m_tray, &Tray::screenshotTriggered, this, &App::screenshot);
    m_tray.setLastRegionAvailable(!m_settings.lastRegion.isEmpty());
    connect(&m_tray, &Tray::pauseTriggered, &m_recorder, &Recorder::togglePause);
    connect(&m_tray, &Tray::cancelTriggered, &m_recorder, &Recorder::cancel);
    connect(&m_tray, &Tray::historyTriggered, this, &App::showHistory);
    connect(&m_tray, &Tray::settingsTriggered, this, &App::showSettings);
    connect(&m_tray, &Tray::quitTriggered, this, &App::quit);
    platform::onReopen([this] { showHistory(); });

    connect(&m_recorder, &Recorder::stateChanged, this, &App::onStateChanged);
    connect(&m_recorder, &Recorder::countdownTick, this, [this](int n) {
        if(m_frame)
            m_frame->setCountdown(n);
    });
    connect(&m_recorder, &Recorder::elapsedChanged, this, [this](qint64 ms) {
        if(m_bar)
            m_bar->setElapsed(ms);
    });
    connect(&m_recorder, &Recorder::finished, this, &App::onRecordingFinished);
    connect(&m_input, &InputMonitor::keyPressed, this, [this](const QString& label, bool typed) {
        InputEvent e;
        e.label = label;
        e.typed = typed;
        m_recorder.addInputEvent(e);
    });
    connect(&m_input, &InputMonitor::mousePressed, this, [this](QPoint global) {
        if(m_laser)
            return; // drawing with the laser, not clicking
        // Recording pixels; clicks outside the recorded area aren't shown.
        const QList<ScreenInfo> screens = platform::screens();
        const int si = screenAtLogical(screens, global);
        if(si < 0)
            return;
        InputEvent e;
        e.kind = InputEvent::Kind::Click;
        e.pos = toPhysical(QRect(global, QSize(1, 1)), screens[si]).topLeft() - m_recordRect.topLeft();
        if(QRect(QPoint(), m_recordRect.size()).contains(e.pos))
            m_recorder.addInputEvent(e);
    });
    connect(&m_recorder, &Recorder::failed, this, &App::onRecordingFailed);

    applyHotkeys();

    // Files deleted outside the app (e.g. in the file manager) drop out of the history.
    m_pruneTimer.setSingleShot(true);
    m_pruneTimer.setInterval(300); // one pass for a batch of deletions
    connect(&m_pruneTimer, &QTimer::timeout, this, &App::pruneMissing);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_pruneTimer, qOverload<>(&QTimer::start));
    pruneMissing();
}

App::~App()
{
    m_recorder.cancel();
    teardownRecordingUi();
}

void App::handleCommand(const QString& command)
{
    if(command == "toggle")
        toggle();
    else if(command == "last")
    {
        if(m_recorder.state() == Recorder::State::Idle)
            recordLastRegion();
        else
            toggle(); // same key stops it again
    }
    else if(command == "screenshot")
        screenshot();
    else if(command == "history")
        showHistory();
    else if(command == "settings")
        showSettings();
    else if(command == "quit")
        quit();
    else if(command.startsWith("edit "))
    {
        const QString id = command.mid(5).trimmed();
        if(m_store.find(id))
            openEditor(id);
        else
            showHistory(); // deleted since: show what there is
    }
}

void App::toggle()
{
    switch(m_recorder.state())
    {
        case Recorder::State::Idle:
            if(!m_overlay)
                startSelection(CapturePurpose::Record);
            else if(m_overlay->purpose() == CapturePurpose::Record)
                m_overlay->close(); // hotkey again while picking = cancel
            else
                m_overlay->setPurpose(CapturePurpose::Record);
            break;
        case Recorder::State::Countdown:
        case Recorder::State::Recording:
        case Recorder::State::Paused: m_recorder.stop(); break;
        case Recorder::State::Finalizing: break;
    }
}

void App::screenshot()
{
    if(m_recorder.state() != Recorder::State::Idle)
        return; // the picker would end up in the recording
    if(!m_overlay)
        startSelection(CapturePurpose::Screenshot);
    else if(m_overlay->purpose() == CapturePurpose::Screenshot)
        m_overlay->close(); // hotkey again while picking = cancel
    else
        m_overlay->setPurpose(CapturePurpose::Screenshot);
}

void App::startSelection(CapturePurpose purpose)
{
    if(m_laser)
        toggleLaser(); // it would take the picker's clicks
    m_overlay = new RegionOverlay(platform::screens(), platform::visibleWindowRects(), m_settings.lastRegion, purpose);
    connect(m_overlay, &RegionOverlay::selected, this, &App::onRegionSelected);
    m_overlay->start();
}

void App::recordLastRegion()
{
    if(m_recorder.state() != Recorder::State::Idle || m_overlay)
        return;
    if(m_settings.lastRegion.isEmpty())
        startSelection(CapturePurpose::Record); // nothing remembered yet: pick one
    else
        onRegionSelected(m_settings.lastRegion, CapturePurpose::Record);
}

void App::onRegionSelected(QRect physical, CapturePurpose purpose)
{
    if(m_laser) // e.g. "record last area" while pointing at the screen
        toggleLaser();
    if(purpose == CapturePurpose::Screenshot)
    {
        takeScreenshot(physical);
        return;
    }
    physical = evenSize(physical.intersected(platform::captureBounds(physical)));
    if(physical.width() < kMinRecordSize || physical.height() < kMinRecordSize)
    {
        m_tray.message("Selection too small", "Pick an area of at least 16×16 pixels.", true);
        return;
    }

    QString error;
    const Tools::Capture cap = Tools::capture(m_settings.recordMic, &error);
    if(cap.ffmpeg.isEmpty())
    {
        m_tray.message("Cannot record", error, true);
        return;
    }
    if(!cap.warning.isEmpty())
        m_tray.message("Microphone", cap.warning, true);

    m_settings.lastRegion = physical;
    m_settings.save();
    m_tray.setLastRegionAvailable(true);

    const QList<ScreenInfo> screens = platform::screens();
    const int si = std::max(0, screenAtPhysical(screens, physical.center()));
    const QRect logical = screens.isEmpty() ? physical : toLogical(physical, screens[si]);

    m_recordRect = physical;
    m_recordLogical = logical;
    m_frame = new RecordFrame(logical);
    m_frame->show();
    platform::keepOnTop(m_frame, logical.adjusted(-RecordFrame::kBorder, -RecordFrame::kBorder, RecordFrame::kBorder,
                                                  RecordFrame::kBorder));
    m_bar = new RecordBar;
    connect(m_bar, &RecordBar::stopClicked, &m_recorder, &Recorder::stop);
    connect(m_bar, &RecordBar::pauseClicked, &m_recorder, &Recorder::togglePause);
    connect(m_bar, &RecordBar::cancelClicked, &m_recorder, &Recorder::cancel);
    connect(m_bar, &RecordBar::laserClicked, this, &App::toggleLaser);
    m_bar->setStarting(true);
    // Within the usable area: macOS moves windows out of the menu bar, which could push the
    // bar over the recorded region.
    const QScreen* screen = QGuiApplication::screens().value(si);
    placeBar(logical, screen ? screen->availableGeometry() : logical);

    Recorder::Options opts;
    opts.ffmpeg = cap.ffmpeg;
    platform::setCaptureSource(opts.capture, physical);
    opts.capture.fps = m_settings.fps;
    opts.capture.drawMouse = m_settings.drawMouse;
    opts.capture.audio = cap.audio;
    opts.mastersDir = Settings::mastersDir();
    opts.countdownSeconds = m_settings.countdown ? 3 : 0;
    if(!m_recorder.start(opts))
    {
        teardownRecordingUi();
        return;
    }
    if(m_settings.recordInput)
    {
        QString error;
        if(!m_input.start(&error))
            m_tray.message("Keys & clicks not recorded", error, true);
    }
}

void App::takeScreenshot(QRect physical)
{
    physical = physical.intersected(platform::captureBounds(physical));
    if(physical.width() < kMinShotSize || physical.height() < kMinShotSize)
    {
        m_tray.message("Selection too small", "Pick an area of at least 4×4 pixels.", true);
        return;
    }
    m_settings.lastRegion = physical;
    m_settings.save();
    m_tray.setLastRegionAvailable(true);
    QTimer::singleShot(kGrabDelayMs, this, [this, physical] { captureScreenshot(physical); });
}

void App::captureScreenshot(const QRect& physical)
{
    const QImage image = platform::grab(physical);
    if(image.isNull())
    {
        m_tray.message("Screenshot failed", ("Could not read the screen contents. " + platform::screenAccessHint()).trimmed(),
                       true);
        return;
    }

    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    QString id = "qs-" + stamp;
    for(int n = 2; m_store.find(id) || QFileInfo::exists(Settings::shotsDir() + "/" + id + ".png"); ++n)
        id = QStringLiteral("qs-%1-%2").arg(stamp).arg(n);

    HistoryEntry e;
    e.kind = EntryKind::Screenshot;
    e.id = id;
    e.created = QDateTime::currentDateTime();
    e.master = Settings::shotsDir() + "/" + id + ".png";
    e.thumb = Settings::thumbsDir() + "/" + id + ".png";
    e.size = image.size();
    // Light compression: a full 4K screen saves in a fraction of a second.
    if(!image.save(e.master, "PNG", 80))
    {
        m_tray.message("Screenshot failed", "Could not write " + e.master, true);
        return;
    }
    image.scaled(QSize(480, 270), Qt::KeepAspectRatio, Qt::SmoothTransformation).save(e.thumb, "PNG");
    m_store.add(e);
    saveStore();
    refreshHistory();
    openEditor(id);
}

void App::placeBar(const QRect& region, const QRect& screen)
{
    const QSize sz = m_bar->sizeHint();
    const int gap = RecordFrame::kBorder + 6;
    const int x = std::clamp(region.left(), screen.left(), screen.right() - sz.width());
    const int y = std::clamp(region.top(), screen.top(), screen.bottom() - sz.height());
    const QList<QPoint> candidates = {
        {x, region.bottom() + gap},              // below
        {x, region.top() - gap - sz.height()},   // above
        {region.right() + gap, y},               // right
        {region.left() - gap - sz.width(), y},   // left
    };
    for(const QPoint& p : candidates)
    {
        const QRect r(p, sz);
        if(screen.contains(r) && !r.intersects(region.adjusted(-gap, -gap, gap, gap)))
        {
            m_bar->move(p);
            m_bar->show();
            platform::keepOnTop(m_bar);
            return;
        }
    }
    // Region fills the screen: no room for the bar without it being recorded.
    delete m_bar;
    m_bar = nullptr;
    m_tray.message("Recording", "Press the hotkey or click the tray icon to stop.");
}

void App::onStateChanged(Recorder::State state)
{
    m_tray.setState(state);
    const bool paused = state == Recorder::State::Paused;
    if(m_frame)
        m_frame->setPaused(paused);
    if(m_bar)
    {
        m_bar->setPaused(paused);
        if(state == Recorder::State::Recording)
            m_bar->setStarting(false);
    }
    if(state == Recorder::State::Finalizing || state == Recorder::State::Idle)
    {
        m_input.stop();
        teardownRecordingUi();
    }
}

void App::toggleLaser()
{
    const Recorder::State s = m_recorder.state();
    if(m_laser)
    {
        delete m_laser;
        m_laser = nullptr;
    }
    else if(s == Recorder::State::Recording || s == Recorder::State::Paused || (s == Recorder::State::Idle && !m_overlay))
    {
        // Over the recorded area while recording, else over the screen under the mouse.
        QRect area = m_recordLogical;
        if(s == Recorder::State::Idle)
        {
            const QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
            area = (screen ? screen : QGuiApplication::primaryScreen())->geometry();
        }
        m_laser = new LaserOverlay(area, m_settings.laserFadeMs);
        connect(m_laser, &LaserOverlay::dismissed, this, &App::toggleLaser, Qt::QueuedConnection);
        m_laser->show();
        platform::keepOnTop(m_laser, area);
    }
    // Esc turns it off; it is only taken from other apps while the laser is on.
    if(m_laser)
    {
        QString error;
        if(!m_laserEscape.setShortcut(QKeySequence(Qt::Key_Escape), &error))
            spdlog::warn("laser: Esc unavailable: {}", error.toStdString());
    }
    else
        m_laserEscape.clear();
    if(m_bar)
        m_bar->setLaser(m_laser);
    m_tray.setLaser(m_laser);
}

void App::teardownRecordingUi()
{
    delete m_laser;
    m_laser = nullptr;
    m_laserEscape.clear();
    m_tray.setLaser(false);
    delete m_frame;
    m_frame = nullptr;
    delete m_bar;
    m_bar = nullptr;
}

void App::onRecordingFinished(const QString& id, const QString& master, double duration, bool hasAudio)
{
    HistoryEntry e;
    e.id = id;
    e.created = QDateTime::currentDateTime();
    e.master = master;
    e.thumb = Settings::thumbsDir() + "/" + id + ".png";
    e.duration = duration;
    e.size = m_recordRect.size();
    e.hasAudio = hasAudio;
    if(const QList<InputEvent> input = m_recorder.takeInputEvents(); !input.isEmpty())
        saveInputLog(inputLogPath(master), input);
    m_store.add(e);
    saveStore();

    makeSprite(master, duration, e.thumb, this, [this](bool) { refreshHistory(); });
    refreshHistory();
    openEditor(id);
}

void App::onRecordingFailed(const QString& message)
{
    teardownRecordingUi();
    m_tray.message("Recording failed", message, true);
}

void App::showHistory()
{
    if(!m_history)
    {
        m_history = new HistoryWindow(m_store, m_settings);
        connect(m_history, &HistoryWindow::openRequested, this, &App::openEditor);
        connect(m_history, &HistoryWindow::deleteRequested, this,
                [this](const QString& id) { confirmDelete(id, m_history); });
        connect(m_history, &HistoryWindow::deleteExportRequested, this, [this](const QString& path) {
            if(QMessageBox::question(m_history, "Delete export",
                                     QStringLiteral("Delete %1 from disk?").arg(QFileInfo(path).fileName())) !=
               QMessageBox::Yes)
                return;
            if(QFile::exists(path) && !QFile::remove(path))
            {
                m_tray.message("Could not delete", path, true);
                return;
            }
            if(m_store.removeExport(path))
                saveStore();
            refreshHistory();
        });
        connect(m_history, &HistoryWindow::notify, &m_tray, &Tray::message);
    }
    platform::activateApp();
    m_history->show();
    m_history->raise();
    m_history->activateWindow();
}

void App::refreshHistory()
{
    if(m_history)
        m_history->refresh();
}

void App::openEditor(const QString& id)
{
    if(auto it = m_editors.find(id); it != m_editors.end() && *it)
    {
        (*it)->raise();
        (*it)->activateWindow();
        return;
    }
    const HistoryEntry* entry = m_store.find(id);
    if(!entry)
        return;
    // Both editors have the same signals.
    auto wire = [this, id](auto* editor) {
        using Editor = std::remove_pointer_t<decltype(editor)>;
        m_editors.insert(id, editor);
        connect(editor, &Editor::exported, this, [this](const QString& eid, const QString& path) {
            m_store.addExport(eid, path);
            watchFolders(); // it may have gone to a folder we don't watch yet
            saveStore();
            refreshHistory();
        });
        connect(editor, &Editor::discardRequested, this, &App::deleteRecording);
        connect(editor, &Editor::editsChanged, this, [this](const QString& eid, const EditState& edits) {
            if(m_store.setEdits(eid, edits))
                saveStore();
        });
        connect(editor, &Editor::notify, &m_tray, &Tray::message);
        connect(editor, &QObject::destroyed, this, [this, id] {
            m_editors.remove(id);
            refreshHistory(); // "edited" marker
        });
        platform::activateApp();
        editor->show();
        editor->raise();
        editor->activateWindow();
    };
    if(entry->kind == EntryKind::Screenshot)
        wire(new ScreenshotEditor(*entry, m_settings));
    else
        wire(new EditorWindow(*entry, m_settings));
}

void App::confirmDelete(const QString& id, QWidget* parent)
{
    const HistoryEntry* e = m_store.find(id);
    const bool shot = e && e->kind == EntryKind::Screenshot;
    if(QMessageBox::question(parent, shot ? "Delete screenshot" : "Delete recording",
                             shot ? "Delete this screenshot? Files already saved are kept."
                                  : "Delete this recording and its preview? Files already exported are kept.") ==
       QMessageBox::Yes)
        deleteRecording(id);
}

void App::deleteRecording(const QString& id)
{
    if(auto it = m_editors.find(id); it != m_editors.end() && *it)
        (*it)->close();
    if(const HistoryEntry* e = m_store.find(id))
    {
        QFile::remove(e->master);
        QFile::remove(e->thumb);
        QFile::remove(inputLogPath(e->master));
    }
    m_store.remove(id);
    saveStore();
    refreshHistory();
}

void App::saveStore()
{
    if(!m_store.save())
        m_tray.message("Could not save history", Settings::indexPath(), true);
}

void App::showSettings()
{
    if(m_settingsDialog)
    {
        m_settingsDialog->raise();
        m_settingsDialog->activateWindow();
        return;
    }
    platform::activateApp();
    m_settingsDialog = new SettingsDialog(m_settings);
    m_settingsDialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_settingsDialog, &QDialog::accepted, this, [this] {
        m_settingsDialog->apply(m_settings);
        m_settings.save();
        theme::apply(m_settings.theme);
        theme::setTextScale(m_settings.textScale);
        if(!Settings::setAutostart(m_settingsDialog->autostart()))
            m_tray.message("Autostart", "Could not update the autostart entry.", true);
        applyHotkeys();
        watchFolders(); // the export / screenshot folders may have changed
    });
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void App::applyHotkeys()
{
    // Release all first, so swapping keys doesn't collide with the old grab.
    m_hotkey.clear();
    m_shotHotkey.clear();
    m_laserHotkey.clear();
    bindHotkey(m_hotkey, m_settings.hotkey, "Hotkey unavailable", &Tray::setHotkeyText);
    if(!m_settings.screenshotHotkey.isEmpty() &&
       QKeySequence(m_settings.screenshotHotkey) == QKeySequence(m_settings.hotkey))
    {
        m_tray.setScreenshotHotkeyText({});
        m_tray.message("Screenshot hotkey unavailable",
                       "It is the same as the record hotkey.\nPick another one in Settings.", true);
        return;
    }
    bindHotkey(m_shotHotkey, m_settings.screenshotHotkey, "Screenshot hotkey unavailable",
               &Tray::setScreenshotHotkeyText);
    bindHotkey(m_laserHotkey, m_settings.laserHotkey, "Laser pointer hotkey unavailable", &Tray::setLaserHotkeyText);
}

void App::bindHotkey(GlobalHotkey& hotkey, const QString& sequence, const QString& title,
                     void (Tray::*setText)(const QString&))
{
    const QKeySequence seq(sequence);
    if(seq.isEmpty())
    {
        hotkey.clear();
        (m_tray.*setText)({});
        return;
    }
    QString error;
    if(hotkey.setShortcut(seq, &error))
    {
        (m_tray.*setText)(seq.toString(QKeySequence::NativeText));
        spdlog::info("hotkey: {}", seq.toString().toStdString());
    }
    else
    {
        (m_tray.*setText)({});
        m_tray.message(title, error + "\nPick another one in Settings.", true);
    }
}

void App::watchFolders()
{
    QSet<QString> wanted = {Settings::mastersDir(), Settings::shotsDir(), m_settings.exportDir,
                            m_settings.screenshotDir};
    for(const HistoryEntry& e : m_store.entries())
        for(const QString& path : e.exports)
            wanted << QFileInfo(path).absolutePath();
    const QStringList watched = m_watcher.directories();
    for(const QString& dir : watched)
        if(!wanted.contains(dir))
            m_watcher.removePath(dir);
    for(const QString& dir : std::as_const(wanted))
        if(!watched.contains(dir) && QFileInfo(dir).isDir())
            m_watcher.addPath(dir);
}

void App::pruneMissing()
{
    QList<HistoryEntry> removed;
    if(m_store.pruneMissing(&removed))
    {
        for(const HistoryEntry& e : removed)
        {
            QFile::remove(e.thumb);
            QFile::remove(inputLogPath(e.master));
        }
        saveStore();
        refreshHistory();
    }
    watchFolders();
}

void App::quit()
{
    m_recorder.cancel();
    QApplication::quit();
}

} // namespace qr
