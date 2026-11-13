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
    NumericalFailure, WireNotCertified, TolerantClosure
};
struct Diagnostic {
    Code code;
    QString message;
    QStringList entityIds;
    std::optional<Sketch::Point> location;
};
struct Curve {
    QString entityId;
    std::variant<Sketch::Segment,Sketch::Circle,Sketch::Arc,Sketch::Ellipse,Sketch::EllipticalArc,Sketch::Spline> geometry;
    bool reversed=false; // Traversal relative to the referenced source entity.
};
enum class Orientation { Clockwise, Counterclockwise };
struct Contour {
    std::vector<Curve> curves;
    long double signedArea=0;
    Orientation orientation=Orientation::Counterclockwise;
    std::optional<size_t> parent;
    size_t depth=0; // Even = material, odd = hole.
    double maximumEndpointGap=0;
};
struct Region {
    size_t outer; // Indices into Result::contours; islands have their own Region.
    std::vector<size_t> holes; // Direct children only.
};
enum class WireReadiness { NotCertified };
struct Result {
    Status status=Status::Invalid;
    // Populated only when the entire input is valid. Segments are ordered and
    // reversed as needed; exact source coordinates are never snapped/repaired.
    std::vector<Contour> contours;
    std::vector<Diagnostic> diagnostics;
    std::vector<Region> regions;
    // Topological validation never certifies an OCCT wire. Future adapters must
    // check exact closure, tolerances and B-Rep validity without implicit healing.
    WireReadiness wireReadiness=WireReadiness::NotCertified;
    std::vector<Diagnostic> constructionDiagnostics;
    double connectionTolerance=0; // Recorded only for a successful analysis.
};
// Same model-space tolerance as Sketch snapping/Overdraw, independent of zoom.
// Includes hidden/locked Profile geometry. Supports simple Segment/Arc contours,
// full circles and even-odd nesting. Touching boundaries and conics are refused.
// Numerically unresolvable coordinates return Unsupported; no auto-healing.
Result validate(const std::vector<Sketch::Entity>& entities,const QString& workplaneId,
                double tolerance=1.e-7);
}
