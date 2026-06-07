#include <QtTest/QtTest>
#include <QApplication>
#include <cmath>
#include "BwConverter.h"

class BwConverterTest : public QObject {
    Q_OBJECT

private slots:
    void neutralPureRedReencodedToSrgb();
    void neutralGreyRoundTrips();
    void grayPixelUnaffectedByBands();
    void positiveBandLightensMatchingHue();
    void negativeBandDarkensMatchingHue();
    void outputFormatIsGrayscale16();
    void looksRenderColorsDifferently();
    void contrastSliderSpreadsTones();
    void lookPresetLoadsDefaults();
    void convertHandlesRGB32AndRGB888Input();
};

static QImage makeSolid(int w, int h, QRgb color) {
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(color);
    return img;
}

static int readGray16(const QImage &img, int x, int y) {
    return (int)reinterpret_cast<const uint16_t *>(img.constScanLine(y))[x];
}

static int gray16At(QRgb color, const BwParams &p) {
    return readGray16(BwConverter::convert(makeSolid(1, 1, color), p), 0, 0);
}

static double linToSrgb(double c) {
    return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

void BwConverterTest::neutralPureRedReencodedToSrgb() {
    // Pure red sRGB(255,0,0) -> linear (1,0,0); BT.709 luminance = 0.2126.
    // The fix: the linear luminance is re-encoded to sRGB before storage, so the
    // grey is the perceptually-correct mid value, not the dark 0.2126 raw linear.
    int got = gray16At(qRgb(255, 0, 0), BwParams{});
    int expected = (int)std::round(linToSrgb(0.2126) * 65535.0);
    QVERIFY2(qAbs(got - expected) <= 40,
             qPrintable(QString("expected ~%1 got %2").arg(expected).arg(got)));
    // Sanity: clearly brighter than the old raw-linear result (~13933).
    QVERIFY(got > 25000);
}

void BwConverterTest::neutralGreyRoundTrips() {
    // A neutral grey must come back out essentially unchanged (no gamma error).
    QImage out = BwConverter::convert(makeSolid(1, 1, qRgb(128, 128, 128)), BwParams{});
    int got = readGray16(out, 0, 0);
    int expected = (int)std::round((128.0 / 255.0) * 65535.0);
    QVERIFY2(qAbs(got - expected) <= 60,
             qPrintable(QString("expected ~%1 got %2").arg(expected).arg(got)));
}

void BwConverterTest::grayPixelUnaffectedByBands() {
    // R=G=B has zero saturation, so the hue-band sliders must do nothing.
    QRgb grey = qRgb(128, 128, 128);
    BwParams adjusted;
    adjusted.reds = 100; adjusted.blues = -100; adjusted.greens = 50;
    QCOMPARE(gray16At(grey, BwParams{}), gray16At(grey, adjusted));
}

void BwConverterTest::positiveBandLightensMatchingHue() {
    // Pure red (hue 0). Raising the Reds band lightens it; +40 stays below clip.
    BwParams boosted; boosted.reds = 40;
    QVERIFY(gray16At(qRgb(255, 0, 0), boosted) > gray16At(qRgb(255, 0, 0), BwParams{}));
}

void BwConverterTest::negativeBandDarkensMatchingHue() {
    // Pure blue (hue 240). Lowering the Blues band darkens it.
    BwParams darkened; darkened.blues = -80;
    QVERIFY(gray16At(qRgb(0, 0, 255), darkened) < gray16At(qRgb(0, 0, 255), BwParams{}));
}

void BwConverterTest::outputFormatIsGrayscale16() {
    QImage img = makeSolid(10, 10, qRgb(100, 150, 200));
    QImage out = BwConverter::convert(img, BwParams{});
    QCOMPARE(out.format(), QImage::Format_Grayscale16);
    QCOMPARE(out.size(), img.size());
}

void BwConverterTest::looksRenderColorsDifferently() {
    // A panchromatic sensor (Monochrom) weights red far more than BT.709
    // luminance does, so pure red renders much brighter than Neutral.
    BwParams neutral{};
    BwParams mono = BwConverter::lookPreset(BwLook::Monochrom);
    QVERIFY(gray16At(qRgb(255, 0, 0), mono) > gray16At(qRgb(255, 0, 0), neutral));

    // Classic (Rec.601) and Neutral (BT.709) disagree on a saturated green.
    BwParams classic = BwConverter::lookPreset(BwLook::ClassicLuma);
    QVERIFY(gray16At(qRgb(0, 255, 0), classic) != gray16At(qRgb(0, 255, 0), neutral));
}

void BwConverterTest::contrastSliderSpreadsTones() {
    // Positive contrast pushes a shadow darker and a highlight brighter.
    QRgb dark  = qRgb(64, 64, 64);    // below mid
    QRgb light = qRgb(192, 192, 192); // above mid

    BwParams flat{};                  // Neutral, contrast 0 (identity curve)
    BwParams punchy{}; punchy.contrast = 60;

    QVERIFY(gray16At(dark,  punchy) < gray16At(dark,  flat));
    QVERIFY(gray16At(light, punchy) > gray16At(light, flat));
}

void BwConverterTest::lookPresetLoadsDefaults() {
    // A preset selects the look, zeroes the hue bands, and loads the look's
    // built-in contrast (High Contrast ships with a strong default).
    BwParams p = BwConverter::lookPreset(BwLook::HighContrast);
    QCOMPARE(p.look, BwLook::HighContrast);
    QCOMPARE(p.reds, 0);
    QCOMPARE(p.yellows, 0);
    QCOMPARE(p.greens, 0);
    QCOMPARE(p.cyans, 0);
    QCOMPARE(p.blues, 0);
    QCOMPARE(p.magentas, 0);
    QVERIFY(p.contrast > 0);
}

void BwConverterTest::convertHandlesRGB32AndRGB888Input() {
    QImage rgb32(4, 4, QImage::Format_RGB32);
    rgb32.fill(qRgb(200, 100, 50));
    QImage rgb888 = rgb32.convertToFormat(QImage::Format_RGB888);

    QImage out32  = BwConverter::convert(rgb32,  BwParams{});
    QImage out888 = BwConverter::convert(rgb888, BwParams{});

    QCOMPARE(readGray16(out32,  0, 0),
             readGray16(out888, 0, 0));
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    BwConverterTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_bw_converter.moc"
