#pragma once
#include "EditManifest.h"
#include "ExifReader.h"
#include <QImage>
#include <QList>
#include <QMainWindow>
#include <QString>
#include <Qt>

class AdjustPanel;
class BackgroundColorPicker;
class BwPanel;
class CompareTabBar;
class ExifOverlay;
class HelpOverlay;
class HistogramOverlay;
class ExitOverlay;
class ImagePane;
class ImageViewer;
class RotatePanel;
class QHBoxLayout;
class QResizeEvent;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QString &imagePath, QWidget *parent = nullptr);

public slots:
    void toggleFullscreen();

public:
    // Display-only metadata derived from the focused pane's edit manifest. Merged
    // into the EXIF data shown by the metadata overlay. Public for tests.
    ExifReader::ExifData imageStateData() const;

    // The focused pane's manifest — the canonical record of its applied edits.
    // Exposed for tests.
    const EditManifest &manifest() const;

    // The currently focused image's viewer (the one keyboard shortcuts act on).
    // Public for tests.
    ImageViewer *activeViewer() const;

    // The rotate-mode panel, which owns the two lossless quarter-turn buttons.
    // Public so the quarter turns can be driven in tests.
    RotatePanel *rotatePanel() const { return m_rotatePanel; }

    // True while two images are open side by side.
    bool compareMode() const { return m_panes.size() > 1; }

    // Open a second image for side-by-side compare. The Shift+O shortcut routes
    // through the native open dialog before calling this; exposed directly so the
    // comparison flow can be driven in tests.
    void openComparison(const QString &path);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    enum class OrientationStep { RotateCW, RotateCCW, FlipH, FlipV };

    ImagePane *focused() const;

    // --- Pane / compare management ---------------------------------------
    ImagePane *createPane(const QString &path);   // build + wire a pane (not laid out)
    void wirePane(ImagePane *pane);               // connect that pane's viewer signals
    void openSecondImage();                       // Shift+O: enter side-by-side compare
    void closePane(int index);                    // ✕: back to single-image mode
    void setFocusIndex(int index);                // make a pane the focused one
    void updateTabBar();                          // rebuild / hide the compare tab strip
    void updateWindowTitle();                     // title from the focused image
    void syncPanelsToFocused();                   // reflect focused manifest in the panels
    void syncView(ImagePane *src, ImagePane *dst);// copy relative zoom/pan src → dst
    void syncViewFrom(ImagePane *src);            // src changed → mirror onto the other

    // --- Focused-pane action handlers ------------------------------------
    void onAdjustPanelRequested();
    void onBwPanelRequested();
    void toggleCompare();
    void deactivateBw();
    void onOverlayModeChanged(ImagePane *pane);   // crop/rotate opened or closed
    void commitOverlay(ImagePane *pane);          // fold the overlay into the manifest
    void applyOrientationStep(OrientationStep step);
    void toggleExif();
    void toggleHistogram();      // G: show/hide the histogram panel
    void refreshHistogram();     // recompute from the focused pane's displayed image
    void positionHistogram();    // anchor the panel to the top-right corner
    void showColorPicker();
    void saveFocused();
    void openExternalFocused(bool useOriginal);
    void openExternalPickerFocused();
    void folderBrowseFocused();
    void requestExit();
    void exitApplication();
    void openFile();
    void updateExternalEditorName();

    // Layout: a container holding the compare tab strip above a horizontal row of
    // pane viewers (one viewer in single mode, two side by side in compare mode).
    QWidget       *m_container      = nullptr;
    QHBoxLayout   *m_viewersLayout  = nullptr;
    CompareTabBar *m_tabBar         = nullptr;

    QList<ImagePane *> m_panes;
    int  m_focus        = 0;
    bool m_syncingViews = false;   // re-entrancy guard for view synchronization

    HelpOverlay *m_helpOverlay = nullptr;
    ExifOverlay *m_exifOverlay = nullptr;
    HistogramOverlay *m_histogramOverlay = nullptr;
    ExitOverlay *m_exitOverlay = nullptr;
    QWidget     *m_idleOverlay = nullptr;
    BackgroundColorPicker *m_colorPicker = nullptr;
    Qt::WindowStates m_windowStateBeforeFullscreen = Qt::WindowNoState;
    bool m_forwardingKeyEvent = false;
    int  m_backgroundGrey = 0;     // shared across panes so both images match

    BwPanel     *m_bwPanel     = nullptr;
    AdjustPanel *m_adjustPanel = nullptr;
    RotatePanel *m_rotatePanel = nullptr;

    QTimer *m_exitDebounce = nullptr;
};
