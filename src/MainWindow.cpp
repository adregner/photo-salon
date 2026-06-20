#include "MainWindow.h"
#include "AdjustPanel.h"
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

    // One debounce + one off-thread worker drive the whole post-crop pipeline
    // (adjust → color → B&W), so the panels never race each other on the display.
    m_renderDebounce = new QTimer(this);
    m_renderDebounce->setSingleShot(true);
    m_renderDebounce->setInterval(50);
    connect(m_renderDebounce, &QTimer::timeout, this, &MainWindow::applyRender);

    m_renderWatcher = new QFutureWatcher<QImage>(this);
    connect(m_renderWatcher, &QFutureWatcher<QImage>::finished, this, [this]() {
        m_lastRenderPixmap = QPixmap::fromImage(m_renderWatcher->result());
        // Don't clobber the crop UI, show a stale result, or override compare.
        if (hasDisplayEdits() && !m_comparing && !m_viewer->cropMode())
            m_viewer->setDisplayPixmap(m_lastRenderPixmap);
    });

    m_adjustPanel = new AdjustPanel(this);
    m_adjustPanel->hide();
    connect(viewer, &ImageViewer::adjustPanelRequested, this, &MainWindow::onAdjustPanelRequested);

    // Light/tone and colour changes flow into their own manifest edits (created
    // only while non-neutral) and are persisted, then a render is scheduled.
    connect(m_adjustPanel, &AdjustPanel::adjustParamsChanged, this, [this](const AdjustParams &p) {
        if (ImageAdjust::isNeutral(p)) m_manifest.removeAdjust();
        else                           m_manifest.ensureAdjust().setParams(p);
        persistManifest();
        scheduleRender();
    });
    connect(m_adjustPanel, &AdjustPanel::colorParamsChanged, this, [this](const ColorParams &p) {
        if (ImageAdjust::isNeutral(p)) m_manifest.removeColor();
        else                           m_manifest.ensureColor().setParams(p);
        persistManifest();
        scheduleRender();
    });

    m_bwPanel = new BwPanel(this);
    m_bwPanel->hide();

    connect(viewer, &ImageViewer::bwPanelRequested,  this, &MainWindow::onBwPanelRequested);
    connect(viewer, &ImageViewer::bwCompareRequested, this, &MainWindow::toggleCompare);

    // Slider/look changes flow straight into the manifest's B&W edit (the canonical
    // settings) and are persisted, then a (debounced) re-render is scheduled.
    connect(m_bwPanel, &BwPanel::paramsChanged, this, [this](const BwParams &p) {
        if (bwActive() && !m_comparing) {
            m_manifest.bw()->setParams(p);
            persistManifest();
            scheduleRender();
        }
    });

    connect(m_bwPanel, &BwPanel::compareToggled, this, [this](bool showOriginal) {
        m_comparing = showOriginal;
        m_bwPanel->setComparing(m_comparing);
        if (!hasDisplayEdits() || m_baseImage.isNull()) return;
        if (showOriginal)
            showBase();
        else if (!m_lastRenderPixmap.isNull())
            m_viewer->setDisplayPixmap(m_lastRenderPixmap);
        else
            scheduleRender();
    });

    connect(m_bwPanel, &BwPanel::resetToColorRequested, this, &MainWindow::deactivateBw);

    // Crop apply: fold the viewer's selection into the manifest as a normalized
    // crop edit, re-derive the base from it, and re-run B&W if active. This keeps
    // the manifest the single source of truth for the pipeline base → crop → B&W.
    connect(viewer, &ImageViewer::cropModeChanged, this, [this, viewer](bool cropActive) {
        if (cropActive) {
            // Entering crop: stop any pending render; the crop UI shows the color image.
            m_renderDebounce->stop();
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
        m_comparing = false;
        m_bwPanel->setComparing(false);
        if (hasDisplayEdits())
            applyRender();
        else
            showBase();
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

    // Reset transient view state for the new image.
    m_comparing = false;
    m_renderDebounce->stop();
    m_lastRenderPixmap = {};

    rebuildOriented();

    // Re-arm the viewer's crop selection from the saved crop (oriented pixels).
    if (const CropEdit *c = m_manifest.crop())
        m_viewer->setCropRect(QRectF(CropEdit::toPixels(c->rect(), m_orientedImage.size())));

    rebuildBase();

    // Load any saved adjust/color/B&W settings into the panels without
    // re-triggering a render (the render is scheduled once, below).
    {
        QSignalBlocker block(m_adjustPanel);
        m_adjustPanel->setAdjustParams(m_manifest.adjust() ? m_manifest.adjust()->params() : AdjustParams{});
        m_adjustPanel->setColorParams(m_manifest.color() ? m_manifest.color()->params() : ColorParams{});
    }
    if (m_manifest.bw()) {
        QSignalBlocker block(m_bwPanel);
        m_bwPanel->setParams(m_manifest.bw()->params());
    }
    m_bwPanel->setComparing(false);

    // New image: dismiss any open panels so they reopen against the new state.
    m_adjustPanel->hide();
    m_bwPanel->hide();

    // Show the color base first (so we never flash the unedited disk image while a
    // render runs), then render the post-crop edits if there are any.
    showBase();
    if (hasDisplayEdits())
        applyRender();
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
        if (m_adjustPanel && m_adjustPanel->isVisible()) {
            m_adjustPanel->hide();
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
    if (m_adjustPanel && m_adjustPanel->isVisible()) {
        int y = height() - m_adjustPanel->sizeHint().height() - 10;
        m_adjustPanel->move(10, y);
    }
}

// ---------------------------------------------------------------------------
// Live display pipeline (shared by adjust, color, and B&W)
// ---------------------------------------------------------------------------
bool MainWindow::hasDisplayEdits() const {
    return m_manifest.adjust() != nullptr
        || m_manifest.color() != nullptr
        || m_manifest.bw() != nullptr;
}

void MainWindow::scheduleRender() {
    if (m_baseImage.isNull()) return;
    // Comparing or no post-crop edits → just show the color base; nothing to render.
    if (m_comparing || !hasDisplayEdits()) {
        showBase();
        return;
    }
    if (m_viewer->cropMode()) return;   // crop UI owns the display while active
    m_renderDebounce->start();
}

void MainWindow::applyRender() {
    if (m_baseImage.isNull()) return;
    if (!hasDisplayEdits()) { showBase(); return; }
    if (m_renderWatcher->isRunning()) {
        m_renderDebounce->start();
        return;
    }
    // Apply the post-crop edits off the GUI thread via the manifest's own render
    // path. The manifest is value-semantic, so the copy keeps the worker
    // independent of any later edit changes.
    QImage base = m_baseImage;
    EditManifest snapshot = m_manifest;
    m_renderWatcher->setFuture(
        QtConcurrent::run([base, snapshot]() { return snapshot.renderAfterCrop(base); }));
}

// ---------------------------------------------------------------------------
// Adjustments / color panel
// ---------------------------------------------------------------------------
void MainWindow::onAdjustPanelRequested() {
    if (m_adjustPanel->isVisible()) {
        m_adjustPanel->hide();
        return;
    }
    if (m_baseImage.isNull()) return;

    // Reflect the manifest's current settings without re-triggering a render.
    {
        QSignalBlocker block(m_adjustPanel);
        m_adjustPanel->setAdjustParams(m_manifest.adjust() ? m_manifest.adjust()->params() : AdjustParams{});
        m_adjustPanel->setColorParams(m_manifest.color() ? m_manifest.color()->params() : ColorParams{});
    }

    int y = height() - m_adjustPanel->sizeHint().height() - 10;
    m_adjustPanel->move(10, y);
    m_adjustPanel->show();
    m_adjustPanel->raise();
    m_adjustPanel->setFocus();
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
        scheduleRender();
    }

    m_bwPanel->setComparing(m_comparing);
    int x = 10;
    int y = height() - m_bwPanel->sizeHint().height() - 10;
    m_bwPanel->move(x, y);
    m_bwPanel->show();
    m_bwPanel->raise();
    m_bwPanel->setFocus();
}

void MainWindow::toggleCompare() {
    if (!hasDisplayEdits() || m_baseImage.isNull()) return;
    m_comparing = !m_comparing;
    m_bwPanel->setComparing(m_comparing);
    if (m_comparing)
        showBase();
    else if (!m_lastRenderPixmap.isNull())
        m_viewer->setDisplayPixmap(m_lastRenderPixmap);
    else
        scheduleRender();
}

void MainWindow::deactivateBw() {
    const bool wasActive = bwActive();
    m_manifest.removeBw();
    if (wasActive)
        persistManifest();

    // Re-render whatever post-crop edits remain (adjust/color), or restore the
    // color base when B&W was the only one.
    m_comparing = false;
    scheduleRender();

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

    m_comparing = false;
    m_bwPanel->setComparing(false);
    if (hasDisplayEdits())
        applyRender();
    else
        showBase();
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
