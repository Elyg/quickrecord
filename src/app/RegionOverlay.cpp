#include "app/RegionOverlay.h"

#include "app/Platform.h"

#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace qr
{

namespace
{
constexpr int kDragThreshold = 4;
constexpr int kMinSelection = 8; // logical px, while adjusting

QColor accent(CapturePurpose purpose)
{
    return purpose == CapturePurpose::Screenshot ? QColor("#3e9bff") : QColor("#ff3b3f");
}
} // namespace

RegionOverlay::RegionOverlay(QList<ScreenInfo> screens, const QList<QRect>& windowsPhysical,
                             const QRect& lastPhysical, CapturePurpose purpose, bool showHelp)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint | Qt::Tool)
    , m_screens(std::move(screens))
    , m_windowsPhysical(windowsPhysical)
    , m_purpose(purpose)
    , m_showHelp(showHelp)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);

    for(const ScreenInfo& s : m_screens)
        m_virtual |= s.geometry;
    for(const QRect& r : m_windowsPhysical)
    {
        const int si = screenAtPhysical(m_screens, r.center());
        m_windowsLogical << (si >= 0 ? toLogical(r, m_screens[si]) : r);
    }
    if(!lastPhysical.isEmpty())
    {
        const int si = screenAtPhysical(m_screens, lastPhysical.center());
        const QRect logical = si >= 0 ? toLogical(lastPhysical, m_screens[si]) : lastPhysical;
        if(logical.intersects(m_virtual)) // monitor layout may have changed since
        {
            m_lastPhysical = lastPhysical;
            m_lastLogical = logical;
        }
    }
    m_cursor = QCursor::pos();
    m_shift = QGuiApplication::queryKeyboardModifiers() & Qt::ShiftModifier;
}

void RegionOverlay::start()
{
    setGeometry(m_virtual);
    show();
    platform::keepOnTop(this);
    platform::activateApp();
    raise();
    activateWindow();
    grabKeyboard();
}

void RegionOverlay::setPurpose(CapturePurpose purpose)
{
    m_purpose = purpose;
    update();
}

RegionOverlay::Target RegionOverlay::currentTarget() const
{
    Target t;
    if(m_dragging)
    {
        t.mode = Mode::Region;
        t.logical = rectFromPoints(m_pressPos, m_cursor);
        t.physical = physicalOf(t.logical, m_pressPos);
        return t;
    }
    if(m_adjusting)
    {
        t.mode = Mode::Region;
        t.logical = m_sel;
        t.physical = physicalOf(m_sel, m_sel.center());
        return t;
    }
    const int si = screenAtLogical(m_screens, m_cursor);
    if(!m_shift)
    {
        for(int i = 0; i < m_windowsLogical.size(); ++i)
        {
            if(m_windowsLogical[i].contains(m_cursor))
            {
                t.mode = Mode::Window;
                t.logical = m_windowsLogical[i];
                t.physical = m_windowsPhysical[i];
                return t;
            }
        }
    }
    t.mode = Mode::Screen;
    if(si >= 0)
    {
        t.logical = m_screens[si].geometry;
        t.physical = physicalGeometry(m_screens[si]);
    }
    return t;
}

QRect RegionOverlay::physicalOf(const QRect& logical, QPoint ref) const
{
    const int si = screenAtLogical(m_screens, ref);
    return si >= 0 ? toPhysical(logical, m_screens[si]) : logical;
}

void RegionOverlay::startAdjusting(const QRect& logical)
{
    m_adjusting = true;
    m_sel = logical;
    updateCursor();
    update();
}

int RegionOverlay::selectionHit(QPoint p) const
{
    // Smaller grab zones on tiny regions, so their inside can still be dragged.
    const int tol = std::clamp(std::min(m_sel.width(), m_sel.height()) / 4, 2, 8);
    if(!m_sel.adjusted(-tol, -tol, tol, tol).contains(p))
        return 0;
    const int right = m_sel.left() + m_sel.width();
    const int bottom = m_sel.top() + m_sel.height();
    int hit = 0;
    if(std::abs(p.x() - m_sel.left()) <= tol)
        hit |= EdgeLeft;
    else if(std::abs(p.x() - right) <= tol)
        hit |= EdgeRight;
    if(std::abs(p.y() - m_sel.top()) <= tol)
        hit |= EdgeTop;
    else if(std::abs(p.y() - bottom) <= tol)
        hit |= EdgeBottom;
    return hit ? hit : EdgeMove;
}

