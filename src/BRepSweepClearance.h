#pragma once
// F2.9G.3 geometry-independent planar sweep-clearance proof (production helper).
// Moved from the F2.9G.2-F2.9G.2C isolated experiment; that experiment now
// reuses these functions. Display-only admission evidence: nothing here
// certifies, copies the B-Rep, runs a Boolean or modifies a shape.
//
// Terms: h(p) = n.p - L for the plan's outward normal n and target level L;
// walls = plan.local.neighbors; others = every other non-target Face.
#include "BRepCertification.h"
#include "BRepReconstruction.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_BezierSurface.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Lin.hxx>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace BRepReconstruction::SweepClearance {
// Existing policies only: 1e-6 is the enforced sharp-reconstruction tolerance
// (planPlanarSweep refuses looser sources); the margin is twice that band; the
// wall bound derives from the certified contact threshold (1e-8 relative).
inline constexpr double toleranceLimit=1.e-6;
inline constexpr double margin=2*toleranceLimit;
inline constexpr double contactRelative=1.e-8;

struct Range {
    double lo=INFINITY,hi=-INFINITY;
    void add(double x){lo=std::min(lo,x);hi=std::max(hi,x);}
    void add(const Range& r){lo=std::min(lo,r.lo);hi=std::max(hi,r.hi);}
};
// Exact range of k + A cos t + B sin t on [t0, t1].
inline Range harmonic(double k,double A,double B,double t0,double t1) {
    Range r;r.add(k+A*std::cos(t0)+B*std::sin(t0));r.add(k+A*std::cos(t1)+B*std::sin(t1));
    if(std::hypot(A,B)==0) return r;
    const double phi=std::atan2(B,A);
    for(double base:{phi,phi+M_PI}) {
        const double first=base+2*M_PI*std::ceil((t0-base)/(2*M_PI));
        for(double t=first;t<=t1;t+=2*M_PI) r.add(k+A*std::cos(t)+B*std::sin(t));
    }
    return r;
}
template<class Poles,class Weights>
inline bool hull(const Poles& poles,const Weights* weights,const gp_Dir& n,Range& r) {
    if(weights)for(int i=weights->Lower();i<=weights->Upper();++i)if(!(weights->Value(i)>0))return false;
    for(int i=poles.Lower();i<=poles.Upper();++i)r.add(gp_Vec(poles.Value(i).XYZ()).Dot(gp_Vec(n)));
    return true;
}
// Exact (analytic) or convex-hull (polynomial/rational) range of n.p over an
// edge's 3D curve on its parameter range. Other curve types: false.
inline bool edgeRange(const TopoDS_Edge& edge,const gp_Dir& n,Range& r) {
    if(BRep_Tool::Degenerated(edge))return true;
    const BRepAdaptor_Curve c(edge);
    const double t0=c.FirstParameter(),t1=c.LastParameter();
    if(!std::isfinite(t0)||!std::isfinite(t1)||t0>t1)return false;
    const gp_Vec N(n);
    switch(c.GetType()) {
    case GeomAbs_Line: r.add(gp_Vec(c.Value(t0).XYZ()).Dot(N));r.add(gp_Vec(c.Value(t1).XYZ()).Dot(N));return true;
    case GeomAbs_Circle: {const auto g=c.Circle();const auto& a=g.Position();
        r.add(harmonic(gp_Vec(g.Location().XYZ()).Dot(N),g.Radius()*gp_Vec(a.XDirection()).Dot(N),g.Radius()*gp_Vec(a.YDirection()).Dot(N),t0,t1));return true;}
    case GeomAbs_Ellipse: {const auto g=c.Ellipse();const auto& a=g.Position();
        r.add(harmonic(gp_Vec(g.Location().XYZ()).Dot(N),g.MajorRadius()*gp_Vec(a.XDirection()).Dot(N),g.MinorRadius()*gp_Vec(a.YDirection()).Dot(N),t0,t1));return true;}
    case GeomAbs_BSplineCurve: {const auto g=c.BSpline();return hull(g->Poles(),g->Weights(),n,r);}
    case GeomAbs_BezierCurve: {const auto g=c.Bezier();return hull(g->Poles(),g->Weights(),n,r);}
    default: return false;
    }
}
// Range of n.p over a trimmed Face: planes by their boundary curves; cylinders
// exactly over the conservative UV bounding rectangle; B-spline/Bezier surfaces
// by the convex hull of their poles. Other supports: false.
inline bool faceRange(const TopoDS_Face& face,const gp_Dir& n,Range& r,unsigned& edges) {
    const BRepAdaptor_Surface s(face);const gp_Vec N(n);
    switch(s.GetType()) {
    case GeomAbs_Plane:
        for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next()){++edges;if(!edgeRange(TopoDS::Edge(e.Current()),n,r))return false;}
        return r.lo<=r.hi;
    case GeomAbs_Cylinder: {
        double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        if(!std::isfinite(u0)||!std::isfinite(u1)||!std::isfinite(v0)||!std::isfinite(v1)||u0>u1||v0>v1)return false;
        const auto g=s.Cylinder();const auto& a=g.Position();
        const auto ring=harmonic(gp_Vec(g.Location().XYZ()).Dot(N),g.Radius()*gp_Vec(a.XDirection()).Dot(N),g.Radius()*gp_Vec(a.YDirection()).Dot(N),u0,u1);
        const double c=gp_Vec(a.Direction()).Dot(N);
        r.add(ring.lo+std::min(c*v0,c*v1));r.add(ring.hi+std::max(c*v0,c*v1));return true;}
    case GeomAbs_BSplineSurface: {const auto g=s.BSpline();const auto& p=g->Poles();Range q;
        for(int i=p.LowerRow();i<=p.UpperRow();++i)for(int j=p.LowerCol();j<=p.UpperCol();++j){
            if(g->IsURational()||g->IsVRational())if(!(g->Weight(i,j)>0))return false;
            q.add(gp_Vec(p(i,j).XYZ()).Dot(N));}
        r.add(q);return true;}
    case GeomAbs_BezierSurface: {const auto g=s.Bezier();const auto& p=g->Poles();Range q;
        for(int i=p.LowerRow();i<=p.UpperRow();++i)for(int j=p.LowerCol();j<=p.UpperCol();++j){
            if(g->IsURational()||g->IsVRational())if(!(g->Weight(i,j)>0))return false;
            q.add(gp_Vec(p(i,j).XYZ()).Dot(N));}
        r.add(q);return true;}
    default: return false;
    }
}

