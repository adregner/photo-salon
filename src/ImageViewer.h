#pragma once
#include <QGraphicsView>
#include <QSize>
#include <QString>

class QGraphicsScene;
class QKeyEvent;
class QResizeEvent;
class QShowEvent;
class QWheelEvent;

class ImageViewer : public QGraphicsView {
    Q_OBJECT

public:
    explicit ImageViewer(const QString &imagePath, QWidget *parent = nullptr);

    QSize nativeImageSize() const { return m_nativeSize; }
    bool helpVisible() const { return m_helpVisible; }

signals:
    void helpVisibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void fitImage();
    void applyZoom(double factor);

    QGraphicsScene *m_scene;
    QSize m_nativeSize;
    bool m_fitted = true;
    bool m_helpVisible = false;
};
