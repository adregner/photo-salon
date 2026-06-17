# Architecture & Implementation

Deep reference for the photo-salon codebase. `CLAUDE.md` has the lean summary;
this file has the detail. All source lives in `src/`.

## Component map

| Class / file | Base | Responsibility |
|---|---|---|
| `main.cpp` | — | Entry point. Parses `argv[1]`, resolves it to an image path (or shows the open dialog), constructs `MainWindow`. No business logic. |
| `MainWindow` | `QMainWindow` | Orchestrator. Owns the viewer, every overlay/panel, and **all image-transform state**. Wires `ImageViewer` signals to handlers. Runs the display pipeline. |
| `ImageViewer` | `QGraphicsView` | Display + input. Owns the `QGraphicsScene` and the single `QGraphicsPixmapItem`. Handles zoom/pan/fit, folder navigation, the crop UI, and emits *intent* signals for everything it does not own. |
| `HelpOverlay` | `QWidget` | Mouse-transparent overlay painting the keyboard-shortcut list. Font auto-scales to fit. |
| `ExifOverlay` | `QWidget` | Template-driven metadata overlay. Reverse-geocodes GPS via Nominatim. Shows live edit state. |
| `ExifReader` | namespace | Reads file info + EXIF (via `easyexif`) into a `QMap<QString,QString>` of preformatted strings. |
| `ExitOverlay` | `QWidget` | "Press Q again to exit" overlay shown during the quit debounce window. |
| `BackgroundColorPicker` | `QWidget` | Grey-value (0–255) slider for the viewport background. Auto-dismisses. |
| `BwPanel` | `QWidget` (Tool) | Black-&-white control panel: seven look buttons, six hue-band sliders, a contrast slider, Compare, Reset. Auto-dismisses. |
| `BwConverter` | namespace | Off-thread B&W conversion: named "looks" (channel weights + tonal curve) plus hue-band and contrast adjustments. |
| `ImageFormats` | free fns | Supported-extension globs, file-dialog filter, CLI path resolution. |
| `OpenDialog` | free fn | `showOpenDialog()` — native open dialog. macOS uses `NSOpenPanel` (`.mm`); Linux/Windows use `QFileDialog`. |
| `Const.h` | — | `PANEL_DISMISS = 7500 ms`, `EXIT_DEBOUNCE = 1200 ms`. |

**Design rule:** `ImageViewer` never owns transform/business state. When the user
presses a key the viewer doesn't act on directly (rotate, flip, save, B&W, metadata,
fullscreen, open, background), it **emits a signal** and `MainWindow` decides what to do.
The only things the viewer mutates on its own are view state (zoom/pan/fit), folder
navigation, and the in-progress crop selection.

## Startup & idle state

`main()` resolves `argv[1]` with `resolveImagePath()` (a directory resolves to its
first image, sorted by name). With no argument it calls `showOpenDialog()`. **An empty
path is valid:** if the user cancels the startup dialog, `MainWindow` is constructed
with `""` and opens in *idle state* — a black `m_idleOverlay` plus the help overlay,
window staying open. Loading any image (`O`, `Tab`) hides the idle overlay. Toggling
fullscreen while idle re-shows help.

## Event & keyboard routing

Input routing is deliberately centralized because a `QGraphicsView` has several focus
sinks (viewport, scene, child widgets) that would otherwise swallow keys.

1. **`MainWindow` is installed as an application-wide event filter** (`qApp->installEventFilter`).
   `MainWindow::eventFilter` only looks at `KeyPress`, and bails if a modal widget is active.
   - **Escape** is dismissed in priority order: metadata overlay → background picker →
     help (only when an image is loaded) → B&W panel → crop mode → fullscreen. If none
     apply it returns `false` (Escape does nothing else).
   - **Any other key**, when the focused object is *not* the viewer or its viewport, is
     forwarded to the viewer via `QCoreApplication::sendEvent`. This is guarded by
     `m_forwardingKeyEvent` to prevent infinite recursion: `QGraphicsView::keyPressEvent`
     forwards unhandled keys to the scene, which re-triggers this same filter.
