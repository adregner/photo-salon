#pragma once
#include <QGraphicsView>
#include <QString>

class QGraphicsScene;
class QWheelEvent;
class QKeyEvent;

class ImageViewer : public QGraphicsView {
    Q_OBJECT

public:
    explicit ImageViewer(const QString &imagePath, QWidget *parent = nullptr);

protected:
    bool event(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void fitImage();
    void applyZoom(double factor);

    QGraphicsScene *m_scene;
    bool m_fitted = true;
};
