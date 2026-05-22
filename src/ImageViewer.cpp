#include "ImageViewer.h"
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsTextItem>
#include <QShowEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QPixmap>
#include <QPainter>
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

    QPixmap pixmap(imagePath);
    if (pixmap.isNull()) {
        auto *errorText = new QGraphicsTextItem(
            QString("Failed to load image:\n%1").arg(imagePath));
        errorText->setDefaultTextColor(Qt::red);
        m_scene->addItem(errorText);
    } else {
        m_scene->addPixmap(pixmap);
        m_scene->setSceneRect(pixmap.rect());
        fitImage();
    }
}

void ImageViewer::showEvent(QShowEvent *event) {
    QGraphicsView::showEvent(event);
    if (m_fitted)
        fitImage();
}

void ImageViewer::wheelEvent(QWheelEvent *event) {
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        event->ignore();
        return;
    }
    applyZoom(std::pow(1.15, delta / 120.0));
    event->accept();
}

void ImageViewer::keyPressEvent(QKeyEvent *event) {
    QGraphicsView::keyPressEvent(event);
}

void ImageViewer::applyZoom(double factor) {
    const double currentScale = transform().m11();
    const double newScale = currentScale * factor;
    if (newScale < 0.05 || newScale > 32.0)
        return;
    scale(factor, factor);
    m_fitted = false;
}

void ImageViewer::fitImage() {
    if (!m_scene->items().isEmpty())
        fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
}
