// Tests for the histogram: the tone-distribution computation (Histogram::compute
// and the derived readouts) and the overlay's integration into MainWindow — the
// G shortcut, Escape, and the panel tracking whatever image is displayed.
#include <QtTest/QtTest>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include "Histogram.h"
#include "HistogramOverlay.h"
#include "ImageViewer.h"
#include "MainWindow.h"

namespace {

QImage solid(int w, int h, QRgb color) {
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(color);
    return img;
}

// Index of the fullest bin in one channel.
int peakBin(const std::array<quint32, HistogramData::Bins> &bins) {
    int best = 0;
    for (int i = 1; i < HistogramData::Bins; ++i)
        if (bins[i] > bins[best]) best = i;
    return best;
}

}  // namespace

class HistogramTest : public QObject {
    Q_OBJECT

private slots:
    void init() { QSettings().clear(); }   // isolate from saved manifests

    // --- computation ---
    void compute_nullImage_isEmpty();
    void compute_solidGrey_countsEveryPixelInOneBin();
    void compute_pureRed_separatesChannelsAndWeightsLuma();
    void compute_subsamplesLargeImagesButKeepsShape();
    void compute_convertsUnusualFormats();
    void plotCeiling_ignoresTheEndBinSpike();
    void clipping_reportsPinnedPixelsPerChannel();

    // --- overlay / MainWindow integration ---
    void gKey_togglesThePanel();
    void escape_dismissesThePanel();
    void panel_measuresTheDisplayedImage();
    void panel_followsNavigationToAnotherImage();
    void panel_measuresEditedPixels_notTheFileOnDisk();
};

void HistogramTest::compute_nullImage_isEmpty() {
    HistogramData h = Histogram::compute(QImage{});
    QVERIFY(h.isEmpty());
    QCOMPARE(h.sampleCount, quint64(0));
    QCOMPARE(h.plotCeiling(), quint32(1));   // never zero: safe to divide by
    QCOMPARE(h.clippedShadows(), 0.0);
    QCOMPARE(h.clippedHighlights(), 0.0);
}

void HistogramTest::compute_solidGrey_countsEveryPixelInOneBin() {
    HistogramData h = Histogram::compute(solid(40, 30, qRgb(128, 128, 128)));

    QCOMPARE(h.sampleCount, quint64(40 * 30));
    QCOMPARE(h.red[128],   quint32(40 * 30));
    QCOMPARE(h.green[128], quint32(40 * 30));
    QCOMPARE(h.blue[128],  quint32(40 * 30));
    QCOMPARE(h.luma[128],  quint32(40 * 30));   // neutral grey → same luma
}

void HistogramTest::compute_pureRed_separatesChannelsAndWeightsLuma() {
    HistogramData h = Histogram::compute(solid(20, 20, qRgb(255, 0, 0)));
    const quint32 n = 20 * 20;

    QCOMPARE(h.red[255],  n);
    QCOMPARE(h.green[0],  n);
    QCOMPARE(h.blue[0],   n);
    // Rec.709 luma: pure red is dark, ~21 % — (255*54)>>8 == 53.
    QCOMPARE(h.luma[53], n);

    // Pure green is the brightest primary, pure blue the darkest.
    QCOMPARE(peakBin(Histogram::compute(solid(8, 8, qRgb(0, 255, 0))).luma), 182);
    QCOMPARE(peakBin(Histogram::compute(solid(8, 8, qRgb(0, 0, 255))).luma), 18);
}

