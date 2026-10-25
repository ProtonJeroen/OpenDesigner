#pragma once
#include "SketchGeometry.h"

// Derived presentation data only: never persisted and never a Sketch constraint.
namespace Sketch {
struct DisplayRectangle {
    double minX=0,minY=0,maxX=0,maxY=0;
    bool operator==(const DisplayRectangle&) const = default;
};
std::optional<DisplayRectangle> finiteBounds(const Geometry& geometry);
// Includes hidden/locked entities of both roles, but only on this Workplane.
// Empty owners use their legacy extent/offset as a base. Outside infinite lines
// extend it by their perpendicular offset, independently of stored line origins.
DisplayRectangle displayRectangle(const Construction::Object& owner,const std::vector<Entity>& entities);
std::array<gp_Pnt,4> displayCorners(const DisplayRectangle& rectangle,const gp_Ax3& frame);
// Analytic, display-only clipping. No intersection (including point-only
// corner contact) has no drawable segment; the source stays infinite.
std::optional<Segment> clipLine(const InfiniteLine& line,const DisplayRectangle& rectangle);
TopoDS_Shape displayShape(const Entity& entity,const gp_Ax3& frame,const DisplayRectangle& rectangle);
}
