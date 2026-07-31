#include "MainWindow.h"
#include "AdjustPanel.h"
#include "BackgroundColorPicker.h"
#include "BwPanel.h"
#include "CompareTabBar.h"
#include "Const.h"
#include "ExifOverlay.h"
#include "ExifReader.h"
#include "ExternalLauncher.h"
#include "HelpOverlay.h"
#include "ExitOverlay.h"
#include "ImageAdjust.h"
#include "ImagePane.h"
#include "ImageFormats.h"
#include "ImageViewer.h"
#include "OpenDialog.h"
#include "RotateGeometry.h"
#include "RotatePanel.h"
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
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
    // Central layout: the compare tab strip stacked above a horizontal row of
    // pane viewers. In single-image mode the strip is hidden and the row holds
    // one viewer; in compare mode the strip shows two tabs over two viewers.
    m_container = new QWidget(this);
    auto *outer = new QVBoxLayout(m_container);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_tabBar = new CompareTabBar(m_container);
    m_tabBar->hide();
    connect(m_tabBar, &CompareTabBar::tabSelected, this, &MainWindow::setFocusIndex);
    connect(m_tabBar, &CompareTabBar::tabClosed,   this, &MainWindow::closePane);
    outer->addWidget(m_tabBar);

    auto *row = new QWidget(m_container);
    m_viewersLayout = new QHBoxLayout(row);
    m_viewersLayout->setContentsMargins(0, 0, 0, 0);
    m_viewersLayout->setSpacing(2);
    outer->addWidget(row, 1);

    setCentralWidget(m_container);
    qApp->installEventFilter(this);

    // Shared overlays and edit panels are created up front (before any pane), so
    // a pane's signals can be wired to them as soon as it exists. They always act
    // on the focused pane.
    m_helpOverlay = new HelpOverlay(this);
    m_helpOverlay->raise();

    m_exifOverlay = new ExifOverlay(this);
    m_exifOverlay->raise();

    m_colorPicker = new BackgroundColorPicker(this);
    m_colorPicker->hide();
    connect(m_colorPicker, &BackgroundColorPicker::greyChanged, this, [this](int v) {
        m_backgroundGrey = v;
        for (ImagePane *p : m_panes)
            p->viewer()->setBackgroundGrey(v);
    });

    // One debounce + one off-thread worker live inside each ImagePane, so the two
    // images never race each other on the display.
    m_adjustPanel = new AdjustPanel(this);
    m_adjustPanel->hide();

    // Light/tone and colour changes flow into the focused image's own manifest
    // edits (created only while non-neutral) and are persisted, then a render is
    // scheduled — for that image alone.
    connect(m_adjustPanel, &AdjustPanel::adjustParamsChanged, this, [this](const AdjustParams &p) {
        ImagePane *pane = focused();
        if (ImageAdjust::isNeutral(p)) pane->manifest().removeAdjust();
        else                           pane->manifest().ensureAdjust().setParams(p);
        pane->persistManifest();
        pane->scheduleRender();
    });
    connect(m_adjustPanel, &AdjustPanel::colorParamsChanged, this, [this](const ColorParams &p) {
        ImagePane *pane = focused();
        if (ImageAdjust::isNeutral(p)) pane->manifest().removeColor();
        else                           pane->manifest().ensureColor().setParams(p);
        pane->persistManifest();
        pane->scheduleRender();
    });

    // The rotate panel is the visible face of rotate mode: MainWindow shows and
    // hides it with the mode rather than letting it dismiss itself.
    m_rotatePanel = new RotatePanel(this);
    m_rotatePanel->hide();

    connect(m_rotatePanel, &RotatePanel::angleChanged, this, [this](double degrees) {
        ImageViewer *viewer = activeViewer();
        if (viewer->rotateMode())
            viewer->setRotateAngle(degrees);
    });
    connect(m_rotatePanel, &RotatePanel::rotateLeftRequested,  this,
            [this] { applyOrientationStep(OrientationStep::RotateCCW); });
    connect(m_rotatePanel, &RotatePanel::rotateRightRequested, this,
            [this] { applyOrientationStep(OrientationStep::RotateCW); });

    m_bwPanel = new BwPanel(this);
    m_bwPanel->hide();

    // Slider/look changes flow straight into the focused image's B&W edit and are
    // persisted, then a (debounced) re-render is scheduled.
    connect(m_bwPanel, &BwPanel::paramsChanged, this, [this](const BwParams &p) {
        ImagePane *pane = focused();
        if (pane->bwActive() && !pane->comparing()) {
            pane->manifest().bw()->setParams(p);
            pane->persistManifest();
            pane->scheduleRender();
        }
    });

    connect(m_bwPanel, &BwPanel::compareToggled, this, [this](bool showOriginal) {
        ImagePane *pane = focused();
        pane->setComparing(showOriginal);
        m_bwPanel->setComparing(showOriginal);
        if (!pane->hasDisplayEdits() || pane->baseImage().isNull()) return;
        if (showOriginal)
            pane->showBase();
        else if (!pane->lastRenderPixmap().isNull())
            pane->viewer()->setDisplayPixmap(pane->lastRenderPixmap());
        else
            pane->scheduleRender();
    });

    connect(m_bwPanel, &BwPanel::resetToColorRequested, this, &MainWindow::deactivateBw);

    m_exitOverlay = new ExitOverlay(this);
    m_exitOverlay->resize(size());
    m_exitOverlay->raise();

    m_exitDebounce = new QTimer(this);
    m_exitDebounce->setSingleShot(true);
    m_exitDebounce->setInterval(EXIT_DEBOUNCE);
    connect(m_exitDebounce, &QTimer::timeout, m_exitOverlay, &ExitOverlay::hide);

    // Now build the first image pane and wire its viewer to the shared widgets.
    ImagePane *pane = createPane(imagePath);
    m_panes.append(pane);
    m_viewersLayout->addWidget(pane->viewer(), 1);

    QSize imageSize = pane->viewer()->nativeImageSize();
    if (!imageSize.isEmpty()) {
        QSize available = screen()->availableGeometry().size();
        resize(imageSize.boundedTo(available));   // resizeEvent sizes the overlays
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
        m_helpOverlay->raise();

        pane->viewer()->setHelpVisible(true);
        QTimer::singleShot(0, this, &MainWindow::openFile);
    }

    updateExternalEditorName();

    // Reflect the first image's initial state into the panels and title.
    syncPanelsToFocused();
    updateWindowTitle();
}

