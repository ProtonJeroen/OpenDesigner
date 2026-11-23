#include "BRepMeasurement.h"
#include "BRepTiming.h"
#include <BRepGProp.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include "BRepCertification.h"
#include "BRepReconstruction.h"
#include "BooleanGeometry.h"
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepFeat_SplitShape.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array2OfReal.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Circ.hxx>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace M=BRepMeasurement;
namespace C=BRepCertification;
namespace {
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
void near(double actual,double expected,const char* message) {
    if(!std::isfinite(actual)||std::abs(actual-expected)>1.e-8*std::max(1.,std::abs(expected))) {
        std::ostringstream out;out.precision(17);out<<message<<": "<<actual<<" vs "<<expected;throw std::runtime_error(out.str());
    }
}
std::string brep(const TopoDS_Shape& shape) {std::ostringstream out;BRepTools::Write(shape,out);return out.str();}
Handle(Geom_BSplineCurve) arch(bool piecewise=false) {
    if(piecewise) {
        // Two cubic Bezier spans, with a C0 knot: areas are respectively
        // 30*(20+24+26+20)/4=675 and 30*(20+21+23+20)/4=630.
        TColgp_Array1OfPnt poles(1,7);
        const double y[]={20,24,26,20,21,23,20};
        for(int i=1;i<=7;++i) poles(i)=gp_Pnt(10*(i-1),y[i-1],0);
        TColStd_Array1OfReal knots(1,3);knots(1)=0;knots(2)=.5;knots(3)=1;
        TColStd_Array1OfInteger mults(1,3);mults(1)=mults(3)=4;mults(2)=3;
        return new Geom_BSplineCurve(poles,knots,mults,3);
    }
    // Exact cubic x=60t, y=20+12t-6t^2-6t^3. Its area above y=0 is
    // 60*(20+6-2-1.5)=1350, independent of OCCT integration or sampled fits.
    TColgp_Array1OfPnt poles(1,4);poles(1)=gp_Pnt(0,20,0);poles(2)=gp_Pnt(20,24,0);
    poles(3)=gp_Pnt(40,26,0);poles(4)=gp_Pnt(60,20,0);
    TColStd_Array1OfReal knots(1,2);knots(1)=0;knots(2)=1;
    TColStd_Array1OfInteger mults(1,2);mults(1)=4;mults(2)=4;
    return new Geom_BSplineCurve(poles,knots,mults,3);
}
TopoDS_Face profile(bool holes=false,bool piecewise=false) {
    BRepBuilderAPI_MakeWire wire;
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0,0,0),gp_Pnt(60,0,0)).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(60,0,0),gp_Pnt(60,20,0)).Edge());
    auto edge=BRepBuilderAPI_MakeEdge(arch(piecewise)).Edge();edge.Reverse();wire.Add(edge);
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0,20,0),gp_Pnt(0,0,0)).Edge());
    BRepBuilderAPI_MakeFace face(wire.Wire());
    if(holes) for(const auto& hole:{std::pair{15.,3.},std::pair{45.,4.}}) {
        auto inner=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(hole.first,10,0),gp_Dir(0,0,1)),hole.second)).Edge()).Wire();
        inner.Reverse();face.Add(inner);
    }
    return face.Face();
}
TopoDS_Shape prism(bool holes=false) {return BRepPrimAPI_MakePrism(profile(holes),gp_Vec(0,0,20),true,true).Shape();}
void measured(const TopoDS_Shape& shape,double volume,double area=-1) {
    const auto before=brep(shape);M::Result result;QString error;
    if(!M::volume(shape,result,error)) throw std::runtime_error("Volume: "+error.toStdString());
    near(result.properties.Mass(),volume,"shared volume");near(C::volume(shape),volume,"certification volume");
    check(result.relativeError<=1.e-12,"volume error estimate not certified");
    if(area>=0) {
        if(!M::area(shape,result,error)) throw std::runtime_error("Area: "+error.toStdString());
        near(result.properties.Mass(),area,"shared area");near(C::area(shape),area,"certification area");
        check(result.relativeError<=1.e-12,"area error estimate not certified");
    }
    check(brep(shape)==before,"measurement mutated source");
}
void analyticAndEmpty() {
    const auto box=BRepPrimAPI_MakeBox(10,20,30).Shape();measured(box,6000,2200);
    const auto cylinder=BRepPrimAPI_MakeCylinder(10,20).Shape();measured(cylinder,2000*std::numbers::pi,600*std::numbers::pi);
    M::Result result;QString error;check(M::volume(box,result,error)&&!result.adaptive,"analytic fast path changed");
    const auto remote=BRepPrimAPI_MakeBox(gp_Pnt(100,0,0),10,20,30).Shape();
    const auto common=BooleanGeometry::combine<BRepAlgoAPI_Common>(box,remote);
    const auto cut=BooleanGeometry::combine<BRepAlgoAPI_Cut>(box,box);
    measured(common,0,0);measured(cut,0,0);
    double volume=0;check(!C::singleSolid(cut,volume)&&!BooleanGeometry::validSolid(cut,volume),"empty difference became a valid solid");
    check(C::equivalent(common,cut),"empty material equivalence failed");
    const auto touching=BRepPrimAPI_MakeBox(gp_Pnt(0,0,30),10,20,30).Shape();
    const auto contact=BooleanGeometry::combine<BRepAlgoAPI_Common>(box,touching);measured(contact,0);
    // OCCT may omit lower-dimensional contacts from solid Common. Exercise a
    // retained contact Face explicitly rather than depending on that choice.
    const auto contactFace=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,30),gp_Dir(0,0,1)),0,10,0,20).Face();
    measured(contactFace,0,200);
    BRep_Builder builder;TopoDS_Compound mixed;builder.MakeCompound(mixed);builder.Add(mixed,box);builder.Add(mixed,contactFace);
    measured(mixed,6000); // a loose face contact must not add signed volume
    std::cout<<"Analytic solids, empty sets and lower-dimensional Common contacts passed\n";
}
void splines() {
    const auto solid=prism(),withHoles=prism(true);const auto section=profile(),holedSection=profile(true);
    measured(section,0,1350);measured(holedSection,0,1350-25*std::numbers::pi);
    measured(solid,27000);measured(withHoles,27000-500*std::numbers::pi);
    const auto spans=profile(false,true);measured(spans,0,1305);
    measured(BRepPrimAPI_MakePrism(spans,gp_Vec(0,0,20),true,true).Shape(),26100);
    M::Result result;QString error;check(M::area(section,result,error)&&result.adaptive,"planar spline trims did not select adaptive integration");
    near(result.properties.CentreOfMass().X(),452./15,"independent cubic area centroid");
    check(M::volume(solid,result,error),"cubic centroid integration failed");
    near(result.properties.CentreOfMass().X(),452./15,"independent cubic volume centroid");
    near(result.properties.CentreOfMass().Z(),10,"extrusion volume centroid height");
    double volume=0;check(C::singleSolid(solid,volume)&&BooleanGeometry::validSolid(solid,volume),"spline positive-solid policy failed");
    near(volume,27000,"Boolean/direct spline measurements disagree");
    check(C::occupancy(withHoles,gp_Pnt(15,10,10),1.e-7)==TopAbs_OUT
       &&C::occupancy(withHoles,gp_Pnt(45,10,10),1.e-7)==TopAbs_OUT
       &&C::occupancy(withHoles,gp_Pnt(30,10,10),1.e-7)==TopAbs_IN,"two-hole material occupancy wrong");
    auto independent=solid;
    for(const auto& hole:{std::pair{15.,3.},std::pair{45.,4.}})
        independent=BooleanGeometry::combine<BRepAlgoAPI_Cut>(independent,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(hole.first,10,0),gp_Dir(0,0,1)),hole.second,20).Shape());
    measured(independent,27000-500*std::numbers::pi);
    check(C::equivalent(withHoles,independent),"equivalent profile and Boolean hole routes differ");
    const auto cavity=BRepPrimAPI_MakeBox(gp_Pnt(27,5,5),6,4,5).Shape();
    const auto hollow=BooleanGeometry::combine<BRepAlgoAPI_Cut>(withHoles,cavity);
    measured(hollow,27000-500*std::numbers::pi-120);
    check(BooleanGeometry::validSolid(hollow,volume)&&!C::singleSolid(hollow,volume),"cavity/single-shell policy collapsed");
    check(!C::equivalent(hollow,withHoles),"missing internal cavity accepted");
    gp_Trsf shift;shift.SetTranslation(gp_Vec(100,0,0));const auto translated=BRepBuilderAPI_Transform(solid,shift,true).Shape();
    measured(translated,27000);check(!C::equivalent(solid,translated),"equal volume substituted for material occupancy");
    const auto before=brep(solid);BRepBuilderAPI_Copy copy(solid,true,false);TopoDS_Face top;
    for(TopExp_Explorer it(copy.Shape(),TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());BRepAdaptor_Surface s(face);
        if(s.GetType()==GeomAbs_Plane&&s.Plane().Distance(gp_Pnt(30,10,20))<1.e-7
           &&s.Plane().Axis().Direction().IsParallel(gp_Dir(0,0,1),1.e-8)) top=face;
    }
    check(!top.IsNull(),"split fixture cap missing");BRepFeat_SplitShape split(copy.Shape());
    split.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(30,0,20),gp_Pnt(30,23.75,20)).Edge(),top);split.Build();
    check(split.IsDone(),"split fixture failed");measured(split.Shape(),27000);
    check(C::equivalent(solid,split.Shape())&&brep(solid)==before,"valid split topology changed material/source");
    TopoDS_Shape normalized;check(BRepReconstruction::normalize(split.Shape(),normalized,error,true),"split normalization refused valid spline material");
    measured(normalized,27000);check(C::equivalent(solid,normalized),"normalization changed spline material");
    gp_Trsf placement;placement.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),.4);placement.SetTranslationPart(gp_Vec(13,-7,21));
    measured(withHoles.Moved(TopLoc_Location(placement)),27000-500*std::numbers::pi);
    TopoDS_Face actualTop;
    for(TopExp_Explorer it(solid,TopAbs_FACE);it.More();it.Next())
        if(C::sameCarrier(TopoDS::Face(it.Current()),top)) actualTop=TopoDS::Face(it.Current());
    check(!actualTop.IsNull(),"actual member cap missing");BRepReconstruction::Result refused;
    check(!BRepReconstruction::movePlanarLocal(solid,actualTop,15,refused,error)
       &&error.contains("changes cross section"),"measurement enabled spline Move Face");
    std::cout<<"Exact cubic area 1350; volume 27000; two holes, cavity, split topology and material equivalence passed\n";
}
void directKnotSelection() {
    const auto single=prism();
    const auto multiple=BRepPrimAPI_MakePrism(profile(false,true),gp_Vec(0,0,20),true,true).Shape();
    const auto checkPath=[&](const TopoDS_Shape& shape,double expected,bool direct) {
        const auto before=brep(shape);M::Result result;QString error;
        BRepTiming::reset();BRepTiming::enabled=true;BRepTiming::detailed=true;
        const bool ok=M::volume(shape,result,error);BRepTiming::enabled=false;
        check(ok,error.toStdString().c_str());near(result.properties.Mass(),expected,"selected path polynomial volume");
        check(result.relativeError>=0&&result.relativeError<=1.e-12,"selected path insufficient precision");
        int directCalls=0,rawCalls=0,retries=0;
        for(const auto& entry:BRepTiming::details) {
            directCalls+=entry.kind=="Integrate.Volume.GK.Direct";
            rawCalls+=entry.kind=="Integrate.Volume.GK";
            retries+=entry.kind=="Integrate.Volume.GK.Retry";
        }
        check(directCalls==(direct?1:0)&&rawCalls==(direct?0:1)&&retries==0,"knot path selection or retry count changed");
        // Independently invoke OCCT on an exact integration-only conversion.
        // Compare measured properties as well as the polynomial mass oracle.
        BRepBuilderAPI_Copy copy(shape,true,false);BRepBuilderAPI_NurbsConvert spans(copy.Shape(),true);
        GProp_GProps oracle;const double estimate=BRepGProp::VolumePropertiesGK(spans.Shape(),oracle,1.e-12,false,true,true);
        check(std::isfinite(estimate)&&estimate>=0&&estimate<=1.e-12,"independent span estimate refused");
        near(result.properties.Mass(),oracle.Mass(),"span measurement disagreement");
        check(result.properties.CentreOfMass().Distance(oracle.CentreOfMass())<1.e-9,"span centroid disagreement");
        check(brep(shape)==before,"knot path mutated source");
        BRepTiming::detailed=false;BRepTiming::reset();
    };
    checkPath(single,27000,false);checkPath(multiple,26100,true);
    // Trim fully inside a single polynomial span. An internal knot in the
    // untrimmed basis alone must not select the conversion path.
    auto basis=arch(true);BRepBuilderAPI_MakeWire boundary;
    boundary.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0,0,0),gp_Pnt(24,0,0)).Edge());
    boundary.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(24,0,0),basis->Value(.4)).Edge());
    auto trimmedEdge=BRepBuilderAPI_MakeEdge(basis,0,.4).Edge();trimmedEdge.Reverse();boundary.Add(trimmedEdge);
    boundary.Add(BRepBuilderAPI_MakeEdge(basis->Value(0),gp_Pnt(0,0,0)).Edge());
    const auto trimmed=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(boundary.Wire()).Face(),gp_Vec(0,0,20),true,true).Shape();
    // First span over t=0..0.8: 30*(20t+6t²-2t³-1.5t⁴)=546.048.
    checkPath(trimmed,10920.96,false);
    const auto holes=BRepPrimAPI_MakePrism(profile(true,true),gp_Vec(0,0,20),true,true).Shape();
    measured(holes,26100-500*std::numbers::pi);
    auto independentlyCut=multiple;
    for(const auto& hole:{std::pair{15.,3.},std::pair{45.,4.}})
        independentlyCut=BooleanGeometry::combine<BRepAlgoAPI_Cut>(independentlyCut,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(hole.first,10,0),gp_Dir(0,0,1)),hole.second,20).Shape());
    check(C::equivalent(holes,independentlyCut),"direct multi-span hole routes disagree");
    const auto cavity=BooleanGeometry::combine<BRepAlgoAPI_Cut>(holes,BRepPrimAPI_MakeBox(gp_Pnt(27,5,5),6,4,5).Shape());
    measured(cavity,26100-500*std::numbers::pi-120);
    check(!C::equivalent(cavity,holes),"direct multi-span missing cavity accepted");
    gp_Trsf move;move.SetTranslation(gp_Vec(100,0,0));
    const auto relocated=BRepBuilderAPI_Transform(multiple,move,true).Shape();
    measured(relocated,26100);
    check(!C::equivalent(multiple,relocated),"equal-volume disjoint multi-span material accepted");
    std::cout<<"Single-span fast path and direct multi-span volume, error estimates, centroids and immutability passed\n";
}
void trimmedAndRefused() {
    // Exact rational quarter-cylinder represented as a tensor-product B-spline.
    TColgp_Array2OfPnt poles(1,3,1,2);TColStd_Array2OfReal weights(1,3,1,2);
    for(int j=1;j<=2;++j) {
        poles(1,j)=gp_Pnt(20,0,20*(j-1));poles(2,j)=gp_Pnt(20,20,20*(j-1));poles(3,j)=gp_Pnt(0,20,20*(j-1));
        weights(1,j)=weights(3,j)=1;weights(2,j)=std::sqrt(.5);
    }
    TColStd_Array1OfReal knots(1,2);knots(1)=0;knots(2)=1;
    TColStd_Array1OfInteger um(1,2),vm(1,2);um(1)=um(2)=3;vm(1)=vm(2)=2;
    const Handle(Geom_BSplineSurface) surface=new Geom_BSplineSurface(poles,weights,knots,knots,um,vm,2,1);
    const auto face=BRepBuilderAPI_MakeFace(surface,0,1,.25,.75,1.e-7).Face();
    measured(face,0,100*std::numbers::pi);
    const Handle(Geom_SurfaceOfLinearExtrusion) unbounded=new Geom_SurfaceOfLinearExtrusion(arch(),gp_Dir(0,0,1));
    const auto infiniteFace=BRepBuilderAPI_MakeFace(unbounded,1.e-7).Face();
    M::Result result;QString error;
    check(M::area(face,result,error),"trimmed spline area failed");
    check(!M::area(infiniteFace,result,error)&&result.properties.Mass()==0&&!error.isEmpty(),"unbounded spline area accepted or stale output retained");
    check(!M::volume({},result,error)&&result.properties.Mass()==0&&!error.isEmpty(),"null geometry treated as valid empty material");
    BRep_Builder builder;TopoDS_Shell shell;builder.MakeShell(shell);builder.Add(shell,face);
    const auto open=BRepBuilderAPI_MakeSolid(shell).Shape();
    check(!M::volume(open,result,error)&&result.properties.Mass()==0&&!error.isEmpty(),"open invalid solid accepted as empty difference");
    std::cout<<"Trimmed B-spline surface analytic area and explicit unsupported/invalid refusals passed\n";
}
}
int main() {
    try {analyticAndEmpty();splines();directKnotSelection();trimmedAndRefused();return 0;}
    catch(const Standard_Failure& failure) {std::cerr<<failure.GetMessageString()<<'\n';return 1;}
    catch(const std::exception& failure) {std::cerr<<failure.what()<<'\n';return 1;}
}
