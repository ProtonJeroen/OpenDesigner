#pragma once
#include "BRepReconstruction.h"
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Ax1.hxx>
#include <gp_Vec.hxx>
#include <string>

// No viewport, Part, creation metadata or history dependencies. Coordinates are
// in the input B-Rep frame (including its TopLoc_Location).
namespace CylindricalMoveGeometry {
enum class Status { Success, Unsupported, InvalidInput, Failure };
enum class BossBoundaryMode { Trim, Extend };
enum class Code {
    Moved, Unchanged, HoleExited, InvalidDisplacement, InvalidSource,
    UnsupportedFeature, UnsupportedStock, AmbiguousMaterial, InvalidResult, KernelFailure,
    InvalidPolicy, DisconnectedBoss, InteractingFeatures
};
struct Result {
    Status status = Status::Failure;
    Code code = Code::KernelFailure;
    std::string message;
    TopoDS_Shape shape; // null on any refusal
    gp_Ax1 axis;       // translated axis, even when the hole has exited
    double radius = 0, height = 0;
    explicit operator bool() const { return status == Status::Success; }
};
// Analytic cylindrical identity is independent of creation, stock shape, rim
// curve types and adjacent plane orientation. Reconstruction is certified from
// current adjacency and material, with specific diagnostics when unavailable.
// Local hole reconstruction clips the cylinder against actual planar rim
// supports, including oblique/elliptical and certified open trims. Independent
// material and unaffected face checks protect unrelated features; no cylinder
// count or stock pattern is required. Boundary crossings/complete exit can be
// accepted when certified. Does not mutate source, including on refusal.
Result moveThroughHole(const TopoDS_Shape& source, const TopoDS_Face& mantle,
                       const gp_Vec& displacement);
// A complete, trimmed or overhanging boss on a certified planar support.
// The boundary fallback supports repeated moves across one planar side using
// the current healed support; corners and interacting features are refused.
// The local interior path
// preserves unrelated geometry without a rectangular-stock/cylinder-count rule.
// Existing boundary paths preserve independent parallel cylindrical features.
// Trim clips against certified lateral support boundaries; the local fallback
// uses the healed current planar support. Extend retains the full cylinder, including a connected
// overhang. No invented bridges or support pedestals. Policy is always explicit.
Result moveBoss(const TopoDS_Shape& source, const TopoDS_Face& mantle,
                const gp_Vec& displacement, BossBoundaryMode policy);
// Whole-feature deletion from one current notch/boundary-boss wall. Reuses the
// same material certificates as movement; includes connected split mantles and
// boss cap/underside regions. Returns immutable source-to-result correspondence.
bool removeBoundaryFeature(const TopoDS_Shape& source,const TopoDS_Face& mantle,
                           BRepReconstruction::Result& result,QString& error);
// Radial Pull on current geometry: cylindrical holes bounded by certified
// planar rims (including oblique/open trims), and current full or planar-trimmed
// bosses with a certified connected attachment, including overhangs. Actual
// cap/cut supports stay fixed; a vanished historical Trim plane is never inferred.
// No offset-kernel fallback; unsupported neighborhoods return diagnostics.
Result resizeRadius(const TopoDS_Shape& source, const TopoDS_Face& mantle,
                    double radius);
// Planar boss-cap height handles derived from current wall, attachment and cap.
// Height reconstruction reuses the certified local planar sweep, without offset.
Result bossHeightReference(const TopoDS_Shape& source,const TopoDS_Face& cap);
Result resizeBossHeight(const TopoDS_Shape& source,const TopoDS_Face& cap,double height);
// Certified paths before global offset: preserve exterior features during a
// side-plane edit, or reconstruct an outward planar cut through one boss.
// Uses actual rim/cap planes and material, not primitive history or a stock crop.
Result movePlanarPreservingExterior(const TopoDS_Shape& source,
                                    const TopoDS_Face& plane, double distance);
}
