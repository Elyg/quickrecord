#include "app/ShotTimerBar.h"

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

ShotTimerBar::ShotTimerBar()
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint |
                           Qt::Tool | Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setStyleSheet("QLabel { color: #e8e8e8; font-weight: 600; }"
                  "QToolButton { color: #e8e8e8; font-size: 14px; min-width: 26px; min-height: 22px;"
                  "  border-radius: 5px; border: none; background: transparent; padding: 0 4px; }"
                  "QToolButton:hover { background: rgba(255,255,255,0.12); }");

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(10, 4, 6, 4);
    lay->setSpacing(4);

    auto* dot = new QLabel("●", this);
    dot->setStyleSheet("color: #3e9bff;");
    m_text = new QLabel(this);
    // Wide enough for two digits, so the strip doesn't jump while counting down.
    m_text->setMinimumWidth(m_text->fontMetrics().horizontalAdvance("Screenshot in 00 s") + 4);
    auto* now = button("Now", "Take the screenshot now", this);
    auto* cancel = button("✕", "Cancel the screenshot", this);

    lay->addWidget(dot);
    lay->addWidget(m_text);
    lay->addWidget(now);
    lay->addWidget(cancel);

    connect(now, &QToolButton::clicked, this, &ShotTimerBar::nowClicked);
    connect(cancel, &QToolButton::clicked, this, &ShotTimerBar::cancelClicked);

    setRemaining(0);
    adjustSize();
}

void ShotTimerBar::setRemaining(int seconds)
{
    m_text->setText(QStringLiteral("Screenshot in %1 s").arg(seconds));
}

void ShotTimerBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor(255, 255, 255, 30));
    p.setBrush(QColor(24, 26, 31, 235));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
}

} // namespace qr
