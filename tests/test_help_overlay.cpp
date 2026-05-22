#include <QtTest/QtTest>
#include <QApplication>
#include <QImage>
#include <QTemporaryFile>
#include "ImageViewer.h"

class HelpOverlayTest : public QObject {
    Q_OBJECT

public:
    HelpOverlayTest();
    ~HelpOverlayTest();

private slots:
    void keyQuestionShowsOverlay();
    void keyQuestionTogglesOverlay();
    void anyKeyDismissesOverlay();
    void zoomStillWorksWhenOverlayVisible();

private:
    QString m_imagePath;
    QTemporaryFile *m_tmpFile = nullptr;
};

HelpOverlayTest::HelpOverlayTest() {
    m_tmpFile = new QTemporaryFile("test_XXXXXX.png", this);
    m_tmpFile->open();
    m_tmpFile->close();
    QImage img(200, 150, QImage::Format_RGB32);
    img.fill(Qt::blue);
    img.save(m_tmpFile->fileName());
    m_imagePath = m_tmpFile->fileName();
}

HelpOverlayTest::~HelpOverlayTest() {}

void HelpOverlayTest::keyQuestionShowsOverlay() {
    ImageViewer viewer(m_imagePath);
    QTest::keyClick(&viewer, Qt::Key_Question);
    QVERIFY(viewer.helpVisible());
}

void HelpOverlayTest::keyQuestionTogglesOverlay() {
    ImageViewer viewer(m_imagePath);
    QTest::keyClick(&viewer, Qt::Key_Question);
    QTest::keyClick(&viewer, Qt::Key_Question);
    QVERIFY(!viewer.helpVisible());
}

void HelpOverlayTest::anyKeyDismissesOverlay() {
    ImageViewer viewer(m_imagePath);
    QTest::keyClick(&viewer, Qt::Key_Question);
    QVERIFY(viewer.helpVisible());
    QTest::keyClick(&viewer, Qt::Key_Plus);
    QVERIFY(!viewer.helpVisible());
}

void HelpOverlayTest::zoomStillWorksWhenOverlayVisible() {
    ImageViewer viewer(m_imagePath);
    QTest::keyClick(&viewer, Qt::Key_Question);
    double before = viewer.transform().m11();
    QTest::keyClick(&viewer, Qt::Key_Plus);
    QVERIFY(viewer.transform().m11() > before);
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    HelpOverlayTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_help_overlay.moc"
