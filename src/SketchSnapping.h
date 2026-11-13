#pragma once
#include "SketchGeometry.h"
#include <functional>
#include <optional>

namespace Sketch {
enum class IntersectionKind { None, Finite, Coincident, Unsupported, NumericalFailure };
struct Intersections { IntersectionKind kind=IntersectionKind::None; std::vector<Point> points; };
// Linear model tolerance is independent of the pixel-based catch radius.
// Elliptic centers/endpoints/nearest and line-conic intersections are supported.
// Conic-conic intersections report Unsupported; elliptic tangent snaps remain
// unavailable. Curve construction/Overdraw eligibility is a separate predicate.
bool contains(const Geometry& curve, Point point, double tolerance=1.e-7);
Intersections intersect(const Geometry& a, const Geometry& b, double tolerance=1.e-7);
std::vector<Point> tangentPoints(Point anchor, const Geometry& curve, double tolerance=1.e-7);
std::optional<Point> nearestPoint(const Geometry& curve, Point point);
enum class SnapKind { Endpoint, Midpoint, Center, Intersection, Tangent, Nearest, ConstructionPoint };
// Identity of a cursor-independent Sketch point (v0.5.3). It is re-evaluated
// from the current entities by resolvePoint, the same evaluation snapTargets
// uses, so a snapped point and a picked reference always coincide.
enum class PointFeature { Position, Start, End, Midpoint, Center, Intersection };
struct PointReference {
    QString entity, other; // other: second curve of an Intersection
    PointFeature feature = PointFeature::Position;
    int index = 0;         // Intersection index in intersect(entity, other)
    bool operator==(const PointReference&) const = default;
};
struct SnapTarget { Point point; SnapKind kind; QString label; std::optional<PointReference> reference = {}; };
// Only visible, selectable entities on any Workplane supply points; stale,
// hidden or changed-topology references are refused with a diagnostic.
std::optional<Point> resolvePoint(const std::vector<Entity>& entities, const PointReference& reference, QString& error);
std::vector<SnapTarget> snapTargets(const std::vector<Entity>& entities, const QString& workplaneId,
                                  Point cursor, std::optional<Point> anchor = {}, bool intersections = true);
using Projector = std::function<Point(Point)>;
std::optional<SnapTarget> closestSnap(const std::vector<SnapTarget>& targets, Point screenCursor,
                                    double pixelRadius, const Projector& project);
}
