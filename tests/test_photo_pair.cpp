// Tests for auto-pairing: two images whose base file names both end with
// "_pair" automatically open side by side, however either one is opened.
#include <QtTest/QtTest>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QImage>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>
#include "CompareTabBar.h"
#include "ImageFormats.h"
#include "ImageViewer.h"
#include "MainWindow.h"

static QString makePng(const QDir &dir, const QString &name) {
    QString path = dir.absoluteFilePath(name);
    QImage img(10, 10, QImage::Format_RGB32);
    img.fill(Qt::red);
    img.save(path);
    return path;
}

class PhotoPairTest : public QObject {
    Q_OBJECT

private slots:
    void init() { QSettings().clear(); }

    // findPairPartner()
    void partner_matchesTwoPairFiles();
    void partner_plainFile_returnsEmpty();
    void partner_onlyOnePairFile_returnsEmpty();
    void partner_threePairFiles_returnsEmpty();

    // Opening a "_pair" file, however it happens, auto-opens both.
    void constructingWithEitherPairFile_opensBothWithLeftmostOnLeft();
    void openImage_withPairFile_entersAutoPairedCompare();
    void openingPlainFile_staysSingleMode();

    // Closing one tab is the only way to isolate a single paired photo.
    void closingOneTab_leavesTheOtherAloneWithoutRepairing();

    // Arrow keys step past an auto-paired pair as a unit.
    void rightArrow_fromAutoPairedView_skipsPastPairToSingleMode();
    void leftArrow_fromAutoPairedView_skipsPastPairToSingleMode();
};

void PhotoPairTest::partner_matchesTwoPairFiles() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    QString a = makePng(dir, "002_pair.png");
    QString b = makePng(dir, "003_pair.png");

    QCOMPARE(findPairPartner(a), b);
    QCOMPARE(findPairPartner(b), a);
}

void PhotoPairTest::partner_plainFile_returnsEmpty() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    makePng(dir, "002_pair.png");
    QString plain = makePng(dir, "plain.png");

    QVERIFY(findPairPartner(plain).isEmpty());
}

void PhotoPairTest::partner_onlyOnePairFile_returnsEmpty() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    QString a = makePng(dir, "solo_pair.png");
    makePng(dir, "other.png");

    QVERIFY(findPairPartner(a).isEmpty());
}

void PhotoPairTest::partner_threePairFiles_returnsEmpty() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    QString a = makePng(dir, "a_pair.png");
    makePng(dir, "b_pair.png");
    makePng(dir, "c_pair.png");

    // Ambiguous — more than two candidates share the folder.
    QVERIFY(findPairPartner(a).isEmpty());
}

void PhotoPairTest::constructingWithEitherPairFile_opensBothWithLeftmostOnLeft() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    makePng(dir, "002_pair.png");
    makePng(dir, "003_pair.png");

    // Opening the lexicographically *later* file directly still lands the
    // earlier one on the left.
    MainWindow w(dir.absoluteFilePath("003_pair.png"));
    w.resize(800, 600);
    w.show();
    QCoreApplication::processEvents();

    QVERIFY(w.compareMode());
    QVERIFY(w.autoPaired());

    auto *bar = w.findChild<CompareTabBar *>();
    QVERIFY(bar);
    auto names = bar->findChildren<QToolButton *>(QStringLiteral("tabName"));
    QCOMPARE(names.size(), 2);
    QCOMPARE(names.at(0)->text(), QString("002_pair.png"));
    QCOMPARE(names.at(1)->text(), QString("003_pair.png"));

    // The file the caller actually asked to open is the one left focused.
    QCOMPARE(QFileInfo(w.activeViewer()->currentPath()).fileName(), QString("003_pair.png"));
}

