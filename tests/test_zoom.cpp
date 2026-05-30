#include <QtTest/QtTest>
#include <QApplication>
#include <QDir>
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
    void zoomPreservesScaleOnResize();
    void keyZeroRestoreFit();
    void wheelZoomIn();
    void wheelZoomOut();
    void wheelClampHigh();
    void wheelClampLow();
    void keyPlusZoomsIn();
    void keyPlusClampHigh();
    void keyMinusZoomsOut();
    void keyMinusClampLow();

private:
    QString m_imagePath;
    QTemporaryFile *m_tmpFile = nullptr;
};

ZoomTest::ZoomTest() {
    // Use absolute path template: relative templates fail on some Qt/macOS configs.
    // Write image while the file is open, then close for QPixmap to read it.
    m_tmpFile = new QTemporaryFile(QDir::tempPath() + "/test_XXXXXX.png", this);
    Q_ASSERT(m_tmpFile->open());
    m_imagePath = m_tmpFile->fileName();
    QImage img(200, 150, QImage::Format_RGB32);
    img.fill(Qt::blue);
    img.save(m_tmpFile, "PNG");
    m_tmpFile->close();
}

ZoomTest::~ZoomTest() {}

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
    QCoreApplication::sendEvent(viewer.viewport(), &zoomIn);
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
    viewer.show();  // required so showEvent() calls fitImage() before we capture fitScale
    QCoreApplication::processEvents();

    // Capture fit-to-window scale after the widget is shown and fitImage() has run
    double fitScale = viewer.transform().m11();

    // Zoom in — scale moves away from fitScale
    QWheelEvent zoomIn(
        QPointF(200, 150), QPointF(200, 150),
        QPoint(0, 0), QPoint(0, 120),
        Qt::NoButton, Qt::NoModifier,
        Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(viewer.viewport(), &zoomIn);
    QVERIFY(!qFuzzyCompare(viewer.transform().m11(), fitScale));

    // Key 0 calls fitImage → scale returns to fit value
    QTest::keyClick(&viewer, Qt::Key_0);
    QVERIFY(qFuzzyCompare(viewer.transform().m11(), fitScale));
}

void ZoomTest::wheelZoomIn() {
    ImageViewer viewer(m_imagePath);
    double before = viewer.transform().m11(); // 1.0 (identity, no real viewport)

    QWheelEvent event(
        QPointF(50, 50), QPointF(50, 50),
        QPoint(0, 0), QPoint(0, 120),
        Qt::NoButton, Qt::NoModifier,
        Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(viewer.viewport(), &event);

    // scale should have increased by factor ~1.15
    QVERIFY(viewer.transform().m11() > before);
}

void ZoomTest::wheelZoomOut() {
    ImageViewer viewer(m_imagePath);
    viewer.setTransform(QTransform::fromScale(2.0, 2.0));

    QWheelEvent event(
        QPointF(50, 50), QPointF(50, 50),
        QPoint(0, 0), QPoint(0, -120),
        Qt::NoButton, Qt::NoModifier,
        Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(viewer.viewport(), &event);

    QVERIFY(viewer.transform().m11() < 2.0);
}

void ZoomTest::wheelClampHigh() {
    ImageViewer viewer(m_imagePath);
    // 31.0 * 1.15 = 35.65 > 32.0 — should be blocked
    viewer.setTransform(QTransform::fromScale(31.0, 31.0));

    QWheelEvent event(
        QPointF(50, 50), QPointF(50, 50),
        QPoint(0, 0), QPoint(0, 120),
        Qt::NoButton, Qt::NoModifier,
        Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(viewer.viewport(), &event);

    QVERIFY(viewer.transform().m11() <= 32.0);
    QVERIFY(qFuzzyCompare(viewer.transform().m11(), 31.0));
}

void ZoomTest::wheelClampLow() {
    ImageViewer viewer(m_imagePath);
    // 0.04 / 1.15 = 0.0348 < 0.05 — should be blocked
    viewer.setTransform(QTransform::fromScale(0.04, 0.04));

    QWheelEvent event(
        QPointF(50, 50), QPointF(50, 50),
        QPoint(0, 0), QPoint(0, -120),
        Qt::NoButton, Qt::NoModifier,
        Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(viewer.viewport(), &event);

    // Zoom out is clamped, so scale should stay at 0.04
    QVERIFY(qAbs(viewer.transform().m11() - 0.04) < 1e-9);
}

void ZoomTest::keyPlusZoomsIn() {
    ImageViewer viewer(m_imagePath);
    double before = viewer.transform().m11();

    QTest::keyClick(&viewer, Qt::Key_Plus);

    QVERIFY(viewer.transform().m11() > before);
}

void ZoomTest::keyPlusClampHigh() {
    ImageViewer viewer(m_imagePath);
    // 31.0 * 1.15 = 35.65 > 32.0 — should be blocked
    viewer.setTransform(QTransform::fromScale(31.0, 31.0));

    QTest::keyClick(&viewer, Qt::Key_Plus);

    QVERIFY(qFuzzyCompare(viewer.transform().m11(), 31.0));
}

void ZoomTest::keyMinusZoomsOut() {
    ImageViewer viewer(m_imagePath);
    viewer.setTransform(QTransform::fromScale(2.0, 2.0));

    QTest::keyClick(&viewer, Qt::Key_Minus);

    QVERIFY(viewer.transform().m11() < 2.0);
}

void ZoomTest::keyMinusClampLow() {
    ImageViewer viewer(m_imagePath);
    // 0.055 / 1.15 = 0.0478 < 0.05 — clamp triggers
    viewer.setTransform(QTransform::fromScale(0.055, 0.055));

    QTest::keyClick(&viewer, Qt::Key_Minus);

    QVERIFY(qAbs(viewer.transform().m11() - 0.055) < 1e-9);
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    ZoomTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_zoom.moc"
