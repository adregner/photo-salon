#include <QtTest/QtTest>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QMouseEvent>
#include <QTemporaryFile>
#include "ImageViewer.h"

// Drag from one viewport point to another with the left button held. Events are
// sent directly (with explicit button state) so the press/move/release sequence
// is deterministic regardless of the virtual cursor position.
static void dragOnViewport(QWidget *vp, const QPoint &from, const QPoint &to) {
    const QPointF gFrom = vp->mapToGlobal(from);
    const QPointF gTo   = vp->mapToGlobal(to);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(from), gFrom,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    qApp->sendEvent(vp, &press);
    QMouseEvent move(QEvent::MouseMove, QPointF(to), gTo,
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    qApp->sendEvent(vp, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(to), gTo,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    qApp->sendEvent(vp, &release);
}

static QString makeTempImage(QObject *parent) {
    // Use absolute path template: relative templates fail on some Qt/macOS configs.
    // Write image while the file is open, then close for QPixmap to read it.
    auto *tmp = new QTemporaryFile(QDir::tempPath() + "/crop_XXXXXX.png", parent);
    if (!tmp->open()) return {};
    QString path = tmp->fileName();
    QImage img(200, 150, QImage::Format_RGB32);
    img.fill(Qt::cyan);
    img.save(tmp, "PNG");
    tmp->close();
    return path;
}

class CropToolTest : public QObject {
    Q_OBJECT

public:
    CropToolTest();

private slots:
    void xKeyEntersCropMode();
    void xKeyTogglesCropModeOff();
    void cropModeChangedSignal();
    void enterCropMode_initializesFullCropRect();
    void exitCropMode_withSubRect_appliesCrop();
    void exitCropMode_withFullRect_keepsFullImage();
    void reenterCropMode_reloadsOriginal();
    void sKeyEmitsSaveRequested();
    void setCropModeRestoresDragMode();
    void doubleClickInCropRect_resetsCropRectToFull();
    void doubleClickOutsideCropRect_leavesRectUnchanged();
    void firstCropEntry_noticeIsVisible();
    void secondCropEntry_noticeIsNotVisible();
    void doubleClickInCropRect_dismissesNotice();
    void cornerHandle_grabbableFromOutsideTheRect();

private:
    QString m_imagePath;
};

CropToolTest::CropToolTest() {
    m_imagePath = makeTempImage(this);
}

void CropToolTest::xKeyEntersCropMode() {
    ImageViewer viewer(m_imagePath);
    QVERIFY(!viewer.cropMode());
    QTest::keyClick(&viewer, Qt::Key_X);
    QVERIFY(viewer.cropMode());
}

// Escape is handled globally by MainWindow's event filter; use X or
// setCropMode() to toggle off when testing the viewer in isolation.
void CropToolTest::xKeyTogglesCropModeOff() {
    ImageViewer viewer(m_imagePath);
    QTest::keyClick(&viewer, Qt::Key_X);
    QVERIFY(viewer.cropMode());
    QTest::keyClick(&viewer, Qt::Key_X);
    QVERIFY(!viewer.cropMode());
}

void CropToolTest::cropModeChangedSignal() {
    ImageViewer viewer(m_imagePath);
    QSignalSpy spy(&viewer, &ImageViewer::cropModeChanged);

    QTest::keyClick(&viewer, Qt::Key_X);
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

    QTest::keyClick(&viewer, Qt::Key_X);
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

    QTest::keyClick(&viewer, Qt::Key_X);
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
    QTest::keyClick(&viewer, Qt::Key_X);
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
    QTest::keyClick(&viewer, Qt::Key_X);
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

void CropToolTest::doubleClickInCropRect_resetsCropRectToFull() {
    ImageViewer viewer(m_imagePath); // 200x150
    viewer.resize(400, 300);
    viewer.show();
    QCoreApplication::processEvents();

    viewer.setCropMode(true);
    viewer.setCropRect(QRectF(50, 37, 100, 76));
    QCOMPARE(viewer.cropRect(), QRectF(50, 37, 100, 76));

    // Double-click at the scene-center of the sub-rect (inside it)
    QPoint vp = viewer.mapFromScene(QPointF(100, 75));
    QTest::mouseDClick(viewer.viewport(), Qt::LeftButton, Qt::NoModifier, vp);

    QSize native = viewer.nativeImageSize();
    QCOMPARE(viewer.cropRect(), QRectF(0, 0, native.width(), native.height()));
}

void CropToolTest::doubleClickOutsideCropRect_leavesRectUnchanged() {
    ImageViewer viewer(m_imagePath); // 200x150
    viewer.resize(800, 600);
    viewer.show();
    QCoreApplication::processEvents();

    viewer.setCropMode(true);
    // Crop rect is tiny in the top-left corner of image space
    viewer.setCropRect(QRectF(0, 0, 20, 15));

    // Map a point well outside the sub-rect (bottom-right of image) to viewport
    QPoint vp = viewer.mapFromScene(QPointF(190, 140));
    QTest::mouseDClick(viewer.viewport(), Qt::LeftButton, Qt::NoModifier, vp);

    // Crop rect should be unchanged
    QCOMPARE(viewer.cropRect(), QRectF(0, 0, 20, 15));
}

void CropToolTest::firstCropEntry_noticeIsVisible() {
    ImageViewer viewer(m_imagePath);
    viewer.resize(400, 300);
    viewer.show();
    QCoreApplication::processEvents();

    QVERIFY(!viewer.overlayNoticeVisible());
    viewer.setCropMode(true);
    QVERIFY(viewer.overlayNoticeVisible());
}

void CropToolTest::secondCropEntry_noticeIsNotVisible() {
    ImageViewer viewer(m_imagePath);
    viewer.resize(400, 300);
    viewer.show();
    QCoreApplication::processEvents();

    viewer.setCropMode(true);
    QVERIFY(viewer.overlayNoticeVisible());
    viewer.setCropMode(false);

    viewer.setCropMode(true);
    QVERIFY(!viewer.overlayNoticeVisible());
}

void CropToolTest::doubleClickInCropRect_dismissesNotice() {
    ImageViewer viewer(m_imagePath); // 200x150
    viewer.resize(400, 300);
    viewer.show();
    QCoreApplication::processEvents();

    viewer.setCropMode(true);
    QVERIFY(viewer.overlayNoticeVisible());

    QPoint vp = viewer.mapFromScene(QPointF(100, 75));
    QTest::mouseDClick(viewer.viewport(), Qt::LeftButton, Qt::NoModifier, vp);

    QVERIFY(!viewer.overlayNoticeVisible());
}

// The corner handles have a generous catch radius, so the user can grab a
// corner from a little outside the crop rectangle (here, 15 px diagonally out).
void CropToolTest::cornerHandle_grabbableFromOutsideTheRect() {
    ImageViewer viewer(m_imagePath); // 200x150
    viewer.resize(400, 300);
    viewer.show();
    QCoreApplication::processEvents();

    viewer.setCropMode(true);
    viewer.setCropRect(QRectF(50, 30, 100, 60));

    // Top-left corner in viewport space, then a press point just outside it.
    QPoint corner = viewer.mapFromScene(QPointF(50, 30));
    QPoint grabFrom = corner - QPoint(15, 15);
    QPoint dragTo   = corner + QPoint(20, 16);
    dragOnViewport(viewer.viewport(), grabFrom, dragTo);

    // The top-left corner moved, so the handle was grabbed from outside the rect.
    QVERIFY(viewer.cropRect().topLeft() != QPointF(50, 30));
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    CropToolTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_crop_tool.moc"
