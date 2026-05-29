#include "ImageViewer.h"
#include "ImageFormats.h"
#include <QDir>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>
#include <cmath>

ImageViewer::ImageViewer(const QString &imagePath, QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setBackgroundBrush(Qt::black);
    loadImage(imagePath);
}

void ImageViewer::loadImage(const QString &path) {
    m_scene->clear();
    m_pixmapItem = nullptr;
    m_imagePath = path;
    m_cropRect = QRectF();
    m_cropMode = false;
    m_activeHandle = CropHandle::None;
    m_cropBasePixmap = {};

    QPixmap pixmap(path);
    if (pixmap.isNull()) {
        auto *errorText = new QGraphicsTextItem(
            QString("Failed to load image:\n%1").arg(path));
        errorText->setDefaultTextColor(Qt::red);
        m_scene->addItem(errorText);
    } else {
        m_nativeSize = pixmap.size();
        m_pixmapItem = m_scene->addPixmap(pixmap);
        m_scene->setSceneRect(pixmap.rect());
        m_fitted = true;
        fitImage();
    }
    emit imagePathChanged(m_imagePath);
}

QPixmap ImageViewer::pixmap() const {
    return m_pixmapItem ? m_pixmapItem->pixmap() : QPixmap{};
}

QPixmap ImageViewer::currentDisplayPixmap() const {
    return m_pixmapItem ? m_pixmapItem->pixmap() : QPixmap{};
}

void ImageViewer::navigate(int delta) {
    if (m_imagePath.isEmpty()) return;
    QFileInfo info(m_imagePath);
    QDir dir = info.absoluteDir();
    QStringList exts = supportedExtensions();
    exts.removeAll("*.svg");
    QStringList files = dir.entryList(exts, QDir::Files, QDir::Name);
    int idx = files.indexOf(info.fileName());
    if (idx < 0 || files.isEmpty()) return;
    int next = ((idx + delta) % files.size() + files.size()) % files.size();
    loadImage(dir.absoluteFilePath(files[next]));
}

void ImageViewer::showEvent(QShowEvent *event) {
    QGraphicsView::showEvent(event);
    if (m_fitted) fitImage();
}

void ImageViewer::resizeEvent(QResizeEvent *event) {
    QGraphicsView::resizeEvent(event);
    if (m_fitted && isVisible()) fitImage();
}

void ImageViewer::wheelEvent(QWheelEvent *event) {
    const int delta = event->angleDelta().y();
    if (delta == 0) { event->ignore(); return; }
    applyZoom(std::pow(1.15, delta / 120.0));
    event->accept();
}

void ImageViewer::keyPressEvent(QKeyEvent *event) {
    if (m_helpVisible && event->key() != Qt::Key_Question) {
        m_helpVisible = false;
        emit helpVisibilityChanged(false);
    }

    switch (event->key()) {
    case Qt::Key_Left:
        navigate(-1);
        event->accept();
        break;
    case Qt::Key_Right:
        navigate(1);
        event->accept();
        break;
    case Qt::Key_Question:
        m_helpVisible = !m_helpVisible;
        emit helpVisibilityChanged(m_helpVisible);
        event->accept();
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        applyZoom(1.15);
        event->accept();
        break;
    case Qt::Key_Minus:
        applyZoom(1.0 / 1.15);
        event->accept();
        break;
    case Qt::Key_0:
        m_fitted = true;
        fitImage();
        event->accept();
        break;
    case Qt::Key_Tab:
        emit folderBrowseRequested();
        event->accept();
        break;
    case Qt::Key_F:
        emit fullscreenToggleRequested();
        event->accept();
        break;
    case Qt::Key_B:
        emit backgroundPickerRequested();
        event->accept();
        break;
    case Qt::Key_C:
        setCropMode(!m_cropMode);
        event->accept();
        break;
    case Qt::Key_S:
        emit saveRequested();
        event->accept();
        break;
    case Qt::Key_W:
        emit bwPanelRequested();
        event->accept();
        break;
    case Qt::Key_Q:
        emit exitRequested();
        event->accept();
        break;
    case Qt::Key_Backslash:
        emit bwCompareRequested();
        event->accept();
        break;
    case Qt::Key_O:
        emit openFileRequested();
        event->accept();
        break;
    default:
        QGraphicsView::keyPressEvent(event);
        break;
    }
}

void ImageViewer::applyZoom(double factor) {
    const double currentScale = transform().m11();
    const double newScale = currentScale * factor;
    if (newScale < 0.05 || newScale > 32.0) return;
    scale(factor, factor);
    m_fitted = false;
}

void ImageViewer::closeHelp() {
    if (m_helpVisible) {
        m_helpVisible = false;
        emit helpVisibilityChanged(false);
    }
}

