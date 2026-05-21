#include <QtTest/QtTest>
#include <QApplication>
#include <QImage>
#include <QShowEvent>
#include <QTemporaryFile>
#include <QWheelEvent>
#include "ImageViewer.h"

class ZoomTest : public QObject {
    Q_OBJECT

public:
    ZoomTest();
    ~ZoomTest();

private slots:
    void placeholder();

private:
    QString m_imagePath;
    QTemporaryFile *m_tmpFile = nullptr;
};

ZoomTest::ZoomTest() {
    m_tmpFile = new QTemporaryFile("test_XXXXXX.png", this);
    m_tmpFile->open();
    QImage img(200, 150, QImage::Format_RGB32);
    img.fill(Qt::blue);
    img.save(m_tmpFile->fileName());
    m_imagePath = m_tmpFile->fileName();
}

ZoomTest::~ZoomTest() {}

void ZoomTest::placeholder() {
    QVERIFY(true);
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    ZoomTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_zoom.moc"
