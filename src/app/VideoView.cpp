#include "app/VideoView.h"

#include <QKeyEvent>
#include "core/Geometry.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVideoFrame>
#include <QVideoSink>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace qr
{

namespace
{
constexpr qreal kMinScale = 0.05;
constexpr qreal kMaxScale = 32.0;
constexpr qreal kWheelStep = 1.25;
constexpr qreal kHandleTolerance = 8.0;

enum CropHit
{
    HitNone = 0,
    HitLeft = 1,
    HitRight = 2,
    HitTop = 4,
    HitBottom = 8,
    HitMove = 16,
    HitNew = 32,
};
} // namespace

VideoView::VideoView(QWidget* parent)
    : QWidget(parent)
    , m_sink(new QVideoSink(this))
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::ClickFocus);
    connect(m_sink, &QVideoSink::videoFrameChanged, this, &VideoView::onFrame);
}

void VideoView::onFrame(const QVideoFrame& frame)
{
    if(frame.isValid())
        showImage(frame.toImage());
}

void VideoView::setImage(const QImage& image)
{
    showImage(image);
}

void VideoView::showImage(const QImage& image)
{
    const bool newSize = image.size() != m_image.size();
    m_image = image;
    if(newSize) // a different clip: start from "fit"
        fitToView();
    else
        update();
}

void VideoView::setOverlay(const QImage& overlay)
{
    m_overlay = overlay;
    update();
}

void VideoView::setCrop(const QRect& crop)
{
    m_crop = crop;
    if(m_crop.isNull() && m_cropEditing && !m_image.isNull())
        m_crop = QRect(QPoint(0, 0), m_image.size());
    update();
}

void VideoView::setCropEditing(bool editing)
{
    m_cropEditing = editing;
    m_cropDrag = HitNone;
    if(editing && m_crop.isNull() && !m_image.isNull())
        m_crop = QRect(QPoint(0, 0), m_image.size());
    updateCursor();
    update();
}

QRectF VideoView::toWidget(const QRectF& r) const
{
    const QRectF img = imageRect();
    const qreal k = m_image.isNull() ? 1.0 : img.width() / m_image.width();
    return QRectF(img.topLeft() + r.topLeft() * k, r.size() * k);
}

QPointF VideoView::toImage(const QPointF& pos) const
{
    const QRectF img = imageRect();
    const qreal k = m_image.isNull() ? 1.0 : img.width() / m_image.width();
    return (pos - img.topLeft()) / k;
}

int VideoView::cropHitTest(const QPointF& pos) const
{
    if(m_crop.isNull())
        return HitNew;
    const QRectF c = toWidget(m_crop);
    const bool inY = pos.y() > c.top() - kHandleTolerance && pos.y() < c.bottom() + kHandleTolerance;
    const bool inX = pos.x() > c.left() - kHandleTolerance && pos.x() < c.right() + kHandleTolerance;
    int hit = HitNone;
    if(inY && std::abs(pos.x() - c.left()) < kHandleTolerance)
        hit |= HitLeft;
    else if(inY && std::abs(pos.x() - c.right()) < kHandleTolerance)
        hit |= HitRight;
    if(inX && std::abs(pos.y() - c.top()) < kHandleTolerance)
        hit |= HitTop;
    else if(inX && std::abs(pos.y() - c.bottom()) < kHandleTolerance)
        hit |= HitBottom;
    if(hit)
        return hit;
    return c.contains(pos) ? HitMove : HitNew;
}

void VideoView::applyCropDrag(const QPointF& pos)
{
    const QPointF at = toImage(pos);
    const QPointF d = at - m_cropAnchor;
    QRectF r = m_cropStart;
    if(m_cropDrag == HitNew)
        r = QRectF(m_cropAnchor, at).normalized();
    else if(m_cropDrag == HitMove)
        r.translate(d);
    else
    {
        if(m_cropDrag & HitLeft)
            r.setLeft(std::min(r.left() + d.x(), r.right() - 16));
        if(m_cropDrag & HitRight)
            r.setRight(std::max(r.right() + d.x(), r.left() + 16));
        if(m_cropDrag & HitTop)
            r.setTop(std::min(r.top() + d.y(), r.bottom() - 16));
        if(m_cropDrag & HitBottom)
            r.setBottom(std::max(r.bottom() + d.y(), r.top() + 16));
        // keep a dragged edge from pushing the opposite one when clamped to the frame
        r = r.intersected(QRectF(QPointF(0, 0), QSizeF(m_image.size())));
    }
    const QRect snapped = snapCropRect(r, m_image.size(), 16, m_cropAlign);
    if(snapped != m_crop)
    {
        m_crop = snapped;
        update();
        emit cropChanged(m_crop);
    }
}

