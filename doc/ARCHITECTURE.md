# Architecture & Implementation

Deep reference for the photo-salon codebase. `CLAUDE.md` has the lean summary;
this file has the detail. All source lives in `src/`.

## Component map

| Class / file | Base | Responsibility |
|---|---|---|
| `main.cpp` | — | Entry point. Parses `argv[1]`, resolves it to an image path (or shows the open dialog), constructs `MainWindow`. No business logic. |
| `MainWindow` | `QMainWindow` | Orchestrator. Owns the image pane(s), every shared overlay/panel, and the compare tab bar. Wires each pane's `ImageViewer` signals to handlers, always acting on the **focused** pane. |
| `ImagePane` | `QObject` | All per-image state: one `ImageViewer`, that image's **`EditManifest`**, the derived `QImage` buffers, and the off-thread display-render pipeline. One pane in single mode; two independent panes in side-by-side compare mode. |
| `CompareTabBar` | `QWidget` | The minimal tab strip shown atop the window in compare mode: one tab per image (file name + `✕`), focused tab lighter. Emits `tabSelected` / `tabClosed`. Hidden when only one image is open. |
| `EditManifest` | value type | The single, ordered, canonical record of every edit applied to one image. Typed accessors, `render()`, JSON (de)serialization, and per-path persistence in `QSettings`. |
| `ImageEdit` | interface | Common interface every editing module implements: `apply(QImage)` on an in-memory buffer, JSON (de)serialization, `clone()`, and a `summary()` tag. Concrete: `OrientationEdit`, `RotateEdit`, `CropEdit`, `AdjustEdit`, `ColorEdit`, `BwEdit`. |
| `ImageViewer` | `QGraphicsView` | Display + input. Owns the `QGraphicsScene` and the single `QGraphicsPixmapItem`. Handles zoom/pan/fit, folder navigation, the crop/rotate overlay, exposes relative zoom/pan for sync, and emits *intent* signals for everything it does not own. |
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
presses a key the viewer doesn't act on directly (flip, save, B&W, metadata,
fullscreen, open, background), it **emits a signal** and `MainWindow` decides what to do.
The only things the viewer mutates on its own are view state (zoom/pan/fit), folder
navigation, and the in-progress crop selection and rotation angle — which are still only
*applied* once `MainWindow` folds them into the manifest.

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
     help (only when an image is loaded) → B&W panel → adjust panel → the crop/rotate
     overlay → fullscreen. If none
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
| `C` | Toggle light/levels & color panel | → `adjustPanelRequested` |
| `X` | Toggle crop mode | `setCropMode()` |
| `W` | Toggle B&W panel / conversion | → `bwPanelRequested` |
| `\` | Compare against original color image | → `bwCompareRequested` (`toggleCompare`) |
| `R` | Toggle rotate mode | `setRotateMode()` |
| `H` / `V` | Flip horizontal / vertical | → `flipHorizontalRequested` / `flipVerticalRequested` |
| `S` | Save current displayed image | → `saveRequested` |
| `O` | Open file (native dialog) | → `openFileRequested` |
| `Shift+O` | Open a second image to compare side by side | → `compareOpenRequested` (`openSecondImage`) |
| `Tab` | Open another file from the current folder | → `folderBrowseRequested` (list dialog) |
| `Q` | Quit — press twice within `EXIT_DEBOUNCE` | → `exitRequested` |
| `?` | Toggle this help overlay | `helpVisibilityChanged` |
| `Esc` | Dismiss overlay / panel / crop-rotate / fullscreen | handled in `MainWindow::eventFilter` |
| Scroll wheel | Zoom, anchored under cursor | `wheelEvent` |
| Drag (no overlay) | Pan | `ScrollHandDrag` |
| Drag a corner (rotate mode) | Turn the image | `mouseMoveEvent` → `setRotateAngle()` |

## The edit manifest (canonical edit state)

Every modification is recorded in one **`EditManifest`** owned by the image's `ImagePane` —
the single source of truth for *what* edits are applied and *in what order*. The display is
produced by applying the manifest to the disk image; the scattered transform flags of earlier
designs (`m_rotationAngle`, `m_flippedH/V`, `m_cropApplied`, `m_bwActive`, …) no longer exist.
(In compare mode each pane has its own manifest, so edits never cross between images.)

**`ImageEdit`** is the common interface every editing module implements:

| Member | Purpose |
|---|---|
| `apply(const QImage&) → QImage` | Apply this edit's settings to an in-memory buffer. Pure (no shared state) so a manifest can re-render from disk deterministically, off the GUI thread. The buffer is a `QImage`, not a `QPixmap`, precisely so it is thread-safe. |
| `toJson()` / `fromJson()` | Round-trip the edit's settings through the persisted store. |
| `clone()` | Deep copy (the manifest is value-semantic). |
| `summary()` | Short tag for the metadata edit-state line (`90° rotation`, `crop`, `B&W`); empty to omit. |

Six concrete edits implement it:

- **`OrientationEdit`** — lossless rotation/flip. Stores the *net* linear transform (the
  dihedral group) so repeated `R`/`H`/`V` presses compose exactly the way they did on screen,
  and a reopened image reproduces the same orientation. It also keeps descriptive
  rotation/flip counters that drive `summary()`.
- **`RotateEdit`** — free-angle rotation for straightening a tilted horizon, clamped to
  ±45° (`RotateGeometry::kMaxAngle`); whole quarter turns stay in `OrientationEdit`, and the
  two together reach any angle. `apply()` grows the buffer to the bounding box of the tilted
  image, leaving blank corners the crop is guaranteed to exclude.
- **`CropEdit`** — a rectangle in **normalized** coordinates (fractions of the buffer, 0..1),
  so it is resolution-independent. `apply()` copies that region out of the oriented image.
- **`AdjustEdit`** — light/tone: brightness, contrast, exposure, saturation, and black/white
  level endpoints. Wraps `AdjustParams` and defers to `ImageAdjust::applyTone()`.
- **`ColorEdit`** — colour balance: temperature, tint, per-channel red/green/blue gains, and
  eight per-hue saturation bands (Red…Magenta, `ImageAdjust::hueBand()`). Wraps `ColorParams`
  and defers to `ImageAdjust::applyColor()` (a cosine-falloff hue LUT scales each pixel's HSV
  saturation). A *separate* manifest step from `AdjustEdit`, though the same pop-up
  (`AdjustPanel`, the `C` key) drives both via its two tabs.
- **`BwEdit`** — wraps `BwParams` and defers to `BwConverter::convert()`, making B&W conform
  to the interface.

`EditManifest` keeps its edits in canonical pipeline order (orientation → rotate → crop → adjust →
color → B&W) via `editOrderIndex()`; the `ensure*()` accessors insert at the right slot, and
an edit is present **only while it is applied** (removed when it returns to identity / full /
neutral / reset-to-colour). `render()` folds `apply()` over the edits in order — the path used
to reconstruct an image from disk. `renderAfterCrop()` applies only the post-crop edits
(adjust → color → B&W) to an already-cropped base; this is the live display pipeline's hot
path, since orientation, rotation and crop are cached as buffers. `summary()` joins the per-edit
summaries.

**Persistence:** `saveFor(path)` / `loadFor(path)` serialize the manifest to compact JSON
stored in `QSettings`, keyed by a hash of the image's **absolute path** (under the
`manifests/` group). Saving an empty manifest clears the entry. The pane calls
`persistManifest()` after every edit, and `ImagePane::reloadFromDisk()` loads and re-applies
the saved manifest whenever an image is opened — so edits are remembered per file across sessions.

## The display pipeline

Every feature that changes what's on screen participates in one ordered pipeline owned
by each `ImagePane`. Order:

1. **Disk image** — `ImageViewer::loadImage()` reads the file with
   `QImageReader::setAutoTransform(true)` (EXIF orientation is baked in at load). On the
   `imagePathChanged` signal `ImagePane::reloadFromDisk()` captures it into `m_diskImage`,
   loads that path's saved manifest, and re-derives the buffers below.
2. **Orientation** — the rotate panel's 90° buttons and `H`/`V` → `applyOrientationStep()`
   composes a step onto the manifest's `OrientationEdit`.
3. **Rotate** — the free angle dragged out in rotate mode, folded into the manifest's
   `RotateEdit` when the overlay closes.
4. **Crop** — `ImageViewer` owns the selection overlay; on close `MainWindow` folds the
   selection into the manifest's `CropEdit` (normalized against the *rotated* image).
5. **B&W** — the manifest's `BwEdit` runs off-thread via `BwConverter::convert()`; non-destructive.
6. **Display** — `ImageViewer::setDisplayPixmap()` swaps the item's pixmap and refreshes
   the scene rect if dimensions changed (e.g. after a 90° rotation).

### Buffer state model (four `QImage` fields in `ImagePane`)

Each buffer is *derived* from the manifest applied to the previous stage — `ImagePane` does
not store transform state independently of the manifest. `rebuildOriented()` and
`rebuildBase()` recompute them via the edit interface.

| Field | Definition | Recomputed when |
|---|---|---|
| `m_diskImage` | The image exactly as loaded from disk. | Load / navigation only (never edited). |
| `m_uprightImage` | `OrientationEdit::apply(m_diskImage)`. The **overlay base** — passed to `setBasePixmapForCrop()`; the viewer applies the free angle to it itself, so dragging stays interactive. | `rebuildOriented()`: load and every orientation change. |
| `m_orientedImage` | `RotateEdit::apply(m_uprightImage)`. The **full-size crop base**. | `rebuildOriented()`: load, orientation change, rotation change. |
| `m_baseImage` | `CropEdit::apply(m_orientedImage)`. The **B&W source**. | `rebuildBase()`: load, every crop apply, every orientation/rotation change. |

**Contract for any new display-transform feature:** add an `ImageEdit` subclass, store its
settings in the `EditManifest`, read input from `m_baseImage`, and write output through
`setDisplayPixmap()`. Mutate the manifest, re-derive the buffers, and call
`persistManifest()` — that is the only way an edit is applied. A non-destructive view
transform (like B&W) leaves `m_baseImage` alone.

## Side-by-side compare (`ImagePane`, `CompareTabBar`)

All of the per-image state above (the viewer, the manifest, the three buffers, and the
off-thread render watcher/debounce) lives in **`ImagePane`**, not `MainWindow`. `MainWindow`
keeps a `QList<ImagePane*>` and a `m_focus` index, plus the *shared* overlays/panels
(help, EXIF, background picker, adjust, B&W) which always act on `focused()`.

- **Layout** — the central widget is a container with a vertical layout: a `CompareTabBar`
  above a horizontal row of pane viewers. Single mode hides the strip and holds one viewer;
  compare mode shows two tabs over two viewers (equal stretch).
- **Entering** — `Shift+O` (`compareOpenRequested` → `openSecondImage`) shows the open
  dialog, then `openComparison(path)` builds a second `ImagePane`, adds its viewer, shows the
  tab bar, and focuses the new image. Both images start fit to their half (the late-added
  viewer is fit once via a deferred `ImageViewer::fitToWindow()`, since its real size is only
  known after layout).
- **Focus** — exactly one pane is focused; its tab is drawn lighter (`CompareTabBar::
  setFocusedIndex`). Clicking a tab (`tabSelected`) or clicking/scrolling a viewer
  (`ImageViewer::focusRequested`) calls `setFocusIndex()`, which restyles the tab, resyncs
  the shared panels to that pane's manifest, and gives the viewer keyboard focus. The
  app-wide key filter forwards every shortcut to `activeViewer()`, so even keys arriving at
  the *other* viewer act on the focused image.
- **Closing** — a tab's `✕` (`tabClosed` → `closePane`) deletes that pane's viewer (the pane
  is its `QObject` child, so it goes too), leaving the survivor as the sole, focused image and
  hiding the strip.
- **View sync** — `ImageViewer` exposes `relativeZoom()` (scale ÷ `fitScale()`) and
  `relativeCenter()` (viewport centre as a 0..1 fraction of the image), plus
  `applyRelativeView()` to set them. Any zoom or pan emits `viewChanged`; `MainWindow::
  syncViewFrom()` mirrors the **focused** viewer's relative view onto the other (guarded by
  `m_syncingViews`, and one-directional so the mirror target never echoes back). This makes
  zoom relative to "fit" and pan relative to pixels, so differently-sized images stay matched.

## Crop & rotate overlay (in `ImageViewer`)

`X` and `R` are two modes of one selection overlay
(`OverlayMode::{None, Crop, Rotate}`, driven by `setOverlayMode()`). Both put the same
bounding box over the **full, uncropped image**; they differ only in what a drag does.

- **Enter** (`enterOverlay()`): swaps the pixmap item to the overlay base
  (`m_cropBasePixmap`, set via `setBasePixmapForCrop()`; falls back to reloading the file
  from disk if unset, and caches the result), so the user always sees the whole upright
  original and can *expand* a previous selection as well as shrink it. Then
  `syncOverlayGeometry()` puts the current free angle on the item as a transform, sizes the
  scene to the rotated bounding box, the selection is armed, and `ScrollHandDrag` is off.
- **Switching `X` ⇄ `R`** applies **nothing** — the box and the image stay exactly as they
  are. Both `cropModeChanged` and `rotateModeChanged` are emitted, and `MainWindow` ignores
  either while `overlayActive()` is still true.
- **Crop interaction**: `hitTestHandle()` maps a viewport point to a corner (24 px grab),
  edge (15 px grab ≈ ±7.5× the border line width, anywhere along the edge), interior
  (move), or none. The grab distances and handle size derive from `kCropLineWidth`
  (top of `ImageViewer.cpp`). `mouseMoveEvent` resizes/moves `m_cropRect`, clamped to the
  rotated bounds and shrunk inside the tilted quad.
- **Rotate interaction**: only the four corners are live. A press on one records the
  pointer's angle about the scene centre (`pointerAngle()`); each move feeds the swept
  delta to `setRotateAngle()`. Hovering a corner shows a generated curved-arrow cursor
  (`rotateCursorPixmap()`, cached per 5°) turned so its two arrowheads lie along the two
  ways that corner can travel. Double-click resets the angle (crop mode's double-click
  resets the selection).
- **Painting** (`drawForeground()`): the dark mask over excluded regions and a dashed
  outline of the tilted photograph in scene coords, then a white border and handles in
  viewport coords — eight square handles in crop mode, four round ones in rotate mode.
- **Exit/apply** (`exitOverlay()`): bakes rotation *then* selection into the shown pixmap
  and emits the mode-changed signal(s). `MainWindow::commitOverlay()` then writes the angle
  into the manifest's `RotateEdit`, calls `rebuildOriented()` so the rotated buffer exists,
  and folds `viewer->cropRect()` into the `CropEdit` as a rect **normalized against that
  rotated buffer** (removing either edit when it returns to identity/full).
- `setCropRect()` clamps against the rotated bounds derived from `m_cropBasePixmap`, which
  lets `MainWindow` store a *transformed* crop rect while no overlay is open (see
  orientation below).

## Rotation geometry (`RotateGeometry`)

Rotating grows an image to the axis-aligned bounding box of the tilted original and leaves
blank triangles in the corners. Every helper here works in **that bounding box's coordinate
space**, which is also the space `m_cropRect` and the `CropEdit` live in. They share
`QImage::trueMatrix()` with the actual render, so the overlay's geometry and the produced
buffer never drift apart.

| Function | Purpose |
|---|---|
| `rotatedBounds(size, deg)` | The tilted original's four corners — the convex quad the selection must stay inside. |
| `boundingSize(size, deg)` | Size of the buffer `RotateEdit::apply()` returns. |
| `contains(poly, rect)` | All four rect corners inside the convex polygon (winding derived from the signed area, so orientation never has to be assumed). |
| `largestInscribedRect(size, deg)` | The maximum-area axis-aligned rectangle inside the tilted original. Closed-form, then nudged in by `shrinkToFit` because the analytic answer rests exactly on the edges and rounding can leave a corner a hair outside. |
| `shrinkToFit(rect, poly)` | Binary-searches the largest scale about the rect's own centre that fits; re-centres first if the centre has drifted off the quad. |
| `remapBetweenAngles(rect, size, from, to)` | Carries a selection from one angle's space to another's: same size, offset from the centre turned with the image, then shrunk to fit. |

**The guarantee:** a selection is never allowed outside `rotatedBounds()`, so the applied
crop can never include a blank corner and the result is always a full rectangle of
photograph. `ImageViewer` tracks whether the selection is *automatic* (`m_cropIsAuto`) — an
untouched box is recomputed as `largestInscribedRect()` on every angle change, so an
un-cropped photo that is straightened keeps the largest image possible; a box the user
placed is carried through `remapBetweenAngles()` and only shrunk when the tilt demands it.

## Rotate panel (`RotatePanel`)

The visible face of rotate mode, shown and hidden by `MainWindow` with the mode itself (it
never auto-dismisses, unlike `AdjustPanel` / `BwPanel`). It holds the two lossless
quarter-turn buttons — the behaviour the `R` key used to have — and a **Straighten** slider
in tenths of a degree over ±`RotateGeometry::kMaxAngle`. It only reports intent: the buttons
route to `applyOrientationStep()`, the slider to `ImageViewer::setRotateAngle()`, and
`rotateAngleChanged` from a corner drag echoes back into the slider.

## Orientation (`MainWindow::applyOrientationStep`)

- The step (`OrientationStep::RotateCW` / `RotateCCW` / `FlipH` / `FlipV`) is composed onto
  the manifest's `OrientationEdit`, which accumulates the **net** dihedral transform.
  `rebuildOriented()` re-derives `m_uprightImage = OrientationEdit::apply(m_diskImage)` — a
  single transform of the disk image, never an incremental transform of a transform.
- Quarter turns are available **from inside rotate mode** (its two buttons), so the step must
  work without tearing the overlay down: when one is open the live `viewer->cropRect()` is
  the authority and nothing is committed, otherwise the manifest's stored crop is.
- Quarter turns commute with the free rotation, so the angle rides along untouched. A **flip
  does not** — mirroring the frame reverses which way the image leans — so the `RotateEdit`'s
  angle is negated, which makes the new oriented image exactly the mirror of the old one.
- `QPixmap::trueMatrix(t, w, h)` reproduces the translation Qt adds when transforming, which
  is used to map the crop rect into the new coordinate space — so the same region stays
  selected after a rotate/flip.
- Order matters: the overlay base is re-armed to the new `m_uprightImage` (in
  `rebuildOriented()`) **before** the crop rect is remapped, because `setCropRect()` clamps
  against those bounds. A 90°/270° rotation swaps width and height, so clamping against the
  stale bounds would clip the rotated selection.
- `OrientationEdit::summary()` (`90° rotation`, `H flip`, `V flip`) and
  `RotateEdit::summary()` (`3.5° straighten`) feed the metadata edit-state line.

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

**`BwPanel`** is a frameless translucent `Qt::Tool` widget docked bottom-left. Seven look
buttons (exclusive `QButtonGroup`) sit above six hue-band sliders and a contrast slider,
all −100…100. Picking a look calls `BwConverter::lookPreset()` to load its defaults (bands
zeroed, the look's own contrast). It emits `paramsChanged` / `compareToggled` /
`resetToColorRequested`. Like the color picker, it auto-hides after `PANEL_DISMISS` unless
hovered or focused.

## Adjustments & colour (`AdjustEdit`, `ColorEdit`, `AdjustPanel`)

Two more edits adjust the colour image before any B&W conversion: `AdjustEdit` (light/tone:
brightness, contrast, exposure, saturation, black/white levels) and `ColorEdit` (colour
balance: temperature, tint, per-channel R/G/B, plus eight per-hue saturation bands). Both are
pure pixel passes in `ImageAdjust::applyTone()` / `applyColor()` (normalized float maths over
`Format_ARGB32`), neutral when every slider is 0 (the edit is then removed from the manifest).
They live at pipeline positions adjust → color, between crop and B&W.

**`AdjustPanel`** (the `C` key) is a frameless translucent `Qt::Tool` widget like `BwPanel`,
but split into two tabs — **Light & Levels** and **Color** — backing the two edits. The Color
tab carries the five balance sliders plus the eight hue-band sliders; each hue row shows a
colour swatch and a groove whose tint tracks the slider's current value (`styleHueGroove()`).
The active tab is persisted in `QSettings` (`adjustPanel/activeTab`), so the panel reopens on
whichever tab was last shown. It emits `adjustParamsChanged` / `colorParamsChanged`; a per-tab
**Reset** button zeroes the current tab.

**The per-image live-display pipeline (`ImagePane`):** adjust, color, and B&W all feed one
render path off `m_baseImage`, so the panels never race each other on the display. Each pane
owns its own debounce + render watcher, so two compared images render independently.
- `hasDisplayEdits()` is true when the manifest holds any post-crop edit. `scheduleRender()`
  shows `m_baseImage` directly when nothing applies (or while comparing), and otherwise arms
  `m_renderDebounce` (50 ms, coalescing slider drags).
- `applyRender()` runs `EditManifest::renderAfterCrop()` (a value copy of the whole manifest)
  via `QtConcurrent::run` + `m_renderWatcher` on `m_baseImage`; the UI never blocks. The
  watcher pushes `m_lastRenderPixmap` to the viewer only when display edits exist, not
  comparing, and not mid-overlay.
- **Compare** (`\` or the B&W panel button) toggles `m_comparing` between `m_baseImage` (the
  original colour) and `m_lastRenderPixmap`.
- B&W is still "active" exactly when the manifest holds a `BwEdit` (`bwActive()`); `W` records
  the panel's look, "Reset to Color" (`deactivateBw()`) removes it and re-renders whatever
  adjust/color edits remain. On image load, the pane re-applies any saved adjust/color/B&W
  settings and schedules one render; `MainWindow` reflects the focused pane's state into the
  shared panels (`syncPanelsToFocused()`).

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
(`EditManifest::summary()`, e.g. `90° rotation · H flip · crop · B&W`), authoritative
`{Dimensions}` (the EXIF-oriented size as loaded from `m_diskImage`, so it is always present
and correct after edits), and `{CurrentDimensions}` (the edited `m_baseImage` size, shown
only when it differs) into the data before the overlay is shown, so the overlay reflects
in-memory edits, not just the file's EXIF.

## Image format support

`supportedExtensions()` enumerates **every format Qt's image plugins support**
(`QImageReader::supportedImageFormats()`) — JPEG is primary but PNG and others load too.
This drives folder navigation, the file-dialog filter, and the macOS `NSOpenPanel`
content types. Folder *navigation* (`←`/`→`) additionally excludes `*.svg`. The Save
dialog defaults to `<name>-saved.jpg` and offers **every format Qt can write**
(`supportedSaveFilter()` → `QImageWriter::supportedImageFormats()`).

The TIFF/WebP/etc. plugins come from Qt's **`qtimageformats`** module, which
`fetch-linux-qt.sh` and the macOS CI install alongside `qtbase`. The Linux fetch also
drops a `libtiff.so.5` compat symlink into the Qt prefix, because the prebuilt `qtiff`
plugin links that older soname while modern Linux (Ubuntu 24.04+) ships `libtiff.so.6`.
The **"Open in..."** external-editor feature exports the edited image to a temp file as
**TIFF** — lossless, so the hand-off preserves full quality (see
`writeExportForExternalApp()` in `MainWindow.cpp`). On Windows the bundled Qt must
include the `qtiff` plugin for this export to work at runtime — see `doc/WINDOWS.md`.

## Adding a feature — checklist

1. If it reacts to a key, add the case to `ImageViewer::keyPressEvent` and **emit a
   signal** rather than acting directly (unless it's pure view state).
2. Wire the signal to a handler in the `MainWindow` constructor.
3. If it transforms the image, add an `ImageEdit` subclass, store its settings in the
   `EditManifest`, read `m_baseImage`, write `setDisplayPixmap()`, and obey the buffer-state
   contract above (mutate the manifest → re-derive the buffers → `persistManifest()`).
4. Long-running work goes off-thread (`QtConcurrent` + `QFutureWatcher`), mirroring B&W.
5. New overlay/panel: resize it in `MainWindow::resizeEvent`, add it to the Escape
   priority chain, and `raise()` it on show.
6. Add a `tests/test_*.cpp` (run headless with `QT_QPA_PLATFORM=offscreen`).
7. Update the help text in `HelpOverlay`, and `README.md` / `CLAUDE.md` / `ROADMAP.md` as needed.
