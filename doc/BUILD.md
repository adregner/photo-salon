# Build, Test & Release

Developer-facing build reference. End-user install/run instructions are in `README.md`;
Windows cross-compilation lives in `doc/WINDOWS.md`.

## CMake layout

`CMakeLists.txt` defines two targets:

- **`photo-salon-lib`** — a `STATIC` library containing every source file *except*
  `main.cpp`. All tests link against this, which is why nearly all logic lives outside
  `main.cpp`.
- **`photo-salon`** — the executable, just `src/main.cpp` linked to the lib.

Requirements: **C++17**, **Qt 6.11+** with components `Widgets`, `Concurrent`, `Network`.
`qt_standard_project_setup()` handles MOC/UIC/RCC automatically — **do not** set
`CMAKE_AUTOMOC` by hand.

Platform-specific sources:
- **macOS** compiles `src/OpenDialog.mm` and links `AppKit` + `UniformTypeIdentifiers`;
  `OBJCXX` is enabled only on Apple.
- **Linux/Windows** compile `src/OpenDialog.cpp` instead.

### Fetched dependencies (CMake `FetchContent`)

| Dependency | Where | Pinned | Purpose |
|---|---|---|---|
| **easyexif** | root `CMakeLists.txt` | commit `cd994a3…` | Single-file JPEG EXIF parser (`exif.cpp`). |
| **exif-py samples** | `tests/CMakeLists.txt` | commit `2adb9d1…` | Real-camera JPEGs for `test_exif_reader`; path passed as `EXIF_SAMPLES_DIR`. |

## Building

```bash
./build            # configure + build into _build/ (Release)
./build run img.jpg # build the photo-salon target and run it with args
```

The `build` script locates Qt in this order: locally fetched Qt at
`/opt/qt-linux/<ver>/gcc_64` → system/Homebrew `qmake` (if ≥ 6.11.1) → on macOS, the
Homebrew `qt` keg. If none is found:
- **Linux:** it runs `fetch-linux-qt.sh`, which `aqt install-qt`s Qt 6.11.1
  (`qtbase` + `icu`) into `/opt/qt-linux`.
- **macOS:** it prints `brew install qt` and exits.

### compile_commands.json

For clangd / tooling, regenerate when stale:

```bash
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON .
```

## Testing

Tests use **Qt Test** and run headless. Each is registered with CTest and forced onto the
offscreen platform (`ENVIRONMENT QT_QPA_PLATFORM=offscreen`).

```bash
cd _build && ctest --output-on-failure
# or a single test binary directly:
QT_QPA_PLATFORM=offscreen ./_build/tests/test_crop_tool
```

Current suites (in `tests/`): `test_zoom`, `test_help_overlay`, `test_image_formats`,
`test_folder_navigation`, `test_open_folder`, `test_fullscreen`, `test_background_color`,
`test_crop_tool`, `test_bw_converter`, `test_exif_reader`. New tests link
`photo-salon-lib` + `Qt6::Test` and should set the offscreen platform property.

## Packaging

| Platform | Command | Output |
|---|---|---|
| macOS `.app` / `.dmg` | `./bundle-macos.sh [--dmg]` | `_bundle/photo-salon.app` (+ `.dmg`) — Qt frameworks bundled in. |
| Windows (cross from macOS/Linux) | `./build-windows.sh` | `_build_win/photo-salon.exe` (static, MSVC ABI). See `doc/WINDOWS.md`. |
| Windows (native) | `cmake -B _build -DCMAKE_PREFIX_PATH=...msvc2022_64 && cmake --build _build --config Release` | `_build/Release/photo-salon.exe`. |

**Code signing is opt-in** and driven by environment variables:
- macOS: `CODESIGN_IDENTITY`, `NOTARIZE_APPLE_ID`, `NOTARIZE_PASSWORD`, `NOTARIZE_TEAM_ID`
  (see `README.md`).
- Windows: Azure Trusted Signing (`AZURE_*`) or a local PFX (`CODESIGN_CERT` /
  `CODESIGN_PASSWORD`) — see `doc/WINDOWS.md`.

## Release automation

- **`release.sh [patch|minor|major]`** — bumps from the latest GitHub release tag, creates
  a new `vX.Y.Z` release with auto-generated notes (via `gh`), and waits for the workflow
  to start.
- **`.github/workflows/release.yml`** (triggered on release `created`):
  1. `build` (ubuntu) — cross-compiles the **unsigned** Windows `.exe` and uploads it as an
     artifact.
  2. `sign-and-release` (windows) — signs with Azure Trusted Signing and uploads the
     signed `.exe` to the release.
  3. `build-macos` (macos) — builds/bundles/signs/notarizes the `.dmg` and uploads it.
     Signing/notarization are skipped when the `MACOS_CERTIFICATE` secret is absent.

  Required secrets/variables are documented in the header comment of `release.yml`.

## Devcontainer

`.devcontainer/` provides an Ubuntu 24.04 image with `qt6-base-dev`, clang, cmake, and
ninja. `postCreateCommand` configures + builds with Ninja into `_build`. Note this uses
the distro Qt, which may be older than the 6.11 the `build` script wants — prefer `./build`
for a version-correct toolchain.
