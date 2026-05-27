#include <QtTest/QtTest>
#include <QApplication>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryFile>
#include "ImageViewer.h"

class FullscreenTest : public QObject {
    Q_OBJECT

public:
    FullscreenTest();
    ~FullscreenTest();

private slots:
    void keyF_emitsToggle();
    void keyEscape_closesHelp_whenHelpOpen();
    void keyEscape_doesNotEmitToggle_whenHelpClosed();
    void keyF_withHelpOpen_closesHelpAndEmitsToggle();
    void otherKeys_doNotEmitToggle();

private:
    QString m_imagePath;
    QTemporaryFile *m_tmpFile = nullptr;
};

FullscreenTest::FullscreenTest() {
    m_tmpFile = new QTemporaryFile("test_XXXXXX.png", this);
    QVERIFY(m_tmpFile->open());
    m_tmpFile->close();
    QImage img(200, 150, QImage::Format_RGB32);
    img.fill(Qt::green);
    img.save(m_tmpFile->fileName());
    m_imagePath = m_tmpFile->fileName();
}

FullscreenTest::~FullscreenTest() {}

void FullscreenTest::keyF_emitsToggle() {
    ImageViewer viewer(m_imagePath);
    QSignalSpy spy(&viewer, &ImageViewer::fullscreenToggleRequested);
    QTest::keyClick(&viewer, Qt::Key_F);
    QCOMPARE(spy.count(), 1);
}

// Escape is handled globally by MainWindow's event filter.
// When ImageViewer is tested in isolation, pressing Escape closes the
// help overlay (via the top-of-keyPressEvent guard) but does NOT emit
// fullscreenToggleRequested — that is MainWindow's responsibility.
void FullscreenTest::keyEscape_closesHelp_whenHelpOpen() {
    ImageViewer viewer(m_imagePath);
    QTest::keyClick(&viewer, Qt::Key_Question);
    QVERIFY(viewer.helpVisible());
    QTest::keyClick(&viewer, Qt::Key_Escape);
    QVERIFY(!viewer.helpVisible());
}

void FullscreenTest::keyEscape_doesNotEmitToggle_whenHelpClosed() {
    ImageViewer viewer(m_imagePath);
    QSignalSpy spy(&viewer, &ImageViewer::fullscreenToggleRequested);
    QTest::keyClick(&viewer, Qt::Key_Escape);
    QCOMPARE(spy.count(), 0);
}

void FullscreenTest::keyF_withHelpOpen_closesHelpAndEmitsToggle() {
    ImageViewer viewer(m_imagePath);
    QTest::keyClick(&viewer, Qt::Key_Question);
    QVERIFY(viewer.helpVisible());

    QSignalSpy spy(&viewer, &ImageViewer::fullscreenToggleRequested);
    QTest::keyClick(&viewer, Qt::Key_F);

    QVERIFY(!viewer.helpVisible());
    QCOMPARE(spy.count(), 1);
}

void FullscreenTest::otherKeys_doNotEmitToggle() {
    ImageViewer viewer(m_imagePath);
    QSignalSpy spy(&viewer, &ImageViewer::fullscreenToggleRequested);
    QTest::keyClick(&viewer, Qt::Key_Plus);
    QTest::keyClick(&viewer, Qt::Key_Minus);
    QTest::keyClick(&viewer, Qt::Key_0);
    QCOMPARE(spy.count(), 0);
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    FullscreenTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_fullscreen.moc"
