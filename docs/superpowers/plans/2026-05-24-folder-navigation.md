# Folder Navigation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Allow the user to press Left/Right arrow keys to navigate to the previous/next image in the same folder as the currently open file, wrapping around at the ends.

**Architecture:** `ImageViewer` gains a `loadImage(path)` method that replaces scene content, stores `m_imagePath`, and emits `imagePathChanged`. A private `navigate(delta)` method uses `QDir::entryList()` filtered by `supportedExtensions()` (from the Expanded Format Support plan) to find the adjacent file. `MainWindow` updates the window title whenever the path changes.

**Tech Stack:** Qt6 (`QDir`, `QFileInfo`, `QGraphicsScene`), `supportedExtensions()` from `src/ImageFormats.h` (must be implemented first — see `2026-05-24-expanded-format-support.md`), QTest, C++17

---

## Prerequisites

The `Expanded Format Support` plan must be implemented first. Specifically, `src/ImageFormats.h` and `src/ImageFormats.cpp` must exist and be part of `photo-salon-lib` before building this plan.

---

## File Structure

- Modify: `src/ImageViewer.h` — add `loadImage()`, `currentPath()`, `m_imagePath`, `m_pixmapItem`, `imagePathChanged` signal, `navigate()` private method
- Modify: `src/ImageViewer.cpp` — implement `loadImage()`, `navigate()`, Left/Right key handling; refactor constructor to call `loadImage()`
- Modify: `src/MainWindow.cpp` — connect `imagePathChanged` to update window title
- Create: `tests/test_folder_navigation.cpp`
- Modify: `tests/CMakeLists.txt`

---

## Task 1: Register the test target

**Files:**
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Append to `tests/CMakeLists.txt`**

```cmake
qt_add_executable(test_folder_navigation test_folder_navigation.cpp)
target_link_libraries(test_folder_navigation PRIVATE Qt6::Test photo-salon-lib)
add_test(NAME test_folder_navigation COMMAND test_folder_navigation)
set_tests_properties(test_folder_navigation PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 2: Confirm CMake reconfigures**

```bash
cmake -B _build 2>&1 | tail -5
```

Expected: exits 0, no errors.

---

## Task 2: Write the failing tests

**Files:**
- Create: `tests/test_folder_navigation.cpp`

- [ ] **Step 1: Create the test file**

```cpp
#include <QtTest/QtTest>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include "ImageViewer.h"

static QString makePng(const QDir &dir, const QString &name) {
    QString path = dir.absoluteFilePath(name);
    QImage img(10, 10, QImage::Format_RGB32);
    img.fill(Qt::red);
    img.save(path);
    return path;
}

class FolderNavigationTest : public QObject {
    Q_OBJECT

private slots:
    void navigateNextWraps();
    void navigatePrevWraps();
    void navigateRightKey();
    void navigateLeftKey();
    void imagePathChangedSignal();
    void loadImageUpdatesPath();
};

void FolderNavigationTest::loadImageUpdatesPath() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QString a = makePng(QDir(tmp.path()), "a.png");
    QString b = makePng(QDir(tmp.path()), "b.png");

    ImageViewer viewer(a);
    QCOMPARE(viewer.currentPath(), a);

    viewer.loadImage(b);
    QCOMPARE(viewer.currentPath(), b);
}

void FolderNavigationTest::imagePathChangedSignal() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QString a = makePng(QDir(tmp.path()), "a.png");
    QString b = makePng(QDir(tmp.path()), "b.png");

    ImageViewer viewer(a);
    QSignalSpy spy(&viewer, &ImageViewer::imagePathChanged);

    viewer.loadImage(b);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), b);
}

void FolderNavigationTest::navigateNextWraps() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    QString a = makePng(dir, "a.png");
    QString b = makePng(dir, "b.png");
    QString c = makePng(dir, "c.png");

    ImageViewer viewer(a);
    QTest::keyClick(&viewer, Qt::Key_Right);
    QCOMPARE(viewer.currentPath(), b);
    QTest::keyClick(&viewer, Qt::Key_Right);
    QCOMPARE(viewer.currentPath(), c);
    QTest::keyClick(&viewer, Qt::Key_Right);   // wraps to first
    QCOMPARE(viewer.currentPath(), a);
}

