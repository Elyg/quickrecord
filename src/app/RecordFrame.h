#pragma once

#include <QWidget>

namespace qr
{

// Dashed border drawn just *outside* the captured rectangle, so it never shows up in
// the recording. Input-transparent; its interior is masked out entirely except while a
// countdown number is shown (before capture starts).
class RecordFrame : public QWidget
{
    Q_OBJECT

public:
    static constexpr int kBorder = 5;

    explicit RecordFrame(const QRect& logicalRegion);

    void setPaused(bool paused);
    void setCountdown(int seconds); // 0 hides it

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    void updateMask();

    bool m_paused = false;
    int m_countdown = 0;
};

} // namespace qr
