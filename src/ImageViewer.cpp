#include "ImageViewer.h"
#include "ImageFormats.h"
#include <QDir>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QKeyEvent>
#include <QPixmap>
#include <QPainter>
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
    loadImage(imagePath);
}

void ImageViewer::loadImage(const QString &path) {
    m_scene->clear();
    m_pixmapItem = nullptr;
    m_imagePath = path;

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
    case Qt::Key_Escape:
        emit fullscreenToggleRequested();
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

void ImageViewer::fitImage() {
    if (!m_scene->items().isEmpty())
        fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
}
