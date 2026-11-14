#pragma once

#include <QWidget>

class QLabel;

namespace qr
{

// Floating "Screenshot in N s" strip shown while a delayed screenshot counts down.
// Never takes focus, so menus opened meanwhile stay open; it is hidden before the grab.
class ShotTimerBar : public QWidget
{
    Q_OBJECT

public:
    ShotTimerBar();

    void setRemaining(int seconds);

signals:
    void nowClicked();
    void cancelClicked();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QLabel* m_text;
};

} // namespace qr
