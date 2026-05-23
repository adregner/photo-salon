# photo-salon — Agent Instructions

## Project Overview

C++ Qt6 desktop image viewer. Cross-platform: Linux, macOS (primary dev/test), and Windows 10/11.

## Build

```bash
cmake -B _build
cmake --build _build
```

If `compile_commands.json` is missing or out of date, run this to create it:

```bash
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON .
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
- `MainWindow` (QMainWindow subclass) — thin shell; sets window title and hosts ImageViewer
- `main.cpp` — entry point; CLI arg parsing only, no business logic

## Key Notes

- `fitInView` must be called in both the constructor and `showEvent` — the widget has no real size until shown
- `qt_standard_project_setup()` handles MOC automatically — do not add `CMAKE_AUTOMOC` manually
- `setDragMode(ScrollHandDrag)` is a forward-looking stub for pan — not fully functional until zoom is implemented
