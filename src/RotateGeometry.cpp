#include "RotateGeometry.h"
#include <QImage>
#include <QTransform>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

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

// A linear constraint `nx*x + ny*y >= d`. The tilted bounds are convex, so
// "inside" is exactly the intersection of one of these per edge — and so is
// every constrained drag derived from them.
struct HalfPlane {
    double nx, ny, d;
};

// The polygon's edges as inward half-planes, with unit normals so a violation
// reads as a distance in pixels.
std::vector<HalfPlane> insideHalfPlanes(const QPolygonF &poly) {
    std::vector<HalfPlane> planes;
    if (poly.size() < 3) return planes;

    double twiceArea = 0.0;
    for (int i = 0; i < poly.size(); ++i) {
        const QPointF &a = poly[i];
        const QPointF &b = poly[(i + 1) % poly.size()];
        twiceArea += a.x() * b.y() - b.x() * a.y();
    }
    const double winding = twiceArea >= 0.0 ? 1.0 : -1.0;

    planes.reserve(poly.size());
    for (int i = 0; i < poly.size(); ++i) {
        const QPointF &a = poly[i];
        const QPointF &b = poly[(i + 1) % poly.size()];
        const double ex = b.x() - a.x(), ey = b.y() - a.y();
        const double len = std::hypot(ex, ey);
        if (len <= 0.0) continue;   // a closed polygon repeats its first point
        // Inside is winding * cross(edge, p - a) >= 0, rearranged into n·p >= d.
        const double nx = -winding * ey / len;
        const double ny =  winding * ex / len;
        planes.push_back({nx, ny, nx * a.x() + ny * a.y()});
    }
    return planes;
}

// Push `p` into the intersection of the half-planes by repeatedly stepping it
// back across whichever one it breaks (cyclic projection). Converges as long as
// the intersection is non-empty, which every caller guarantees by starting from
// a position that already fits. Pinned axes are held fixed, so a constraint only
// the pinned axis could satisfy is simply skipped.
QPointF projectInto(QPointF p, const std::vector<HalfPlane> &planes, bool freeX, bool freeY) {
    if (planes.empty() || (!freeX && !freeY)) return p;

    constexpr int    kSweeps = 64;
    constexpr double kSlack  = 1e-7;
    // Land a hair *inside* rather than exactly on the edge, so the result still
    // reads as contained after the round-off a projection leaves behind.
    constexpr double kMargin = 1e-4;

    for (int sweep = 0; sweep < kSweeps; ++sweep) {
        double worst = 0.0;
        for (const HalfPlane &h : planes) {
            const double violation = h.d - (h.nx * p.x() + h.ny * p.y());
            if (violation <= kSlack) continue;
            const double gx = freeX ? h.nx : 0.0;
            const double gy = freeY ? h.ny : 0.0;
            const double gg = gx * gx + gy * gy;
            if (gg < 1e-12) continue;   // nothing the free axes can do about it
            p += QPointF(gx, gy) * ((violation + kMargin) / gg);
            worst = std::max(worst, violation);
        }
        if (worst <= kSlack) break;
    }
    return p;
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

QRectF slideInside(const QRectF &rect, const QPointF &delta, const QPolygonF &poly) {
    if (rect.isEmpty()) return rect;
    const std::vector<HalfPlane> edges = insideHalfPlanes(poly);
    if (edges.empty()) return rect.translated(delta);

    const QPointF corners[4] = {rect.topLeft(), rect.topRight(),
                                rect.bottomRight(), rect.bottomLeft()};

    // Translating the whole rectangle collapses each edge's four corner
    // constraints into one constraint on the offset — set by whichever corner
    // leads into that edge. The rectangle's size never enters the solution, so
    // it cannot change.
    std::vector<HalfPlane> onOffset;
    onOffset.reserve(edges.size());
    for (const HalfPlane &h : edges) {
        double nearest = std::numeric_limits<double>::max();
        for (const QPointF &c : corners)
            nearest = std::min(nearest, h.nx * c.x() + h.ny * c.y());
        onOffset.push_back({h.nx, h.ny, h.d - nearest});
    }

    return rect.translated(projectInto(delta, onOffset, true, true));
}

QRectF resizeInside(const QPointF &anchor, const QPointF &startCorner,
                    const QPointF &desiredCorner, bool freeX, bool freeY,
                    const QPolygonF &poly) {
    // A pinned axis keeps the side it controls exactly where the drag began.
    QPointF corner(freeX ? desiredCorner.x() : startCorner.x(),
                   freeY ? desiredCorner.y() : startCorner.y());

    const std::vector<HalfPlane> edges = insideHalfPlanes(poly);
    if (edges.empty()) return QRectF(anchor, corner).normalized();

    // The anchor is fixed, so the rectangle is determined by the moving corner
    // alone. Of its four corners, the anchor is already inside; the other three
    // give one constraint each — two of them on a single axis, since they share
    // a coordinate with the anchor.
    std::vector<HalfPlane> onCorner;
    onCorner.reserve(edges.size() * 3 + 2);
    for (const HalfPlane &h : edges) {
        onCorner.push_back(h);                                        // the moving corner
        onCorner.push_back({h.nx, 0.0, h.d - h.ny * anchor.y()});      // (corner.x, anchor.y)
        onCorner.push_back({0.0, h.ny, h.d - h.nx * anchor.x()});      // (anchor.x, corner.y)
    }

    // Keep the rectangle from collapsing through — or turning inside out at —
    // its anchor.
    constexpr double kMinSide = 1.0;
    if (freeX) {
        const double side = startCorner.x() >= anchor.x() ? 1.0 : -1.0;
        onCorner.push_back({side, 0.0, side * anchor.x() + kMinSide});
    }
    if (freeY) {
        const double side = startCorner.y() >= anchor.y() ? 1.0 : -1.0;
        onCorner.push_back({0.0, side, side * anchor.y() + kMinSide});
    }

    corner = projectInto(corner, onCorner, freeX, freeY);
    return QRectF(anchor, corner).normalized();
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