// ---------------------------------------------------------------------------
// Pane creation / wiring
// ---------------------------------------------------------------------------
ImagePane *MainWindow::createPane(const QString &path) {
    auto *pane = new ImagePane(path, m_container);
    pane->viewer()->setBackgroundGrey(m_backgroundGrey);
    wirePane(pane);
    return pane;
}

void MainWindow::wirePane(ImagePane *pane) {
    ImageViewer *v = pane->viewer();

    // Clicking or scrolling a pane focuses it; all shortcuts then act on it.
    connect(v, &ImageViewer::focusRequested, this, [this, pane] {
        setFocusIndex(m_panes.indexOf(pane));
    });

    connect(v, &ImageViewer::imagePathChanged, this, [this, pane](const QString &path) {
        if (pane == focused()) {
            if (!path.isEmpty() && m_idleOverlay)
                m_idleOverlay->hide();
            updateWindowTitle();
            if (m_exifOverlay && m_exifOverlay->isVisible())
                m_exifOverlay->hide();
        }
        updateTabBar();
    });

    // After a pane reloads an image, resync the panels (if it's focused) and
    // dismiss any open ones so they reopen against the new state.
    connect(pane, &ImagePane::reloaded, this, [this, pane] {
        if (pane == focused()) {
            syncPanelsToFocused();
            m_adjustPanel->hide();
            m_bwPanel->hide();
            m_rotatePanel->hide();   // loading resets the viewer's overlay mode
        }
    });

    connect(v, &ImageViewer::viewChanged, this, [this, pane] { syncViewFrom(pane); });

    connect(v, &ImageViewer::helpVisibilityChanged, m_helpOverlay, &QWidget::setVisible);
    connect(v, &ImageViewer::exifRequested,         this, &MainWindow::toggleExif);
    connect(v, &ImageViewer::fullscreenToggleRequested, this, &MainWindow::toggleFullscreen);
    connect(v, &ImageViewer::backgroundPickerRequested, this, &MainWindow::showColorPicker);
    connect(v, &ImageViewer::saveRequested,         this, &MainWindow::saveFocused);
    connect(v, &ImageViewer::openExternalRequested, this, &MainWindow::openExternalFocused);
    connect(v, &ImageViewer::openExternalPickerRequested, this, &MainWindow::openExternalPickerFocused);
    connect(v, &ImageViewer::adjustPanelRequested,  this, &MainWindow::onAdjustPanelRequested);
    connect(v, &ImageViewer::bwPanelRequested,      this, &MainWindow::onBwPanelRequested);
    connect(v, &ImageViewer::bwCompareRequested,    this, &MainWindow::toggleCompare);
    // Crop and rotate are two faces of one overlay: either signal means the
    // overlay's state changed, and only closing it applies anything.
    connect(v, &ImageViewer::cropModeChanged,   this, [this, pane] { onOverlayModeChanged(pane); });
    connect(v, &ImageViewer::rotateModeChanged, this, [this, pane] { onOverlayModeChanged(pane); });
    connect(v, &ImageViewer::rotateAngleChanged, this, [this, pane](double degrees) {
        if (pane == focused()) {
            QSignalBlocker block(m_rotatePanel);
            m_rotatePanel->setAngle(degrees);
        }
    });
    connect(v, &ImageViewer::folderBrowseRequested, this, &MainWindow::folderBrowseFocused);
    connect(v, &ImageViewer::flipHorizontalRequested, this, [this] { applyOrientationStep(OrientationStep::FlipH); });
    connect(v, &ImageViewer::flipVerticalRequested,   this, [this] { applyOrientationStep(OrientationStep::FlipV); });
    connect(v, &ImageViewer::exitRequested,     this, &MainWindow::requestExit);
    connect(v, &ImageViewer::openFileRequested, this, &MainWindow::openFile);
    connect(v, &ImageViewer::compareOpenRequested, this, &MainWindow::openSecondImage);
}