// Whole-source representation: every Face carried by Plane/Cylinder and every
// non-degenerate edge a Line/Circle/Ellipse, classified through BRepAdaptor
// exactly as production sameCarrier does. Required because finishLocal runs
// coverage() on EVERY source Face and sameCarrier knows only Plane/Cylinder/Cone.
enum class RepresentationIssue { None, Surface, Curve };
inline RepresentationIssue analyticRepresentation(const TopoDS_Shape& shape,unsigned* edgeCount=nullptr) {
    for(TopExp_Explorer f(shape,TopAbs_FACE);f.More();f.Next()) {
        const auto t=BRepAdaptor_Surface(TopoDS::Face(f.Current())).GetType();
        if(t!=GeomAbs_Plane&&t!=GeomAbs_Cylinder)return RepresentationIssue::Surface;
    }
    TopTools_IndexedMapOfShape edges;TopExp::MapShapes(shape,TopAbs_EDGE,edges);
    for(int i=1;i<=edges.Extent();++i) {
        if(edgeCount)++*edgeCount;
        const auto e=TopoDS::Edge(edges(i));if(BRep_Tool::Degenerated(e))continue;
        const auto t=BRepAdaptor_Curve(e).GetType();
        if(t!=GeomAbs_Line&&t!=GeomAbs_Circle&&t!=GeomAbs_Ellipse)return RepresentationIssue::Curve;
    }
    return RepresentationIssue::None;
}

