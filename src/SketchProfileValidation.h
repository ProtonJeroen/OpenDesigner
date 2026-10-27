#pragma once
#include "SketchGeometry.h"

// Evaluated local XY topology only: no widgets, presentations, persistence or
// changes to source entities. Construction and other Workplanes are ignored.
namespace SketchProfileValidation {
enum class Status { Valid, Invalid, Unsupported };
enum class Code {
    InvalidInput, EmptyProfile, InvalidGeometry, DuplicateIdentity, DegenerateCurve,
    DuplicateCurve, Overlap, OpenContour, Branch, SelfIntersection,
    AmbiguousConnection, UnsupportedCurve, ContourIntersection, NestedContours,
    NumericalFailure
};
struct Diagnostic {
    Code code;
    QString message;
    QStringList entityIds;
    std::optional<Sketch::Point> location;
};
struct Curve {
    QString entityId;
    std::variant<Sketch::Segment,Sketch::Circle> geometry;
};
struct Contour { std::vector<Curve> curves; };
struct Result {
    Status status=Status::Invalid;
    // Populated only when the entire input is valid. Segments are ordered and
    // reversed as needed; exact source coordinates are never snapped/repaired.
    std::vector<Contour> contours;
    std::vector<Diagnostic> diagnostics;
};
// Same model-space tolerance as Sketch snapping/Overdraw, independent of zoom.
// Includes hidden/locked Profile geometry. Supports disjoint simple polygons
// and full circles; nesting/holes, touching circles and other curves are gated.
// Numerically unresolvable coordinates return Unsupported; no auto-healing.
Result validate(const std::vector<Sketch::Entity>& entities,const QString& workplaneId,
                double tolerance=1.e-7);
}
