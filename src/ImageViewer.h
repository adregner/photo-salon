#pragma once
#include <QGraphicsView>
#include <QPointF>
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
    void setCropMode(bool active);
    bool cropMode() const { return m_cropMode; }
    void setCropRect(const QRectF &rect);
    QRectF cropRect() const { return m_cropRect; }
    bool cropNoticeVisible() const { return m_cropNoticeVisible; }

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
    // The displayed image content changed (a new render, a load, entering or
    // leaving crop) — drives the histogram, which reads what is on screen.
    void displayImageChanged();
    // The user interacted with this viewer (click/wheel) — request input focus.
    void focusRequested();
    // Shift+O — open a second image to compare side by side.
    void compareOpenRequested();
    void imagePathChanged(const QString &path);
    void folderBrowseRequested();
    void fullscreenToggleRequested();
    void backgroundPickerRequested();
    void cropModeChanged(bool active);
    void saveRequested();
    void bwPanelRequested();
    void bwCompareRequested();
    void adjustPanelRequested();
    void rotateRequested();
    void flipHorizontalRequested();
    void flipVerticalRequested();
    void exitRequested();
    void openFileRequested();
    void exifRequested();
    void histogramRequested();
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
    enum class CropHandle {
        None, Move,
        TopLeft, Top, TopRight,
        Left, Right,
        BottomLeft, Bottom, BottomRight
    };

    void fitImage();
    void applyZoom(double factor);
    void navigate(int delta);
    void emitViewChanged();   // emit viewChanged() unless suppressed
    CropHandle hitTestHandle(const QPoint &viewportPos) const;
    void updateCropCursor(const QPoint &viewportPos);

    QGraphicsScene *m_scene;
    QGraphicsPixmapItem *m_pixmapItem = nullptr;
    QPixmap m_cropBasePixmap;
    QString m_imagePath;
    QSize m_nativeSize;
    bool m_fitted = true;
    bool m_helpVisible = false;
    int m_backgroundGrey = 0;
    bool m_cropMode = false;
    QRectF m_cropRect;
    CropHandle m_activeHandle = CropHandle::None;
    QPointF m_dragStartScene;
    QRectF m_dragStartCropRect;
    bool m_cropNoticeShown = false;
    bool m_cropNoticeVisible = false;
    QTimer *m_cropNoticeTimer = nullptr;
    // Suppress viewChanged() while we are programmatically applying another
    // viewer's view (prevents a sync feedback loop).
    bool m_suppressViewChanged = false;
};
