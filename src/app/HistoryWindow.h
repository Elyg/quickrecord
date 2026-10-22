#pragma once

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
class QSlider;
class QSplitter;
class QTabWidget;

namespace qr
{

class HistoryStore;
class PreviewPane;
struct HistoryEntry;
struct Settings;

// Two tabs, newest first, with a preview pane and a thumbnail size slider (also Ctrl+wheel
// over a grid):
//  - Recordings: hovering a tile flips through its sprite sheet.
//  - Screenshots: the edited version where there is one, and contact sheets.
// Badges show what an item has (EDITED, GIF, MP4, SHEET); the preview switches between its
// versions. Both grids select several items: Ctrl+C copies them, Delete deletes them, and two
// or more screenshots make a contact sheet.
class HistoryWindow : public QWidget
{
    Q_OBJECT

public:
    HistoryWindow(const HistoryStore& store, Settings& settings);
    ~HistoryWindow() override;

    void refresh();

signals:
    void openRequested(QString id);
    void deleteRequested(QStringList ids);
    void sheetRequested(QStringList ids); // screenshots, oldest first
    void notify(QString title, QString text, bool error);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
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
    QStringList sheetMembers() const;                       // selected screenshots, not sheets, oldest first
    void copySelection();
    void copyFiles(const QStringList& paths);
    void requestDelete();
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
    void togglePreviewDetached();
    void showMenu(QListWidget* list, const QPoint& pos);
    void setRecordingHover(QListWidgetItem* item);

    const HistoryStore& m_store;
    Settings& m_settings;
    QSize m_tile; // thumbnail size, logical px
    QSlider* m_tileSlider;
    QTimer m_tileTimer; // debounces rebuilding every tile while the slider moves
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

    QHash<QString, QList<QPixmap>> m_frames;
    QListWidgetItem* m_hover = nullptr;
    int m_hoverFrame = 0;
    QTimer m_anim;

    QHash<QString, QPixmap> m_shotTiles;
};

} // namespace qr
