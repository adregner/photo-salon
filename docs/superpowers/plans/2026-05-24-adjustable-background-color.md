# Adjustable Background Color Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Default the viewer background to solid black, and let the user press **B** to show a floating greyscale slider; moving the slider changes the background color immediately, and pressing **Escape** (or clicking elsewhere) dismisses the panel.

**Architecture:** `ImageViewer` sets `setBackgroundBrush(Qt::black)` in its constructor and exposes `setBackgroundGrey(int)` / `backgroundGrey()`. It emits `backgroundPickerRequested()` on **B**. `MainWindow` owns a `BackgroundColorPicker` child widget (hidden by default); it shows/positions it on the signal and pipes `greyChanged` → `viewer->setBackgroundGrey()`.

**Tech Stack:** Qt6 (`QGraphicsView::setBackgroundBrush`, `QSlider`, `QLabel`, `QWidget`), QTest, C++17, CMake

---

## File Structure

- Modify: `src/ImageViewer.h` — add `setBackgroundGrey(int)`, `backgroundGrey()`, `backgroundPickerRequested()` signal, `m_backgroundGrey` member
- Modify: `src/ImageViewer.cpp` — set black background in constructor, implement `setBackgroundGrey`, emit signal on B key
- Create: `src/BackgroundColorPicker.h` — floating overlay widget with QSlider
- Create: `src/BackgroundColorPicker.cpp` — implementation
- Modify: `src/MainWindow.h` — add `BackgroundColorPicker *m_colorPicker` member
- Modify: `src/MainWindow.cpp` — create picker, connect signals, position on show
- Modify: `CMakeLists.txt` — add `src/BackgroundColorPicker.cpp` to `photo-salon-lib`
- Create: `tests/test_background_color.cpp`
- Modify: `tests/CMakeLists.txt`

---

## Task 1: Register the test target

**Files:**
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Append to `tests/CMakeLists.txt`**

```cmake
qt_add_executable(test_background_color test_background_color.cpp)
target_link_libraries(test_background_color PRIVATE Qt6::Test photo-salon-lib)
add_test(NAME test_background_color COMMAND test_background_color)
set_tests_properties(test_background_color PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 2: Reconfigure**

```bash
cmake -B _build 2>&1 | tail -5
```

Expected: exits 0.

---

## Task 2: Write the failing tests

**Files:**
- Create: `tests/test_background_color.cpp`

- [ ] **Step 1: Create the test file**

```cpp
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
    // QGraphicsView::backgroundBrush() returns the brush set via setBackgroundBrush
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
```

- [ ] **Step 2: Build — expect failure**

```bash
cmake --build _build --target test_background_color 2>&1 | tail -10
```

Expected: errors about `backgroundGrey`, `setBackgroundGrey`, `backgroundPickerRequested` not existing on `ImageViewer`.

---

## Task 3: Extend `ImageViewer` with background color support

**Files:**
- Modify: `src/ImageViewer.h`
- Modify: `src/ImageViewer.cpp`

- [ ] **Step 1: Update `src/ImageViewer.h`**

Add to the `public:` section:

```cpp
void setBackgroundGrey(int value);
int backgroundGrey() const { return m_backgroundGrey; }
```

Add to `signals:`:

```cpp
void backgroundPickerRequested();
```

Add to `private:` members:

```cpp
int m_backgroundGrey = 0;
```

- [ ] **Step 2: Implement in `src/ImageViewer.cpp`**

In the constructor, add after `setTransformationAnchor(...)`:

```cpp
setBackgroundBrush(Qt::black);
```

Add the new method implementation (anywhere before the end of the file):

```cpp
void ImageViewer::setBackgroundGrey(int value) {
    m_backgroundGrey = qBound(0, value, 255);
    setBackgroundBrush(QColor(m_backgroundGrey, m_backgroundGrey, m_backgroundGrey));
}
```

In `keyPressEvent`, add a new case before `default:`:

```cpp
case Qt::Key_B:
    emit backgroundPickerRequested();
    event->accept();
    break;
```

- [ ] **Step 3: Build and run the test**

```bash
cmake -B _build && cmake --build _build 2>&1 | tail -5
QT_QPA_PLATFORM=offscreen ./_build/tests/test_background_color
```

Expected:
```
********* Start testing of BackgroundColorTest *********
PASS   : BackgroundColorTest::defaultIsBlack()
PASS   : BackgroundColorTest::setGreyChangesBackground()
PASS   : BackgroundColorTest::bKeyEmitsSignal()
PASS   : BackgroundColorTest::greyClampedTo255()
PASS   : BackgroundColorTest::greyClampedTo0()
Totals: 5 passed, 0 failed, 0 skipped
```

- [ ] **Step 4: Commit**

```bash
git add src/ImageViewer.h src/ImageViewer.cpp \
        tests/test_background_color.cpp tests/CMakeLists.txt
