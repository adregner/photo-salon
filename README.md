# photo-salon

A cross-platform minimalistic desktop photo viewing application to facilitate in-person photography salons.

## Prerequisites

### Linux (Debian/Ubuntu)

CMake and a C++ compiler are required. Qt 6.11+ will be fetched automatically by the build script if the system version is too old.

```bash
sudo apt install cmake g++ libheif-dev libheif-plugin-libde265 libopenjp2-7-dev
```

`libheif` and `OpenJPEG` add HEIC and JPEG 2000 (`.jpf`) support. They are optional —
without them the build succeeds and simply cannot open those two formats. Debian and
Ubuntu split libheif's HEVC decoder into `libheif-plugin-libde265`, which is loaded at
runtime; without it HEIC files are recognised but fail to decode.

### macOS

- [Homebrew](https://brew.sh/)
- Qt 6.11+, CMake, and the HEIC / JPEG 2000 codecs:

```bash
brew install qt cmake libheif openjpeg
```

### Windows
- [Qt6](https://www.qt.io/download) via Qt Online Installer — select the MSVC 2022 64-bit component
- [CMake](https://cmake.org/download/)
- Visual Studio 2022 (Community edition is sufficient)
- HEIC and JPEG 2000 support needs MSVC builds of `libheif` and `OpenJPEG`. The
  cross-build downloads a prebuilt bundle automatically; see
  [`doc/WINDOWS.md`](doc/WINDOWS.md) if you are building natively or rebuilding it.

## Building

[![CI](https://github.com/adregner/photo-salon/actions/workflows/ci.yml/badge.svg)](https://github.com/adregner/photo-salon/actions/workflows/ci.yml)
[![macOS](https://github.com/adregner/photo-salon/actions/workflows/release-macos.yml/badge.svg?event=release)](https://github.com/adregner/photo-salon/actions/workflows/release-macos.yml)
[![Windows](https://github.com/adregner/photo-salon/actions/workflows/release-windows.yml/badge.svg?event=release)](https://github.com/adregner/photo-salon/actions/workflows/release-windows.yml)

### Linux / macOS

```bash
./build
```

On Linux, if Qt 6.11+ is not found the script automatically downloads it via
[`fetch-linux-qt.sh`](fetch-linux-qt.sh) and builds against it.

On macOS, if Qt 6.11+ is not found the script prints the Homebrew command to
install it and exits.

### Windows (native)

Open a **Developer Command Prompt for VS 2022**, then:

```powershell
cmake -B _build -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2022_64
cmake --build _build --config Release
```

Replace `6.x.x` with the Qt version you installed (e.g. `6.8.0`).

### Windows (cross-compile from macOS)

Cross-compiles a self-contained static `.exe` using `clang-cl` targeting the MSVC ABI. See [`doc/WINDOWS.md`](doc/WINDOWS.md) for prerequisites and setup.

```bash
./build-windows.sh
# → _build_win/photo-salon.exe
```

### macOS app bundle

Produces a self-contained `photo-salon.app` (and optionally a `.dmg`) with all Qt frameworks bundled in:

```bash
./bundle-macos.sh           # → _bundle/photo-salon.app
./bundle-macos.sh --dmg     # → _bundle/photo-salon.app + _bundle/photo-salon.dmg
```

**Code signing** is opt-in via environment variables:

| Variable | Purpose |
|---|---|
| `CODESIGN_IDENTITY` | Signing identity — `"Developer ID Application: Name (TEAMID)"` or `"-"` for ad-hoc |
| `NOTARIZE_APPLE_ID` | Apple ID email for notarization (requires a signed DMG) |
| `NOTARIZE_PASSWORD` | App-specific password or `@keychain:<item>` |
| `NOTARIZE_TEAM_ID` | 10-character Apple Developer team ID |

```bash
# Signed + notarized DMG (for distribution to other Macs)
CODESIGN_IDENTITY="Developer ID Application: Your Name (ABC123DEF4)" \
NOTARIZE_APPLE_ID="you@example.com" \
NOTARIZE_PASSWORD="@keychain:notarytool" \
NOTARIZE_TEAM_ID="ABC123DEF4" \
./bundle-macos.sh
```

A **Developer ID** certificate requires an Apple Developer Program membership. Notarization is required for Gatekeeper to allow the app on other Macs (macOS 10.15+).

## Running

### Linux / macOS

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

Opens the specified image in a resizable window, scaled to fit. JPEG, PNG, TIFF, WebP
and the other formats Qt reads are supported, plus **HEIC/HEIF** (iPhone photos) and the
**JPEG 2000** family — `.jpf`, `.jpx`, `.jp2`, `.j2k`.

Edits are **non-destructive and remembered per image**: rotation, flips, crop, light/level
and colour adjustments, and the black-&-white look are tracked in an internal change manifest
that is saved locally and re-applied automatically the next time you open the same file. The
original on disk is never modified — use `S` to save an edited copy.

Press `?` in the app for the full list of shortcuts. The editing keys are `C` (adjust light,
levels & colour — a two-tab pop-up panel), `X` (crop), `W` (black & white), `R` (rotate),
`H` / `V` (flip), and `\` (compare against the original colour image).

### Compare two images side by side

Press `Shift+O` to open a second image alongside the current one. A minimal tab bar appears
at the top showing both file names; the **focused** image (the one all editing shortcuts act
on) is shown with a lighter tab. Click a tab — or click into an image — to change focus, and
click the `✕` on a tab to close it and return to single-image mode with the other photo.

While comparing, **zoom and pan are synchronized relative to each image's pixels**, not their
on-screen size: panning to a point 25 % down and 70 % across one image centres the other on
the same relative point, and a zoom level is relative to "fit", so the two stay matched even
when the photos differ greatly in resolution.
