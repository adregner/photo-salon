#include <QtTest/QtTest>
#include <QApplication>
#include "BwConverter.h"

class BwConverterTest : public QObject {
    Q_OBJECT

private slots:
    void neutralParamsMatchBT709Luminosity();
    void grayPixelUnaffectedBySliders();
    void positiveSlidersLightenMatchingHue();
    void negativeSlidersBarkenMatchingHue();
    void outputFormatIsGrayscale16();
    void autoParamsReturnValidRange();
    void autoParamsSkipAbsentBands();
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

void BwConverterTest::neutralParamsMatchBT709Luminosity() {
    // Pure red pixel: sRGB (255,0,0) → linear (1.0,0,0)
    // BT.709 lum = 0.2126 → round(0.2126 * 65535) = 13933
    QImage img = makeSolid(1, 1, qRgb(255, 0, 0));
    BwParams p;
    QImage out = BwConverter::convert(img, p);
    QCOMPARE(out.format(), QImage::Format_Grayscale16);
    int val = readGray16(out, 0, 0);
    QVERIFY2(qAbs(val - 13933) <= 2, qPrintable(QString("expected ~13933 got %1").arg(val)));
}

void BwConverterTest::grayPixelUnaffectedBySliders() {
    // R=G=B=128: sat_hsv = 0, so sliders must have zero effect
    QImage img = makeSolid(1, 1, qRgb(128, 128, 128));
    BwParams p;
    p.reds = 100; p.blues = -100; p.greens = 50;
    QImage neutral; { BwParams n; neutral = BwConverter::convert(img, n); }
    QImage adjusted = BwConverter::convert(img, p);
    QCOMPARE(readGray16(neutral,  0, 0),
             readGray16(adjusted, 0, 0));
}

void BwConverterTest::positiveSlidersLightenMatchingHue() {
    // Pure green pixel (hue ≈ 120°): increasing greens slider should raise output
    QImage img = makeSolid(1, 1, qRgb(0, 255, 0));
    BwParams neutral, boosted;
    boosted.greens = 80;
    QImage outN = BwConverter::convert(img, neutral);
    QImage outB = BwConverter::convert(img, boosted);
    int vN = readGray16(outN, 0, 0);
    int vB = readGray16(outB, 0, 0);
    QVERIFY2(vB > vN, qPrintable(QString("expected boosted (%1) > neutral (%2)").arg(vB).arg(vN)));
}

void BwConverterTest::negativeSlidersBarkenMatchingHue() {
    // Pure blue pixel (hue ≈ 240°): decreasing blues slider should lower output
    QImage img = makeSolid(1, 1, qRgb(0, 0, 255));
    BwParams neutral, darkened;
    darkened.blues = -80;
    QImage outN = BwConverter::convert(img, neutral);
    QImage outD = BwConverter::convert(img, darkened);
    int vN = readGray16(outN, 0, 0);
    int vD = readGray16(outD, 0, 0);
    QVERIFY2(vD < vN, qPrintable(QString("expected darkened (%1) < neutral (%2)").arg(vD).arg(vN)));
}

void BwConverterTest::outputFormatIsGrayscale16() {
    QImage img = makeSolid(10, 10, qRgb(100, 150, 200));
    QImage out = BwConverter::convert(img, BwParams{});
    QCOMPARE(out.format(), QImage::Format_Grayscale16);
    QCOMPARE(out.size(), img.size());
}

void BwConverterTest::autoParamsReturnValidRange() {
    // Any image should produce params in [-100, 100]
    QImage img(64, 64, QImage::Format_RGB32);
    // Fill with a gradient of colors
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            img.setPixel(x, y, qRgb(x * 4, y * 4, (x + y) * 2));

    BwParams p = BwConverter::autoParams(img);
    QVERIFY(p.reds     >= -100 && p.reds     <= 100);
    QVERIFY(p.yellows  >= -100 && p.yellows  <= 100);
    QVERIFY(p.greens   >= -100 && p.greens   <= 100);
    QVERIFY(p.cyans    >= -100 && p.cyans    <= 100);
    QVERIFY(p.blues    >= -100 && p.blues    <= 100);
    QVERIFY(p.magentas >= -100 && p.magentas <= 100);
}

void BwConverterTest::autoParamsSkipAbsentBands() {
    // Pure red image — only the reds band is populated; others should stay 0
    QImage img = makeSolid(20, 20, qRgb(255, 0, 0));
    BwParams p = BwConverter::autoParams(img);
    QCOMPARE(p.yellows,  0);
    QCOMPARE(p.greens,   0);
    QCOMPARE(p.cyans,    0);
    QCOMPARE(p.blues,    0);
    QCOMPARE(p.magentas, 0);
}

void BwConverterTest::convertHandlesRGB32AndRGB888Input() {
    QImage rgb32(4, 4, QImage::Format_RGB32);
    rgb32.fill(qRgb(200, 100, 50));
    QImage rgb888 = rgb32.convertToFormat(QImage::Format_RGB888);

    QImage out32  = BwConverter::convert(rgb32,  BwParams{});
    QImage out888 = BwConverter::convert(rgb888, BwParams{});

    // Both should produce the same grayscale value
    QCOMPARE(readGray16(out32,  0, 0),
             readGray16(out888, 0, 0));
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    BwConverterTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_bw_converter.moc"
