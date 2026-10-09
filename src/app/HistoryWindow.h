#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QPixmap>
#include <QTimer>
#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QMovie;
class QPushButton;
class QSplitter;
class QTabWidget;

namespace qr
{

class HistoryStore;
class PreviewPane;
struct Settings;

// Three tabs, newest first, with a preview pane that loops the selected item:
//  - Recordings: the masters; hovering a tile flips through its sprite sheet.
//  - Screenshots: the original captures.
//  - Exports: every exported GIF/MP4/PNG still on disk; hovering a GIF plays it.
class HistoryWindow : public QWidget
{
    Q_OBJECT

public:
    HistoryWindow(const HistoryStore& store, const Settings& settings);
    ~HistoryWindow() override;

    void refresh();

signals:
    void openRequested(QString id);
    void deleteRequested(QString id);
    void deleteExportRequested(QString path);
    void notify(QString title, QString text, bool error);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct ExportFile
    {
        QString path;
        QString sourceId; // recording it came from; empty if unknown
        QDateTime modified;
        qint64 size = 0;
    };

    QListWidget* makeGrid();
    QList<ExportFile> collectExports() const;
    const QList<QPixmap>& framesFor(const QString& id, const QString& thumbPath);
    QPixmap exportTile(const ExportFile& f);
    QPixmap shotTile(const QString& id, const QString& thumbPath, const QString& original);
    void refreshRecordings();
    void refreshShots();
    void refreshExports();
    void updateCount();
    void updatePreview();
    void togglePreviewDetached();
    void updateFolderButtons();
    void showRecordingMenu(const QPoint& pos);
    void showShotMenu(const QPoint& pos);
    void showExportMenu(const QPoint& pos);
    void setRecordingHover(QListWidgetItem* item);
    void setExportHover(QListWidgetItem* item);

    const HistoryStore& m_store;
    const Settings& m_settings;
    QTabWidget* m_tabs;
    QListWidget* m_list;
    QListWidget* m_shots;
    QListWidget* m_exports;
    QPushButton* m_openExports;
    QPushButton* m_openMasters;
    QLabel* m_count;
    PreviewPane* m_preview;
    QSplitter* m_split;

    QHash<QString, QList<QPixmap>> m_frames;
    QListWidgetItem* m_hover = nullptr;
    int m_hoverFrame = 0;
    QTimer m_anim;

    QHash<QString, QPixmap> m_shotTiles;
    QHash<QString, QPixmap> m_exportTiles;
    QListWidgetItem* m_exportHover = nullptr;
    QMovie* m_movie = nullptr;
};

} // namespace qr
