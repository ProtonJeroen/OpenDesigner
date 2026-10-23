#pragma once
#include "SketchGeometry.h"
#include <functional>
#include <optional>

namespace Sketch {
enum class IntersectionKind { None, Finite, Coincident };
struct Intersections { IntersectionKind kind=IntersectionKind::None; std::vector<Point> points; };
// Linear model tolerance is independent of the pixel-based catch radius.
bool contains(const Geometry& curve, Point point, double tolerance=1.e-7);
Intersections intersect(const Geometry& a, const Geometry& b, double tolerance=1.e-7);
std::vector<Point> tangentPoints(Point anchor, const Geometry& curve, double tolerance=1.e-7);
std::optional<Point> nearestPoint(const Geometry& curve, Point point);
enum class SnapKind { Endpoint, Midpoint, Center, Intersection, Tangent, Nearest, ConstructionPoint };
struct SnapTarget { Point point; SnapKind kind; QString label; };
std::vector<SnapTarget> snapTargets(const std::vector<Entity>& entities, const QString& workplaneId,
                                  Point cursor, std::optional<Point> anchor = {}, bool intersections = true);
using Projector = std::function<Point(Point)>;
std::optional<SnapTarget> closestSnap(const std::vector<SnapTarget>& targets, Point screenCursor,
                                    double pixelRadius, const Projector& project);
}
