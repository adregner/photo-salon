#include <QtTest/QtTest>
#include <QApplication>
#include <QImage>
#include <QImageWriter>
#include <QTemporaryDir>
#include "ImageFormats.h"

class ImageFormatsTest : public QObject {
    Q_OBJECT
private slots:
    void containsJpeg();
    void containsPng();
    void containsBmp();
    void noEmptyEntries();
    void allEntriesHaveGlobPrefix();
    void tiffRoundTrip();
    void saveFilterCoversWritableFormats();
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

// The "Open in..." feature exports the edited image as TIFF, so the format must
// round-trip losslessly. Skips (rather than fails) on a qtbase-only Qt with no
// TIFF plugin, so CI stays green where the plugin genuinely isn't installed.
void ImageFormatsTest::tiffRoundTrip() {
    if (!QImageWriter::supportedImageFormats().contains("tiff"))
        QSKIP("TIFF write support not available (qtimageformats plugin missing)");

    QImage src(40, 30, QImage::Format_RGB32);
    src.fill(Qt::green);
    src.setPixel(5, 5, qRgb(10, 20, 30));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("roundtrip.tiff");
    QVERIFY2(src.save(path, "TIFF"), qPrintable(path));

    QImage loaded(path);
    QVERIFY(!loaded.isNull());
    QCOMPARE(loaded.size(), src.size());
    QCOMPARE(loaded.convertToFormat(QImage::Format_RGB32).pixel(5, 5), qRgb(10, 20, 30));
}

// The Save dialog must let the user pick any format Qt can write.
void ImageFormatsTest::saveFilterCoversWritableFormats() {
    const QString filter = supportedSaveFilter();
    for (const QByteArray &fmt : QImageWriter::supportedImageFormats()) {
        const QString glob = QString("*.%1").arg(QString::fromLatin1(fmt).toLower());
        QVERIFY2(filter.contains(glob), qPrintable(glob));
    }
    QVERIFY(filter.contains("*.png"));
    QVERIFY(filter.contains("*.jpg") || filter.contains("*.jpeg"));
    QVERIFY(filter.contains("All Files (*)"));
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    ImageFormatsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_image_formats.moc"
