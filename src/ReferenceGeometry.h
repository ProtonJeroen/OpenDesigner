#pragma once
#include "ConstructionGeometry.h"
#include "SketchGeometry.h"
#include "SketchSnapping.h"
#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Trsf.hxx>
#include <optional>
#include <vector>

// v0.5.3 unified geometric references. Pure values: no widgets, AIS or history.
// A Target identifies what was picked; resolve() evaluates it against the
// current document values. Nothing is ever rebound to other geometry: a
// missing, hidden, locked or topologically replaced source is refused.
// See docs/WORKPLANE_REFERENCES.md.
namespace References {
enum class Kind { Part, Face, Edge, Vertex, Axis, Workplane, Point, SketchLine, SketchPoint };
struct Target {
    Kind kind = Kind::Point;
    QString ownerId;               // Part persistent id, construction id or Sketch entity id
    TopoDS_Shape subshape;         // Face/Edge/Vertex of the Part's canonical local B-Rep
    Sketch::PointReference point;  // SketchPoint only (point.entity == ownerId)
};
struct PartSource {
    QString id, name;
    TopoDS_Shape shape;            // canonical local B-Rep
    gp_Trsf placement;             // local -> world
    bool visible = true;
};
struct Model {
    std::vector<PartSource> parts;
    std::vector<Construction::Object> constructions;
    std::vector<Sketch::Entity> sketches;
};
struct Resolved {
    Kind kind = Kind::Point;
    std::optional<gp_Pnt> point;   // world position (centroid, midpoint, center, ...)
    std::optional<gp_Ax1> line;    // world line with a signed direction
    std::optional<gp_Ax3> plane;   // world plane; Direction() is the oriented normal
    QString label;
};
QString kindName(Kind kind);
bool resolve(const Model& model, const Target& target, Resolved& result, QString& error);
// Direction supplied by one reference: line direction, else plane normal.
bool direction(const Resolved& reference, gp_Dir& result, QString& error);
// Signed unit direction and length from the first to the second point.
bool pointToPoint(const Resolved& from, const Resolved& to, gp_Dir& result, double& length, QString& error);
bool sameTarget(const Target& a, const Target& b);
}
