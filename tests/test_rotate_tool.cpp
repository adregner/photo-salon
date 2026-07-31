// Tests for free-angle rotation: the geometry that keeps a selection inside the
// tilted frame, the RotateEdit that carries the angle through the manifest, the
// rotate-mode overlay in ImageViewer, and MainWindow's commit — including the
// headline promise that a rotated photograph comes back fully rectangular with
// no blank corners.
#include <QtTest/QtTest>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include "EditManifest.h"
#include "ImageEdit.h"
#include "ImageViewer.h"
#include "MainWindow.h"
#include "RotateGeometry.h"
#include "RotatePanel.h"

namespace {
const QColor kFill(220, 90, 40);   // a distinctive, definitely-not-black colour

QImage makeImage(int w, int h) {
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(kFill);
    return img;
}

// Drag with the left button held, sending events directly so the sequence is
// deterministic regardless of where the virtual cursor happens to be.
void dragOnViewport(QWidget *vp, const QPoint &from, const QPoint &to) {
    const QPointF gFrom = vp->mapToGlobal(from);
    const QPointF gTo   = vp->mapToGlobal(to);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(from), gFrom,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    qApp->sendEvent(vp, &press);
    QMouseEvent move(QEvent::MouseMove, QPointF(to), gTo,
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    qApp->sendEvent(vp, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(to), gTo,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    qApp->sendEvent(vp, &release);
}

void hoverViewport(QWidget *vp, const QPoint &at) {
    QMouseEvent move(QEvent::MouseMove, QPointF(at), vp->mapToGlobal(at),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    qApp->sendEvent(vp, &move);
}
}  // namespace

class RotateToolTest : public QObject {
    Q_OBJECT

private slots:
    void init() { QSettings().clear(); }
    void initTestCase();

    // --- geometry ---------------------------------------------------------
    void geometry_boundingBoxGrowsWithAngle();
    void geometry_largestInscribedRectStaysInsideTiltedBounds();
    void geometry_shrinkToFitPullsAnOverhangingRectInside();
    void geometry_remapKeepsSizeAndStaysInside();

    // --- the manifest edit ------------------------------------------------
    void rotateEdit_appliesAngleAndClampsToStraightenRange();
    void rotateEdit_jsonRoundTrip();
    void manifest_rotateSitsBetweenOrientationAndCrop();

    // --- the overlay ------------------------------------------------------
    void rKeyTogglesRotateMode();
    void rotateMode_emitsModeChanged();
    void switchingCropAndRotate_doesNotApplyAnything();
    void escapeFromRotateMode_isHandledByMainWindow();
    void draggingACornerRotatesTheImage();
    void hoveringACornerShowsARotateCursor();
    void rotating_shrinksAnUntouchedSelectionToTheLargestThatFits();
    void rotating_keepsAPlacedSelectionInsideTheTiltedBounds();
    void doubleClickInRotateMode_resetsTheAngle();

    // --- MainWindow commit ------------------------------------------------
    void leavingRotateMode_recordsRotateAndCropEdits();
    void rotatedResultHasNoBlankCorners();
    void quarterTurnFromThePanel_staysInRotateMode();
    void panelSlider_drivesTheAngle();
    void rotationSurvivesReopen();

private:
    QString m_imagePath;
    QTemporaryDir m_dir;
};

void RotateToolTest::initTestCase() {
    QVERIFY(m_dir.isValid());
    m_imagePath = QDir(m_dir.path()).absoluteFilePath("photo.png");
    QVERIFY(makeImage(200, 150).save(m_imagePath, "PNG"));
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------
void RotateToolTest::geometry_boundingBoxGrowsWithAngle() {
    const QSize size(200, 150);
    QCOMPARE(RotateGeometry::boundingSize(size, 0.0), QSizeF(size));

    const QSizeF tilted = RotateGeometry::boundingSize(size, 20.0);
    QVERIFY(tilted.width()  > size.width());
    QVERIFY(tilted.height() > size.height());
    // 200x150 turned 20°: 200cos20 + 150sin20 wide, 200sin20 + 150cos20 tall.
    QVERIFY(qAbs(tilted.width()  - 239.2) < 1.5);
    QVERIFY(qAbs(tilted.height() - 209.4) < 1.5);
}

void RotateToolTest::geometry_largestInscribedRectStaysInsideTiltedBounds() {
    const QSize size(200, 150);
    for (double angle : {-45.0, -30.0, -7.5, 0.0, 3.0, 12.0, 45.0}) {
        const QRectF inscribed = RotateGeometry::largestInscribedRect(size, angle);
        const QPolygonF bounds = RotateGeometry::rotatedBounds(size, angle);
        QVERIFY2(RotateGeometry::contains(bounds, inscribed),
                 qPrintable(QStringLiteral("escaped the frame at %1°").arg(angle)));
        QVERIFY(inscribed.width() > 0 && inscribed.height() > 0);
        // It really is the *largest*: growing it by 2% must break containment.
        if (!RotateGeometry::isZeroAngle(angle)) {
            QRectF bigger(0, 0, inscribed.width() * 1.02, inscribed.height() * 1.02);
            bigger.moveCenter(inscribed.center());
            QVERIFY(!RotateGeometry::contains(bounds, bigger));
        }
    }
}

void RotateToolTest::geometry_shrinkToFitPullsAnOverhangingRectInside() {
    const QSize size(200, 150);
    const QPolygonF bounds = RotateGeometry::rotatedBounds(size, 15.0);
    const QRectF box = RotateGeometry::boundingSize(size, 15.0).isEmpty()
        ? QRectF() : QRectF(QPointF(0, 0), RotateGeometry::boundingSize(size, 15.0));

    // The whole bounding box overhangs the tilted photograph at every corner.
    QVERIFY(!RotateGeometry::contains(bounds, box));
    const QRectF fitted = RotateGeometry::shrinkToFit(box, bounds);
    QVERIFY(RotateGeometry::contains(bounds, fitted));
    QVERIFY(fitted.width() < box.width());
    // Shrinking is about the centre, and keeps the aspect ratio.
    QVERIFY(qAbs(fitted.center().x() - box.center().x()) < 0.5);
    QVERIFY(qAbs(fitted.width() / fitted.height() - box.width() / box.height()) < 1e-6);

    // Something that already fits is returned untouched.
    const QRectF inside = RotateGeometry::largestInscribedRect(size, 15.0);
    QCOMPARE(RotateGeometry::shrinkToFit(inside, bounds), inside);
}

void RotateToolTest::geometry_remapKeepsSizeAndStaysInside() {
    const QSize size(200, 150);
    // A small selection off to one side, carried from upright to 12°.
    const QRectF from(20, 20, 60, 40);
    const QRectF to = RotateGeometry::remapBetweenAngles(from, size, 0.0, 12.0);
    QCOMPARE(to.size(), from.size());   // small enough that nothing is lost
    QVERIFY(RotateGeometry::contains(RotateGeometry::rotatedBounds(size, 12.0), to));
    QVERIFY(to.center() != from.center());
}

// ---------------------------------------------------------------------------
// RotateEdit
// ---------------------------------------------------------------------------
void RotateToolTest::rotateEdit_appliesAngleAndClampsToStraightenRange() {
    RotateEdit r;
    QVERIFY(r.isIdentity());
    QCOMPARE(r.apply(makeImage(200, 150)).size(), QSize(200, 150));
    QVERIFY(r.summary().isEmpty());

    r.setAngle(10.0);
    QVERIFY(!r.isIdentity());
    const QImage out = r.apply(makeImage(200, 150));
    QVERIFY(out.width()  > 200);
    QVERIFY(out.height() > 150);
    QVERIFY(r.summary().contains(QStringLiteral("straighten")));

    // Whole quarter turns are OrientationEdit's job, so the free angle stops at
    // the straightening range rather than duplicating them.
    r.setAngle(120.0);
    QCOMPARE(r.angle(), RotateGeometry::kMaxAngle);
    r.setAngle(-120.0);
    QCOMPARE(r.angle(), -RotateGeometry::kMaxAngle);
}

void RotateToolTest::rotateEdit_jsonRoundTrip() {
    RotateEdit r;
    r.setAngle(-6.25);
    RotateEdit restored;
    restored.fromJson(r.toJson());
    QCOMPARE(restored.angle(), r.angle());
    QCOMPARE(restored.summary(), r.summary());
}

void RotateToolTest::manifest_rotateSitsBetweenOrientationAndCrop() {
    EditManifest m;
    m.ensureCrop();
    m.ensureBw();
    m.ensureRotate().setAngle(4.0);
    m.ensureOrientation().rotateClockwise();

    const auto &edits = m.edits();
    QCOMPARE(int(edits.size()), 4);
    QCOMPARE(edits[0]->type(), QStringLiteral("orientation"));
    QCOMPARE(edits[1]->type(), QStringLiteral("rotate"));
    QCOMPARE(edits[2]->type(), QStringLiteral("crop"));
    QCOMPARE(edits[3]->type(), QStringLiteral("bw"));

    // The order is the pipeline: the quarter turn transposes 200x150 to 150x200,
    // the free angle then grows that, and the crop takes the middle half.
    const QImage out = m.render(makeImage(200, 150));
    QVERIFY(out.width()  > 0);
    QVERIFY(!m.rotate()->isIdentity());

    m.removeRotate();
    QVERIFY(m.rotate() == nullptr);
    QCOMPARE(int(m.edits().size()), 3);
}

// ---------------------------------------------------------------------------
// The overlay
// ---------------------------------------------------------------------------
void RotateToolTest::rKeyTogglesRotateMode() {
    ImageViewer viewer(m_imagePath);
    QVERIFY(!viewer.rotateMode());
    QVERIFY(!viewer.overlayActive());
    QTest::keyClick(&viewer, Qt::Key_R);
    QVERIFY(viewer.rotateMode());
    QVERIFY(viewer.overlayActive());
    QVERIFY(!viewer.cropMode());
    QTest::keyClick(&viewer, Qt::Key_R);
    QVERIFY(!viewer.rotateMode());
    QVERIFY(!viewer.overlayActive());
}

void RotateToolTest::rotateMode_emitsModeChanged() {
    ImageViewer viewer(m_imagePath);
    QSignalSpy spy(&viewer, &ImageViewer::rotateModeChanged);
    QTest::keyClick(&viewer, Qt::Key_R);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);
    viewer.setRotateMode(false);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toBool(), false);
}

// X and R are separate modes over one shared box: switching between them must
// not apply the selection, only change what a drag does.
void RotateToolTest::switchingCropAndRotate_doesNotApplyAnything() {
    ImageViewer viewer(m_imagePath);   // 200x150
    viewer.resize(400, 300);
    viewer.show();
    QCoreApplication::processEvents();

    QTest::keyClick(&viewer, Qt::Key_X);
    viewer.setCropRect(QRectF(40, 30, 100, 60));

    QTest::keyClick(&viewer, Qt::Key_R);
    QVERIFY(viewer.rotateMode());
    QVERIFY(!viewer.cropMode());
    // Still the full image on screen, and the same box over it.
    QCOMPARE(viewer.currentDisplayPixmap().size(), QSize(200, 150));
    QCOMPARE(viewer.cropRect(), QRectF(40, 30, 100, 60));

    QTest::keyClick(&viewer, Qt::Key_X);
    QVERIFY(viewer.cropMode());
    QCOMPARE(viewer.currentDisplayPixmap().size(), QSize(200, 150));

    // Leaving the overlay altogether is what applies it.
    viewer.closeOverlay();
    QCOMPARE(viewer.currentDisplayPixmap().size(), QSize(100, 60));
}

void RotateToolTest::escapeFromRotateMode_isHandledByMainWindow() {
    MainWindow w(m_imagePath);
    w.resize(600, 400);
    w.show();
    QCoreApplication::processEvents();

    ImageViewer *v = w.activeViewer();
    QTest::keyClick(v, Qt::Key_R);
    QVERIFY(v->rotateMode());
    QVERIFY(w.rotatePanel()->isVisible());

    QTest::keyClick(v, Qt::Key_Escape);
    QVERIFY(!v->rotateMode());
    QVERIFY(!w.rotatePanel()->isVisible());
}

void RotateToolTest::draggingACornerRotatesTheImage() {
    ImageViewer viewer(m_imagePath);   // 200x150
    viewer.resize(600, 450);
    viewer.show();
    QCoreApplication::processEvents();

    QTest::keyClick(&viewer, Qt::Key_R);
    QCOMPARE(viewer.rotateAngle(), 0.0);

    // Grab the top-right corner and pull it down: clockwise around the centre.
    const QPoint corner = viewer.mapFromScene(viewer.cropRect().topRight());
    dragOnViewport(viewer.viewport(), corner, corner + QPoint(-10, 40));
    QVERIFY2(viewer.rotateAngle() > 1.0,
             qPrintable(QStringLiteral("angle was %1").arg(viewer.rotateAngle())));

    // Dragging the same corner back up turns it the other way.
    const double clockwise = viewer.rotateAngle();
    const QPoint corner2 = viewer.mapFromScene(viewer.cropRect().topRight());
    dragOnViewport(viewer.viewport(), corner2, corner2 + QPoint(10, -60));
    QVERIFY(viewer.rotateAngle() < clockwise);
}

void RotateToolTest::hoveringACornerShowsARotateCursor() {
    ImageViewer viewer(m_imagePath);
    viewer.resize(600, 450);
    viewer.show();
    QCoreApplication::processEvents();

    QTest::keyClick(&viewer, Qt::Key_R);

    // Over a corner: the custom bitmap rotate cursor.
    hoverViewport(viewer.viewport(), viewer.mapFromScene(viewer.cropRect().topLeft()));
    QCOMPARE(viewer.viewport()->cursor().shape(), Qt::BitmapCursor);

    // The two corners on one diagonal face opposite ways, so their icons differ.
    const QPixmap topLeft = viewer.viewport()->cursor().pixmap();
    hoverViewport(viewer.viewport(), viewer.mapFromScene(viewer.cropRect().bottomRight()));
    QCOMPARE(viewer.viewport()->cursor().shape(), Qt::BitmapCursor);
    QVERIFY(viewer.viewport()->cursor().pixmap().toImage() != topLeft.toImage());

    // Mid-box, where nothing can be grabbed, stays a plain arrow — rotate mode
    // does not move the box.
    hoverViewport(viewer.viewport(), viewer.mapFromScene(viewer.cropRect().center()));
    QCOMPARE(viewer.viewport()->cursor().shape(), Qt::ArrowCursor);
}

// An un-cropped photograph that gets rotated a little must still come out full
// and rectangular, so the box shrinks to the largest rectangle the tilt allows.
void RotateToolTest::rotating_shrinksAnUntouchedSelectionToTheLargestThatFits() {
    ImageViewer viewer(m_imagePath);   // 200x150
    viewer.resize(600, 450);
    viewer.show();
    QCoreApplication::processEvents();

    QTest::keyClick(&viewer, Qt::Key_R);
    QCOMPARE(viewer.cropRect(), QRectF(0, 0, 200, 150));   // whole frame while upright

    viewer.setRotateAngle(10.0);
    const QRectF selection = viewer.cropRect();
    QCOMPARE(selection, RotateGeometry::largestInscribedRect(QSize(200, 150), 10.0));
    QVERIFY(RotateGeometry::contains(viewer.rotateBounds(), selection));
    // Smaller than the grown bounding box, but still most of the photograph.
    QVERIFY(selection.width()  < viewer.rotatedBoundsRect().width());
    QVERIFY(selection.width() * selection.height() > 0.6 * 200 * 150);
}

void RotateToolTest::rotating_keepsAPlacedSelectionInsideTheTiltedBounds() {
    ImageViewer viewer(m_imagePath);   // 200x150
    viewer.resize(600, 450);
    viewer.show();
    QCoreApplication::processEvents();

    QTest::keyClick(&viewer, Qt::Key_X);
    // A selection the user placed in a corner — the part of the frame a rotation
    // eats into first.
    viewer.setCropRect(QRectF(0, 0, 120, 90));
    QTest::keyClick(&viewer, Qt::Key_R);

    for (double angle : {5.0, 15.0, 30.0, 45.0}) {
        viewer.setRotateAngle(angle);
        QVERIFY2(RotateGeometry::contains(viewer.rotateBounds(), viewer.cropRect()),
                 qPrintable(QStringLiteral("selection escaped at %1°").arg(angle)));
    }
}

void RotateToolTest::doubleClickInRotateMode_resetsTheAngle() {
    ImageViewer viewer(m_imagePath);
    viewer.resize(600, 450);
    viewer.show();
    QCoreApplication::processEvents();

    QTest::keyClick(&viewer, Qt::Key_R);
    viewer.setRotateAngle(12.0);
    QVERIFY(viewer.rotateAngle() > 0.0);

    QTest::mouseDClick(viewer.viewport(), Qt::LeftButton, Qt::NoModifier,
                       viewer.mapFromScene(viewer.cropRect().center()));
    QCOMPARE(viewer.rotateAngle(), 0.0);
    QCOMPARE(viewer.cropRect(), QRectF(0, 0, 200, 150));
}

// ---------------------------------------------------------------------------
// MainWindow commit
// ---------------------------------------------------------------------------
void RotateToolTest::leavingRotateMode_recordsRotateAndCropEdits() {
    MainWindow w(m_imagePath);   // 200x150
    w.resize(600, 400);
    w.show();
    QCoreApplication::processEvents();

    ImageViewer *v = w.activeViewer();
    QTest::keyClick(v, Qt::Key_R);
    v->setRotateAngle(8.0);
    QTest::keyClick(v, Qt::Key_R);           // leave rotate mode → apply
    QCoreApplication::processEvents();

    QVERIFY(w.manifest().rotate() != nullptr);
    QCOMPARE(w.manifest().rotate()->angle(), 8.0);
    // The tilt cost some of the frame, so a crop was recorded alongside it.
    QVERIFY(w.manifest().crop() != nullptr);
    QVERIFY(!w.manifest().crop()->isFull());

    const QString edits = w.imageStateData().value("State_Edits");
    QVERIFY(edits.contains(QStringLiteral("straighten")));
    QVERIFY(edits.contains(QStringLiteral("crop")));
}

// The headline promise: the rotated image is still a full rectangle of
// photograph, with none of the blank triangles the rotation opened up.
void RotateToolTest::rotatedResultHasNoBlankCorners() {
    MainWindow w(m_imagePath);
    w.resize(600, 400);
    w.show();
    QCoreApplication::processEvents();

    ImageViewer *v = w.activeViewer();
    QTest::keyClick(v, Qt::Key_R);
    v->setRotateAngle(11.0);
    QTest::keyClick(v, Qt::Key_R);
    QCoreApplication::processEvents();

    const QImage result = v->currentDisplayPixmap().toImage();
    QVERIFY(!result.isNull());
    QVERIFY(result.width() > 0 && result.height() > 0);

    // Sample just inside each corner — the first place a blank triangle shows up.
    const int inset = 2;
    const QPoint probes[4] = {
        {inset, inset},
        {result.width() - 1 - inset, inset},
        {inset, result.height() - 1 - inset},
        {result.width() - 1 - inset, result.height() - 1 - inset},
    };
    for (const QPoint &p : probes) {
        const QColor c = result.pixelColor(p);
        QVERIFY2(qAbs(c.red()   - kFill.red())   < 24
              && qAbs(c.green() - kFill.green()) < 24
              && qAbs(c.blue()  - kFill.blue())  < 24,
                 qPrintable(QStringLiteral("blank corner at %1,%2 → %3")
                                .arg(p.x()).arg(p.y()).arg(c.name())));
    }
}

void RotateToolTest::quarterTurnFromThePanel_staysInRotateMode() {
    MainWindow w(m_imagePath);   // 200x150
    w.resize(600, 400);
    w.show();
    QCoreApplication::processEvents();

    ImageViewer *v = w.activeViewer();
    QTest::keyClick(v, Qt::Key_R);
    QVERIFY(w.rotatePanel()->isVisible());

    w.rotatePanel()->rotateRightButton()->click();
    QCoreApplication::processEvents();

    // The turn landed in the manifest, and the mode is still open so the user
    // can keep straightening.
    QVERIFY(w.manifest().orientation() != nullptr);
    QVERIFY(v->rotateMode());
    QCOMPARE(v->rotatedBoundsRect().size().toSize(), QSize(150, 200));

    // Rotate left twice returns past upright to a quarter turn the other way.
    w.rotatePanel()->rotateLeftButton()->click();
    QCoreApplication::processEvents();
    QVERIFY(w.manifest().orientation() == nullptr);   // back to square
    QCOMPARE(v->rotatedBoundsRect().size().toSize(), QSize(200, 150));
}

void RotateToolTest::panelSlider_drivesTheAngle() {
    MainWindow w(m_imagePath);
    w.resize(600, 400);
    w.show();
    QCoreApplication::processEvents();

    ImageViewer *v = w.activeViewer();
    QTest::keyClick(v, Qt::Key_R);

    w.rotatePanel()->setAngle(0.0);
    QCOMPARE(w.rotatePanel()->angle(), 0.0);

    // A drag on the image reports back into the panel.
    v->setRotateAngle(-7.5);
    QCOMPARE(w.rotatePanel()->angle(), -7.5);
}

void RotateToolTest::rotationSurvivesReopen() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = QDir(tmp.path()).absoluteFilePath("tilted.png");
    QVERIFY(makeImage(200, 150).save(path, "PNG"));

    QSize applied;
    {
        MainWindow w(path);
        w.resize(600, 400);
        w.show();
        QCoreApplication::processEvents();
        ImageViewer *v = w.activeViewer();
        QTest::keyClick(v, Qt::Key_R);
        v->setRotateAngle(9.0);
        QTest::keyClick(v, Qt::Key_R);
        QCoreApplication::processEvents();
        applied = v->currentDisplayPixmap().size();
        QVERIFY(!applied.isEmpty());
    }
    {
        MainWindow w2(path);
        w2.resize(600, 400);
        w2.show();
        QCoreApplication::processEvents();
        QVERIFY(w2.manifest().rotate() != nullptr);
        QCOMPARE(w2.manifest().rotate()->angle(), 9.0);
        // The same pixels come back, straight from the saved manifest.
        QCOMPARE(w2.activeViewer()->currentDisplayPixmap().size(), applied);
    }
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    // Isolate QSettings (the manifest store) into a throwaway sandbox.
    QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName(QStringLiteral("photo-salon-test"));
    app.setApplicationName(QStringLiteral("photo-salon-rotate-test"));
    RotateToolTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_rotate_tool.moc"