void FolderNavigationTest::navigatePrevWraps() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    QString a = makePng(dir, "a.png");
    makePng(dir, "b.png");
    QString c = makePng(dir, "c.png");

    ImageViewer viewer(a);
    QTest::keyClick(&viewer, Qt::Key_Left);   // wraps to last
    QCOMPARE(viewer.currentPath(), c);
}

void FolderNavigationTest::navigateRightKey() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    QString a = makePng(dir, "img1.png");
    QString b = makePng(dir, "img2.png");

    ImageViewer viewer(a);
    QTest::keyClick(&viewer, Qt::Key_Right);
    QCOMPARE(viewer.currentPath(), b);
}

void FolderNavigationTest::navigateLeftKey() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    makePng(dir, "img1.png");
    QString b = makePng(dir, "img2.png");
    QString c = makePng(dir, "img3.png");

    ImageViewer viewer(c);
    QTest::keyClick(&viewer, Qt::Key_Left);
    QCOMPARE(viewer.currentPath(), b);
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    FolderNavigationTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_folder_navigation.moc"
```

- [ ] **Step 2: Build — expect failure**

```bash
cmake --build _build --target test_folder_navigation 2>&1 | tail -10
```

Expected: errors about `currentPath()` and `imagePathChanged` not existing on `ImageViewer`.

---

## Task 3: Refactor `ImageViewer` to support `loadImage()`

**Files:**
- Modify: `src/ImageViewer.h`
- Modify: `src/ImageViewer.cpp`

- [ ] **Step 1: Update `src/ImageViewer.h`**

```cpp
#pragma once
#include <QGraphicsView>
#include <QSize>
#include <QString>

class QGraphicsPixmapItem;
class QGraphicsScene;
class QKeyEvent;
class QResizeEvent;
class QShowEvent;
class QWheelEvent;

class ImageViewer : public QGraphicsView {
    Q_OBJECT

public:
    explicit ImageViewer(const QString &imagePath, QWidget *parent = nullptr);

    void loadImage(const QString &path);
    QString currentPath() const { return m_imagePath; }
    QSize nativeImageSize() const { return m_nativeSize; }
    bool helpVisible() const { return m_helpVisible; }
    QPixmap pixmap() const;

signals:
    void helpVisibilityChanged(bool visible);
    void imagePathChanged(const QString &path);

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void fitImage();
    void applyZoom(double factor);
    void navigate(int delta);

    QGraphicsScene *m_scene;
    QGraphicsPixmapItem *m_pixmapItem = nullptr;
    QString m_imagePath;
    QSize m_nativeSize;
    bool m_fitted = true;
    bool m_helpVisible = false;
};
```

- [ ] **Step 2: Update `src/ImageViewer.cpp`**

```cpp
#include "ImageViewer.h"
#include "ImageFormats.h"
#include <QDir>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QKeyEvent>
#include <QPixmap>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>
#include <cmath>

ImageViewer::ImageViewer(const QString &imagePath, QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    loadImage(imagePath);
}

void ImageViewer::loadImage(const QString &path) {
    m_scene->clear();
    m_pixmapItem = nullptr;
    m_imagePath = path;

    QPixmap pixmap(path);
    if (pixmap.isNull()) {
        auto *errorText = new QGraphicsTextItem(
            QString("Failed to load image:\n%1").arg(path));
        errorText->setDefaultTextColor(Qt::red);
        m_scene->addItem(errorText);
    } else {
        m_nativeSize = pixmap.size();
        m_pixmapItem = m_scene->addPixmap(pixmap);
        m_scene->setSceneRect(pixmap.rect());
        m_fitted = true;
        fitImage();
    }
    emit imagePathChanged(m_imagePath);
}

QPixmap ImageViewer::pixmap() const {
    return m_pixmapItem ? m_pixmapItem->pixmap() : QPixmap{};
}

void ImageViewer::navigate(int delta) {
    if (m_imagePath.isEmpty()) return;
    QFileInfo info(m_imagePath);
    QDir dir = info.absoluteDir();
    QStringList files = dir.entryList(supportedExtensions(), QDir::Files, QDir::Name);
    int idx = files.indexOf(info.fileName());
    if (idx < 0 || files.isEmpty()) return;
    int next = ((idx + delta) % files.size() + files.size()) % files.size();
    loadImage(dir.absoluteFilePath(files[next]));
}

