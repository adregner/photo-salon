#include "MainWindow.h"
#include "BackgroundColorPicker.h"
#include "HelpOverlay.h"
#include "ImageFormats.h"
#include "ImageViewer.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QScreen>

MainWindow::MainWindow(const QString &imagePath, QWidget *parent)
    : QMainWindow(parent)
{
    m_viewer = new ImageViewer(imagePath, this);
    auto *viewer = m_viewer;
    setCentralWidget(viewer);
    qApp->installEventFilter(this);

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

    m_colorPicker = new BackgroundColorPicker(this);
    m_colorPicker->hide();
    connect(viewer, &ImageViewer::backgroundPickerRequested, this, [this, viewer]() {
        m_colorPicker->setCurrentValue(viewer->backgroundGrey());
        int x = 10;
        int y = height() - m_colorPicker->sizeHint().height() - 10;
        m_colorPicker->move(x, y);
        m_colorPicker->show();
        m_colorPicker->raise();
        m_colorPicker->setFocus();
    });
    connect(m_colorPicker, &BackgroundColorPicker::greyChanged,
            viewer, &ImageViewer::setBackgroundGrey);

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

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() != QEvent::KeyPress)
        return false;
    if (QApplication::activeModalWidget())
        return false;

    auto *ke = static_cast<QKeyEvent *>(event);

    if (ke->key() == Qt::Key_Escape) {
        if (m_colorPicker && m_colorPicker->isVisible()) {
            m_colorPicker->hide();
            return true;
        }
        if (m_helpOverlay && m_helpOverlay->isVisible()) {
            m_viewer->closeHelp();
            return true;
        }
        if (windowState() & Qt::WindowFullScreen) {
            toggleFullscreen();
            return true;
        }
        return false;
    }

    // Forward all other key events to the viewer when something else has focus
    if (m_viewer && obj != m_viewer && obj != m_viewer->viewport()) {
        QCoreApplication::sendEvent(m_viewer, event);
        return true;
    }

    return false;
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
    if (m_colorPicker && m_colorPicker->isVisible()) {
        int y = height() - m_colorPicker->sizeHint().height() - 10;
        m_colorPicker->move(10, y);
    }
}
