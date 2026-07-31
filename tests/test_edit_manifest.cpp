// Tests for the centralized edit manifest: the common ImageEdit interface, the
// EditManifest container (ordering / render / serialization), path-keyed
// persistence, and the end-to-end "reopen re-applies saved edits" behavior.
#include <QtTest/QtTest>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QSettings>
#include <QStandardPaths>
#include <QPushButton>
#include <QTemporaryDir>
#include "EditManifest.h"
#include "ImageEdit.h"
#include "MainWindow.h"
#include "RotatePanel.h"
#include "ImageViewer.h"

static QString dims(int w, int h) { return QString("%1 × %2").arg(w).arg(h); }

class EditManifestTest : public QObject {
    Q_OBJECT

private slots:
    void init() { QSettings().clear(); }

    // --- individual edits -------------------------------------------------
    void orientation_rotateSwapsDimensions();
    void orientation_fourRotationsAreIdentity();
    void orientation_doubleFlipIsIdentity();
    void orientation_jsonRoundTrip();
    void crop_appliesNormalizedRegion();
    void crop_normalizedRoundTripsThroughPixels();
    void bw_producesGrayscale();

    // --- the manifest container ------------------------------------------
    void manifest_keepsCanonicalOrder();
    void manifest_renderAppliesEditsInOrder();
    void manifest_summaryJoinsActiveEdits();
    void manifest_jsonRoundTrip();

    // --- persistence ------------------------------------------------------
    void persistence_roundTripsByPath();
    void persistence_emptyManifestClearsEntry();
    void persistence_reappliesSavedEditsOnReopen();

private:
    ImageViewer *viewerOf(MainWindow &w) {
        return w.activeViewer();
    }
    QImage makeImage(int w, int h, QColor c = Qt::darkCyan) {
        QImage img(w, h, QImage::Format_RGB32);
        img.fill(c);
        return img;
    }
};

void EditManifestTest::orientation_rotateSwapsDimensions() {
    OrientationEdit o;
    o.rotateClockwise();
    QImage out = o.apply(makeImage(200, 150));
    QCOMPARE(out.size(), QSize(150, 200));
    QVERIFY(!o.isIdentity());
    QCOMPARE(o.summary(), QStringLiteral("90° rotation"));
}

void EditManifestTest::orientation_fourRotationsAreIdentity() {
    OrientationEdit o;
    for (int i = 0; i < 4; ++i) o.rotateClockwise();
    QVERIFY(o.isIdentity());
    QImage src = makeImage(200, 150);
    QCOMPARE(o.apply(src).size(), src.size());
}

void EditManifestTest::orientation_doubleFlipIsIdentity() {
    OrientationEdit o;
    o.flipHorizontal();
    QVERIFY(!o.isIdentity());
    o.flipHorizontal();
    QVERIFY(o.isIdentity());
}

void EditManifestTest::orientation_jsonRoundTrip() {
    OrientationEdit o;
    o.rotateClockwise();
    o.flipHorizontal();
    QJsonObject j = o.toJson();

    OrientationEdit restored;
    restored.fromJson(j);
    // Same pixels (transform) and same human summary survive the round-trip.
    QImage src = makeImage(80, 40);
    QCOMPARE(restored.apply(src), o.apply(src));
    QCOMPARE(restored.summary(), o.summary());
}

void EditManifestTest::crop_appliesNormalizedRegion() {
    CropEdit c;
    c.setRect(QRectF(0.25, 0.20, 0.5, 0.4));   // of a 200x150 image → (50,30,100,60)
    QImage out = c.apply(makeImage(200, 150));
    QCOMPARE(out.size(), QSize(100, 60));
    QVERIFY(!c.isFull());
}

void EditManifestTest::crop_normalizedRoundTripsThroughPixels() {
    const QSize size(200, 150);
    const QRectF px(50, 30, 100, 60);
    QRectF n = CropEdit::toNormalized(px, size);
    QCOMPARE(CropEdit::toPixels(n, size), px.toRect());
}

void EditManifestTest::bw_producesGrayscale() {
    BwEdit b;
    QImage src(8, 8, QImage::Format_RGB32);
    src.fill(QColor(200, 50, 50));
    QImage out = b.apply(src);
    QVERIFY(!out.isNull());
    QCOMPARE(out.size(), src.size());
    // A grayscale result has equal channels.
    QColor px = out.pixelColor(4, 4);
    QCOMPARE(px.red(), px.green());
    QCOMPARE(px.green(), px.blue());
}

void EditManifestTest::manifest_keepsCanonicalOrder() {
    EditManifest m;
    // Insert out of pipeline order; the manifest must still store them ordered
    // orientation → crop → B&W.
    m.ensureBw();
    m.ensureOrientation();
    m.ensureCrop();
    const auto &edits = m.edits();
    QCOMPARE(int(edits.size()), 3);
    QCOMPARE(edits[0]->type(), QStringLiteral("orientation"));
    QCOMPARE(edits[1]->type(), QStringLiteral("crop"));
    QCOMPARE(edits[2]->type(), QStringLiteral("bw"));
}

