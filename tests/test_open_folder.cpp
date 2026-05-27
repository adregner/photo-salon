#include <QtTest/QtTest>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include "ImageFormats.h"

static void makePng(const QDir &dir, const QString &name) {
    QImage img(10, 10, QImage::Format_RGB32);
    img.fill(Qt::green);
    img.save(dir.absoluteFilePath(name));
}

class OpenFolderTest : public QObject {
    Q_OBJECT
private slots:
    void resolveFile_returnsFile();
    void resolveDir_returnsFirstImage();
    void resolveDir_emptyDir_setsError();
    void resolveNonExistent_setsError();
};

void OpenFolderTest::resolveFile_returnsFile() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    makePng(QDir(tmp.path()), "photo.png");
    QString file = QDir(tmp.path()).absoluteFilePath("photo.png");

    QString error;
    QString result = resolveImagePath(file, &error);
    QCOMPARE(result, file);
    QVERIFY(error.isEmpty());
}

void OpenFolderTest::resolveDir_returnsFirstImage() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    makePng(dir, "alpha.png");
    makePng(dir, "beta.png");
    makePng(dir, "gamma.png");

    QString error;
    QString result = resolveImagePath(tmp.path(), &error);
    QCOMPARE(result, dir.absoluteFilePath("alpha.png"));
    QVERIFY(error.isEmpty());
}

void OpenFolderTest::resolveDir_emptyDir_setsError() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QString error;
    QString result = resolveImagePath(tmp.path(), &error);
    QVERIFY(result.isEmpty());
    QVERIFY(!error.isEmpty());
}

void OpenFolderTest::resolveNonExistent_setsError() {
    QString error;
    QString result = resolveImagePath("/nonexistent/path/that/does/not/exist.jpg", &error);
    QVERIFY(result.isEmpty());
    QVERIFY(!error.isEmpty());
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    OpenFolderTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_open_folder.moc"
