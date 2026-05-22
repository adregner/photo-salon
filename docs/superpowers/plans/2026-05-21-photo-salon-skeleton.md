# Photo Salon Skeleton Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a skeleton C++ Qt6 desktop app that accepts a JPG path on the CLI and displays it fit-to-window.

**Architecture:** `main.cpp` handles CLI args and wires up `QApplication` + `MainWindow`. `MainWindow` is a thin `QMainWindow` shell that hosts `ImageViewer` as its central widget. `ImageViewer` is a `QGraphicsView` subclass that owns the scene and pixmap item — the correct foundation for future zoom, pan, and crop.

**Tech Stack:** C++17, Qt6 (Widgets), CMake 3.16+, Homebrew (macOS)

---

## File Map

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `CMakeLists.txt` | Build system, Qt6 discovery |
| Create | `.gitignore` | Exclude build artifacts |
| Create | `src/main.cpp` | Entry point, CLI arg parsing |
| Create | `src/MainWindow.h` | QMainWindow subclass declaration |
| Create | `src/MainWindow.cpp` | QMainWindow subclass implementation |
| Create | `src/ImageViewer.h` | QGraphicsView subclass declaration |
| Create | `src/ImageViewer.cpp` | QGraphicsView subclass implementation |
| Create | `README.md` | Build + run instructions for macOS and Windows |
| Create | `CLAUDE.md` | Agent instructions |
| Create | `ROADMAP.md` | Planned features not in this skeleton |

---

## Task 1: Prerequisites and git init

**Files:**
- Create: `.gitignore`

- [ ] **Step 1: Install Qt6 via Homebrew**

```bash
brew install qt cmake
```

Expected: Qt6 installed (or already up to date). Verify with:
```bash
brew --prefix qt
# e.g. /opt/homebrew/opt/qt
qmake --version  # should say Qt version 6.x.x
```

- [ ] **Step 2: Initialize git repository**

```bash
cd /Users/andrew_regner/dev/personal/photo-salon
git init
```

Expected: `Initialized empty Git repository in .../photo-salon/.git/`

- [ ] **Step 3: Create .gitignore**

Create `.gitignore`:
```
build/
*.user
.DS_Store
Thumbs.db
```

- [ ] **Step 4: Commit**

```bash
git add .gitignore
git commit -m "chore: initialize repo with .gitignore"
```

---

## Task 2: CMake build system

**Files:**
- Create: `CMakeLists.txt`

- [ ] **Step 1: Create CMakeLists.txt**

Create `CMakeLists.txt` at the project root:
```cmake
cmake_minimum_required(VERSION 3.16)
project(photo-salon VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

qt_add_executable(photo-salon
    src/main.cpp
    src/MainWindow.cpp
    src/ImageViewer.cpp
)

target_include_directories(photo-salon PRIVATE src)
target_link_libraries(photo-salon PRIVATE Qt6::Widgets)
```

- [ ] **Step 2: Verify CMake configures**

```bash
cmake -B build -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
```

Expected output ends with:
```
-- Build files have been written to: .../photo-salon/build
```
No errors about missing Qt6.

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: add CMakeLists.txt with Qt6 Widgets"
```

---

## Task 3: ImageViewer — header and stub implementation

**Files:**
- Create: `src/ImageViewer.h`
- Create: `src/ImageViewer.cpp` (stub — compiles but does nothing yet)

- [ ] **Step 1: Create ImageViewer.h**

Create `src/ImageViewer.h`:
```cpp
#pragma once
#include <QGraphicsView>
#include <QString>

class QGraphicsScene;

