#include "ImageAdjust.h"
#include <algorithm>
#include <cmath>

namespace {
inline double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
inline int    to8(double v)     { return static_cast<int>(std::lround(clamp01(v) * 255.0)); }
}  // namespace

bool ImageAdjust::isNeutral(const AdjustParams &p) {
    return p.brightness == 0 && p.contrast == 0 && p.exposure == 0
        && p.saturation == 0 && p.blacks == 0 && p.whites == 0;
}

bool ImageAdjust::isNeutral(const ColorParams &p) {
    return p.temperature == 0 && p.tint == 0
        && p.red == 0 && p.green == 0 && p.blue == 0;
}

QImage ImageAdjust::applyTone(const QImage &src, const AdjustParams &p) {
    if (src.isNull() || isNeutral(p)) return src;
    QImage img = src.convertToFormat(QImage::Format_ARGB32);

    // Precompute the per-channel curve coefficients once.
    const double exposure = std::pow(2.0, p.exposure / 100.0);   // ±1 stop at ±100
    const double inBlack  = p.blacks / 100.0 * 0.5;              // levels black point
    double       inWhite  = 1.0 - p.whites / 100.0 * 0.5;        // levels white point
    double       span     = inWhite - inBlack;
    if (span < 1e-3) span = 1e-3;                               // guard divide-by-zero
    const double bright   = p.brightness / 100.0 * 0.5;          // additive
    const double contrast = 1.0 + p.contrast / 100.0;            // 0..2 around mid-grey
    const double sat      = 1.0 + p.saturation / 100.0;          // 0..2 around luma

    auto tone = [&](double v) {
        v *= exposure;
        v = (v - inBlack) / span;             // black/white level remap
        v += bright;
        v = (v - 0.5) * contrast + 0.5;       // contrast around mid-grey
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

    const int h = img.height(), w = img.width();
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb px = line[x];
            const double r = qRed(px)   / 255.0 * rGain;
            const double g = qGreen(px) / 255.0 * gGain;
            const double b = qBlue(px)  / 255.0 * bGain;
            line[x] = qRgba(to8(r), to8(g), to8(b), qAlpha(px));
        }
    }
    return img;
}
