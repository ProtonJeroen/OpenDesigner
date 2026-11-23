#pragma once
// F2.9G.3 geometry-independent "clear planar sweep" provider. Display-only
// admission evidence for the shared provisional pipeline; NOT a certificate.
// It proves, from the actual B-Rep only (no primitive, profile or history):
//   proveSource : whole-source analytic representation (every Face Plane/
//                 Cylinder, every edge Line/Circle/Ellipse) and no two distinct
//                 Faces on matching-but-non-identical carriers (production
//                 sameCarrier semantics); the selected Face is planar.
//   admitsPlan  : the shared plan's patch is exactly the target Face, planar,
//                 bounded only by Line/Circle edges.
//   admitsSweep : conservative outward/inward clearance on the plan's copy.
// The shared layer adds the full source certificate (SourceAdmission), the
// shared planar plan (planPlanarSweep: invariant walls, tolerance, budgets),
// generic finite/non-zero values, sweep, construction and validation. A
// display failure degrades to the certified preview. See F2.9G.2-F2.9G.3.
#include "BRepProvisionalNegotiation.h"
#include "BRepSweepClearance.h"

namespace ProvisionalPlanarClearance {
namespace Provisional=BRepReconstruction::Provisional;
namespace Clearance=BRepReconstruction::SweepClearance;
inline bool proveSource(const TopoDS_Shape& source,const TopoDS_Face& face,Provisional::ProviderProof&) {
    if(source.IsNull()||face.IsNull()||BRepAdaptor_Surface(face).GetType()!=GeomAbs_Plane)return false;
    return Clearance::analyticRepresentation(source)==Clearance::RepresentationIssue::None
        &&Clearance::nearCoincidentCarriers(source)==Clearance::CarrierIssue::None;
}
// Finite and non-zero are enforced generically; the clearance is value-dependent
// and is decided in admitsSweep on the plan.
inline bool admitsValue(const Provisional::ProviderProof&,double){return true;}
inline bool admitsPlan(const BRepReconstruction::PlanarSweepPlan& plan) {
    return !plan.splineEdit&&plan.local.patch.size()==1&&plan.local.patch.front().IsSame(plan.local.target)
        &&BRepAdaptor_Surface(plan.local.target).GetType()==GeomAbs_Plane&&Clearance::lineCircleBoundary(plan.local.target);
}
inline bool admitsSweep(const BRepReconstruction::PlanarSweepPlan& plan,Provisional::ProviderProof&,double displacement) {
    const auto heights=Clearance::measure(plan);
    if(!heights.bounded)return false;
    const double area=displacement>0?BRepCertification::area(plan.local.target):1.;
    return Clearance::clearance(heights,area,displacement)==Clearance::Clearance::Clear;
}
// Generic display form (no planar-only, line-only or manifold restriction);
// display failure falls back to the certified preview.
inline const Provisional::PlanarMoveProvider provider{
    &proveSource,&admitsValue,&admitsPlan,"Shared plan differs from the provisional clear-sweep admission.",
    {false,false,false,64,256,"Provisional candidate failed display checks.",true},&admitsSweep};
}
