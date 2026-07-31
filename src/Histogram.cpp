#include "Histogram.h"
#include <QImage>
#include <algorithm>
#include <cmath>

quint32 HistogramData::plotCeiling() const {
    quint32 peak = 0;
    for (int i = 1; i < Bins - 1; ++i) {
        peak = std::max({peak, red[i], green[i], blue[i], luma[i]});
    }
    if (peak > 0)
        return peak;

    // Everything lives in the end bins (a pure black or pure white frame): fall
    // back to the true maximum so the trace is still drawn.
    for (int i : {0, Bins - 1})
        peak = std::max({peak, red[i], green[i], blue[i], luma[i]});
    return std::max<quint32>(peak, 1);
}

double HistogramData::clippedShadows() const {
    if (sampleCount == 0) return 0.0;
    const quint32 worst = std::max({red[0], green[0], blue[0]});
    return double(worst) / double(sampleCount);
}

double HistogramData::clippedHighlights() const {
    if (sampleCount == 0) return 0.0;
    const quint32 worst = std::max({red[Bins - 1], green[Bins - 1], blue[Bins - 1]});
    return double(worst) / double(sampleCount);
}

HistogramData Histogram::compute(const QImage &image, int maxSamples) {
    HistogramData h;
    if (image.isNull())
        return h;

    // One 8-bit RGB layout for the hot loop, whatever came in (B&W renders arrive
    // as Grayscale16, adjust/colour renders as ARGB32).
    const QImage src = (image.format() == QImage::Format_RGB32
                        || image.format() == QImage::Format_ARGB32)
        ? image
        : image.convertToFormat(QImage::Format_RGB32);
    if (src.isNull())
        return h;

    const int width  = src.width();
    const int height = src.height();
    const qint64 total = qint64(width) * qint64(height);

    // Same step in x and y, so the sample grid stays uniform over the frame and
    // no repeating structure in the image can bias one axis.
    int step = 1;
    if (maxSamples > 0 && total > maxSamples)
        step = int(std::ceil(std::sqrt(double(total) / double(maxSamples))));

    for (int y = 0; y < height; y += step) {
        const auto *line = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        for (int x = 0; x < width; x += step) {
            const QRgb px = line[x];
            const int r = qRed(px), g = qGreen(px), b = qBlue(px);
            ++h.red[r];
            ++h.green[g];
            ++h.blue[b];
            // Rec.709 luma on the gamma-encoded values — the perceptual
            // brightness a camera plots as its combined exposure curve.
            // Weights are 0.2126/0.7152/0.0722 scaled by 256 (54+183+19 = 256).
            ++h.luma[(r * 54 + g * 183 + b * 19) >> 8];
            ++h.sampleCount;
        }
    }
    return h;
}
