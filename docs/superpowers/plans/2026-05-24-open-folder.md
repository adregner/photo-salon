# Open Folder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Accept a folder path as a CLI argument (in addition to a single image file), opening the first image in the folder; and add a Tab key shortcut that shows a file-picker dialog listing all images in the current folder.

**Architecture:** A `resolveImagePath(arg)` free function in `ImageFormats` handles the dir-vs-file distinction in a testable way. `main.cpp` calls it and exits with an error if no images are found. `ImageViewer` emits `folderBrowseRequested()` on Tab; `MainWindow` responds by listing the current folder's images via `QInputDialog::getItem()` and calling `viewer->loadImage()` with the chosen file.

**Tech Stack:** Qt6 (`QDir`, `QInputDialog`), `supportedExtensions()` + new `resolveImagePath()` in `src/ImageFormats`, QTest, C++17

---

## Prerequisites

Both the **Expanded Format Support** plan (`src/ImageFormats.h`) and the **Folder Navigation** plan (`ImageViewer::loadImage()`, `ImageViewer::currentPath()`) must be implemented before this plan.

---

## File Structure

- Modify: `src/ImageFormats.h` — add `resolveImagePath(const QString &arg, QString *error)`
- Modify: `src/ImageFormats.cpp` — implement `resolveImagePath`
- Modify: `src/main.cpp` — use `resolveImagePath`, update usage string
- Modify: `src/ImageViewer.h` — add `folderBrowseRequested()` signal
- Modify: `src/ImageViewer.cpp` — handle Tab key
- Modify: `src/MainWindow.h` — no struct changes needed (lambda capture suffices)
- Modify: `src/MainWindow.cpp` — connect `folderBrowseRequested` to show `QInputDialog`
- Create: `tests/test_open_folder.cpp`
- Modify: `tests/CMakeLists.txt`

---

## Task 1: Add `resolveImagePath` and tests

