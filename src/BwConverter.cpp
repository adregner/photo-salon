#include "BwConverter.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <QColorSpace>

namespace {

// ---------------------------------------------------------------------------
// sRGB transfer (linear light -> gamma-encoded display value).
// ---------------------------------------------------------------------------
inline float linToSrgb(float c) {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.0031308f ? c * 12.92f
                           : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

// ---------------------------------------------------------------------------
// Per-look definition.
//
// Two levers give each look its character:
//   * channel weights (wR,wG,wB, summing to 1) decide how bright each colour
//     renders. They are applied either in linear light (true luminance, e.g. a
//     panchromatic sensor) or on the gamma-encoded values (luma, how most
//     digital editors mix), per `gammaMix`.
//   * a tonal curve — black point, S-curve contrast, highlight shoulder and an
//     output floor — gives the look its "punch".
//
// `contrast` is the *default* S-curve strength loaded into the panel's Contrast
// slider when the look is picked; the user can then push it either way.
// ---------------------------------------------------------------------------
struct LookDef {
    float wR, wG, wB;   // channel weights (sum to 1)
    bool  gammaMix;     // mix in gamma (luma) vs linear light (luminance)
    float blackPoint;   // crush shadows from below  [0..1)
    float shoulder;     // highlight roll-off strength [0..]
    float blackLift;    // raise the output floor (film toe) [0..1)
    int   contrast;     // default S-curve strength for the slider
    const char *name;
};

LookDef def(BwLook look) {
    switch (look) {
    //                       wR      wG      wB     gamma  black  shldr  lift  ctr  name
    case BwLook::Neutral:      return {0.2126f,0.7152f,0.0722f, false, 0.00f, 0.00f, 0.000f,  0, "Neutral"};
    case BwLook::Photoshop:    return {0.400f, 0.400f, 0.200f,  true,  0.00f, 0.00f, 0.000f, 12, "Photoshop"};
    case BwLook::IPhone:       return {0.250f, 0.550f, 0.200f,  true,  0.04f, 0.50f, 0.000f, 40, "iPhone"};
    case BwLook::Monochrom:    return {0.400f, 0.340f, 0.260f,  false, 0.00f, 0.35f, 0.000f, 10, "Monochrom"};
    case BwLook::ClassicLuma:  return {0.299f, 0.587f, 0.114f,  true,  0.00f, 0.00f, 0.000f,  0, "Classic"};
    case BwLook::Film:         return {0.280f, 0.560f, 0.160f,  true,  0.00f, 0.60f, 0.045f, 22, "Film"};
    case BwLook::HighContrast: return {0.300f, 0.550f, 0.150f,  true,  0.06f, 0.20f, 0.000f, 70, "High Contrast"};
    }
    return {0.2126f,0.7152f,0.0722f,false,0.0f,0.0f,0.0f,0,"Neutral"};
}

// Normalised logistic S-curve: maps 0->0, 0.5->0.5, 1->1 for any strength `a`.
// a>0 adds contrast, a<0 removes it. e0/e1 are the endpoint constants (precomputed).
inline float sCurve(float x, float a, float e0, float e1) {
    if (std::fabs(a) < 1e-4f) return x;
    float l = 1.0f / (1.0f + std::exp(-a * (x - 0.5f)));
    return (l - e0) / (e1 - e0);
}

// Full tonal curve for a look, applied to a perceptual grey in [0,1].
float applyTone(float g, const LookDef &d, float a, float e0, float e1) {
    if (d.blackPoint > 0.0f)                                  // input black point
        g = std::clamp((g - d.blackPoint) / (1.0f - d.blackPoint), 0.0f, 1.0f);

    g = sCurve(g, a, e0, e1);                                 // contrast

    if (d.shoulder > 0.0f) {                                  // highlight roll-off
        const float K = 0.7f;
        if (g > K) {
            float t = (g - K) / (1.0f - K);
            g = K + (1.0f - K) * std::pow(t, 1.0f + d.shoulder);
        }
    }

    if (d.blackLift > 0.0f)                                   // output floor (toe)
        g = d.blackLift + g * (1.0f - d.blackLift);

    return std::clamp(g, 0.0f, 1.0f);
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
    const LookDef d = def(params.look);

    QImage img = toLinearFloat(src);
    QImage out(img.size(), QImage::Format_Grayscale16);

    const int w = img.width();
    const int h = img.height();

    // Two 1-D lookup tables, constant for the whole image, keep the hot loop free
    // of pow()/exp(): one maps linear light -> sRGB; the other is the full tonal
    // curve (a pure function of the perceptual grey value).
    constexpr int N = 4096;
    std::vector<float> lin2srgb(N + 1);
    for (int i = 0; i <= N; ++i)
        lin2srgb[i] = linToSrgb((float)i / N);

    const float a  = (params.contrast / 100.0f) * 6.0f;   // S-curve strength
    const float e0 = 1.0f / (1.0f + std::exp(a * 0.5f));
    const float e1 = 1.0f / (1.0f + std::exp(-a * 0.5f));
    std::vector<float> tone(N + 1);
    for (int i = 0; i <= N; ++i)
        tone[i] = applyTone((float)i / N, d, a, e0, e1);

    auto toSrgb = [&](float lin) {
        int i = (int)(std::clamp(lin, 0.0f, 1.0f) * N + 0.5f);
        return lin2srgb[i];
    };

    for (int y = 0; y < h; ++y) {
        const float *srcLine = reinterpret_cast<const float *>(img.constScanLine(y));
        uint16_t    *dstLine = reinterpret_cast<uint16_t *>(out.scanLine(y));

        for (int x = 0; x < w; ++x) {
            float rL = srcLine[x * 4 + 0];
            float gL = srcLine[x * 4 + 1];
            float bL = srcLine[x * 4 + 2];

            // sRGB-encoded values: needed for luma looks and for hue/saturation.
            float rS = toSrgb(rL), gS = toSrgb(gL), bS = toSrgb(bL);

            // Base grey, in perceptual (sRGB) space.
            float grey = d.gammaMix
                ? (d.wR * rS + d.wG * gS + d.wB * bS)             // luma
                : toSrgb(d.wR * rL + d.wG * gL + d.wB * bL);      // luminance

            // Hue-selective adjustment (the six sliders), perceptually.
            float hue, sat;
            rgbToHSV(rS, gS, bS, hue, sat);
            float adj = hueAdjustment(hue, params) / 100.0f;
            grey = std::clamp(grey + adj * sat, 0.0f, 1.0f);

            // Tonal curve.
            int ti = (int)(grey * N + 0.5f);
            dstLine[x] = static_cast<uint16_t>(tone[ti] * 65535.0f + 0.5f);
        }
    }

    return out;
}

BwParams BwConverter::lookPreset(BwLook look) {
    BwParams p;
    p.look     = look;
    p.contrast = def(look).contrast;
    return p;
}

const char *BwConverter::lookName(BwLook look) {
    return def(look).name;
}