2. **`ImageViewer` installs an event filter on its own `viewport()`** solely to catch
   `Tab`/`Backtab`. Qt's focus machinery consumes those before they ever reach
   `keyPressEvent`, so they're intercepted at the viewport and routed to `keyPressEvent`
   manually. `focusNextPrevChild()` is overridden to return `false` so Tab never moves focus.
3. **`ImageViewer::keyPressEvent`** is the single `switch` that maps keys to actions and
   signals. Pressing any key except `?` while help is visible dismisses help first.

### Keyboard shortcuts (source of truth: `ImageViewer::keyPressEvent`)

| Key | Action | Mechanism |
|---|---|---|
| `←` / `→` | Previous / next image in folder | `navigate(±1)` (wraps; excludes `*.svg`) |
| `+` `=` / `-` | Zoom in / out | `applyZoom()` (scale clamp 0.05–32×) |
| `0` | Fit to window | `fitImage()` |
| `F` | Toggle fullscreen | → `fullscreenToggleRequested` |
| `B` | Background color picker | → `backgroundPickerRequested` |
| `I` | Toggle metadata overlay | → `exifRequested` |
| `C` | Toggle crop mode | `setCropMode()` |
| `W` | Toggle B&W panel / conversion | → `bwPanelRequested` |
| `\` | Compare against original (B&W) | → `bwCompareRequested` |
| `R` | Rotate 90° clockwise | → `rotateRequested` |
| `H` / `V` | Flip horizontal / vertical | → `flipHorizontalRequested` / `flipVerticalRequested` |
| `S` | Save current displayed image | → `saveRequested` |
| `O` | Open file (native dialog) | → `openFileRequested` |
| `Tab` | Open another file from the current folder | → `folderBrowseRequested` (list dialog) |
| `Q` | Quit — press twice within `EXIT_DEBOUNCE` | → `exitRequested` |
| `?` | Toggle this help overlay | `helpVisibilityChanged` |
| `Esc` | Dismiss overlay / panel / crop / fullscreen | handled in `MainWindow::eventFilter` |
| Scroll wheel | Zoom, anchored under cursor | `wheelEvent` |
| Drag (no crop) | Pan | `ScrollHandDrag` |

## The display pipeline

Every feature that changes what's on screen participates in one ordered pipeline owned
by `MainWindow`. Order:

1. **Disk image** — `ImageViewer::loadImage()` reads the file with
   `QImageReader::setAutoTransform(true)` (EXIF orientation is baked in at load). On the
   `imagePathChanged` signal `MainWindow` captures it into the three pixmap fields below
   and resets all transform state.
2. **Orientation** — `R`/`H`/`V` → `applyOrientationTransform()`.
3. **Crop** — `ImageViewer` owns the crop UI; on exit `MainWindow` folds the result into the base.
4. **B&W** — `BwConverter::convert()` runs off-thread; non-destructive.
5. **Display** — `ImageViewer::setDisplayPixmap()` swaps the item's pixmap and refreshes
   the scene rect if dimensions changed (e.g. after a 90° rotation).

### Pixmap state model (three fields in `MainWindow`)

| Field | Definition | Updated when | Never |
|---|---|---|---|
| `m_diskPixmap` | The image exactly as loaded from disk. | Load / navigation only. | Modified by crop, orientation, or B&W. |
| `m_orientedDiskPixmap` | `m_diskPixmap` with all rotation/flip applied. The **full-size crop base** — always passed to `setBasePixmapForCrop()`. | Load/navigation (= disk) and every orientation change. | — |
| `m_basePixmap` | `m_orientedDiskPixmap` with the current crop applied. The **B&W source**, restored by `deactivateBw()`. | Load/navigation (= disk), every crop apply, every orientation change. | Cleared. |

**Contract for any new display-transform feature:** read input from `m_basePixmap`,
write output through `setDisplayPixmap()`. If the feature *permanently* changes image
content (like crop/orientation), update `m_basePixmap`. If it's a non-destructive view
transform (like B&W), leave `m_basePixmap` alone.

## Crop tool (in `ImageViewer`)

- **Enter** (`setCropMode(true)`): swaps the pixmap item to the crop base
  (`m_cropBasePixmap`, set via `setBasePixmapForCrop()`; falls back to reloading the file
  from disk if unset), so the user always sees the **full oriented original** and can
  *expand* a previous selection as well as shrink it. Initializes/clamps `m_cropRect`,
  fits, disables `ScrollHandDrag`.
- **Interaction**: `hitTestHandle()` maps a viewport point to a corner (24 px grab),
  edge (15 px grab ≈ ±7.5× the border line width, anywhere along the edge), interior
  (move), or none. The grab distances and handle size derive from `kCropLineWidth`
  (top of `ImageViewer.cpp`). `mouseMoveEvent` resizes/moves `m_cropRect` (normalized,
  clamped to the image). `drawForeground()` paints the dark mask over excluded regions
  (scene coords) plus a white border and eight handles (viewport coords).
- **Exit/apply** (`setCropMode(false)`): copies the selected rect out of the current
  pixmap and sets it on the item, then emits `cropModeChanged(false)`. `MainWindow` then
  sets `m_basePixmap = viewer->pixmap()`, re-arms the crop base to `m_orientedDiskPixmap`,
  and re-runs B&W if active.
- `setCropRect()` clamps against `m_cropBasePixmap` when set, which lets `MainWindow`
  store a *transformed* crop rect while crop is inactive (see orientation below).

## Orientation (`MainWindow::applyOrientationTransform`)

- If crop is active, it's applied first so the rotation acts on the cropped image.
- `QPixmap::trueMatrix(t, w, h)` reproduces the translation Qt adds when transforming,
  which is used to map the saved crop rect into the new coordinate space — so re-entering
  crop still pre-selects the same region after a rotate/flip.
- Updates `m_orientedDiskPixmap`, re-arms the crop base, transforms `m_basePixmap`, then
  either re-runs B&W (if active) or pushes the result via `setDisplayPixmap()`.
- `R` accumulates `m_rotationAngle` mod 360; `H`/`V` toggle `m_flippedH` / `m_flippedV`.
  These flags feed the metadata edit-state line.

## Black & white conversion

The model is a set of named **looks** (conversion techniques), each tunable with six
hue-band sliders and a contrast slider. A look is defined by `LookDef` in
`BwConverter.cpp`: channel weights `wR,wG,wB` (sum 1), whether they mix in linear light
or on gamma-encoded values (`gammaMix`), and a tonal curve (black point, highlight
shoulder, output floor, default contrast).

| Look | Character |
|---|---|
| **Neutral** | BT.709 luminance in linear light. Accurate, even, flat. The baseline. |
| **Photoshop** | Photoshop "Black & White" default mix (≈40/40/20), gamma space, mild S-curve. |
| **iPhone** | Punchy phone look: deep black point, strong S-curve, highlight shoulder. |
| **Monochrom** | Panchromatic-sensor weights (red-leaning, linear) + gentle shoulder — Leica Monochrom-style wide, true luminance. |
| **Classic** | Rec.601 luma (0.299/0.587/0.114), gamma space, no curve. |
| **Film** | Tri-X/Ilford curve: lifted toe (no pure black), rolled highlights, mid contrast. |
| **High Contrast** | Strong S-curve, crushed blacks, bright whites. |

**Algorithm (`BwConverter::convert`)**:
1. Decode the source to `Format_RGBX32FPx4` linear-light float (`toLinearFloat()`), and
   derive the gamma-encoded `R'G'B'` per pixel.
