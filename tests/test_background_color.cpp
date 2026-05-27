#include <QtTest/QtTest>
#include <QApplication>
#include <QImage>
#include <QTemporaryFile>
#include "ImageViewer.h"

class BackgroundColorTest : public QObject {
    Q_OBJECT

public:
    BackgroundColorTest();

private slots:
    void defaultIsBlack();
    void setGreyChangesBackground();
    void bKeyEmitsSignal();
    void greyClampedTo255();
    void greyClampedTo0();

private:
    QString m_imagePath;
    QTemporaryFile *m_tmpFile = nullptr;
};

BackgroundColorTest::BackgroundColorTest() {
    m_tmpFile = new QTemporaryFile("bgtest_XXXXXX.png", this);
    m_tmpFile->open();
    m_tmpFile->close();
    QImage img(50, 50, QImage::Format_RGB32);
    img.fill(Qt::blue);
    img.save(m_tmpFile->fileName());
    m_imagePath = m_tmpFile->fileName();
}

void BackgroundColorTest::defaultIsBlack() {
    ImageViewer viewer(m_imagePath);
    QCOMPARE(viewer.backgroundGrey(), 0);
    QCOMPARE(viewer.backgroundBrush().color(), QColor(0, 0, 0));
}

void BackgroundColorTest::setGreyChangesBackground() {
    ImageViewer viewer(m_imagePath);
    viewer.setBackgroundGrey(128);
    QCOMPARE(viewer.backgroundGrey(), 128);
    QCOMPARE(viewer.backgroundBrush().color(), QColor(128, 128, 128));
}

void BackgroundColorTest::bKeyEmitsSignal() {
    ImageViewer viewer(m_imagePath);
    QSignalSpy spy(&viewer, &ImageViewer::backgroundPickerRequested);
    QTest::keyClick(&viewer, Qt::Key_B);
    QCOMPARE(spy.count(), 1);
}

void BackgroundColorTest::greyClampedTo255() {
    ImageViewer viewer(m_imagePath);
    viewer.setBackgroundGrey(300);
    QCOMPARE(viewer.backgroundGrey(), 255);
}

void BackgroundColorTest::greyClampedTo0() {
    ImageViewer viewer(m_imagePath);
    viewer.setBackgroundGrey(-10);
    QCOMPARE(viewer.backgroundGrey(), 0);
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    BackgroundColorTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_background_color.moc"
