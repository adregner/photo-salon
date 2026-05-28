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
