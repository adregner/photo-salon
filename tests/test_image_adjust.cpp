// Tests for the light/tone and colour-balance edits: the pixel maths in
// ImageAdjust, the AdjustEdit/ColorEdit wrappers (JSON, summaries, neutrality),
// and their placement in the EditManifest pipeline.
#include <QtTest/QtTest>
#include <QApplication>
#include <QImage>
#include "EditManifest.h"
#include "ImageAdjust.h"
#include "ImageEdit.h"

class ImageAdjustTest : public QObject {
    Q_OBJECT

private slots:
    void neutral_isIdentity();
    void brightness_lightensMidGrey();
    void exposure_negativeDarkens();
    void saturation_zeroGrays();
    void temperature_warmsTowardRed();
    void channelGain_boostsThatChannel();
    void hueBand_boostsMatchingHueSaturation();
    void hueBand_leavesOtherHuesAndNeutralsAlone();
    void blacks_upLightensDownDarkens();
    void edits_jsonRoundTrip();
    void manifest_keepsAdjustColorBetweenCropAndBw();
    void manifest_renderAfterCropSkipsOrientationAndCrop();

private:
    QImage solid(int w, int h, QColor c) {
        QImage img(w, h, QImage::Format_RGB32);
        img.fill(c);
        return img;
    }
};

void ImageAdjustTest::neutral_isIdentity() {
    QImage src = solid(8, 8, QColor(120, 130, 140));
    QCOMPARE(ImageAdjust::applyTone(src, AdjustParams{}), src);
    QCOMPARE(ImageAdjust::applyColor(src, ColorParams{}), src);
    QVERIFY(ImageAdjust::isNeutral(AdjustParams{}));
    QVERIFY(ImageAdjust::isNeutral(ColorParams{}));
}

void ImageAdjustTest::brightness_lightensMidGrey() {
    QImage src = solid(4, 4, QColor(128, 128, 128));
    AdjustParams p; p.brightness = 50;
    QColor out = ImageAdjust::applyTone(src, p).pixelColor(2, 2);
    QVERIFY(out.red() > 128);
    QCOMPARE(out.red(), out.green());   // tone is applied per channel, equally
    QCOMPARE(out.green(), out.blue());
}

void ImageAdjustTest::exposure_negativeDarkens() {
    QImage src = solid(4, 4, QColor(200, 200, 200));
    AdjustParams p; p.exposure = -100;   // −1 stop ≈ half
    QColor out = ImageAdjust::applyTone(src, p).pixelColor(1, 1);
    QVERIFY(out.red() < 200);
    QVERIFY(out.red() <= 120);           // roughly halved
}

void ImageAdjustTest::saturation_zeroGrays() {
    QImage src = solid(4, 4, QColor(200, 60, 60));
    AdjustParams p; p.saturation = -100;   // fully desaturate → grey
    QColor out = ImageAdjust::applyTone(src, p).pixelColor(1, 1);
    QVERIFY(qAbs(out.red() - out.green()) <= 1);
    QVERIFY(qAbs(out.green() - out.blue()) <= 1);
}

void ImageAdjustTest::temperature_warmsTowardRed() {
    QImage src = solid(4, 4, QColor(128, 128, 128));
    ColorParams p; p.temperature = 100;   // warm: more red, less blue
    QColor out = ImageAdjust::applyColor(src, p).pixelColor(1, 1);
    QVERIFY(out.red() > 128);
    QVERIFY(out.blue() < 128);
}

void ImageAdjustTest::channelGain_boostsThatChannel() {
    QImage src = solid(4, 4, QColor(100, 100, 100));
    ColorParams p; p.green = 100;
    QColor out = ImageAdjust::applyColor(src, p).pixelColor(1, 1);
    QVERIFY(out.green() > 100);
    QCOMPARE(out.red(), 100);
    QCOMPARE(out.blue(), 100);
}

// Pushing the Blue band's slider up must deepen the saturation of a blue pixel.
void ImageAdjustTest::hueBand_boostsMatchingHueSaturation() {
    // Find which band index is "Blue" (≈225°) so the test is independent of order.
    int blueIdx = -1;
    for (int i = 0; i < ImageAdjust::hueBandCount(); ++i)
        if (QString(ImageAdjust::hueBand(i).name) == "Blue") blueIdx = i;
    QVERIFY(blueIdx >= 0);

    QImage src = solid(4, 4, QColor(120, 140, 210));   // a muted blue
    ColorParams p;
    p.hues[blueIdx] = 100;                              // vivify blues
    QColor in  = src.pixelColor(1, 1);
    QColor out = ImageAdjust::applyColor(src, p).pixelColor(1, 1);
    QVERIFY(out.saturation() > in.saturation());        // HSV saturation rises
    QCOMPARE(out.hue(), in.hue());                      // hue is preserved
}