void HistogramTest::compute_subsamplesLargeImagesButKeepsShape() {
    // Half black, half white: the shape must survive sub-sampling even though
    // only a fraction of the pixels are visited.
    QImage img(1000, 800, QImage::Format_RGB32);
    img.fill(Qt::black);
    for (int y = 0; y < 400; ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) line[x] = qRgb(255, 255, 255);
    }

    HistogramData h = Histogram::compute(img, /*maxSamples=*/10000);

    QVERIFY(h.sampleCount > 0);
    QVERIFY(h.sampleCount < quint64(1000) * 800);          // it really sub-sampled
    QCOMPARE(h.luma[0] + h.luma[255], h.sampleCount);      // only the two tones
    // Both halves are sampled evenly — the grid is uniform, so each lands within
    // one sampled row of half the total.
    const double half = h.sampleCount / 2.0;
    QVERIFY(qAbs(h.luma[0]   - half) / half < 0.05);
    QVERIFY(qAbs(h.luma[255] - half) / half < 0.05);

    // Sampling everything gives the exact counts.
    HistogramData full = Histogram::compute(img, /*maxSamples=*/0);
    QCOMPARE(full.sampleCount, quint64(1000) * 800);
    QCOMPARE(full.luma[255], quint32(1000 * 400));
}

void HistogramTest::compute_convertsUnusualFormats() {
    // B&W renders arrive as Grayscale16 — they must count, not be dropped.
    QImage grey = solid(16, 16, qRgb(200, 200, 200)).convertToFormat(QImage::Format_Grayscale16);
    HistogramData h = Histogram::compute(grey);

    QCOMPARE(h.sampleCount, quint64(16 * 16));
    QVERIFY(qAbs(peakBin(h.luma) - 200) <= 1);   // 16-bit round-trip may shift by one
    QCOMPARE(peakBin(h.red), peakBin(h.blue));   // grey: all channels equal
}

void HistogramTest::plotCeiling_ignoresTheEndBinSpike() {
    // A dark frame with a small bright detail: most pixels pile into bin 0. The
    // ceiling must come from the interior bins, or the detail would be invisible.
    QImage img(100, 100, QImage::Format_RGB32);
    img.fill(Qt::black);
    for (int y = 0; y < 10; ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < 20; ++x) line[x] = qRgb(160, 160, 160);
    }

    HistogramData h = Histogram::compute(img, /*maxSamples=*/0);
    QCOMPARE(h.luma[0], quint32(100 * 100 - 200));
    QCOMPARE(h.plotCeiling(), quint32(200));   // the interior peak, not 9800

    // With nothing but end-bin pixels there is still a usable ceiling.
    QCOMPARE(Histogram::compute(solid(10, 10, qRgb(0, 0, 0))).plotCeiling(), quint32(100));
}

void HistogramTest::clipping_reportsPinnedPixelsPerChannel() {
    QImage img(100, 100, QImage::Format_RGB32);
    img.fill(qRgb(120, 120, 120));
    // 10 % of the frame blows out the red channel only.
    for (int y = 0; y < 10; ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < 100; ++x) line[x] = qRgb(255, 120, 120);
    }

    HistogramData h = Histogram::compute(img, /*maxSamples=*/0);
    QVERIFY(qFuzzyCompare(h.clippedHighlights() + 1.0, 0.10 + 1.0));   // worst channel
    QCOMPARE(h.clippedShadows(), 0.0);

    // A frame with crushed blacks reports on the other end.
    QImage dark(50, 50, QImage::Format_RGB32);
    dark.fill(Qt::black);
    QCOMPARE(Histogram::compute(dark).clippedShadows(), 1.0);
}

// --- overlay / MainWindow integration -------------------------------------

void HistogramTest::gKey_togglesThePanel() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = QDir(tmp.path()).absoluteFilePath("a.png");
    QVERIFY(solid(120, 90, qRgb(90, 90, 90)).save(path));

    MainWindow w(path);
    w.resize(800, 600);
    w.show();
    QCoreApplication::processEvents();

    auto *panel = w.findChild<HistogramOverlay *>();
    QVERIFY(panel);
    QVERIFY(!panel->isVisible());

    QTest::keyClick(w.activeViewer(), Qt::Key_G);
    QVERIFY(panel->isVisible());
    // A corner panel, not a full-window sheet: the photo stays visible.
    QVERIFY(panel->width()  < w.width()  / 2);
    QVERIFY(panel->height() < w.height() / 2);
    QVERIFY(panel->geometry().right() <= w.width());

    QTest::keyClick(w.activeViewer(), Qt::Key_G);
    QVERIFY(!panel->isVisible());
}

