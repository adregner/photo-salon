# photo-salon — Agent Instructions

C++ / Qt6 desktop image viewer for in-person photography salons. Cross-platform:
Linux, macOS (primary dev/test), and Windows 10/11. Single-window viewer with
non-destructive edits (orientation, crop, hue-selective B&W) and a metadata overlay.

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
- Tests are Qt Test binaries run with `QT_QPA_PLATFORM=offscreen` (CTest sets this).
- Regenerate `compile_commands.json` when stale:
  `cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON .`
- Packaging, dependencies, and the release workflow: **`doc/BUILD.md`**.
- Windows cross-compile from macOS (`./build-windows.sh`): **`doc/WINDOWS.md`**.

## Conventions

- C++17, Qt6 only — no Qt5 shims, no qmake/`.pro` files, CMake only.
- Source in `src/`; tests in `tests/` (link `photo-salon-lib`, not `main.cpp`).
- Project docs in `doc/*.md`. Keep `CLAUDE.md` lean — put depth in `doc/`.
- Loads **any format Qt's image plugins support** (JPEG primary; PNG etc. work too).
  Folder navigation excludes `*.svg`.
- When adding a feature, check whether `README.md`, `CLAUDE.md`, `ROADMAP.md`, and the
  `HelpOverlay` shortcut list need updating.
- When adding a Qt module or library that pulls in new Windows system DLLs, update the
  SDK import stubs in `fetch-windows-deps.sh`. See **`doc/BUILD.md` § Windows SDK stubs**.

## Architecture (summary)

Full detail — pipeline, event routing, crop/orientation/B&W internals, metadata overlay —
is in **`doc/ARCHITECTURE.md`**. The essentials:

- **`ImageViewer`** (`QGraphicsView`) — display + input; owns the scene and the single
  pixmap item. It never holds transform/business state: keys it doesn't act on directly
  become **signals**.
- **`MainWindow`** (`QMainWindow`) — orchestrator; owns every overlay/panel and **all
  image-transform state**, and runs the display pipeline. Wires viewer signals to handlers.
- **`main.cpp`** — CLI parsing only. An empty path is valid (idle state).

### Display pipeline & pixmap state

Features that change the screen run in one ordered pipeline owned by `MainWindow`:
**disk → orientation → crop → B&W → display**. Three pixmap fields track state:

| Field | Meaning |
|---|---|
| `m_diskPixmap` | Image exactly as loaded (EXIF-oriented at load). Never touched by edits. |
| `m_orientedDiskPixmap` | Disk pixmap + rotation/flip. The full-size **crop base**. |
| `m_basePixmap` | Oriented pixmap + crop applied. The **B&W source**; never cleared. |

**New display-transform features must:** read input from `m_basePixmap`, write output via
`ImageViewer::setDisplayPixmap()`. If the change is permanent (crop/orientation), update
`m_basePixmap`; if non-destructive (B&W), leave it. Long-running work goes off the main
thread (`QtConcurrent` + `QFutureWatcher`), like B&W.

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
| Build system, deps, tests, packaging, CI release | `doc/BUILD.md` |
| Windows cross-compile & code signing | `doc/WINDOWS.md` |
| End-user install / run / packaging | `README.md` |
| Planned & researched features | `ROADMAP.md` |
