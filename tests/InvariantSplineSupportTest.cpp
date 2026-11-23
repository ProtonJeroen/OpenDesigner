#include "BRepCertification.h"
#include "BRepTiming.h"
#include <BRepAlgoAPI_Common.hxx>
#include "BRepMeasurement.h"
#include "BRepReconstruction.h"
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <Geom2d_Line.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace R=BRepReconstruction;
namespace C=BRepCertification;
namespace {
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
void near(double a,double b,const char* message) {check(std::abs(a-b)<=1.e-8*std::max(1.,std::abs(b)),message);}
std::string brep(const TopoDS_Shape& shape) {std::ostringstream out;BRepTools::Write(shape,out);return out.str();}
Handle(Geom_BSplineCurve) basis(bool rational=false) {
    TColgp_Array1OfPnt poles(1,4);poles(1)=gp_Pnt(0,20,0);poles(2)=gp_Pnt(20,24,0);
    poles(3)=gp_Pnt(40,26,0);poles(4)=gp_Pnt(60,20,0);
    TColStd_Array1OfReal knots(1,2);knots(1)=0;knots(2)=1;
    TColStd_Array1OfInteger mults(1,2);mults(1)=mults(2)=4;
    if(rational) {TColStd_Array1OfReal weights(1,4);weights.Init(1);weights(2)=1.2;return new Geom_BSplineCurve(poles,weights,knots,mults,3);}
    return new Geom_BSplineCurve(poles,knots,mults,3);
}
TopoDS_Face side(double v0=0,double v1=20,double u0=0,double u1=1,Handle(Geom_BSplineCurve) curve={}) {
    if(curve.IsNull()) curve=basis();
    const Handle(Geom_SurfaceOfLinearExtrusion) surface=new Geom_SurfaceOfLinearExtrusion(curve,gp_Dir(0,0,1));
    return BRepBuilderAPI_MakeFace(surface,u0,u1,v0,v1,1.e-7).Face();
}
TopoDS_Shape compound(const std::vector<TopoDS_Shape>& shapes) {
    BRep_Builder builder;TopoDS_Compound result;builder.MakeCompound(result);
    for(const auto& shape:shapes) builder.Add(result,shape);return result;
}
R::InvariantSplineSupport recognize(const TopoDS_Face& face,const R::TopologyIndex* context=nullptr) {
    R::InvariantSplineSupport proof;QString error;
    if(!R::SupportDescriptor(face).invariantSpline(proof,error,context)) throw std::runtime_error(error.toStdString());
    return proof;
}
void refuse(const TopoDS_Face& face,const char* reason) {
    const auto before=brep(face);R::InvariantSplineSupport proof=recognize(side());QString error;
    check(!R::SupportDescriptor(face).invariantSpline(proof,error)&&proof.face.IsNull()&&proof.basis.IsNull()
        &&!proof.axialContinuation&&!error.isEmpty(),reason);
    check(brep(face)==before,"refused recognition changed source");
}
void recognition() {
    const auto face=side();const auto before=brep(face);const auto proof=recognize(face);
    check(proof.axis.IsEqual(gp_Dir(0,0,1),1.e-8)&&proof.axialContinuation,"extrusion axis/continuation incorrect");
    near(proof.firstU,0,"basis interval start");near(proof.lastU,1,"basis interval end");
    near(proof.firstV,0,"trim interval start");near(proof.lastV,20,"trim interval end");
    check(proof.trims.size()==4&&proof.basis->Degree()==3&&!proof.basis->IsRational(),"exact support data lost");
    near(proof.firstSection.Distance(gp_Pnt(30,10,0)),0,"first section plane incorrect");
    near(proof.lastSection.Distance(gp_Pnt(30,10,20)),0,"last section plane incorrect");
    for(int i=1;i<=proof.trimmedBasis->NbPoles();++i) near(proof.basisPlane.Distance(proof.trimmedBasis->Pole(i)),0,"trimmed basis not planar");
    auto reversed=TopoDS::Face(face.Reversed());const auto reversedProof=recognize(reversed);
    check(reversedProof.face.Orientation()==TopAbs_REVERSED,"Face orientation not retained");
    for(const auto& trim:proof.trims) {
        const auto other=std::find_if(reversedProof.trims.begin(),reversedProof.trims.end(),[&](const auto& candidate){return candidate.edge.IsSame(trim.edge);});
        check(other!=reversedProof.trims.end()&&other->edge.Orientation()!=trim.edge.Orientation(),"reversed edge use orientation lost");
    }
    auto reverseCurve=basis();reverseCurve->Reverse();recognize(side(0,20,0,1,reverseCurve));
    gp_Trsf placement;placement.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),.6);placement.SetTranslationPart(gp_Vec(17,-9,23));
    for(const auto& moved:{TopoDS::Face(face.Moved(TopLoc_Location(placement))),
                          TopoDS::Face(BRepBuilderAPI_Transform(face,placement,true).Shape())}) {
        const auto placed=recognize(moved);
        check(placed.axis.IsEqual(gp_Dir(0,0,1).Transformed(placement),1.e-8),"rigid carrier axis not transformed");
        near(placed.firstSection.Distance(gp_Pnt(30,10,0).Transformed(placement)),0,"located section plane wrong");
    }
    const auto trimmed=recognize(side(2,13,.2,.8));
    near(trimmed.firstU,.2,"finite U trim lost");near(trimmed.lastU,.8,"finite U trim lost");
    near(trimmed.firstV,2,"finite V trim lost");near(trimmed.lastV,13,"finite V trim lost");
    // Exact whole solid context supplies current cap and side adjacency.
    BRepBuilderAPI_MakeWire wire;wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0,20,0),gp_Pnt(60,20,0)).Edge());
    auto edge=BRepBuilderAPI_MakeEdge(basis()).Edge();edge.Reverse();wire.Add(edge);
    const auto profile=BRepBuilderAPI_MakeFace(wire.Wire()).Face();
    const auto solid=BRepPrimAPI_MakePrism(profile,gp_Vec(0,0,20),true,true).Shape();
    R::TopologyIndex topology;QString error;check(R::TopologyIndex::build(solid,topology,error),"context index failed");
    bool found=false;TopoDS_Face actualCap;
    for(const auto& record:topology.faces()) if(record.support.surface.GetType()==GeomAbs_SurfaceOfExtrusion) {
        const auto contextual=recognize(record.support.face,&topology);int caps=0;
        for(const auto& trim:contextual.trims) caps+=int(trim.planarCaps.size());
        check(caps==2,"actual planar cap adjacency not retained");found=true;
    } else if(record.support.surface.GetType()==GeomAbs_Plane
              &&record.support.surface.Plane().Axis().Direction().IsParallel(gp_Dir(0,0,1),1.e-8)
              &&record.support.surface.Plane().Distance(gp_Pnt(30,20,20))<1.e-7) actualCap=record.support.face;
    check(found,"context extrusion wall missing");R::Result refusal;
    check(!actualCap.IsNull()&&!R::movePlanarLocal(solid,actualCap,15,refusal,error)
        &&error.contains("changes cross section"),"recognition enabled spline Move Face");
    check(brep(face)==before,"recognition mutated source");
    std::cout<<"Bounded coefficient proof, domains, rigid locations, orientations and cap adjacency passed\n";
}
void correspondence() {
    const auto whole=side(),same=side(),extended=side(0,35),left=side(0,10),right=side(10,20);
    C::Correspondence map;QString error;
    check(!C::correspond(whole,same,map,error),"legacy analytic correspondence gained spline support");
    const auto before=brep(whole);
    check(C::correspondInvariantSpline(whole,same,map,error)&&map.faces[0].relation==C::FaceRelation::SameRegion,"trimmed identity failed");
    check(C::correspondInvariantSpline(whole,extended,map,error)&&map.faces[0].relation==C::FaceRelation::Modified,"changed axial trims not distinguished");
    check(C::correspondInvariantSpline(whole,side(10,30),map,error)&&map.faces[0].relation==C::FaceRelation::Modified,"equal-area shifted trims treated as identical");
    check(C::correspondInvariantSpline(whole,compound({left,right}),map,error)&&map.faces[0].relation==C::FaceRelation::Split,"axial split failed");
    check(C::correspondInvariantSpline(compound({left,right}),whole,map,error)
        &&std::all_of(map.faces.begin(),map.faces.end(),[](const auto& f){return f.relation==C::FaceRelation::Merged;}),"axial merge failed");
    check(C::correspondInvariantSpline(whole,compound({side(0,20,0,.5),side(0,20,.5,1)}),map,error)
        &&map.faces[0].relation==C::FaceRelation::Split,"basis-interval split failed");
    check(C::correspondInvariantSpline(whole,side(30,40),map,error)&&map.faces[0].relation==C::FaceRelation::Removed
        &&map.faces[0].regions.carriers.size()==1&&map.faces[0].regions.overlaps.empty()&&map.newFaces.size()==1,"carrier/removed/new distinction failed");
    auto reversed=basis();reversed->Reverse();
    check(C::correspondInvariantSpline(whole,side(0,20,0,1,reversed),map,error)&&map.faces[0].relation==C::FaceRelation::SameRegion,"reversed basis geometric relation failed");
    check(C::correspondInvariantSpline(whole,TopoDS::Face(same.Reversed()),map,error)&&map.faces[0].relation==C::FaceRelation::SameRegion,"reversed Face relation failed");
    auto shifted=basis();gp_Trsf axialShift;axialShift.SetTranslation(gp_Vec(0,0,5));shifted->Transform(axialShift);
    check(C::correspondInvariantSpline(whole,side(-5,15,0,1,shifted),map,error)
        &&map.faces[0].relation==C::FaceRelation::SameRegion,"axially shifted basis origin confused with changed trims");
    check(!C::correspondInvariantSpline(whole,compound({same,side()}),map,error)&&map.faces.empty()&&!error.isEmpty(),"duplicate overlapping regions not refused");
    check(!C::correspondInvariantSpline(compound({left,right}),compound({side(0,12),side(12,20)}),map,error)&&map.faces.empty(),"many-to-many ambiguity not refused");
    auto inserted=basis();inserted->InsertKnot(.5,1);
    check(!C::correspondInvariantSpline(whole,side(0,20,0,1,inserted),map,error)&&map.faces.empty()
        &&error.contains("knot insertion"),"unproven knot equivalence accepted");
    check(brep(whole)==before,"correspondence mutated source");
    std::cout<<"Opt-in trimmed, modified, split/merged, removed/new and ambiguous relations passed\n";
}
void orderedOverlapReuse() {
    C::Correspondence mapping;QString error;
    const auto query=[&](const TopoDS_Shape& source,const TopoDS_Shape& target,bool expected) {
        const auto before=brep(source),after=brep(target);
        BRepTiming::reset();BRepTiming::enabled=true;
        const bool ok=C::correspondInvariantSpline(source,target,mapping,error);
        BRepTiming::enabled=false;
        check(ok==expected,"overlap reuse changed correspondence success/refusal");
        check(brep(source)==before&&brep(target)==after,"overlap reuse mutated source/result");
        if(!ok) check(mapping.faces.empty()&&mapping.newFaces.empty()&&!error.isEmpty(),"failure retained a partial mapping");
        return BRepTiming::stats[typeid(BRepAlgoAPI_Common).name()].calls;
    };
    const auto whole=side(),extended=side(0,35);
    check(query(whole,extended,true)==1&&BRepTiming::stats["BRepCertification.overlapReused"].calls==2
        &&mapping.faces.front().relation==C::FaceRelation::Modified,"same ordered pair rebuilt more than once");
    check(query(whole,extended,true)==1,"overlap escaped its invocation");
    const auto lower=side(0,10),upper=side(10,20);
    const auto both=compound({lower,upper});
    // A/B contact is queried by both ambiguity scans and coverage; B/A is
    // deliberately another ordered query. Both self-pairs are distinct too.
    check(query(both,both,true)==4&&BRepTiming::stats["BRepCertification.overlapReused"].calls==6
        &&mapping.faces.size()==2&&std::all_of(mapping.faces.begin(),mapping.faces.end(),[](const auto& f){return f.relation==C::FaceRelation::SameRegion;}),
        "zero-area contact, order or source/target coverage changed");
    check(query(both,both.Reversed(),true)==6&&mapping.faces.size()==2
        &&std::all_of(mapping.faces.begin(),mapping.faces.end(),[](const auto& f){return f.relation==C::FaceRelation::SameRegion;}),
        "opposite orientations aliased ordered query identities");
    gp_Trsf shift;shift.SetTranslation(gp_Vec(0,0,10));
    const auto located=compound({lower,lower.Moved(TopLoc_Location(shift))});
    check(query(whole,located,true)==3&&mapping.faces.front().relation==C::FaceRelation::Split,
        "same carrier/TShape at distinct locations aliased trimmed regions");
    const auto reversed=TopoDS::Face(whole.Reversed());
    check(query(whole,reversed,true)==1&&mapping.faces.front().relation==C::FaceRelation::SameRegion,"reversed Face compatibility changed");
    const auto thinOverlap=side(20-1.e-4,40);
    check(query(whole,thinOverlap,true)==1&&mapping.faces.front().relation==C::FaceRelation::Modified
        &&mapping.faces.front().regions.positiveArea>0,"small positive overlap became empty or identical");
    const auto ambiguous=compound({side(),side()});
    check(query(ambiguous,extended,false)==1,"ambiguous pair was not independently evaluated");
    check(query(whole,extended,true)==1,"failed invocation leaked a cached result");
    std::cout<<"Invocation-local ordered overlap reuse, zero contact, located/reversed trims and refusal isolation passed\n";
}
void turningCurves() {
    auto turning=basis();
    turning->SetPole(1,gp_Pnt(0,20,0));turning->SetPole(2,gp_Pnt(-10,50,0));
    turning->SetPole(3,gp_Pnt(30,50,0));turning->SetPole(4,gp_Pnt(20,20,0));
    const auto face=side(0,20,0,1,turning);const auto before=brep(face);
    recognize(face);recognize(TopoDS::Face(face.Reversed()));
    auto reversed=Handle(Geom_BSplineCurve)::DownCast(turning->Copy());reversed->Reverse();
    recognize(side(0,20,0,1,reversed));
    auto spans=Handle(Geom_BSplineCurve)::DownCast(turning->Copy());
    spans->InsertKnot(.25,1,1.e-12);spans->InsertKnot(.75,1,1.e-12);
    recognize(side(0,20,0,1,spans));
    gp_Trsf rotation;rotation.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),.6);
    recognize(TopoDS::Face(BRepBuilderAPI_Transform(face,rotation,true).Shape()));
    check(brep(face)==before,"turning certificate changed source");
    // x(t)=120t-300t^2+200t^3, y(t)=20+120t(1-t).
    // t=(1 +/- sqrt(.6))/2 are distinct points at the same (10,32).
    auto crossing=Handle(Geom_BSplineCurve)::DownCast(turning->Copy());
    crossing->SetPole(2,gp_Pnt(40,60,0));crossing->SetPole(3,gp_Pnt(-20,60,0));
    refuse(side(0,20,0,1,crossing),"self-intersecting cubic accepted");
    // Derivative vanishes at t=.5: a cusp cannot pass strict regularity.
    auto cusp=Handle(Geom_BSplineCurve)::DownCast(turning->Copy());
    cusp->SetPole(2,gp_Pnt(20,40,0));cusp->SetPole(3,gp_Pnt(0,40,0));
    refuse(side(0,20,0,1,cusp),"singular cubic accepted");
    std::cout<<"Bounded Bezier turning-curve proof, reversed orientation, crossing/cusp refusal passed\n";
}
void unsupported() {
    refuse(side(0,20,0,1,basis(true)),"rational basis accepted");
    auto elevated=basis();elevated->IncreaseDegree(4);refuse(side(0,20,0,1,elevated),"degree >3 accepted");
    auto spatial=basis();spatial->SetPole(2,gp_Pnt(20,24,1));refuse(side(0,20,0,1,spatial),"nonplanar basis accepted");
    auto linear=basis();linear->SetPole(2,gp_Pnt(20,20,0));linear->SetPole(3,gp_Pnt(40,20,0));
    refuse(side(0,20,0,1,linear),"unproven analytic-plane equivalent accepted");
    BRepBuilderAPI_Copy coarse(side(),true,false);const auto coarseFace=TopoDS::Face(coarse.Shape());
    TopExp_Explorer vertex(coarseFace,TopAbs_VERTEX);BRep_Builder toleranceBuilder;
    toleranceBuilder.UpdateVertex(TopoDS::Vertex(vertex.Current()),2.e-6);refuse(coarseFace,"coarse topology tolerance accepted");
    TColgp_Array2OfPnt poles(1,4,1,3);
    for(int i=1;i<=4;++i) for(int j=1;j<=3;++j) poles(i,j)=gp_Pnt(20*(i-1),10*(j-1),(i==2&&j==2)?5:0);
    TColStd_Array1OfReal knots(1,2);knots(1)=0;knots(2)=1;
    TColStd_Array1OfInteger u(1,2),v(1,2);u(1)=u(2)=4;v(1)=v(2)=3;
    const Handle(Geom_BSplineSurface) freeform=new Geom_BSplineSurface(poles,knots,knots,u,v,3,2);
    refuse(BRepBuilderAPI_MakeFace(freeform,0,1,0,1,1.e-7).Face(),"genuine non-extrusion B-spline surface accepted");
    BRepBuilderAPI_NurbsConvert converted(side(),true);
    const auto exact=TopoDS::Face(converted.Shape());R::InvariantSplineSupport proof;QString error;
    check(!R::SupportDescriptor(exact).invariantSpline(proof,error)&&error.contains("B-spline surface"),"generic tensor-product equivalent accepted without proof");
    BRepBuilderAPI_Copy copy(side(),true,false);const auto inconsistent=TopoDS::Face(copy.Shape());
    TopExp_Explorer edge(inconsistent,TopAbs_EDGE);BRep_Builder builder;
    const Handle(Geom2d_Line) bad=new Geom2d_Line(gp_Pnt2d(100,100),gp_Dir2d(1,1));
    builder.UpdateEdge(TopoDS::Edge(edge.Current()),bad,inconsistent,1.e-7);
    refuse(inconsistent,"inconsistent pcurve accepted");
    R::TopologyIndex other;check(R::TopologyIndex::build(side(),other,error),"foreign context setup failed");
    check(!R::SupportDescriptor(side()).invariantSpline(proof,error,&other)&&proof.face.IsNull(),"foreign topology context accepted");
    std::cout<<"Unsupported representations, coefficients, inconsistent pcurves and stale context refusals passed\n";
}
}
int main() {
    try {recognition();correspondence();orderedOverlapReuse();turningCurves();unsupported();return 0;}
    catch(const Standard_Failure& failure) {std::cerr<<failure.GetMessageString()<<'\n';return 1;}
    catch(const std::exception& failure) {std::cerr<<failure.what()<<'\n';return 1;}
}