QList<RegionOverlay::ToolButton> RegionOverlay::toolButtons() const
{
    QFont f = font();
    f.setBold(true);
    const QFontMetrics fm(f);
    QList<ToolButton> out = {
        {Button::Record, QStringLiteral("● Record"), {}},
        {Button::Screenshot, QStringLiteral("Screenshot"), {}},
        {Button::Cancel, QStringLiteral("Cancel"), {}},
    };
    constexpr int gap = 6;
    constexpr int margin = 10;
    const int h = fm.height() + 12;
    int total = -gap;
    for(ToolButton& b : out)
    {
        b.rect.setSize(QSize(fm.horizontalAdvance(b.text) + 24, h));
        total += b.rect.width() + gap;
    }

    // Right-aligned under the region; above it if there's no room; else inside its bottom.
    const QRect sel = m_sel.translated(-m_virtual.topLeft());
    const int si = screenAtLogical(m_screens, m_sel.center());
    const QRect screen = si >= 0 ? m_screens[si].geometry.translated(-m_virtual.topLeft()) : rect();
    int y = sel.bottom() + margin;
    if(y + h > screen.bottom())
        y = sel.top() - margin - h;
    if(y < screen.top())
        y = sel.bottom() - margin - h;
    int x = std::max(screen.left() + margin, std::min(sel.right() + 1 - total, screen.right() - margin - total));
    for(ToolButton& b : out)
    {
        b.rect.moveTopLeft(QPoint(x, y));
        x += b.rect.width() + gap;
    }
    return out;
}

void RegionOverlay::updateCursor()
{
    if(!m_adjusting || m_dragging)
    {
        setCursor(Qt::CrossCursor);
        return;
    }
    for(const ToolButton& b : toolButtons())
    {
        if(b.rect.contains(m_cursor - m_virtual.topLeft()))
        {
            setCursor(Qt::PointingHandCursor);
            return;
        }
    }
    const int hit = m_selDrag ? m_selDrag : selectionHit(m_cursor);
    Qt::CursorShape shape = Qt::CrossCursor;
    if(hit == EdgeMove)
        shape = Qt::SizeAllCursor;
    else if(hit == (EdgeLeft | EdgeTop) || hit == (EdgeRight | EdgeBottom))
        shape = Qt::SizeFDiagCursor;
    else if(hit == (EdgeRight | EdgeTop) || hit == (EdgeLeft | EdgeBottom))
        shape = Qt::SizeBDiagCursor;
    else if(hit & (EdgeLeft | EdgeRight))
        shape = Qt::SizeHorCursor;
    else if(hit & (EdgeTop | EdgeBottom))
        shape = Qt::SizeVerCursor;
    setCursor(shape);
}

void RegionOverlay::paintAdjusting(QPainter& p)
{
    // Resize handles on the corners and edge midpoints.
    const QRectF sel = QRectF(m_sel.translated(-m_virtual.topLeft()));
    constexpr qreal half = 4;
    p.setPen(QPen(accent(m_purpose), 1.5));
    p.setBrush(Qt::white);
    for(const qreal x : {sel.left(), sel.center().x(), sel.right()})
        for(const qreal y : {sel.top(), sel.center().y(), sel.bottom()})
            if(x != sel.center().x() || y != sel.center().y())
                p.drawRect(QRectF(x - half, y - half, 2 * half, 2 * half));

    QFont f = font();
    f.setBold(true);
    p.setFont(f);
    const QPoint cursor = m_cursor - m_virtual.topLeft();
    for(const ToolButton& b : toolButtons())
    {
        const bool primary = (b.button == Button::Record && m_purpose == CapturePurpose::Record) ||
                             (b.button == Button::Screenshot && m_purpose == CapturePurpose::Screenshot);
        const bool hover = b.rect.contains(cursor);
        QColor fill = primary ? accent(m_purpose) : QColor(32, 34, 40, 235);
        if(hover)
            fill = primary ? fill.lighter(115) : QColor(58, 61, 70, 240);
        p.setPen(primary ? Qt::NoPen : QPen(QColor(255, 255, 255, 60), 1));
        p.setBrush(fill);
        p.drawRoundedRect(b.rect, 5, 5);
        p.setPen(Qt::white);
        p.drawText(b.rect, Qt::AlignCenter, b.text);
    }
}

