#pragma once
#include "SketchSnapping.h"

namespace Sketch {
enum class ConstructionTool { OffsetLine, CommonTangent };
enum class LineOutput { Infinite, Finite };
// Return only missing axes; all existing lines count, including hidden/locked ones.
std::vector<Entity> missingConstructionAxes(const std::vector<Entity>& entities,const QString& owner,
                                             bool localX,bool localY);
// Parameter measured along the infinite source's unit direction from its origin.
struct LineInterval { double start=0, length=1; };
struct CommonTangent {
    Point first, second, direction;
    bool internal=false;
};
bool straightReference(const Geometry& geometry);
bool circularReference(const Geometry& geometry);
// Positive offset is left of the source direction in Workplane XY.
bool offsetConstructionLine(const Geometry& source,double distance,LineOutput output,
                            std::optional<LineInterval> interval,Geometry& result,QString& error,
                            double tolerance=1.e-7);
bool commonTangents(const Geometry& first,const Geometry& second,std::vector<CommonTangent>& result,
                    QString& error,double tolerance=1.e-7);
// A tangent at a shared contact is unique as an infinite line, but has no finite span.
bool commonTangentLine(const CommonTangent& tangent,LineOutput output,Geometry& result,
                       QString& error,double tolerance=1.e-7);
}
