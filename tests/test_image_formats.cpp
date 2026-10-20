#include <QtTest/QtTest>
#include <QApplication>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QTemporaryDir>
#include "ImageFormats.h"

class ImageFormatsTest : public QObject {
    Q_OBJECT
private slots:
    void cleanup();
    void allocationLimitGovernsDecoding();
    void allocationLimitCoversFullResolutionFiles();
    void containsJpeg();
    void containsPng();
    void containsBmp();
    void noEmptyEntries();
    void allEntriesHaveGlobPrefix();
    void tiffRoundTrip();
    void saveFilterCoversWritableFormats();
};

// allocationLimitGovernsDecoding() deliberately squeezes the limit, and a failed
// QVERIFY would leave it that way for everything after it. cleanup() runs after
// every test function, including a failing one.
void ImageFormatsTest::cleanup() {
    raiseImageAllocationLimit();
}

// Qt's default 256 MB allocation limit refuses full-resolution camera files, and
// does it quietly: the read just returns null. Rather than allocate 300-odd MB
// here, squeeze the limit below a small image to show that it really is the
// thing that decides, then show raiseImageAllocationLimit() lifts it.
void ImageFormatsTest::allocationLimitGovernsDecoding() {
    QImage src(600, 600, QImage::Format_RGB32);   // ~1.4 MB once decoded
    src.fill(Qt::blue);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("limit.png");
    QVERIFY2(src.save(path, "PNG"), qPrintable(path));

    QImageReader::setAllocationLimit(1);
    QVERIFY2(QImage(path).isNull(), "a 1 MB limit should have blocked a 1.4 MB image");

    raiseImageAllocationLimit();
    QCOMPARE(QImageReader::allocationLimit(), kImageAllocationLimitMb);
    QVERIFY2(!QImage(path).isNull(), "raiseImageAllocationLimit() did not lift the limit");
}

// The file that found this: a 7200x5400 16-bit TIFF from a Leica Q3. Qt expands
// 16-bit RGB to RGBA64, so the decoded buffer is 8 bytes a pixel.
void ImageFormatsTest::allocationLimitCoversFullResolutionFiles() {
    const qint64 leicaQ3Mb = qint64(7200) * 5400 * 8 / (1024 * 1024);
    QVERIFY2(kImageAllocationLimitMb > leicaQ3Mb,
             qPrintable(QStringLiteral("limit is %1 MB, needs to exceed %2 MB")
                            .arg(kImageAllocationLimitMb).arg(leicaQ3Mb)));
}

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
