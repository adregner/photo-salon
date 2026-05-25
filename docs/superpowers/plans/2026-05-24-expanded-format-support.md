# Expanded Format Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose a `supportedExtensions()` helper that enumerates all image formats Qt6 supports natively, and make it available to the rest of the codebase (particularly folder navigation).

**Architecture:** Qt6 already loads any format its plugins support via `QPixmap`/`QImageReader` — no format-specific code is needed. The work is exposing a `QStringList supportedExtensions()` free function (returning `"*.png"` style glob patterns) in a new `ImageFormats` translation unit so folder navigation and open-folder can filter directory listings consistently.

**Tech Stack:** Qt6 (`QImageReader`), C++17, QTest, CMake

---

## File Structure

- Create: `src/ImageFormats.h` — declares `supportedExtensions()`
- Create: `src/ImageFormats.cpp` — implements via `QImageReader::supportedImageFormats()`
- Modify: `CMakeLists.txt` — add `src/ImageFormats.cpp` to `photo-salon-lib`
- Create: `tests/test_image_formats.cpp` — verifies common formats present
- Modify: `tests/CMakeLists.txt` — register new test target

---

## Task 1: Register the test target

**Files:**
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Append the new test to `tests/CMakeLists.txt`**

Add at the end of `tests/CMakeLists.txt`:

```cmake
qt_add_executable(test_image_formats test_image_formats.cpp)
target_link_libraries(test_image_formats PRIVATE Qt6::Test photo-salon-lib)
add_test(NAME test_image_formats COMMAND test_image_formats)
set_tests_properties(test_image_formats PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 2: Rebuild to confirm CMake picks up the new target**

```bash
cmake -B _build && cmake --build _build 2>&1 | tail -5
```

Expected: build succeeds (test_image_formats target appears in output), no errors.

---

## Task 2: Write the failing test

**Files:**
- Create: `tests/test_image_formats.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
#include <QtTest/QtTest>
#include <QApplication>
#include "ImageFormats.h"

class ImageFormatsTest : public QObject {
    Q_OBJECT
private slots:
    void containsJpeg();
    void containsPng();
    void containsBmp();
    void noEmptyEntries();
    void allEntriesHaveGlobPrefix();
};

void ImageFormatsTest::containsJpeg() {
    QStringList exts = supportedExtensions();
    QVERIFY(exts.contains("*.jpg") || exts.contains("*.jpeg"));
}

void ImageFormatsTest::containsPng() {
    QVERIFY(supportedExtensions().contains("*.png"));
}

void ImageFormatsTest::containsBmp() {
    QVERIFY(supportedExtensions().contains("*.bmp"));
}

void ImageFormatsTest::noEmptyEntries() {
    for (const QString &e : supportedExtensions())
        QVERIFY(!e.isEmpty());
}

void ImageFormatsTest::allEntriesHaveGlobPrefix() {
    for (const QString &e : supportedExtensions())
        QVERIFY2(e.startsWith("*."), qPrintable(e));
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    ImageFormatsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_image_formats.moc"
```

- [ ] **Step 2: Rebuild — expect compile failure**

```bash
cmake --build _build --target test_image_formats 2>&1 | tail -10
```

Expected: error `'ImageFormats.h' file not found` (or linker error on `supportedExtensions`).

---

## Task 3: Implement `ImageFormats`

**Files:**
- Create: `src/ImageFormats.h`
- Create: `src/ImageFormats.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create the header**

```cpp
// src/ImageFormats.h
#pragma once
#include <QStringList>

// Returns glob patterns for all image formats Qt6 supports, e.g. "*.png", "*.jpg".
// Suitable for use as QDir::entryList() nameFilters.
QStringList supportedExtensions();
```

- [ ] **Step 2: Create the implementation**

```cpp
// src/ImageFormats.cpp
#include "ImageFormats.h"
#include <QImageReader>

QStringList supportedExtensions() {
    QStringList filters;
    for (const QByteArray &fmt : QImageReader::supportedImageFormats())
        filters << QString("*.%1").arg(QString::fromLatin1(fmt).toLower());
    return filters;
}
```

- [ ] **Step 3: Add to the library target in `CMakeLists.txt`**

In the `add_library(photo-salon-lib STATIC ...)` block, add `src/ImageFormats.cpp`:

```cmake
add_library(photo-salon-lib STATIC
    src/MainWindow.cpp
    src/ImageViewer.cpp
    src/HelpOverlay.cpp
    src/ImageFormats.cpp
)
```

- [ ] **Step 4: Rebuild**

```bash
cmake -B _build && cmake --build _build 2>&1 | tail -5
```

Expected: build succeeds with no errors.

- [ ] **Step 5: Run the test — expect PASS**

```bash
QT_QPA_PLATFORM=offscreen ./_build/tests/test_image_formats
```

Expected output:
```
********* Start testing of ImageFormatsTest *********
PASS   : ImageFormatsTest::containsJpeg()
PASS   : ImageFormatsTest::containsPng()
PASS   : ImageFormatsTest::containsBmp()
PASS   : ImageFormatsTest::noEmptyEntries()
PASS   : ImageFormatsTest::allEntriesHaveGlobPrefix()
Totals: 5 passed, 0 failed, 0 skipped
```

- [ ] **Step 6: Run full test suite**

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir _build -V 2>&1 | tail -20
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/ImageFormats.h src/ImageFormats.cpp CMakeLists.txt \
        tests/test_image_formats.cpp tests/CMakeLists.txt
git commit -m "feat: add supportedExtensions() helper for Qt6 image format enumeration"
```