**Files:**
- Modify: `src/ImageFormats.h`
- Modify: `src/ImageFormats.cpp`
- Create: `tests/test_open_folder.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Register new test in `tests/CMakeLists.txt`**

```cmake
qt_add_executable(test_open_folder test_open_folder.cpp)
target_link_libraries(test_open_folder PRIVATE Qt6::Test photo-salon-lib)
add_test(NAME test_open_folder COMMAND test_open_folder)
set_tests_properties(test_open_folder PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 2: Write the failing test**

```cpp
// tests/test_open_folder.cpp
#include <QtTest/QtTest>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include "ImageFormats.h"

static void makePng(const QDir &dir, const QString &name) {
    QImage img(10, 10, QImage::Format_RGB32);
    img.fill(Qt::green);
    img.save(dir.absoluteFilePath(name));
}

class OpenFolderTest : public QObject {
    Q_OBJECT
private slots:
    void resolveFile_returnsFile();
    void resolveDir_returnsFirstImage();
    void resolveDir_emptyDir_setsError();
    void resolveNonExistent_setsError();
};

void OpenFolderTest::resolveFile_returnsFile() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    makePng(QDir(tmp.path()), "photo.png");
    QString file = QDir(tmp.path()).absoluteFilePath("photo.png");

    QString error;
    QString result = resolveImagePath(file, &error);
    QCOMPARE(result, file);
    QVERIFY(error.isEmpty());
}

void OpenFolderTest::resolveDir_returnsFirstImage() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    makePng(dir, "alpha.png");
    makePng(dir, "beta.png");
    makePng(dir, "gamma.png");

    QString error;
    QString result = resolveImagePath(tmp.path(), &error);
    QCOMPARE(result, dir.absoluteFilePath("alpha.png"));
    QVERIFY(error.isEmpty());
}

void OpenFolderTest::resolveDir_emptyDir_setsError() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QString error;
    QString result = resolveImagePath(tmp.path(), &error);
    QVERIFY(result.isEmpty());
    QVERIFY(!error.isEmpty());
}

void OpenFolderTest::resolveNonExistent_setsError() {
    QString error;
    QString result = resolveImagePath("/nonexistent/path/that/does/not/exist.jpg", &error);
    QVERIFY(result.isEmpty());
    QVERIFY(!error.isEmpty());
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    OpenFolderTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_open_folder.moc"
```

- [ ] **Step 3: Build — expect failure**

```bash
cmake -B _build && cmake --build _build --target test_open_folder 2>&1 | tail -10
```

Expected: error about `resolveImagePath` not declared.

- [ ] **Step 4: Add `resolveImagePath` declaration to `src/ImageFormats.h`**

```cpp
#pragma once
#include <QString>
#include <QStringList>

// Returns glob patterns for all image formats Qt6 supports, e.g. "*.png", "*.jpg".
// Suitable for use as QDir::entryList() nameFilters.
QStringList supportedExtensions();

// Resolves a CLI argument to an absolute image file path.
// If arg is a directory, returns the first image file (sorted by name).
// If arg is a file, returns its absolute path.
// Returns an empty string and sets *error on failure (non-null error pointer only).
QString resolveImagePath(const QString &arg, QString *error = nullptr);
```

- [ ] **Step 5: Implement `resolveImagePath` in `src/ImageFormats.cpp`**

```cpp
#include "ImageFormats.h"
#include <QDir>
#include <QFileInfo>
#include <QImageReader>

QStringList supportedExtensions() {
    QStringList filters;
    for (const QByteArray &fmt : QImageReader::supportedImageFormats())
        filters << QString("*.%1").arg(QString::fromLatin1(fmt).toLower());
    return filters;
}

QString resolveImagePath(const QString &arg, QString *error) {
    QFileInfo info(arg);

    if (!info.exists()) {
        if (error) *error = QString("File or folder not found: %1").arg(arg);
        return {};
    }

    if (info.isDir()) {
        QDir dir(arg);
        QStringList files = dir.entryList(supportedExtensions(), QDir::Files, QDir::Name);
        if (files.isEmpty()) {
            if (error) *error = QString("No supported images found in: %1").arg(arg);
            return {};
        }
        return dir.absoluteFilePath(files.first());
    }

    return info.absoluteFilePath();
}
```

- [ ] **Step 6: Build and run the test**

```bash
cmake -B _build && cmake --build _build 2>&1 | tail -5
QT_QPA_PLATFORM=offscreen ./_build/tests/test_open_folder
```

Expected:
```
********* Start testing of OpenFolderTest *********
PASS   : OpenFolderTest::resolveFile_returnsFile()
PASS   : OpenFolderTest::resolveDir_returnsFirstImage()
PASS   : OpenFolderTest::resolveDir_emptyDir_setsError()
PASS   : OpenFolderTest::resolveNonExistent_setsError()
Totals: 4 passed, 0 failed, 0 skipped
```

- [ ] **Step 7: Commit**

```bash
git add src/ImageFormats.h src/ImageFormats.cpp \
        tests/test_open_folder.cpp tests/CMakeLists.txt
git commit -m "feat: add resolveImagePath() to handle directory CLI arguments"
```

---

## Task 2: Update `main.cpp` to accept folder paths

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Replace `src/main.cpp` with folder-aware version**

```cpp
#include <QApplication>
#include <QTextStream>
#include "ImageFormats.h"
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        QTextStream err(stderr);
        err << "Usage: photo-salon <image.jpg|folder>\n";
        return 1;
    }

    QApplication app(argc, argv);

    QString error;
    QString path = resolveImagePath(QString::fromLocal8Bit(argv[1]), &error);
    if (path.isEmpty()) {
        QTextStream err(stderr);
        err << error << "\n";
        return 1;
    }

    MainWindow window(path);
    window.show();
    return app.exec();
}
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build _build --target photo-salon 2>&1 | tail -5
```

Expected: builds cleanly.

- [ ] **Step 3: Smoke-test with a folder path**

```bash
mkdir -p /tmp/test-photos
cp /path/to/any/image.jpg /tmp/test-photos/photo.jpg
./_build/photo-salon /tmp/test-photos
```

Expected: application launches and shows `photo.jpg`.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat: accept folder path as CLI argument, open first image"
```

---

## Task 3: Add Tab key folder-browse in `ImageViewer` and `MainWindow`

**Files:**
- Modify: `src/ImageViewer.h`
- Modify: `src/ImageViewer.cpp`
- Modify: `src/MainWindow.cpp`

- [ ] **Step 1: Add `folderBrowseRequested` signal to `src/ImageViewer.h`**

In the `signals:` section, add:

```cpp
void folderBrowseRequested();
```

- [ ] **Step 2: Handle Tab key in `ImageViewer::keyPressEvent`**

In `src/ImageViewer.cpp`, in the `switch` block inside `keyPressEvent`, add before `default:`:

```cpp
case Qt::Key_Tab:
    emit folderBrowseRequested();
    event->accept();
    break;
```

- [ ] **Step 3: Handle the signal in `MainWindow::MainWindow`**

In `src/MainWindow.cpp`, add the necessary includes and connect the signal. Add `#include <QInputDialog>` to the includes, then inside the constructor after the other `connect()` calls:

```cpp
connect(viewer, &ImageViewer::folderBrowseRequested, this, [this, viewer]() {
    QString currentPath = viewer->currentPath();
    if (currentPath.isEmpty()) return;

    QDir dir = QFileInfo(currentPath).absoluteDir();
    QStringList files = dir.entryList(supportedExtensions(), QDir::Files, QDir::Name);
    if (files.isEmpty()) return;

    int current = files.indexOf(QFileInfo(currentPath).fileName());
    bool ok = false;
    QString selected = QInputDialog::getItem(
        this,
        QStringLiteral("Open Image"),
        QStringLiteral("Select image:"),
        files,
        qMax(0, current),
        /*editable=*/false,
        &ok);
    if (ok && !selected.isEmpty())
        viewer->loadImage(dir.absoluteFilePath(selected));
});
```

Also add `#include "ImageFormats.h"` to `src/MainWindow.cpp` if not already present.

- [ ] **Step 4: Build and run full suite**

```bash
cmake -B _build && cmake --build _build 2>&1 | tail -5
QT_QPA_PLATFORM=offscreen ctest --test-dir _build -V 2>&1 | tail -20
```

Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/ImageViewer.h src/ImageViewer.cpp src/MainWindow.cpp
git commit -m "feat: add Tab key folder-browse dialog to jump between images in folder"
```
