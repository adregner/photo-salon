// Tests for the pan cursor: the viewer shows a plain arrow at rest and the
// (closed) hand only while actively dragging to pan.
#include <QtTest/QtTest>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QMouseEvent>
#include <QTemporaryFile>
#include "ImageViewer.h"

class PanCursorTest : public QObject {
    Q_OBJECT

public:
    PanCursorTest();

private slots:
    void restingCursorIsArrow();
    void closedHandWhilePanning_arrowAfterRelease();
    void cursorIsArrowAfterLeavingCropMode();

private:
    QString m_imagePath;
    QTemporaryFile *m_tmpFile = nullptr;
};

PanCursorTest::PanCursorTest() {
    m_tmpFile = new QTemporaryFile(QDir::tempPath() + "/pan_XXXXXX.png", this);
    if (!m_tmpFile->open()) return;
    QImage img(200, 150, QImage::Format_RGB32);
    img.fill(Qt::magenta);
    img.save(m_tmpFile, "PNG");
    m_tmpFile->close();
    m_imagePath = m_tmpFile->fileName();
}

void PanCursorTest::restingCursorIsArrow() {
    ImageViewer viewer(m_imagePath);
    viewer.resize(400, 300);
    viewer.show();
    QCoreApplication::processEvents();
    QCOMPARE(viewer.viewport()->cursor().shape(), Qt::ArrowCursor);
}

void PanCursorTest::closedHandWhilePanning_arrowAfterRelease() {
    ImageViewer viewer(m_imagePath);
    viewer.resize(400, 300);
    viewer.show();
    QCoreApplication::processEvents();

    QWidget *vp = viewer.viewport();
    const QPointF p(100, 100);
    const QPointF g = vp->mapToGlobal(p);

    QMouseEvent press(QEvent::MouseButtonPress, p, g,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    qApp->sendEvent(vp, &press);
    QCOMPARE(vp->cursor().shape(), Qt::ClosedHandCursor);

    QMouseEvent release(QEvent::MouseButtonRelease, p, g,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    qApp->sendEvent(vp, &release);
    QCOMPARE(vp->cursor().shape(), Qt::ArrowCursor);
}

void PanCursorTest::cursorIsArrowAfterLeavingCropMode() {
    ImageViewer viewer(m_imagePath);
    viewer.resize(400, 300);
    viewer.show();
    QCoreApplication::processEvents();

    viewer.setCropMode(true);
    viewer.setCropMode(false);
    QCOMPARE(viewer.viewport()->cursor().shape(), Qt::ArrowCursor);
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    PanCursorTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_pan_cursor.moc"
