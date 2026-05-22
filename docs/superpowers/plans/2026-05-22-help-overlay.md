# Help Overlay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `?`-triggered semi-transparent help overlay listing all keyboard and mouse controls; any keypress dismisses it while still processing the triggering key normally.

**Architecture:** `ImageViewer` tracks `m_helpVisible` and emits `helpVisibilityChanged(bool)` on `?` or any dismissal key; `HelpOverlay` is a focus-free, mouse-transparent `QWidget` child of `MainWindow` that renders the shortcut list via `paintEvent`; `MainWindow` connects the signal to `HelpOverlay::setVisible` and keeps the overlay geometry in sync on resize.

**Tech Stack:** C++17, Qt6 (Widgets, Test), CMake, QtTest

---

## File Map

| File | Change |
|------|--------|
| `src/ImageViewer.h` | Add `m_helpVisible`, `helpVisible()` accessor, `helpVisibilityChanged` signal |
| `src/ImageViewer.cpp` | Update `keyPressEvent` to handle `?` and dismiss-on-any-key |
| `src/HelpOverlay.h` | New — declare `HelpOverlay : QWidget` |
| `src/HelpOverlay.cpp` | New — paint semi-transparent overlay with shortcut content |
| `src/MainWindow.h` | Add `m_helpOverlay` member, `resizeEvent` override, `QResizeEvent` forward decl |
| `src/MainWindow.cpp` | Create overlay, connect signal, resize in constructor and `resizeEvent` |
| `CMakeLists.txt` | Add `src/HelpOverlay.cpp` to `photo-salon-lib` |
| `tests/test_help_overlay.cpp` | New — QtTest for ImageViewer help visibility behavior |
| `tests/CMakeLists.txt` | Add `test_help_overlay` target |

---

## Task 1: Write failing tests and implement ImageViewer help visibility

**Files:**
- Create: `tests/test_help_overlay.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/ImageViewer.h`
- Modify: `src/ImageViewer.cpp`

- [ ] **Step 1: Write the test file**

Create `tests/test_help_overlay.cpp`:

```cpp
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
```

- [ ] **Step 2: Add test target to tests/CMakeLists.txt**

Replace the full contents of `tests/CMakeLists.txt` with:

```cmake
find_package(Qt6 REQUIRED COMPONENTS Test Widgets)

qt_add_executable(test_zoom test_zoom.cpp)
target_link_libraries(test_zoom PRIVATE Qt6::Test photo-salon-lib)
add_test(NAME test_zoom COMMAND test_zoom)
set_tests_properties(test_zoom PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(test_help_overlay test_help_overlay.cpp)
target_link_libraries(test_help_overlay PRIVATE Qt6::Test photo-salon-lib)
add_test(NAME test_help_overlay COMMAND test_help_overlay)
set_tests_properties(test_help_overlay PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Attempt build — confirm it fails**

```bash
cmake -B _build && cmake --build _build 2>&1 | grep -i "error"
```

Expected: compile error mentioning `helpVisible` not found on `ImageViewer`.

- [ ] **Step 4: Update ImageViewer.h**

Replace the full contents of `src/ImageViewer.h` with:

```cpp
#pragma once
#include <QGraphicsView>
#include <QSize>
#include <QString>

class QGraphicsScene;
class QKeyEvent;
class QResizeEvent;
class QShowEvent;
class QWheelEvent;

class ImageViewer : public QGraphicsView {
    Q_OBJECT

public:
    explicit ImageViewer(const QString &imagePath, QWidget *parent = nullptr);

    QSize nativeImageSize() const { return m_nativeSize; }
    bool helpVisible() const { return m_helpVisible; }

signals:
    void helpVisibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void fitImage();
    void applyZoom(double factor);

