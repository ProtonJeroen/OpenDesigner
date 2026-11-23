#pragma once
// Display-only Box gate. This is NOT a modeling/material certificate.
// F2.9F.1: internal provider of BRepProvisionalNegotiation.h. Sessions must not
// call it directly; they negotiate through Provisional::negotiatePlanarMove.
// F2.9F.3/F2.9F.4: geometric proof only, exported as the provider contract
// value `provider`; reconstruction, validation and selection are shared.
#include "BRepReconstruction.h"
#include "BRepTiming.h"
#include "BRepCertification.h"
#include "BRepProvisional.h"
#include "BRepProvisionalNegotiation.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <array>
#include <optional>
#include <set>
#include <cmath>

namespace ProvisionalBoxPreview {
namespace Provisional=BRepReconstruction::Provisional;
using Point=std::array<double,3>;
inline Point point(const gp_Pnt& p){return {p.X(),p.Y(),p.Z()};}
// F2.9D.4 placement rule; provider-neutral since F2.9F.1 (BRepProvisional.h).
using Provisional::identityPlacement;
inline TopTools_IndexedMapOfShape map(const TopoDS_Shape& s,TopAbs_ShapeEnum type) {
    TopTools_IndexedMapOfShape result;TopExp::MapShapes(s,type,result);return result;
}
// Exact Box topology/geometry, selected +Z Face and the 1e-6 source tolerance
// limit; everything in eligible() except the source solid certificate.
inline bool boxShape(const TopoDS_Shape& source,const TopoDS_Face& selected,Point& lo,Point& hi) {
    if(source.IsNull()||!source.Location().IsIdentity()||source.Orientation()!=TopAbs_FORWARD
       ||map(source,TopAbs_SOLID).Extent()!=1||map(source,TopAbs_SHELL).Extent()!=1
       ||map(source,TopAbs_FACE).Extent()!=6||map(source,TopAbs_EDGE).Extent()!=12
       ||map(source,TopAbs_VERTEX).Extent()!=8)return false;
    lo={INFINITY,INFINITY,INFINITY};hi={-INFINITY,-INFINITY,-INFINITY};std::set<Point> vertices;
    for(TopExp_Explorer v(source,TopAbs_VERTEX);v.More();v.Next()) {
        auto p=point(BRep_Tool::Pnt(TopoDS::Vertex(v.Current())));
        for(int i=0;i<3;++i){if(!std::isfinite(p[i]))return false;lo[i]=std::min(lo[i],p[i]);hi[i]=std::max(hi[i],p[i]);}
        vertices.insert(p);
    }
    std::set<Point> corners;
    for(int mask=0;mask<8;++mask)corners.insert({mask&1?hi[0]:lo[0],mask&2?hi[1]:lo[1],mask&4?hi[2]:lo[2]});
    if(vertices!=corners||corners.size()!=8)return false;
    for(int i=0;i<3;++i)if(hi[i]-lo[i]<=1.e-6)return false;
    std::set<std::pair<int,int>> supports;bool top=false;
    for(TopExp_Explorer it(source,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());BRepAdaptor_Surface carrier(face);
        if(!face.Location().IsIdentity()||carrier.GetType()!=GeomAbs_Plane
           ||map(face,TopAbs_WIRE).Extent()!=1||map(face,TopAbs_EDGE).Extent()!=4
           ||map(face,TopAbs_VERTEX).Extent()!=4)return false;
        auto normal=carrier.Plane().Axis().Direction();
        if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();else if(face.Orientation()!=TopAbs_FORWARD)return false;
        int axis=-1;for(int i=0;i<3;++i)if(std::abs(normal.Coord(i+1))==1.)axis=i;
        if(axis<0)return false;
        for(int i=0;i<3;++i)if(i!=axis&&normal.Coord(i+1)!=0)return false;
        const double level=carrier.Plane().Location().Coord(axis+1);
        const int side=level==lo[axis]?0:level==hi[axis]?1:-1;
        if(side<0||normal.Coord(axis+1)!=(side?1.:-1.)||!supports.insert({axis,side}).second)return false;
        std::set<Point> actual,wanted;for(const auto& p:corners)if(p[axis]==level)wanted.insert(p);
        for(TopExp_Explorer v(face,TopAbs_VERTEX);v.More();v.Next())actual.insert(point(BRep_Tool::Pnt(TopoDS::Vertex(v.Current()))));
        if(actual!=wanted)return false;
        if(axis==2&&side==1)top=face.IsEqual(selected);
    }
    if(!top||supports.size()!=6)return false;
    for(TopExp_Explorer it(source,TopAbs_EDGE);it.More();it.Next()) {
        const auto edge=TopoDS::Edge(it.Current());BRepAdaptor_Curve c(edge);
        if(!edge.Location().IsIdentity()||c.GetType()!=GeomAbs_Line)return false;
        TopoDS_Vertex a,b;TopExp::Vertices(edge,a,b);
        if(a.IsNull()||b.IsNull())return false;
        auto p=point(BRep_Tool::Pnt(a)),q=point(BRep_Tool::Pnt(b));int varying=0;
        for(int i=0;i<3;++i)if(p[i]!=q[i])++varying;
        if(varying!=1||!std::isfinite(c.FirstParameter())||!std::isfinite(c.LastParameter())||c.FirstParameter()>=c.LastParameter())return false;
        const auto first=point(c.Value(c.FirstParameter())),last=point(c.Value(c.LastParameter()));
        if(!((first==p&&last==q)||(first==q&&last==p)))return false;
    }
    return BRepCertification::withinTolerance(source,1.e-6);
}
// Unchanged predicate: boxShape and the full source certificate.
inline bool eligible(const TopoDS_Shape& source,const TopoDS_Face& selected,Point& lo,Point& hi) {
    BRepTiming::Scope timer("MoveFace.BoxProvisional.Eligibility");
    double volume=0;
    return boxShape(source,selected,lo,hi)&&BRepCertification::singleSolid(source,volume);
}
// F2.9F.3: the Box provider owns only its geometric proof. It plans, sweeps,
// copies, fuses/cuts and constructs nothing; BRepProvisionalNegotiation.h runs
// the shared stages (planPlanarSweep / sweepPlanarRegion / constructPlanarSweep,
// the same functions as the certified local route) and the shared validation.
// Why the certified route's region-validity and contact refusals may be skipped
// for this display-only result: proveSource() establishes an exact axis-aligned
// Box with its +Z Face selected, admitsValue() keeps a positive height and
// admitsPlan() binds the shared plan to one patch with outward normal exactly
// +Z, so the region is an exact box above the source (d>0, face contact only)
// or inside it (d<0). OK still runs complete certification.
// Proof scalars: lo xyz in [0..2], hi xyz in [3..5] (provider-owned layout).
inline Point lo(const Provisional::ProviderProof& p){return {p.scalars[0],p.scalars[1],p.scalars[2]};}
inline Point hi(const Provisional::ProviderProof& p){return {p.scalars[3],p.scalars[4],p.scalars[5]};}
// Exact Box geometry, selected +Z Face, 1e-6 source tolerance. The shared layer
// binds the source certificate (SourceAdmission) right after this proof.
inline bool proveSource(const TopoDS_Shape& source,const TopoDS_Face& face,Provisional::ProviderProof& proof) {
    Point l{},h{};const bool proven=boxShape(source,face,l,h);
    for(int i=0;i<3;++i){proof.scalars[i]=l[i];proof.scalars[3+i]=h[i];}
    return proven;
}
// Box collapse rule: the moved Box keeps a positive height.
inline bool admitsValue(const Provisional::ProviderProof& proof,double displacement) {
    return hi(proof)[2]-lo(proof)[2]+displacement>1.e-6;
}
// Box binding of the shared plan: one planar patch, outward normal exactly +Z.
inline bool admitsPlan(const BRepReconstruction::PlanarSweepPlan& plan) {
    return plan.local.patch.size()==1&&plan.normal.X()==0.&&plan.normal.Y()==0.&&plan.normal.Z()==1.;
}
// F2.9F.4 contract value. The display form is unchanged from F2.9F.2.
inline const Provisional::PlanarMoveProvider provider{
    &proveSource,&admitsValue,&admitsPlan,"Shared plan differs from the provisional Box admission.",
    {true,true,true,64,256,"Provisional Box failed display checks."}};
inline const Provisional::DisplayExpectation& display=provider.display;
}