2. **Base grey** (perceptual/sRGB space): a luminance look weights the linear RGB then
   re-encodes to sRGB; a luma look (`gammaMix`) weights the gamma-encoded RGB directly.
3. **Hue-band adjustment**: `hueAdjustment()` interpolates the six band sliders
   (Reds…Magentas, one per 60° of hue); `grey += adjustment·saturation` (neutral tones,
   saturation 0, are untouched).
4. **Tonal curve** (`applyTone`): black-point crush → logistic S-curve (the contrast
   slider, ±) → highlight shoulder → output floor.
5. Store as `Format_Grayscale16`.

The key correctness point: the grey is **gamma-encoded (sRGB) before storage**, so a
mid-luminance colour reads as a mid grey rather than a too-dark raw-linear value. The hot
loop is kept free of `pow`/`exp` by two per-image LUTs (linear→sRGB, and the full tonal
curve, which is a pure function of the grey value).

**Threading & state (`MainWindow`):**
- `convert()` runs via `QtConcurrent::run` + `QFutureWatcher`; the UI never blocks.
- `m_bwDebounce` (50 ms) coalesces slider drags; if a conversion is already running it
  reschedules.
- `m_originalImage = m_basePixmap.toImage()` is the cached source; `m_lastBwPixmap` is the
  latest result. The watcher only pushes the result to the viewer when B&W is active, not
  comparing, and not mid-crop.
