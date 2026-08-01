#pragma once
#include "EditManifest.h"
#include "ExifReader.h"
#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QString>

class ImageViewer;
class QTimer;
class QWidget;

// ---------------------------------------------------------------------------
// ImagePane — all per-image state: one ImageViewer, that image's EditManifest,
// the derived pixmap buffers, and the off-thread display-render pipeline. In
// single-image mode MainWindow owns exactly one pane; in side-by-side compare
// mode it owns two, each fully independent so edits apply only to its own image.
//
// The pane owns the canonical pipeline (disk → orientation → rotate → crop →
// post-crop render → display); MainWindow drives it (mutating the manifest, then
// asking the pane to re-derive). The shared edit panels/overlays stay in
// MainWindow and always act on the *focused* pane.
// ---------------------------------------------------------------------------
class ImagePane : public QObject {
    Q_OBJECT

public:
    explicit ImagePane(const QString &imagePath, QWidget *viewerParent);

    ImageViewer *viewer() const { return m_viewer; }
    QString path() const;

    EditManifest &manifest() { return m_manifest; }
    const EditManifest &manifest() const { return m_manifest; }

    const QImage &diskImage() const { return m_diskImage; }
    const QImage &uprightImage() const { return m_uprightImage; }
    const QImage &orientedImage() const { return m_orientedImage; }
    const QImage &baseImage() const { return m_baseImage; }

    bool comparing() const { return m_comparing; }
    void setComparing(bool c) { m_comparing = c; }
    QPixmap lastRenderPixmap() const { return m_lastRenderPixmap; }

    // The image currently on screen for this pane, as a QImage — what the
    // histogram measures. Mirrors the display logic: the crop UI shows the full
    // oriented original, comparing shows the colour base, and otherwise it is the
    // rendered result (falling back to the base until the first render lands).
    QImage displayImage() const;

    bool bwActive() const { return m_manifest.bw() != nullptr; }
    // True when any post-crop edit (adjust / color / B&W) is applied, so the
    // displayed image must be re-rendered off the base rather than shown as-is.
    bool hasDisplayEdits() const;

    // Re-derive the pixmap buffers from the manifest via the edit interface.
    void rebuildOriented();   // m_uprightImage, then m_orientedImage (free rotation)
    void rebuildBase();       // m_baseImage = crop edit applied to oriented
    void showBase();          // push the (color) base image to the viewer
    void persistManifest();   // save the manifest for this image's path

    // The single live-display pipeline shared by adjust, color, and B&W: derive
    // the shown image from m_baseImage by applying the post-crop edits off-thread.
    void scheduleRender();    // debounce, or show base directly when nothing applies
    void applyRender();       // launch the off-thread render of the post-crop edits

    // Display-only metadata derived from this pane's manifest and buffers
    // (orientation/crop/B&W summary plus original and current dimensions).
    ExifReader::ExifData stateData() const;

signals:
    // The image was (re)loaded from disk and its buffers rebuilt — the focused
    // pane's edit panels should resync to the new state.
    void reloaded();

private:
    // Capture the freshly-loaded disk image and (re-)apply the saved manifest.
    void reloadFromDisk();

    ImageViewer *m_viewer = nullptr;

    // The single source of truth for what edits are applied to this image.
    EditManifest m_manifest;

    // Pixmap buffers, all derived from the manifest applied to the disk image.
    QImage m_diskImage;      // image exactly as loaded from disk; never edited
    QImage m_uprightImage;   // disk image with the orientation edit applied (overlay base)
    QImage m_orientedImage;  // upright image with the free rotation applied (crop base)
    QImage m_baseImage;      // oriented image with the crop edit applied (B&W source)

    QImage                  m_lastRenderImage;   // most recent render, pre-conversion
    QPixmap                 m_lastRenderPixmap;  // most recent rendered display image
    bool                    m_comparing = false; // showing the original color base
    QFutureWatcher<QImage> *m_renderWatcher = nullptr;
    QTimer                 *m_renderDebounce = nullptr;
};
