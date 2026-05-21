# Zoom Feature Design

**Date:** 2026-05-21
**Status:** Approved

## Overview

Add zoom to `ImageViewer` (QGraphicsView subclass): mouse wheel to zoom anchored to the cursor position, keyboard shortcuts for step zoom and reset.

## Inputs

| Input | Action |
|-------|--------|
| Mouse wheel up | Zoom in, anchored to cursor position |
| Mouse wheel down | Zoom out, anchored to cursor position |
| `+` or `=` | Zoom in one step |
| `-` | Zoom out one step |
| `0` | Reset to fit-to-window |

## Behavior

**Zoom anchor:** Cursor position. The scene point under the mouse stays fixed as the view scales. Implemented via `setTransformationAnchor(QGraphicsView::AnchorUnderMouse)`.

**Step size:** Exponential — each step multiplies or divides the scale by `1.15`. One mouse wheel tick (delta = 120) equals one step. Fractional ticks scale proportionally via `pow(1.15, delta / 120.0)`.

**Zoom limits:** 5% minimum (scale 0.05), 3200% maximum (scale 32.0). At the limits, zoom events are silently ignored (no visual feedback needed).

**Fit-to-window state:** Tracked with a `bool m_fitted` member. When `true`, `showEvent` re-fits on resize (preserving current behavior). Any zoom operation sets `m_fitted = false`, so the zoom level is maintained across resizes. Pressing `0` sets `m_fitted = true` and calls `fitImage()`.

**Pan:** No additional work. `ScrollHandDrag` (already set) enables click-drag panning automatically once the scene extends beyond the viewport.

## Implementation

### Changes to `ImageViewer`

**Constructor additions:**
- `setTransformationAnchor(QGraphicsView::AnchorUnderMouse)`
- Initialize `m_fitted = true`

**New member:**
- `bool m_fitted` — tracks whether the view is in fit-to-window state

**New overrides:**
- `wheelEvent(QWheelEvent*)` — compute factor from delta, clamp, call `scale()`
- `keyPressEvent(QKeyEvent*)` — dispatch `+`/`-`/`0`

**Modified:**
- `showEvent` — guard `fitImage()` call with `if (m_fitted)`

**Clamping logic (shared by wheel and key handlers):**
```
currentScale = transform().m11()
newScale = currentScale * factor
if newScale < 0.05 or newScale > 32.0: return
scale(factor, factor)
m_fitted = false
```

No changes to `MainWindow` or `main.cpp`.

## Out of Scope

- Zoom percentage indicator / status bar
- Animated zoom transitions
- Pinch-to-zoom (touch input)
