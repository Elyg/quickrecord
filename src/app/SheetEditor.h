#pragma once

#include "core/HistoryStore.h"

#include <QHash>
#include <QImage>
#include <QTimer>
#include <QWidget>
#include <array>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
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
class SheetEditor : public QWidget
{
    Q_OBJECT

public:
    // A new sheet of these screenshots, in this order.
    SheetEditor(const HistoryStore& store, const QStringList& members, Settings& settings);
    // A saved one.
    SheetEditor(const HistoryStore& store, const HistoryEntry& entry, Settings& settings);

    QString id() const { return m_id; }

signals:
    void saved(qr::HistoryEntry entry);                  // its PNG was (re)written
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
    void setBackground(const QColor& color);
    void setTextColor(const QColor& color);

    const HistoryStore& m_store;
    Settings& m_settings;
    QString m_id;
    QDateTime m_created;
    QString m_master; // its PNG; empty until first saved
    bool m_isNew = true;
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
    QPushButton* m_close;
    QPushButton* m_save;
    QPushButton* m_copy;

    QTimer m_renderTimer;
    QTimer m_saveTimer;
    bool m_loading = false;
};

} // namespace qr
