# Black & White Conversion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Press **W** to open a floating B&W panel. The image converts to grayscale using a hue-selective luminosity mixer (identical in concept to Photoshop's Black & White adjustment layer). Six sliders independently control how each hue band (Reds, Yellows, Greens, Cyans, Blues, Magentas) maps to brightness. Seven named presets correspond to classic Wratten darkroom filters. An **Auto** button analyzes the image and sets initial slider values for good tonal spread. Press **\\** (backslash) to toggle a before/after comparison at any time. **Escape** while the panel is visible reverts the image to full color and hides the panel. The panel auto-dismisses after 15 seconds of inactivity and the B&W conversion persists.

**Target users:** Photography experts and Photoshop power users. The vocabulary (Wratten filter numbers, hue band names, numerical slider values) must match what they already know.

---

## Architecture

### New classes

- **`BwConverter`** (`src/BwConverter.h/.cpp`) — stateless algorithm. Exposes:
  - `struct BwParams` — six `int` slider values (−100 to +100), one per hue band
  - `QImage BwConverter::convert(const QImage &src, const BwParams &p)` — returns `QImage::Format_Grayscale16`
  - `BwParams BwConverter::autoParams(const QImage &src)` — analyzes image, returns suggested params

- **`BwPanel`** (`src/BwPanel.h/.cpp`) — floating overlay widget parented to `MainWindow`, same dark translucent rounded style as `BackgroundColorPicker`. Contains preset buttons, six labeled sliders, and a Compare toggle button.

### Modified classes