void RegionOverlay::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(0, 0, 0, 110));

    const bool shot = m_purpose == CapturePurpose::Screenshot;
    const Target t = currentTarget();
    const QRect hl = t.logical.translated(-m_virtual.topLeft());
    if(!hl.isEmpty())
    {
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.fillRect(hl, Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setPen(QPen(accent(m_purpose), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(hl).adjusted(1, 1, -1, -1));

        const QString mode = t.mode == Mode::Region ? "Region" : (t.mode == Mode::Window ? "Window" : "Screen");
        const QString label = QStringLiteral("%1 · %2  %3 × %4")
                                  .arg(shot ? "Screenshot" : "Record", mode)
                                  .arg(t.physical.width())
                                  .arg(t.physical.height());
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 0.95);
        f.setBold(true);
        p.setFont(f);
        const QRect textRect = p.fontMetrics().boundingRect(label).adjusted(-8, -4, 8, 4);
        QPoint at = hl.topLeft() + QPoint(0, -textRect.height() - 6);
        if(at.y() < 0)
            at = hl.topLeft() + QPoint(6, 6);
        const QRect box(at, textRect.size());
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(20, 20, 24, 220));
        p.drawRoundedRect(box, 5, 5);
        p.setPen(Qt::white);
        p.drawText(box, Qt::AlignCenter, label);
    }

    if(m_adjusting && !m_dragging)
        paintAdjusting(p);

    // Previously used area, so it can be re-used with Enter.
    if(!m_lastLogical.isEmpty() && !m_dragging && !m_adjusting)
    {
        const QRect last = m_lastLogical.translated(-m_virtual.topLeft());
        QPen pen(QColor(255, 255, 255, 200), 1.5, Qt::DashLine);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(last).adjusted(0.5, 0.5, -0.5, -0.5));
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 0.9);
        p.setFont(f);
        const QString label = shot ? QStringLiteral("Last area — Enter to capture it")
                                   : QStringLiteral("Last area — Enter to record it");
        QRect box = p.fontMetrics().boundingRect(label).adjusted(-6, -3, 6, 3);
        box.moveTopLeft(last.bottomLeft() + QPoint(0, 6));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(20, 20, 24, 200));
        p.drawRoundedRect(box, 4, 4);
        p.setPen(QColor("#e8e8e8"));
        p.drawText(box, Qt::AlignCenter, label);
    }

    // Shortcut help on the monitor under the cursor.
    const int si = screenAtLogical(m_screens, m_cursor);
    if(si >= 0 && !m_dragging)
        paintHelp(p, m_screens[si].geometry.translated(-m_virtual.topLeft()));
}

