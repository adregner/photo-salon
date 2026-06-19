#include "MainWindow.h"
#include "BackgroundColorPicker.h"
#include "BwConverter.h"
#include "BwPanel.h"
#include "Const.h"
#include "ExifOverlay.h"
#include "ExifReader.h"
#include "ExternalLauncher.h"
#include "HelpOverlay.h"
#include "ExitOverlay.h"
#include "ImageFormats.h"
#include "ImageViewer.h"
#include "OpenDialog.h"
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QResizeEvent>
#include <QPalette>
#include <QPixmap>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

namespace {
// Writes the edited pixmap to a temp file for hand-off to an external editor.
// TIFF is lossless, so round-tripping through the editor preserves full quality
// (unlike a re-encoded JPEG). Returns the path, or an empty string on failure.
QString writeExportForExternalApp(const QPixmap &pixmap) {
    const QString tempPath = QDir::tempPath() + QStringLiteral("/photo-salon-export.tiff");
    if (!pixmap.save(tempPath, "TIFF"))
        return {};
    return tempPath;
}
}  // namespace

MainWindow::MainWindow(const QString &imagePath, QWidget *parent)
    : QMainWindow(parent)
{
    m_viewer = new ImageViewer(imagePath, this);
    auto *viewer = m_viewer;
    setCentralWidget(viewer);
    qApp->installEventFilter(this);

    auto updateTitle = [this](const QString &path) {
        if (path.isEmpty())
            setWindowTitle("photo-salon");
        else
            setWindowTitle(QString("photo-salon — %1").arg(QFileInfo(path).fileName()));
    };
    updateTitle(imagePath);
    // A new image from disk resets every buffer and re-applies that image's saved
    // manifest (orientation/crop/B&W) — see onImageLoaded().
    connect(viewer, &ImageViewer::imagePathChanged, this, [this, viewer, updateTitle](const QString &path) {
        updateTitle(path);
        if (!path.isEmpty() && m_idleOverlay)
            m_idleOverlay->hide();
        onImageLoaded(path);
    });

    QSize imageSize = viewer->nativeImageSize();
    if (!imageSize.isEmpty()) {
        QSize available = screen()->availableGeometry().size();
        resize(imageSize.boundedTo(available));
    } else {
        resize(800, 600);
    }

    if (imagePath.isEmpty()) {
        m_idleOverlay = new QWidget(this);
        m_idleOverlay->setAutoFillBackground(true);
        m_idleOverlay->setBackgroundRole(QPalette::Shadow);
        QPalette p;
        p.setColor(QPalette::Window, Qt::black);
        m_idleOverlay->setPalette(p);
        m_idleOverlay->resize(size());
        m_idleOverlay->raise();
    }

    m_helpOverlay = new HelpOverlay(this);
    m_helpOverlay->resize(size());
    m_helpOverlay->raise();
    updateExternalEditorName();
    connect(viewer, &ImageViewer::helpVisibilityChanged, m_helpOverlay, &QWidget::setVisible);
    if (imagePath.isEmpty()) {
        viewer->setHelpVisible(true);
        QTimer::singleShot(0, this, &MainWindow::openFile);
    }

    m_exifOverlay = new ExifOverlay(this);
    m_exifOverlay->resize(size());
    m_exifOverlay->raise();
    connect(viewer, &ImageViewer::exifRequested, this, [this, viewer]() {
        if (m_exifOverlay->isVisible()) {
            m_exifOverlay->hide();
            return;
        }
        auto data = ExifReader::read(viewer->currentPath());
        const auto state = imageStateData();
        for (auto it = state.cbegin(); it != state.cend(); ++it)
            data.insert(it.key(), it.value());
        m_exifOverlay->setData(data);
        m_exifOverlay->show();
        m_exifOverlay->raise();
    });
    // Refresh EXIF data when a new image is loaded
    connect(viewer, &ImageViewer::imagePathChanged, this, [this](const QString &) {
        if (m_exifOverlay->isVisible())
            m_exifOverlay->hide();
    });

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
            supportedSaveFilter());

        if (savePath.isEmpty()) return;

        if (!display.save(savePath))
            QMessageBox::critical(this, "Save", QString("Failed to save: %1").arg(savePath));
    });

    connect(viewer, &ImageViewer::openExternalRequested, this, [this, viewer](bool useOriginal) {
        if (useOriginal) {
            QString path = viewer->currentPath();
            if (path.isEmpty()) return;
            if (openInExternalApp(path, this))
                updateExternalEditorName();
        } else {
            QPixmap display = viewer->currentDisplayPixmap();
            if (display.isNull()) return;
            QString tempPath = writeExportForExternalApp(display);
            if (tempPath.isEmpty()) return;
            if (openInExternalApp(tempPath, this))
                updateExternalEditorName();
        }
    });

    connect(viewer, &ImageViewer::openExternalPickerRequested, this, [this, viewer]() {
        QPixmap display = viewer->currentDisplayPixmap();
        if (display.isNull()) return;
        QString tempPath = writeExportForExternalApp(display);
        if (tempPath.isEmpty()) return;
        if (openInExternalApp(tempPath, this, /*forcePick=*/true))
            updateExternalEditorName();
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
        if (bwActive() && !m_bwComparing && !m_viewer->cropMode())
            m_viewer->setDisplayPixmap(m_lastBwPixmap);
    });

    m_bwPanel = new BwPanel(this);
    m_bwPanel->hide();

    connect(viewer, &ImageViewer::bwPanelRequested,  this, &MainWindow::onBwPanelRequested);
    connect(viewer, &ImageViewer::bwCompareRequested, this, &MainWindow::toggleBwCompare);

    // Slider/look changes flow straight into the manifest's B&W edit (the canonical
    // settings) and are persisted, then a (debounced) re-conversion is scheduled.
    connect(m_bwPanel, &BwPanel::paramsChanged, this, [this](const BwParams &p) {
        if (bwActive() && !m_bwComparing) {
            m_manifest.bw()->setParams(p);
            persistManifest();
            m_bwDebounce->start();
        }
    });

    connect(m_bwPanel, &BwPanel::compareToggled, this, [this](bool showOriginal) {
        m_bwComparing = showOriginal;
        m_bwPanel->setComparing(m_bwComparing);
        if (!bwActive() || m_baseImage.isNull()) return;
        if (showOriginal)
            showBase();
        else if (!m_lastBwPixmap.isNull())
            m_viewer->setDisplayPixmap(m_lastBwPixmap);
    });

    connect(m_bwPanel, &BwPanel::resetToColorRequested, this, &MainWindow::deactivateBw);

    // Crop apply: fold the viewer's selection into the manifest as a normalized
    // crop edit, re-derive the base from it, and re-run B&W if active. This keeps
    // the manifest the single source of truth for the pipeline base → crop → B&W.
    connect(viewer, &ImageViewer::cropModeChanged, this, [this, viewer](bool cropActive) {
        if (cropActive) {
            // Entering crop: stop any pending BW work; the crop UI shows the color image.
            m_bwDebounce->stop();
            return;
        }
        QRectF sel = viewer->cropRect();
        if (!sel.isValid() || sel.isEmpty())
            sel = QRectF(QPointF(0, 0), QSizeF(m_orientedImage.size()));
        CropEdit &c = m_manifest.ensureCrop();
        c.setRect(CropEdit::toNormalized(sel, m_orientedImage.size()));
        if (c.isFull())
            m_manifest.removeCrop();
        rebuildBase();
        persistManifest();
        if (bwActive()) {
            m_bwComparing = false;
            m_bwPanel->setComparing(false);
            applyBwConversion();
        } else {
            showBase();
        }
    });

    connect(viewer, &ImageViewer::folderBrowseRequested, this, [this, viewer]() {
        QString currentPath = viewer->currentPath();
        if (currentPath.isEmpty()) {
            openFile();
            return;
        }

        QDir dir = QFileInfo(currentPath).absoluteDir();
        QStringList files = dir.entryList(supportedExtensions(), QDir::Files, QDir::Name);
        if (files.isEmpty()) return;

        int current = files.indexOf(QFileInfo(currentPath).fileName());

        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Open Image"));
        auto *layout = new QVBoxLayout(&dialog);
        layout->addWidget(new QLabel(QStringLiteral("Select image:"), &dialog));
        auto *list = new QListWidget(&dialog);
        list->addItems(files);
        list->setCurrentRow(qMax(0, current));
        layout->addWidget(list);

        // Picking a file — single click, double click, or Enter — opens it
        // immediately; Escape dismisses the dialog without changing the image.
        bool opened = false;
        auto openItem = [&](QListWidgetItem *item) {
            if (opened || !item) return;
            opened = true;
            viewer->loadImage(dir.absoluteFilePath(item->text()));
            dialog.accept();
        };
        connect(list, &QListWidget::itemClicked,   &dialog, [&](QListWidgetItem *i) { openItem(i); });
        connect(list, &QListWidget::itemActivated, &dialog, [&](QListWidgetItem *i) { openItem(i); });

        list->setFocus();
        dialog.exec();
    });

    m_exitOverlay = new ExitOverlay(this);
    m_exitOverlay->resize(size());
    m_exitOverlay->raise();

    m_exitDebounce = new QTimer(this);
    m_exitDebounce->setSingleShot(true);
    m_exitDebounce->setInterval(EXIT_DEBOUNCE);
    connect(m_exitDebounce, &QTimer::timeout, m_exitOverlay, &ExitOverlay::hide);

    connect(viewer, &ImageViewer::openFileRequested, this, &MainWindow::openFile);

    connect(viewer, &ImageViewer::rotateRequested,         this, [this]() { applyOrientationStep(OrientationStep::RotateCW); });
    connect(viewer, &ImageViewer::flipHorizontalRequested, this, [this]() { applyOrientationStep(OrientationStep::FlipH); });
    connect(viewer, &ImageViewer::flipVerticalRequested,   this, [this]() { applyOrientationStep(OrientationStep::FlipV); });

    connect(viewer, &ImageViewer::exitRequested, this, [this]() {
        if (m_exitDebounce->isActive()) {
            exitApplication();
        } else {
            m_exitOverlay->show();
            m_exitOverlay->raise();
            m_exitDebounce->start();
        }
    });

    // ImageViewer loaded the image in its constructor, before the signal above was
    // connected, so capture that first image and apply its manifest explicitly.
    onImageLoaded(imagePath);
}

