# photo-salon — Agent Instructions

C++ / Qt6 desktop image viewer for in-person photography salons. Cross-platform:
Linux, macOS (primary dev/test), and Windows 10/11. Single-window viewer with
non-destructive edits (orientation, free-angle rotation, crop, light/level & colour
adjustments, hue-selective B&W) and a metadata overlay.

## Session start — do this first, every session

Sync the local checkout before any work:

```bash
git fetch origin main
git checkout main && git pull --ff-only origin main   # if not already on main
```

Ensure `main` and `origin/main` point at the same latest commit and `main` tracks
`origin/main`. If you're working on a feature branch, still fetch and fast-forward
`main` so it's current (`git fetch origin main && git branch -f main origin/main`
when not checked out on it). `origin/main` is occasionally force-updated, so always
fetch rather than trusting the local ref.

## Build · Run · Test

```bash
./build                      # configure + build → _build/  (Release)
./build run /path/img.jpg    # build the app target and launch it
cd _build && ctest --output-on-failure   # run the test suite (headless)
```

- Qt 6.11+ required. On Linux `./build` auto-fetches it; on macOS it prints
  `brew install qt` and exits.
- Optional codecs: `libheif` + `OpenJPEG` (`brew install libheif openjpeg`,
  `apt install libheif-dev libheif-plugin-libde265 libopenjp2-7-dev`). Missing ones only
  drop that format — CMake warns and carries on. The Windows cross-build gets MSVC builds
  of both from the pre-built codecs bundle; see `doc/WINDOWS.md`.
- **Release builds require them**: `PHOTO_SALON_REQUIRE_CODECS=1 ./build` (set by
  `bundle-macos.sh` and both release workflows) turns a missing codec into a fatal
  configure error, so no published binary lacks a format it advertises.
- Tests are Qt Test binaries run with `QT_QPA_PLATFORM=offscreen` (CTest sets this).
- Regenerate `compile_commands.json` when stale:
  `cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON .`
- Packaging, dependencies, and the release workflow: **`doc/BUILD.md`**.
- Windows cross-compile from macOS/Linux (`./build-windows.sh`): **`doc/WINDOWS.md`**.
  Produces a standalone `.exe` (static CRT, no VC++ Redistributable). Its pre-built
  inputs are pinned in `windows-deps.lock` and regenerated on a Windows machine by
  `windows/toolchain/Make-WindowsToolchain.ps1`.

## Conventions

- C++17, Qt6 only — no Qt5 shims, no qmake/`.pro` files, CMake only.
- Source in `src/`; tests in `tests/` (link `photo-salon-lib`, not `main.cpp`).
- Project docs in `doc/*.md`. Keep `CLAUDE.md` lean — put depth in `doc/`.
- Loads **any format Qt's image plugins support** (JPEG primary; PNG etc. work too),
  plus **HEIC/HEIF** and the **JPEG 2000** family (`.jpf`/`.jpx`/`.jp2`/`.j2k`) via the
  static plugins in `src/imageformats/` (libheif + OpenJPEG; optional at build time).
  Folder navigation excludes `*.svg`.
- When adding a feature, check whether `README.md`, `CLAUDE.md`, `ROADMAP.md`, and the
  `HelpOverlay` shortcut list need updating.
- Adding a Qt module or library that pulls in new Windows system DLLs needs no action:
  the SDK bundle vendors every x64 import library the SDK ships.

## Architecture (summary)

Full detail — pipeline, event routing, crop/orientation/B&W internals, metadata overlay —
is in **`doc/ARCHITECTURE.md`**. The essentials:

- **`ImageViewer`** (`QGraphicsView`) — display + input; owns the scene and the single
  pixmap item. It never holds transform/business state: keys it doesn't act on directly
  become **signals**.
- **`ImagePane`** (`QObject`) — all per-image state: one `ImageViewer`, that image's
  **`EditManifest`**, the derived buffers, and its off-thread render pipeline. One pane in
  single mode; two (each fully independent) in side-by-side compare mode.
- **`MainWindow`** (`QMainWindow`) — orchestrator; owns the panes, every shared
  overlay/panel, and the compare `CompareTabBar`. Runs the display pipeline via the
  **focused** pane and wires each pane's viewer signals to handlers.
- **`HistogramOverlay`** (`QWidget`) — `G`: a camera-style RGB + exposure histogram panel
  measuring whatever the focused pane currently displays. Read-only; not part of the
  edit pipeline.
- **`main.cpp`** — CLI parsing only. An empty path is valid (idle state).

### Crop & rotate: one overlay, two modes

