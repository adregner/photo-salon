#include <QtTest/QtTest>
#include <QApplication>
#include "ImageFormats.h"

class ImageFormatsTest : public QObject {
    Q_OBJECT
private slots:
    void containsJpeg();
    void containsPng();
    void containsBmp();
    void noEmptyEntries();
    void allEntriesHaveGlobPrefix();
};

void ImageFormatsTest::containsJpeg() {
    QStringList exts = supportedExtensions();
    QVERIFY(exts.contains("*.jpg") || exts.contains("*.jpeg"));
}

void ImageFormatsTest::containsPng() {
    QVERIFY(supportedExtensions().contains("*.png"));
}

void ImageFormatsTest::containsBmp() {
    QVERIFY(supportedExtensions().contains("*.bmp"));
}

void ImageFormatsTest::noEmptyEntries() {
    for (const QString &e : supportedExtensions())
        QVERIFY(!e.isEmpty());
}

void ImageFormatsTest::allEntriesHaveGlobPrefix() {
    for (const QString &e : supportedExtensions())
        QVERIFY2(e.startsWith("*."), qPrintable(e));
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    ImageFormatsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_image_formats.moc"
