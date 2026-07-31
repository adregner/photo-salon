// Tests for side-by-side compare mode: opening a second image, the tab strip,
// focus handling, per-image edit isolation, and synchronized zoom/pan.
#include <QtTest/QtTest>
#include <QApplication>
#include <QDir>
#include <QFrame>
#include <QImage>
#include <QSettings>
#include <QStandardPaths>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolButton>
#include "CompareTabBar.h"
#include "ImageViewer.h"
#include "MainWindow.h"
#include "RotatePanel.h"

class CompareTest : public QObject {
    Q_OBJECT

private slots:
    void init() { QSettings().clear(); }
    void initTestCase();

    void shiftO_opensSecondImage_inCompareMode();
    void tabBar_hiddenInSingle_visibleWithTwoTabs();
    void closingTab_returnsToSingleModeWithOtherImage();
    void editsActOnFocusedImageOnly();
    void clickingViewer_changesFocus();
    void zoom_isSynchronizedRelativeToViewport();

private:
    // The viewer in compare mode that is *not* the focused one.
    ImageViewer *otherViewer(MainWindow &w) {
        for (ImageViewer *v : w.findChildren<ImageViewer *>())
            if (v != w.activeViewer()) return v;
        return nullptr;
    }

    QString m_a, m_b;
    QTemporaryDir m_dir;
};

void CompareTest::initTestCase() {
    QVERIFY(m_dir.isValid());
    QDir dir(m_dir.path());
    QImage a(400, 300, QImage::Format_RGB32); a.fill(Qt::red);
    QImage b(200, 200, QImage::Format_RGB32); b.fill(Qt::blue);
    m_a = dir.absoluteFilePath("a.png");
    m_b = dir.absoluteFilePath("b.png");
    QVERIFY(a.save(m_a));
    QVERIFY(b.save(m_b));
}

void CompareTest::shiftO_opensSecondImage_inCompareMode() {
    MainWindow w(m_a);
    w.resize(800, 600);
    w.show();
    QCoreApplication::processEvents();

    QVERIFY(!w.compareMode());
    w.openComparison(m_b);
    QCoreApplication::processEvents();

    QVERIFY(w.compareMode());
    QCOMPARE(w.findChildren<ImageViewer *>().size(), 2);
    // The newly opened image is the focused one.
    QCOMPARE(QFileInfo(w.activeViewer()->currentPath()).fileName(), QString("b.png"));
}

void CompareTest::tabBar_hiddenInSingle_visibleWithTwoTabs() {
    MainWindow w(m_a);
    w.resize(800, 600);
    w.show();
    QCoreApplication::processEvents();

    auto *bar = w.findChild<CompareTabBar *>();
    QVERIFY(bar);
    QVERIFY(!bar->isVisible());   // single image → no tab strip

    w.openComparison(m_b);
    QCoreApplication::processEvents();
    QVERIFY(bar->isVisible());
    // One tab per image, each labelled with its file name.
    auto names = bar->findChildren<QToolButton *>(QStringLiteral("tabName"));
    QCOMPARE(names.size(), 2);
    QStringList labels{names.at(0)->text(), names.at(1)->text()};
    QVERIFY(labels.contains("a.png"));
    QVERIFY(labels.contains("b.png"));

    // The focused image's tab reads lighter than the other's.
    auto tabs = bar->findChildren<QFrame *>(QStringLiteral("compareTab"));
    QCOMPARE(tabs.size(), 2);
    const int focusedTab = (QFileInfo(w.activeViewer()->currentPath()).fileName() == "a.png") ? 0 : 1;
    QVERIFY(tabs.at(focusedTab)->styleSheet().contains("#5a5a5a"));   // lighter = focused
    QVERIFY(tabs.at(1 - focusedTab)->styleSheet().contains("#2e2e2e"));
}

