#pragma once
#include <QGraphicsView>
#include <QSize>
#include <QString>

class QGraphicsPixmapItem;
class QGraphicsScene;
class QKeyEvent;
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
    QPixmap pixmap() const;

signals:
    void helpVisibilityChanged(bool visible);
    void imagePathChanged(const QString &path);
    void folderBrowseRequested();
    void fullscreenToggleRequested();

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void fitImage();
    void applyZoom(double factor);
    void navigate(int delta);

    QGraphicsScene *m_scene;
    QGraphicsPixmapItem *m_pixmapItem = nullptr;
    QString m_imagePath;
    QSize m_nativeSize;
    bool m_fitted = true;
    bool m_helpVisible = false;
};
