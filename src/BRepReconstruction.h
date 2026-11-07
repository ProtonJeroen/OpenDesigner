#pragma once
#include <QString>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <vector>

// Geometry-only preparation and acceptance policy shared by all face commands.
namespace BRepReconstruction {
struct FaceMapping { TopoDS_Face original; std::vector<TopoDS_Face> modified; };
struct Prepared {
    TopoDS_Shape solid;
    TopoDS_Face target;
    std::vector<FaceMapping> history;
};
// Final support correspondence: an empty modified list means a certified
// disappearance; several faces may represent disconnected patches of a support.
using Result = Prepared;
// Geometry-only local analysis. Patch expansion follows shared edges on the
// same analytic support; neighbors and rim edges come from actual topology.
// history maps the caller's source faces to the isolated copy, not feature history.
struct Neighborhood : Prepared {
    std::vector<TopoDS_Face> patch, neighbors;
    std::vector<TopoDS_Edge> boundary;
};
bool neighborhood(const TopoDS_Shape& source, const TopoDS_Face& target,
                  Neighborhood& result, QString& error);
// Caller certifies material semantics INSIDE editRegion. This common finish
// checks the outside independently and preserves unaffected trimmed face regions.
// affected/moved contain faces of Neighborhood::solid; moved is a subset of affected.
bool finishLocal(const Neighborhood& source, const TopoDS_Shape& candidate,
                 const TopoDS_Shape& editRegion, const std::vector<TopoDS_Face>& affected,
                 const std::vector<TopoDS_Face>& moved, const gp_Trsf& displacement,
                 Result& result, QString& error);
// Constant-section planar motion along invariant adjacent supports. No stock
// reconstruction: sweep the current trimmed face and certify the local change.
bool movePlanarLocal(const TopoDS_Shape& source, const TopoDS_Face& target,
                     double distance, Result& result, QString& error);
bool finish(const TopoDS_Shape& source, const TopoDS_Shape& candidate,
            const TopoDS_Face& target, const gp_Pln& expected, Result& result, QString& error);
bool rebuildPlanar(const TopoDS_Shape& source, const TopoDS_Face& target,
                   const gp_Pln& expected, Result& result, QString& error);
bool equivalent(const TopoDS_Shape& a, const TopoDS_Shape& b);
bool prepare(const TopoDS_Shape& source, const TopoDS_Face& target,
             Prepared& result, QString& error);
bool validSolid(const TopoDS_Shape& shape, double& volume);
// Common final cleanup; callers still enforce their operation's material policy.
// Local reconstruction may retain certified split faces if optional same-domain
// merging fails. The unmerged result must pass the SAME solid/material checks.
bool normalize(const TopoDS_Shape& candidate, TopoDS_Shape& result, QString& error,
               bool allowValidSplits = false);
bool sameAnalyticSupport(const TopoDS_Face& a, const TopoDS_Face& b);
bool supports(const TopoDS_Shape& source, const TopoDS_Shape& result,
              const TopoDS_Face& target, const gp_Pln& expected, QString& error);
}
