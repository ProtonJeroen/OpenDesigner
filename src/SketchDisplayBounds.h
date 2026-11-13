#pragma once
#include "SketchGeometry.h"

// Derived presentation data only: never persisted and never a Sketch constraint.
namespace Sketch {
struct DisplayRectangle {
    double minX=0,minY=0,maxX=0,maxY=0;
    bool operator==(const DisplayRectangle&) const = default;
};
// Presentation only: an exact 10 mm ring (15 mm before v0.5.3) is added
// outside the Sketch bounds. Keeping it separate retains 6B.1's analytic
// bounds and snapping semantics.
inline constexpr double WorkplaneBorderWidth = 10.0;
// v0.5.3: the free opening spans at least 100 x 100 mm in Workplane-local
// coordinates, centered on the content-derived rectangle.
inline constexpr double WorkplaneMinimumOpening = 100.0;
DisplayRectangle borderedDisplayRectangle(const DisplayRectangle& inner);
std::optional<DisplayRectangle> finiteBounds(const Geometry& geometry);
// Includes hidden/locked entities of both roles, but only on this Workplane.
// Empty owners use their legacy extent/offset as a base. Outside infinite lines
// extend it by their perpendicular offset, independently of stored line origins.
DisplayRectangle displayRectangle(const Construction::Object& owner,const std::vector<Entity>& entities);
// The existing padded rectangle is the free opening; the fixed model-space
// border is added outside it. Neither rectangle is document geometry.
DisplayRectangle displayOuterRectangle(const Construction::Object& owner,const std::vector<Entity>& entities);
TopoDS_Shape workplaneBorder(const DisplayRectangle& opening,const gp_Ax3& frame);
std::array<gp_Pnt,4> displayCorners(const DisplayRectangle& rectangle,const gp_Ax3& frame);
// Analytic, display-only clipping. No intersection (including point-only
// corner contact) has no drawable segment; the source stays infinite.
std::optional<Segment> clipLine(const InfiniteLine& line,const DisplayRectangle& rectangle);
TopoDS_Shape displayShape(const Entity& entity,const gp_Ax3& frame,const DisplayRectangle& rectangle);
}
