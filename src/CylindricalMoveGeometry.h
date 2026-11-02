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
    InvalidPolicy, DisconnectedBoss
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
// Initially supports one complete circular through-hole normal to a rectangular
// stock face. Boundary crossings and complete exit are allowed; disconnected or
// nonmanifold results are refused. Does not mutate source, even on failure.
Result moveThroughHole(const TopoDS_Shape& source, const TopoDS_Face& mantle,
                       const gp_Vec& displacement);
// Complete boss on rectangular stock. Trim clips against the original lateral
// stock supports; Extend retains the full cylinder, including a connected
// overhang. No invented bridges or support pedestals. Policy is always explicit.
Result moveBoss(const TopoDS_Shape& source, const TopoDS_Face& mantle,
                const gp_Vec& displacement, BossBoundaryMode policy);
}
