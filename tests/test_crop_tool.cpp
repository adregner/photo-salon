#include <QtTest/QtTest>
#include <QApplication>
#include <QImage>
#include <QTemporaryFile>
#include "ImageViewer.h"

static QString makeTempImage(QObject *parent) {
    auto *tmp = new QTemporaryFile("crop_XXXXXX.png", parent);
    tmp->open();
    tmp->close();
    QImage img(200, 150, QImage::Format_RGB32);
    img.fill(Qt::cyan);
    img.save(tmp->fileName());
    return tmp->fileName();
}

class CropToolTest : public QObject {
    Q_OBJECT

public:
    CropToolTest();

private slots:
    void cKeyEntersCropMode();
    void cKeyTogglesCropModeOff();
    void cropModeChangedSignal();
    void enterCropMode_initializesFullCropRect();
    void exitCropMode_withSubRect_appliesCrop();
    void exitCropMode_withFullRect_keepsFullImage();
    void reenterCropMode_reloadsOriginal();
    void sKeyEmitsSaveRequested();
    void setCropModeRestoresDragMode();

private:
    QString m_imagePath;
};

CropToolTest::CropToolTest() {
    m_imagePath = makeTempImage(this);
}

void CropToolTest::cKeyEntersCropMode() {
    ImageViewer viewer(m_imagePath);
    QVERIFY(!viewer.cropMode());
    QTest::keyClick(&viewer, Qt::Key_C);
    QVERIFY(viewer.cropMode());
}

// Escape is handled globally by MainWindow's event filter; use C or
// setCropMode() to toggle off when testing the viewer in isolation.
void CropToolTest::cKeyTogglesCropModeOff() {
    ImageViewer viewer(m_imagePath);
    QTest::keyClick(&viewer, Qt::Key_C);
    QVERIFY(viewer.cropMode());
    QTest::keyClick(&viewer, Qt::Key_C);
    QVERIFY(!viewer.cropMode());
}

void CropToolTest::cropModeChangedSignal() {
    ImageViewer viewer(m_imagePath);
    QSignalSpy spy(&viewer, &ImageViewer::cropModeChanged);

    QTest::keyClick(&viewer, Qt::Key_C);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);

    viewer.setCropMode(false);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toBool(), false);
}

void CropToolTest::enterCropMode_initializesFullCropRect() {
    ImageViewer viewer(m_imagePath);
    viewer.resize(400, 300);
    viewer.show();
    QCoreApplication::processEvents();

    QTest::keyClick(&viewer, Qt::Key_C);
    QVERIFY(viewer.cropMode());

    // Crop rect should span the full image on first entry
    QSize native = viewer.nativeImageSize();
    QCOMPARE(viewer.cropRect(), QRectF(0, 0, native.width(), native.height()));
}

void CropToolTest::exitCropMode_withSubRect_appliesCrop() {
    ImageViewer viewer(m_imagePath); // 200x150
    viewer.resize(400, 300);
    viewer.show();
    QCoreApplication::processEvents();

    QTest::keyClick(&viewer, Qt::Key_C);
    QVERIFY(viewer.cropMode());

    // Set a partial crop rect (half the image)
    viewer.setCropRect(QRectF(50, 37, 100, 76));
    QCOMPARE(viewer.cropRect(), QRectF(50, 37, 100, 76));

    viewer.setCropMode(false);
    QVERIFY(!viewer.cropMode());

    // Display pixmap should match the cropped dimensions
    QPixmap display = viewer.currentDisplayPixmap();
    QVERIFY(!display.isNull());
    QCOMPARE(display.size(), QSize(100, 76));
}

void CropToolTest::exitCropMode_withFullRect_keepsFullImage() {
    ImageViewer viewer(m_imagePath);
    viewer.resize(400, 300);
    viewer.show();
    QCoreApplication::processEvents();

    // Enter crop mode (defaults to full image rect), exit without changing
    QTest::keyClick(&viewer, Qt::Key_C);
    viewer.setCropMode(false);

    QPixmap display = viewer.currentDisplayPixmap();
    QSize native = viewer.nativeImageSize();
    QCOMPARE(display.size(), native);
}

void CropToolTest::reenterCropMode_reloadsOriginal() {
    ImageViewer viewer(m_imagePath); // 200x150
    viewer.resize(400, 300);
    viewer.show();
    QCoreApplication::processEvents();

    // Apply a crop
    QTest::keyClick(&viewer, Qt::Key_C);
    viewer.setCropRect(QRectF(50, 37, 100, 76));
    viewer.setCropMode(false);
    QCOMPARE(viewer.currentDisplayPixmap().size(), QSize(100, 76));

    // Re-enter crop mode: original should be reloaded, crop rect preserved
    viewer.setCropMode(true);
    QVERIFY(viewer.cropMode());
    QCOMPARE(viewer.cropRect(), QRectF(50, 37, 100, 76));

    // Exit again with same crop rect: same result
    viewer.setCropMode(false);
    QCOMPARE(viewer.currentDisplayPixmap().size(), QSize(100, 76));
}

void CropToolTest::sKeyEmitsSaveRequested() {
    ImageViewer viewer(m_imagePath);
    QSignalSpy spy(&viewer, &ImageViewer::saveRequested);
    QTest::keyClick(&viewer, Qt::Key_S);
    QCOMPARE(spy.count(), 1);
}

void CropToolTest::setCropModeRestoresDragMode() {
    ImageViewer viewer(m_imagePath);
    QCOMPARE(viewer.dragMode(), QGraphicsView::ScrollHandDrag);
    viewer.setCropMode(true);
    QCOMPARE(viewer.dragMode(), QGraphicsView::NoDrag);
    viewer.setCropMode(false);
    QCOMPARE(viewer.dragMode(), QGraphicsView::ScrollHandDrag);
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    CropToolTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_crop_tool.moc"
