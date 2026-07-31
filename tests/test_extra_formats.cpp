#include <QDir>
#include <QImage>
#include <QImageReader>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "ExifReader.h"
#include "ImageFormats.h"
#include "ImageViewer.h"

// Either back-end is optional at build time. These have to be macros, not helper
// functions: QSKIP only aborts the function it sits in.
#ifdef PHOTO_SALON_HAVE_HEIF
#define SKIP_UNLESS_HEIF() do {} while (false)
#else
#define SKIP_UNLESS_HEIF() QSKIP("built without libheif")
#endif

#ifdef PHOTO_SALON_HAVE_JPEG2000
#define SKIP_UNLESS_JPEG2000() do {} while (false)
#else
#define SKIP_UNLESS_JPEG2000() QSKIP("built without OpenJPEG")
#endif

// Decoding of the formats Qt's own plugins don't cover: HEIF/HEIC (libheif) and
// the JPEG 2000 family (OpenJPEG), both registered as static image plugins.
class ExtraFormatsTest : public QObject {
    Q_OBJECT

private slots:
    void heicFormatIsRegistered();
    void jpegFormatsAreRegistered();
    void heicExtensionsAreOffered();
    void jpegFormatExtensionsAreOffered();
    void readsHeic();
    void readsHeicSizeWithoutDecoding();
    void readsJpf();
    void readsRawCodestream();
    void readsGrayscaleJp2();
    void detectsFormatFromContent();
    void rejectsNonImageData();
    void readsExifFromHeic();
    void viewerDisplaysHeic();
    void folderNavigationReachesNewFormats();

private:
    static QString sample(const QString &name) {
        return QStringLiteral(EXTRA_FORMAT_SAMPLES_DIR "/") + name;
    }
    // The fixtures are lossy-encoded, so channels are compared loosely.
    static bool closeTo(int actual, int expected, int tolerance = 24) {
        return qAbs(actual - expected) <= tolerance;
    }
};

void ExtraFormatsTest::heicFormatIsRegistered() {
    SKIP_UNLESS_HEIF();
    const auto formats = QImageReader::supportedImageFormats();
    QVERIFY(formats.contains("heic"));
    QVERIFY(formats.contains("heif"));
}

void ExtraFormatsTest::jpegFormatsAreRegistered() {
    SKIP_UNLESS_JPEG2000();
    const auto formats = QImageReader::supportedImageFormats();
    QVERIFY(formats.contains("jpf"));
    QVERIFY(formats.contains("jp2"));
    QVERIFY(formats.contains("j2k"));
}

// The dialogs, folder navigation and the macOS NSOpenPanel all key off this list.
void ExtraFormatsTest::heicExtensionsAreOffered() {
    SKIP_UNLESS_HEIF();
    QVERIFY(supportedExtensions().contains("*.heic"));
    QVERIFY(supportedFileFilter().contains("*.heic"));
}

void ExtraFormatsTest::jpegFormatExtensionsAreOffered() {
    SKIP_UNLESS_JPEG2000();
    QVERIFY(supportedExtensions().contains("*.jpf"));
    QVERIFY(supportedFileFilter().contains("*.jpf"));
}

void ExtraFormatsTest::readsHeic() {
    SKIP_UNLESS_HEIF();
    QImageReader reader(sample("gradient.heic"));
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    QVERIFY2(!image.isNull(), qPrintable(reader.errorString()));
    QCOMPARE(image.size(), QSize(64, 48));

    // Source gradient: R = (x*4) % 256, G = (y*5) % 256, B = 128.
    const QColor topLeft = image.pixelColor(0, 0);
    QVERIFY(closeTo(topLeft.red(), 0));
    QVERIFY(closeTo(topLeft.green(), 0));
    QVERIFY(closeTo(topLeft.blue(), 128));

    const QColor mid = image.pixelColor(32, 24);
    QVERIFY(closeTo(mid.red(), 128));
    QVERIFY(closeTo(mid.green(), 120));
    QVERIFY(closeTo(mid.blue(), 128));
}

