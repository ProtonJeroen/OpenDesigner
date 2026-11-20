#pragma once
// F2.9G.2 - F2.9G.2C ISOLATED EXPERIMENT (oracle harness).
// Since F2.9G.3 the geometric proof lives in src/BRepSweepClearance.h and is
// used by the production clear-sweep provider. This header only keeps the
// experiment's policy switches (G2/G2A/G2B/G2C), its sampled orientation probe
// and its diagnostics, and delegates every geometric step to that helper, so
// the 223-case oracle continuously regresses the production proof.
#include "BRepSweepClearance.h"
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepTopAdaptor_FClass2d.hxx>
#include <chrono>
#include <string>

namespace SweepClearance {
using namespace BRepReconstruction::SweepClearance;
enum class Verdict { ProvenClear, NotProven, InvalidInput };
inline const char* name(Verdict v) {return v==Verdict::ProvenClear?"ProvenClear":v==Verdict::NotProven?"NotProven":"InvalidInput";}

// Any: F2.9G.2 (no representation restriction). AnalyticPlaneCylinder: the
// F2.9G.2A whole-source restriction (analyticRepresentation).
enum class Representation { Any, AnalyticPlaneCylinder };
// Defaults are the F2.9G.2C configuration, which the production provider uses.
struct Options {
    Representation representation=Representation::AnalyticPlaneCylinder;
    bool lineCircleBoundary=true;
    bool sampleOrientation=false;
    bool nearCoincidentRefusal=true;
};
struct Result {
    Verdict verdict=Verdict::NotProven;
    std::string reason;
    double level=0,wallTop=-INFINITY,otherTop=-INFINITY,clearance=INFINITY,wallBound=0;
    unsigned faces=0,edges=0;
    unsigned representationEdges=0;
    double representationMs=0;
    unsigned carrierPairs=0,carrierMatches=0;
    double nearMs=0;
    double ms=0;
};
// F2.9G.2 sampling evidence (experiment only; redundant with the source
// certificate, see F2.9G.2B #7).
inline bool orientationConsistent(const TopoDS_Shape& solid,const TopoDS_Face& target,const gp_Dir& n) {
    double u0,u1,v0,v1;BRepTools::UVBounds(target,u0,u1,v0,v1);
    BRepTopAdaptor_FClass2d inside(target,Precision::PConfusion());const BRepAdaptor_Surface s(target);
    for(int i=1;i<8;++i)for(int j=1;j<8;++j) {
        const gp_Pnt2d uv(u0+(u1-u0)*i/8,v0+(v1-v0)*j/8);
        if(inside.Perform(uv)!=TopAbs_IN)continue;
        const auto q=s.Value(uv.X(),uv.Y());const double delta=1.e-3;
        BRepClass3d_SolidClassifier below(solid,q.Translated(-gp_Vec(n)*delta),Precision::Confusion());
        BRepClass3d_SolidClassifier above(solid,q.Translated(gp_Vec(n)*delta),Precision::Confusion());
        return below.State()==TopAbs_IN&&above.State()==TopAbs_OUT;
    }
    return false;
}

// `plan` must come from planPlanarSweep(source, face, false, plan, ...).
inline Result provePlanarSweepClearance(const TopoDS_Shape& source,const TopoDS_Face& face,
                                        const BRepReconstruction::PlanarSweepPlan& plan,bool planned,double d,
                                        Options options) {
    const auto start=std::chrono::steady_clock::now();
    Result r;
    auto finish=[&](Verdict v,std::string why){r.verdict=v;r.reason=std::move(why);
        r.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();return r;};
    if(source.IsNull()||face.IsNull()||!std::isfinite(d)||std::abs(d)<1.e-7)return finish(Verdict::InvalidInput,"null input or non-finite/zero displacement");
    if(!planned) {
        TopTools_IndexedMapOfShape members;TopExp::MapShapes(source,TopAbs_FACE,members);
        return members.Contains(face)?finish(Verdict::NotProven,"shared plan refused")
                                     :finish(Verdict::InvalidInput,"selected Face is not a member of the source");
    }
    if(plan.splineEdit)return finish(Verdict::NotProven,"spline plan");
    if(plan.local.patch.size()!=1||!plan.local.patch.front().IsSame(plan.local.target))return finish(Verdict::NotProven,"patch is not the single target Face");
    const auto& solid=plan.local.solid;const auto& target=plan.local.target;const gp_Dir n=plan.normal;
    if(BRepAdaptor_Surface(target).GetType()!=GeomAbs_Plane)return finish(Verdict::NotProven,"target not planar");
    if(options.representation==Representation::AnalyticPlaneCylinder) {
        const auto t=std::chrono::steady_clock::now();
        const auto issue=analyticRepresentation(solid,&r.representationEdges);
        r.representationMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();
        if(issue==RepresentationIssue::Surface)return finish(Verdict::NotProven,"a source Face is not carried by an analytic Plane/Cylinder");
        if(issue==RepresentationIssue::Curve)return finish(Verdict::NotProven,"a source edge is not a Line/Circle/Ellipse");
    }
    if(options.nearCoincidentRefusal) {
        const auto t=std::chrono::steady_clock::now();
        const auto issue=nearCoincidentCarriers(solid,&r.carrierPairs,&r.carrierMatches);
        r.nearMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();
        if(issue==CarrierIssue::Unsupported)return finish(Verdict::NotProven,"carrier outside the analytic set");
        if(issue==CarrierIssue::Ambiguous)return finish(Verdict::NotProven,"distinct Faces have matching but non-identical carriers");
    }
    if(options.lineCircleBoundary) {
        const auto t=std::chrono::steady_clock::now();const bool ok=lineCircleBoundary(target);
        r.representationMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();
        if(!ok)return finish(Verdict::NotProven,"a target boundary edge is not a Line/Circle");
    }
    if(options.sampleOrientation&&!orientationConsistent(solid,target,n))return finish(Verdict::NotProven,"orientation evidence inconsistent");
    const auto h=measure(plan);
    r.level=h.level;r.wallTop=h.wallTop;r.otherTop=h.otherTop;r.clearance=h.clearance;r.faces=h.faces;r.edges=h.edges;
    if(!h.bounded)return finish(Verdict::NotProven,"Face support or curve has no exact/conservative bound");
    const double area=d>0?BRepCertification::area(target):1.;
    if(d>0)r.wallBound=0.5*contactRelative*std::max(1./area,d);
    switch(clearance(h,area,d)) {
    case Clearance::OtherAtCap: return finish(Verdict::NotProven,"non-wall material reaches the cap plane margin");
    case Clearance::WallAbove: return finish(Verdict::NotProven,"a wall rises above the cap plane");
    case Clearance::Depth: return finish(Verdict::NotProven,"depth reaches the first non-wall boundary (clearance margin)");
    case Clearance::Clear: break;
    }
    return finish(Verdict::ProvenClear,d>0?"cap plane supports the solid":"slab contains only the target and its invariant walls");
}
inline Result provePlanarSweepClearance(const TopoDS_Shape& source,const TopoDS_Face& face,
                                        const BRepReconstruction::PlanarSweepPlan& plan,bool planned,double d,
                                        Representation policy=Representation::AnalyticPlaneCylinder) {
    Options o;o.representation=policy;return provePlanarSweepClearance(source,face,plan,planned,d,o);
}
}
