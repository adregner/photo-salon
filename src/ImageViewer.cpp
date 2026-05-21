#include "ImageViewer.h"
#include <QGraphicsScene>
#include <QShowEvent>

ImageViewer::ImageViewer(const QString &imagePath, QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    Q_UNUSED(imagePath)
    setScene(m_scene);
}

void ImageViewer::showEvent(QShowEvent *event) {
    QGraphicsView::showEvent(event);
    fitImage();
}

void ImageViewer::fitImage() {
    if (!m_scene->items().isEmpty())
        fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
}
