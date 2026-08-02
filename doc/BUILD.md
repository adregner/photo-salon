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

### Optional system dependencies (image codecs)

Two decoders Qt doesn't ship are built from `src/imageformats/` as static Qt image
plugins (see `doc/ARCHITECTURE.md` § Image format support):

| Library | Adds | Install |
|---|---|---|
| **libheif** | HEIF/HEIC | `brew install libheif` · `apt install libheif-dev libheif-plugin-libde265` |
| **OpenJPEG** | JPEG 2000 — `.jpf` `.jpx` `.jp2` `.j2k` `.j2c` | `brew install openjpeg` · `apt install libopenjp2-7-dev` |

On Debian/Ubuntu libheif's HEVC decoder is a *separate* package it `dlopen`s at runtime
(`libheif-plugin-libde265`). Without it a HEIC file is recognised — `QImageReader::size()`
even works — but decoding fails; the handler logs libheif's reason. Homebrew's libheif
links the decoder in, so macOS needs nothing extra.

Both are optional. `photo_salon_find_codec()` in the root `CMakeLists.txt` looks for each
with pkg-config, then with a plain header/library search; a miss prints a warning, skips
that plugin, and leaves the corresponding `PHOTO_SALON_HAVE_HEIF` /
`PHOTO_SALON_HAVE_JPEG2000` compile definition undefined — the build still succeeds, just
without that format. Point the search at a specific build by passing e.g.
`-DHEIF_INCLUDE_DIR=… -DHEIF_LIBRARY=…`.

pkg-config is skipped while cross-compiling, so the Windows build never links the host's
libraries. It gets MSVC builds of both codecs from the codecs bundle that
`fetch-windows-deps.sh` unpacks into `windows/codecs/x64/` — see `doc/WINDOWS.md`
§ Image codecs for how that bundle is built and republished.

On macOS `macdeployqt` copies both dylibs (and their transitive dependencies, such as
`libde265`) into `photo-salon.app/Contents/Frameworks` during `./bundle-macos.sh`, so the
released bundle is self-contained.

#### Codecs are mandatory for release binaries

Optional is fine while developing; a *published* binary must never be missing a format it
advertises. `PHOTO_SALON_REQUIRE_CODECS` turns every "not found" warning above into a
configure-time `FATAL_ERROR`:

```bash
PHOTO_SALON_REQUIRE_CODECS=1 ./build            # or ./build-windows.sh
cmake -B _build -DPHOTO_SALON_REQUIRE_CODECS=ON # same thing, directly
```

Both `build` and `build-windows.sh` forward the environment variable to CMake, and it is
set in three places, so no release path can skip it:

| Where | What it covers |
|---|---|
| `bundle-macos.sh` | Sets it unconditionally — everything that script produces is a distributable |
| `release-macos.yml` | Job-level `env:`, so the plain `./build` + `ctest` step is gated too |
| `release-windows.yml` | Job-level `env:` on both the Linux test job and the cross-compile job |

`bundle-macos.sh` then adds an artifact-level check after `macdeployqt`, because a
successful configure only proves the codecs existed on the *build* machine: it verifies
the app binary's `libheif` / `libopenjp2` references were rewritten to `@executable_path`
(or `@rpath`) and that matching dylibs really landed in `Contents/Frameworks`. A bundle
that would open HEICs on the build Mac and nowhere else fails there, before signing.

The macOS and Linux release paths also run `ctest`, where `test_extra_formats` proves the
plugins actually register in the built binary — the configure gate guarantees they were
compiled in, that suite guarantees `QImageReader` can see them.

The Windows release job passes the same gate using the pre-built codecs bundle
(`doc/WINDOWS.md` § Image codecs). If that bundle ever goes missing or regresses,
the job fails rather than publishing an `.exe` without HEIC and JPEG 2000 — which is the
intended behaviour.

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
`test_crop_tool`, `test_bw_converter`, `test_exif_reader`, `test_external_launch`,
`test_extra_formats`. New tests link `photo-salon-lib` + `Qt6::Test` and should set the
offscreen platform property.

`test_extra_formats` covers the HEIF / JPEG 2000 plugins against the small committed
fixtures in `tests/resources/` (see the README there for how each was generated); its
cases skip themselves when the matching codec wasn't found at configure time.

## Packaging

| Platform | Command | Output |
|---|---|---|
| macOS `.app` / `.dmg` | `./bundle-macos.sh [--dmg]` | `_bundle/photo-salon.app` (+ `.dmg`) — Qt frameworks bundled in. |
| Windows (cross from macOS/Linux) | `./build-windows.sh` | `_build_win/photo-salon.exe` (static, MSVC ABI). See `doc/WINDOWS.md`. |
| Windows (native) | `cmake -B _build -DCMAKE_PREFIX_PATH=...msvc2022_64 && cmake --build _build --config Release` | `_build/Release/photo-salon.exe`. |

### Windows cross-compile dependencies

Nothing under `windows/msvc/`, `windows/sdk/`, `windows/qt-6.11/` or `windows/codecs/`
is committed. `fetch-windows-deps.sh` downloads all four bundles named in
`windows-deps.lock` and verifies each against the SHA-256 recorded there.

Adding a Qt module or a library that depends on a new Windows system DLL needs **no
action**: the SDK bundle vendors every x64 import library the SDK ships, so there is no
longer a "copy one more `.lib` into the repo when the link fails" step. Regenerating or
republishing the bundles is `windows/toolchain/Make-WindowsToolchain.ps1` on a Windows
machine — see `doc/WINDOWS.md`.

**Code signing is opt-in** and driven by environment variables:
- macOS: `CODESIGN_IDENTITY`, `NOTARIZE_APPLE_ID`, `NOTARIZE_PASSWORD`, `NOTARIZE_TEAM_ID`
  (see `README.md`).
- Windows: Azure Trusted Signing (`AZURE_*`) or a local PFX (`CODESIGN_CERT` /
  `CODESIGN_PASSWORD`) — see `doc/WINDOWS.md`.

## Release automation

- **`release.sh [patch|minor|major]`** — bumps from the latest GitHub release tag, creates
  a new `vX.Y.Z` release with auto-generated notes (via `gh`), and waits for the workflow
  to start.
- **`.github/workflows/ci.yml`** (push / pull_request on `main`): runs tests on macOS and
  Windows (native MSVC build). Badge shows current test health.
- **`.github/workflows/release-macos.yml`** (release `created`): installs Qt via Homebrew,
  runs tests, then builds/bundles/signs/notarizes the `.dmg` and uploads it. Signing and
  notarization are skipped when the `MACOS_CERTIFICATE` secret is absent.
- **`.github/workflows/release-windows.yml`** (release `created`): runs tests on Windows
  (native MSVC), cross-compiles the unsigned `.exe` on Linux, signs with Azure Trusted
  Signing on Windows, and uploads the signed `.exe`.

  Required secrets/variables are documented in the header comments of each workflow file.

## Devcontainer

`.devcontainer/` provides an Ubuntu 24.04 image with `qt6-base-dev`, clang, cmake, and
ninja. `postCreateCommand` configures + builds with Ninja into `_build`. Note this uses
the distro Qt, which may be older than the 6.11 the `build` script wants — prefer `./build`
for a version-correct toolchain.