void RegionOverlay::paintHelp(QPainter& p, const QRect& screen)
{
    const bool shot = m_purpose == CapturePurpose::Screenshot;
    // Bottom-left corner of the screen, or bottom-right when the cursor comes near it.
    constexpr int kInset = 24;
    const QPoint cursor = m_cursor - m_virtual.topLeft();
    auto place = [&](QSize size) {
        QRect box(QPoint(), size);
        box.moveBottomLeft(QPoint(screen.left() + kInset, screen.bottom() - kInset));
        if(box.adjusted(-80, -80, 80, 80).contains(cursor))
            box.moveBottomRight(QPoint(screen.right() - kInset, screen.bottom() - kInset));
        return box;
    };

    if(!m_showHelp)
    {
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 0.95);
        const QFontMetrics fm(f);
        const QString text = (shot ? QStringLiteral("Screenshot") : QStringLiteral("Record")) + " · H shortcuts";
        const int h = fm.height() + 12;
        const QRect box = place(QSize(fm.horizontalAdvance(text) + h + 14, h));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(20, 20, 24, 170));
        p.drawRoundedRect(box, h / 2.0, h / 2.0);
        p.setBrush(accent(m_purpose));
        p.drawEllipse(QPointF(box.left() + h / 2.0 + 2, box.center().y() + 0.5), 4.5, 4.5);
        p.setFont(f);
        p.setPen(QColor("#e8e8e8"));
        p.drawText(box.adjusted(h + 4, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, text);
        return;
    }

    QList<std::pair<QString, QString>> rows;
    if(m_adjusting)
    {
        rows = {{"Drag edges", "resize"},
                {"Drag inside", "move"},
                {"Drag outside", "start a new area"},
                {"Arrows", "move by a pixel (Shift: resize)"},
                {"Enter", shot ? "take the screenshot" : "start recording"}};
    }
    else
    {
        rows = {{"Drag", "select an area"}, {"Click", "pick a window"}, {"Shift + click", "whole screen"}};
        if(!m_lastLogical.isEmpty())
            rows.emplace_back("Enter", "the last area");
    }
    rows.emplace_back("Tab", shot ? "record instead" : "screenshot instead");
    rows.emplace_back("H", "hide this list");
    rows.emplace_back("Esc", "cancel");

    QFont titleFont = font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.15);
    QFont keyFont = font();
    keyFont.setBold(true);
    keyFont.setPointSizeF(keyFont.pointSizeF() * 0.9);
    const QFont textFont = font();
    const QFontMetrics titleFm(titleFont), keyFm(keyFont), textFm(textFont);

    constexpr int kPad = 14;
    constexpr int kCapPad = 6;
    int keyW = 0;
    int textW = 0;
    for(const auto& [key, text] : rows)
    {
        keyW = std::max(keyW, keyFm.horizontalAdvance(key) + 2 * kCapPad);
        textW = std::max(textW, textFm.horizontalAdvance(text));
    }
    const QString title = shot ? QStringLiteral("Screenshot") : QStringLiteral("Record");
    const QString subtitle = m_adjusting ? QStringLiteral("adjust the area") : QString();
    const int capH = keyFm.height() + 4;
    const int rowH = std::max(capH, textFm.height()) + 6;
    const int titleH = titleFm.height();
    const int width = std::max(keyW + 12 + textW, titleFm.horizontalAdvance(title + "   " + subtitle) + 18) + 2 * kPad;
    const int height = kPad + titleH + 8 + int(rows.size()) * rowH - 6 + kPad;

    const QRect box = place(QSize(width, height));

    p.setPen(QPen(accent(m_purpose), 1.5));
    p.setBrush(QColor(20, 20, 24, 250)); // nearly opaque: it often sits over the cleared highlight
    p.drawRoundedRect(box, 10, 10);

    // Title: a dot in the mode's colour, the mode, and what the keys do right now.
    int y = box.top() + kPad;
    p.setPen(Qt::NoPen);
    p.setBrush(accent(m_purpose));
    p.drawEllipse(QPointF(box.left() + kPad + 5, y + titleH / 2.0), 5, 5);
    p.setFont(titleFont);
    p.setPen(Qt::white);
    const QRect titleRect(box.left() + kPad + 18, y, width, titleH);
    p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, title);
    if(!subtitle.isEmpty())
    {
        p.setFont(textFont);
        p.setPen(QColor("#a8acb4"));
        p.drawText(titleRect.translated(titleFm.horizontalAdvance(title + "   "), 0), Qt::AlignLeft | Qt::AlignVCenter,
                   subtitle);
    }
    y += titleH + 8;

    for(const auto& [key, text] : rows)
    {
        const QRect cap(box.left() + kPad, y + (rowH - 6 - capH) / 2, keyFm.horizontalAdvance(key) + 2 * kCapPad, capH);
        p.setPen(QPen(QColor(255, 255, 255, 70), 1));
        p.setBrush(QColor(255, 255, 255, 28));
        p.drawRoundedRect(QRectF(cap).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        p.setFont(keyFont);
        p.setPen(QColor("#f2f2f2"));
        p.drawText(cap, Qt::AlignCenter, key);

        p.setFont(textFont);
        p.setPen(QColor("#d8dadf"));
        p.drawText(QRect(box.left() + kPad + keyW + 12, y, textW + 4, rowH - 6), Qt::AlignLeft | Qt::AlignVCenter, text);
        y += rowH;
    }
}

void RegionOverlay::mousePressEvent(QMouseEvent* e)
{
    if(e->button() == Qt::RightButton)
    {
        finish({});
        return;
    }
    if(e->button() != Qt::LeftButton)
        return;
    m_cursor = e->globalPosition().toPoint();
    if(m_adjusting)
    {
        for(const ToolButton& b : toolButtons())
        {
            if(!b.rect.contains(m_cursor - m_virtual.topLeft()))
                continue;
            if(b.button == Button::Cancel)
                finish({});
            else
            {
                m_purpose = b.button == Button::Screenshot ? CapturePurpose::Screenshot : CapturePurpose::Record;
                finish(currentTarget().physical);
            }
            return;
        }
        if(const int hit = selectionHit(m_cursor))
        {
            m_selDrag = hit;
            m_selAnchor = m_cursor;
            m_selStart = m_sel;
            return;
        }
        // Outside the region: a drag from here draws a new one.
    }
    m_pressed = true;
    m_pressPos = m_cursor;
}

