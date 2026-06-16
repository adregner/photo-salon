#include <QtTest/QtTest>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryFile>
#include "ImageViewer.h"

class TestExternalLaunch : public QObject {
    Q_OBJECT

public:
    TestExternalLaunch();

private slots:
    void keyP_emitsOpenExternal_currentVersion();
    void keyShiftP_emitsOpenExternal_originalFile();
    void keyCtrlP_emitsPickerRequested();

private:
    QString m_imagePath;
    QTemporaryFile *m_tmpFile = nullptr;
};

TestExternalLaunch::TestExternalLaunch() {
    m_tmpFile = new QTemporaryFile(QDir::tempPath() + "/test_XXXXXX.png", this);
    QVERIFY(m_tmpFile->open());
    m_imagePath = m_tmpFile->fileName();
    QImage img(200, 150, QImage::Format_RGB32);
    img.fill(Qt::red);
    img.save(m_tmpFile, "PNG");
    m_tmpFile->close();
}

void TestExternalLaunch::keyP_emitsOpenExternal_currentVersion() {
    ImageViewer viewer(m_imagePath);
    QSignalSpy spy(&viewer, &ImageViewer::openExternalRequested);
    QTest::keyClick(&viewer, Qt::Key_P);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), false);
}

void TestExternalLaunch::keyShiftP_emitsOpenExternal_originalFile() {
    ImageViewer viewer(m_imagePath);
    QSignalSpy spy(&viewer, &ImageViewer::openExternalRequested);
    QTest::keyClick(&viewer, Qt::Key_P, Qt::ShiftModifier);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);
}

void TestExternalLaunch::keyCtrlP_emitsPickerRequested() {
    ImageViewer viewer(m_imagePath);
    QSignalSpy spy(&viewer, &ImageViewer::openExternalPickerRequested);
#ifdef Q_OS_MACOS
    QTest::keyClick(&viewer, Qt::Key_P, Qt::MetaModifier);
#else
    QTest::keyClick(&viewer, Qt::Key_P, Qt::ControlModifier);
#endif
    QCOMPARE(spy.count(), 1);
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    TestExternalLaunch test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_external_launch.moc"
