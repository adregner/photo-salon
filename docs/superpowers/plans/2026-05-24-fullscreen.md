# Fullscreen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Press **F** to toggle true fullscreen (no titlebar, no OS taskbar) and press **F** or **Escape** to exit, restoring the previous window state.

**Architecture:** `MainWindow` owns fullscreen state via a `m_windowStateBeforeFullscreen` member and a `toggleFullscreen()` slot; `ImageViewer` handles the **F** and **Escape** key events and emits a `fullscreenToggleRequested` signal; `MainWindow` connects that signal to `toggleFullscreen()` and calls `QMainWindow::showFullScreen()` / `QMainWindow::showNormal()`. The help overlay's shortcut list is updated to include the new binding.

**Tech Stack:** C++17, Qt6 (Widgets, Test), CMake, QTest (offscreen platform)

---

## File Map

| File | Change |
|------|--------|
| `src/ImageViewer.h` | Add `fullscreenToggleRequested` signal |
| `src/ImageViewer.cpp` | Handle `Key_F` and `Key_Escape` in `keyPressEvent`; emit signal |
| `src/MainWindow.h` | Add `m_windowStateBeforeFullscreen`, `toggleFullscreen()` slot |
| `src/MainWindow.cpp` | Connect signal; implement `toggleFullscreen()` |
| `src/HelpOverlay.cpp` | Add **F** and **Escape** entries to the shortcut list text |
| `tests/test_fullscreen.cpp` | New — QTest suite for fullscreen signal and key handling |
| `tests/CMakeLists.txt` | Add `test_fullscreen` target |

---

## Task 1: Write failing tests for fullscreen key events

**Files:**
- Create: `tests/test_fullscreen.cpp`
- Modify: `tests/CMakeLists.txt`

The tests drive only the `ImageViewer` in isolation (offscreen, no real window manager needed). They verify:
1. Pressing **F** emits `fullscreenToggleRequested`.
2. Pressing **Escape** emits `fullscreenToggleRequested`.
3. Pressing **F** with the help overlay open closes the help overlay AND emits the signal.
4. Zoom keys still work normally (regression guard).

- [ ] **Step 1: Create `tests/test_fullscreen.cpp`**

```cpp
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
```

- [ ] **Step 2: Add the test target to `tests/CMakeLists.txt`**

Append to the existing `tests/CMakeLists.txt` (do not remove existing targets):

```cmake
qt_add_executable(test_fullscreen test_fullscreen.cpp)
target_link_libraries(test_fullscreen PRIVATE Qt6::Test photo-salon-lib)
add_test(NAME test_fullscreen COMMAND test_fullscreen)
set_tests_properties(test_fullscreen PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

The full file after the addition:

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

qt_add_executable(test_fullscreen test_fullscreen.cpp)
target_link_libraries(test_fullscreen PRIVATE Qt6::Test photo-salon-lib)
add_test(NAME test_fullscreen COMMAND test_fullscreen)
set_tests_properties(test_fullscreen PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Attempt build — confirm it fails**

```bash
cmake -B _build && cmake --build _build 2>&1 | tail -20
```

Expected: compile error in `test_fullscreen.cpp` about `fullscreenToggleRequested` not existing on `ImageViewer`.

---

## Task 2: Add `fullscreenToggleRequested` signal and key handling to `ImageViewer`

**Files:**
- Modify: `src/ImageViewer.h`
- Modify: `src/ImageViewer.cpp`

- [ ] **Step 1: Update `src/ImageViewer.h`**

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
    void fullscreenToggleRequested();

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

- [ ] **Step 2: Update `keyPressEvent` in `src/ImageViewer.cpp`**

Replace the `keyPressEvent` function body with:

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
    case Qt::Key_F:
    case Qt::Key_Escape:
        emit fullscreenToggleRequested();
        event->accept();
        break;
    default:
        QGraphicsView::keyPressEvent(event);
        break;
    }
}
```

- [ ] **Step 3: Build and run the new tests**

```bash
cmake -B _build && cmake --build _build && QT_QPA_PLATFORM=offscreen ./_build/tests/test_fullscreen
```

Expected output:

```
********* Start testing of FullscreenTest *********
PASS   : FullscreenTest::keyF_emitsToggle()
PASS   : FullscreenTest::keyEscape_emitsToggle()
PASS   : FullscreenTest::keyF_withHelpOpen_closesHelpAndEmitsToggle()
PASS   : FullscreenTest::keyEscape_withHelpClosed_emitsToggle()
PASS   : FullscreenTest::otherKeys_doNotEmitToggle()
Totals: 5 passed, 0 failed, 0 skipped
********* Finished testing of FullscreenTest *********
```

