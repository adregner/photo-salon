# Photo Salon — Skeleton Design

**Date:** 2026-05-21
**Scope:** Initial skeleton — display a single JPG image specified on the CLI, fit-to-window

---

## Overview

A cross-platform C++ desktop image viewer built with Qt6 and CMake. The skeleton establishes the correct architectural foundation for the full feature set (zoom, pan, crop, resize) without implementing those features yet.

Target platforms: macOS (develop/test), Windows 10 and 11 (compile and run).

---

## Architecture

```
photo-salon/
├── CMakeLists.txt
├── README.md
├── CLAUDE.md
├── ROADMAP.md
├── docs/
│   └── superpowers/
│       └── specs/
│           └── 2026-05-21-photo-salon-skeleton-design.md
└── src/
    ├── main.cpp
    ├── MainWindow.h
    ├── MainWindow.cpp
    ├── ImageViewer.h
    └── ImageViewer.cpp
```

### Components

**`main.cpp`**
- Validates `argc == 2`; prints usage to stderr and exits with code 1 otherwise
- Constructs `QApplication`, `MainWindow` (passing the image path), calls `show()` and `exec()`

**`MainWindow`** (`QMainWindow` subclass)
- Constructs an `ImageViewer` as its central widget
- Sets window title to `photo-salon — <filename>` (bare filename, not full path)
- No menu bar or toolbar in the skeleton

**`ImageViewer`** (`QGraphicsView` subclass)
- Owns a `QGraphicsScene` and a `QGraphicsPixmapItem`
- On construction: loads the JPG via `QPixmap::load(path)`, adds it to the scene, sets `Qt::SmoothTransformation` render hint
- Sets `setDragMode(QGraphicsView::ScrollHandDrag)` — forward-looking placeholder for pan
- Calls `fitInView(scene()->itemsBoundingRect(), Qt::KeepAspectRatio)` in both constructor and overridden `showEvent` to correctly fit the image once the window has a real size
- If the image fails to load, shows a red error message in the window instead of crashing

---

## Build System

Qt6 CMake integration:

```cmake
cmake_minimum_required(VERSION 3.16)
project(photo-salon)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

qt_add_executable(photo-salon
    src/main.cpp
    src/MainWindow.cpp
    src/ImageViewer.cpp
)

target_link_libraries(photo-salon PRIVATE Qt6::Widgets)
```

- `qt_standard_project_setup()` handles MOC, UIC, and RCC automatically
- No `.pro` file; qmake is not used

### macOS build

```bash
brew install qt
cmake -B build -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build
```

### Windows build

```powershell
cmake -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2022_64  # replace 6.x.x with installed version
cmake --build build --config Release
```

---

## Project Documentation Files

**`README.md`** — How to build and run on macOS and Windows, prerequisites, basic usage.

**`CLAUDE.md`** — Agent instructions: build commands, project conventions, what not to touch.

**`ROADMAP.md`** — Features planned but not in this skeleton:
- Zoom in/out (mouse wheel and keyboard shortcuts)
- Pan (click-drag)
- Crop tool (rubber-band selection, save cropped region)
- Resize/resample output
- Folder navigation (open next/previous image in the same folder)
- Support for image formats beyond JPG

---

## Error Handling

- Invalid/missing CLI argument: print usage to stderr, exit code 1
- Image fails to load: display a `QGraphicsTextItem` with the error message in the scene rather than crashing

---

## Out of Scope for This Skeleton

- Zoom, pan, crop, resize — architecture supports them but they are not wired up
- Toolbar, menu bar, keyboard shortcuts
- Non-JPG formats
- Folder browsing
