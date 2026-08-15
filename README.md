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
levels & colour — a two-tab pop-up panel), `X` (crop), `R` (rotate), `W` (black & white),
`H` / `V` (flip), and `\` (compare against the original colour image).

### Crop and rotate

`X` and `R` put the same bounding box over the full, uncropped photograph, and differ in
what dragging does. In **crop mode** (`X`) the box is dragged and resized by its corners and
edge midpoints. In **rotate mode** (`R`) the box is fixed and **dragging a corner turns the
image** underneath it — the cursor becomes a curved arrow pointing the two ways that corner
can travel. A panel appears with **Rotate Left** / **Rotate Right** for lossless 90° turns
and a **Straighten** slider for the fine angle; together they reach any angle. Double-click
resets the crop, or the rotation, depending on which mode you are in. `X` and `R` switch
straight between the two modes without applying anything; `Esc` (or pressing the same key
again) leaves the overlay and applies the result.

Straightening a photograph tilts it inside a larger frame and would otherwise leave blank
triangles in the corners. Photo Salon never lets that happen: the box is held inside the
tilted photograph's bounds, so an un-cropped photo that is rotated a little automatically
picks up the largest crop that still fits and the result stays a full rectangle. A crop you
placed yourself is carried along with the rotation and only shrunk if the tilt would push it
past the edge.

The same limit applies in crop mode: on a tilted frame the box is held inside the dashed
outline of the photograph rather than being allowed out over a blank corner. It behaves
exactly as it does on an upright photo — dragging a corner moves only the two sides that
meet it, dragging the box never changes its size, and either way it simply stops when it
runs out of photograph.

### Histogram

Press `G` for a histogram of the photo on screen, drawn the way a professional camera draws
it: the red, green and blue channels plotted additively — overlaps read yellow, cyan or
white — over a filled luminance curve for overall exposure, with a tone ramp beneath and
clipping readouts (`▼` shadows, `▲` highlights) whenever more than 0.1 % of a channel is
pinned at either end.

It is a corner panel, not a full-screen sheet, so the photo stays visible, and it updates
live: adjust the light, convert to black & white, crop, or move to the next photo and the
histogram follows. Press `G` again (or `Escape`) to dismiss it.

### Compare two images side by side

Press `Shift+O` to open a second image alongside the current one. A minimal tab bar appears
at the top showing both file names; the **focused** image (the one all editing shortcuts act
on) is shown with a lighter tab. Click a tab — or click into an image — to change focus, and
click the `✕` on a tab to close it and return to single-image mode with the other photo.

While comparing, **zoom and pan are synchronized relative to each image's pixels**, not their
on-screen size: panning to a point 25 % down and 70 % across one image centres the other on
the same relative point, and a zoom level is relative to "fit", so the two stay matched even
when the photos differ greatly in resolution.

#### Auto-pairing "_pair" files

If a folder holds exactly two images whose file names (before the extension) both end with
`_pair` — e.g. `smith-001_pair.jpg` and `smith-002_pair.jpg` — opening either one, by any
means (launching photo-salon on it, `File > Open`, the `Tab` folder browser, or arrow-key
navigation), automatically opens both side by side, with the lexicographically first file in
the left pane. The → / ← arrow keys step past the pair as a single unit, back to single-image
mode, rather than stepping into it. To view just one of the pair alone, let it auto-open both
and close the other tab with its `✕`.