void ImageViewer::setCropMode(bool active) {
    if (m_cropMode == active) return;
    m_cropMode = active;

    if (active) {
        // Use in-memory base pixmap if available (avoids disk reload and respects
        // any in-memory edits like a previous crop); fall back to disk only if unset.
        if (m_pixmapItem) {
            QPixmap base = !m_cropBasePixmap.isNull() ? m_cropBasePixmap : QPixmap(m_imagePath);
            if (!base.isNull())
                m_pixmapItem->setPixmap(base);
        }

        if (m_pixmapItem) {
            QRectF imageRect = QRectF(m_pixmapItem->pixmap().rect());
            m_scene->setSceneRect(imageRect);

            // Initialize or clamp crop rect to image bounds
            if (!m_cropRect.isValid() || m_cropRect.isEmpty()) {
                m_cropRect = imageRect;
            } else {
                m_cropRect = m_cropRect.intersected(imageRect);
                if (!m_cropRect.isValid() || m_cropRect.isEmpty())
                    m_cropRect = imageRect;
            }

            m_fitted = true;
            fitImage();
        }

        m_activeHandle = CropHandle::None;
        setDragMode(QGraphicsView::NoDrag);
        viewport()->setCursor(Qt::ArrowCursor);
    } else {
        // Apply crop: extract selected region from the (reloaded) original
        if (m_pixmapItem) {
            QRectF imageRect = QRectF(m_pixmapItem->pixmap().rect());
            QRectF cropRect = (m_cropRect.isValid() && !m_cropRect.isEmpty())
                ? m_cropRect.intersected(imageRect)
                : imageRect;

            QPixmap full = m_pixmapItem->pixmap();
            QRect cropPx = cropRect.toAlignedRect().intersected(full.rect());
            if (!cropPx.isEmpty())
                m_pixmapItem->setPixmap(full.copy(cropPx));

            m_scene->setSceneRect(m_pixmapItem->pixmap().rect());
            m_fitted = true;
            fitImage();
        }

        m_activeHandle = CropHandle::None;
        setDragMode(QGraphicsView::ScrollHandDrag);
        viewport()->unsetCursor();
    }

    emit cropModeChanged(m_cropMode);
}

void ImageViewer::setCropRect(const QRectF &rect) {
    if (!m_pixmapItem) return;
    QRectF imageRect = QRectF(m_pixmapItem->pixmap().rect());
    m_cropRect = rect.intersected(imageRect);
    if (m_cropMode) viewport()->update();
}

ImageViewer::CropHandle ImageViewer::hitTestHandle(const QPoint &vp) const {
    if (!m_cropRect.isValid() || m_cropRect.isEmpty()) return CropHandle::None;

    QRect vr = mapFromScene(m_cropRect).boundingRect();
    const int E = 8;   // edge grab distance in viewport pixels
    const int C = 14;  // corner grab distance in viewport pixels

    // Corners take priority
    if (qAbs(vp.x() - vr.left())  <= C && qAbs(vp.y() - vr.top())    <= C) return CropHandle::TopLeft;
    if (qAbs(vp.x() - vr.right()) <= C && qAbs(vp.y() - vr.top())    <= C) return CropHandle::TopRight;
    if (qAbs(vp.x() - vr.left())  <= C && qAbs(vp.y() - vr.bottom()) <= C) return CropHandle::BottomLeft;
    if (qAbs(vp.x() - vr.right()) <= C && qAbs(vp.y() - vr.bottom()) <= C) return CropHandle::BottomRight;

    // Edges (grab anywhere along the edge, not just the midpoint handle)
    if (qAbs(vp.y() - vr.top())    <= E && vp.x() > vr.left() && vp.x() < vr.right()) return CropHandle::Top;
    if (qAbs(vp.y() - vr.bottom()) <= E && vp.x() > vr.left() && vp.x() < vr.right()) return CropHandle::Bottom;
    if (qAbs(vp.x() - vr.left())   <= E && vp.y() > vr.top()  && vp.y() < vr.bottom()) return CropHandle::Left;
    if (qAbs(vp.x() - vr.right())  <= E && vp.y() > vr.top()  && vp.y() < vr.bottom()) return CropHandle::Right;

    // Interior → move
    if (vr.contains(vp)) return CropHandle::Move;

    return CropHandle::None;
}

void ImageViewer::updateCropCursor(const QPoint &viewportPos) {
    switch (hitTestHandle(viewportPos)) {
    case CropHandle::None:        viewport()->setCursor(Qt::ArrowCursor); break;
    case CropHandle::Move:        viewport()->setCursor(Qt::SizeAllCursor); break;
    case CropHandle::TopLeft:
    case CropHandle::BottomRight: viewport()->setCursor(Qt::SizeFDiagCursor); break;
    case CropHandle::TopRight:
    case CropHandle::BottomLeft:  viewport()->setCursor(Qt::SizeBDiagCursor); break;
    case CropHandle::Top:
    case CropHandle::Bottom:      viewport()->setCursor(Qt::SizeVerCursor); break;
    case CropHandle::Left:
    case CropHandle::Right:       viewport()->setCursor(Qt::SizeHorCursor); break;
    }
}

void ImageViewer::mousePressEvent(QMouseEvent *event) {
    if (!m_cropMode) { QGraphicsView::mousePressEvent(event); return; }
    if (event->button() == Qt::LeftButton) {
        m_activeHandle = hitTestHandle(event->pos());
        m_dragStartScene = mapToScene(event->pos());
        m_dragStartCropRect = m_cropRect;
    }
    event->accept();
}