// ---------------------------------------------------------------------------
// Image load / manifest application
// ---------------------------------------------------------------------------
void MainWindow::onImageLoaded(const QString &path) {
    m_diskImage = m_viewer->pixmap().toImage();
    m_manifest  = EditManifest::loadFor(path);

    // Reset transient B&W view state for the new image.
    m_bwComparing = false;
    m_bwDebounce->stop();
    m_lastBwImage  = {};
    m_lastBwPixmap = {};

    rebuildOriented();

    // Re-arm the viewer's crop selection from the saved crop (oriented pixels).
    if (const CropEdit *c = m_manifest.crop())
        m_viewer->setCropRect(QRectF(CropEdit::toPixels(c->rect(), m_orientedImage.size())));

    rebuildBase();

    if (m_manifest.bw()) {
        // Load the saved look into the panel without re-triggering conversion, then
        // run it once. Until it finishes, show the color base rather than flashing
        // the unedited disk image.
        {
            QSignalBlocker block(m_bwPanel);
            m_bwPanel->setParams(m_manifest.bw()->params());
        }
        m_bwPanel->setComparing(false);
        showBase();
        applyBwConversion();
    } else {
        if (m_bwPanel) {
            m_bwPanel->setComparing(false);
            m_bwPanel->hide();
        }
        showBase();
    }
}

