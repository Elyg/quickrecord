#pragma once

#include "core/HistoryStore.h"

#include <QImage>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <optional>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QToolButton;

namespace qr
{

struct Settings;
class CaptionBar;
class VideoView;

// Post-screenshot window: draw on, crop and caption the original. Save writes its one edited
// version ("<id>-edited.png" next to it, replaced each time), Copy also puts it on the clipboard;
// without edits nothing is written and Copy copies the original. Drawings are kept per
// screenshot like the other edits, with undo/redo; closing keeps them.
class ScreenshotEditor : public QWidget
{
    Q_OBJECT

public:
    ScreenshotEditor(const HistoryEntry& entry, Settings& settings);

    QString id() const { return m_entry.id; }

signals:
    void exported(QString id, QString path); // the edited PNG was (re)written
    void editsChanged(QString id, qr::EditState edits); // debounced; flushed on close
    void notify(QString title, QString text, bool error);

protected:
    void closeEvent(QCloseEvent*) override;
    bool eventFilter(QObject* watched, QEvent* event) override; // Move tool keys on the preview

private:
    enum class Action
    {
        Save, // and close
        Copy, // save, copy and close
    };

    QImage composed() const; // crop + caption, as previewed
    bool hasEdits() const;
    void finish(Action action);
    QSize outputSize() const;
    QRect frame() const; // the crop, or the whole image
    EditState currentEdits() const;
    void restoreEdits(const EditState& edits);
    void scheduleEditsSave();
    void setCrop(const QRect& crop);
    void updateOverlay();

    // Drawing
    void setTool(int index); // into the tool buttons (0 = Move); -1 = pan/zoom only
    std::optional<AnnotationTool> drawingTool() const;
    bool moveToolActive() const { return m_activeTool == 0; }
    void select(int index); // -1 = nothing
    const Annotation* selectedText() const; // Move tool selection, when it is text
    void addText(QPointF pos);              // asks for it; placed at `pos` or the chosen corner
    void updateTextPlace();                 // shown for the Text tool and selected text
    void editSelected(const std::function<void(Annotation&)>& change); // undoable
    void deleteSelected();
    void onDrawPressed(QPointF pos);
    void onDrawMoved(QPointF pos);
    void onDrawReleased(QPointF pos);
    void changeAnnotations(const QList<Annotation>& annotations); // undoable
    void undo();
    void redo();
    void rebuildBase();
    void refreshView();
    void updateDrawButtons();

    HistoryEntry m_entry;
    Settings& m_settings;
    QImage m_image;

    VideoView* m_view;
    QToolButton* m_cropEdit;
    QToolButton* m_cropReset;
    QLabel* m_cropLabel;
    CaptionBar* m_caption;
    QLabel* m_status;
    QPushButton* m_close;
    QPushButton* m_save;
    QPushButton* m_copy;
    QRect m_crop; // empty = whole image

    QList<QToolButton*> m_toolButtons; // indexed by AnnotationTool
    QToolButton* m_drawColorButton;
    QSpinBox* m_drawWidth;
    QComboBox* m_textPlace;
    QToolButton* m_undo;
    QToolButton* m_redo;
    QToolButton* m_clear;
    QColor m_drawColor;
    int m_activeTool = -1;
    int m_selected = -1;     // Move tool selection, index into m_annotations
    bool m_dragging = false; // Move tool drag in progress
    QPointF m_dragStart;
    Annotation m_dragOriginal;
    QImage m_dragBase; // everything except the dragged annotation
    QList<Annotation> m_annotations;
    QList<QList<Annotation>> m_undoStack;
    QList<QList<Annotation>> m_redoStack;
    std::optional<Annotation> m_current; // being drawn
    QImage m_base;                       // m_image with m_annotations painted on

    QTimer m_editsTimer;
    bool m_restoring = false;
};

} // namespace qr
