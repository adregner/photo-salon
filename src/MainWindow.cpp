#include "MainWindow.h"
#include "HelpOverlay.h"
#include "ImageViewer.h"
#include <QFileInfo>
#include <QGuiApplication>
#include <QResizeEvent>
#include <QScreen>

MainWindow::MainWindow(const QString &imagePath, QWidget *parent)
    : QMainWindow(parent)
{
    auto *viewer = new ImageViewer(imagePath, this);
    setCentralWidget(viewer);

    setWindowTitle(QString("photo-salon — %1").arg(QFileInfo(imagePath).fileName()));
    connect(viewer, &ImageViewer::imagePathChanged, this, [this](const QString &path) {
        setWindowTitle(QString("photo-salon — %1").arg(QFileInfo(path).fileName()));
    });

    QSize imageSize = viewer->nativeImageSize();
    if (!imageSize.isEmpty()) {
        QSize available = screen()->availableGeometry().size();
        resize(imageSize.boundedTo(available));
    } else {
        resize(800, 600);
    }

    m_helpOverlay = new HelpOverlay(this);
    m_helpOverlay->resize(size());
    m_helpOverlay->raise();
    connect(viewer, &ImageViewer::helpVisibilityChanged, m_helpOverlay, &QWidget::setVisible);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (m_helpOverlay)
        m_helpOverlay->resize(size());
}
