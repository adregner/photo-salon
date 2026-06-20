// Tests for MainWindow's orchestration of image-transform state. MainWindow is
// driven through its public surface: the ImageViewer is reached via
// centralWidget(), and edit state is inspected via imageStateData().
#include <QtTest/QtTest>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QImage>
#include <QListWidget>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTimer>
#include <QTransform>
#include "MainWindow.h"
#include "ImageViewer.h"

static QString dims(int w, int h) { return QString("%1 × %2").arg(w).arg(h); }

class MainWindowTest : public QObject {
    Q_OBJECT

public:
    MainWindowTest();

private slots:
    void init() { QSettings().clear(); }   // isolate each test from saved manifests
    void metadata_showsOrientedOriginalDimensions();
    void metadata_afterCrop_showsBothOriginalAndCurrent();
    void cropRect_rotatesWithImage_withoutClipping();
    void tabFolderDialog_clickingFileOpensItImmediately();

private:
    ImageViewer *viewerOf(MainWindow &w) {
        return w.activeViewer();
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

// The Tab "open from current folder" dialog opens the picked file at once: a
// single click on a list entry loads it (no separate confirmation step).
void MainWindowTest::tabFolderDialog_clickingFileOpensItImmediately() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    QImage img(40, 30, QImage::Format_RGB32);
    img.fill(Qt::red);  QVERIFY(img.save(dir.absoluteFilePath("a.png")));
    img.fill(Qt::blue); QVERIFY(img.save(dir.absoluteFilePath("b.png")));

    MainWindow w(dir.absoluteFilePath("a.png"));
    w.show();
    QCoreApplication::processEvents();
    ImageViewer *viewer = viewerOf(w);
    QVERIFY(viewer);
    QCOMPARE(QFileInfo(viewer->currentPath()).fileName(), QString("a.png"));

    // Once the modal dialog is up, click the entry for "b.png".
    bool clicked = false;
    auto *poll = new QTimer(&w);
    poll->setInterval(10);
    connect(poll, &QTimer::timeout, &w, [&]() {
        auto *dlg = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dlg) return;
        poll->stop();
        auto *list = dlg->findChild<QListWidget *>();
        if (!list) { dlg->reject(); return; }
        QListWidgetItem *target = nullptr;
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->text() == "b.png") target = list->item(i);
        if (!target) { dlg->reject(); return; }
        list->scrollToItem(target);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                          list->visualItemRect(target).center());
        clicked = true;
    });
    // Safety net: never hang if the dialog can't be driven.
    QTimer::singleShot(3000, &w, [&]() {
        if (auto *m = QApplication::activeModalWidget()) m->close();
    });

    poll->start();
    QTest::keyClick(viewer, Qt::Key_Tab);   // opens the modal dialog (blocks until closed)

    QVERIFY(clicked);
    QCOMPARE(QFileInfo(viewer->currentPath()).fileName(), QString("b.png"));
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    // Redirect QSettings to a throwaway sandbox so the manifest store written by
    // MainWindow never touches the real user configuration.
    QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName(QStringLiteral("photo-salon-test"));
    app.setApplicationName(QStringLiteral("photo-salon-test"));
    MainWindowTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_main_window.moc"
