#include "CylindricalMoveGeometry.h"
#include "BRepReconstruction.h"
#include "BooleanGeometry.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepFeat_SplitShape.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Circ.hxx>
#include <gp_Trsf.hxx>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>

using namespace CylindricalMoveGeometry;
namespace {
constexpr double pi=std::numbers::pi;
void require(bool condition,const std::string& message) {if(!condition) throw std::runtime_error(message);}
void near(double a,double b,const char* message) {require(std::abs(a-b)<1.e-6*std::max(1.,std::abs(b)),message);}
TopoDS_Face mantle(const TopoDS_Shape& shape) {
    for(TopExp_Explorer f(shape,TopAbs_FACE);f.More();f.Next())
        if(BRepAdaptor_Surface(TopoDS::Face(f.Current())).GetType()==GeomAbs_Cylinder) return TopoDS::Face(f.Current());
    return {};
}
TopoDS_Shape block() {return BRepPrimAPI_MakeBox(100,80,30).Shape();}
TopoDS_Shape fixture() {
    return BooleanGeometry::combine<BRepAlgoAPI_Cut>(block(),
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(30,40,-5),gp_Dir(0,0,1)),10,40).Shape());
}
std::string serialized(const TopoDS_Shape& s) {std::ostringstream out;BRepTools::Write(s,out);return out.str();}
double volume(const TopoDS_Shape& s) {GProp_GProps p;BRepGProp::VolumeProperties(s,p);return p.Mass();}
void occupancy(const TopoDS_Shape& shape,double cx,double cy,const gp_Trsf& transform=gp_Trsf()) {
    // Independent analytic membership of the box minus a disk, at points
    // away from surfaces; includes the filled old footprint and removed new one.
    for(int i=-1;i<=21;++i) for(int j=-1;j<=17;++j) for(double z:{-1.,7.,23.,31.}) {
        const double x=i*5.+.37,y=j*5.+.61;
        const double r2=(x-cx)*(x-cx)+(y-cy)*(y-cy);
        if(std::abs(r2-100)<1.e-5) continue;
        const bool in=x>0&&x<100&&y>0&&y<80&&z>0&&z<30&&r2>100;
        BRepClass3d_SolidClassifier c(shape,gp_Pnt(x,y,z).Transformed(transform),1.e-7);
        require(c.State()==(in?TopAbs_IN:TopAbs_OUT),"Analytic material occupancy mismatch");
    }
}
void certified(const TopoDS_Shape& shape,double expected) {
    double v=0;require(BRepReconstruction::validSolid(shape,v),"Result is not a certified connected solid");near(v,expected,"Wrong volume");
    TopTools_IndexedMapOfShape edges;TopExp::MapShapes(shape,TopAbs_EDGE,edges);
    for(int e=1;e<=edges.Extent();++e) {
        int uses=0;
        for(TopExp_Explorer f(shape,TopAbs_FACE);f.More();f.Next())
            for(TopExp_Explorer it(f.Current(),TopAbs_EDGE);it.More();it.Next()) if(it.Current().IsSame(edges(e))) ++uses;
        require(uses==2&&!BRep_Tool::Degenerated(TopoDS::Edge(edges(e))),"Nonmanifold edge");
    }
}
void moveCase(double dx,double dy,double expected,Code code=Code::Moved,const gp_Trsf& transform=gp_Trsf(),bool located=false) {
    auto source=fixture();
    if(located) source.Location(TopLoc_Location(transform));
    else source=BRepBuilderAPI_Transform(source,transform,true).Shape();
    const auto before=serialized(source);
    const auto result=moveThroughHole(source,mantle(source),gp_Vec(dx,dy,0).Transformed(transform));
    require(bool(result),"Move refused: "+result.message);require(result.code==code,"Unexpected success diagnostic");
    certified(result.shape,expected);occupancy(result.shape,30+dx,40+dy,transform);
    near(result.radius,10,"Changed hole radius");near(result.height,30,"Changed axial span");
    require(result.axis.Direction().IsParallel(gp_Dir(0,0,1).Transformed(transform),1.e-8),"Changed axis direction");
    const gp_Ax1 expectedAxis(gp_Pnt(30+dx,40+dy,0).Transformed(transform),gp_Dir(0,0,1).Transformed(transform));
    require(result.axis.IsCoaxial(expectedAxis,1.e-8,1.e-6),"Wrong translated axis");
    int cylinders=0;
    for(TopExp_Explorer f(result.shape,TopAbs_FACE);f.More();f.Next()) {
        const BRepAdaptor_Surface s(TopoDS::Face(f.Current()));
        if(s.GetType()==GeomAbs_Cylinder) {++cylinders;near(s.Cylinder().Radius(),10,"Wrong result cylinder radius");
            require(s.Cylinder().Axis().IsCoaxial(expectedAxis,1.e-8,1.e-6),"Wrong result cylinder axis");}
    }
    require(code==Code::HoleExited?cylinders==0:cylinders>0,"Incorrect cylindrical target survival");
    require(serialized(source)==before,"Source B-Rep mutated");
}
void splitMantleCase(bool axial) {
    const auto source=fixture();
    const auto face=mantle(source);
    // Split with a circular section or a generator opposite the existing seam.
    // Recognition must handle both adjacent bands and angular patches, ignoring
    // internal edges while assembling the two exterior rims.
    BRepFeat_SplitShape split(source);
    if(axial) split.Add(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(30,40,15),gp_Dir(0,0,1)),10)).Edge(),face);
    else split.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(20,40,0),gp_Pnt(20,40,30)).Edge(),face);
    split.Build();require(split.IsDone(),"Could not create split mantle fixture");
    const auto shape=split.Shape();const auto before=serialized(shape);
    int cylinders=0;
    for(TopExp_Explorer f(shape,TopAbs_FACE);f.More();f.Next())
        if(BRepAdaptor_Surface(TopoDS::Face(f.Current())).GetType()==GeomAbs_Cylinder) ++cylinders;
    require(cylinders==2,"Fixture did not split mantle");
    const auto result=moveThroughHole(shape,mantle(shape),gp_Vec(25,5,0));
    require(bool(result),"Split mantle refused: "+result.message);
    certified(result.shape,240000-3000*pi);occupancy(result.shape,55,45);
    require(serialized(shape)==before,"Split mantle source mutated");
}
void refused(const TopoDS_Shape& source,const TopoDS_Face& face,const gp_Vec& delta,Status status) {
    const auto before=serialized(source);const auto result=moveThroughHole(source,face,delta);
    require(!result&&result.shape.IsNull(),"Refusal returned geometry");
    require(result.status==status,"Wrong refusal category: "+result.message);
    require(!result.message.empty(),"Missing refusal diagnostic");require(serialized(source)==before,"Refusal mutated source");
}
TopoDS_Shape bossFixture() {
    return BooleanGeometry::combine<BRepAlgoAPI_Fuse>(block(),
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(30,40,30),gp_Dir(0,0,1)),10,20).Shape());
}
void bossOccupancy(const TopoDS_Shape& shape,double cx,double cy,BossBoundaryMode policy,
                   const gp_Trsf& transform=gp_Trsf()) {
    for(int i=-3;i<=25;++i) for(int j=-3;j<=21;++j) for(double z:{-1.,7.,29.,31.,43.,49.,51.}) {
        const double x=i*5.+.37,y=j*5.+.61;
        const double r2=(x-cx)*(x-cx)+(y-cy)*(y-cy);
        if(std::abs(r2-100)<1.e-5) continue;
        const bool inFootprint=x>0&&x<100&&y>0&&y<80;
        const bool inStock=inFootprint&&z>0&&z<30;
        const bool inBoss=r2<100&&z>30&&z<50&&(policy==BossBoundaryMode::Extend||inFootprint);
        BRepClass3d_SolidClassifier c(shape,gp_Pnt(x,y,z).Transformed(transform),1.e-7);
        require(c.State()==(inStock||inBoss?TopAbs_IN:TopAbs_OUT),"Boss analytic material occupancy mismatch");
    }
}
TopoDS_Shape bossCase(double dx,double dy,BossBoundaryMode policy,double expected,
                      const gp_Trsf& transform=gp_Trsf(),bool located=false) {
    auto source=bossFixture();
    if(located) source.Location(TopLoc_Location(transform));
    else source=BRepBuilderAPI_Transform(source,transform,true).Shape();
    const auto before=serialized(source);
    const auto result=moveBoss(source,mantle(source),gp_Vec(dx,dy,0).Transformed(transform),policy);
    require(bool(result),"Boss move refused: "+result.message);
    require(result.code==(dx==0&&dy==0?Code::Unchanged:Code::Moved),"Unexpected boss success code");
    require(result.message.find(policy==BossBoundaryMode::Trim?"Trim":"Extend")!=std::string::npos,"Missing explicit policy diagnostic");
    certified(result.shape,expected);bossOccupancy(result.shape,30+dx,40+dy,policy,transform);
    near(result.radius,10,"Changed boss radius");near(result.height,20,"Changed boss height");
    const gp_Ax1 axis(gp_Pnt(30+dx,40+dy,30).Transformed(transform),gp_Dir(0,0,1).Transformed(transform));
    require(result.axis.IsCoaxial(axis,1.e-8,1.e-6),"Wrong translated boss axis");
    double mantleArea=0,capArea=0;
    for(TopExp_Explorer it(result.shape,TopAbs_FACE);it.More();it.Next()) {
        const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);
        GProp_GProps p;BRepGProp::SurfaceProperties(f,p);
        if(s.GetType()==GeomAbs_Cylinder) {
            near(s.Cylinder().Radius(),10,"Wrong final boss radius");
            require(s.Cylinder().Axis().IsCoaxial(axis,1.e-8,1.e-6),"Wrong final boss cylinder");
            mantleArea+=p.Mass();
        } else if(s.GetType()==GeomAbs_Plane&&s.Plane().Axis().Direction().IsParallel(axis.Direction(),1.e-8)&&
                  s.Plane().Distance(gp_Pnt(30+dx,40+dy,50).Transformed(transform))<1.e-6)
            capArea+=p.Mass();
    }
    require(mantleArea>0,"Lost boss curved wall");
    if(policy==BossBoundaryMode::Extend) {near(mantleArea,400*pi,"Extend truncated the mantle");near(capArea,100*pi,"Extend truncated the cap");}
    require(serialized(source)==before,"Boss source mutated");return result.shape;
}
void bossRefused(const TopoDS_Shape& source,const TopoDS_Face& face,const gp_Vec& delta,
                 BossBoundaryMode policy,Status status,Code code) {
    const auto before=serialized(source);const auto result=moveBoss(source,face,delta,policy);
    require(!result&&result.shape.IsNull(),"Boss refusal returned geometry");
    require(result.status==status&&result.code==code,"Wrong boss diagnostic: "+result.message);
    require(!result.message.empty(),"Missing boss error");require(serialized(source)==before,"Boss refusal mutated input");
}
void bossTests() {
    const double full=240000+2000*pi,half=240000+1000*pi,quarter=240000+500*pi;
    const double segment=100*std::acos(.5)-5*std::sqrt(75.);
    for(auto policy:{BossBoundaryMode::Trim,BossBoundaryMode::Extend}) {
        bossCase(0,0,policy,full);
        bossCase(70,0,policy,policy==BossBoundaryMode::Trim?half:full);
        bossCase(-30,0,policy,policy==BossBoundaryMode::Trim?half:full);
        bossCase(75,0,policy,policy==BossBoundaryMode::Trim?240000+20*segment:full);
        bossCase(70,40,policy,policy==BossBoundaryMode::Trim?quarter:full);
        bossCase(-22,-32,policy,policy==BossBoundaryMode::Trim?full-20*(200*std::acos(.8)-16*6):full);
        // Last case crosses two sides near a corner; the discarded segments do
        // not overlap because the corner lies outside the circle.
        gp_Trsf transform;transform.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),.71);
        transform.SetTranslationPart(gp_Vec(210,-130,73));
        bossCase(70,0,policy,policy==BossBoundaryMode::Trim?half:full,transform);
        bossCase(75,0,policy,policy==BossBoundaryMode::Trim?240000+20*segment:full,transform,true);
        const auto source=bossFixture();const auto face=mantle(source);
        for(double dx:{80.,100.}) // tangent line contact and full separation
            bossRefused(source,face,gp_Vec(dx,0,0),policy,Status::Failure,Code::DisconnectedBoss);
        bossRefused(source,face,gp_Vec(3,0,1),policy,Status::InvalidInput,Code::InvalidDisplacement);
        bossRefused(source,face,gp_Vec(std::numeric_limits<double>::quiet_NaN(),0,0),policy,Status::InvalidInput,Code::InvalidDisplacement);
        bossRefused(source,{},gp_Vec(3,0,0),policy,Status::Unsupported,Code::UnsupportedFeature);
        const auto standalone=BRepPrimAPI_MakeCylinder(10,20).Shape();
        bossRefused(standalone,mantle(standalone),gp_Vec(3,0,0),policy,Status::Unsupported,Code::UnsupportedFeature);
        const auto hole=fixture();
        bossRefused(hole,mantle(hole),gp_Vec(3,0,0),policy,Status::Unsupported,Code::UnsupportedFeature);
        // A partial source boss is deliberately outside initial recognition.
        const auto partial=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(block(),
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,40,30),gp_Dir(0,0,1)),10,20).Shape());
        bossRefused(partial,mantle(partial),gp_Vec(3,0,0),policy,Status::Unsupported,Code::UnsupportedStock);
        const auto two=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(source,
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(70,40,30),gp_Dir(0,0,1)),5,20).Shape());
        bossRefused(two,mantle(two),gp_Vec(3,0,0),policy,Status::Unsupported,Code::UnsupportedStock);
    }
    const auto trim=bossCase(25,5,BossBoundaryMode::Trim,full);
    const auto extend=bossCase(25,5,BossBoundaryMode::Extend,full);
    require(BRepReconstruction::equivalent(trim,extend),"Interior boss policies differ");
    const auto source=bossFixture();
    bossRefused(source,mantle(source),gp_Vec(1,0,0),static_cast<BossBoundaryMode>(99),Status::InvalidInput,Code::InvalidPolicy);
    // Surface parameterization may point from the cap toward the support.
    // Recognition must infer the attachment side, without reversing the axis.
    const auto reversed=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(block(),
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(30,40,50),gp_Dir(0,0,-1)),10,20).Shape());
    const auto reversedBefore=serialized(reversed);
    for(auto policy:{BossBoundaryMode::Trim,BossBoundaryMode::Extend}) {
        const auto result=moveBoss(reversed,mantle(reversed),gp_Vec(70,0,0),policy);
        require(bool(result),"Reversed-axis boss refused: "+result.message);
        require(result.axis.Direction().IsEqual(BRepAdaptor_Surface(mantle(reversed)).Cylinder().Axis().Direction(),1.e-8),"Boss axis was reversed");
        certified(result.shape,policy==BossBoundaryMode::Trim?half:full);bossOccupancy(result.shape,100,40,policy);
        require(serialized(reversed)==reversedBefore,"Reversed-axis source mutated");
    }
    for(bool axial:{true,false}) {
        BRepFeat_SplitShape split(source);
        if(axial) split.Add(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(30,40,40),gp_Dir(0,0,1)),10)).Edge(),mantle(source));
        else split.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(20,40,30),gp_Pnt(20,40,50)).Edge(),mantle(source));
        split.Build();require(split.IsDone(),"Boss split fixture failed");
        const auto shape=split.Shape();const auto before=serialized(shape);
        for(auto policy:{BossBoundaryMode::Trim,BossBoundaryMode::Extend}) {
            const auto result=moveBoss(shape,mantle(shape),gp_Vec(70,0,0),policy);
            require(bool(result),"Split boss refused: "+result.message);
            certified(result.shape,policy==BossBoundaryMode::Trim?half:full);bossOccupancy(result.shape,100,40,policy);
            require(serialized(shape)==before,"Split boss source mutated");
        }
    }
}
}
int main() {
    try {
        const double full=240000-3000*pi,half=240000-1500*pi;
        moveCase(25,5,full);moveCase(70,0,half);moveCase(100,0,240000,Code::HoleExited);
        moveCase(80,0,240000,Code::HoleExited); // external tangency, no removed volume
        moveCase(-30,0,half);moveCase(-15,-10,full);moveCase(70,40,240000-750*pi);
        const double segment=100*std::acos(.5)-5*std::sqrt(75.);
        moveCase(75,0,240000-30*segment);moveCase(-35,0,240000-30*segment);
        splitMantleCase(true);splitMantleCase(false);
        moveCase(0,0,full,Code::Unchanged);
        gp_Trsf transform;transform.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),.71);
        transform.SetTranslationPart(gp_Vec(210,-130,73));
        moveCase(25,5,full,Code::Moved,transform);moveCase(70,0,half,Code::Moved,transform,true);
        moveCase(100,0,240000,Code::HoleExited,transform,true);
        const auto source=fixture();const auto selected=mantle(source);
        const auto downward=BooleanGeometry::combine<BRepAlgoAPI_Cut>(block(),
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(30,40,35),gp_Dir(0,0,-1)),10,40).Shape());
        const auto downResult=moveThroughHole(downward,mantle(downward),gp_Vec(25,5,0));
        require(bool(downResult),"Downward-axis hole was refused: "+downResult.message);
        certified(downResult.shape,full);occupancy(downResult.shape,55,45);
        refused(source,selected,gp_Vec(3,0,1),Status::InvalidInput);
        refused(source,selected,gp_Vec(std::numeric_limits<double>::quiet_NaN(),0,0),Status::InvalidInput);
        refused(source,selected,gp_Vec(0,std::numeric_limits<double>::infinity(),0),Status::InvalidInput);
        refused(source,mantle(fixture()),gp_Vec(1,0,0),Status::Unsupported);
        refused(source,{},gp_Vec(1,0,0),Status::Unsupported);
        refused(selected,selected,gp_Vec(1,0,0),Status::InvalidInput); // open mantle, no solid
        TopoDS_Face plane;
        for(TopExp_Explorer f(source,TopAbs_FACE);f.More();f.Next())
            if(BRepAdaptor_Surface(TopoDS::Face(f.Current())).GetType()==GeomAbs_Plane) {plane=TopoDS::Face(f.Current());break;}
        refused(source,plane,gp_Vec(1,0,0),Status::Unsupported);
        const auto blind=BooleanGeometry::combine<BRepAlgoAPI_Cut>(block(),
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(30,40,10),gp_Dir(0,0,1)),10,25).Shape());
        refused(blind,mantle(blind),gp_Vec(5,0,0),Status::Unsupported);
        const auto notch=BooleanGeometry::combine<BRepAlgoAPI_Cut>(block(),
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,40,-5),gp_Dir(0,0,1)),10,40).Shape());
        refused(notch,mantle(notch),gp_Vec(5,0,0),Status::Unsupported);
        const auto cylinder=BRepPrimAPI_MakeCylinder(10,30).Shape();
        refused(cylinder,mantle(cylinder),gp_Vec(5,0,0),Status::Unsupported);
        const auto boss=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(block(),
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(30,40,30),gp_Dir(0,0,1)),10,20).Shape());
        refused(boss,mantle(boss),gp_Vec(5,0,0),Status::Unsupported);
        const auto two=BooleanGeometry::combine<BRepAlgoAPI_Cut>(source,
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(70,40,-5),gp_Dir(0,0,1)),5,40).Shape());
        refused(two,mantle(two),gp_Vec(5,0,0),Status::Unsupported);
        // A circle crossing both sides can isolate a corner island. A valid
        // multi-solid Boolean is still an invalid Move Face result.
        refused(source,selected,gp_Vec(-22,-32,0),Status::Failure);
        refused(source,selected,gp_Vec(-20,0,0),Status::Failure); // pinched tangent wall
        bossTests();
        std::cout<<"Cylindrical move geometry tests passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
