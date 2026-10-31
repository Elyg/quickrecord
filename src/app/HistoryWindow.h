#pragma once

#include "app/Exporter.h"

#include <QHash>
#include <QList>
#include <QPixmap>
#include <QStringList>
#include <QTimer>
#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSplitter;
class QTabWidget;

namespace qr
{

class HistoryStore;
class PreviewPane;
struct HistoryEntry;
struct Settings;
struct SheetState;
struct SheetTile;

// Two tabs, newest first, with a preview pane. Thumbnail size: in the settings, or Ctrl+wheel
// over a grid.
//  - Recordings: hovering a tile flips through its sprite sheet. Contact sheets of recordings
//    are videos, so they are here.
//  - Screenshots: the edited version where there is one, and contact sheets of screenshots.
// Badges show what an item has (EDITED, GIF, MP4, SHEET); the preview switches between its
// versions. Both grids select several items: Ctrl+C copies them, Delete deletes them, and two
// or more preview and make a contact sheet; recordings play in that preview. F2 renames one. Tiles drag out as their files; videos dropped on the window join
// the recordings.
class HistoryWindow : public QWidget
{
    Q_OBJECT

public:
    HistoryWindow(const HistoryStore& store, Settings& settings);
    ~HistoryWindow() override;

    void refresh();
    void setTileWidth(int width); // logical px, clamped; saved in the settings

signals:
    void openRequested(QString id);
    void deleteRequested(QStringList ids);
    void renameRequested(QString id);
    void sheetRequested(QStringList ids); // screenshots or recordings, oldest first
    void videosDropped(QStringList paths);
    void notify(QString title, QString text, bool error);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    struct Variant
    {
        QString label;
        QString path;
    };

    QListWidget* makeGrid();
    QListWidget* currentList() const;
    QStringList selectedIds(const QListWidget* list) const; // in grid order
    QStringList sheetMembers() const;                       // selected items, not sheets, oldest first
    void copySelection();
    void copyFiles(const QStringList& paths);
    void requestDelete();
    void requestRename();
    // The tile's lines: its name if it was given one, else its date; then `second` and `third`, which
    // also tell what the first doesn't.
    QString tileText(const HistoryEntry& e, const QString& second, QStringList third) const;
    void requestSheet();
    void applyTileSize();
    QPixmap fitTile(const QPixmap& src) const; // centred in a tile-sized, HiDPI pixmap
    QPixmap readScaled(const QString& path) const;
    QStringList badges(const HistoryEntry& e) const;
    const QList<QPixmap>& framesFor(const HistoryEntry& e);
    QPixmap shotTile(const HistoryEntry& e);
    QList<Variant> variants(const HistoryEntry& e) const;
    void refreshRecordings();
    void refreshShots();
    void updateHeader();
    void updatePreview();
    void showVariant(int index);
    void showSheetPreview();
    void renderSheetPreview(const QStringList& members);
    // The still sheet being previewed comes to life: a quick render of it, then played.
    void startSheetVideo(const QStringList& members, const SheetState& state, const QList<SheetTile>& tiles);
    void stopSheetVideo();
    void togglePreviewDetached();
    void showMenu(QListWidget* list, const QPoint& pos);
    void setRecordingHover(QListWidgetItem* item);

    const HistoryStore& m_store;
    Settings& m_settings;
    QSize m_tile; // thumbnail size, logical px
    int m_tileWidth;    // what m_tile is on its way to
    QTimer m_tileTimer; // debounces rebuilding every tile while the size changes
    QTabWidget* m_tabs;
    QListWidget* m_list;
    QListWidget* m_shots;
    QPushButton* m_openFolder;
    QPushButton* m_makeSheet;
    QLabel* m_count;
    PreviewPane* m_preview;
    QSplitter* m_split;

    QString m_previewId; // entry whose versions the preview switches between
    QList<Variant> m_variants;
    QStringList m_sheetPreviewOf; // members of the sheet preview being shown
    QTimer m_sheetTimer;          // debounces it while the selection changes
    Exporter m_sheetVideo;        // renders it for recordings
    QString m_sheetVideoPath;     // what that is rendering or last played, kept for this window's life

    QHash<QString, QList<QPixmap>> m_frames;
    QListWidgetItem* m_hover = nullptr;
    int m_hoverFrame = 0;
    QTimer m_anim;

    QHash<QString, QPixmap> m_shotTiles;
};

} // namespace qr
