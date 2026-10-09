#pragma once

#include "core/Geometry.h"

#include <QList>
#include <QRect>
#include <QWidget>

namespace qr
{

enum class CapturePurpose
{
    Record,
    Screenshot,
};

// Fullscreen translucent picker spanning every monitor.
//   drag        → free region, which then stays up for adjusting:
//                   drag its edges/corners to resize, inside to move, outside for a new one;
//                   arrows move it (Shift+arrows resize); Enter, double-click or the
//                   toolbar under it confirms
//   click       → window under the cursor
//   Shift+click → whole monitor under the cursor
//   Enter       → the last used area again (outlined while picking; confirms while adjusting)
//   Tab         → switch between recording and screenshot (S / R pick one directly)
//   Esc / right-click → cancel
class RegionOverlay : public QWidget
{
    Q_OBJECT

public:
    RegionOverlay(QList<ScreenInfo> screens, const QList<QRect>& windowsPhysical, const QRect& lastPhysical,
                  CapturePurpose purpose);

    void start();
    CapturePurpose purpose() const { return m_purpose; }
    void setPurpose(CapturePurpose purpose);

signals:
    void selected(QRect physical, qr::CapturePurpose purpose);
    void cancelled();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;

private:
    enum class Mode
    {
        Region,
        Window,
        Screen,
    };

    struct Target
    {
        Mode mode = Mode::Screen;
        QRect logical;  // global logical coords
        QRect physical; // what gets recorded
    };

    // Painted buttons under the region being adjusted.
    enum class Button
    {
        Record,
        Screenshot,
        Cancel,
    };
    struct ToolButton
    {
        Button button;
        QString text;
        QRect rect; // widget coords
    };

    Target currentTarget() const;
    QRect physicalOf(const QRect& logical, QPoint ref) const;
    void startAdjusting(const QRect& logical);
    int selectionHit(QPoint global) const; // RectEdge flags; 0 = outside
    QList<ToolButton> toolButtons() const;
    void paintAdjusting(QPainter& p);
    void updateCursor();
    void finish(const QRect& physical);

    QList<ScreenInfo> m_screens;
    QList<QRect> m_windowsPhysical; // top-most first
    QList<QRect> m_windowsLogical;
    CapturePurpose m_purpose;
    QRect m_lastPhysical;           // previously used area (may be null)
    QRect m_lastLogical;
    QRect m_virtual;                // union of screens, global logical
    QPoint m_cursor;                // global logical
    QPoint m_pressPos;
    bool m_pressed = false;
    bool m_dragging = false;
    bool m_shift = false;
    bool m_done = false;

    bool m_adjusting = false;
    QRect m_sel;        // region being adjusted, global logical
    int m_selDrag = 0;  // RectEdge flags of the active handle/move drag
    QPoint m_selAnchor; // cursor where that drag started
    QRect m_selStart;
};

} // namespace qr
