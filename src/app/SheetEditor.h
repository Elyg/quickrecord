#pragma once

#include "app/Exporter.h"
#include "core/HistoryStore.h"

#include <QHash>
#include <QImage>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>
#include <array>
#include <memory>

class QCheckBox;
class QLabel;
class QLineEdit;
class QMediaPlayer;
class QPushButton;
class QSlider;
class QSpinBox;
class QToolButton;

namespace qr
{

struct Settings;
class VideoView;

// Contact sheet of screenshots (their edited versions where there are some): same-size tiles in
// a grid, text in the six corners of the sheet's header / footer, of every tile, or of one tile.
// Click a tile to select it, drag it onto another to move it there. A new sheet becomes a
// history item ("qsheet-….png" in the screenshot folder) when saved; reopening one continues
// where it was, and members deleted since are drawn as "missing".
// A sheet of recordings is the same with the clips playing together, saved as a recording of its
// own ("qsheet-….mp4" in the recordings folder). It plays here too: a quick render of the sheet,
// redone after every change, which shows on the clips' middle frames (makeSheetFrames() writes
// those, before this opens) meanwhile.
class SheetEditor : public QWidget
{
    Q_OBJECT

public:
    // A new sheet of these items, in this order.
    SheetEditor(const HistoryStore& store, const QStringList& members, Settings& settings);
    // A saved one.
    SheetEditor(const HistoryStore& store, const HistoryEntry& entry, Settings& settings);

    QString id() const { return m_id; }

signals:
    void saved(qr::HistoryEntry entry);                  // its file was (re)written
    void sheetChanged(QString id, qr::SheetState state); // debounced; flushed on close (saved sheets)
    void notify(QString title, QString text, bool error);

protected:
    void closeEvent(QCloseEvent*) override;

private:
    enum class Scope
    {
        Sheet,
        EveryTile,
        ThisTile,
    };
    enum class Action
    {
        Save, // and close
        Copy, // save, copy and close
    };

    void build();
    void loadImages();
    QList<SheetTile> tiles(bool preview) const;
    void render(); // the preview
    void scheduleRender();
    void selectTile(int index);
    SlotText* scopeText(); // what the slot fields edit; null for "This tile" with none selected
    void loadFields();
    void finish(Action action);
    void renderVideo(Action action);
    void published(const QString& path, QSize size, double duration, const SheetState& state, Action action);
    QString filePath(const QString& id) const; // of a sheet with this id, in its folder
    void renderPlayback();       // the quick render of the sheet as it is now
    void onPlaybackRendered(bool ok, const QString& path, const QString& error);
    void dropPlayback();         // the sheet changed: what plays is out of date
    void togglePlay();
    void updatePlayControls();
    void setBackground(const QColor& color);
    void setTextColor(const QColor& color);

    const HistoryStore& m_store;
    Settings& m_settings;
    QString m_id;
    QDateTime m_created;
    QString m_master; // its file; empty until first saved
    bool m_isNew = true;
    bool m_video = false; // of recordings: an MP4, not a PNG
    Exporter m_exporter;
    SheetState m_state;

    QHash<QString, QImage> m_full;    // member id → image; missing ones absent
    QHash<QString, QImage> m_preview; // the same, scaled by m_previewScale
    double m_previewScale = 1.0;
    SheetLayout m_layout; // of the preview
    int m_selected = -1;
    int m_dragFrom = -1;
    Scope m_scope = Scope::EveryTile;

    VideoView* m_view;
    QSpinBox* m_columns;
    QSpinBox* m_gap;
    QToolButton* m_backgroundButton;
    std::array<QToolButton*, 3> m_scopeButtons{};
    std::array<QLineEdit*, 6> m_fields{};
    QLabel* m_scopeHint;
    QSpinBox* m_textSize;
    QToolButton* m_textColorButton;
    QCheckBox* m_textBox;
    QLabel* m_status;
    QMediaPlayer* m_player = nullptr; // these four only on a sheet of recordings
    QToolButton* m_play = nullptr;
    QSlider* m_seek = nullptr;
    QLabel* m_time = nullptr;
    Exporter m_playRender;
    std::unique_ptr<QTemporaryDir> m_playDir;
    QTimer m_playTimer;       // waits for a pause in the edits
    QString m_playPath;       // what the player has
    int m_playRenders = 0;
    bool m_wantPlay = true;   // false once paused
    qint64 m_resumeAt = -1;   // where to carry on in the next render
    QPushButton* m_close;
    QPushButton* m_save;
    QPushButton* m_copy;

    QTimer m_renderTimer;
    QTimer m_saveTimer;
    bool m_loading = false;
};

} // namespace qr
