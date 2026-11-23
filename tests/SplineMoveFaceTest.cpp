#include "SplineExtrusionFixture.h"
#include "BRepCertification.h"
#include "BRepReconstruction.h"
#include "MoveFaceGeometry.h"
#include "BRepTiming.h"
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepTools.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include "BooleanGeometry.h"
#include <sstream>
#include <iostream>
#include <stdexcept>
namespace C=BRepCertification;
namespace R=BRepReconstruction;
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void near(double a,double b,const char* message) {check(std::abs(a-b)<1.e-8*std::max(1.,std::abs(b)),message);}
std::string brep(const TopoDS_Shape& s) {std::ostringstream o;BRepTools::Write(s,o);return o.str();}
TopoDS_Shape turningSolid(double height) {
    auto c=SplineFixture::curve(true);
    c->SetPole(1,gp_Pnt(0,20,0));c->SetPole(2,gp_Pnt(-10,50,0));
    c->SetPole(3,gp_Pnt(30,50,0));c->SetPole(4,gp_Pnt(20,20,0));
    BRepBuilderAPI_MakeWire wire;wire.Add(BRepBuilderAPI_MakeEdge(c).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(20,20,0),gp_Pnt(0,20,0)).Edge());
    return BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(wire.Wire()).Face(),gp_Vec(0,0,height),true,true).Shape();
}
void turningMove() {
    const auto source=turningSolid(20);const auto before=brep(source);QString error;
    const auto top=SplineFixture::cap(source,20);gp_Pnt center;gp_Dir normal;
    BRepTiming::reset();BRepTiming::enabled=true;
    const bool eligible=MoveFaceGeometry::referenceForMove(source,top,center,normal,error);
    BRepTiming::enabled=false;check(eligible,"turning Move reference refused");
    check(BRepTiming::stats["BRepCertification.singleSolid"].calls==0,"eligibility integrated source volume");
    // Integral of y*x' for x=-30t+150t²-100t³, y-20=90t(1-t)
    // is 450, independent of OCCT integration and reconstruction.
    near(C::volume(source),450*20,"turning polynomial source volume");
    for(double delta:{15.,-5.}) {
        const auto result=MoveFaceGeometry::move(source,top,delta,error,MoveFaceGeometry::ReconstructionPolicy::AxialInvariantCap);
        check(!result.IsNull(),error.toStdString().c_str());
        near(C::volume(result),450*(20+delta),"turning polynomial result volume");
        check(C::manifold(result)&&C::withinTolerance(result,1.e-6),"turning topology");
        check(C::equivalent(result,turningSolid(20+delta)),"turning material oracle");
        check(C::sameTrimmedRegion(SplineFixture::cap(source,0),SplineFixture::cap(result,0)),"turning bottom changed");
    }
    const auto generic=BRepBuilderAPI_NurbsConvert(source,true).Shape();const auto saved=brep(generic);
    BRepTiming::reset();BRepTiming::enabled=true;
    const auto refused=MoveFaceGeometry::move(generic,SplineFixture::cap(generic,20),15,error,MoveFaceGeometry::ReconstructionPolicy::AxialInvariantCap);
    BRepTiming::enabled=false;
    check(refused.IsNull()&&BRepTiming::stats["BRepCertification.singleSolid"].calls==0,"unsupported representation did expensive validation");
    check(brep(generic)==saved&&brep(source)==before,"turning/refused source changed");
}
int main() {
 try {
    turningMove();
    QString error;const auto source=SplineFixture::solid();const auto before=brep(source);
    const auto top=SplineFixture::cap(source,20);near(C::area(top),SplineFixture::area,"polynomial section area");
    near(C::volume(source),8400,"source polynomial volume");
    const auto policy=MoveFaceGeometry::ReconstructionPolicy::AxialInvariantCap;
    for(double distance:{15.,-5.}) {
        R::Result result;
        if(!R::movePlanarLocal(source,top,distance,result,error,true)) throw std::runtime_error(error.toStdString());
        const double height=20+distance;near(C::volume(result.solid),SplineFixture::area*height,"area times height");
        check(C::validBRep(result.solid)&&C::manifold(result.solid)&&C::withinTolerance(result.solid,1.e-6),"topology certificate");
        check(C::equivalent(result.solid,SplineFixture::solid(height)),"independent material prism");
        near(BRepAdaptor_Surface(result.target).Plane().Location().Z(),height,"target height");
        check(C::sameTrimmedRegion(SplineFixture::cap(source,0),SplineFixture::cap(result.solid,0)),"bottom changed");
        for(TopExp_Explorer old(top,TopAbs_EDGE);old.More();old.Next()) {
            const auto original=TopoDS::Edge(old.Current());const BRepAdaptor_Curve a(original);
            check(a.GetType()==GeomAbs_BSplineCurve,"fixture cap is not entirely exact splines");
            bool matched=false;
            for(TopExp_Explorer edge(result.target,TopAbs_EDGE);edge.More();edge.Next()) {
                const auto current=TopoDS::Edge(edge.Current());const BRepAdaptor_Curve b(current);
                if(b.GetType()!=GeomAbs_BSplineCurve) continue;
                const auto x=a.BSpline(),y=b.BSpline();
                if(x->Degree()!=y->Degree()||x->NbPoles()!=y->NbPoles()||x->NbKnots()!=y->NbKnots()
                   ||original.Orientation()!=current.Orientation()) continue;
                bool equal=std::abs(a.FirstParameter()-b.FirstParameter())<1.e-12&&std::abs(a.LastParameter()-b.LastParameter())<1.e-12;
                for(int i=1;i<=x->NbKnots();++i) equal=equal&&x->Multiplicity(i)==y->Multiplicity(i)&&std::abs(x->Knot(i)-y->Knot(i))<1.e-12;
                for(int i=1;i<=x->NbPoles();++i) equal=equal&&x->Pole(i).Translated(gp_Vec(0,0,distance)).Distance(y->Pole(i))<1.e-10;
                matched=matched||equal;
            }
            check(matched,"translated cap coefficients, trims or edge orientation changed");
        }

        const auto region=BRepPrimAPI_MakePrism(top,gp_Vec(0,0,distance),true,true).Shape();
        check(C::preservedOutside(source,result.solid,region),"protected material changed");
        C::MaterialDifference difference;check(C::difference(source,result.solid,difference,error),"material difference failed");
        check(C::equivalent(distance>0?difference.added:difference.removed,region),"directional change differs");
        near(C::volume(distance>0?difference.removed:difference.added),0,"opposite material change");
        for(double z:{1.,height/2,height-1}) {
            check(C::occupancy(result.solid,gp_Pnt(30,20,z),1.e-7)==TopAbs_IN,"missing interior");
            check(C::occupancy(result.solid,gp_Pnt(30,30,z),1.e-7)==TopAbs_OUT,"extra exterior");
        }
        for(const auto& mapping:result.history) {
            check(!mapping.modified.empty(),"missing Face correspondence");
            if(BRepAdaptor_Surface(mapping.original).GetType()!=GeomAbs_SurfaceOfExtrusion) continue;
            R::InvariantSplineSupport a;check(R::SupportDescriptor(mapping.original).invariantSpline(a,error),"source proof");
            for(const auto& face:mapping.modified) {
                R::InvariantSplineSupport b;check(R::SupportDescriptor(face).invariantSpline(b,error),"result proof");
                check(a.basis->Degree()==b.basis->Degree()&&a.basis->NbPoles()==b.basis->NbPoles()&&a.basis->NbKnots()==b.basis->NbKnots(),"spline representation changed");
                for(int i=1;i<=a.basis->NbKnots();++i) {
                    near(a.basis->Knot(i),b.basis->Knot(i),"knot changed");check(a.basis->Multiplicity(i)==b.basis->Multiplicity(i),"multiplicity changed");
                }
                for(int i=1;i<=a.basis->NbPoles();++i) {
                    near(a.basis->Pole(i).X(),b.basis->Pole(i).X(),"coefficient x");near(a.basis->Pole(i).Y(),b.basis->Pole(i).Y(),"coefficient y");
                }
                C::Correspondence relation;check(C::correspondInvariantSpline(mapping.original,face,relation,error)&&(relation.faces[0].relation==C::FaceRelation::Modified||relation.faces[0].relation==C::FaceRelation::SameRegion),"changed trim correspondence");
            }
        }
        const auto routed=MoveFaceGeometry::move(source,top,distance,error,policy);
        check(!routed.IsNull()&&C::equivalent(routed,result.solid),"Move Face routing");
    }
    for(double distance:{-20.,-21.}) check(MoveFaceGeometry::move(source,top,distance,error,policy).IsNull(),"invalid height accepted");
    check(MoveFaceGeometry::move(source,top,15,error).IsNull(),"default/Pull policy enabled spline");
    for(TopExp_Explorer it(source,TopAbs_FACE);it.More();it.Next()) if(BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType()!=GeomAbs_Plane)
        check(MoveFaceGeometry::move(source,TopoDS::Face(it.Current()),15,error,policy).IsNull(),"nonaxial wall motion accepted");
    const auto hole=BooleanGeometry::combine<BRepAlgoAPI_Cut>(source,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(30,20,0),gp_Dir(0,0,1)),1,20).Shape());
    const auto holeBefore=brep(hole);
    check(MoveFaceGeometry::move(hole,SplineFixture::cap(hole,20),15,error,policy).IsNull()&&brep(hole)==holeBefore,"hole capability enabled or source changed");
    const auto nurbs=BRepBuilderAPI_NurbsConvert(source,true).Shape();const auto snapshot=brep(nurbs);
    check(MoveFaceGeometry::move(nurbs,SplineFixture::cap(nurbs,20),15,error,policy).IsNull(),"generic spline surface accepted");
    check(brep(nurbs)==snapshot&&brep(source)==before,"source mutated");
    std::cout<<"Spline axial growth/shortening, polynomial volume, exact coefficients, material, correspondence and refusals passed\n";
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
