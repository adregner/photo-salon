#pragma once
#include <QCursor>
#include <QGraphicsView>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSize>
#include <QString>

class QGraphicsPixmapItem;
class QGraphicsScene;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QResizeEvent;
class QShowEvent;
class QTimer;
class QWheelEvent;

class ImageViewer : public QGraphicsView {
    Q_OBJECT

public:
    explicit ImageViewer(const QString &imagePath, QWidget *parent = nullptr);

    void loadImage(const QString &path);
    QString currentPath() const { return m_imagePath; }
    QSize nativeImageSize() const { return m_nativeSize; }
    bool helpVisible() const { return m_helpVisible; }
    void setHelpVisible(bool visible);
    void closeHelp();
    QPixmap pixmap() const;
    QPixmap currentDisplayPixmap() const;
    // Re-fit the image to the viewport (as the `0` key does). Useful after a
    // viewer is added to a layout, when its real size is only known post-layout.
    void fitToWindow();
    void setDisplayPixmap(const QPixmap &px);
    void setBasePixmapForCrop(const QPixmap &px);
    void setBackgroundGrey(int value);
    int backgroundGrey() const { return m_backgroundGrey; }

    // --- The selection overlay (crop mode and rotate mode) -----------------
    // Both modes put the same bounding box over the full, uncropped image; they
    // differ only in what dragging does — crop drags the box, rotate turns the
    // image under it. Switching directly between them keeps the box as it is;
    // leaving the overlay altogether is what applies the selection.
    void setCropMode(bool active);
    bool cropMode() const { return m_overlay == OverlayMode::Crop; }
    void setRotateMode(bool active);
    bool rotateMode() const { return m_overlay == OverlayMode::Rotate; }
    bool overlayActive() const { return m_overlay != OverlayMode::None; }
    void closeOverlay() { setOverlayMode(OverlayMode::None); }

    // The selection, in the coordinates of the *rotated* image — the same space
    // the manifest's CropEdit normalizes against.
    void setCropRect(const QRectF &rect);
    QRectF cropRect() const { return m_cropRect; }
    bool overlayNoticeVisible() const { return m_noticeVisible; }

    // Free rotation, in degrees (positive turns the image clockwise). The
    // overlay applies it to the pixmap item, so dragging stays interactive; the
    // committed, full-quality rotation is the manifest's RotateEdit.
    void setRotateAngle(double degrees);
    double rotateAngle() const { return m_rotateAngle; }
    // Bounding box of the rotated image: the coordinate space cropRect() and
    // the overlay live in.
    QRectF rotatedBoundsRect() const;
    // The tilted original's outline inside that box. The selection is always
    // held inside it, so the applied crop never includes a blank corner.
    QPolygonF rotateBounds() const;

    // --- View synchronization (side-by-side compare) ---------------------
    // The scale fitInView() would apply right now (viewport ÷ scene). Used to
    // express zoom relative to "fit", independent of the image's pixel size.
    double fitScale() const;
    // Current zoom as a multiple of fitScale() (1.0 == fit to window).
    double relativeZoom() const;
    // Viewport centre as a fraction (0..1) of the image, so the same relative
    // pixel can be centred in another, differently-sized image.
    QPointF relativeCenter() const;
    // Mirror another viewer's relative zoom + centre onto this one.
    void applyRelativeView(double relZoom, const QPointF &relCenter);

signals:
    void helpVisibilityChanged(bool visible);
    // Zoom or pan changed (drives side-by-side view synchronization).
    void viewChanged();
    // The user interacted with this viewer (click/wheel) — request input focus.
    void focusRequested();
    // Shift+O — open a second image to compare side by side.
    void compareOpenRequested();
    void imagePathChanged(const QString &path);
    void folderBrowseRequested();
    void fullscreenToggleRequested();
    void backgroundPickerRequested();
    void cropModeChanged(bool active);
    void rotateModeChanged(bool active);
    void rotateAngleChanged(double degrees);
    void saveRequested();
    void bwPanelRequested();
    void bwCompareRequested();
    void adjustPanelRequested();
    void flipHorizontalRequested();
    void flipVerticalRequested();
    void exitRequested();
    void openFileRequested();
    void exifRequested();
    void openExternalRequested(bool useOriginal);
    void openExternalPickerRequested();

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;
    bool event(QEvent *event) override;
    bool focusNextPrevChild(bool next) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    enum class OverlayMode { None, Crop, Rotate };

    enum class CropHandle {
        None, Move,
        TopLeft, Top, TopRight,
        Left, Right,
        BottomLeft, Bottom, BottomRight
    };
    static bool isCorner(CropHandle h);

    void fitImage();
    void applyZoom(double factor);
    void navigate(int delta);
    void emitViewChanged();   // emit viewChanged() unless suppressed
    CropHandle hitTestHandle(const QPoint &viewportPos) const;
    void updateOverlayCursor(const QPoint &viewportPos);
    QCursor rotateCursorFor(CropHandle handle) const;
    // Angle of the pointer around the image centre, in degrees — the quantity a
    // rotate drag tracks.
    double pointerAngle(const QPoint &viewportPos) const;

    // --- Overlay plumbing --------------------------------------------------
    void setOverlayMode(OverlayMode mode);
    void enterOverlay();       // swap in the uncropped base and arm the selection
    void exitOverlay();        // bake rotation + crop into the displayed pixmap
    void syncOverlayGeometry();// item transform + scene rect for the current angle
    void showOverlayNotice();  // the one-time hint for the mode being entered
    QSize overlayBaseSize() const;   // size of the full, unrotated base image

    QGraphicsScene *m_scene;
    QGraphicsPixmapItem *m_pixmapItem = nullptr;
    QPixmap m_cropBasePixmap;
    QString m_imagePath;
    QSize m_nativeSize;
    bool m_fitted = true;
    bool m_helpVisible = false;
    int m_backgroundGrey = 0;

    OverlayMode m_overlay = OverlayMode::None;
    QRectF m_cropRect;
    // True while the selection is the automatic "largest rectangle that fits"
    // rather than one the user placed. An automatic selection is recomputed as
    // the angle changes; a placed one is carried along and only shrunk to fit.
    bool m_cropIsAuto = true;
    double m_rotateAngle = 0.0;
    CropHandle m_activeHandle = CropHandle::None;
    QPointF m_dragStartScene;
    QRectF m_dragStartCropRect;
    bool m_rotating = false;
    double m_rotateDragStartAngle = 0.0;
    double m_rotateDragStartPointer = 0.0;

    bool m_cropNoticeShown = false;
    bool m_rotateNoticeShown = false;
    bool m_noticeVisible = false;
    QTimer *m_noticeTimer = nullptr;
    // Suppress viewChanged() while we are programmatically applying another
    // viewer's view (prevents a sync feedback loop).
    bool m_suppressViewChanged = false;
};
