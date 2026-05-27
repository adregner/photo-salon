#include "MainWindow.h"
#include "HelpOverlay.h"
#include "ImageFormats.h"
#include "ImageViewer.h"
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QInputDialog>
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

    connect(viewer, &ImageViewer::fullscreenToggleRequested, this, &MainWindow::toggleFullscreen);

    connect(viewer, &ImageViewer::folderBrowseRequested, this, [this, viewer]() {
        QString currentPath = viewer->currentPath();
        if (currentPath.isEmpty()) return;

        QDir dir = QFileInfo(currentPath).absoluteDir();
        QStringList files = dir.entryList(supportedExtensions(), QDir::Files, QDir::Name);
        if (files.isEmpty()) return;

        int current = files.indexOf(QFileInfo(currentPath).fileName());
        bool ok = false;
        QString selected = QInputDialog::getItem(
            this,
            QStringLiteral("Open Image"),
            QStringLiteral("Select image:"),
            files,
            qMax(0, current),
            /*editable=*/false,
            &ok);
        if (ok && !selected.isEmpty())
            viewer->loadImage(dir.absoluteFilePath(selected));
    });
}

void MainWindow::toggleFullscreen() {
    if (windowState() & Qt::WindowFullScreen) {
        setWindowState(m_windowStateBeforeFullscreen);
    } else {
        m_windowStateBeforeFullscreen = windowState();
        showFullScreen();
    }
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (m_helpOverlay)
        m_helpOverlay->resize(size());
}