- **`ImageViewer`** — two new key bindings (`W`, `\`) emit new signals; new public method `setDisplayPixmap()` lets `MainWindow` swap in the processed grayscale pixmap.
- **`MainWindow`** — owns `BwPanel`, holds the pre-conversion `QImage` as `m_originalImage` and `QPixmap` as `m_originalPixmap`, orchestrates the panel ↔ converter ↔ viewer loop. Conversion runs off the main thread via `QtConcurrent::run()` with a `QFutureWatcher` and a 50 ms debounce timer.
- **`HelpOverlay`** — two new lines in the keyboard shortcut text.
- **`CMakeLists.txt`** — four new source files added to `photo-salon-lib`; `Qt6::Concurrent` added to `target_link_libraries`.

### Data flow

```
W key
  → ImageViewer emits bwPanelRequested()
  → MainWindow::onBwPanelRequested()
      • captures viewer->pixmap() as m_originalPixmap and .toImage() as m_originalImage
      • calls applyBwConversion() directly (no debounce for initial render)
      • positions and shows BwPanel

applyBwConversion()
  → if m_bwWatcher->isRunning(): m_bwDebounce->start() (retry after current job finishes)
  → else: QtConcurrent::run(BwConverter::convert) → m_bwWatcher
  → m_bwWatcher::finished (main thread):
      m_lastBwImage  = result
      m_lastBwPixmap = QPixmap::fromImage(result)
      if m_bwActive && !m_bwComparing: viewer->setDisplayPixmap(m_lastBwPixmap)

BwPanel slider changed
  → BwPanel emits paramsChanged(BwParams)
  → MainWindow: m_bwDebounce->start() (50 ms, restarts on each change)
  → timer fires → applyBwConversion()

BwPanel Auto clicked
  → BwPanel emits autoRequested()
  → MainWindow: QtConcurrent::run(BwConverter::autoParams) → temp QFutureWatcher<BwParams>
  → watcher::finished (main thread): panel->setParams(result) → triggers paramsChanged → debounce

\ key
  → ImageViewer emits bwCompareRequested()
  → MainWindow::toggleBwCompare()
      • m_bwComparing = !m_bwComparing
      • QSignalBlocker syncs m_compareBtn checked state
      • if comparing: viewer->setDisplayPixmap(m_originalPixmap)
      • else:         viewer->setDisplayPixmap(m_lastBwPixmap)

Escape (while panel visible)
  → MainWindow::deactivateBw()
      • m_bwActive = false (in-flight conversion result silently discarded by watcher)
      • viewer->setDisplayPixmap(m_originalPixmap)
      • clears all images/pixmaps, stops debounce
      • hide panel

imagePathChanged signal (navigation / new load)
  → MainWindow::deactivateBw() silently (no panel interaction)
```

---

## Algorithm

### `BwConverter::convert()`

**Step 1 — normalize to linear sRGB float (one pass; handles any source color space including HDR and CMYK with embedded ICC profile):**
```cpp
QColorSpace srcSpace = src.colorSpace().isValid()
    ? src.colorSpace() : QColorSpace(QColorSpace::SRgb);
QColorTransform toLinear =
    srcSpace.transformationToColorSpace(QColorSpace(QColorSpace::SRgbLinear));
QImage img = src.colorTransformed(toLinear, QImage::Format_RGBX32FPx4);
```

**Step 2 — per pixel:**

For each pixel — `r`, `g`, `b` are `float` values in `[0, 1]` read directly from `Format_RGBX32FPx4`, already in linear light:

```
// srcLine = reinterpret_cast<const float *>(img.constScanLine(y))
r = srcLine[x * 4 + 0]
g = srcLine[x * 4 + 1]
b = srcLine[x * 4 + 2]

// BT.709 luminosity — correct on linear values
lum = 0.2126·r + 0.7152·g + 0.0722·b

// HSV saturation (colorfulness factor; 0 for gray pixels)
maxc = max(r, g, b)
minc = min(r, g, b)
delta = maxc − minc
sat_hsv = (maxc > 0) ? delta / maxc : 0

// HSV hue in [0, 360)
if delta < 1e-6:  hue = 0
elif maxc == r:   hue = 60 · fmod((g−b)/delta, 6)
elif maxc == g:   hue = 60 · ((b−r)/delta + 2)
else:             hue = 60 · ((r−g)/delta + 4)
if hue < 0: hue += 360

// Hue band lookup — tent-function interpolation between band centers at 0°, 60°, 120°, 180°, 240°, 300°
// bands[] order: [reds, yellows, greens, cyans, blues, magentas] (each slider value as float)
seg  = floor(hue / 60) % 6          // which 60° segment (0–5)
t    = (hue − seg·60) / 60          // position within segment [0, 1)
next = (seg + 1) % 6
adj  = bands[seg]·(1−t) + bands[next]·t   // interpolated adjustment

// Final output
output = clamp(lum + (adj / 100.0f) · sat_hsv, 0.0f, 1.0f)
out_pixel = round(output · 65535)   // uint16_t
```

The saturation multiplier guarantees that neutral-gray pixels (sat_hsv = 0) are always converted by plain BT.709 luminosity, regardless of slider values.

**Output format:** `QImage::Format_Grayscale16`

### `BwConverter::autoParams()`

Uses the same Step 1 linear-float normalization as `convert()`.

For each of the 6 hue bands:
1. Iterate all pixels. A pixel "belongs" to band i if `floor(hue/60) % 6 == i` (dominant band, no interpolation for band assignment).
2. Accumulate `sum_lum_sat += lum * sat_hsv` and `sum_sat += sat_hsv`.
3. If `sum_sat < 0.01` (band is essentially absent in this image), leave that slider at 0.
4. Else: `mean_lum = sum_lum_sat / sum_sat`
5. `adjustment = (0.5f − mean_lum) * 100.0f * 0.8f` (0.8 scale avoids over-correction)
6. Clamp to [−100, +100], round to int.

This nudges each hue band's average output toward the midtone, maximizing tonal spread without blowing highlights or crushing shadows.

---

## Preset Values

| Preset | Reds | Yellows | Greens | Cyans | Blues | Magentas |
|---|---|---|---|---|---|---|
| **Neutral** | 0 | 0 | 0 | 0 | 0 | 0 |
| **Yellow #8** | +20 | +50 | +20 | −10 | −50 | 0 |
| **Orange #15** | +40 | +60 | +10 | −30 | −70 | +20 |
| **Red A #25** | +80 | +40 | −40 | −80 | −90 | +50 |
| **Deep Red #29** | +100 | +60 | −60 | −100 | −100 | +70 |
| **Green #58** | −70 | −10 | +80 | +30 | −50 | −80 |
| **Infrared** | +60 | +50 | +100 | +30 | −80 | +20 |

Rationale: each preset simulates placing the corresponding Wratten gelatin filter on the enlarger lens (or camera lens) in a B&W darkroom workflow. Red/orange filters darken blue skies and lighten warm tones (classic landscape effect). The green filter lightens foliage and darkens skin and sky. The infrared preset simulates the Wood Effect where chlorophyll-rich foliage scatters near-IR radiation and appears nearly white.

---

## File Structure

- **Create:** `src/BwConverter.h`
- **Create:** `src/BwConverter.cpp`
- **Create:** `src/BwPanel.h`
- **Create:** `src/BwPanel.cpp`
- **Create:** `tests/test_bw_converter.cpp`
- **Modify:** `src/ImageViewer.h` — add signals `bwPanelRequested()`, `bwCompareRequested()`; add public method `setDisplayPixmap()`
- **Modify:** `src/ImageViewer.cpp` — add `W` and `\` key press cases
- **Modify:** `src/MainWindow.h` — add `BwPanel *m_bwPanel`, `QImage m_originalImage`, `QPixmap m_originalPixmap`, `QImage m_lastBwImage`, `QPixmap m_lastBwPixmap`, `bool m_bwActive`, `bool m_bwComparing`, `QFutureWatcher<QImage> *m_bwWatcher`, `QTimer *m_bwDebounce`; add private methods `onBwPanelRequested()`, `applyBwConversion()`, `toggleBwCompare()`, `deactivateBw()`
- **Modify:** `src/MainWindow.cpp` — implement all wiring
- **Modify:** `src/HelpOverlay.cpp` — add `W` and `\` shortcut lines
- **Modify:** `CMakeLists.txt` — add four new source files to `photo-salon-lib`; add `Qt6::Concurrent` to `target_link_libraries`

---

## Task 1: Register the test target and add sources to CMakeLists

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add new sources to `photo-salon-lib` in `CMakeLists.txt`**

In the `add_library(photo-salon-lib STATIC ...)` block, add:
```cmake
    src/BwConverter.cpp
    src/BwPanel.cpp
```

- [ ] **Step 1b: Add `Qt6::Concurrent` to `target_link_libraries` in `CMakeLists.txt`**

Change:
```cmake
target_link_libraries(photo-salon-lib PUBLIC Qt6::Widgets)
```
to:
```cmake
target_link_libraries(photo-salon-lib PUBLIC Qt6::Widgets Qt6::Concurrent)
```

- [ ] **Step 2: Append to `tests/CMakeLists.txt`**

```cmake
qt_add_executable(test_bw_converter test_bw_converter.cpp)
target_link_libraries(test_bw_converter PRIVATE Qt6::Test photo-salon-lib)
add_test(NAME test_bw_converter COMMAND test_bw_converter)
set_tests_properties(test_bw_converter PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Reconfigure (stubs not yet created — expect missing-file errors)**

```bash
cmake -B _build 2>&1 | tail -5
```

Proceed to the next task; the configure error will clear once the source files exist.

---

## Task 2: Create `BwConverter`

**Files:**
- Create: `src/BwConverter.h`
- Create: `src/BwConverter.cpp`

- [ ] **Step 1: Create `src/BwConverter.h`**

```cpp
#pragma once
#include <QImage>

struct BwParams {
    int reds     = 0;
    int yellows  = 0;
    int greens   = 0;
    int cyans    = 0;
    int blues    = 0;
    int magentas = 0;
};

namespace BwConverter {
    QImage   convert(const QImage &src, const BwParams &params);
    BwParams autoParams(const QImage &src);
}
```

- [ ] **Step 2: Create `src/BwConverter.cpp`**

```cpp
#include "BwConverter.h"
#include <algorithm>
#include <cmath>
#include <QColorSpace>

namespace {

inline float luminosity(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

void rgbToHSV(float r, float g, float b, float &hue, float &sat) {
    float maxc = std::max({r, g, b});
    float minc = std::min({r, g, b});
    float delta = maxc - minc;

    sat = (maxc > 0.0f) ? delta / maxc : 0.0f;

    if (delta < 1e-6f) { hue = 0.0f; return; }

    if      (maxc == r) hue = 60.0f * std::fmod((g - b) / delta, 6.0f);
    else if (maxc == g) hue = 60.0f * ((b - r) / delta + 2.0f);
    else                hue = 60.0f * ((r - g) / delta + 4.0f);

    if (hue < 0.0f) hue += 360.0f;
}

float hueAdjustment(float hue, const BwParams &p) {
    const float bands[6] = {
        (float)p.reds, (float)p.yellows, (float)p.greens,
        (float)p.cyans, (float)p.blues,  (float)p.magentas
    };
    int   seg  = (int)(hue / 60.0f) % 6;
    float t    = (hue - seg * 60.0f) / 60.0f;
    int   next = (seg + 1) % 6;
    return bands[seg] * (1.0f - t) + bands[next] * t;
}

QImage toLinearFloat(const QImage &src) {
    QColorSpace srcSpace = src.colorSpace().isValid()
        ? src.colorSpace() : QColorSpace(QColorSpace::SRgb);
    QColorTransform toLinear =
        srcSpace.transformationToColorSpace(QColorSpace(QColorSpace::SRgbLinear));
    return src.colorTransformed(toLinear, QImage::Format_RGBX32FPx4);
}

} // namespace

QImage BwConverter::convert(const QImage &src, const BwParams &params) {
    QImage img = toLinearFloat(src);
    QImage out(img.size(), QImage::Format_Grayscale16);

    const int w = img.width();
    const int h = img.height();

    for (int y = 0; y < h; ++y) {
        const float *srcLine = reinterpret_cast<const float *>(img.constScanLine(y));
        uint16_t    *dstLine = reinterpret_cast<uint16_t *>(out.scanLine(y));

        for (int x = 0; x < w; ++x) {
            float r = srcLine[x * 4 + 0];
            float g = srcLine[x * 4 + 1];
            float b = srcLine[x * 4 + 2];

            float lum = luminosity(r, g, b);
            float hue, sat;
            rgbToHSV(r, g, b, hue, sat);

            float adj    = hueAdjustment(hue, params) / 100.0f;
            float output = std::clamp(lum + adj * sat, 0.0f, 1.0f);
            dstLine[x]   = static_cast<uint16_t>(output * 65535.0f + 0.5f);
        }
    }

    return out;
}

BwParams BwConverter::autoParams(const QImage &src) {
    QImage img = toLinearFloat(src);

    double sumLumSat[6] = {};
    double sumSat[6]    = {};

    const int w = img.width();
    const int h = img.height();

    for (int y = 0; y < h; ++y) {
        const float *line = reinterpret_cast<const float *>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            float r = line[x * 4 + 0];
            float g = line[x * 4 + 1];
            float b = line[x * 4 + 2];

            float lum = luminosity(r, g, b);
            float hue, sat;
            rgbToHSV(r, g, b, hue, sat);

            int band        = (int)(hue / 60.0f) % 6;
            sumLumSat[band] += (double)(lum * sat);
            sumSat[band]    += (double)sat;
        }
    }

    BwParams result;
    int *sliders[6] = {
        &result.reds, &result.yellows, &result.greens,
        &result.cyans, &result.blues,  &result.magentas
    };

    for (int i = 0; i < 6; ++i) {
        if (sumSat[i] < 0.01) { *sliders[i] = 0; continue; }
        double mean = sumLumSat[i] / sumSat[i];
        double adj  = (0.5 - mean) * 100.0 * 0.8;
        *sliders[i] = (int)std::clamp(std::round(adj), -100.0, 100.0);
    }

    return result;
}
```

---

## Task 3: Write failing tests for `BwConverter`

**Files:**
- Create: `tests/test_bw_converter.cpp`

- [ ] **Step 1: Create `tests/test_bw_converter.cpp`**

```cpp
#include <QtTest/QtTest>
#include <QApplication>
#include "BwConverter.h"

class BwConverterTest : public QObject {
    Q_OBJECT

private slots:
    void neutralParamsMatchBT709Luminosity();
    void grayPixelUnaffectedBySliders();
    void positiveSlidersLightenMatchingHue();
    void negativeSlidersBarkenMatchingHue();
    void outputFormatIsGrayscale16();
    void autoParamsReturnValidRange();
    void autoParamsSkipAbsentBands();
    void convertHandlesRGB32AndRGB888Input();
};

static QImage makeSolid(int w, int h, QRgb color) {
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(color);
    return img;
}

static int readGray16(const QImage &img, int x, int y) {
    return (int)reinterpret_cast<const uint16_t *>(img.constScanLine(y))[x];
}

void BwConverterTest::neutralParamsMatchBT709Luminosity() {
    // Pure red pixel: sRGB (255,0,0) → linear (1.0,0,0)
    // BT.709 lum = 0.2126 → round(0.2126 * 65535) = 13933
    QImage img = makeSolid(1, 1, qRgb(255, 0, 0));
    BwParams p;
    QImage out = BwConverter::convert(img, p);
    QCOMPARE(out.format(), QImage::Format_Grayscale16);
    int val = readGray16(out, 0, 0);
    QVERIFY2(qAbs(val - 13933) <= 2, qPrintable(QString("expected ~13933 got %1").arg(val)));
}

void BwConverterTest::grayPixelUnaffectedBySliders() {
    // R=G=B=128: sat_hsv = 0, so sliders must have zero effect
    QImage img = makeSolid(1, 1, qRgb(128, 128, 128));
    BwParams p;
    p.reds = 100; p.blues = -100; p.greens = 50;
    QImage neutral; { BwParams n; neutral = BwConverter::convert(img, n); }
    QImage adjusted = BwConverter::convert(img, p);
    QCOMPARE(readGray16(neutral,  0, 0),
             readGray16(adjusted, 0, 0));
}

void BwConverterTest::positiveSlidersLightenMatchingHue() {
    // Pure green pixel (hue ≈ 120°): increasing greens slider should raise output
    QImage img = makeSolid(1, 1, qRgb(0, 255, 0));
    BwParams neutral, boosted;
    boosted.greens = 80;
    QImage outN = BwConverter::convert(img, neutral);
    QImage outB = BwConverter::convert(img, boosted);
    int vN = readGray16(outN, 0, 0);
    int vB = readGray16(outB, 0, 0);
    QVERIFY2(vB > vN, qPrintable(QString("expected boosted (%1) > neutral (%2)").arg(vB).arg(vN)));
}

void BwConverterTest::negativeSlidersBarkenMatchingHue() {
    // Pure blue pixel (hue ≈ 240°): decreasing blues slider should lower output
    QImage img = makeSolid(1, 1, qRgb(0, 0, 255));
    BwParams neutral, darkened;
    darkened.blues = -80;
    QImage outN = BwConverter::convert(img, neutral);
    QImage outD = BwConverter::convert(img, darkened);
    int vN = readGray16(outN, 0, 0);
    int vD = readGray16(outD, 0, 0);
    QVERIFY2(vD < vN, qPrintable(QString("expected darkened (%1) < neutral (%2)").arg(vD).arg(vN)));
}

void BwConverterTest::outputFormatIsGrayscale16() {
    QImage img = makeSolid(10, 10, qRgb(100, 150, 200));
    QImage out = BwConverter::convert(img, BwParams{});
    QCOMPARE(out.format(), QImage::Format_Grayscale16);
    QCOMPARE(out.size(), img.size());
}

void BwConverterTest::autoParamsReturnValidRange() {
    // Any image should produce params in [-100, 100]
    QImage img(64, 64, QImage::Format_RGB32);
    // Fill with a gradient of colors
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            img.setPixel(x, y, qRgb(x * 4, y * 4, (x + y) * 2));

    BwParams p = BwConverter::autoParams(img);
    QVERIFY(p.reds     >= -100 && p.reds     <= 100);
    QVERIFY(p.yellows  >= -100 && p.yellows  <= 100);
    QVERIFY(p.greens   >= -100 && p.greens   <= 100);
    QVERIFY(p.cyans    >= -100 && p.cyans    <= 100);
    QVERIFY(p.blues    >= -100 && p.blues    <= 100);
    QVERIFY(p.magentas >= -100 && p.magentas <= 100);
}

void BwConverterTest::autoParamsSkipAbsentBands() {
    // Pure red image — only the reds band is populated; others should stay 0
    QImage img = makeSolid(20, 20, qRgb(255, 0, 0));
    BwParams p = BwConverter::autoParams(img);
    QCOMPARE(p.yellows,  0);
    QCOMPARE(p.greens,   0);
    QCOMPARE(p.cyans,    0);
    QCOMPARE(p.blues,    0);
    QCOMPARE(p.magentas, 0);
}

void BwConverterTest::convertHandlesRGB32AndRGB888Input() {
    QImage rgb32(4, 4, QImage::Format_RGB32);
    rgb32.fill(qRgb(200, 100, 50));
    QImage rgb888 = rgb32.convertToFormat(QImage::Format_RGB888);

    QImage out32  = BwConverter::convert(rgb32,  BwParams{});
    QImage out888 = BwConverter::convert(rgb888, BwParams{});

    // Both should produce the same grayscale value
    QCOMPARE(readGray16(out32,  0, 0),
             readGray16(out888, 0, 0));
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    BwConverterTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_bw_converter.moc"
```

- [ ] **Step 2: Build and run — expect all tests to pass**

```bash
cmake -B _build && cmake --build _build --target test_bw_converter 2>&1 | tail -10
QT_QPA_PLATFORM=offscreen ./_build/tests/test_bw_converter
```

Expected:
```
PASS   : BwConverterTest::neutralParamsMatchBT709Luminosity()
PASS   : BwConverterTest::grayPixelUnaffectedBySliders()
PASS   : BwConverterTest::positiveSlidersLightenMatchingHue()
PASS   : BwConverterTest::negativeSlidersBarkenMatchingHue()
PASS   : BwConverterTest::outputFormatIsGrayscale16()
PASS   : BwConverterTest::autoParamsReturnValidRange()
PASS   : BwConverterTest::autoParamsSkipAbsentBands()
PASS   : BwConverterTest::convertHandlesRGB32AndRGB888Input()
Totals: 8 passed, 0 failed, 0 skipped
```

- [ ] **Step 3: Commit**

```bash
git add src/BwConverter.h src/BwConverter.cpp \
        tests/test_bw_converter.cpp tests/CMakeLists.txt \
        CMakeLists.txt
git commit -m "feat: add BwConverter with hue-selective luminosity mixing"
```

---

## Task 4: Create `BwPanel`

**Files:**
- Create: `src/BwPanel.h`
- Create: `src/BwPanel.cpp`

### Panel visual specification

- Parent: `MainWindow`, positioned in the lower-left corner (10px from left and bottom edges), same as `BackgroundColorPicker`
- Widget flags: `Qt::Tool | Qt::FramelessWindowHint`
- Attribute: `Qt::WA_TranslucentBackground`
- Background: painted rounded rect, `QColor(30, 30, 30, 220)`, radius 6
- Fixed width: 680px; height: computed from content
- Focus policy: `Qt::StrongFocus`
- Auto-dismiss timer: 15 seconds, resets on mouse enter or focus gain (identical behavior to `BackgroundColorPicker`)

### Layout (top to bottom, QVBoxLayout, margins 12/10/12/12)

**Row 1 — Preset buttons (QHBoxLayout, spacing 4):**
Eight `QPushButton` instances, flat style, text labels:
`"Neutral"`, `"Yellow #8"`, `"Orange #15"`, `"Red A #25"`, `"Deep Red #29"`, `"Green #58"`, `"Infrared"`, `"Auto"`
Style: `"QPushButton { color: white; background: #444; border: 1px solid #666; border-radius: 3px; padding: 3px 7px; font-size: 12px; } QPushButton:hover { background: #555; }"`
All buttons except Auto are preset triggers. Auto emits `autoRequested()`.

**Row 2 — Separator:** `QFrame::HLine`, `QFrame::Sunken`, with 4px vertical margins

**Rows 3–8 — Six slider rows (QGridLayout, 6 columns, spacing 6):**

For each hue band in order [Reds, Yellows, Greens, Cyans, Blues, Magentas]:

| Column | Widget | Details |
|---|---|---|
| 0 | Color swatch `QLabel` | Fixed 14×14 px, `setAutoFillBackground(true)`, background color per band (see below), 1px solid #888 border via stylesheet |
| 1 | Band name `QLabel` | Fixed width 68px, right-aligned, white text, 13px font, text = band name |
| 2 | `QSlider` | Horizontal, range −100 to +100, single step 1, page step 10, initial value 0, minimum width 400px |
| 3 | Value `QLabel` | Fixed width 38px, center-aligned, white text, 13px monospace font, initial text `" +0"` |

Swatch colors:
- Reds: `#FF3333`
- Yellows: `#FFEE00`
- Greens: `#33CC33`
- Cyans: `#00CCCC`
- Blues: `#3366FF`
- Magentas: `#CC33CC`

Value label format: `QString("%1%2").arg(v >= 0 ? " +" : " ").arg(v)` — always 3 chars wide (space + sign + up to 3 digits). Use a monospace font so the slider doesn't jump horizontally.

**Row 9 — Bottom row (QHBoxLayout):**
- Left: `QLabel` with hint text `"W: show/hide  \\: compare  Esc: revert"`, color #888, 11px font
- Right: `QPushButton` m_compareBtn, text `"Compare"`, checkable=true, same flat style as preset buttons

**Row 10 — Separator + vertical spacer:** small (4px) bottom margin

- [ ] **Step 1: Create `src/BwPanel.h`**

```cpp
#pragma once
#include "BwConverter.h"
#include <QWidget>

class QEnterEvent;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;

class BwPanel : public QWidget {
    Q_OBJECT
public:
    explicit BwPanel(QWidget *parent = nullptr);

    BwParams    params() const;
    void        setParams(const BwParams &p);
    QPushButton *compareButton() const { return m_compareBtn; }

signals:
    void paramsChanged(const BwParams &p);
    void autoRequested();
    void compareToggled(bool showOriginal);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void applyPreset(const BwParams &p);
    void onAnySliderChanged();
    void updateDismissTimer();

    struct BandRow {
        QSlider *slider;
        QLabel  *valueLabel;
    };

    BandRow      m_bands[6];
    QPushButton *m_compareBtn = nullptr;
    QTimer      *m_dismissTimer = nullptr;
    bool         m_mouseOver = false;
    bool         m_inhibitSignal = false;
};
```

- [ ] **Step 2: Create `src/BwPanel.cpp`**

Implement the full widget. Key points:

- Constructor builds the layout exactly as specified above.
- Each slider's `valueChanged` signal connects to `onAnySliderChanged()`.
- `onAnySliderChanged()` updates the corresponding value label and, unless `m_inhibitSignal` is true, emits `paramsChanged(params())`.
- `setParams()` sets `m_inhibitSignal = true`, sets all six sliders, updates all value labels, then sets `m_inhibitSignal = false`, then emits `paramsChanged(params())` once.
- `params()` reads all six slider values into a `BwParams` and returns it.
- `applyPreset(p)` calls `setParams(p)`.
- The Auto button connects to `emit autoRequested()`.
- The Compare button (`m_compareBtn`) connects to `emit compareToggled(m_compareBtn->isChecked())`.
- Dismiss timer: 15 seconds, single-shot. `enterEvent` stops it; `leaveEvent` and focus changes restart it (same logic as `BackgroundColorPicker::updateDismissTimer()`). `hideEvent` stops it and resets `m_mouseOver`.
- `paintEvent` draws the rounded background rect with `QColor(30, 30, 30, 220)` and radius 6.
- Connect `qApp->focusChanged` to `updateDismissTimer()` (same as `BackgroundColorPicker`).

Preset button connections (in constructor, one connect per button):
```cpp
connect(btnNeutral,  &QPushButton::clicked, this, [this]{ applyPreset({0,0,0,0,0,0}); });
connect(btnYellow,   &QPushButton::clicked, this, [this]{ applyPreset({20,50,20,-10,-50,0}); });
connect(btnOrange,   &QPushButton::clicked, this, [this]{ applyPreset({40,60,10,-30,-70,20}); });
connect(btnRedA,     &QPushButton::clicked, this, [this]{ applyPreset({80,40,-40,-80,-90,50}); });
connect(btnDeepRed,  &QPushButton::clicked, this, [this]{ applyPreset({100,60,-60,-100,-100,70}); });
connect(btnGreen,    &QPushButton::clicked, this, [this]{ applyPreset({-70,-10,80,30,-50,-80}); });
connect(btnInfrared, &QPushButton::clicked, this, [this]{ applyPreset({60,50,100,30,-80,20}); });
connect(btnAuto,     &QPushButton::clicked, this, [this]{ emit autoRequested(); });
```

`BwParams` aggregate initialization order is `{reds, yellows, greens, cyans, blues, magentas}`.

- [ ] **Step 3: Build the lib target**

```bash
cmake -B _build && cmake --build _build --target photo-salon-lib 2>&1 | tail -10
```

Expected: exits 0, no warnings.

- [ ] **Step 4: Commit**

```bash
git add src/BwPanel.h src/BwPanel.cpp
git commit -m "feat: add BwPanel floating overlay with hue sliders and presets"
```

---

## Task 5: Add key bindings to `ImageViewer`

**Files:**
- Modify: `src/ImageViewer.h`
- Modify: `src/ImageViewer.cpp`

- [ ] **Step 1: Update `src/ImageViewer.h`**

Add to `signals:`:
```cpp
void bwPanelRequested();
void bwCompareRequested();
```

Add to `public:`:
```cpp
void setDisplayPixmap(const QPixmap &px);
```

- [ ] **Step 2: Implement `setDisplayPixmap` in `src/ImageViewer.cpp`**

```cpp
void ImageViewer::setDisplayPixmap(const QPixmap &px) {
    if (m_pixmapItem)
        m_pixmapItem->setPixmap(px);
}
```

- [ ] **Step 3: Add key cases in `keyPressEvent` in `src/ImageViewer.cpp`**

Add before `default:`:
```cpp
case Qt::Key_W:
    emit bwPanelRequested();
    event->accept();
    break;
case Qt::Key_Backslash:
    emit bwCompareRequested();
    event->accept();
    break;
```

- [ ] **Step 4: Build**

```bash
cmake --build _build 2>&1 | tail -5
```

Expected: exits 0.

- [ ] **Step 5: Commit**

```bash
git add src/ImageViewer.h src/ImageViewer.cpp
git commit -m "feat: add W and \\ key bindings to ImageViewer for B&W panel"
```

---

## Task 6: Wire everything in `MainWindow`

**Files:**
- Modify: `src/MainWindow.h`
- Modify: `src/MainWindow.cpp`

- [ ] **Step 1: Update `src/MainWindow.h`**

Add forward declarations / includes at top:
```cpp
#include <QFutureWatcher>
class BwPanel;
class QTimer;
```

Add to private members:
```cpp
BwPanel                *m_bwPanel       = nullptr;
QImage                  m_originalImage;
QPixmap                 m_originalPixmap;
QImage                  m_lastBwImage;
QPixmap                 m_lastBwPixmap;
bool                    m_bwActive      = false;
bool                    m_bwComparing   = false;
QFutureWatcher<QImage> *m_bwWatcher     = nullptr;
QTimer                 *m_bwDebounce    = nullptr;
```

Add private methods:
```cpp
void onBwPanelRequested();
void applyBwConversion();
void toggleBwCompare();
void deactivateBw();
```

- [ ] **Step 2: Update `src/MainWindow.cpp`**

Add includes at top:
```cpp
#include "BwConverter.h"
#include "BwPanel.h"
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QTimer>
```

**In the constructor**, after the `m_colorPicker` block, add:

```cpp
m_bwDebounce = new QTimer(this);
m_bwDebounce->setSingleShot(true);
m_bwDebounce->setInterval(50);
connect(m_bwDebounce, &QTimer::timeout, this, &MainWindow::applyBwConversion);

m_bwWatcher = new QFutureWatcher<QImage>(this);
connect(m_bwWatcher, &QFutureWatcher<QImage>::finished, this, [this]() {
    m_lastBwImage  = m_bwWatcher->result();
    m_lastBwPixmap = QPixmap::fromImage(m_lastBwImage);
    if (m_bwActive && !m_bwComparing)
        m_viewer->setDisplayPixmap(m_lastBwPixmap);
});

m_bwPanel = new BwPanel(this);
m_bwPanel->hide();

connect(viewer, &ImageViewer::bwPanelRequested,   this, &MainWindow::onBwPanelRequested);
connect(viewer, &ImageViewer::bwCompareRequested,  this, &MainWindow::toggleBwCompare);

connect(m_bwPanel, &BwPanel::paramsChanged, this, [this](const BwParams &) {
    if (m_bwActive && !m_bwComparing)
        m_bwDebounce->start();
});

connect(m_bwPanel, &BwPanel::autoRequested, this, [this]() {
    if (m_originalImage.isNull()) return;
    QImage src = m_originalImage;
    auto *watcher = new QFutureWatcher<BwParams>(this);
    connect(watcher, &QFutureWatcher<BwParams>::finished, this, [this, watcher]() {
        m_bwPanel->setParams(watcher->result());
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([src]() { return BwConverter::autoParams(src); }));
});

connect(m_bwPanel, &BwPanel::compareToggled, this, [this](bool showOriginal) {
    m_bwComparing = showOriginal;
    if (!m_bwActive || m_originalImage.isNull()) return;
    if (showOriginal)
        m_viewer->setDisplayPixmap(m_originalPixmap);
    else if (!m_lastBwPixmap.isNull())
        m_viewer->setDisplayPixmap(m_lastBwPixmap);
});

// Reset B&W state when a new image loads
connect(viewer, &ImageViewer::imagePathChanged, this, [this]() {
    deactivateBw();
});
```

**Add `onBwPanelRequested()`:**

```cpp
void MainWindow::onBwPanelRequested() {
    if (m_bwPanel->isVisible()) {
        m_bwPanel->raise();
        m_bwPanel->setFocus();
        return;
    }

    if (!m_bwActive) {
        QPixmap current = m_viewer->pixmap();
        if (current.isNull()) return;
        m_originalPixmap = current;
        m_originalImage  = current.toImage();
        m_bwActive = true;
        applyBwConversion();
    }

    int x = 10;
    int y = height() - m_bwPanel->sizeHint().height() - 10;
    m_bwPanel->move(x, y);
    m_bwPanel->show();
    m_bwPanel->raise();
    m_bwPanel->setFocus();
}
```

**Add `applyBwConversion()`:**

```cpp
void MainWindow::applyBwConversion() {
    if (!m_bwActive || m_originalImage.isNull()) return;
    if (m_bwWatcher->isRunning()) {
        m_bwDebounce->start();  // retry once current job finishes
        return;
    }
    QImage   src = m_originalImage;
    BwParams p   = m_bwPanel->params();
    m_bwWatcher->setFuture(
        QtConcurrent::run([src, p]() { return BwConverter::convert(src, p); }));
}
```

**Add `toggleBwCompare()`:**

```cpp
void MainWindow::toggleBwCompare() {
    if (!m_bwActive || m_originalImage.isNull()) return;
    m_bwComparing = !m_bwComparing;
    {
        QSignalBlocker blocker(m_bwPanel->compareButton());
        m_bwPanel->compareButton()->setChecked(m_bwComparing);
    }
    if (m_bwComparing)
        m_viewer->setDisplayPixmap(m_originalPixmap);
    else if (!m_lastBwPixmap.isNull())
        m_viewer->setDisplayPixmap(m_lastBwPixmap);
}
```

**Add `deactivateBw()`:**

```cpp
void MainWindow::deactivateBw() {
    if (!m_bwActive && m_originalImage.isNull()) return;

    m_bwActive    = false;  // cleared first — watcher::finished checks this before updating display
    m_bwComparing = false;
    m_bwDebounce->stop();

    if (!m_originalPixmap.isNull())
        m_viewer->setDisplayPixmap(m_originalPixmap);

    m_originalImage  = {};
    m_originalPixmap = {};
    m_lastBwImage    = {};
    m_lastBwPixmap   = {};

    if (m_bwPanel)
        m_bwPanel->hide();
}
```

**Add Escape handling in `eventFilter`:**

In the `Key_Escape` block, add a check before the fullscreen check:
```cpp
if (m_bwPanel && m_bwPanel->isVisible()) {
    deactivateBw();
    return true;
}
```
This block must appear after the `m_colorPicker` and `m_helpOverlay` checks, and before the fullscreen check.

**Add panel repositioning in `resizeEvent`:**

After the existing `m_colorPicker` repositioning block:
```cpp
if (m_bwPanel && m_bwPanel->isVisible()) {
    int y = height() - m_bwPanel->sizeHint().height() - 10;
    m_bwPanel->move(10, y);
}
```

- [ ] **Step 3: Build the full app**

```bash
cmake -B _build && cmake --build _build 2>&1 | tail -10
```

Expected: exits 0, photo-salon binary built.

- [ ] **Step 4: Run full test suite**

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir _build -V 2>&1 | tail -20
```

Expected: all tests pass (including pre-existing tests).

- [ ] **Step 5: Commit**

```bash
git add src/MainWindow.h src/MainWindow.cpp
git commit -m "feat: wire B&W panel into MainWindow with async conversion and compare"
```

---

## Task 7: Update `HelpOverlay`

**Files:**
- Modify: `src/HelpOverlay.cpp`

- [ ] **Step 1: Add two lines to the help text**

In the `text` string constant, add after the `"  S        Save current image\n"` line:
```cpp
"  W        Black & white conversion\n"
"  \\        Compare B&W / original\n"
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build _build 2>&1 | tail -5
```

- [ ] **Step 3: Commit**

```bash
git add src/HelpOverlay.cpp
git commit -m "docs: add W and \\ shortcuts to help overlay"
```

---

## Task 8: Run full suite and push

- [ ] **Step 1: Clean build and full test run**

```bash
cmake -B _build && cmake --build _build 2>&1 | tail -5
QT_QPA_PLATFORM=offscreen ctest --test-dir _build -V
```

Expected: all tests pass, zero build warnings.

- [ ] **Step 2: Push**

```bash
git push -u origin claude/sleepy-galileo-VNVRZ
```

---

## Notes

### Async conversion and thread safety

`BwConverter::convert()` and `autoParams()` run on a `QtConcurrent` thread pool thread. Both functions are pure (no shared mutable state) and capture their inputs by value, so they are thread-safe. The `QFutureWatcher::finished` signal is delivered on the main thread via Qt's event loop.

If a slider change arrives while a conversion is already running, `applyBwConversion()` detects `m_bwWatcher->isRunning()` and restarts the 50 ms debounce timer rather than queuing a second future. This means at most one conversion is in flight at any time, and the result is always based on the slider values current at the time of the last fire.

`deactivateBw()` sets `m_bwActive = false` before returning. Any in-flight conversion will complete normally, but its `finished` handler checks `m_bwActive` before updating the display, so the result is silently discarded.

### Color space handling

`BwConverter::toLinearFloat()` uses `QColorSpace::transformationToColorSpace(QColorSpace::SRgbLinear)` (Qt 6.8) combined with the 3-argument `colorTransformed(transform, Format_RGBX32FPx4)` overload to convert format and color space in a single pass. This correctly handles:
- Standard sRGB JPEGs (most camera output)
- Wide-gamut sources (Display P3, Adobe RGB) — hue angles are computed from perceptually correct linear values
- HDR sources (Bt2100Pq, Bt2020) — tone-mapped to linear sRGB before processing
- CMYK JPEGs with embedded ICC profiles — Qt 6.8's ICC A2B lut support handles the CMYK→linear sRGB mapping via the embedded profile

Images without an embedded color space are assumed to be sRGB.

The BT.709 luminance coefficients (0.2126 R + 0.7152 G + 0.0722 B) are defined for linear light and are applied correctly here. The 16-bit output (`Format_Grayscale16`) preserves the precision of the linear computation.

### `setDisplayPixmap` does not affect `m_nativeSize` or `m_imagePath`

The method only touches `m_pixmapItem->setPixmap()`. This means `currentPath()`, `nativeImageSize()`, and the save path logic all continue to reference the original file. The B&W pixmap will be what gets saved if the user presses S while B&W is active (because `currentDisplayPixmap()` returns `m_pixmapItem->pixmap()`), which is the correct behavior — "save what you see."

### Compare button vs `\` key

Both the `\` key (via `bwCompareRequested` signal) and the Compare toggle button in `BwPanel` trigger comparison mode. `toggleBwCompare()` uses `QSignalBlocker` on `m_compareBtn` to keep the button's checked state in sync with the keyboard shortcut without risk of signal recursion.

### Interaction with crop mode

`loadImage()` is called when the user navigates to a new image (arrow keys) or `Tab`-picks a file. It emits `imagePathChanged`, which `MainWindow` connects to `deactivateBw()`. This means crop mode and B&W mode cannot be active simultaneously in a conflicting way — navigating to a new image always resets B&W state cleanly.

If the user enters crop mode while B&W is active, the crop tool calls `loadImage`-equivalent logic internally that reloads from disk. After exiting crop mode, `m_pixmapItem` contains the (possibly cropped) original-color image. B&W state will have been cleared by `imagePathChanged`. The user must press W again to re-apply B&W to the cropped result. This is the correct behavior — the B&W conversion is a display transform on top of whatever is currently loaded, not a persistent edit.
