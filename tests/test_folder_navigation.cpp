#include <QtTest/QtTest>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include "ImageViewer.h"

static QString makePng(const QDir &dir, const QString &name) {
    QString path = dir.absoluteFilePath(name);
    QImage img(10, 10, QImage::Format_RGB32);
    img.fill(Qt::red);
    img.save(path);
    return path;
}

class FolderNavigationTest : public QObject {
    Q_OBJECT

private slots:
    void navigateNextWraps();
    void navigatePrevWraps();
    void navigateRightKey();
    void navigateLeftKey();
    void imagePathChangedSignal();
    void loadImageUpdatesPath();
};

void FolderNavigationTest::loadImageUpdatesPath() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QString a = makePng(QDir(tmp.path()), "a.png");
    QString b = makePng(QDir(tmp.path()), "b.png");

    ImageViewer viewer(a);
    QCOMPARE(viewer.currentPath(), a);

    viewer.loadImage(b);
    QCOMPARE(viewer.currentPath(), b);
}

void FolderNavigationTest::imagePathChangedSignal() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QString a = makePng(QDir(tmp.path()), "a.png");
    QString b = makePng(QDir(tmp.path()), "b.png");

    ImageViewer viewer(a);
    QSignalSpy spy(&viewer, &ImageViewer::imagePathChanged);

    viewer.loadImage(b);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), b);
}

void FolderNavigationTest::navigateNextWraps() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    QString a = makePng(dir, "a.png");
    QString b = makePng(dir, "b.png");
    QString c = makePng(dir, "c.png");

    ImageViewer viewer(a);
    QTest::keyClick(&viewer, Qt::Key_Right);
    QCOMPARE(viewer.currentPath(), b);
    QTest::keyClick(&viewer, Qt::Key_Right);
    QCOMPARE(viewer.currentPath(), c);
    QTest::keyClick(&viewer, Qt::Key_Right);   // wraps to first
    QCOMPARE(viewer.currentPath(), a);
}

void FolderNavigationTest::navigatePrevWraps() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    QString a = makePng(dir, "a.png");
    makePng(dir, "b.png");
    QString c = makePng(dir, "c.png");

    ImageViewer viewer(a);
    QTest::keyClick(&viewer, Qt::Key_Left);   // wraps to last
    QCOMPARE(viewer.currentPath(), c);
}

void FolderNavigationTest::navigateRightKey() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    QString a = makePng(dir, "img1.png");
    QString b = makePng(dir, "img2.png");

    ImageViewer viewer(a);
    QTest::keyClick(&viewer, Qt::Key_Right);
    QCOMPARE(viewer.currentPath(), b);
}

void FolderNavigationTest::navigateLeftKey() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    makePng(dir, "img1.png");
    QString b = makePng(dir, "img2.png");
    QString c = makePng(dir, "img3.png");

    ImageViewer viewer(c);
    QTest::keyClick(&viewer, Qt::Key_Left);
    QCOMPARE(viewer.currentPath(), b);
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    FolderNavigationTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_folder_navigation.moc"
