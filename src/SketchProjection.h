#pragma once
#include "SketchGeometry.h"
#include <gp_Trsf.hxx>
#include <cstddef>

// Analytic computation only: no widgets, AIS, selection state or document writes.
namespace SketchProjection {
struct Source {
    // A Part-local vertex/edge/face, including accumulated OCCT subshape locations.
    // Do NOT bake partPlacement into shape as well. For a world-space shape pass
    // identity placement. OCCT locations are consumed by BRep_Tool/Adaptor once;
    // the separate outer Part placement is then applied once to their values.
    TopoDS_Shape shape;
    gp_Trsf partPlacement;
    QString label; // Optional transient diagnostic label; never persisted.
};
enum class Code {
    InvalidWorkplane, InvalidTolerance, InvalidExistingGeometry,
    NullSource, UnsupportedShape, InvalidPlacement, InvalidOrientation,
    MissingCurve, NonlinearEdge, UnboundedEdge, DegenerateEdge,
    InvalidBRep, CollapsedProjection, NumericalFailure, KernelFailure,
    Duplicate, PartialOverlap, NonCircularProjection, UnsupportedCircularSpan,
    InvalidFaceBoundary, DegenerateSkipped, UnsupportedConicComparison
};
struct Diagnostic {
    Code code;
    QString message;
    std::optional<size_t> sourceIndex; // Zero based; absent for request errors.
    QString sourceLabel;
    std::optional<size_t> relatedSourceIndex;
    QString existingEntityId;
    bool isError() const {return code!=Code::Duplicate && code!=Code::DegenerateSkipped;}
};
struct Result {
    bool success=false;
    // New independent values only. Append to the unchanged existing collection
    // for one future SketchChange. On ANY failure this vector is empty.
    std::vector<Sketch::Entity> additions;
    std::vector<Diagnostic> diagnostics;
    bool changed() const {return success && !additions.empty();}
};
// Deduplicate against accepted batch values and existing Construction geometry
// on this Workplane, including hidden/locked entities. Profile geometry and
// other owners are not duplicate Construction references. Point/curve contacts
// and crossings are allowed; nonzero collinear/cocircular overlaps are refused.
// Parallel circular edges retain Circle/Arc. Oblique circles and analytic
// ellipses use exact Ellipse/EllipticalArc coefficients and signed phase mapping.
// Rank loss/unstable conversion fails; no tessellation/fitting. Until 6A.3b,
// ellipse-vs-conic comparisons are refused, including within a batch; existing
// target Construction ellipses retain the conservative request guard.
// Elliptical face boundaries remain explicitly unsupported in this phase.
// Faces expand all actual outer/inner wires into those same analytic curves.
// Validate complete closed wire walks before deduplication; no endpoint points,
// silhouette or contour reconstruction. Proven spherical/conical pole edges
// may be skipped with a non-error diagnostic. Other degenerate edges fail.
// Empty or fully duplicate requests succeed with no additions. New IDs are
// allocated once for the returned values, not derived from source topology.
Result project(const std::vector<Source>& sources,const Construction::Object& workplane,
               const std::vector<Sketch::Entity>& existing={},double tolerance=1.e-7);
}