void VideoView::clear()
{
    m_image = {};
    update();
}

QSizeF VideoView::logicalImageSize() const
{
    return QSizeF(m_image.size()) / devicePixelRatioF();
}

qreal VideoView::fitScale() const
{
    const QSizeF img = logicalImageSize();
    if(img.isEmpty() || width() <= 0 || height() <= 0)
        return 1.0;
    return std::min(width() / img.width(), height() / img.height());
}

qreal VideoView::scale() const
{
    return m_fit ? fitScale() : m_scale;
}

QRectF VideoView::imageRect() const
{
    const QSizeF size = logicalImageSize() * scale();
    QRectF r(QPointF(0, 0), size);
    r.moveCenter(QRectF(rect()).center() + (m_fit ? QPointF() : m_pan));
    return r;
}

void VideoView::clampPan()
{
    // Centre an axis that fits; otherwise don't let the image edge leave the view.
    const QSizeF size = logicalImageSize() * scale();
    const qreal maxX = std::max(0.0, (size.width() - width()) / 2);
    const qreal maxY = std::max(0.0, (size.height() - height()) / 2);
    m_pan.setX(std::clamp(m_pan.x(), -maxX, maxX));
    m_pan.setY(std::clamp(m_pan.y(), -maxY, maxY));
}

void VideoView::fitToView()
{
    m_fit = true;
    m_pan = {};
    updateCursor();
    update();
    emit zoomChanged(scale(), true);
}

void VideoView::setActualSize()
{
    zoomAround(1.0, QRectF(rect()).center());
}

void VideoView::zoomBy(qreal factor)
{
    zoomAround(scale() * factor, QRectF(rect()).center());
}

void VideoView::zoomAround(qreal newScale, QPointF anchor)
{
    if(m_image.isNull())
        return;
    const qreal oldScale = scale();
    const QPointF oldCenter = imageRect().center();
    newScale = std::clamp(newScale, kMinScale, kMaxScale);
    // Keep the image point under `anchor` fixed while scaling.
    const QPointF imagePoint = (anchor - oldCenter) / oldScale;
    const QPointF newCenter = anchor - imagePoint * newScale;
    m_fit = false;
    m_scale = newScale;
    m_pan = newCenter - QRectF(rect()).center();
    clampPan();
    updateCursor();
    update();
    emit zoomChanged(m_scale, false);
}

void VideoView::setDrawing(bool drawing, Qt::CursorShape cursor)
{
    m_drawing = drawing;
    m_drawCursor = cursor;
    m_drawStroke = false;
    updateCursor();
}

void VideoView::setHighlight(const QRectF& imageRect)
{
    if(imageRect == m_highlight)
        return;
    m_highlight = imageRect;
    update();
}

qreal VideoView::imagePixelsPerWidgetPixel() const
{
    const QRectF img = imageRect();
    return (m_image.isNull() || img.width() <= 0) ? 1.0 : m_image.width() / img.width();
}

void VideoView::updateCursor()
{
    if(m_drawing)
    {
        setCursor(m_drawCursor);
        return;
    }
    if(m_cropEditing)
    {
        setCursor(Qt::CrossCursor);
        return;
    }
    const QSizeF size = logicalImageSize() * scale();
    const bool pannable = !m_fit && (size.width() > width() + 0.5 || size.height() > height() + 0.5);
    setCursor(pannable ? (m_dragging ? Qt::ClosedHandCursor : Qt::OpenHandCursor) : Qt::ArrowCursor);
}

void VideoView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor("#111317"));
    if(m_image.isNull())
        return;
    // Smooth when shrinking or mildly enlarging; hard pixels from 200% so they can be inspected.
    p.setRenderHint(QPainter::SmoothPixmapTransform, scale() < 2.0 && std::abs(scale() - 1.0) > 1e-3);
    p.drawImage(imageRect(), m_image);

    const bool cropped = !m_crop.isNull() && m_crop != QRect(QPoint(0, 0), m_image.size());
    const QRectF target = m_crop.isNull() ? imageRect() : toWidget(m_crop);
    if(!m_overlay.isNull())
    {
        p.setRenderHint(QPainter::SmoothPixmapTransform, scale() < 2.0);
        p.drawImage(target, m_overlay);
    }
    if(!m_highlight.isNull())
    {
        // Two-tone dashes stay visible on any content.
        const QRectF r = toWidget(m_highlight).adjusted(-3, -3, 3, 3);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0, 0, 0, 160), 1));
        p.drawRect(r);
        p.setPen(QPen(Qt::white, 1, Qt::DashLine));
        p.drawRect(r);
    }
    if(cropped || m_cropEditing)
    {
        p.setRenderHint(QPainter::Antialiasing, false);
        QPainterPath outside;
        outside.addRect(imageRect());
        QPainterPath inside;
        inside.addRect(target);
        p.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 160));
        QPen pen(m_cropEditing ? QColor("#ff3b3f") : QColor(255, 255, 255, 180), m_cropEditing ? 2 : 1,
                 m_cropEditing ? Qt::SolidLine : Qt::DashLine);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(target);
        if(m_cropEditing)
        {
            p.setBrush(QColor("#ff3b3f"));
            p.setPen(Qt::white);
            const QPointF c = target.center();
            for(const QPointF& h : {target.topLeft(), target.topRight(), target.bottomLeft(), target.bottomRight(),
                                    QPointF(c.x(), target.top()), QPointF(c.x(), target.bottom()),
                                    QPointF(target.left(), c.y()), QPointF(target.right(), c.y())})
                p.drawRect(QRectF(h - QPointF(4, 4), QSizeF(8, 8)));
        }
    }
}