- [ ] **Step 4: Verify all existing tests still pass**

```bash
ctest --test-dir _build --output-on-failure
```

Expected: `test_zoom`, `test_help_overlay`, and `test_fullscreen` all pass, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add src/ImageViewer.h src/ImageViewer.cpp tests/test_fullscreen.cpp tests/CMakeLists.txt
git commit -m "feat: emit fullscreenToggleRequested on F and Escape in ImageViewer"
```

---

## Task 3: Implement `toggleFullscreen()` in `MainWindow`

**Files:**
- Modify: `src/MainWindow.h`
- Modify: `src/MainWindow.cpp`

`MainWindow::toggleFullscreen()` uses `QWindow::windowStates()` to detect the current state. If currently fullscreen it calls `setWindowState(m_windowStateBeforeFullscreen)` to restore; otherwise it saves the current state and calls `showFullScreen()`. `Qt::WindowFullScreen` sets a true fullscreen window — no OS titlebar, no taskbar — on macOS, Linux, and Windows.

- [ ] **Step 1: Update `src/MainWindow.h`**

Replace the full contents with:

```cpp
#pragma once
#include <QMainWindow>
#include <QString>
#include <Qt>

class HelpOverlay;
class QResizeEvent;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QString &imagePath, QWidget *parent = nullptr);

public slots:
    void toggleFullscreen();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    HelpOverlay *m_helpOverlay = nullptr;
    Qt::WindowStates m_windowStateBeforeFullscreen = Qt::WindowNoState;
};
```

- [ ] **Step 2: Update `src/MainWindow.cpp`**

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
    m_helpOverlay->raise();
    connect(viewer, &ImageViewer::helpVisibilityChanged, m_helpOverlay, &QWidget::setVisible);
    connect(viewer, &ImageViewer::fullscreenToggleRequested, this, &MainWindow::toggleFullscreen);
}

void MainWindow::toggleFullscreen() {
    if (windowState() & Qt::WindowFullScreen) {
        setWindowState(m_windowStateBeforeFullscreen);
    } else {
        m_windowStateBeforeFullscreen = windowState();
        showFullScreen();
    }
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (m_helpOverlay)
        m_helpOverlay->resize(size());
}
```

- [ ] **Step 3: Build**

```bash
cmake -B _build && cmake --build _build
```

Expected: clean build with no errors or warnings.

- [ ] **Step 4: Run all tests**

```bash
ctest --test-dir _build --output-on-failure
```

Expected: all three test suites pass, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add src/MainWindow.h src/MainWindow.cpp
git commit -m "feat: implement toggleFullscreen in MainWindow"
```

---

## Task 4: Update the help overlay shortcut list

**Files:**
- Modify: `src/HelpOverlay.cpp`

The help overlay's `paintEvent` currently lists zoom and scroll shortcuts. Add the fullscreen and escape bindings so the user can discover them.

- [ ] **Step 1: Update the shortcut text in `src/HelpOverlay.cpp`**

Replace the full contents of `src/HelpOverlay.cpp` with:

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
        "  F          Toggle fullscreen\n"
        "  Escape     Exit fullscreen\n"
        "  ?          Show/hide this help\n"
        "\n"
        "Mouse & Other Controls\n"
        "\n"
        "  Scroll wheel    Zoom (anchored to cursor)";

    p.drawText(rect(), Qt::AlignCenter, text);
}
```

- [ ] **Step 2: Build and run all tests**

```bash
cmake -B _build && cmake --build _build && ctest --test-dir _build --output-on-failure
```

Expected: clean build, all tests pass.

- [ ] **Step 3: Smoke test manually**

```bash
./build/photo-salon /path/to/any/image.jpg
```

- Press **?** — verify the help overlay appears and lists **F** and **Escape**.
- Press **?** again — overlay dismisses.
- Press **F** — window goes fullscreen (no titlebar, no taskbar).
- Press **F** again — window returns to its previous size and position.
- Press **F** to enter fullscreen, then press **Escape** — window restores.
- Press **?** to open the help overlay while in windowed mode, then press **F** — overlay closes and window goes fullscreen.

- [ ] **Step 4: Commit**

```bash
git add src/HelpOverlay.cpp
git commit -m "feat: add fullscreen shortcuts to help overlay"
```
