#include <QtTest/QtTest>
#include <QApplication>
#include <QImage>
#include <QShowEvent>
#include <QTemporaryFile>
#include <QWheelEvent>
#include "ImageViewer.h"

class ZoomTest : public QObject {
    Q_OBJECT

public:
    ZoomTest();
    ~ZoomTest();

private slots:
    void placeholder();
    void zoomPreservesScaleOnResize();
    void keyZeroRestoreFit();

private:
    QString m_imagePath;
    QTemporaryFile *m_tmpFile = nullptr;
};

ZoomTest::ZoomTest() {
    m_tmpFile = new QTemporaryFile("test_XXXXXX.png", this);
    m_tmpFile->open();
    m_tmpFile->close();
    QImage img(200, 150, QImage::Format_RGB32);
    img.fill(Qt::blue);
    img.save(m_tmpFile->fileName());
    m_imagePath = m_tmpFile->fileName();
}

ZoomTest::~ZoomTest() {}

void ZoomTest::placeholder() {
    QVERIFY(true);
}

void ZoomTest::zoomPreservesScaleOnResize() {
    ImageViewer viewer(m_imagePath);
    viewer.resize(400, 300);
    QCoreApplication::processEvents();

    // Zoom in so scale > 1.0 and m_fitted becomes false
    QWheelEvent zoomIn(
        QPointF(200, 150), QPointF(200, 150),
        QPoint(0, 0), QPoint(0, 120),
        Qt::NoButton, Qt::NoModifier,
        Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&viewer, &zoomIn);
    double zoomedScale = viewer.transform().m11();
    QVERIFY(zoomedScale > 1.0);

    // showEvent must NOT re-fit (m_fitted is false after zoom)
    QShowEvent showEv;
    QCoreApplication::sendEvent(&viewer, &showEv);
    QVERIFY(qFuzzyCompare(viewer.transform().m11(), zoomedScale));
}

void ZoomTest::keyZeroRestoreFit() {
    ImageViewer viewer(m_imagePath);
    viewer.resize(400, 300);
    QCoreApplication::processEvents();

    // Zoom in
    QWheelEvent zoomIn(
        QPointF(200, 150), QPointF(200, 150),
        QPoint(0, 0), QPoint(0, 120),
        Qt::NoButton, Qt::NoModifier,
        Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&viewer, &zoomIn);
    QVERIFY(viewer.transform().m11() < 1.5); // scale ≈ 1.15

    // Key 0 calls fitImage (image 200x150 fits into ~400x300 → scale ≈ 2.0)
    QTest::keyClick(&viewer, Qt::Key_0);
    QVERIFY(viewer.transform().m11() > 1.5); // scale jumped to fit value
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    ZoomTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_zoom.moc"
