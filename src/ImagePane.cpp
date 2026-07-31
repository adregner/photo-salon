#include "ImagePane.h"
#include "ImageViewer.h"
#include <QPixmap>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

ImagePane::ImagePane(const QString &imagePath, QWidget *viewerParent)
    : QObject(nullptr)
{
    m_viewer = new ImageViewer(imagePath, viewerParent);
    // Tie this pane's lifetime to its viewer, so deleting the viewer (when a
    // compare tab is closed) cleans up the pane too.
    setParent(m_viewer);

    // One debounce + one off-thread worker drive this image's whole post-crop
    // pipeline (adjust → color → B&W), so the panels never race on the display.
    m_renderDebounce = new QTimer(this);
    m_renderDebounce->setSingleShot(true);
    m_renderDebounce->setInterval(50);
    connect(m_renderDebounce, &QTimer::timeout, this, &ImagePane::applyRender);

    m_renderWatcher = new QFutureWatcher<QImage>(this);
    connect(m_renderWatcher, &QFutureWatcher<QImage>::finished, this, [this]() {
        m_lastRenderImage  = m_renderWatcher->result();
        m_lastRenderPixmap = QPixmap::fromImage(m_lastRenderImage);
        // Don't clobber the crop UI, show a stale result, or override compare.
        if (hasDisplayEdits() && !m_comparing && !m_viewer->cropMode())
            m_viewer->setDisplayPixmap(m_lastRenderPixmap);
    });

    // A new image from disk (navigation, etc.) resets every buffer and re-applies
    // that image's saved manifest.
    connect(m_viewer, &ImageViewer::imagePathChanged, this, [this](const QString &) {
        reloadFromDisk();
    });

    // ImageViewer loaded the image in its constructor, before the signal above was
    // connected, so capture that first image and apply its manifest explicitly.
    reloadFromDisk();
}

QString ImagePane::path() const {
    return m_viewer ? m_viewer->currentPath() : QString{};
}

void ImagePane::reloadFromDisk() {
    m_diskImage = m_viewer->pixmap().toImage();
    m_manifest  = EditManifest::loadFor(path());

    // Reset transient view state for the new image.
    m_comparing = false;
    m_renderDebounce->stop();
    m_lastRenderImage  = {};
    m_lastRenderPixmap = {};

    rebuildOriented();

    // Re-arm the viewer's crop selection from the saved crop (oriented pixels).
    if (const CropEdit *c = m_manifest.crop())
        m_viewer->setCropRect(QRectF(CropEdit::toPixels(c->rect(), m_orientedImage.size())));

    rebuildBase();

    // Show the color base first (so we never flash the unedited disk image while a
    // render runs), then render the post-crop edits if there are any.
    showBase();
    if (hasDisplayEdits())
        applyRender();

    emit reloaded();
}

void ImagePane::rebuildOriented() {
    const OrientationEdit *o = m_manifest.orientation();
    m_orientedImage = o ? o->apply(m_diskImage) : m_diskImage;
    // The crop UI always works against the full oriented original.
    m_viewer->setBasePixmapForCrop(QPixmap::fromImage(m_orientedImage));
}

void ImagePane::rebuildBase() {
    const CropEdit *c = m_manifest.crop();
    m_baseImage = c ? c->apply(m_orientedImage) : m_orientedImage;
}

void ImagePane::showBase() {
    if (!m_baseImage.isNull())
        m_viewer->setDisplayPixmap(QPixmap::fromImage(m_baseImage));
}

void ImagePane::persistManifest() {
    m_manifest.saveFor(path());
}

QImage ImagePane::displayImage() const {
    // The crop UI shows the full oriented original, not the cropped base.
    if (m_viewer && m_viewer->cropMode() && !m_orientedImage.isNull())
        return m_orientedImage;
    if (m_comparing || !hasDisplayEdits())
        return m_baseImage;
    return m_lastRenderImage.isNull() ? m_baseImage : m_lastRenderImage;
}

bool ImagePane::hasDisplayEdits() const {
    return m_manifest.adjust() != nullptr
        || m_manifest.color() != nullptr
        || m_manifest.bw() != nullptr;
}

void ImagePane::scheduleRender() {
    if (m_baseImage.isNull()) return;
    // Comparing or no post-crop edits → just show the color base; nothing to render.
    if (m_comparing || !hasDisplayEdits()) {
        showBase();
        return;
    }
    if (m_viewer->cropMode()) return;   // crop UI owns the display while active
    m_renderDebounce->start();
}

void ImagePane::applyRender() {
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

ExifReader::ExifData ImagePane::stateData() const {
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