void ImageViewer::showEvent(QShowEvent *event) {
    QGraphicsView::showEvent(event);
    if (m_fitted)
        fitImage();
}

void ImageViewer::resizeEvent(QResizeEvent *event) {
    QGraphicsView::resizeEvent(event);
    if (m_fitted && isVisible())
        fitImage();
}

void ImageViewer::wheelEvent(QWheelEvent *event) {
    const int delta = event->angleDelta().y();
    if (delta == 0) { event->ignore(); return; }
    applyZoom(std::pow(1.15, delta / 120.0));
    event->accept();
}

void ImageViewer::keyPressEvent(QKeyEvent *event) {
    if (m_helpVisible && event->key() != Qt::Key_Question) {
        m_helpVisible = false;
        emit helpVisibilityChanged(false);
    }

    switch (event->key()) {
    case Qt::Key_Left:
        navigate(-1);
        event->accept();
        break;
    case Qt::Key_Right:
        navigate(1);
        event->accept();
        break;
    case Qt::Key_Question:
        m_helpVisible = !m_helpVisible;
        emit helpVisibilityChanged(m_helpVisible);
        event->accept();
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        applyZoom(1.15);
        event->accept();
        break;
    case Qt::Key_Minus:
        applyZoom(1.0 / 1.15);
        event->accept();
        break;
    case Qt::Key_0:
        m_fitted = true;
        fitImage();
        event->accept();
        break;
    default:
        QGraphicsView::keyPressEvent(event);
        break;
    }
}

void ImageViewer::applyZoom(double factor) {
    const double currentScale = transform().m11();
    const double newScale = currentScale * factor;
    if (newScale < 0.05 || newScale > 32.0) return;
    scale(factor, factor);
    m_fitted = false;
}

void ImageViewer::fitImage() {
    if (!m_scene->items().isEmpty())
        fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
}

void ImageViewer::applyZoom(double factor) {
    const double currentScale = transform().m11();
    const double newScale = currentScale * factor;
    if (newScale < 0.05 || newScale > 32.0) return;
    scale(factor, factor);
    m_fitted = false;
}
```

**Note:** The `applyZoom` definition appears twice above — remove the duplicate. Here is the correct final file without duplication:

```cpp
#include "ImageViewer.h"
#include "ImageFormats.h"
#include <QDir>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QKeyEvent>
#include <QPixmap>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>
#include <cmath>

ImageViewer::ImageViewer(const QString &imagePath, QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    loadImage(imagePath);
}

void ImageViewer::loadImage(const QString &path) {
    m_scene->clear();
    m_pixmapItem = nullptr;
    m_imagePath = path;

    QPixmap pixmap(path);
    if (pixmap.isNull()) {
        auto *errorText = new QGraphicsTextItem(
            QString("Failed to load image:\n%1").arg(path));
        errorText->setDefaultTextColor(Qt::red);
        m_scene->addItem(errorText);
    } else {
        m_nativeSize = pixmap.size();
        m_pixmapItem = m_scene->addPixmap(pixmap);
        m_scene->setSceneRect(pixmap.rect());
        m_fitted = true;
        fitImage();
    }
    emit imagePathChanged(m_imagePath);
}

QPixmap ImageViewer::pixmap() const {
    return m_pixmapItem ? m_pixmapItem->pixmap() : QPixmap{};
}

void ImageViewer::navigate(int delta) {
    if (m_imagePath.isEmpty()) return;
    QFileInfo info(m_imagePath);
    QDir dir = info.absoluteDir();
    QStringList files = dir.entryList(supportedExtensions(), QDir::Files, QDir::Name);
    int idx = files.indexOf(info.fileName());
    if (idx < 0 || files.isEmpty()) return;
    int next = ((idx + delta) % files.size() + files.size()) % files.size();
    loadImage(dir.absoluteFilePath(files[next]));
}

void ImageViewer::showEvent(QShowEvent *event) {
    QGraphicsView::showEvent(event);
    if (m_fitted) fitImage();
}

void ImageViewer::resizeEvent(QResizeEvent *event) {
    QGraphicsView::resizeEvent(event);
    if (m_fitted && isVisible()) fitImage();
}

void ImageViewer::wheelEvent(QWheelEvent *event) {
    const int delta = event->angleDelta().y();
    if (delta == 0) { event->ignore(); return; }
    applyZoom(std::pow(1.15, delta / 120.0));
    event->accept();
}

