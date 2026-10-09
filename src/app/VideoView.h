#pragma once

#include <QImage>
#include <QPointF>
#include <QWidget>

class QVideoFrame;
class QVideoSink;

namespace qr
{

// Raster video surface: paints frames from a QVideoSink with QPainter. Unlike
// QVideoWidget it needs no OpenGL, so the preview works regardless of GPU driver setup.
//
// Zoom/pan for inspecting recordings: the wheel zooms around the cursor, dragging pans,
// double-click toggles fit ↔ 100%, keys 0 (fit) / 1 (100%) / + / -. "100%" means one
// recorded pixel per physical screen pixel.
//
// Optional crop rectangle (image pixels): the outside is dimmed, the overlay is laid out
// inside it, and in crop-editing mode the mouse drags its edges/corners/body instead of
// panning (dragging outside it draws a new one).
class VideoView : public QWidget
{
    Q_OBJECT

public:
    explicit VideoView(QWidget* parent = nullptr);

    QVideoSink* sink() const { return m_sink; }

    // Shows a still image (e.g. a GIF frame) instead of sink frames.
    void setImage(const QImage& image);
    void clear();
    // Transparent image drawn over every frame, scaled like the video (e.g. a caption).
    void setOverlay(const QImage& overlay);

    void setCrop(const QRect& crop); // null = whole frame
    QRect crop() const { return m_crop; }
    void setCropEditing(bool editing);
    // Crop x/y/size multiple: 2 for video (default), 1 for still images.
    void setCropAlignment(int align) { m_cropAlign = align; }

    void fitToView();
    void setActualSize();
    void zoomBy(qreal factor);
    bool isFit() const { return m_fit; }
    qreal scale() const; // effective scale in physical pixels (1.0 = 100%)

signals:
    void zoomChanged(qreal scale, bool fit);
    void cropChanged(QRect crop); // while dragging; snapped to even pixels

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    void onFrame(const QVideoFrame& frame);
    void showImage(const QImage& image);
    void zoomAround(qreal newScale, QPointF anchor);
    QSizeF logicalImageSize() const;
    qreal fitScale() const;
    QRectF imageRect() const;
    void clampPan();
    void updateCursor();
    QRectF toWidget(const QRectF& imageRectPx) const;
    QPointF toImage(const QPointF& widgetPos) const;
    int cropHitTest(const QPointF& widgetPos) const;
    void applyCropDrag(const QPointF& widgetPos);

    QVideoSink* m_sink;
    QImage m_image;
    QImage m_overlay;
    bool m_fit = true;
    qreal m_scale = 1.0;   // used when !m_fit
    QPointF m_pan;         // image centre offset from widget centre (logical px)
    bool m_dragging = false;
    QPointF m_dragStart;
    QPointF m_panStart;

    QRect m_crop;
    bool m_cropEditing = false;
    int m_cropAlign = 2;
    int m_cropDrag = 0;        // CropHit flags of the active drag
    QPointF m_cropAnchor;      // image coords where the drag started
    QRect m_cropStart;
};

} // namespace qr