void VideoView::resizeEvent(QResizeEvent*)
{
    if(m_fit)
        emit zoomChanged(scale(), true);
    else
        clampPan();
    updateCursor();
}

void VideoView::wheelEvent(QWheelEvent* e)
{
    const qreal notches = e->angleDelta().y() / 120.0;
    if(notches == 0 || m_image.isNull())
        return;
    zoomAround(scale() * std::pow(kWheelStep, notches), e->position());
    e->accept();
}

void VideoView::mousePressEvent(QMouseEvent* e)
{
    if(m_cropEditing && e->button() == Qt::LeftButton && !m_image.isNull())
    {
        m_cropDrag = cropHitTest(e->position());
        m_cropAnchor = toImage(e->position());
        m_cropStart = m_crop;
        return;
    }
    if(m_drawing && e->button() == Qt::LeftButton && !m_image.isNull())
    {
        m_drawStroke = true;
        emit drawPressed(toImage(e->position()));
        return;
    }
    const bool panButton = e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && !m_drawing);
    if(!panButton || m_fit)
        return QWidget::mousePressEvent(e);
    m_dragging = true;
    m_dragStart = e->position();
    m_panStart = m_pan;
    updateCursor();
}

void VideoView::mouseMoveEvent(QMouseEvent* e)
{
    if(m_cropEditing)
    {
        if(m_cropDrag != HitNone)
            applyCropDrag(e->position());
        else
        {
            const int hit = cropHitTest(e->position());
            const bool h = hit & (HitLeft | HitRight), v = hit & (HitTop | HitBottom);
            if(h && v)
                setCursor(((hit & HitLeft) != 0) == ((hit & HitTop) != 0) ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
            else if(h)
                setCursor(Qt::SizeHorCursor);
            else if(v)
                setCursor(Qt::SizeVerCursor);
            else
                setCursor(hit == HitMove ? Qt::SizeAllCursor : Qt::CrossCursor);
        }
        return;
    }
    if(m_drawStroke)
    {
        emit drawMoved(toImage(e->position()));
        return;
    }
    if(!m_dragging)
        return QWidget::mouseMoveEvent(e);
    m_pan = m_panStart + (e->position() - m_dragStart);
    clampPan();
    update();
}

void VideoView::mouseReleaseEvent(QMouseEvent* e)
{
    if(m_cropEditing && e->button() == Qt::LeftButton)
    {
        m_cropDrag = HitNone;
        return;
    }
    if(e->button() == Qt::LeftButton && m_drawStroke)
    {
        m_drawStroke = false;
        emit drawReleased(toImage(e->position()));
        return;
    }
    if((e->button() == Qt::LeftButton || e->button() == Qt::MiddleButton) && m_dragging)
    {
        m_dragging = false;
        updateCursor();
    }
    QWidget::mouseReleaseEvent(e);
}

void VideoView::mouseDoubleClickEvent(QMouseEvent* e)
{
    if(m_drawing)
        return mousePressEvent(e); // quick successive strokes, not a zoom toggle
    if(m_image.isNull() || m_cropEditing)
        return;
    if(m_fit)
        zoomAround(1.0, e->position());
    else
        fitToView();
}

void VideoView::keyPressEvent(QKeyEvent* e)
{
    switch(e->key())
    {
        case Qt::Key_0: fitToView(); break;
        case Qt::Key_1: setActualSize(); break;
        case Qt::Key_Plus:
        case Qt::Key_Equal: zoomBy(kWheelStep); break;
        case Qt::Key_Minus: zoomBy(1.0 / kWheelStep); break;
        default: QWidget::keyPressEvent(e); return;
    }
}

} // namespace qr
