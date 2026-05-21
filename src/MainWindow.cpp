#include "MainWindow.h"
#include "ImageViewer.h"
#include <QFileInfo>

MainWindow::MainWindow(const QString &imagePath, QWidget *parent)
    : QMainWindow(parent)
{
    auto *viewer = new ImageViewer(imagePath, this);
    setCentralWidget(viewer);

    QString filename = QFileInfo(imagePath).fileName();
    setWindowTitle(QString("photo-salon — %1").arg(filename));
    resize(1024, 768);
}