void EditManifestTest::manifest_renderAppliesEditsInOrder() {
    EditManifest m;
    m.ensureOrientation().rotateClockwise();          // 200x150 → 150x200
    m.ensureCrop().setRect(QRectF(0, 0, 1.0, 0.5));    // top half → 150x100
    QImage out = m.render(makeImage(200, 150));
    QCOMPARE(out.size(), QSize(150, 100));
}

void EditManifestTest::manifest_summaryJoinsActiveEdits() {
    EditManifest m;
    m.ensureOrientation().rotateClockwise();
    m.ensureCrop().setRect(QRectF(0.1, 0.1, 0.5, 0.5));
    m.ensureBw();
    QCOMPARE(m.summary(), QStringLiteral("90° rotation · crop · B&W"));
}

void EditManifestTest::manifest_jsonRoundTrip() {
    EditManifest m;
    m.ensureOrientation().rotateClockwise();
    m.ensureCrop().setRect(QRectF(0.1, 0.2, 0.5, 0.6));
    BwParams p;
    p.look = BwLook::Film;
    p.reds = 25;
    p.contrast = -10;
    m.ensureBw().setParams(p);

    EditManifest restored = EditManifest::fromJson(m.toJson());
    QImage src = makeImage(200, 150);
    // The whole pipeline reproduces identical pixels after a JSON round-trip.
    QCOMPARE(restored.render(src), m.render(src));
    QCOMPARE(restored.summary(), m.summary());
    QVERIFY(restored.bw());
    QCOMPARE(int(restored.bw()->params().look), int(BwLook::Film));
    QCOMPARE(restored.bw()->params().reds, 25);
    QCOMPARE(restored.bw()->params().contrast, -10);
}

void EditManifestTest::persistence_roundTripsByPath() {
    const QString path = QStringLiteral("/some/folder/picture.jpg");
    EditManifest m;
    m.ensureOrientation().rotateClockwise();
    m.ensureCrop().setRect(QRectF(0.1, 0.2, 0.5, 0.6));
    m.saveFor(path);

    EditManifest loaded = EditManifest::loadFor(path);
    QImage src = makeImage(200, 150);
    QCOMPARE(loaded.render(src), m.render(src));
    // A different path has no stored manifest.
    QVERIFY(EditManifest::loadFor(QStringLiteral("/some/folder/other.jpg")).isEmpty());
}

void EditManifestTest::persistence_emptyManifestClearsEntry() {
    const QString path = QStringLiteral("/folder/img.jpg");
    EditManifest m;
    m.ensureCrop().setRect(QRectF(0.1, 0.1, 0.5, 0.5));
    m.saveFor(path);
    QVERIFY(!EditManifest::loadFor(path).isEmpty());

    EditManifest empty;
    empty.saveFor(path);   // saving empty clears the stored entry
    QVERIFY(EditManifest::loadFor(path).isEmpty());
}

// The headline behavior: edits made in one window are remembered and re-applied
// when the same path is opened again.
void EditManifestTest::persistence_reappliesSavedEditsOnReopen() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = QDir(tmp.path()).absoluteFilePath("photo.png");
    QVERIFY(makeImage(200, 150).save(path, "PNG"));

    // Session 1: rotate, crop, and convert to B&W.
    {
        MainWindow w(path);
        w.resize(400, 300);
        w.show();
        QCoreApplication::processEvents();
        ImageViewer *v = viewerOf(w);
        QVERIFY(v);

        // Rotate mode, then a quarter turn from its panel → oriented 150x200.
        QTest::keyClick(v, Qt::Key_R);
        w.rotatePanel()->rotateRightButton()->click();
        QTest::keyClick(v, Qt::Key_R);                 // leave rotate mode

        v->setCropMode(true);
        v->setCropRect(QRectF(10, 20, 60, 100));       // crop in oriented space
        v->setCropMode(false);
        QTest::keyClick(v, Qt::Key_W);                 // activate B&W
        QCoreApplication::processEvents();

        QCOMPARE(int(w.manifest().edits().size()), 3);
    }

    // Session 2: same path → the manifest is loaded and re-applied.
    {
        MainWindow w2(path);
        w2.resize(400, 300);
        w2.show();
        QCoreApplication::processEvents();

        QCOMPARE(int(w2.manifest().edits().size()), 3);

        auto data = w2.imageStateData();
        const QString edits = data.value("State_Edits");
        QVERIFY(edits.contains("rotation"));
        QVERIFY(edits.contains("crop"));
        QVERIFY(edits.contains("B&W"));

        // Original dimensions preserved; current dimensions reflect rotate+crop.
        QCOMPARE(data.value("Dimensions"), dims(200, 150));
        QCOMPARE(data.value("CurrentDimensions"), "→ " + dims(60, 100));
    }
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    // Isolate QSettings (the manifest store) into a throwaway sandbox.
    QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName(QStringLiteral("photo-salon-test"));
    app.setApplicationName(QStringLiteral("photo-salon-test"));
    EditManifestTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_edit_manifest.moc"
