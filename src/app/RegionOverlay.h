#pragma once

#include "core/Geometry.h"

#include <QFont>
#include <QList>
#include <QObject>
#include <QRect>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QWidget;

namespace qr
{

enum class CapturePurpose
{
    Record,
    Screenshot,
};

// Fullscreen translucent picker over every monitor: one window per monitor, so each is mapped
// with its own origin and scale (a single window spanning monitors gets one monitor's mapping
// and lands offset on the others). All state is in global logical coordinates.
//   drag        → free region, which then stays up for adjusting:
//                   drag its edges/corners to resize, inside to move, outside for a new one;
//                   arrows move it (Shift+arrows resize); Enter, double-click or the
//                   toolbar under it confirms
//   click       → window under the cursor
//   Shift+click → whole monitor under the cursor
//   Enter       → the last used area again (outlined while picking; confirms while adjusting)
//   Tab         → switch between recording and screenshot (S / R pick one directly)
//   H           → show / hide the shortcut list (else a small pill in a bottom corner)
//   T           → cycle the screenshot timer: off, 3, 5, 10 s (also a toolbar chip while adjusting)
//   Esc / right-click → cancel
class RegionOverlay : public QObject
{
    Q_OBJECT

public:
    RegionOverlay(QList<ScreenInfo> screens, const QList<QRect>& windowsPhysical, const QRect& lastPhysical,
                  CapturePurpose purpose, bool showHelp, int shotDelaySeconds);

    ~RegionOverlay() override;

    void start();
    void close(); // cancel; deletes the overlay
    CapturePurpose purpose() const { return m_purpose; }
    void setPurpose(CapturePurpose purpose);
    int shotDelay() const { return m_shotDelay; }

signals:
    void selected(QRect physical, qr::CapturePurpose purpose);
    void cancelled();
    void helpToggled(bool shown); // H; remembered for next time
    void shotDelayChanged(int seconds);

private:
    friend class OverlayPane;

    // Called by the per-monitor panes; `area` is the pane's global logical geometry.
    void paint(QPainter& p, const QRect& area);
    void mousePress(QMouseEvent*);
    void mouseMove(QMouseEvent*);
    void mouseRelease(QMouseEvent*);
    void mouseDoubleClick(QMouseEvent*);
    void keyPress(QKeyEvent*);
    void keyRelease(QKeyEvent*);

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
        Timer,
        Cancel,
    };
    struct ToolButton
    {
        Button button;
        QString text;
        QRect rect; // global logical coords
    };

    Target currentTarget() const;
    QRect physicalOf(const QRect& logical, QPoint ref) const;
    void startAdjusting(const QRect& logical);
    int selectionHit(QPoint global) const; // RectEdge flags; 0 = outside
    QList<ToolButton> toolButtons() const;
    void paintAdjusting(QPainter& p);
    void paintHelp(QPainter& p, const QRect& screen);
    void updateCursor();
    void finish(const QRect& physical);
    void cycleShotDelay();

    void update();
    void setCursor(Qt::CursorShape shape);

    QList<ScreenInfo> m_screens;
    QList<QWidget*> m_panes; // one per screen, owned
    QWidget* m_keyPane = nullptr;
    QFont m_font;
    QList<QRect> m_windowsPhysical; // top-most first
    QList<QRect> m_windowsLogical;
    CapturePurpose m_purpose;
    int m_shotDelay = 0;            // seconds; 0 = capture right away
    QRect m_lastPhysical;           // previously used area (may be null)
    QRect m_lastLogical;
    QRect m_virtual;                // union of screens, global logical
    QPoint m_cursor;                // global logical
    QPoint m_pressPos;
    bool m_pressed = false;
    bool m_dragging = false;
    bool m_shift = false;
    bool m_done = false;
    bool m_showHelp = false;

    bool m_adjusting = false;
    QRect m_sel;        // region being adjusted, global logical
    int m_selDrag = 0;  // RectEdge flags of the active handle/move drag
    QPoint m_selAnchor; // cursor where that drag started
    QRect m_selStart;
};

} // namespace qr