class ImageViewer : public QGraphicsView {
    Q_OBJECT

public:
    explicit ImageViewer(const QString &imagePath, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void fitImage();

    QGraphicsScene *m_scene;
};
```

- [ ] **Step 2: Create ImageViewer.cpp stub**

Create `src/ImageViewer.cpp`:
```cpp
#include "ImageViewer.h"
#include <QGraphicsScene>
#include <QShowEvent>

ImageViewer::ImageViewer(const QString &imagePath, QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    Q_UNUSED(imagePath)
    setScene(m_scene);
}

void ImageViewer::showEvent(QShowEvent *event) {
    QGraphicsView::showEvent(event);
    fitImage();
}

void ImageViewer::fitImage() {
    if (!m_scene->items().isEmpty())
        fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
}
```

- [ ] **Step 3: Commit**

```bash
git add src/ImageViewer.h src/ImageViewer.cpp
git commit -m "feat: add ImageViewer stub (QGraphicsView subclass)"
```

---

## Task 4: MainWindow

**Files:**
- Create: `src/MainWindow.h`
- Create: `src/MainWindow.cpp`

- [ ] **Step 1: Create MainWindow.h**

Create `src/MainWindow.h`:
```cpp
#pragma once
#include <QMainWindow>
#include <QString>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QString &imagePath, QWidget *parent = nullptr);
};
```

- [ ] **Step 2: Create MainWindow.cpp**

Create `src/MainWindow.cpp`:
```cpp
#include "MainWindow.h"
#include "ImageViewer.h"
#include <QFileInfo>

MainWindow::MainWindow(const QString &imagePath, QWidget *parent)
    : QMainWindow(parent)
{
    auto *viewer = new ImageViewer(imagePath, this);
    setCentralWidget(viewer);

    QString filename = QFileInfo(imagePath).fileName();
    setWindowTitle(QString("photo-salon — %1").arg(filename));
    resize(1024, 768);
}
```

- [ ] **Step 3: Commit**

```bash
git add src/MainWindow.h src/MainWindow.cpp
git commit -m "feat: add MainWindow (thin QMainWindow shell)"
```

---

## Task 5: main.cpp and first build

**Files:**
- Create: `src/main.cpp`

- [ ] **Step 1: Create main.cpp**

Create `src/main.cpp`:
```cpp
#include <QApplication>
#include <QTextStream>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        QTextStream err(stderr);
        err << "Usage: photo-salon <image.jpg>\n";
        return 1;
    }

    QApplication app(argc, argv);
    MainWindow window(argv[1]);
    window.show();
    return app.exec();
}
```

- [ ] **Step 2: Build**

```bash
cmake --build build
```

Expected: builds without errors. Binary at `build/photo-salon`.

- [ ] **Step 3: Verify CLI error path**

```bash
./build/photo-salon
```

Expected output to stderr:
```
Usage: photo-salon <image.jpg>
```
And exit code 1:
```bash
echo $?
# 1
```

- [ ] **Step 4: Verify app opens (blank window)**

```bash
./build/photo-salon /path/to/any/existing.jpg
```

Expected: a 1024×768 window titled `photo-salon — existing.jpg` opens. The image area is blank (ImageViewer is still a stub). Close the window.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat: add main.cpp with CLI arg parsing"
```

---

## Task 6: ImageViewer — full image loading implementation

**Files:**
- Modify: `src/ImageViewer.cpp`

- [ ] **Step 1: Replace ImageViewer.cpp with full implementation**

Replace the contents of `src/ImageViewer.cpp`:
```cpp
#include "ImageViewer.h"
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsTextItem>
#include <QShowEvent>
#include <QPixmap>
#include <QPainter>

ImageViewer::ImageViewer(const QString &imagePath, QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QPixmap pixmap(imagePath);
    if (pixmap.isNull()) {
        auto *errorText = new QGraphicsTextItem(
            QString("Failed to load image:\n%1").arg(imagePath));
        errorText->setDefaultTextColor(Qt::red);
        m_scene->addItem(errorText);
    } else {
        m_scene->addPixmap(pixmap);
        m_scene->setSceneRect(pixmap.rect());
        fitImage();
    }
}

void ImageViewer::showEvent(QShowEvent *event) {
    QGraphicsView::showEvent(event);
    fitImage();
}

void ImageViewer::fitImage() {
    if (!m_scene->items().isEmpty())
        fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
}
```

- [ ] **Step 2: Build**

```bash
cmake --build build
```

Expected: builds without errors or warnings.

- [ ] **Step 3: Test with a real JPG**

```bash
./build/photo-salon /path/to/a/real/photo.jpg
```

Expected: window opens, image displays fit-to-window with correct aspect ratio. Window title shows `photo-salon — photo.jpg`.

- [ ] **Step 4: Test error path**

```bash
./build/photo-salon /path/to/nonexistent.jpg
```