- **Compare** (`\` or the panel button) toggles between `m_basePixmap` (color) and
  `m_lastBwPixmap`.
- `deactivateBw()` restores `m_basePixmap`, clears the B&W caches (but **not**
  `m_basePixmap`), and hides the panel. It's called on every image load/navigation.

**`BwPanel`** is a frameless translucent `Qt::Tool` widget docked bottom-left. Seven look
buttons (exclusive `QButtonGroup`) sit above six hue-band sliders and a contrast slider,
all −100…100. Picking a look calls `BwConverter::lookPreset()` to load its defaults (bands
zeroed, the look's own contrast). It emits `paramsChanged` / `compareToggled` /
`resetToColorRequested`. Like the color picker, it auto-hides after `PANEL_DISMISS` unless
hovered or focused.

## Metadata overlay

**`ExifReader::read()`** returns a `QMap<QString,QString>` of preformatted, human-readable
fields (camera, lens, exposure triplet, focal length + 35 mm equiv, GPS as DMS + raw
decimals, date, etc.). Parsing uses **easyexif** (`exif.cpp`, pulled via CMake
`FetchContent`). Always-present file fields: `FileName`, `FileSize`, `Dimensions`. See the
header for the full key list.

**`ExifOverlay`** renders from a **template** (`QStringList`, see `defaultTemplate()`):
each line is either a blank separator or text with `{FieldName}` placeholders.
`renderLine()` substitutes from the data map and **omits any line whose placeholders all
resolve empty**, so the overlay stays clean when fields are missing. Replace the layout
with `setTemplate()`.

**Reverse geocoding:** when GPS is present, `resolveLocation()` queries
`nominatim.openstreetmap.org/reverse` over `QNetworkAccessManager` (requires the
`Qt6::Network` link), parses city/state/country into a `{Location}` line, and caches by
lat/long so repeat views don't refetch. Stale replies are dropped by comparing against
`m_pendingGeoKey`.

**Live edit state:** `MainWindow::imageStateData()` injects a `{State_Edits}` summary
(`90° rotation · H flip · crop · B&W`), authoritative `{Dimensions}` (the EXIF-oriented
size as loaded, so it is always present and correct after edits), and `{CurrentDimensions}`
(the edited size, shown only when it differs) into the data before the overlay is shown,
so the overlay reflects in-memory edits, not just the file's EXIF.

## Image format support

`supportedExtensions()` enumerates **every format Qt's image plugins support**
(`QImageReader::supportedImageFormats()`) — JPEG is primary but PNG and others load too.
This drives folder navigation, the file-dialog filter, and the macOS `NSOpenPanel`
content types. Folder *navigation* (`←`/`→`) additionally excludes `*.svg`. The Save
dialog defaults to `<name>-saved.jpg` and offers JPEG/PNG.

## Adding a feature — checklist

1. If it reacts to a key, add the case to `ImageViewer::keyPressEvent` and **emit a
   signal** rather than acting directly (unless it's pure view state).
2. Wire the signal to a handler in the `MainWindow` constructor.
3. If it transforms the image, read `m_basePixmap`, write `setDisplayPixmap()`, and obey
   the pixmap-state contract above.
4. Long-running work goes off-thread (`QtConcurrent` + `QFutureWatcher`), mirroring B&W.
5. New overlay/panel: resize it in `MainWindow::resizeEvent`, add it to the Escape
   priority chain, and `raise()` it on show.
6. Add a `tests/test_*.cpp` (run headless with `QT_QPA_PLATFORM=offscreen`).
7. Update the help text in `HelpOverlay`, and `README.md` / `CLAUDE.md` / `ROADMAP.md` as needed.
