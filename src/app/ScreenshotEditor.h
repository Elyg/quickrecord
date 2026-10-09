#pragma once

#include "core/HistoryStore.h"

#include <QImage>
#include <QTimer>
#include <QWidget>

class QLabel;
class QPushButton;
class QToolButton;

namespace qr
{

struct Settings;
class CaptionBar;
class VideoView;

// Post-screenshot window: crop and caption the original, then save and/or copy a PNG.
class ScreenshotEditor : public QWidget
{
    Q_OBJECT

public:
    ScreenshotEditor(const HistoryEntry& entry, Settings& settings);

    QString id() const { return m_entry.id; }

signals:
    void exported(QString id, QString path); // written to the screenshot folder
    void editsChanged(QString id, qr::EditState edits); // debounced; flushed on close
    void discardRequested(QString id);
    void notify(QString title, QString text, bool error);

protected:
    void closeEvent(QCloseEvent*) override;

private:
    enum class Action
    {
        Save,
        Copy,
        SaveAndCopy,
    };

    QImage composed() const; // crop + caption, as previewed
    void runExport(Action action);
    QString uniqueExportPath() const;
    QSize outputSize() const;
    EditState currentEdits() const;
    void restoreEdits(const EditState& edits);
    void scheduleEditsSave();
    void cancel();
    void setCrop(const QRect& crop);
    void updateOverlay();

    HistoryEntry m_entry;
    Settings& m_settings;
    QImage m_image;

    VideoView* m_view;
    QToolButton* m_cropEdit;
    QToolButton* m_cropReset;
    QLabel* m_cropLabel;
    CaptionBar* m_caption;
    QLabel* m_status;
    QPushButton* m_discard;
    QPushButton* m_cancel;
    QPushButton* m_copy;
    QPushButton* m_save;
    QPushButton* m_saveCopy;
    QRect m_crop; // empty = whole image

    QTimer m_editsTimer;
    bool m_restoring = false;
    EditState m_initialEdits; // what Cancel reverts to
    bool m_cancelled = false;
};

} // namespace qr
