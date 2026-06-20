#pragma once
#include <QImage>
#include <QMetaType>

// ---------------------------------------------------------------------------
// Tonal and colour adjustment maths, kept off the GUI thread by operating on a
// plain QImage. AdjustParams covers the light/tone controls (brightness,
// contrast, exposure, saturation, and the black/white level endpoints);
// ColorParams covers the colour-balance controls (temperature, tint, and the
// per-channel red/green/blue gains). Both are intentionally separate so they map
// to two independent edits in the manifest, even though one panel drives them.
// ---------------------------------------------------------------------------

// Light & tone. Each slider is -100..100, 0 = no change.
struct AdjustParams {
    int brightness = 0;   // additive lift/drop of overall level
    int contrast   = 0;   // expand/compress around mid-grey
    int exposure   = 0;   // multiplicative, in stops (±1 stop at ±100)
    int saturation = 0;   // pull colours toward/away from their luma
    int blacks     = 0;   // input black point (raise to deepen shadows)
    int whites     = 0;   // input white point (raise to brighten highlights)
};

// Colour balance. Each slider is -100..100, 0 = no change.
struct ColorParams {
    int temperature = 0;  // cool (−) ↔ warm (+): trades blue for red
    int tint        = 0;  // green (−) ↔ magenta (+)
    int red         = 0;  // per-channel gain
    int green       = 0;
    int blue        = 0;
    // Per-hue saturation, one entry per band in ImageAdjust::hueBand() order.
    // Positive vivifies that family of colours, negative mutes it toward grey.
    int hues[8]     = {0, 0, 0, 0, 0, 0, 0, 0};
};

namespace ImageAdjust {
    // The eight hue bands the Color tab exposes — a name, the hue angle they are
    // centred on (degrees, 0..360), and a vivid swatch colour for the UI.
    struct HueBand { const char *name; double center; const char *swatch; };
    int             hueBandCount();       // 8
    const HueBand  &hueBand(int i);

    // Apply the tone/colour curve to a copy of src and return it. A neutral
    // parameter set returns src unchanged (and cheaply).
    QImage applyTone(const QImage &src, const AdjustParams &p);
    QImage applyColor(const QImage &src, const ColorParams &p);

    // True when every slider is at 0 (nothing to apply).
    bool isNeutral(const AdjustParams &p);
    bool isNeutral(const ColorParams &p);
}

// Registered so the params can travel through signals/slots and QSignalSpy.
Q_DECLARE_METATYPE(AdjustParams)
Q_DECLARE_METATYPE(ColorParams)
