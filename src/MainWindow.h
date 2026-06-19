#pragma once
#include "EditManifest.h"
#include "ExifReader.h"
#include <QFutureWatcher>
#include <QImage>
#include <QMainWindow>
#include <QString>
#include <QTransform>
#include <Qt>

class BackgroundColorPicker;
class BwPanel;
class ExifOverlay;
class HelpOverlay;
class ExitOverlay;
class ImageViewer;
class QResizeEvent;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QString &imagePath, QWidget *parent = nullptr);

public slots:
    void toggleFullscreen();

public:
    // Display-only metadata derived from the current edit manifest (orientation/
    // crop/B&W summary plus original and current dimensions). Merged into the EXIF
    // data shown by the metadata overlay. Public for tests.
    ExifReader::ExifData imageStateData() const;

    // The manifest is the canonical record of all applied edits. Exposed for tests.
    const EditManifest &manifest() const { return m_manifest; }

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    enum class OrientationStep { RotateCW, FlipH, FlipV };

    // Capture the freshly-loaded disk image and (re-)apply the saved manifest.
    void onImageLoaded(const QString &path);
    // Re-derive the pixmap buffers from the manifest via the edit interface.
    void rebuildOriented();   // m_orientedImage = orientation edit applied to disk
    void rebuildBase();       // m_baseImage = crop edit applied to oriented
    void showBase();          // push the (color) base image to the viewer
    void persistManifest();   // save the manifest for the current image path
    bool bwActive() const { return m_manifest.bw() != nullptr; }

    void onBwPanelRequested();
    void applyBwConversion();
    void toggleBwCompare();
    void deactivateBw();
    void applyOrientationStep(OrientationStep step);
    void exitApplication();
    void openFile();
    void updateExternalEditorName();

    ImageViewer *m_viewer = nullptr;
    HelpOverlay *m_helpOverlay = nullptr;
    ExifOverlay *m_exifOverlay = nullptr;
    ExitOverlay *m_exitOverlay = nullptr;
    QWidget     *m_idleOverlay = nullptr;
    BackgroundColorPicker *m_colorPicker = nullptr;
    Qt::WindowStates m_windowStateBeforeFullscreen = Qt::WindowNoState;
    bool m_forwardingKeyEvent = false;

    // The single source of truth for what edits are applied, and in what order.
    EditManifest m_manifest;

    // Pixmap buffers, all derived from the manifest applied to the disk image:
    QImage m_diskImage;      // image exactly as loaded from disk; never edited
    QImage m_orientedImage;  // disk image with the orientation edit applied (crop base)
    QImage m_baseImage;      // oriented image with the crop edit applied (B&W source)

    BwPanel                *m_bwPanel       = nullptr;
    QImage                  m_lastBwImage;
    QPixmap                 m_lastBwPixmap;
    bool                    m_bwComparing   = false;
    QFutureWatcher<QImage> *m_bwWatcher     = nullptr;
    QTimer                 *m_bwDebounce    = nullptr;
    QTimer                 *m_exitDebounce  = nullptr;
};