void RegionOverlay::mouseMoveEvent(QMouseEvent* e)
{
    m_cursor = e->globalPosition().toPoint();
    if(m_selDrag)
        m_sel = dragRect(m_selStart, m_selDrag, m_cursor - m_selAnchor, m_virtual, kMinSelection);
    else if(m_pressed && !m_dragging && (m_cursor - m_pressPos).manhattanLength() > kDragThreshold)
        m_dragging = true;
    updateCursor();
    update();
}

void RegionOverlay::mouseReleaseEvent(QMouseEvent* e)
{
    if(e->button() != Qt::LeftButton)
        return;
    m_cursor = e->globalPosition().toPoint();
    if(m_selDrag)
    {
        m_selDrag = 0;
        updateCursor();
        update();
        return;
    }
    if(!m_pressed)
        return;
    const Target t = currentTarget();
    const bool dragged = m_dragging;
    m_pressed = false;
    m_dragging = false;
    if(dragged && t.physical.width() >= kDragThreshold && t.physical.height() >= kDragThreshold)
    {
        startAdjusting(t.logical);
        return;
    }
    if(m_adjusting)
    {
        update(); // a click outside the region keeps it
        return;
    }
    finish(currentTarget().physical); // a click (or a tiny drag): the window or screen under the cursor
}

void RegionOverlay::mouseDoubleClickEvent(QMouseEvent* e)
{
    if(e->button() == Qt::LeftButton && m_adjusting && selectionHit(e->globalPosition().toPoint()) == EdgeMove)
    {
        m_selDrag = 0;
        finish(currentTarget().physical);
        return;
    }
    mousePressEvent(e);
}

void RegionOverlay::keyPressEvent(QKeyEvent* e)
{
    if(e->key() == Qt::Key_Escape)
    {
        finish({});
        return;
    }
    if(e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
    {
        if(m_adjusting)
            finish(currentTarget().physical);
        else if(!m_lastPhysical.isEmpty())
            finish(m_lastPhysical);
        return;
    }
    if(m_adjusting && !m_selDrag && !m_dragging)
    {
        QPoint d;
        switch(e->key())
        {
            case Qt::Key_Left: d = {-1, 0}; break;
            case Qt::Key_Right: d = {1, 0}; break;
            case Qt::Key_Up: d = {0, -1}; break;
            case Qt::Key_Down: d = {0, 1}; break;
            default: break;
        }
        if(!d.isNull())
        {
            // Arrows move the region; Shift+arrows move its right/bottom edge.
            const int edges = (e->modifiers() & Qt::ShiftModifier) ? (EdgeRight | EdgeBottom) : EdgeMove;
            m_sel = dragRect(m_sel, edges, d, m_virtual, kMinSelection);
            update();
            return;
        }
    }
    if(e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab)
    {
        setPurpose(m_purpose == CapturePurpose::Record ? CapturePurpose::Screenshot : CapturePurpose::Record);
        return;
    }
    if(e->key() == Qt::Key_H)
    {
        m_showHelp = !m_showHelp;
        emit helpToggled(m_showHelp);
        update();
        return;
    }
    if(e->key() == Qt::Key_S || e->key() == Qt::Key_R)
    {
        setPurpose(e->key() == Qt::Key_S ? CapturePurpose::Screenshot : CapturePurpose::Record);
        return;
    }
    if(e->key() == Qt::Key_Shift)
    {
        m_shift = true;
        update();
    }
}

void RegionOverlay::keyReleaseEvent(QKeyEvent* e)
{
    if(e->key() == Qt::Key_Shift)
    {
        m_shift = false;
        update();
    }
}

void RegionOverlay::finish(const QRect& physical)
{
    if(m_done)
        return;
    m_done = true;
    releaseKeyboard();
    hide();
    if(physical.isEmpty())
        emit cancelled();
    else
        emit selected(physical, m_purpose);
    close();
}

} // namespace qr
