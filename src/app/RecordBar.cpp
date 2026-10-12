#include "app/RecordBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QToolButton>

namespace qr
{

namespace
{
QToolButton* button(const QString& text, const QString& tip, QWidget* parent)
{
    auto* b = new QToolButton(parent);
    b->setText(text);
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    b->setAutoRaise(true);
    return b;
}
} // namespace

RecordBar::RecordBar()
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint |
                           Qt::Tool | Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setStyleSheet("QLabel { color: #e8e8e8; font-weight: 600; }"
                  "QToolButton { color: #e8e8e8; font-size: 14px; min-width: 26px; min-height: 22px;"
                  "  border-radius: 5px; border: none; background: transparent; padding: 0; }"
                  "QToolButton:hover { background: rgba(255,255,255,0.12); }");

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(10, 4, 6, 4);
    lay->setSpacing(4);

    m_dot = new QLabel("●", this);
    m_time = new QLabel("0:00", this);
    m_time->setMinimumWidth(44);
    m_pause = button("❚❚", "Pause / resume", this);
    auto* stop = button("■", "Stop and open the editor", this);
    auto* cancel = button("✕", "Cancel (discard)", this);

    lay->addWidget(m_dot);
    lay->addWidget(m_time);
    lay->addWidget(m_pause);
    lay->addWidget(stop);
    lay->addWidget(cancel);

    connect(m_pause, &QToolButton::clicked, this, &RecordBar::pauseClicked);
    connect(stop, &QToolButton::clicked, this, &RecordBar::stopClicked);
    connect(cancel, &QToolButton::clicked, this, &RecordBar::cancelClicked);

    setPaused(false);
    adjustSize();
}

void RecordBar::setElapsed(qint64 ms)
{
    const qint64 s = ms / 1000;
    m_time->setText(QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0')));
}

void RecordBar::setPaused(bool paused)
{
    m_dot->setStyleSheet(paused ? "color: #f5a524;" : "color: #ff3b3f;");
    m_pause->setText(paused ? "▶" : "❚❚");
}

void RecordBar::setStarting(bool starting)
{
    m_pause->setEnabled(!starting);
    m_dot->setStyleSheet(starting ? "color: #8a8f98;" : "color: #ff3b3f;");
}

void RecordBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor(255, 255, 255, 30));
    p.setBrush(QColor(24, 26, 31, 235));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
}

} // namespace qr