    QGraphicsScene *m_scene;
    QSize m_nativeSize;
    bool m_fitted = true;
    bool m_helpVisible = false;
};
```

- [ ] **Step 5: Update keyPressEvent in ImageViewer.cpp**

Replace the `keyPressEvent` function (lines 60–80) with:

```cpp
void ImageViewer::keyPressEvent(QKeyEvent *event) {
    if (m_helpVisible && event->key() != Qt::Key_Question) {
        m_helpVisible = false;
        emit helpVisibilityChanged(false);
    }

    switch (event->key()) {
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
```

- [ ] **Step 6: Build and run tests**

```bash
cmake -B _build && cmake --build _build && QT_QPA_PLATFORM=offscreen ./_build/tests/test_help_overlay
```

Expected output (all 4 tests pass):
```
********* Start testing of HelpOverlayTest *********
PASS   : HelpOverlayTest::keyQuestionShowsOverlay()
PASS   : HelpOverlayTest::keyQuestionTogglesOverlay()
PASS   : HelpOverlayTest::anyKeyDismissesOverlay()
PASS   : HelpOverlayTest::zoomStillWorksWhenOverlayVisible()
Totals: 4 passed, 0 failed, 0 skipped
********* Finished testing of HelpOverlayTest *********
```

Also verify existing zoom tests still pass:

```bash
QT_QPA_PLATFORM=offscreen ./_build/tests/test_zoom
```

Expected: all existing zoom tests pass (same output as before).

- [ ] **Step 7: Commit**

```bash
git add tests/test_help_overlay.cpp tests/CMakeLists.txt src/ImageViewer.h src/ImageViewer.cpp
git commit -m "feat: add help visibility to ImageViewer with tests"
```

---

## Task 2: Create HelpOverlay widget

**Files:**
- Create: `src/HelpOverlay.h`
- Create: `src/HelpOverlay.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create src/HelpOverlay.h**

```cpp
#pragma once
#include <QWidget>

class QPaintEvent;

class HelpOverlay : public QWidget {
    Q_OBJECT
public:
    explicit HelpOverlay(QWidget *parent = nullptr);
protected:
    void paintEvent(QPaintEvent *event) override;
};
```

- [ ] **Step 2: Create src/HelpOverlay.cpp**

```cpp
#include "HelpOverlay.h"
#include <QFontDatabase>
#include <QPainter>

HelpOverlay::HelpOverlay(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    hide();
}

void HelpOverlay::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 128));

    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(12);
    p.setFont(font);
    p.setPen(Qt::white);

    const QString text =
        "Keyboard Shortcuts\n"
        "\n"
        "  +  /  =    Zoom in\n"
        "  -          Zoom out\n"
        "  0          Fit to window\n"
        "  ?          Show/hide this help\n"
        "\n"
        "Mouse & Other Controls\n"
        "\n"
        "  Scroll wheel    Zoom (anchored to cursor)";

    p.drawText(rect(), Qt::AlignCenter, text);
}
```

- [ ] **Step 3: Add HelpOverlay.cpp to CMakeLists.txt**

In the root `CMakeLists.txt`, replace:

```cmake
add_library(photo-salon-lib STATIC
    src/MainWindow.cpp
    src/ImageViewer.cpp
)
```

with:

```cmake
add_library(photo-salon-lib STATIC
    src/MainWindow.cpp
    src/ImageViewer.cpp
    src/HelpOverlay.cpp
)
```

- [ ] **Step 4: Build and verify**

```bash
cmake -B _build && cmake --build _build
```

Expected: clean build with no errors. All tests still pass:

```bash
ctest --test-dir _build --output-on-failure
```

Expected: both `test_zoom` and `test_help_overlay` pass.

- [ ] **Step 5: Commit**

```bash
git add src/HelpOverlay.h src/HelpOverlay.cpp CMakeLists.txt
git commit -m "feat: add HelpOverlay widget"
```

---

## Task 3: Wire HelpOverlay into MainWindow

**Files:**
- Modify: `src/MainWindow.h`
- Modify: `src/MainWindow.cpp`

- [ ] **Step 1: Update src/MainWindow.h**

Replace the full contents with:

```cpp
#pragma once
#include <QMainWindow>
#include <QString>

class HelpOverlay;
class QResizeEvent;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QString &imagePath, QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    HelpOverlay *m_helpOverlay = nullptr;
};
```

- [ ] **Step 2: Update src/MainWindow.cpp**

Replace the full contents with:

```cpp
#include "MainWindow.h"
#include "HelpOverlay.h"
#include "ImageViewer.h"
#include <QFileInfo>
#include <QGuiApplication>
#include <QResizeEvent>
#include <QScreen>

MainWindow::MainWindow(const QString &imagePath, QWidget *parent)
    : QMainWindow(parent)
{
    auto *viewer = new ImageViewer(imagePath, this);
    setCentralWidget(viewer);

    QString filename = QFileInfo(imagePath).fileName();
    setWindowTitle(QString("photo-salon — %1").arg(filename));

    QSize imageSize = viewer->nativeImageSize();
    if (!imageSize.isEmpty()) {
        QSize available = screen()->availableGeometry().size();
        resize(imageSize.boundedTo(available));
    } else {
        resize(800, 600);
    }

    m_helpOverlay = new HelpOverlay(this);
    m_helpOverlay->resize(size());
    connect(viewer, &ImageViewer::helpVisibilityChanged, m_helpOverlay, &QWidget::setVisible);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (m_helpOverlay)
        m_helpOverlay->resize(size());
}
```

- [ ] **Step 3: Build and run all tests**

```bash
cmake -B _build && cmake --build _build && ctest --test-dir _build --output-on-failure
```

Expected: clean build, all tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/MainWindow.h src/MainWindow.cpp
git commit -m "feat: wire HelpOverlay into MainWindow"
```
