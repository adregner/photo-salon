#include "HistogramOverlay.h"
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPolygonF>
#include <QRectF>

namespace {
// Panel geometry, all in device-independent pixels.
constexpr qreal kPadding    = 11.0;   // panel edge → content
constexpr qreal kRadius     = 6.0;    // panel corner radius
constexpr qreal kRowGap     = 6.0;    // header → plot, plot → tone ramp
constexpr int   kZones      = 5;      // vertical grid divisions (camera zone bars)
constexpr double kClipFloor = 0.001;  // show a clipping readout above 0.1 % of pixels

// Fills used for the additive channel traces, and the brighter outline colours
// drawn over them.
const QColor kChannelFill[3] = {QColor(255,  56,  56, 120),
                                QColor( 56, 255,  86, 120),
                                QColor( 64, 128, 255, 120)};
const QColor kChannelLine[3] = {QColor(255, 120, 120, 210),
                                QColor(120, 255, 150, 210),
                                QColor(130, 175, 255, 210)};

const QColor kLumaFill(255, 255, 255,  60);
const QColor kLumaLine(255, 255, 255, 165);
const QColor kTitle   (180, 220, 255);
const QColor kShadowClip(130, 190, 255);
const QColor kHighClip  (255, 190,  80);
}  // namespace

HistogramOverlay::HistogramOverlay(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    hide();
}

// static
QSize HistogramOverlay::panelSizeFor(const QSize &available) {
    const int w = qBound(240, available.width() / 4, 400);
    return QSize(w, int(w * 0.66));
}

void HistogramOverlay::setData(const HistogramData &data) {
    m_data = data;
    update();
}

QPolygonF HistogramOverlay::traceFor(const std::array<quint32, HistogramData::Bins> &bins,
                                     const QRectF &plot, quint32 ceiling) const {
    QPolygonF poly;
    poly.reserve(HistogramData::Bins + 2);
    poly << QPointF(plot.left(), plot.bottom());
    for (int i = 0; i < HistogramData::Bins; ++i) {
        const qreal x = plot.left() + plot.width() * i / double(HistogramData::Bins - 1);
        // Counts above the ceiling are clipped flat against the top of the plot,
        // exactly as a camera clips its peak rather than rescaling the curve.
        const qreal frac = qMin(1.0, double(bins[i]) / double(ceiling));
        poly << QPointF(x, plot.bottom() - frac * plot.height());
    }
    poly << QPointF(plot.right(), plot.bottom());
    return poly;
}

void HistogramOverlay::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Panel
    const QRectF panel = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(QColor(255, 255, 255, 45), 1.0));
    p.setBrush(QColor(0, 0, 0, 212));
    p.drawRoundedRect(panel, kRadius, kRadius);

    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSizeF(qMax(8.0, height() * 0.058));
    p.setFont(font);
    const QFontMetricsF fm(font);

    const QRectF inner = panel.adjusted(kPadding, kPadding, -kPadding, -kPadding);
    const QRectF header(inner.left(), inner.top(), inner.width(), fm.height());

    p.setPen(kTitle);
    p.drawText(header, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Histogram"));

    // Clipping readouts, right-aligned: shadows first, then highlights. Both are
    // per-channel worst cases, so a single blown channel still raises the flag.
    auto clipText = [](QChar arrow, double frac) {
        return QString("%1%2%").arg(arrow).arg(frac * 100.0, 0, 'f', 1);
    };
    qreal clipRight = header.right();
    if (m_data.clippedHighlights() > kClipFloor) {
        const QString t = clipText(QChar(0x25B2), m_data.clippedHighlights());
        const qreal w = fm.horizontalAdvance(t);
        p.setPen(kHighClip);
        p.drawText(QRectF(clipRight - w, header.top(), w, header.height()),
                   Qt::AlignRight | Qt::AlignVCenter, t);
        clipRight -= w + fm.horizontalAdvance(QLatin1Char(' ')) * 2;
    }
    if (m_data.clippedShadows() > kClipFloor) {
        const QString t = clipText(QChar(0x25BC), m_data.clippedShadows());
        const qreal w = fm.horizontalAdvance(t);
        p.setPen(kShadowClip);
        p.drawText(QRectF(clipRight - w, header.top(), w, header.height()),
                   Qt::AlignRight | Qt::AlignVCenter, t);
    }

    // Plot area, with the black→white tone ramp pinned to the bottom edge.
    const qreal rampH = qMax(6.0, height() * 0.035);
    const QRectF plot(inner.left(), header.bottom() + kRowGap, inner.width(),
                      inner.bottom() - rampH - kRowGap - (header.bottom() + kRowGap));
    const QRectF ramp(inner.left(), inner.bottom() - rampH, inner.width(), rampH);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 10));
    p.drawRect(plot);

    // Zone grid: verticals at each fifth of the tonal range plus a half-height
    // reference line, the reading aid a camera's histogram screen provides.
    p.setPen(QPen(QColor(255, 255, 255, 32), 1.0));
    for (int i = 1; i < kZones; ++i) {
        const qreal x = plot.left() + plot.width() * i / double(kZones);
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }
    p.drawLine(QPointF(plot.left(),  plot.center().y()),
               QPointF(plot.right(), plot.center().y()));

    if (m_data.isEmpty()) {
        p.setPen(QColor(255, 255, 255, 110));
        p.drawText(plot, Qt::AlignCenter, QStringLiteral("no image"));
        return;
    }

    const quint32 ceiling = m_data.plotCeiling();

    // Combined exposure (luminance) sits behind the channels as a soft fill —
    // the overall brightness distribution the colour traces are read against.
    const QPolygonF lumaTrace = traceFor(m_data.luma, plot, ceiling);
    p.setPen(Qt::NoPen);
    p.setBrush(kLumaFill);
    p.drawPolygon(lumaTrace);

    // Red, green and blue composited additively, so where two channels overlap
    // the plot goes yellow / cyan / magenta and all three read white — the look
    // of an in-camera RGB histogram.
    const std::array<quint32, HistogramData::Bins> *channels[3] =
        {&m_data.red, &m_data.green, &m_data.blue};
    QPolygonF traces[3];
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    for (int c = 0; c < 3; ++c) {
        traces[c] = traceFor(*channels[c], plot, ceiling);
        p.setBrush(kChannelFill[c]);
        p.drawPolygon(traces[c]);
    }
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    // Outlines on top: the channel edges, then the luminance curve brightest of
    // all so the combined exposure stays legible over busy colour.
    p.setBrush(Qt::NoBrush);
    for (int c = 0; c < 3; ++c) {
        p.setPen(QPen(kChannelLine[c], 1.0));
        p.drawPolyline(traces[c].mid(1, HistogramData::Bins));
    }
    p.setPen(QPen(kLumaLine, 1.4));
    p.drawPolyline(lumaTrace.mid(1, HistogramData::Bins));

    // Tone ramp: which part of the plot is shadow, midtone, highlight.
    QLinearGradient g(ramp.topLeft(), ramp.topRight());
    g.setColorAt(0.0, QColor(0, 0, 0));
    g.setColorAt(1.0, QColor(255, 255, 255));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRect(ramp);
    p.setPen(QPen(QColor(255, 255, 255, 45), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(ramp);
}