ImagePane *MainWindow::focused() const {
    return m_panes.at(m_focus);
}

ImageViewer *MainWindow::activeViewer() const {
    return focused()->viewer();
}

const EditManifest &MainWindow::manifest() const {
    return focused()->manifest();
}

ExifReader::ExifData MainWindow::imageStateData() const {
    return focused()->stateData();
}

// ---------------------------------------------------------------------------
// Side-by-side compare: open / close / focus
// ---------------------------------------------------------------------------
void MainWindow::openSecondImage() {
    if (compareMode()) return;                 // already two images open
    if (focused()->path().isEmpty()) return;   // nothing to compare against

    QString startDir = QFileInfo(focused()->path()).absolutePath();
    QString selected = showOpenDialog(this, startDir);
    if (selected.isEmpty()) return;
    QString resolved = resolveImagePath(selected);
    if (resolved.isEmpty()) return;
    openComparison(resolved);
}

void MainWindow::openComparison(const QString &path) {
    if (compareMode()) return;
    if (path.isEmpty() || focused()->path().isEmpty()) return;

    ImagePane *second = createPane(path);
    m_panes.append(second);
    m_viewersLayout->addWidget(second->viewer(), 1);

    // Both images start fit to their half of the window — naturally aligned (both
    // at "fit", centred) and both re-fit together on resize. From here, any zoom
    // or pan of the focused image is mirrored onto the other relative to its size.
    updateTabBar();   // build both tabs before focusing one
    setFocusIndex(m_panes.indexOf(second));

    // The new viewer only learns its real size after the layout settles, so fit
    // it once that has happened (its fit at construction was against a stale size).
    ImageViewer *bv = second->viewer();
    QTimer::singleShot(0, bv, [bv] { bv->fitToWindow(); });
}

void MainWindow::closePane(int index) {
    if (!compareMode() || index < 0 || index >= m_panes.size()) return;

    ImagePane *closing = m_panes.at(index);
    ImageViewer *v = closing->viewer();
    m_viewersLayout->removeWidget(v);
    m_panes.removeAt(index);
    delete v;          // deletes the viewer and, as its child, the pane

    m_focus = 0;       // the lone survivor becomes the focused pane
    updateTabBar();    // single image again → strip hides
    syncPanelsToFocused();
    updateWindowTitle();
    focused()->viewer()->setFocus();
}

