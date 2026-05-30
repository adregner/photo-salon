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
class QWheelEvent;

class ImageViewer : public QGraphicsView {
    Q_OBJECT

public:
    explicit ImageViewer(const QString &imagePath, QWidget *parent = nullptr);

    void loadImage(const QString &path);
    QString currentPath() const { return m_imagePath; }
    QSize nativeImageSize() const { return m_nativeSize; }
    bool helpVisible() const { return m_helpVisible; }
    void closeHelp();
    QPixmap pixmap() const;
    QPixmap currentDisplayPixmap() const;
    void setDisplayPixmap(const QPixmap &px);
    void setBasePixmapForCrop(const QPixmap &px);
    void setBackgroundGrey(int value);
    int backgroundGrey() const { return m_backgroundGrey; }
    void setCropMode(bool active);
    bool cropMode() const { return m_cropMode; }
    void setCropRect(const QRectF &rect);
    QRectF cropRect() const { return m_cropRect; }

signals:
    void helpVisibilityChanged(bool visible);
    void imagePathChanged(const QString &path);
    void folderBrowseRequested();
    void fullscreenToggleRequested();
    void backgroundPickerRequested();
    void cropModeChanged(bool active);
    void saveRequested();
    void bwPanelRequested();
    void bwCompareRequested();
    void rotateRequested();
    void flipHorizontalRequested();
    void flipVerticalRequested();
    void exitRequested();
    void openFileRequested();
    void exifRequested();

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;

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
};