`X` (crop) and `R` (rotate) are two modes of a single selection overlay in `ImageViewer`
(`OverlayMode::{None,Crop,Rotate}`). Both put the same box over the full, uncropped image;
crop drags the box, rotate turns the image under it (corner drag + the `RotatePanel`'s
lossless 90° buttons and straighten slider). Switching `X`⇄`R` applies nothing — only
leaving the overlay does, via `MainWindow::commitOverlay()`, which writes both the
`RotateEdit` and the `CropEdit`. `RotateGeometry` keeps the selection inside the tilted
photograph's bounds, so a rotated result is never left with blank corners.

### Side-by-side compare

`Shift+O` opens a second image into a second `ImagePane` beside the first, with a minimal
tab bar (`CompareTabBar`) naming both. Exactly one pane is **focused** (lighter tab); all
editing shortcuts act only on it. Clicking a tab or an image changes focus; the tab's `✕`
closes that pane and returns to single mode. Zoom/pan stay in sync **relative to each image's
pixels** — `ImageViewer::relativeZoom()` / `relativeCenter()` / `applyRelativeView()`, driven
off the focused viewer's `viewChanged` signal.

### The edit manifest (canonical edit state)

All modifications live in one ordered **`EditManifest`** owned by the (focused) `ImagePane`.
It is the single source of truth for *what* is applied and *in what order*. Each modification is an
**`ImageEdit`** — a common interface (`apply(QImage)` on an in-memory buffer, plus JSON
(de)serialization) implemented by `OrientationEdit`, `RotateEdit`, `CropEdit`, `AdjustEdit`,
`ColorEdit`, and `BwEdit`. The manifest
is **persisted in `QSettings`, keyed by the image's absolute path**, so reopening the same
file re-applies the same edits (`EditManifest::saveFor` / `loadFor`).

### Display pipeline & pixmap state

Features that change the screen run in one ordered pipeline owned by each `ImagePane`:
**disk → orientation → rotate → crop → adjust → color → B&W → display**. The buffers are **`QImage`**
(so edits run off the GUI thread), each *derived from the manifest* applied to the previous stage:

| Field | Meaning |
|---|---|
| `m_diskImage` | Image exactly as loaded (EXIF-oriented at load). Never touched by edits. |
| `m_uprightImage` | `m_diskImage` with the manifest's `OrientationEdit` (quarter turns/flips) applied. The **crop/rotate overlay base**. |
| `m_orientedImage` | `m_uprightImage` with the manifest's `RotateEdit` (free angle) applied. The full-size **crop base**. |
| `m_baseImage` | `m_orientedImage` with the manifest's `CropEdit` applied. The **B&W source**. |

**New display-transform features must:** add an `ImageEdit` subclass, store its settings in
the `EditManifest`, and read input from `m_baseImage` / write output via
`ImageViewer::setDisplayPixmap()`. Mutating the manifest then re-deriving the buffers (and
calling `persistManifest()`) is the only way edits are applied. Long-running work goes off
the main thread (`QtConcurrent` + `QFutureWatcher`), like B&W.

## Key Qt gotchas

- `fitInView` must run in **both** the constructor and `showEvent` — the widget has no real
  size until shown.
- `qt_standard_project_setup()` handles MOC — never add `CMAKE_AUTOMOC` manually.
- `setDragMode(ScrollHandDrag)` enables pan; zoom is custom (`wheelEvent` + `+`/`-`/`0`).
- `Tab`/`Backtab` are swallowed by Qt's focus machinery before `keyPressEvent`; they're
  intercepted in `ImageViewer`'s viewport event filter. Cross-widget keys are routed to the
  viewer by `MainWindow`'s app-wide event filter (guard re-entrancy — see
  `doc/ARCHITECTURE.md`).

## Where things live

| Topic | File |
|---|---|
| Architecture, pipeline, event routing, feature internals | `doc/ARCHITECTURE.md` |
| Edit manifest, `ImageEdit` interface, persistence | `src/EditManifest.h`, `src/ImageEdit.h`, `doc/ARCHITECTURE.md` |
| Rotation geometry (tilted bounds, inscribed crop) | `src/RotateGeometry.h`, `src/RotatePanel.h` |
| Build system, deps, tests, packaging, CI release | `doc/BUILD.md` |
| Windows cross-compile & code signing | `doc/WINDOWS.md` |
| End-user install / run / packaging | `README.md` |
| Per-image state & compare panes | `src/ImagePane.h`, `src/CompareTabBar.h`, `doc/ARCHITECTURE.md` |
| HEIC & JPEG 2000 decoders (static Qt image plugins) | `src/imageformats/`, `doc/ARCHITECTURE.md` |
| Histogram computation & camera-style panel | `src/Histogram.h`, `src/HistogramOverlay.h`, `doc/ARCHITECTURE.md` |
| Planned & researched features | `ROADMAP.md` |