void MainWindow::setFocusIndex(int index) {
    if (index < 0 || index >= m_panes.size()) return;
    if (index == m_focus) return;
    m_focus = index;
    if (compareMode())
        m_tabBar->setFocusedIndex(m_focus);   // restyle only; don't rebuild tabs
    syncPanelsToFocused();
    updateWindowTitle();
    // The newly focused image becomes the one that drives view synchronization;
    // the two are already aligned, so there is nothing to mirror right now.
    focused()->viewer()->setFocus();
}

void MainWindow::updateTabBar() {
    if (!compareMode()) {
        m_tabBar->hide();
        return;
    }
    QStringList names;
    for (ImagePane *p : m_panes) {
        const QString path = p->path();
        names << (path.isEmpty() ? QStringLiteral("(none)") : QFileInfo(path).fileName());
    }
    m_tabBar->setTabs(names, m_focus);
    m_tabBar->show();
}

void MainWindow::updateWindowTitle() {
    const QString path = focused()->path();
    if (path.isEmpty())
        setWindowTitle(QStringLiteral("photo-salon"));
    else
        setWindowTitle(QStringLiteral("photo-salon — %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::syncPanelsToFocused() {
    ImagePane *pane = focused();
    {
        QSignalBlocker block(m_adjustPanel);
        m_adjustPanel->setAdjustParams(pane->manifest().adjust() ? pane->manifest().adjust()->params() : AdjustParams{});
        m_adjustPanel->setColorParams(pane->manifest().color() ? pane->manifest().color()->params() : ColorParams{});
    }
    if (pane->manifest().bw()) {
        QSignalBlocker block(m_bwPanel);
        m_bwPanel->setParams(pane->manifest().bw()->params());
    }
    m_bwPanel->setComparing(pane->comparing());

    // Rotate mode belongs to one pane at a time, so its panel follows the focus.
    {
        QSignalBlocker block(m_rotatePanel);
        m_rotatePanel->setAngle(pane->viewer()->rotateAngle());
    }
    if (pane->viewer()->rotateMode()) {
        m_rotatePanel->move(10, height() - m_rotatePanel->sizeHint().height() - 10);
        m_rotatePanel->show();
        m_rotatePanel->raise();
    } else {
        m_rotatePanel->hide();
    }
}

// ---------------------------------------------------------------------------
// View synchronization (relative zoom + pan)
// ---------------------------------------------------------------------------
void MainWindow::syncView(ImagePane *src, ImagePane *dst) {
    if (!src || !dst || src == dst) return;
    dst->viewer()->applyRelativeView(src->viewer()->relativeZoom(),
                                     src->viewer()->relativeCenter());
}

void MainWindow::syncViewFrom(ImagePane *src) {
    if (!compareMode() || m_syncingViews) return;
    if (src != focused()) return;   // only the focused image drives the other
    m_syncingViews = true;
    for (ImagePane *p : m_panes)
        if (p != src)
            syncView(src, p);
    m_syncingViews = false;
}

// ---------------------------------------------------------------------------
// Event routing
// ---------------------------------------------------------------------------
bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() != QEvent::KeyPress)
        return false;
    if (QApplication::activeModalWidget())
        return false;

    auto *ke = static_cast<QKeyEvent *>(event);
    ImageViewer *viewer = activeViewer();

    if (ke->key() == Qt::Key_Escape) {
        if (m_exifOverlay && m_exifOverlay->isVisible()) {
            m_exifOverlay->hide();
            return true;
        }
        if (m_colorPicker && m_colorPicker->isVisible()) {
            m_colorPicker->hide();
            return true;
        }
        if (m_helpOverlay && m_helpOverlay->isVisible() && !viewer->currentPath().isEmpty()) {
            viewer->closeHelp();
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
        if (viewer && viewer->overlayActive()) {
            viewer->closeOverlay();
            return true;
        }
        if (windowState() & Qt::WindowFullScreen) {
            toggleFullscreen();
            return true;
        }
        return false;
    }

    // Tab: the viewport event filter doesn't reliably fire before Qt's focus
    // machinery on macOS, so handle Tab at the app-filter level instead. Forward
    // to the focused viewer widget (not the viewport) so that ImageViewer's
    // focusNextPrevChild override prevents focus traversal and keyPressEvent fires.
    if (ke->key() == Qt::Key_Tab && viewer && !m_forwardingKeyEvent) {
        m_forwardingKeyEvent = true;
        QCoreApplication::sendEvent(viewer, event);
        m_forwardingKeyEvent = false;
        return true;
    }

    // Forward all other key events to the focused viewer when something else has
    // focus (including the non-focused pane's viewer, so shortcuts always act on
    // the focused image). Guard against re-entry: QGraphicsView::keyPressEvent
    // forwards unhandled keys to the scene, which would re-trigger this filter.
    if (viewer && obj != viewer && obj != viewer->viewport()) {
        if (!m_forwardingKeyEvent) {
            m_forwardingKeyEvent = true;
            QCoreApplication::sendEvent(viewer, event);
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
    if (activeViewer()->currentPath().isEmpty())
        activeViewer()->setHelpVisible(true);
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
    if (m_rotatePanel && m_rotatePanel->isVisible()) {
        int y = height() - m_rotatePanel->sizeHint().height() - 10;
        m_rotatePanel->move(10, y);
    }
}

// ---------------------------------------------------------------------------
// Metadata overlay
// ---------------------------------------------------------------------------
void MainWindow::toggleExif() {
    if (m_exifOverlay->isVisible()) {
        m_exifOverlay->hide();
        return;
    }
    ImageViewer *viewer = activeViewer();
    auto data = ExifReader::read(viewer->currentPath());
    const auto state = imageStateData();
    for (auto it = state.cbegin(); it != state.cend(); ++it)
        data.insert(it.key(), it.value());
    m_exifOverlay->setData(data);
    m_exifOverlay->show();
    m_exifOverlay->raise();
}

// ---------------------------------------------------------------------------
// Background colour picker
// ---------------------------------------------------------------------------
void MainWindow::showColorPicker() {
    m_colorPicker->setCurrentValue(m_backgroundGrey);
    int y = height() - m_colorPicker->sizeHint().height() - 10;
    m_colorPicker->move(10, y);
    m_colorPicker->show();
    m_colorPicker->raise();
    m_colorPicker->setFocus();
}

// ---------------------------------------------------------------------------
// Save / external editor (focused image)
// ---------------------------------------------------------------------------
void MainWindow::saveFocused() {
    ImageViewer *viewer = activeViewer();
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
}

void MainWindow::openExternalFocused(bool useOriginal) {
    ImageViewer *viewer = activeViewer();
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
}

void MainWindow::openExternalPickerFocused() {
    QPixmap display = activeViewer()->currentDisplayPixmap();
    if (display.isNull()) return;
    QString tempPath = writeExportForExternalApp(display);
    if (tempPath.isEmpty()) return;
    if (openInExternalApp(tempPath, this, /*forcePick=*/true))
        updateExternalEditorName();
}

// ---------------------------------------------------------------------------
// Folder browse dialog (focused image)
// ---------------------------------------------------------------------------
void MainWindow::folderBrowseFocused() {
    ImageViewer *viewer = activeViewer();
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
}

// ---------------------------------------------------------------------------
// Adjustments / color panel (focused image)
// ---------------------------------------------------------------------------
void MainWindow::onAdjustPanelRequested() {
    if (m_adjustPanel->isVisible()) {
        m_adjustPanel->hide();
        return;
    }
    ImagePane *pane = focused();
    if (pane->baseImage().isNull()) return;

    // Reflect the manifest's current settings without re-triggering a render.
    {
        QSignalBlocker block(m_adjustPanel);
        m_adjustPanel->setAdjustParams(pane->manifest().adjust() ? pane->manifest().adjust()->params() : AdjustParams{});
        m_adjustPanel->setColorParams(pane->manifest().color() ? pane->manifest().color()->params() : ColorParams{});
    }

    int y = height() - m_adjustPanel->sizeHint().height() - 10;
    m_adjustPanel->move(10, y);
    m_adjustPanel->show();
    m_adjustPanel->raise();
    m_adjustPanel->setFocus();
}

// ---------------------------------------------------------------------------
// Black & white (focused image)
// ---------------------------------------------------------------------------
void MainWindow::onBwPanelRequested() {
    if (m_bwPanel->isVisible()) {
        m_bwPanel->hide();
        return;
    }
    ImagePane *pane = focused();

    if (!pane->bwActive()) {
        if (pane->baseImage().isNull()) return;
        // Activating B&W records the panel's current look as a manifest edit.
        pane->manifest().ensureBw().setParams(m_bwPanel->params());
        pane->persistManifest();
        pane->scheduleRender();
    }

    m_bwPanel->setComparing(pane->comparing());
    int x = 10;
    int y = height() - m_bwPanel->sizeHint().height() - 10;
    m_bwPanel->move(x, y);
    m_bwPanel->show();
    m_bwPanel->raise();
    m_bwPanel->setFocus();
}

void MainWindow::toggleCompare() {
    ImagePane *pane = focused();
    if (!pane->hasDisplayEdits() || pane->baseImage().isNull()) return;
    pane->setComparing(!pane->comparing());
    m_bwPanel->setComparing(pane->comparing());
    if (pane->comparing())
        pane->showBase();
    else if (!pane->lastRenderPixmap().isNull())
        pane->viewer()->setDisplayPixmap(pane->lastRenderPixmap());
    else
        pane->scheduleRender();
}

void MainWindow::deactivateBw() {
    ImagePane *pane = focused();
    const bool wasActive = pane->bwActive();
    pane->manifest().removeBw();
    if (wasActive)
        pane->persistManifest();

    // Re-render whatever post-crop edits remain (adjust/color), or restore the
    // color base when B&W was the only one.
    pane->setComparing(false);
    pane->scheduleRender();

    if (m_bwPanel) {
        m_bwPanel->setComparing(false);
        m_bwPanel->hide();
    }
}

// ---------------------------------------------------------------------------
// Crop / rotate overlay (focused image)
// ---------------------------------------------------------------------------
void MainWindow::onOverlayModeChanged(ImagePane *pane) {
    ImageViewer *viewer = pane->viewer();

    // Rotate mode's panel is tied to the mode, and only for the focused pane.
    if (pane == focused()) {
        if (viewer->rotateMode()) {
            QSignalBlocker block(m_rotatePanel);
            m_rotatePanel->setAngle(viewer->rotateAngle());
            m_rotatePanel->move(10, height() - m_rotatePanel->sizeHint().height() - 10);
            m_rotatePanel->show();
            m_rotatePanel->raise();
        } else {
            m_rotatePanel->hide();
        }
    }

    // While either mode is on, the overlay owns the display and nothing is
    // applied — including when switching straight from crop to rotate.
    if (viewer->overlayActive()) return;

    commitOverlay(pane);
}

void MainWindow::commitOverlay(ImagePane *pane) {
    ImageViewer *viewer = pane->viewer();

    // The free angle first: the crop selection is expressed in the rotated
    // image's coordinates, so the rotated buffer has to exist before the
    // selection can be normalized against it.
    RotateEdit &r = pane->manifest().ensureRotate();
    r.setAngle(viewer->rotateAngle());
    if (r.isIdentity())
        pane->manifest().removeRotate();

    const QRectF selection = viewer->cropRect();
    pane->rebuildOriented();

    QRectF sel = selection;
    if (!sel.isValid() || sel.isEmpty())
        sel = QRectF(QPointF(0, 0), QSizeF(pane->orientedImage().size()));
    CropEdit &c = pane->manifest().ensureCrop();
    c.setRect(CropEdit::toNormalized(sel, pane->orientedImage().size()));
    if (c.isFull())
        pane->manifest().removeCrop();

    pane->rebuildBase();
    pane->persistManifest();
    pane->setComparing(false);
    if (pane == focused())
        m_bwPanel->setComparing(false);
    if (pane->hasDisplayEdits())
        pane->applyRender();
    else
        pane->showBase();
}

// ---------------------------------------------------------------------------
// Orientation (focused image)
// ---------------------------------------------------------------------------
void MainWindow::applyOrientationStep(OrientationStep step) {
    ImagePane *pane = focused();
    ImageViewer *viewer = pane->viewer();
    if (pane->diskImage().isNull()) return;

    // A quarter turn is available from inside rotate mode (its two buttons), so
    // it has to work without tearing the overlay down: when one is open the live
    // selection is the authority, otherwise the manifest's is.
    const bool overlay = viewer->overlayActive();

    // Capture the crop selection in the OLD rotated coordinate space before the
    // orientation changes, so it can be re-mapped afterwards.
    const QSize oldRotated = overlay ? viewer->rotatedBoundsRect().size().toSize()
                                     : pane->orientedImage().size();
    const bool hadCrop = overlay || pane->manifest().crop() != nullptr;
    QRectF oldCropPx;
    if (overlay)
        oldCropPx = viewer->cropRect();
    else if (pane->manifest().crop())
        oldCropPx = QRectF(CropEdit::toPixels(pane->manifest().crop()->rect(), pane->orientedImage().size()));

    OrientationEdit &o = pane->manifest().ensureOrientation();
    QTransform incr;
    switch (step) {
    case OrientationStep::RotateCW:  o.rotateClockwise();        incr = QTransform().rotate(90);   break;
    case OrientationStep::RotateCCW: o.rotateCounterClockwise(); incr = QTransform().rotate(-90);  break;
    case OrientationStep::FlipH:     o.flipHorizontal();         incr = QTransform().scale(-1, 1); break;
    case OrientationStep::FlipV:     o.flipVertical();           incr = QTransform().scale(1, -1); break;
    }
    if (o.isIdentity())
        pane->manifest().removeOrientation();

    // Quarter turns commute with a free rotation, so the angle rides along
    // untouched. A flip does not: mirroring the frame reverses which way the
    // image leans, so negate the angle to keep the picture the user is looking
    // at exactly mirrored.
    if (step == OrientationStep::FlipH || step == OrientationStep::FlipV) {
        if (RotateEdit *rot = pane->manifest().rotate()) {
            rot->setAngle(-rot->angle());
            if (rot->isIdentity())
                pane->manifest().removeRotate();
        }
    }

    // Re-derive the oriented image and re-arm the overlay base BEFORE remapping
    // the crop rect: setCropRect() clamps against the (new) bounds, and a
    // 90°/270° rotation swaps width and height, so clamping against the stale
    // bounds would clip the mapped selection.
    pane->rebuildOriented();

    // Remap the crop rect so the same region stays selected, using the same
    // translation Qt bakes into transformed().
    if (hadCrop) {
        QTransform full = QPixmap::trueMatrix(incr, oldRotated.width(), oldRotated.height());
        viewer->setCropRect(full.mapRect(oldCropPx));
        if (!overlay)
            pane->manifest().crop()->setRect(
                CropEdit::toNormalized(viewer->cropRect(), pane->orientedImage().size()));
    }

    pane->persistManifest();

    // With an overlay open the new base is already on screen and the selection
    // is committed when the mode closes; there is nothing to render yet.
    if (overlay) return;

    pane->rebuildBase();
    pane->setComparing(false);
    m_bwPanel->setComparing(false);
    if (pane->hasDisplayEdits())
        pane->applyRender();
    else
        pane->showBase();
}

// ---------------------------------------------------------------------------
// Quit / files
// ---------------------------------------------------------------------------
void MainWindow::requestExit() {
    if (m_exitDebounce->isActive()) {
        exitApplication();
    } else {
        m_exitOverlay->show();
        m_exitOverlay->raise();
        m_exitDebounce->start();
    }
}

void MainWindow::exitApplication() {
    exit(0);
}

void MainWindow::openFile() {
    ImageViewer *viewer = activeViewer();
    QString startDir = viewer->currentPath().isEmpty()
        ? QDir::homePath()
        : QFileInfo(viewer->currentPath()).absolutePath();
    QString selected = showOpenDialog(this, startDir);
    if (selected.isEmpty())
        return;
    QString resolved = resolveImagePath(selected);
    if (!resolved.isEmpty())
        viewer->loadImage(resolved);
}

void MainWindow::updateExternalEditorName() {
    QString path = QSettings().value(QStringLiteral("externalEditor/appPath")).toString();
    m_helpOverlay->setExternalEditorName(
        path.isEmpty() ? QString{} : QFileInfo(path).baseName());
}
