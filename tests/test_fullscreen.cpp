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
    void keyEscape_emitsToggle();
    void keyF_withHelpOpen_closesHelpAndEmitsToggle();
    void keyEscape_withHelpClosed_emitsToggle();
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

void FullscreenTest::keyEscape_emitsToggle() {
    ImageViewer viewer(m_imagePath);
    QSignalSpy spy(&viewer, &ImageViewer::fullscreenToggleRequested);
    QTest::keyClick(&viewer, Qt::Key_Escape);
    QCOMPARE(spy.count(), 1);
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

void FullscreenTest::keyEscape_withHelpClosed_emitsToggle() {
    ImageViewer viewer(m_imagePath);
    QVERIFY(!viewer.helpVisible());

    QSignalSpy spy(&viewer, &ImageViewer::fullscreenToggleRequested);
    QTest::keyClick(&viewer, Qt::Key_Escape);

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
