#include "ImageAdjust.h"
#include <algorithm>
#include <cmath>

namespace {
inline double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
inline int    to8(double v)     { return static_cast<int>(std::lround(clamp01(v) * 255.0)); }

// Eight reasonably-spaced, recognisable hues across the visible spectrum, with a
// vivid swatch for the UI. Order is the canonical order of ColorParams::hues.
const ImageAdjust::HueBand kHueBands[8] = {
    {"Red",       0.0, "#FF3030"},
    {"Orange",   30.0, "#FF8C1A"},
    {"Yellow",   60.0, "#F5D000"},
    {"Green",   120.0, "#33C13A"},
    {"Cyan",    180.0, "#15C4C4"},
    {"Blue",    225.0, "#2E6BFF"},
    {"Purple",  270.0, "#8A45E6"},
    {"Magenta", 315.0, "#E040C0"},
};

// Hue/saturation/value of an sRGB triple (h in 0..360, s/v in 0..1).
void rgbToHsv(double r, double g, double b, double &h, double &s, double &v) {
    const double mx = std::max({r, g, b});
    const double mn = std::min({r, g, b});
    const double d  = mx - mn;
    v = mx;
    s = mx > 1e-9 ? d / mx : 0.0;
    if (d < 1e-9) { h = 0.0; return; }
    if      (mx == r) h = 60.0 * std::fmod((g - b) / d, 6.0);
    else if (mx == g) h = 60.0 * ((b - r) / d + 2.0);
    else              h = 60.0 * ((r - g) / d + 4.0);
    if (h < 0.0) h += 360.0;
}

void hsvToRgb(double h, double s, double v, double &r, double &g, double &b) {
    const double c = v * s;
    const double x = c * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
    const double m = v - c;
    double rr = 0, gg = 0, bb = 0;
    if      (h <  60) { rr = c; gg = x; }
    else if (h < 120) { rr = x; gg = c; }
    else if (h < 180) { gg = c; bb = x; }
    else if (h < 240) { gg = x; bb = c; }
    else if (h < 300) { rr = x; bb = c; }
    else              { rr = c; bb = x; }
    r = rr + m; g = gg + m; b = bb + m;
}

// Smallest circular distance between two hue angles (0..180).
inline double hueDistance(double a, double b) {
    double d = std::fabs(a - b);
    return d > 180.0 ? 360.0 - d : d;
}
}  // namespace

int ImageAdjust::hueBandCount() { return 8; }

const ImageAdjust::HueBand &ImageAdjust::hueBand(int i) {
    return kHueBands[i < 0 ? 0 : (i > 7 ? 7 : i)];
}

bool ImageAdjust::isNeutral(const AdjustParams &p) {
    return p.brightness == 0 && p.contrast == 0 && p.exposure == 0
        && p.saturation == 0 && p.blacks == 0 && p.whites == 0;
}

bool ImageAdjust::isNeutral(const ColorParams &p) {
    if (p.temperature != 0 || p.tint != 0 || p.red != 0 || p.green != 0 || p.blue != 0)
        return false;
    for (int v : p.hues)
        if (v != 0) return false;
    return true;
}

QImage ImageAdjust::applyTone(const QImage &src, const AdjustParams &p) {
    if (src.isNull() || isNeutral(p)) return src;
    QImage img = src.convertToFormat(QImage::Format_ARGB32);

    // Precompute the per-channel curve coefficients once.
    const double exposure = std::pow(2.0, p.exposure / 100.0);   // ±1 stop at ±100
    double       inWhite  = 1.0 - p.whites / 100.0 * 0.5;        // white point (up = brighter)
    if (inWhite < 0.05) inWhite = 0.05;                          // guard divide-by-zero
    const double blacks   = p.blacks / 100.0 * 0.5;             // up = lighter, down = darker
    const double bright   = p.brightness / 100.0 * 0.5;          // additive
    const double contrast = 1.0 + p.contrast / 100.0;            // 0..2 around mid-grey
    const double sat      = 1.0 + p.saturation / 100.0;          // 0..2 around luma

    auto tone = [&](double v) {
        v *= exposure;
        v = v / inWhite;                          // white level (up brightens highlights)
        if (blacks > 0.0)
            v = blacks + v * (1.0 - blacks);      // lift the black floor → lighter blacks
        else if (blacks < 0.0)
            v = (v + blacks) / (1.0 + blacks);    // raise the input black point → darker
        v += bright;
        v = (v - 0.5) * contrast + 0.5;           // contrast around mid-grey
        return v;
    };

    const int h = img.height(), w = img.width();
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb px = line[x];
            double r = tone(qRed(px)   / 255.0);
            double g = tone(qGreen(px) / 255.0);
            double b = tone(qBlue(px)  / 255.0);

            if (p.saturation != 0) {
                const double luma = 0.299 * r + 0.587 * g + 0.114 * b;
                r = luma + (r - luma) * sat;
                g = luma + (g - luma) * sat;
                b = luma + (b - luma) * sat;
            }
            line[x] = qRgba(to8(r), to8(g), to8(b), qAlpha(px));
        }
    }
    return img;
}

QImage ImageAdjust::applyColor(const QImage &src, const ColorParams &p) {
    if (src.isNull() || isNeutral(p)) return src;
    QImage img = src.convertToFormat(QImage::Format_ARGB32);

    const double temp = p.temperature / 100.0 * 0.3;   // warm trades blue for red
    const double tint = p.tint / 100.0 * 0.3;           // magenta pulls green down
    const double rGain = (1.0 + temp) * (1.0 + p.red   / 100.0 * 0.5);
    const double gGain = (1.0 - tint) * (1.0 + p.green / 100.0 * 0.5);
    const double bGain = (1.0 - temp) * (1.0 + p.blue  / 100.0 * 0.5);

    // Per-hue saturation: collapse the eight band sliders into a 1°-resolution LUT
    // of saturation multipliers, so the hot loop is a single table lookup. Each
    // band contributes a cosine-falloff bump around its centre hue.
    bool huesActive = false;
    for (int v : p.hues) if (v != 0) { huesActive = true; break; }

    double satLut[360];
    if (huesActive) {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double window = 50.0;   // ± degrees of influence per band
        for (int deg = 0; deg < 360; ++deg) {
            double adj = 0.0;
            for (int b = 0; b < 8; ++b) {
                if (p.hues[b] == 0) continue;
                const double d = hueDistance(deg, kHueBands[b].center);
                if (d >= window) continue;
                const double wgt = 0.5 * (1.0 + std::cos(kPi * d / window));
                adj += wgt * (p.hues[b] / 100.0);
            }
            satLut[deg] = std::max(0.0, 1.0 + adj);   // saturation multiplier
        }
    }

    const int h = img.height(), w = img.width();
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb px = line[x];
            double r = qRed(px)   / 255.0 * rGain;
            double g = qGreen(px) / 255.0 * gGain;
            double b = qBlue(px)  / 255.0 * bGain;

            if (huesActive) {
                r = clamp01(r); g = clamp01(g); b = clamp01(b);
                double hh, ss, vv;
                rgbToHsv(r, g, b, hh, ss, vv);
                if (ss > 1e-4) {   // neutral pixels carry no hue to adjust
                    int deg = static_cast<int>(hh) % 360;
                    if (deg < 0) deg += 360;
                    ss = clamp01(ss * satLut[deg]);
                    hsvToRgb(hh, ss, vv, r, g, b);
                }
            }
            line[x] = qRgba(to8(r), to8(g), to8(b), qAlpha(px));
        }
    }
    return img;
}
