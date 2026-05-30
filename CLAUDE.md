# photo-salon — Agent Instructions

## Project Overview

C++ Qt6 desktop image viewer. Cross-platform: Linux, macOS (primary dev/test), and Windows 10/11.

## Build

```bash
./build
```

On Linux, if Qt 6.11+ is not installed the script downloads it automatically via `fetch-linux-qt.sh`.
On macOS, if Qt 6.11+ is not installed the script prints the `brew install qt` command and exits.

If `compile_commands.json` is missing or out of date, run this to create it:

```bash
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON .
```

## Windows Cross-Compilation

Cross-compile a static `photo-salon.exe` from macOS using `clang-cl` and `lld-link` (MSVC ABI). See `WINDOWS.md` for prerequisites, directory layout, and full instructions.

```bash
./build-windows.sh
# → _build_win/photo-salon.exe
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
- Only use formal specs and implementation plans for more complex feature development
- All image formats are JPG until format support is added (see ROADMAP.md)
- When adding new features, check to see if `README.md`, `CLAUDE.md`, or `ROADMAP.md` needs to be updated to reflect the new changes

## Architecture

- `ImageViewer` (QGraphicsView subclass) — core display component; owns the scene and pixmap item
- `MainWindow` (QMainWindow subclass) — orchestrates the display pipeline and owns all image-transform state
- `main.cpp` — entry point; CLI arg parsing only, no business logic

### Display pipeline

All features that modify what is shown on screen must participate in a single ordered pipeline owned by `MainWindow`. The pipeline runs in this order:

1. **Disk image** — loaded by `ImageViewer::loadImage()`, captured into both `MainWindow::m_diskPixmap` and `MainWindow::m_basePixmap` via the `imagePathChanged` signal.
2. **Orientation** — R/H/V keys rotate or flip the image. `applyOrientationTransform()` updates `m_orientedDiskPixmap` (full image at current orientation) and transforms the saved crop rect into the new coordinate space before updating the crop base.
3. **Crop** — `ImageViewer` manages the crop UI and applies the crop rect to `m_pixmapItem`. When crop exits (`cropModeChanged(false)`), `MainWindow` updates `m_basePixmap = viewer->pixmap()` (the freshly-cropped image). `setBasePixmapForCrop()` is always called with `m_orientedDiskPixmap` so that re-entering crop always shows the full oriented original — the user can expand the selection as well as shrink it.
4. **B&W conversion** — `BwConverter::convert()` runs off the main thread via `QtConcurrent`. It always operates on `m_originalImage` (derived from `m_basePixmap`). After crop or orientation change, the handler refreshes `m_originalImage` from the new `m_basePixmap` and re-triggers conversion.
5. **Display** — `ImageViewer::setDisplayPixmap()` swaps the pixmap in `m_pixmapItem` and refreshes the scene rect if dimensions changed (e.g. after 90° rotation).

Three pixmap fields track image state in `MainWindow`:
- `m_diskPixmap` — the image exactly as loaded from disk. Updated only on load/navigation. **Never modified by crop, orientation, or BW.**
- `m_orientedDiskPixmap` — `m_diskPixmap` with all rotation/flip transforms applied. This is the full-size crop base. Updated on load/navigation (equals `m_diskPixmap`) and whenever orientation changes. Always passed to `setBasePixmapForCrop()`.
- `m_basePixmap` — `m_orientedDiskPixmap` with the current crop applied. Updated on load/navigation (equals `m_diskPixmap`), on every crop application, and on every orientation change. Used as the BW source and restored by `deactivateBw()`. **Never cleared.**

Any new feature that transforms the displayed image must read from `m_basePixmap` as its input and write its result back through `setDisplayPixmap()`. If the feature permanently changes image content (as crop and orientation do), it must update `m_basePixmap` accordingly. Features that are purely non-destructive display transforms (like BW) leave `m_basePixmap` unchanged.

## Key Notes

- `fitInView` must be called in both the constructor and `showEvent` — the widget has no real size until shown
- `qt_standard_project_setup()` handles MOC automatically — do not add `CMAKE_AUTOMOC` manually
- `setDragMode(ScrollHandDrag)` enables pan; zoom is implemented via `wheelEvent` and keyboard +/-/0
