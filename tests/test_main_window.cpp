// Tests for MainWindow's orchestration of image-transform state. MainWindow is
// driven through its public surface: the ImageViewer is reached via
// centralWidget(), and edit state is inspected via imageStateData().
#include <QtTest/QtTest>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPixmap>
#include <QTemporaryFile>
#include <QTransform>
#include "MainWindow.h"
#include "ImageViewer.h"

static QString dims(int w, int h) { return QString("%1 × %2").arg(w).arg(h); }

class MainWindowTest : public QObject {
    Q_OBJECT

public:
    MainWindowTest();

private slots:
    void metadata_showsOrientedOriginalDimensions();
    void metadata_afterCrop_showsBothOriginalAndCurrent();
    void cropRect_rotatesWithImage_withoutClipping();

private:
    ImageViewer *viewerOf(MainWindow &w) {
        return qobject_cast<ImageViewer *>(w.centralWidget());
    }
    QString m_imagePath;
    QTemporaryFile *m_tmpFile = nullptr;
};

MainWindowTest::MainWindowTest() {
    m_tmpFile = new QTemporaryFile(QDir::tempPath() + "/mw_XXXXXX.png", this);
    if (!m_tmpFile->open()) return;
    QImage img(200, 150, QImage::Format_RGB32);
    img.fill(Qt::darkCyan);
    img.save(m_tmpFile, "PNG");
    m_tmpFile->close();
    m_imagePath = m_tmpFile->fileName();
}

// Before any edit, the original dimensions are present (and match the loaded,
// EXIF-oriented image), with no separate "current" dimensions.
void MainWindowTest::metadata_showsOrientedOriginalDimensions() {
    MainWindow w(m_imagePath);
    w.resize(400, 300);
    w.show();
    QCoreApplication::processEvents();

    auto data = w.imageStateData();
    QCOMPARE(data.value("Dimensions"), dims(200, 150));
    QVERIFY(!data.contains("CurrentDimensions"));
}

// Regression: after a crop the overlay must still report the original size
// (not only the cropped size), plus the new current size.
void MainWindowTest::metadata_afterCrop_showsBothOriginalAndCurrent() {
    MainWindow w(m_imagePath);
    w.resize(400, 300);
    w.show();
    QCoreApplication::processEvents();

    ImageViewer *viewer = viewerOf(w);
    QVERIFY(viewer);

    viewer->setCropMode(true);
    viewer->setCropRect(QRectF(50, 30, 100, 60));
    viewer->setCropMode(false);   // applies the crop; MainWindow folds it in

    auto data = w.imageStateData();
    QCOMPARE(data.value("Dimensions"), dims(200, 150));          // original preserved
    QCOMPARE(data.value("CurrentDimensions"), "→ " + dims(100, 60));
    QVERIFY(data.value("State_Edits").contains("crop"));
}

// Regression: when the image is rotated, a stored crop selection must rotate
// with it so it still surrounds the same area. A 140x60 crop on a 200x150 image
// becomes 60x140 after a 90° turn — it must NOT be clipped (to 60x100) against
// the pre-rotation height.
void MainWindowTest::cropRect_rotatesWithImage_withoutClipping() {
    MainWindow w(m_imagePath); // 200x150
    w.resize(400, 300);
    w.show();
    QCoreApplication::processEvents();

    ImageViewer *viewer = viewerOf(w);
    QVERIFY(viewer);

    // A crop that spans most of the width, so a 90° rotation maps it past the
    // old 150 px height.
    const QRectF crop(50, 30, 140, 60);
    viewer->setCropMode(true);
    viewer->setCropRect(crop);
    viewer->setCropMode(false);
    QCOMPARE(viewer->cropRect(), crop);

    // Rotate 90° clockwise (R), then re-enter crop as the user would.
    QTest::keyClick(viewer, Qt::Key_R);
    QCOMPARE(viewer->cropRect().size(), QSizeF(60, 140));   // swapped, not clipped

    viewer->setCropMode(true);
    QCOMPARE(viewer->cropRect().size(), QSizeF(60, 140));   // preserved on re-entry
    // Stays within the new, transposed image bounds (150 x 200).
    QVERIFY(viewer->cropRect().right()  <= 150.0);
    QVERIFY(viewer->cropRect().bottom() <= 200.0);
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindowTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_main_window.moc"