void HistogramTest::escape_dismissesThePanel() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = QDir(tmp.path()).absoluteFilePath("a.png");
    QVERIFY(solid(120, 90, qRgb(90, 90, 90)).save(path));

    MainWindow w(path);
    w.resize(800, 600);
    w.show();
    QCoreApplication::processEvents();

    auto *panel = w.findChild<HistogramOverlay *>();
    QVERIFY(panel);
    QTest::keyClick(w.activeViewer(), Qt::Key_G);
    QVERIFY(panel->isVisible());

    QTest::keyClick(w.activeViewer(), Qt::Key_Escape);
    QVERIFY(!panel->isVisible());
}

void HistogramTest::panel_measuresTheDisplayedImage() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = QDir(tmp.path()).absoluteFilePath("bright.png");
    QVERIFY(solid(200, 150, qRgb(230, 230, 230)).save(path));

    MainWindow w(path);
    w.resize(800, 600);
    w.show();
    QCoreApplication::processEvents();

    auto *panel = w.findChild<HistogramOverlay *>();
    QVERIFY(panel);
    QTest::keyClick(w.activeViewer(), Qt::Key_G);

    QVERIFY(!panel->data().isEmpty());
    QCOMPARE(peakBin(panel->data().luma), 230);
}

void HistogramTest::panel_followsNavigationToAnotherImage() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    QVERIFY(solid(80, 60, qRgb(20, 20, 20)).save(dir.absoluteFilePath("a-dark.png")));
    QVERIFY(solid(80, 60, qRgb(240, 240, 240)).save(dir.absoluteFilePath("b-bright.png")));

    MainWindow w(dir.absoluteFilePath("a-dark.png"));
    w.resize(800, 600);
    w.show();
    QCoreApplication::processEvents();

    auto *panel = w.findChild<HistogramOverlay *>();
    QVERIFY(panel);
    QTest::keyClick(w.activeViewer(), Qt::Key_G);
    QCOMPARE(peakBin(panel->data().luma), 20);

    QTest::keyClick(w.activeViewer(), Qt::Key_Right);   // next image in the folder
    QCOMPARE(QFileInfo(w.activeViewer()->currentPath()).fileName(), QString("b-bright.png"));
    QCOMPARE(peakBin(panel->data().luma), 240);
}

void HistogramTest::panel_measuresEditedPixels_notTheFileOnDisk() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = QDir(tmp.path()).absoluteFilePath("split.png");
    // Left half dark, right half bright: cropping to the right half must move the
    // histogram's peak, proving it reads the edited pixels.
    QImage img(200, 100, QImage::Format_RGB32);
    img.fill(qRgb(30, 30, 30));
    for (int y = 0; y < img.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 100; x < img.width(); ++x) line[x] = qRgb(220, 220, 220);
    }
    QVERIFY(img.save(path));

    MainWindow w(path);
    w.resize(800, 600);
    w.show();
    QCoreApplication::processEvents();

    auto *panel = w.findChild<HistogramOverlay *>();
    QVERIFY(panel);
    QTest::keyClick(w.activeViewer(), Qt::Key_G);
    QVERIFY(panel->data().luma[30]  > 0);        // both halves are counted
    QVERIFY(panel->data().luma[220] > 0);

    ImageViewer *viewer = w.activeViewer();
    viewer->setCropMode(true);
    viewer->setCropRect(QRectF(100, 0, 100, 100));   // keep only the bright half
    viewer->setCropMode(false);
    QCoreApplication::processEvents();

    QCOMPARE(panel->data().luma[30], quint32(0));
    QVERIFY(panel->data().luma[220] > 0);
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    // Keep the manifest store out of the real user configuration.
    QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName(QStringLiteral("photo-salon-test"));
    app.setApplicationName(QStringLiteral("photo-salon-test"));
    HistogramTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_histogram.moc"