// Near-coincident carriers: refuse when two distinct Faces have carriers that
// production sameCarrier matches (either order; it is asymmetric for planes)
// but that are not identical to 64 ulp (the sameSplineBasis precedent).
struct Carrier { TopoDS_Face face; GeomAbs_SurfaceType type; gp_Dir direction; gp_Pnt location; double radius=0; };
inline bool identicalCarrier(const Carrier& a,const Carrier& b) {
    const double eps=64*std::numeric_limits<double>::epsilon();
    const double scale=std::max({1.,gp_Vec(a.location.XYZ()).Magnitude(),gp_Vec(b.location.XYZ()).Magnitude(),a.radius,b.radius});
    if(a.type==GeomAbs_Plane)
        return gp_Vec(a.direction.XYZ()-b.direction.XYZ()).Magnitude()<=eps
            &&std::abs(gp_Vec(a.direction).Dot(gp_Vec(a.location,b.location)))<=eps*scale;
    return gp_Vec(a.direction).Crossed(gp_Vec(b.direction)).Magnitude()<=eps
        &&gp_Lin(a.location,a.direction).Distance(b.location)<=eps*scale&&std::abs(a.radius-b.radius)<=eps*scale;
}
enum class CarrierIssue { None, Ambiguous, Unsupported };
inline CarrierIssue nearCoincidentCarriers(const TopoDS_Shape& shape,unsigned* pairs=nullptr,unsigned* matches=nullptr) {
    std::vector<Carrier> carriers;
    TopTools_IndexedMapOfShape all;TopExp::MapShapes(shape,TopAbs_FACE,all);
    for(int i=1;i<=all.Extent();++i) {
        Carrier c;c.face=TopoDS::Face(all(i));const BRepAdaptor_Surface s(c.face);c.type=s.GetType();
        if(c.type==GeomAbs_Plane){c.direction=s.Plane().Axis().Direction();if(c.face.Orientation()==TopAbs_REVERSED)c.direction.Reverse();c.location=s.Plane().Location();}
        else if(c.type==GeomAbs_Cylinder){c.direction=s.Cylinder().Axis().Direction();c.location=s.Cylinder().Location();c.radius=s.Cylinder().Radius();}
        else return CarrierIssue::Unsupported;
        carriers.push_back(c);
    }
    for(size_t i=0;i<carriers.size();++i)for(size_t j=i+1;j<carriers.size();++j) {
        const auto& a=carriers[i];const auto& b=carriers[j];
        // Cheap prefilter, far looser than sameCarrier's 1e-7 rad direction test.
        if(a.type!=b.type||std::abs(gp_Vec(a.direction).Dot(gp_Vec(b.direction)))<1-1.e-6)continue;
        if(pairs)++*pairs;
        if(!BRepCertification::sameCarrier(a.face,b.face)&&!BRepCertification::sameCarrier(b.face,a.face))continue;
        if(matches)++*matches;
        if(!identicalCarrier(a,b))return CarrierIssue::Ambiguous;
    }
    return CarrierIssue::None;
}

// Every target edge a Line or Circle: each swept side Face then lies on its
// wall's Plane/Cylinder carrier (certified transition-surface obligation).
inline bool lineCircleBoundary(const TopoDS_Face& target) {
    for(TopExp_Explorer e(target,TopAbs_EDGE);e.More();e.Next()) {
        const auto edge=TopoDS::Edge(e.Current());if(BRep_Tool::Degenerated(edge))continue;
        const auto type=BRepAdaptor_Curve(edge).GetType();
        if(type!=GeomAbs_Line&&type!=GeomAbs_Circle)return false;
    }
    return true;
}

// Heights of the plan's isolated copy relative to the target plane.
struct Heights {
    bool bounded=true;
    double level=0;
    double wallTop=-INFINITY;     // max h over wall Faces
    double otherTop=-INFINITY;    // max h over non-patch, non-wall Faces
    double clearance=INFINITY;    // inward depth before the first non-wall Face (h < margin)
    unsigned faces=0,edges=0;
};
// Requires: single target patch on a Plane (checked by the caller).
inline Heights measure(const PlanarSweepPlan& plan) {
    Heights r;const auto& solid=plan.local.solid;const auto& target=plan.local.target;const gp_Dir n=plan.normal;
    r.level=gp_Vec(BRepAdaptor_Surface(target).Plane().Location().XYZ()).Dot(gp_Vec(n));
    TopTools_IndexedMapOfShape walls;for(const auto& w:plan.local.neighbors)walls.Add(w);
    TopTools_IndexedMapOfShape faces;TopExp::MapShapes(solid,TopAbs_FACE,faces);
    for(int i=1;i<=faces.Extent();++i) {
        const auto f=TopoDS::Face(faces(i));++r.faces;
        if(f.IsSame(target))continue;
        Range range;if(!faceRange(f,n,range,r.edges)){r.bounded=false;return r;}
        const double lo=range.lo-r.level,hi=range.hi-r.level;
        if(walls.Contains(f)){r.wallTop=std::max(r.wallTop,hi);continue;}
        r.otherTop=std::max(r.otherTop,hi);
        if(lo<margin)r.clearance=std::min(r.clearance,-hi);
    }
    return r;
}
enum class Clearance { Clear, OtherAtCap, WallAbove, Depth };
// Outward: others below the plane by the margin and walls within half the
// certified contact threshold. Inward: |d| at most clearance - margin.
inline Clearance clearance(const Heights& h,double targetArea,double d) {
    if(d>0) {
        const double wallBound=0.5*contactRelative*std::max(1./targetArea,d);
        if(h.otherTop>-margin)return Clearance::OtherAtCap;
        if(h.wallTop>wallBound)return Clearance::WallAbove;
        return Clearance::Clear;
    }
    return -d>h.clearance-margin?Clearance::Depth:Clearance::Clear;
}
}
