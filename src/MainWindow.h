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

    // Single entry point for opening one image path as "the" image (replacing
    // whatever is currently shown, collapsing compare mode first). Used by every
    // way of opening a file that isn't the deliberate two-image Shift+O compare
    // (initial launch, File > Open, the Tab folder browser, and arrow-key
    // navigation) so auto-pair detection (see below) always applies. Exposed for
    // tests.
    void openImage(const QString &path);

    // True while the current compare-mode session is one that auto-pairing opened
    // (both panes hold a detected "_pair" pair) rather than a manual Shift+O
    // compare. Exposed for tests.
    bool autoPaired() const { return m_autoPaired; }

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

    // --- Auto-pair ("_pair" file names) -----------------------------------
    // Checks the single pane at m_panes[0] for a "_pair" partner and, if one
    // exists, expands into an auto-paired compare (lexicographically-first path
    // in the left pane). Precondition: m_panes holds exactly one pane.
    void applyAutoPairIfNeeded(const QString &path);
    // Puts a and b (in whichever order is lexicographically correct) into a
    // two-pane compare: reloads the sole existing pane (m_panes[0]) to the left
    // path if needed, then opens the right path beside it. Precondition: m_panes
    // holds exactly one pane.
    void arrangePair(const QString &a, const QString &b);
    // Folder-relative next/prev file, mirroring ImageViewer::navigate() (used
    // both for plain arrow-key navigation and for stepping past an auto-paired
    // pair as a unit).
    QString adjacentImagePath(const QString &path, int delta) const;
    // Left/Right arrow-key handling for the focused pane. In an auto-paired
    // compare, this steps past the whole pair and returns to single mode; in a
    // manual compare it steps only the focused image in place (today's
    // behavior); in single mode it steps to the next file, applying auto-pair
    // detection to the result.
    void navigateFocused(int delta);

    // Layout: a container holding the compare tab strip above a horizontal row of
    // pane viewers (one viewer in single mode, two side by side in compare mode).
    QWidget       *m_container      = nullptr;
    QHBoxLayout   *m_viewersLayout  = nullptr;
    CompareTabBar *m_tabBar         = nullptr;

    QList<ImagePane *> m_panes;
    int  m_focus        = 0;
    bool m_syncingViews = false;   // re-entrancy guard for view synchronization
    bool m_autoPaired   = false;   // compare mode was entered by "_pair" auto-detection

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
