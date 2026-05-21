#pragma once
#include <QGraphicsView>
#include <QString>

class QGraphicsScene;

class ImageViewer : public QGraphicsView {
    Q_OBJECT

public:
    explicit ImageViewer(const QString &imagePath, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void fitImage();

    QGraphicsScene *m_scene;
};
