#pragma once
#include <QPolygonF>
#include <QRectF>
#include <QSize>
#include <QSizeF>

// ---------------------------------------------------------------------------
// RotateGeometry — the geometry behind free-angle rotation, shared by the
// `RotateEdit` that produces the rotated buffer and the crop/rotate overlay that
// draws the selection box on top of it.
//
// Rotating an image by an arbitrary angle grows it to the axis-aligned bounding
// box of the tilted original and leaves blank triangles in the corners. Every
// helper here works in the coordinate space of that bounding box (origin at its
// top-left), which is also the space the crop selection lives in, so a crop can
// be kept strictly inside the tilted original and the result stays a full,
// rectangular photograph.
// ---------------------------------------------------------------------------
namespace RotateGeometry {

// The free rotation is limited to a straightening range; whole quarter turns are
// the lossless `OrientationEdit`'s job, and together the two reach any angle.
constexpr double kMaxAngle = 45.0;

// Angles this small are treated as no rotation at all.
bool isZeroAngle(double degrees);

// Clamp to the straightening range and drop floating-point dust.
double clampAngle(double degrees);

// Size of the axis-aligned bounding box a `size` image occupies once rotated —
// the size of the buffer `RotateEdit::apply()` returns.
QSizeF boundingSize(const QSize &size, double degrees);

// The rotated original's four corners, in bounding-box coordinates. A convex
// quadrilateral; everything outside it is blank corner filler.
QPolygonF rotatedBounds(const QSize &size, double degrees);

// True when every corner of `rect` lies inside (or on the edge of) the convex
// polygon `poly`.
bool contains(const QPolygonF &poly, const QRectF &rect);

// The largest axis-aligned rectangle that fits inside the rotated original,
// centred on it. This is the crop applied automatically when a rotation would
// otherwise expose blank corners — the maximum image still fully covered.
QRectF largestInscribedRect(const QSize &size, double degrees);

// Shrink `rect` about its own centre, keeping its aspect ratio, until it fits
// inside `poly`. Returns it unchanged when it already fits. A rect whose centre
// is outside the polygon is re-centred on the polygon first.
//
// This is for *re-fitting* a selection the tilt has outgrown — when the angle
// changes underneath it. It is the wrong tool for a drag, where the user is
// moving specific edges and expects the rest to hold still; use the two
// functions below for that.
QRectF shrinkToFit(const QRectF &rect, const QPolygonF &poly);

// Move `rect` by `delta` without ever resizing it, stopping at the tilted
// bounds. Against a diagonal edge the rectangle slides along it and simply goes
// no further outward — exactly how it behaves against the sides of an upright
// image (where `poly` is that image's own rectangle).
QRectF slideInside(const QRectF &rect, const QPointF &delta, const QPolygonF &poly);

// Resize by dragging one corner from `startCorner` toward `desiredCorner` while
// `anchor` — the opposite corner — stays put, held inside the tilted bounds.
// Only the two sides meeting the dragged corner ever move. Pinning an axis
// (`freeX` / `freeY` false) turns this into an edge drag, moving one side alone.
QRectF resizeInside(const QPointF &anchor, const QPointF &startCorner,
                    const QPointF &desiredCorner, bool freeX, bool freeY,
                    const QPolygonF &poly);

// Carry a selection from the bounding-box space of one angle into another's:
// the rect keeps its size, and its offset from the image centre turns with the
// image. The result is shrunk to fit the new tilted bounds.
QRectF remapBetweenAngles(const QRectF &rect, const QSize &size,
                          double fromDegrees, double toDegrees);

}  // namespace RotateGeometry
