#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QPointF>
#include <QTimer>
#include <QWidget>

namespace qr
{

// Laser pointer over the recorded region (or a whole screen when not recording): while shown it
// takes the mouse, and dragging draws a red stroke that stays a moment after release and then
// fades away. Drawn on screen, so it ends up in the recording.
class LaserOverlay : public QWidget
{
    Q_OBJECT

public:
    // `fadeMs`: how long a stroke stays after release, fade included.
    LaserOverlay(const QRect& logicalRegion, int fadeMs);

signals:
    void dismissed(); // right-click: turn the laser off

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    struct Stroke
    {
        QList<QPointF> points;
        qint64 releasedMs = -1; // m_clock time; -1 while still drawing
    };

    double opacity(const Stroke& s) const; // 1 → 0 as it fades
    void tick();

    qint64 m_holdMs; // fully visible after release…
    qint64 m_fadeMs; // …then fades out over this
    QList<Stroke> m_strokes;
    QElapsedTimer m_clock;
    QTimer m_animation;
};

} // namespace qr