void CompareTest::closingTab_returnsToSingleModeWithOtherImage() {
    MainWindow w(m_a);
    w.resize(800, 600);
    w.show();
    QCoreApplication::processEvents();
    w.openComparison(m_b);   // focus = b (index 1)
    QCoreApplication::processEvents();

    auto *bar = w.findChild<CompareTabBar *>();
    QVERIFY(bar);
    // Close the focused image's tab (b, the second one).
    auto tabs = bar->findChildren<QFrame *>(QStringLiteral("compareTab"));
    QCOMPARE(tabs.size(), 2);
    auto *closeB = tabs.at(1)->findChild<QToolButton *>(QStringLiteral("tabClose"));
    QVERIFY(closeB);
    closeB->click();
    QCoreApplication::processEvents();

    QVERIFY(!w.compareMode());
    QVERIFY(!bar->isVisible());
    // The remaining image is the one whose tab was not closed.
    QCOMPARE(QFileInfo(w.activeViewer()->currentPath()).fileName(), QString("a.png"));
}

void CompareTest::editsActOnFocusedImageOnly() {
    MainWindow w(m_a);
    w.resize(800, 600);
    w.show();
    QCoreApplication::processEvents();
    w.openComparison(m_b);   // focus = b
    QCoreApplication::processEvents();

    ImageViewer *b = w.activeViewer();
    ImageViewer *a = otherViewer(w);
    QVERIFY(a && b);

    // Rotate the focused image (b). Only b's manifest should carry the edit.
    QTest::keyClick(b, Qt::Key_R);                    // rotate mode
    w.rotatePanel()->rotateRightButton()->click();    // quarter turn
    QTest::keyClick(b, Qt::Key_R);                    // leave rotate mode
    QCoreApplication::processEvents();
    QVERIFY(w.manifest().orientation() != nullptr);   // focused (b)

    // Focus the other image by clicking it, then confirm it is untouched.
    QTest::mouseClick(a->viewport(), Qt::LeftButton);
    QCoreApplication::processEvents();
    QCOMPARE(w.activeViewer(), a);
    QVERIFY(w.manifest().orientation() == nullptr);   // focused (a) — unedited
}

void CompareTest::clickingViewer_changesFocus() {
    MainWindow w(m_a);
    w.resize(800, 600);
    w.show();
    QCoreApplication::processEvents();
    w.openComparison(m_b);   // focus = b
    QCoreApplication::processEvents();

    ImageViewer *a = otherViewer(w);
    QVERIFY(a);
    QVERIFY(w.activeViewer() != a);

    QTest::mouseClick(a->viewport(), Qt::LeftButton);
    QCoreApplication::processEvents();
    QCOMPARE(w.activeViewer(), a);
}

void CompareTest::zoom_isSynchronizedRelativeToViewport() {
    MainWindow w(m_a);
    w.resize(800, 600);
    w.show();
    QCoreApplication::processEvents();
    w.openComparison(m_b);   // focus = b (200x200), other = a (400x300)
    QCoreApplication::processEvents();

    ImageViewer *b = w.activeViewer();
    ImageViewer *a = otherViewer(w);
    QVERIFY(a && b);

    // Establish a clean, synced "fit" baseline at the panes' final laid-out sizes
    // (offscreen nested-layout sizing settles only after the window is shown).
    QTest::keyClick(b, Qt::Key_0);
    QCoreApplication::processEvents();

    // Both read as "fit" (relative zoom ~1.0), despite different pixel sizes.
    QVERIFY(qAbs(a->relativeZoom() - 1.0) < 0.05);
    QVERIFY(qAbs(b->relativeZoom() - 1.0) < 0.05);

    // Zoom the focused image in twice; the other tracks the same relative zoom.
    QTest::keyClick(b, Qt::Key_Plus);
    QTest::keyClick(b, Qt::Key_Plus);
    QCoreApplication::processEvents();

    QVERIFY(b->relativeZoom() > 1.2);
    QVERIFY(qAbs(a->relativeZoom() - b->relativeZoom()) < 0.05);
    // And they remain centred on the same relative pixel.
    QVERIFY(qAbs(a->relativeCenter().x() - b->relativeCenter().x()) < 0.05);
    QVERIFY(qAbs(a->relativeCenter().y() - b->relativeCenter().y()) < 0.05);
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName(QStringLiteral("photo-salon-test"));
    app.setApplicationName(QStringLiteral("photo-salon-test"));
    CompareTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_compare.moc"