void ImageViewer::mouseMoveEvent(QMouseEvent *event) {
    if (!m_cropMode) { QGraphicsView::mouseMoveEvent(event); return; }

    if ((event->buttons() & Qt::LeftButton) && m_activeHandle != CropHandle::None) {
        QPointF scenePos = mapToScene(event->pos());
        QPointF d = scenePos - m_dragStartScene;
        QRectF imageRect = m_pixmapItem ? QRectF(m_pixmapItem->pixmap().rect()) : QRectF();
        QRectF r = m_dragStartCropRect;

        switch (m_activeHandle) {
        case CropHandle::Move:
            r.translate(d);
            if (r.left()   < imageRect.left())   r.moveLeft(imageRect.left());
            if (r.right()  > imageRect.right())  r.moveRight(imageRect.right());
            if (r.top()    < imageRect.top())    r.moveTop(imageRect.top());
            if (r.bottom() > imageRect.bottom()) r.moveBottom(imageRect.bottom());
            break;
        case CropHandle::TopLeft:     r.setTopLeft(r.topLeft() + d);         break;
        case CropHandle::Top:         r.setTop(r.top() + d.y());              break;
        case CropHandle::TopRight:    r.setTopRight(r.topRight() + d);        break;
        case CropHandle::Left:        r.setLeft(r.left() + d.x());            break;
        case CropHandle::Right:       r.setRight(r.right() + d.x());          break;
        case CropHandle::BottomLeft:  r.setBottomLeft(r.bottomLeft() + d);    break;
        case CropHandle::Bottom:      r.setBottom(r.bottom() + d.y());        break;
        case CropHandle::BottomRight: r.setBottomRight(r.bottomRight() + d);  break;
        case CropHandle::None: break;
        }

        r = r.normalized().intersected(imageRect);
        if (r.width() >= 1.0 && r.height() >= 1.0)
            m_cropRect = r;

        viewport()->update();
    } else {
        updateCropCursor(event->pos());
    }

    event->accept();
}

void ImageViewer::mouseReleaseEvent(QMouseEvent *event) {
    if (!m_cropMode) { QGraphicsView::mouseReleaseEvent(event); return; }
    if (event->button() == Qt::LeftButton) {
        m_activeHandle = CropHandle::None;
        updateCropCursor(event->pos());
    }
    event->accept();
}

void ImageViewer::drawForeground(QPainter *painter, const QRectF &rect) {
    QGraphicsView::drawForeground(painter, rect);

    if (!m_cropMode || !m_pixmapItem) return;

    QRectF imageRect = QRectF(m_pixmapItem->pixmap().rect());
    QRectF crop = (m_cropRect.isValid() && !m_cropRect.isEmpty()) ? m_cropRect : imageRect;

    // Overlay the four excluded regions in scene coordinates
    QColor overlay(0, 0, 0, 160);
    painter->setBrush(overlay);
    painter->setPen(Qt::NoPen);

    auto fill = [&](const QRectF &r) { if (r.width() > 0 && r.height() > 0) painter->drawRect(r); };
    fill({imageRect.left(),  imageRect.top(),  imageRect.width(), crop.top()    - imageRect.top()});
    fill({imageRect.left(),  crop.bottom(),    imageRect.width(), imageRect.bottom() - crop.bottom()});
    fill({imageRect.left(),  crop.top(),       crop.left()  - imageRect.left(), crop.height()});
    fill({crop.right(),      crop.top(),       imageRect.right() - crop.right(), crop.height()});

    // Switch to viewport coordinates for the border and resize handles
    painter->save();
    painter->resetTransform();

    QRect vr = mapFromScene(crop).boundingRect();

    // Border
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(Qt::white, 1.5));
    painter->drawRect(vr);

    // Handles at corners and edge midpoints
    const int H = 8, H2 = 4;
    painter->setBrush(Qt::white);
    painter->setPen(QPen(Qt::black, 1));

    auto handle = [&](int cx, int cy) {
        painter->drawRect(cx - H2, cy - H2, H, H);
    };

    handle(vr.left(),  vr.top());
    handle(vr.right(), vr.top());
    handle(vr.left(),  vr.bottom());
    handle(vr.right(), vr.bottom());
    handle((vr.left() + vr.right()) / 2, vr.top());
    handle((vr.left() + vr.right()) / 2, vr.bottom());
    handle(vr.left(),  (vr.top() + vr.bottom()) / 2);
    handle(vr.right(), (vr.top() + vr.bottom()) / 2);

    painter->restore();
}

void ImageViewer::setDisplayPixmap(const QPixmap &px) {
    if (m_pixmapItem)
        m_pixmapItem->setPixmap(px);
}

void ImageViewer::setBasePixmapForCrop(const QPixmap &px) {
    m_cropBasePixmap = px;
}

void ImageViewer::setBackgroundGrey(int value) {
    m_backgroundGrey = qBound(0, value, 255);
    setBackgroundBrush(QColor(m_backgroundGrey, m_backgroundGrey, m_backgroundGrey));
}

void ImageViewer::fitImage() {
    if (!m_scene->items().isEmpty())
        fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
}
