#pragma once

#include <QWidget>

namespace qr
{

// Timeline with two trim handles and a playhead. Times in milliseconds.
class TrimSlider : public QWidget
{
    Q_OBJECT

public:
    explicit TrimSlider(QWidget* parent = nullptr);

    void setDuration(qint64 ms);
    // Clamped to the duration; keeps at least the minimum range.
    void setRange(qint64 start, qint64 end);
    qint64 duration() const { return m_duration; }
    void setPosition(qint64 ms);
    qint64 start() const { return m_start; }
    qint64 end() const { return m_end; }

    QSize sizeHint() const override { return {400, 36}; }

signals:
    void rangeChanged(qint64 start, qint64 end);
    void seekRequested(qint64 ms);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    enum class Drag
    {
        None,
        Start,
        End,
        Seek,
    };

    QRectF track() const;
    double xFor(qint64 ms) const;
    qint64 msFor(double x) const;

    qint64 m_duration = 0;
    qint64 m_start = 0;
    qint64 m_end = 0;
    qint64 m_pos = 0;
    Drag m_drag = Drag::None;
};

} // namespace qr
