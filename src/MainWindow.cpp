#include "MainWindow.h"
#include "BackgroundColorPicker.h"
#include "BwConverter.h"
#include "BwPanel.h"
#include "Const.h"
#include "HelpOverlay.h"
#include "ExitOverlay.h"
#include "ImageFormats.h"
#include "ImageViewer.h"
#include "OpenDialog.h"
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMessageBox>
#include <QResizeEvent>
#include <QScreen>
#include <QPushButton>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

MainWindow::MainWindow(const QString &imagePath, QWidget *parent)
    : QMainWindow(parent)
{
    m_viewer = new ImageViewer(imagePath, this);
    m_diskPixmap = m_viewer->pixmap();   // capture image from initial load
    m_basePixmap = m_diskPixmap;
    m_viewer->setBasePixmapForCrop(m_diskPixmap);
    auto *viewer = m_viewer;
    setCentralWidget(viewer);
    qApp->installEventFilter(this);

    setWindowTitle(QString("photo-salon — %1").arg(QFileInfo(imagePath).fileName()));
    connect(viewer, &ImageViewer::imagePathChanged, this, [this, viewer](const QString &path) {
        setWindowTitle(QString("photo-salon — %1").arg(QFileInfo(path).fileName()));
        m_diskPixmap = viewer->pixmap();   // new image from disk; reset crop origin
        m_basePixmap = m_diskPixmap;
        viewer->setBasePixmapForCrop(m_diskPixmap);
        deactivateBw();
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

    connect(viewer, &ImageViewer::saveRequested, this, [this, viewer]() {
        QPixmap display = viewer->currentDisplayPixmap();
        if (display.isNull()) return;

        QString savePath = QFileDialog::getSaveFileName(
            this,
            QStringLiteral("Save Image"),
            QFileInfo(viewer->currentPath()).dir().absoluteFilePath(
                QFileInfo(viewer->currentPath()).baseName() + "-saved.jpg"),
            QStringLiteral("JPEG Images (*.jpg *.jpeg);;PNG Images (*.png);;All Files (*)"));

        if (savePath.isEmpty()) return;

        if (!display.save(savePath))
            QMessageBox::critical(this, "Save", QString("Failed to save: %1").arg(savePath));
    });

    m_bwDebounce = new QTimer(this);
    m_bwDebounce->setSingleShot(true);
    m_bwDebounce->setInterval(50);
    connect(m_bwDebounce, &QTimer::timeout, this, &MainWindow::applyBwConversion);

    m_bwWatcher = new QFutureWatcher<QImage>(this);
    connect(m_bwWatcher, &QFutureWatcher<QImage>::finished, this, [this]() {
        m_lastBwImage  = m_bwWatcher->result();
        m_lastBwPixmap = QPixmap::fromImage(m_lastBwImage);
        // Don't clobber the crop UI or show a stale result while crop is active.
        if (m_bwActive && !m_bwComparing && !m_viewer->cropMode())
            m_viewer->setDisplayPixmap(m_lastBwPixmap);
    });

    m_bwPanel = new BwPanel(this);
    m_bwPanel->hide();

    connect(viewer, &ImageViewer::bwPanelRequested,  this, &MainWindow::onBwPanelRequested);
    connect(viewer, &ImageViewer::bwCompareRequested, this, &MainWindow::toggleBwCompare);

    connect(m_bwPanel, &BwPanel::paramsChanged, this, [this](const BwParams &) {
        if (m_bwActive && !m_bwComparing)
            m_bwDebounce->start();
    });

    connect(m_bwPanel, &BwPanel::autoRequested, this, [this]() {
        if (m_originalImage.isNull()) return;
        QImage src = m_originalImage;
        auto *watcher = new QFutureWatcher<BwParams>(this);
        connect(watcher, &QFutureWatcher<BwParams>::finished, this, [this, watcher]() {
            m_bwPanel->setParams(watcher->result());
            watcher->deleteLater();
        });
        watcher->setFuture(QtConcurrent::run([src]() { return BwConverter::autoParams(src); }));
    });

    connect(m_bwPanel, &BwPanel::compareToggled, this, [this](bool showOriginal) {
        m_bwComparing = showOriginal;
        m_bwPanel->setComparing(m_bwComparing);
        if (!m_bwActive || m_originalImage.isNull()) return;
        if (showOriginal)
            m_viewer->setDisplayPixmap(m_basePixmap);
        else if (!m_lastBwPixmap.isNull())
            m_viewer->setDisplayPixmap(m_lastBwPixmap);
    });

    connect(m_bwPanel, &BwPanel::resetToColorRequested, this, &MainWindow::deactivateBw);

    // When crop is applied, update m_basePixmap from the freshly-cropped in-memory
    // image and re-run BW on it. This keeps the pipeline: base → crop → BW → display.
    connect(viewer, &ImageViewer::cropModeChanged, this, [this, viewer](bool cropActive) {
        if (cropActive) {
            // Entering crop: stop any pending BW work; the crop UI shows the color image.
            m_bwDebounce->stop();
        } else {
            // Crop applied: viewer->pixmap() is the freshly-cropped color image.
            m_basePixmap = viewer->pixmap();
            // Always pass m_diskPixmap so re-entering crop shows the full original —
            // the user can expand as well as shrink the selection.
            viewer->setBasePixmapForCrop(m_diskPixmap);
            if (m_bwActive) {
                m_bwComparing = false;
                m_bwPanel->setComparing(false);
                m_originalImage = m_basePixmap.toImage();
                applyBwConversion();
            }
        }
    });

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

    m_exitOverlay = new ExitOverlay(this);
    m_exitOverlay->resize(size());
    m_exitOverlay->raise();

    m_exitDebounce = new QTimer(this);
    m_exitDebounce->setSingleShot(true);
    m_exitDebounce->setInterval(EXIT_DEBOUNCE);
    connect(m_exitDebounce, &QTimer::timeout, m_exitOverlay, &ExitOverlay::hide);

    connect(viewer, &ImageViewer::openFileRequested, this, &MainWindow::openFile);

    connect(viewer, &ImageViewer::exitRequested, this, [this]() {
        if (m_exitDebounce->isActive()) {
            exitApplication();
        } else {
            m_exitOverlay->show();
            m_exitOverlay->raise();
            m_exitDebounce->start();
        }
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
        if (m_bwPanel && m_bwPanel->isVisible()) {
            m_bwPanel->hide();
            return true;
        }
        if (m_viewer && m_viewer->cropMode()) {
            m_viewer->setCropMode(false);
            return true;
        }
        if (windowState() & Qt::WindowFullScreen) {
            toggleFullscreen();
            return true;
        }
        return false;
    }

    // Forward all other key events to the viewer when something else has focus.
    // Guard against re-entry: QGraphicsView::keyPressEvent forwards unhandled keys
    // to the scene via sendEvent, which would trigger this filter again and recurse.
    if (m_viewer && obj != m_viewer && obj != m_viewer->viewport()) {
        if (!m_forwardingKeyEvent) {
            m_forwardingKeyEvent = true;
            QCoreApplication::sendEvent(m_viewer, event);
            m_forwardingKeyEvent = false;
        }
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
    if (m_exitOverlay)
        m_exitOverlay->resize(size());
    if (m_colorPicker && m_colorPicker->isVisible()) {
        int y = height() - m_colorPicker->sizeHint().height() - 10;
        m_colorPicker->move(10, y);
    }
    if (m_bwPanel && m_bwPanel->isVisible()) {
        int y = height() - m_bwPanel->sizeHint().height() - 10;
        m_bwPanel->move(10, y);
    }
}

void MainWindow::onBwPanelRequested() {
    if (m_bwPanel->isVisible()) {
        m_bwPanel->hide();
        return;
    }

    if (!m_bwActive) {
        if (m_basePixmap.isNull()) return;
        m_originalImage = m_basePixmap.toImage();
        m_bwActive = true;
        applyBwConversion();
    }

    m_bwPanel->setComparing(m_bwComparing);
    int x = 10;
    int y = height() - m_bwPanel->sizeHint().height() - 10;
    m_bwPanel->move(x, y);
    m_bwPanel->show();
    m_bwPanel->raise();
    m_bwPanel->setFocus();
}

void MainWindow::applyBwConversion() {
    if (!m_bwActive || m_originalImage.isNull()) return;
    if (m_bwWatcher->isRunning()) {
        m_bwDebounce->start();
        return;
    }
    QImage   src = m_originalImage;
    BwParams p   = m_bwPanel->params();
    m_bwWatcher->setFuture(
        QtConcurrent::run([src, p]() { return BwConverter::convert(src, p); }));
}

void MainWindow::toggleBwCompare() {
    if (!m_bwActive || m_originalImage.isNull()) return;
    m_bwComparing = !m_bwComparing;
    m_bwPanel->setComparing(m_bwComparing);
    if (m_bwComparing)
        m_viewer->setDisplayPixmap(m_basePixmap);
    else if (!m_lastBwPixmap.isNull())
        m_viewer->setDisplayPixmap(m_lastBwPixmap);
}

void MainWindow::deactivateBw() {
    if (!m_bwActive && m_originalImage.isNull()) return;

    m_bwActive    = false;
    m_bwComparing = false;
    m_bwDebounce->stop();

    // Restore the color image. m_basePixmap always holds the current in-memory color
    // image (post-crop), so this is always the right thing to show.
    if (!m_basePixmap.isNull())
        m_viewer->setDisplayPixmap(m_basePixmap);

    m_originalImage = {};
    m_lastBwImage   = {};
    m_lastBwPixmap  = {};
    // m_basePixmap intentionally NOT cleared — it stays as the current color image.

    if (m_bwPanel) {
        m_bwPanel->setComparing(false);
        m_bwPanel->hide();
    }
}

void MainWindow::exitApplication() {
    exit(0);
}

void MainWindow::openFile() {
    QString startDir = m_viewer->currentPath().isEmpty()
        ? QDir::homePath()
        : QFileInfo(m_viewer->currentPath()).absolutePath();
    QString selected = showOpenDialog(this, startDir);
    if (selected.isEmpty())
        return;
    QString resolved = resolveImagePath(selected);
    if (!resolved.isEmpty())
        m_viewer->loadImage(resolved);
}
