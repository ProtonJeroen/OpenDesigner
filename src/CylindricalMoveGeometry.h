#pragma once
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
// One selected complete circular through-hole normal to rectangular stock.
// Independent parallel holes/bosses in the same Part are preserved. Boundary
// crossings and complete exit are allowed; interacting, disconnected or
// nonmanifold results are refused. Does not mutate source, even on failure.
// Separated exterior bosses, including unsupported truncated/overhanging source
// bosses, can remain protected while the selected complete stock hole is moved.
// A local rim/material certificate also handles nonrectangular neighborhoods;
// unrelated faces and material must survive independently of any stock pattern.
Result moveThroughHole(const TopoDS_Shape& source, const TopoDS_Face& mantle,
                       const gp_Vec& displacement);
// One complete boss on a certified planar support. The local interior path
// preserves unrelated geometry without a rectangular-stock/cylinder-count rule.
// Existing boundary paths preserve independent parallel cylindrical features.
// Trim clips against the original lateral
// stock supports; Extend retains the full cylinder, including a connected
// overhang. No invented bridges or support pedestals. Policy is always explicit.
Result moveBoss(const TopoDS_Shape& source, const TopoDS_Face& mantle,
                const gp_Vec& displacement, BossBoundaryMode policy);
// Certified paths before global offset: preserve exterior features during a
// side-plane edit, or reconstruct an outward planar cut through one boss.
// Uses actual rim/cap planes and material, not primitive history or a stock crop.
Result movePlanarPreservingExterior(const TopoDS_Shape& source,
                                    const TopoDS_Face& plane, double distance);
}