void ImageViewer::keyPressEvent(QKeyEvent *event) {
    if (m_helpVisible && event->key() != Qt::Key_Question) {
        m_helpVisible = false;
        emit helpVisibilityChanged(false);
    }

    switch (event->key()) {
    case Qt::Key_Left:
        navigate(-1);
        event->accept();
        break;
    case Qt::Key_Right:
        navigate(1);
        event->accept();
        break;
    case Qt::Key_Question:
        m_helpVisible = !m_helpVisible;
        emit helpVisibilityChanged(m_helpVisible);
        event->accept();
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        applyZoom(1.15);
        event->accept();
        break;
    case Qt::Key_Minus:
        applyZoom(1.0 / 1.15);
        event->accept();
        break;
    case Qt::Key_0:
        m_fitted = true;
        fitImage();
        event->accept();
        break;
    default:
        QGraphicsView::keyPressEvent(event);
        break;
    }
}

void ImageViewer::applyZoom(double factor) {
    const double currentScale = transform().m11();
    const double newScale = currentScale * factor;
    if (newScale < 0.05 || newScale > 32.0) return;
    scale(factor, factor);
    m_fitted = false;
}

void ImageViewer::fitImage() {
    if (!m_scene->items().isEmpty())
        fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
}
```

- [ ] **Step 3: Build and run folder navigation tests**

```bash
cmake -B _build && cmake --build _build 2>&1 | tail -5
QT_QPA_PLATFORM=offscreen ./_build/tests/test_folder_navigation
```

Expected:
```
********* Start testing of FolderNavigationTest *********
PASS   : FolderNavigationTest::loadImageUpdatesPath()
PASS   : FolderNavigationTest::imagePathChangedSignal()
PASS   : FolderNavigationTest::navigateNextWraps()
PASS   : FolderNavigationTest::navigatePrevWraps()
PASS   : FolderNavigationTest::navigateRightKey()
PASS   : FolderNavigationTest::navigateLeftKey()
Totals: 6 passed, 0 failed, 0 skipped
```

---

## Task 4: Update `MainWindow` to track the current path in the title

**Files:**
- Modify: `src/MainWindow.cpp`

- [ ] **Step 1: Update the viewer connection in `MainWindow::MainWindow`**

Replace the single static title assignment with a dynamic connection. In `src/MainWindow.cpp`, after `setCentralWidget(viewer)`:

```cpp
// Remove the static setWindowTitle call and replace with:
connect(viewer, &ImageViewer::imagePathChanged, this, [this](const QString &path) {
    setWindowTitle(QString("photo-salon — %1").arg(QFileInfo(path).fileName()));
});
// Trigger once immediately for initial path
setWindowTitle(QString("photo-salon — %1").arg(QFileInfo(imagePath).fileName()));
```

The full updated `MainWindow::MainWindow` constructor:

```cpp
MainWindow::MainWindow(const QString &imagePath, QWidget *parent)
    : QMainWindow(parent)
{
    auto *viewer = new ImageViewer(imagePath, this);
    setCentralWidget(viewer);

    setWindowTitle(QString("photo-salon — %1").arg(QFileInfo(imagePath).fileName()));
    connect(viewer, &ImageViewer::imagePathChanged, this, [this](const QString &path) {
        setWindowTitle(QString("photo-salon — %1").arg(QFileInfo(path).fileName()));
    });

    QSize imageSize = viewer->nativeImageSize();
    if (!imageSize.isEmpty()) {
        QSize available = screen()->availableGeometry().size();
        resize(imageSize.boundedTo(available));
    } else {
        resize(800, 600);
    }

    m_helpOverlay = new HelpOverlay(this);
    m_helpOverlay->resize(size());
    m_helpOverlay->raise();
    connect(viewer, &ImageViewer::helpVisibilityChanged, m_helpOverlay, &QWidget::setVisible);
}
```

- [ ] **Step 2: Run full test suite**

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir _build -V 2>&1 | tail -20
```

Expected: all tests pass.

- [ ] **Step 3: Commit**

```bash
git add src/ImageViewer.h src/ImageViewer.cpp src/MainWindow.cpp \
        tests/test_folder_navigation.cpp tests/CMakeLists.txt
git commit -m "feat: add folder navigation (Left/Right arrows) and loadImage() refactor"
```
