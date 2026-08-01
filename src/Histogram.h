#pragma once
#include <QtGlobal>
#include <array>

class QImage;

// ---------------------------------------------------------------------------
// Tone distribution of one image: the three colour channels plus the combined
// (luminance) exposure curve — the same four traces a professional camera shows
// on its RGB histogram screen.
//
// Counts are of *sampled* pixels, not necessarily every pixel: like a camera,
// which builds its histogram from the preview image rather than the full sensor
// readout, Histogram::compute() sub-samples large images on a regular grid so a
// 50 MP frame costs the same as a small one. The shape is unchanged; only the
// absolute counts scale, and every trace is plotted relative to plotCeiling().
// ---------------------------------------------------------------------------
struct HistogramData {
    static constexpr int Bins = 256;

    std::array<quint32, Bins> red{};
    std::array<quint32, Bins> green{};
    std::array<quint32, Bins> blue{};
    std::array<quint32, Bins> luma{};   // Rec.709 luma of the gamma-encoded pixel

    quint64 sampleCount = 0;            // pixels actually counted

    bool isEmpty() const { return sampleCount == 0; }

    // Bin count that maps to the full height of the plot. The extreme bins (0 and
    // 255) are excluded: a black surround or a blown sky piles up thousands of
    // pixels in a single end bin, which would squash the rest of the curve flat.
    // Cameras clip that spike the same way rather than rescaling everything.
    quint32 plotCeiling() const;

    // Fraction (0..1) of samples pinned at an extreme in the channel that clips
    // most — the "blinkies" readout. Per-channel, so a blown red still registers
    // even when the luminance curve looks safe.
    double clippedShadows() const;
    double clippedHighlights() const;
};

namespace Histogram {

// Upper bound on sampled pixels. ~1 M samples is far more than the 256 bins need
// to be stable, and keeps the pass a few milliseconds even for huge files.
constexpr int MaxSamples = 1 << 20;

// Count the tone distribution of `image`, sub-sampling on a regular grid when it
// holds more than `maxSamples` pixels. Any format is accepted (it is converted to
// 8-bit RGB first); a null image yields empty data.
HistogramData compute(const QImage &image, int maxSamples = MaxSamples);

}  // namespace Histogram
