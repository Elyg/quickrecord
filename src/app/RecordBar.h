#pragma once

#include <QWidget>

class QLabel;
class QToolButton;

namespace qr
{

// Small floating control strip shown next to the recorded region.
class RecordBar : public QWidget
{
    Q_OBJECT

public:
    RecordBar();

    void setElapsed(qint64 ms);
    void setPaused(bool paused);
    void setStarting(bool starting);

signals:
    void stopClicked();
    void pauseClicked();
    void cancelClicked();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QLabel* m_dot;
    QLabel* m_time;
    QToolButton* m_pause;
};

} // namespace qr