Expected: window opens showing red text: `Failed to load image: /path/to/nonexistent.jpg`

- [ ] **Step 5: Commit**

```bash
git add src/ImageViewer.cpp
git commit -m "feat: implement ImageViewer image loading and fit-to-window display"
```

---

## Task 7: Documentation files

**Files:**
- Create: `README.md`
- Create: `CLAUDE.md`
- Create: `ROADMAP.md`

- [ ] **Step 1: Create README.md**

Create `README.md`:
```markdown
# photo-salon

A cross-platform desktop image viewer built with C++ and Qt6.

## Prerequisites

### macOS
- [Homebrew](https://brew.sh/)
- Qt6 and CMake: `brew install qt cmake`

### Windows
- [Qt6](https://www.qt.io/download) via Qt Online Installer — select the MSVC 2022 64-bit component
- [CMake](https://cmake.org/download/)
- Visual Studio 2022 (Community edition is sufficient)

## Building

### macOS

```bash
cmake -B build -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build
```

### Windows

Open a **Developer Command Prompt for VS 2022**, then:

```powershell
cmake -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2022_64
cmake --build build --config Release
```

Replace `6.x.x` with the Qt version you installed (e.g. `6.8.0`).

## Running

### macOS

```bash
./build/photo-salon /path/to/image.jpg
```

### Windows

```powershell
.\build\Release\photo-salon.exe C:\path\to\image.jpg
```

## Usage

```
photo-salon <image.jpg>
```

Opens the specified JPEG image in a resizable window, scaled to fit.
```

- [ ] **Step 2: Create CLAUDE.md**

Create `CLAUDE.md`:
```markdown
# photo-salon — Agent Instructions

## Project Overview

C++ Qt6 desktop image viewer. Cross-platform: macOS (primary dev/test) and Windows 10/11.

## Build

```bash
cmake -B build -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build
```

## Run

```bash
./build/photo-salon /path/to/image.jpg
```

## Conventions

- C++17
- Qt6 only — no Qt5 compatibility shims
- CMake build system — no qmake, no `.pro` files
- Source files live in `src/`
- Design specs in `docs/superpowers/specs/`
- Implementation plans in `docs/superpowers/plans/`
- All image formats are JPG until format support is added (see ROADMAP.md)

## Architecture

- `ImageViewer` (QGraphicsView subclass) — core display component; owns the scene and pixmap item
- `MainWindow` (QMainWindow subclass) — thin shell; sets window title and hosts ImageViewer
- `main.cpp` — entry point; CLI arg parsing only, no business logic

## Key Notes

- `fitInView` must be called in both the constructor and `showEvent` — the widget has no real size until shown
- `qt_standard_project_setup()` handles MOC automatically — do not add `CMAKE_AUTOMOC` manually
- `setDragMode(ScrollHandDrag)` is a forward-looking stub for pan — not fully functional until zoom is implemented
```

- [ ] **Step 3: Create ROADMAP.md**

Create `ROADMAP.md`:
```markdown
# Roadmap

Planned features not yet implemented in photo-salon.

## Upcoming

- **Zoom** — mouse wheel to zoom in/out; keyboard shortcuts (`+` / `-` / `0` to reset)
- **Pan** — click-drag to pan when zoomed in (architecture stub already in place via `ScrollHandDrag`)
- **Crop tool** — rubber-band selection to define a crop region; save cropped output to a new file
- **Resize/resample** — resize image to specific dimensions and export
- **Folder navigation** — open the next or previous image in the same folder as the current file
- **Expanded format support** — PNG, TIFF, WebP, and other formats beyond JPG
```

- [ ] **Step 4: Build one final time to confirm nothing broke**

```bash
cmake --build build
```

Expected: clean build, no errors.

- [ ] **Step 5: Commit**

```bash
git add README.md CLAUDE.md ROADMAP.md
git commit -m "docs: add README, CLAUDE.md, and ROADMAP"
```

---

## Done

At this point the skeleton is complete:
- `./build/photo-salon image.jpg` opens a window displaying the image fit-to-window
- CLI arg validation with a usage message
- Error handling for unloadable images
- `QGraphicsView` foundation ready for zoom, pan, and crop
- README, CLAUDE.md, and ROADMAP in place
