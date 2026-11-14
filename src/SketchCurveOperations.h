#pragma once
#include "SketchGeometry.h"
#include <Geom2d_Curve.hxx>

// Evaluated Workplane-local geometry only. No document mutation or UI state.
namespace SketchCurveOperations {
enum class Status { Success, Invalid, Unsupported, Overlap, Ambiguous, NumericalFailure };
enum class Contact { Crossing, Tangency };
struct Hit {
    Sketch::Point point;
    double firstParameter=0,secondParameter=0; // Original support parameters.
    Contact contact=Contact::Crossing;
};
struct Intersections {
    Status status=Status::Invalid;
    std::vector<Hit> hits; // Sorted in first curve traversal order, then second.
    QString message;
};
// Low-level adapter extracted from Profile Validation. Unsupported types return
// null; callers validate values and catch kernel/standard exceptions. Never re-interpolates
// a spline. The trim is ascending; traversal is retained separately by operations.
Handle(Geom2d_Curve) boundedCurve(const Sketch::Geometry& geometry);
Intersections intersect(const Sketch::Geometry& first,const Sketch::Geometry& second,double tolerance=1.e-7);

struct Interval {
    // Directed original support parameters. A periodic wrap may pass beyond
    // the support's canonical domain. These are values, not new fitted curves.
    double start=0,end=0;
};
struct SegmentSelection {
    Status status=Status::Invalid;
    Interval interval;
    double pickedParameter=0;
    QString message;
};
// Cuts must be certified support parameters (e.g. from intersect). Finite
// endpoints are included automatically. Equal-distance pick solutions, picks
// at cuts, and unresolved intervals fail atomically. Cursor need not be on curve.
// Infinite lines can intersect but cannot be classified by this finite API.
SegmentSelection classifySegment(const Sketch::Geometry& geometry,const std::vector<double>& cuts,
                                Sketch::Point cursor,double tolerance=1.e-7);

struct TrimResult {
    Status status=Status::Invalid;
    std::vector<Sketch::Geometry> remaining; // Original directed order; no fitted fragments.
    Interval removed;
    QString message;
};
// One finite source, immutable boundary values on the same Workplane. Crossings
// bound removals; tangencies do not. Open tails may end at an original endpoint;
// closed curves require at least two distinct crossings. Any failure is atomic.
TrimResult trim(const Sketch::Geometry& source,const std::vector<Sketch::Geometry>& boundaries,
                Sketch::Point cursor,double tolerance=1.e-7);

enum class Endpoint { Start, End };
struct ExtensionEnds {
    Status status=Status::Invalid;
    Sketch::Point start,end;
    QString message;
};
// Only finite open supports with a certified continuation are eligible.
ExtensionEnds extensionEnds(const Sketch::Geometry& source,double tolerance=1.e-7);
struct ExtendResult {
    Status status=Status::Invalid;
    std::optional<Sketch::Geometry> geometry; // Empty on every refusal.
    double targetParameter=0; // On the original support, including periodic aliases.
    QString message;
};
// One explicitly selected endpoint and boundary. Only one beyond-end crossing
// is accepted; multiple candidates are refused, never ranked by distance.
// Coefficient B-splines may only reveal more of their existing knot domain.
ExtendResult extend(const Sketch::Geometry& source,Endpoint endpoint,const Sketch::Geometry& boundary,
                    double tolerance=1.e-7);

struct SupportRelation {
    Status status=Status::Invalid;
    bool common=false,firstContainsSecond=false,secondContainsFirst=false,overlap=false;
    std::vector<Sketch::Point> boundaries; // Finite ends on the certified common support.
    QString message;
};
// Exact analytic supports or identical B-spline coefficients (also certified
// coefficient reversal). No sampled equivalence or fitted parameter mapping.
SupportRelation supportRelation(const Sketch::Geometry& first,const Sketch::Geometry& second,double tolerance=1.e-7);
struct CurvePiece {
    Sketch::Geometry geometry;
    Interval parameters;
    Sketch::Point start,end;
};
struct SplitResult {
    Status status=Status::Invalid;
    std::vector<CurvePiece> pieces;
    QString notice,message;
};
// Geometry-only finite pieces for the existing Overdraw graph. Intrinsic ends
// bound open curves; full closed curves need two distinct supplied boundaries.
SplitResult split(const Sketch::Geometry& source,const std::vector<Sketch::Point>& boundaries,double tolerance=1.e-7);
Sketch::Geometry reversed(const Sketch::Geometry& geometry);
}
