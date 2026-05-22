#include "MainWindow.h"
#include "ImageViewer.h"
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>

MainWindow::MainWindow(const QString &imagePath, QWidget *parent)
    : QMainWindow(parent)
{
    auto *viewer = new ImageViewer(imagePath, this);
    setCentralWidget(viewer);

    QString filename = QFileInfo(imagePath).fileName();
    setWindowTitle(QString("photo-salon — %1").arg(filename));

    QSize imageSize = viewer->nativeImageSize();
    if (!imageSize.isEmpty()) {
        QSize available = screen()->availableGeometry().size();
        resize(imageSize.boundedTo(available));
    } else {
        resize(800, 600);
    }
}
