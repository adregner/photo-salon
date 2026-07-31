#pragma once
#include "Histogram.h"
#include <QWidget>

class QPaintEvent;

// ---------------------------------------------------------------------------
// The on-screen histogram, drawn the way a professional camera draws it: the
// three colour channels plotted additively (overlaps read yellow/cyan/white, as
// on a camera's RGB screen) over a filled luminance curve for combined exposure,
// with a zone grid, a black→white tone ramp under the plot, and clipping
// readouts at either end.
//
// Unlike the help/metadata overlays this is a small corner panel rather than a
// full-window sheet, so the photo stays visible while the histogram is up.
// MainWindow anchors it (see positionHistogram()) and feeds it data.
// ---------------------------------------------------------------------------
class HistogramOverlay : public QWidget {
    Q_OBJECT

public:
    explicit HistogramOverlay(QWidget *parent = nullptr);

    void setData(const HistogramData &data);
    const HistogramData &data() const { return m_data; }

    // Panel size for a window of `available` size: a camera-like landscape panel,
    // a quarter of the window wide, clamped so it stays readable but never eats
    // the frame on a small window.
    static QSize panelSizeFor(const QSize &available);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    // Trace of one 256-bin channel across `plot`, as a closed polygon down to the
    // baseline so it can be filled.
    QPolygonF traceFor(const std::array<quint32, HistogramData::Bins> &bins,
                       const QRectF &plot, quint32 ceiling) const;

    HistogramData m_data;
};