git commit -m "feat: add setBackgroundGrey() and backgroundPickerRequested signal to ImageViewer"
```

---

## Task 4: Create `BackgroundColorPicker` widget

**Files:**
- Create: `src/BackgroundColorPicker.h`
- Create: `src/BackgroundColorPicker.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create `src/BackgroundColorPicker.h`**

```cpp
#pragma once
#include <QWidget>

class QKeyEvent;
class QLabel;
class QSlider;

class BackgroundColorPicker : public QWidget {
    Q_OBJECT
public:
    explicit BackgroundColorPicker(QWidget *parent = nullptr);
    void setCurrentValue(int grey);

signals:
    void greyChanged(int value);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    QSlider *m_slider;
    QLabel *m_label;
};
```

- [ ] **Step 2: Create `src/BackgroundColorPicker.cpp`**

```cpp
#include "BackgroundColorPicker.h"
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QSlider>

BackgroundColorPicker::BackgroundColorPicker(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    setFixedHeight(48);

    m_label = new QLabel("Background: 0", this);
    m_label->setStyleSheet("color: white; font-size: 13px; padding: 0 8px;");

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, 255);
    m_slider->setValue(0);
    m_slider->setMinimumWidth(180);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->addWidget(m_label);
    layout->addWidget(m_slider);

    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        m_label->setText(QString("Background: %1").arg(v));
        emit greyChanged(v);
    });
}

void BackgroundColorPicker::setCurrentValue(int grey) {
    m_slider->setValue(qBound(0, grey, 255));
}

void BackgroundColorPicker::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        event->accept();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void BackgroundColorPicker::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(30, 30, 30, 220));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(rect(), 6, 6);
}
```

- [ ] **Step 3: Add `BackgroundColorPicker.cpp` to `CMakeLists.txt`**

```cmake
add_library(photo-salon-lib STATIC
    src/MainWindow.cpp
    src/ImageViewer.cpp
    src/HelpOverlay.cpp
    src/ImageFormats.cpp
    src/BackgroundColorPicker.cpp
)
```

- [ ] **Step 4: Build**

```bash
cmake -B _build && cmake --build _build 2>&1 | tail -5
```

Expected: builds cleanly.

- [ ] **Step 5: Commit**

```bash
git add src/BackgroundColorPicker.h src/BackgroundColorPicker.cpp CMakeLists.txt
git commit -m "feat: add BackgroundColorPicker floating widget"
```

---

## Task 5: Wire picker into `MainWindow`

**Files:**
- Modify: `src/MainWindow.h`
- Modify: `src/MainWindow.cpp`

- [ ] **Step 1: Update `src/MainWindow.h`**

Add the forward declaration and member:

```cpp
class BackgroundColorPicker;
```

In `private:` members:

```cpp
BackgroundColorPicker *m_colorPicker = nullptr;
```

- [ ] **Step 2: Update `src/MainWindow.cpp`**

Add `#include "BackgroundColorPicker.h"` to the includes.

In the constructor, after creating `m_helpOverlay`, add:

```cpp
m_colorPicker = new BackgroundColorPicker(this);
m_colorPicker->hide();

connect(viewer, &ImageViewer::backgroundPickerRequested, this, [this, viewer]() {
    m_colorPicker->setCurrentValue(viewer->backgroundGrey());
    // Position at bottom-left, 10px from edges
    int x = 10;
    int y = height() - m_colorPicker->sizeHint().height() - 10;
    m_colorPicker->move(x, y);
    m_colorPicker->show();
    m_colorPicker->raise();
    m_colorPicker->setFocus();
});

connect(m_colorPicker, &BackgroundColorPicker::greyChanged,
        viewer, &ImageViewer::setBackgroundGrey);
```

Also update `resizeEvent` to reposition the picker if it's visible:

```cpp
void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (m_helpOverlay)
        m_helpOverlay->resize(size());
    if (m_colorPicker && m_colorPicker->isVisible()) {
        int y = height() - m_colorPicker->sizeHint().height() - 10;
        m_colorPicker->move(10, y);
    }
}
```

- [ ] **Step 3: Build and run full suite**

```bash
cmake -B _build && cmake --build _build 2>&1 | tail -5
QT_QPA_PLATFORM=offscreen ctest --test-dir _build -V 2>&1 | tail -20
```

Expected: all tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/MainWindow.h src/MainWindow.cpp
git commit -m "feat: wire BackgroundColorPicker into MainWindow on B key"
```