void PhotoPairTest::openImage_withPairFile_entersAutoPairedCompare() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    QString solo = makePng(dir, "solo.png");
    makePng(dir, "002_pair.png");
    makePng(dir, "003_pair.png");

    MainWindow w(solo);
    w.resize(800, 600);
    w.show();
    QCoreApplication::processEvents();
    QVERIFY(!w.compareMode());

    // openImage() is the single entry point every "open a file" action (the
    // Open dialog, the folder browser, arrow-key navigation) routes through.
    w.openImage(dir.absoluteFilePath("003_pair.png"));
    QCoreApplication::processEvents();

    QVERIFY(w.compareMode());
    QVERIFY(w.autoPaired());
    auto *bar = w.findChild<CompareTabBar *>();
    auto names = bar->findChildren<QToolButton *>(QStringLiteral("tabName"));
    QCOMPARE(names.at(0)->text(), QString("002_pair.png"));
    QCOMPARE(names.at(1)->text(), QString("003_pair.png"));
}

void PhotoPairTest::openingPlainFile_staysSingleMode() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    QString a = makePng(dir, "a.png");
    makePng(dir, "b.png");

    MainWindow w(a);
    w.resize(800, 600);
    w.show();
    QCoreApplication::processEvents();

    QVERIFY(!w.compareMode());
    QVERIFY(!w.autoPaired());
}

void PhotoPairTest::closingOneTab_leavesTheOtherAloneWithoutRepairing() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    QString left = makePng(dir, "002_pair.png");
    makePng(dir, "003_pair.png");

    MainWindow w(left);
    w.resize(800, 600);
    w.show();
    QCoreApplication::processEvents();
    QVERIFY(w.compareMode());

    auto *bar = w.findChild<CompareTabBar *>();
    auto tabs = bar->findChildren<QFrame *>(QStringLiteral("compareTab"));
    QCOMPARE(tabs.size(), 2);
    // Close the second (right) tab — the only way to isolate one pair member.
    auto *closeRight = tabs.at(1)->findChild<QToolButton *>(QStringLiteral("tabClose"));
    QVERIFY(closeRight);
    closeRight->click();
    QCoreApplication::processEvents();

    QVERIFY(!w.compareMode());
    QVERIFY(!w.autoPaired());
    QCOMPARE(QFileInfo(w.activeViewer()->currentPath()).fileName(), QString("002_pair.png"));
}

void PhotoPairTest::rightArrow_fromAutoPairedView_skipsPastPairToSingleMode() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    makePng(dir, "001.png");
    QString left = makePng(dir, "002_pair.png");
    makePng(dir, "003_pair.png");
    makePng(dir, "004.png");

    MainWindow w(left);
    w.resize(800, 600);
    w.show();
    QCoreApplication::processEvents();
    QVERIFY(w.compareMode());
    QVERIFY(w.autoPaired());

    QTest::keyClick(w.activeViewer(), Qt::Key_Right);
    QCoreApplication::processEvents();

    QVERIFY(!w.compareMode());
    QCOMPARE(QFileInfo(w.activeViewer()->currentPath()).fileName(), QString("004.png"));
}

void PhotoPairTest::leftArrow_fromAutoPairedView_skipsPastPairToSingleMode() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    makePng(dir, "001.png");
    QString left = makePng(dir, "002_pair.png");
    makePng(dir, "003_pair.png");
    makePng(dir, "004.png");

    MainWindow w(left);
    w.resize(800, 600);
    w.show();
    QCoreApplication::processEvents();
    QVERIFY(w.compareMode());
    QVERIFY(w.autoPaired());

    QTest::keyClick(w.activeViewer(), Qt::Key_Left);
    QCoreApplication::processEvents();

    QVERIFY(!w.compareMode());
    QCOMPARE(QFileInfo(w.activeViewer()->currentPath()).fileName(), QString("001.png"));
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName(QStringLiteral("photo-salon-test"));
    app.setApplicationName(QStringLiteral("photo-salon-test"));
    PhotoPairTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_photo_pair.moc"