// A red-band boost leaves a green pixel and a grey pixel untouched.
void ImageAdjustTest::hueBand_leavesOtherHuesAndNeutralsAlone() {
    int redIdx = -1;
    for (int i = 0; i < ImageAdjust::hueBandCount(); ++i)
        if (QString(ImageAdjust::hueBand(i).name) == "Red") redIdx = i;
    QVERIFY(redIdx >= 0);

    ColorParams p;
    p.hues[redIdx] = 100;

    QImage green = solid(4, 4, QColor(40, 200, 60));
    QCOMPARE(ImageAdjust::applyColor(green, p).pixelColor(1, 1), green.pixelColor(1, 1));

    QImage grey = solid(4, 4, QColor(128, 128, 128));   // no hue → nothing to adjust
    QCOMPARE(ImageAdjust::applyColor(grey, p).pixelColor(1, 1), grey.pixelColor(1, 1));
}

void ImageAdjustTest::edits_jsonRoundTrip() {
    AdjustEdit a;
    AdjustParams ap; ap.brightness = 10; ap.contrast = -20; ap.exposure = 5;
    ap.saturation = 30; ap.blacks = -15; ap.whites = 40;
    a.setParams(ap);

    AdjustEdit ra;
    ra.fromJson(a.toJson());
    QImage src = solid(16, 16, QColor(90, 140, 200));
    QCOMPARE(ra.apply(src), a.apply(src));
    // The summary lists the specific, non-zero adjustments (not a bare label).
    QCOMPARE(ra.summary(), a.summary());
    QVERIFY(ra.summary().contains("brightness +10"));
    QVERIFY(ra.summary().contains("blacks -15"));

    ColorEdit c;
    ColorParams cp; cp.temperature = -25; cp.tint = 15; cp.red = 8; cp.green = -8; cp.blue = 20;
    cp.hues[0] = 60; cp.hues[3] = -40; cp.hues[7] = 25;
    c.setParams(cp);

    ColorEdit rc;
    rc.fromJson(c.toJson());
    QCOMPARE(rc.apply(src), c.apply(src));
    QCOMPARE(rc.summary(), c.summary());
    QVERIFY(rc.summary().contains("temp -25"));
    QVERIFY(rc.summary().contains("sat"));   // a per-hue band is named with a "sat" suffix
}

// Blacks: up (+) lifts shadows lighter; down (−) crushes them darker.
void ImageAdjustTest::blacks_upLightensDownDarkens() {
    QImage src = solid(4, 4, QColor(40, 40, 40));   // a dark grey
    AdjustParams up;   up.blacks = 80;
    AdjustParams down; down.blacks = -80;
    const int base = src.pixelColor(1, 1).red();
    QVERIFY(ImageAdjust::applyTone(src, up).pixelColor(1, 1).red()   > base);
    QVERIFY(ImageAdjust::applyTone(src, down).pixelColor(1, 1).red() < base);
}

void ImageAdjustTest::manifest_keepsAdjustColorBetweenCropAndBw() {
    EditManifest m;
    // Insert out of order; the manifest must store orientation → crop → adjust → color → bw.
    m.ensureBw();
    m.ensureColor();
    m.ensureAdjust();
    m.ensureCrop();
    m.ensureOrientation();
    const auto &e = m.edits();
    QCOMPARE(int(e.size()), 5);
    QCOMPARE(e[0]->type(), QStringLiteral("orientation"));
    QCOMPARE(e[1]->type(), QStringLiteral("crop"));
    QCOMPARE(e[2]->type(), QStringLiteral("adjust"));
    QCOMPARE(e[3]->type(), QStringLiteral("color"));
    QCOMPARE(e[4]->type(), QStringLiteral("bw"));
}

void ImageAdjustTest::manifest_renderAfterCropSkipsOrientationAndCrop() {
    EditManifest m;
    m.ensureCrop().setRect(QRectF(0, 0, 0.5, 1.0));   // would halve the width…
    AdjustParams ap; ap.brightness = 40;
    m.ensureAdjust().setParams(ap);

    QImage base = solid(20, 10, QColor(120, 120, 120));
    QImage out  = m.renderAfterCrop(base);
    // renderAfterCrop must NOT re-apply the crop (base is already post-crop): size
    // is preserved, but the tone edit is applied.
    QCOMPARE(out.size(), base.size());
    QVERIFY(out.pixelColor(5, 5).red() > 120);
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    ImageAdjustTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_image_adjust.moc"
