#include "RotateGeometry.h"
#include <QImage>
#include <QTransform>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
// Half a tenth of a degree — below the panel's finest step, so an angle this
// small can never have been asked for on purpose.
constexpr double kAngleEpsilon = 0.005;

// Containment slack, in pixels. Coordinates here are pixel magnitudes computed
// in double precision, so anything above rounding noise is a real overhang.
constexpr double kInsideEpsilon = 1e-6;

// The transform Qt itself uses to rotate an image: the rotation plus the
// translation that lands the result's top-left at the origin. Sharing it is what
// keeps the overlay's geometry and the rendered buffer in the same coordinates.
QTransform trueRotation(const QSize &size, double degrees) {
    return QImage::trueMatrix(QTransform().rotate(degrees), size.width(), size.height());
}
}  // namespace

namespace RotateGeometry {

bool isZeroAngle(double degrees) {
    return std::abs(degrees) < kAngleEpsilon;
}

double clampAngle(double degrees) {
    if (!std::isfinite(degrees)) return 0.0;
    const double clamped = std::clamp(degrees, -kMaxAngle, kMaxAngle);
    return isZeroAngle(clamped) ? 0.0 : clamped;
}

QPolygonF rotatedBounds(const QSize &size, double degrees) {
    if (size.isEmpty()) return {};
    const QRectF r(0, 0, size.width(), size.height());
    QPolygonF corners;
    corners << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft();
    if (isZeroAngle(degrees)) return corners;
    return trueRotation(size, degrees).map(corners);
}

QSizeF boundingSize(const QSize &size, double degrees) {
    if (size.isEmpty()) return {};
    if (isZeroAngle(degrees)) return QSizeF(size);
    return rotatedBounds(size, degrees).boundingRect().size();
}

bool contains(const QPolygonF &poly, const QRectF &rect) {
    if (poly.size() < 3 || !rect.isValid()) return false;

    const QPointF corners[4] = {rect.topLeft(), rect.topRight(),
                                rect.bottomRight(), rect.bottomLeft()};

    // The polygon's winding decides which side of an edge counts as "inside", so
    // derive it from the signed area rather than assuming a direction.
    double twiceArea = 0.0;
    for (int i = 0; i < poly.size(); ++i) {
        const QPointF &a = poly[i];
        const QPointF &b = poly[(i + 1) % poly.size()];
        twiceArea += a.x() * b.y() - b.x() * a.y();
    }
    const double winding = twiceArea >= 0.0 ? 1.0 : -1.0;

    for (int i = 0; i < poly.size(); ++i) {
        const QPointF &a = poly[i];
        const QPointF &b = poly[(i + 1) % poly.size()];
        const QPointF edge = b - a;
        const double len = std::hypot(edge.x(), edge.y());
        if (len <= 0.0) continue;   // a closed polygon repeats its first point
        for (const QPointF &c : corners) {
            // Cross product over the edge length: the corner's signed distance
            // from the edge, in pixels.
            const double dist = (edge.x() * (c.y() - a.y()) - edge.y() * (c.x() - a.x())) / len;
            if (winding * dist < -kInsideEpsilon) return false;
        }
    }
    return true;
}

QRectF largestInscribedRect(const QSize &size, double degrees) {
    if (size.isEmpty()) return {};
    const double w = size.width();
    const double h = size.height();
    const QSizeF bb = boundingSize(size, degrees);
    if (isZeroAngle(degrees)) return QRectF(0, 0, w, h);

    const double rad  = qDegreesToRadians(degrees);
    const double sinA = std::abs(std::sin(rad));
    const double cosA = std::abs(std::cos(rad));

    // A quarter turn leaves no blank corners at all, so the whole frame fits.
    if (sinA < 1e-9 || cosA < 1e-9)
        return QRectF(QPointF(0, 0), bb);

    const QPolygonF bounds = rotatedBounds(size, degrees);
    const bool   widthLonger = w >= h;
    const double sideLong    = widthLonger ? w : h;
    const double sideShort   = widthLonger ? h : w;

    double rw = 0.0, rh = 0.0;
    if (sideShort <= 2.0 * sinA * cosA * sideLong || std::abs(sinA - cosA) < 1e-9) {
        // Half-constrained: the rectangle touches the tilted frame's midpoints,
        // and one of its corners rides the short side.
        const double half = 0.5 * sideShort;
        rw = widthLonger ? half / sinA : half / cosA;
        rh = widthLonger ? half / cosA : half / sinA;
    } else {
        const double cos2a = cosA * cosA - sinA * sinA;
        rw = (w * cosA - h * sinA) / cos2a;
        rh = (h * cosA - w * sinA) / cos2a;
    }

    rw = std::clamp(rw, 1.0, bb.width());
    rh = std::clamp(rh, 1.0, bb.height());

    QRectF result(0, 0, rw, rh);
    result.moveCenter(bounds.boundingRect().center());
    // The closed-form rectangle rests exactly on the tilted edges, so rounding
    // in the transform can leave a corner a hair outside. Nudge it back in — the
    // shrink is far below a pixel, and containment is what callers rely on.
    return shrinkToFit(result, bounds);
}

QRectF shrinkToFit(const QRectF &rect, const QPolygonF &poly) {
    if (poly.size() < 3 || rect.isEmpty()) return rect;
    if (contains(poly, rect)) return rect;

    // Shrinking is only meaningful about a centre that is itself inside; if the
    // selection has drifted off the tilted frame, pull it back to the middle.
    QPointF centre = rect.center();
    QRectF probe(0, 0, 1e-3, 1e-3);
    probe.moveCenter(centre);
    if (!contains(poly, probe))
        centre = poly.boundingRect().center();

    auto scaled = [&](double factor) {
        QRectF r(0, 0, rect.width() * factor, rect.height() * factor);
        r.moveCenter(centre);
        return r;
    };

    // Binary search the largest factor that still fits. `lo` always holds a
    // fitting rectangle, so the loop can only ever return one.
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 40; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (contains(poly, scaled(mid))) lo = mid;
        else                             hi = mid;
    }
    return scaled(lo);
}

QRectF remapBetweenAngles(const QRectF &rect, const QSize &size,
                          double fromDegrees, double toDegrees) {
    if (size.isEmpty() || rect.isEmpty()) return rect;

    const QSizeF fromBounds = boundingSize(size, fromDegrees);
    const QSizeF toBounds   = boundingSize(size, toDegrees);
    const QPointF fromCentre(fromBounds.width() / 2.0, fromBounds.height() / 2.0);
    const QPointF toCentre(toBounds.width() / 2.0, toBounds.height() / 2.0);

    // Both spaces are centred on the image, so only the offset from the centre
    // has to turn with it.
    const QPointF offset =
        QTransform().rotate(toDegrees - fromDegrees).map(rect.center() - fromCentre);

    QRectF moved(QPointF(0, 0), rect.size());
    moved.moveCenter(toCentre + offset);
    return shrinkToFit(moved, rotatedBounds(size, toDegrees));
}

}  // namespace RotateGeometry
