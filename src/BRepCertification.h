#pragma once
#include "BRepTopology.h"
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Elips.hxx>

// Geometry-only certificates. No repair, normalization, feature recipes or
// transaction ownership. Callers retain operation-specific material policies.
namespace BRepCertification {
bool validBRep(const TopoDS_Shape& shape);
bool closedShells(const TopoDS_Shape& shape);
// Existing direct-edit policy: one solid/shell, no loose topology, positive
// volume and self-interference analysis. Not the Boolean multi-solid policy.
bool singleSolid(const TopoDS_Shape& shape, double& volume);
// Existing cylindrical policy: two edge uses and one connected vertex fan;
// degenerate edges are refused. Do not impose this on every OCCT solid.
bool manifold(const TopoDS_Shape& shape);
bool withinTolerance(const TopoDS_Shape& shape, double limit, double* offending = nullptr);
double volume(const TopoDS_Shape& shape);
double area(const TopoDS_Shape& shape);
TopAbs_State occupancy(const TopoDS_Shape& shape, const gp_Pnt& point, double tolerance);

// Shared spline-aware integration and Common-based material certificate. Inputs
// must already be eligible material sets. Empty non-null compounds are allowed.
// Equal volume alone is never sufficient. Measurement does not enable spline edits.
bool equivalent(const TopoDS_Shape& a, const TopoDS_Shape& b);
struct MaterialDifference { TopoDS_Shape removed, added; };
bool difference(const TopoDS_Shape& before, const TopoDS_Shape& after,
                MaterialDifference& result, QString& error);
bool preservedOutside(const TopoDS_Shape& before, const TopoDS_Shape& after,
                      const TopoDS_Shape& editRegion);

// Existing analytic carrier comparison, including its plane orientation policy
// and unoriented cylinder axes. It does NOT establish trimmed-region identity,
// material side, transition identity or persistent subshape naming.
bool sameCarrier(const TopoDS_Face& a, const TopoDS_Face& b,
                 const gp_Pln* expected = nullptr, bool unoriented = false);
// Opt-in support proof for axial elliptical-cap reconstruction only. This does
// not broaden sameCarrier or any existing Rotate/Pull/Delete/blend policy.
struct EllipticExtrusion { gp_Elips section; gp_Dir axis; };
bool ellipticExtrusion(const BRepReconstruction::SupportDescriptor& support, EllipticExtrusion& result);
bool sameEllipticExtrusion(const TopoDS_Face& a, const TopoDS_Face& b);
struct FaceCoverage {
    std::vector<TopoDS_Face> carriers; // can include disjoint trimmed patches
    std::vector<TopoDS_Face> overlaps; // strictly positive area (>1e-12)
    double coveredArea = 0;
    double positiveArea = 0; // sum only the overlaps admitted above
};
// Existing summed-overlap primitive. Callers retain their area threshold and
// whether disappearance, growth, or partial coverage is permitted.
FaceCoverage coverage(const TopoDS_Face& face, const BRepReconstruction::TopologyIndex& result);
bool areaMatches(double expected, double actual, double relativeTolerance);
bool preservedFace(const TopoDS_Face& face, const BRepReconstruction::TopologyIndex& result,
                   double relativeTolerance);
bool sameTrimmedRegion(const TopoDS_Face& a, const TopoDS_Face& b);

enum class FaceRelation { SameRegion, Split, Merged, Modified, Removed };
struct FaceCorrespondence {
    TopoDS_Face source;
    FaceCoverage regions;
    FaceRelation relation = FaceRelation::Removed;
};
struct Correspondence {
    std::vector<FaceCorrespondence> faces;
    std::vector<TopoDS_Face> newFaces; // no positive-area source overlap
};
// Strict geometric region relation, independent of construction history.
// Unsupported carriers, overlapping duplicate regions, or many-to-many trim
// repartitions refuse atomically. Split/merge is a region relation, not a stable
// identity. SameRegion describes geometry only; a consumer choosing one target
// still needs its operation's orientation/material and uniqueness certificate.
bool correspond(const TopoDS_Shape& source, const TopoDS_Shape& result,
                Correspondence& mapping, QString& error);
// Explicit opt-in relation for analytic Faces and one certified spline carrier
// family per call. Differently parameterized bases requiring knot insertion or
// degree elevation, and multiple unrelated spline families, remain refusals.
// Uses exact Face Common regions and shared area measurements after the support
// proof; it is not a new modeling permission or material-side certificate.
bool correspondInvariantSpline(const TopoDS_Shape& source, const TopoDS_Shape& result,
                               Correspondence& mapping, QString& error);
}