void MainWindow::rebuildOriented() {
    const OrientationEdit *o = m_manifest.orientation();
    m_orientedImage = o ? o->apply(m_diskImage) : m_diskImage;
    // The crop UI always works against the full oriented original.
    m_viewer->setBasePixmapForCrop(QPixmap::fromImage(m_orientedImage));
}

void MainWindow::rebuildBase() {
    const CropEdit *c = m_manifest.crop();
    m_baseImage = c ? c->apply(m_orientedImage) : m_orientedImage;
}

void MainWindow::showBase() {
    if (!m_baseImage.isNull())
        m_viewer->setDisplayPixmap(QPixmap::fromImage(m_baseImage));
}

void MainWindow::persistManifest() {
    m_manifest.saveFor(m_viewer->currentPath());
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() != QEvent::KeyPress)
        return false;
    if (QApplication::activeModalWidget())
        return false;

    auto *ke = static_cast<QKeyEvent *>(event);

    if (ke->key() == Qt::Key_Escape) {
        if (m_exifOverlay && m_exifOverlay->isVisible()) {
            m_exifOverlay->hide();
            return true;
        }
        if (m_colorPicker && m_colorPicker->isVisible()) {
            m_colorPicker->hide();
            return true;
        }
        if (m_helpOverlay && m_helpOverlay->isVisible() && !m_viewer->currentPath().isEmpty()) {
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

    // Tab: the viewport event filter doesn't reliably fire before Qt's focus
    // machinery on macOS, so handle Tab at the app-filter level instead.
    // Forward to the viewer widget (not the viewport) so that ImageViewer's
    // focusNextPrevChild override prevents focus traversal and keyPressEvent fires.
    if (ke->key() == Qt::Key_Tab && m_viewer && !m_forwardingKeyEvent) {
        m_forwardingKeyEvent = true;
        QCoreApplication::sendEvent(m_viewer, event);
        m_forwardingKeyEvent = false;
        return true;
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
    if (m_viewer->currentPath().isEmpty())
        m_viewer->setHelpVisible(true);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (m_idleOverlay && m_idleOverlay->isVisible())
        m_idleOverlay->resize(size());
    if (m_helpOverlay)
        m_helpOverlay->resize(size());
    if (m_exifOverlay)
        m_exifOverlay->resize(size());
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

// ---------------------------------------------------------------------------
// Black & white
// ---------------------------------------------------------------------------
void MainWindow::onBwPanelRequested() {
    if (m_bwPanel->isVisible()) {
        m_bwPanel->hide();
        return;
    }

    if (!bwActive()) {
        if (m_baseImage.isNull()) return;
        // Activating B&W records the panel's current look as a manifest edit.
        m_manifest.ensureBw().setParams(m_bwPanel->params());
        persistManifest();
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
    BwEdit *edit = m_manifest.bw();
    if (!edit || m_baseImage.isNull()) return;
    if (m_bwWatcher->isRunning()) {
        m_bwDebounce->start();
        return;
    }
    // Convert off the GUI thread via the edit's own apply() (a value copy keeps the
    // worker independent of later manifest changes).
    QImage src = m_baseImage;
    BwEdit job = *edit;
    m_bwWatcher->setFuture(
        QtConcurrent::run([src, job]() { return job.apply(src); }));
}

void MainWindow::toggleBwCompare() {
    if (!bwActive() || m_baseImage.isNull()) return;
    m_bwComparing = !m_bwComparing;
    m_bwPanel->setComparing(m_bwComparing);
    if (m_bwComparing)
        showBase();
    else if (!m_lastBwPixmap.isNull())
        m_viewer->setDisplayPixmap(m_lastBwPixmap);
}

void MainWindow::deactivateBw() {
    const bool wasActive = bwActive();
    m_manifest.removeBw();
    m_bwComparing = false;
    m_bwDebounce->stop();
    if (wasActive)
        persistManifest();

    // Restore the color image. m_baseImage always holds the current in-memory color
    // image (post-crop), so this is always the right thing to show.
    showBase();

    m_lastBwImage  = {};
    m_lastBwPixmap = {};

    if (m_bwPanel) {
        m_bwPanel->setComparing(false);
        m_bwPanel->hide();
    }
}

// ---------------------------------------------------------------------------
// Orientation
// ---------------------------------------------------------------------------
void MainWindow::applyOrientationStep(OrientationStep step) {
    if (m_diskImage.isNull()) return;

    // If crop is active, apply it first so the transform acts on the cropped image.
    if (m_viewer->cropMode())
        m_viewer->setCropMode(false);

    // Capture the crop selection in the OLD oriented coordinate space before the
    // orientation changes, so it can be re-mapped afterwards.
    const bool hadCrop = m_manifest.crop() != nullptr;
    QRectF oldCropPx;
    if (hadCrop)
        oldCropPx = QRectF(CropEdit::toPixels(m_manifest.crop()->rect(), m_orientedImage.size()));
    const QSize oldOriented = m_orientedImage.size();

    OrientationEdit &o = m_manifest.ensureOrientation();
    QTransform incr;
    switch (step) {
    case OrientationStep::RotateCW: o.rotateClockwise(); incr = QTransform().rotate(90);   break;
    case OrientationStep::FlipH:    o.flipHorizontal();  incr = QTransform().scale(-1, 1); break;
    case OrientationStep::FlipV:    o.flipVertical();    incr = QTransform().scale(1, -1); break;
    }
    if (o.isIdentity())
        m_manifest.removeOrientation();

    // Re-derive the oriented image and re-arm the crop base BEFORE remapping the
    // crop rect: setCropRect() clamps against the (new) crop base, and a 90°/270°
    // rotation swaps width and height, so clamping against the stale bounds would
    // clip the mapped selection.
    rebuildOriented();

    // Remap the saved crop rect so re-entering crop still pre-selects the same
    // region, using the same translation Qt bakes into transformed().
    if (hadCrop) {
        QTransform full = QPixmap::trueMatrix(incr, oldOriented.width(), oldOriented.height());
        m_viewer->setCropRect(full.mapRect(oldCropPx));
        m_manifest.crop()->setRect(
            CropEdit::toNormalized(m_viewer->cropRect(), m_orientedImage.size()));
    }

    rebuildBase();
    persistManifest();

    if (bwActive()) {
        m_bwComparing = false;
        m_bwPanel->setComparing(false);
        applyBwConversion();
    } else {
        showBase();
    }
}

void MainWindow::exitApplication() {
    exit(0);
}

// ---------------------------------------------------------------------------
// Metadata / files
// ---------------------------------------------------------------------------
ExifReader::ExifData MainWindow::imageStateData() const {
    ExifReader::ExifData state;

    const QString edits = m_manifest.summary();
    if (!edits.isEmpty())
        state["State_Edits"] = edits;

    // Original dimensions, taken from the in-memory image as loaded (EXIF-oriented),
    // so they are always present and match what the user sees. This overrides the
    // un-oriented header size from QImageReader::size() and survives any edit —
    // after a crop the overlay still shows the original size, not just the crop.
    if (!m_diskImage.isNull()) {
        QSize orig = m_diskImage.size();
        state["Dimensions"] = QString("%1 × %2").arg(orig.width()).arg(orig.height());
    }
    // Current dimensions, shown only once an edit changes them — a crop, or a
    // 90°/270° rotation that swaps width and height.
    if (!m_diskImage.isNull() && !m_baseImage.isNull()
        && m_baseImage.size() != m_diskImage.size()) {
        QSize cur = m_baseImage.size();
        state["CurrentDimensions"] = QString("→ %1 × %2").arg(cur.width()).arg(cur.height());
    }
    return state;
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

void MainWindow::updateExternalEditorName() {
    QString path = QSettings().value(QStringLiteral("externalEditor/appPath")).toString();
    m_helpOverlay->setExternalEditorName(
        path.isEmpty() ? QString{} : QFileInfo(path).baseName());
}
