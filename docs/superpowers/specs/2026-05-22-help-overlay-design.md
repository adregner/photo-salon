# Help Overlay Design

**Date:** 2026-05-22
**Status:** Approved

## Overview

Add a keyboard-triggered help overlay to photo-salon. Pressing `?` displays a semi-transparent black panel listing all recognized keyboard shortcuts and mouse controls. Any keypress dismisses the overlay; the triggering key is also processed normally so controls remain active while the overlay is visible.

## Components

### `HelpOverlay : QWidget` (new)

- Files: `src/HelpOverlay.h`, `src/HelpOverlay.cpp`
- Focus policy: `Qt::NoFocus` — never intercepts keyboard input
- Mouse attribute: `Qt::WA_TransparentForMouseEvents` — never intercepts mouse input
- `paintEvent`: fills the widget rect with `QColor(0, 0, 0, 128)` (50% opacity black), then draws the shortcut content as white text centered on the overlay
- Starts hidden; shown/hidden entirely via `QWidget::setVisible`

### `ImageViewer` changes

New member:
- `bool m_helpVisible = false` — tracks whether the help overlay should be shown

New signal:
- `void helpVisibilityChanged(bool visible)` — emitted whenever help visibility changes

New accessor:
- `bool helpVisible() const` — returns `m_helpVisible`; used by tests

`keyPressEvent` changes:
- At entry: if `m_helpVisible` is true and the pressed key is not `?`, set `m_helpVisible = false` and emit `helpVisibilityChanged(false)`, then fall through to normal key processing so the key still takes effect
- Add `Qt::Key_Question` case: toggle `m_helpVisible`, emit `helpVisibilityChanged(m_helpVisible)`, accept event

### `MainWindow` changes

New member:
- `HelpOverlay *m_helpOverlay`

Construction:
- Instantiate `m_helpOverlay` as a child of `MainWindow` after creating the viewer
- Connect `viewer->helpVisibilityChanged` to `m_helpOverlay->setVisible`
- Set initial overlay geometry to match the window

`resizeEvent` override:
- Resize `m_helpOverlay` to match the new window size so it always covers the full window

## Content

The overlay renders two labeled sections, centered on the darkened background, in white text.

**Keyboard shortcuts**

| Key | Action |
|-----|--------|
| `+` / `=` | Zoom in |
| `-` | Zoom out |
| `0` | Fit to window |
| `?` | Show/hide this help |

**Mouse & other controls**

| Input | Action |
|-------|--------|
| Scroll wheel | Zoom (anchored to cursor) |

Visual style: white text on the semi-transparent black background, two sections separated by a small gap, fixed-width rendering for the key column. No border or rounded corners.

## Behavior

- `?` pressed while overlay hidden → show overlay (`m_helpVisible = true`)
- `?` pressed while overlay visible → hide overlay (`m_helpVisible = false`)
- Any other key pressed while overlay visible → hide overlay, then process the key normally (zoom, fit, etc. still take effect)
- Overlay covers the full `MainWindow` area and resizes with it

## Testing

New file `tests/test_help_overlay.cpp`, following the same QtTest pattern as `test_zoom.cpp`. Linked as a new `test_help_overlay` target in `tests/CMakeLists.txt`.

| Test | Verifies |
|------|----------|
| `keyQuestionShowsOverlay` | Press `?` on a fresh viewer → `helpVisible()` is true |
| `keyQuestionTogglesOverlay` | Press `?` twice → `helpVisible()` is false |
| `anyKeyDismissesOverlay` | Show overlay, press `+` → `helpVisible()` is false |
| `zoomStillWorksWhenOverlayVisible` | Show overlay, press `+` → scale increased (key processed normally) |

Tests operate on `ImageViewer` directly via `helpVisible()`. `MainWindow` and the actual `HelpOverlay` widget are not involved in unit tests.

## Build

Add `HelpOverlay.cpp` to `photo-salon-lib` sources in the root `CMakeLists.txt`.

## Maintenance

When a new keyboard shortcut or mouse control is added, update the content table in `HelpOverlay::paintEvent`. Both sections (Keyboard shortcuts and Mouse & other controls) must be kept current.

## Out of Scope

- Animated fade in/out for the overlay
- Mouse click to dismiss
- Configurable key bindings
- Scrollable shortcut list (not needed at current scale)