void ExtraFormatsTest::readsHeicSizeWithoutDecoding() {
    SKIP_UNLESS_HEIF();
    QImageReader reader(sample("gradient.heic"));
    QCOMPARE(reader.size(), QSize(64, 48));
}

void ExtraFormatsTest::readsJpf() {
    SKIP_UNLESS_JPEG2000();
    QImageReader reader(sample("gradient.jpf"));
    const QImage image = reader.read();
    QVERIFY2(!image.isNull(), qPrintable(reader.errorString()));
    QCOMPARE(image.size(), QSize(64, 48));

    const QColor mid = image.pixelColor(32, 24);
    QVERIFY(closeTo(mid.red(), 128));
    QVERIFY(closeTo(mid.green(), 120));
    QVERIFY(closeTo(mid.blue(), 128));
}

void ExtraFormatsTest::readsRawCodestream() {
    SKIP_UNLESS_JPEG2000();
    const QImage image = QImageReader(sample("gradient.j2k")).read();
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(64, 48));
}

void ExtraFormatsTest::readsGrayscaleJp2() {
    SKIP_UNLESS_JPEG2000();
    const QImage image = QImageReader(sample("gray.jp2")).read();
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(64, 48));
    // Single-component images expand to gray: R == G == B.
    const QColor mid = image.pixelColor(32, 24);
    QVERIFY(closeTo(mid.red(), 128));
    QCOMPARE(mid.green(), mid.red());
    QCOMPARE(mid.blue(), mid.red());
}

// A misnamed file still opens: both handlers sniff the file signature.
void ExtraFormatsTest::detectsFormatFromContent() {
    SKIP_UNLESS_HEIF();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString misnamed = dir.filePath("mystery.dat");
    QVERIFY(QFile::copy(sample("gradient.heic"), misnamed));

    const QImage image = QImageReader(misnamed).read();
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(64, 48));
}

void ExtraFormatsTest::rejectsNonImageData() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("not-an-image.heic");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(4096, 'x'));
    file.close();

    QVERIFY(QImageReader(path).read().isNull());
}

// HEIF stores EXIF in a metadata item, not a JPEG APP1 segment — the overlay
// depends on ExifReader digging it out anyway.
void ExtraFormatsTest::readsExifFromHeic() {
    SKIP_UNLESS_HEIF();
    const auto data = ExifReader::read(sample("fujifilm-exif.heic"));
    QCOMPARE(data["FileName"], QString("fujifilm-exif.heic"));
    QCOMPARE(data["Make"], QString("FUJIFILM"));
    QVERIFY(data.contains("Model"));
    QVERIFY(data.contains("Dimensions"));
}

void ExtraFormatsTest::viewerDisplaysHeic() {
    SKIP_UNLESS_HEIF();
    ImageViewer viewer(sample("gradient.heic"));
    QCOMPARE(viewer.currentPath(), sample("gradient.heic"));
    QCOMPARE(viewer.pixmap().size(), QSize(64, 48));
}

// ←/→ walks the folder using supportedExtensions(), so the new formats have to
// be part of that list for a mixed folder to navigate at all.
void ExtraFormatsTest::folderNavigationReachesNewFormats() {
    SKIP_UNLESS_HEIF();
    SKIP_UNLESS_JPEG2000();
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QDir dir(tmp.path());
    const QString heic = dir.absoluteFilePath("a.heic");
    const QString jpf  = dir.absoluteFilePath("b.jpf");
    QVERIFY(QFile::copy(sample("gradient.heic"), heic));
    QVERIFY(QFile::copy(sample("gradient.jpf"), jpf));

    ImageViewer viewer(heic);
    QTest::keyClick(&viewer, Qt::Key_Right);
    QCOMPARE(viewer.currentPath(), jpf);
    QCOMPARE(viewer.pixmap().size(), QSize(64, 48));

    QTest::keyClick(&viewer, Qt::Key_Right);   // wraps back around
    QCOMPARE(viewer.currentPath(), heic);
}

QTEST_MAIN(ExtraFormatsTest)
#include "test_extra_formats.moc"
