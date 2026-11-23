#pragma once
#include <QString>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <gp_Dir.hxx>
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
    // Full source validation on this isolated immutable graph only. Zero in a
    // refusal-only preflight. Never a reusable certificate for another shape.
    double certifiedVolume = 0;
    std::vector<TopoDS_Face> patch, neighbors;
    std::vector<TopoDS_Edge> boundary;
};
bool neighborhood(const TopoDS_Shape& source, const TopoDS_Face& target,
                  Neighborhood& result, QString& error);
// Refusal-only Move preflight. No volume/self-interference/material acceptance.
// A successful result MUST still enter the normal source and result certificates.
bool preflightPlanarSpline(const TopoDS_Shape& source, const TopoDS_Face& target,
                          bool& spline, QString& error);
// Caller certifies material semantics INSIDE editRegion. This common finish
// checks the outside independently and preserves unaffected trimmed face regions.
// affected/moved contain faces of Neighborhood::solid; moved is a subset of affected.
bool finishLocal(const Neighborhood& source, const TopoDS_Shape& candidate,
                 const TopoDS_Shape& editRegion, const std::vector<TopoDS_Face>& affected,
                 const std::vector<TopoDS_Face>& moved, const gp_Trsf& displacement,
                 Result& result, QString& error, bool allowSpline = false,
                 const gp_Pln* stackedCylinderSeamPlane = nullptr);
// F2.9E.3 shared planar sweep stages, used by movePlanarLocal in this order:
// planPlanarSweep, value refusals, sweepPlanarRegion, region-validity and
// contact refusals, constructPlanarSweep, then full certification. Plan and
// construction are raw geometry: NEVER a reconstruction or material
// certificate. Standard_Failure propagates to the caller (plan/sweep contents
// are then unspecified). A plan is bound to the exact source/Face passed and
// has no lifetime beyond its caller.
struct PlanarSweepPlan {
    Neighborhood local;       // isolated certified copy, patch, neighbors, boundary
    gp_Dir normal;            // outward normal of the selected planar patch
    bool splineEdit = false;  // allowSpline opt-in and a spline/extrusion neighbor
    double originalHeight = 0; // splineEdit only: certified complete extrusion height
};
// F2.9F.2 generic plan/request invariant: a plan may take the spline route only
// when the request allowed it. Geometry-neutral; no normal or patch assumption.
inline bool planMatchesRequest(const PlanarSweepPlan& plan, bool splineAllowed) {
    return splineAllowed || !plan.splineEdit;
}
struct PlanarSweep {
    gp_Vec delta;
    gp_Trsf shift;
    TopoDS_Shape region;      // union of the trimmed patch sweeps
    TopoDS_Shape directional; // Fuse/Cut of region with the isolated source
    TopoDS_Shape complete;    // splineEdit only: exact complete cap prism
    const TopoDS_Shape& candidate() const { return complete.IsNull() ? directional : complete; }
};
// F2.9E.5 evaluation-local source admission evidence. Constructing it runs the
// existing full source certificate (BRepCertification::singleSolid: counts,
// BRepCheck validity, closed shells, owned topology, validated positive volume,
// self-interference) on exactly `source`. It binds that result to the exact
// source and selected Face (TopoDS IsEqual: TShape, location, orientation).
// Geometry-only and origin-independent: any admission provider may create it.
// Non-copyable and non-movable, so it lives only in the evaluating scope and
// cannot reach a session, cache, plan or later evaluation. It is NOT a
// reconstruction or material certificate and never authorizes a commit.
class SourceAdmission {
public:
    SourceAdmission(const TopoDS_Shape& source, const TopoDS_Face& face);
    SourceAdmission(const SourceAdmission&) = delete;
    SourceAdmission& operator=(const SourceAdmission&) = delete;
    bool valid() const { return valid_; }
    bool binds(const TopoDS_Shape& source, const TopoDS_Face& face) const {
        return valid_ && source_.IsEqual(source) && face_.IsEqual(face);
    }
    double volume() const { return volume_; }
private:
    TopoDS_Shape source_;
    TopoDS_Face face_;
    double volume_ = 0;
    bool valid_ = false;
};
// Neighborhood plus value-independent support admission; refusal sets error.
// Without `admitted`, or when it does not bind exactly this source and Face,
// the isolated copy receives the full source certificate (unchanged certified
// behavior). With binding evidence the copy keeps the refusal-only checks
// (validity, closed shells, one solid/shell, tolerance) and the certificate's
// volume is carried over; volume integration and self-interference are not
// recomputed on the exact copy of the already certified source.
bool planPlanarSweep(const TopoDS_Shape& source, const TopoDS_Face& target, bool allowSpline,
                     PlanarSweepPlan& plan, QString& error,
                     const SourceAdmission* admitted = nullptr);
// Fills delta, shift and region. No validity or contact check.
void sweepPlanarRegion(const PlanarSweepPlan& plan, double distance, PlanarSweep& sweep);
// Fills directional and complete from an existing region. No certification.
void constructPlanarSweep(const PlanarSweepPlan& plan, double distance, PlanarSweep& sweep);
// Constant-section planar motion along invariant adjacent supports. No stock
// reconstruction: sweep the current trimmed face and certify the local change.
// allowSpline is an explicit Move Face-only opt-in for complete no-hole sweeps.
bool movePlanarLocal(const TopoDS_Shape& source, const TopoDS_Face& target,
                     double distance, Result& result, QString& error, bool allowSpline = false);
// Bounded, opt-in current-B-Rep elliptical extrusion certificate. Displacement
// must be axial and normal to the selected cap; no other command opts into it.
bool moveEllipticalCap(const TopoDS_Shape& source, const TopoDS_Face& target,
                       const gp_Vec& displacement, Result& result, QString& error);
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
// F2.9G.5: stackedCylinderSeamPlane (opt-in, planar Move Face only) unpins ONLY
// the shared circular seam between two stacked bands of one cylinder that lies
// in that plane (the moved Face's original plane), plus their periodic seams;
// every other cylindrical edge stays pinned as before. nullptr = unchanged.
bool normalize(const TopoDS_Shape& candidate, TopoDS_Shape& result, QString& error,
               bool allowValidSplits = false, const gp_Pln* stackedCylinderSeamPlane = nullptr);
bool sameAnalyticSupport(const TopoDS_Face& a, const TopoDS_Face& b);
bool supports(const TopoDS_Shape& source, const TopoDS_Shape& result,
              const TopoDS_Face& target, const gp_Pln& expected, QString& error);
}
