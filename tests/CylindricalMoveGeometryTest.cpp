#include "CylindricalMoveGeometry.h"
#include "BRepReconstruction.h"
#include "MoveFaceGeometry.h"
#include "BooleanGeometry.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
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
#include <array>
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
        const auto movedTwo=moveBoss(two,mantle(two),gp_Vec(3,0,0),policy);
        require(bool(movedTwo),"Independent second boss was refused: "+movedTwo.message);certified(movedTwo.shape,240000+2500*pi);
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
void independentFeatureTests() {
    const auto stock=BRepPrimAPI_MakeBox(163,80,60).Shape();
    struct Feature {double x,r,height;bool boss;};
    const auto cylinder=[](const Feature& f) {
        return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(f.x,36,f.boss?60:0),gp_Dir(0,0,1)),f.r,f.height).Shape();
    };
    const auto apply=[&](const TopoDS_Shape& shape,const Feature& f) {
        return f.boss?BooleanGeometry::combine<BRepAlgoAPI_Fuse>(shape,cylinder(f)):BooleanGeometry::combine<BRepAlgoAPI_Cut>(shape,cylinder(f));
    };
    const auto selected=[](const TopoDS_Shape& shape,double radius) {
        for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(face);
            if(s.GetType()==GeomAbs_Cylinder&&std::abs(s.Cylinder().Radius()-radius)<1.e-7) return face;
        }
        return TopoDS_Face();
    };
    for(const auto kinds:{std::pair{false,true},std::pair{false,false},std::pair{true,true}}) {
        const std::array<Feature,2> features{{{54,17,kinds.first?25.:60.,kinds.first},{121,18,kinds.second?55.:60.,kinds.second}}};
        const auto source=apply(apply(stock,features[0]),features[1]);const auto before=serialized(source);
        for(int which=0;which<2;++which) {
            const auto& f=features[which];const auto face=selected(source,f.r);
            for(auto policy:{BossBoundaryMode::Trim,BossBoundaryMode::Extend}) {
                if(!f.boss&&policy==BossBoundaryMode::Trim) continue;
                // Includes both directions, hole exit and boss edge crossings.
                for(double dx:{which==0?-8.:8.,which==0?-54.:42.}) {
                    const auto moved=f.boss?moveBoss(source,face,gp_Vec(dx,0,0),policy):moveThroughHole(source,face,gp_Vec(dx,0,0));
                    require(bool(moved),"Independent feature move refused: "+moved.message);
                    auto translated=f;translated.x+=dx;
                    auto material=cylinder(translated);
                    if(f.boss&&policy==BossBoundaryMode::Trim) {
                        const auto sides=BRepPrimAPI_MakeBox(gp_Pnt(0,0,60),163,80,f.height).Shape();
                        material=BooleanGeometry::combine<BRepAlgoAPI_Common>(material,sides);
                    }
                    auto expected=apply(stock,features[1-which]);
                    expected=f.boss?BooleanGeometry::combine<BRepAlgoAPI_Fuse>(expected,material):BooleanGeometry::combine<BRepAlgoAPI_Cut>(expected,material);
                    require(BRepReconstruction::equivalent(moved.shape,expected),"Unselected feature changed material");
                    certified(moved.shape,volume(expected));
                    for(int x=-7;x<=176;x+=7) for(int y=-3;y<=85;y+=8) for(double z:{-1.,10.,59.,61.,70.,114.,116.}) {
                        const double px=x+.31,py=y+.47;bool inside=px>0&&px<163&&py>0&&py<80&&z>0&&z<60;
                        for(int i=0;i<2;++i) {
                            const auto item=i==which?translated:features[i];
                            const bool circle=(px-item.x)*(px-item.x)+(py-36)*(py-36)<item.r*item.r;
                            if(item.boss) inside=inside||(circle&&z>60&&z<60+item.height&&
                                (i!=which||policy==BossBoundaryMode::Extend||(px>0&&px<163&&py>0&&py<80)));
                            else if(circle&&z>0&&z<60) inside=false;
                        }
                        BRepClass3d_SolidClassifier classifier(moved.shape,gp_Pnt(px,py,z),1.e-7);
                        require(classifier.State()==(inside?TopAbs_IN:TopAbs_OUT),"Independent analytic occupancy mismatch");
                    }
                    const auto remaining=selected(moved.shape,features[1-which].r);
                    require(!remaining.IsNull(),"Lost unselected cylindrical mantle");
                    const auto old=selected(source,features[1-which].r);
                    GProp_GProps a,b;BRepGProp::SurfaceProperties(old,a);BRepGProp::SurfaceProperties(remaining,b);
                    near(a.Mass(),b.Mass(),"Unselected mantle area changed");
                    require(serialized(source)==before,"Independent source mutated");
                }
                const double collision=features[1-which].x-f.x;
                const auto failed=f.boss?moveBoss(source,face,gp_Vec(collision,0,0),policy):moveThroughHole(source,face,gp_Vec(collision,0,0));
                require(!failed&&failed.shape.IsNull()&&failed.code==Code::InteractingFeatures,"Interacting features were accepted");
                require(serialized(source)==before,"Interacting failure mutated source");
            }
        }
        // Entire exit removes only the selected hole, leaving the other feature.
        if(!features[0].boss) {
            const auto exited=moveThroughHole(source,selected(source,17),gp_Vec(-110,0,0));
            require(bool(exited)&&exited.code==Code::HoleExited,"Multi-feature hole exit failed");
            require(BRepReconstruction::equivalent(exited.shape,apply(stock,features[1])),"Hole exit lost unrelated feature");
        }
        if(!features[0].boss&&features[1].boss) {
            gp_Trsf transform;transform.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),.71);
            transform.SetTranslationPart(gp_Vec(210,-130,73));
            const auto located=source.Moved(TopLoc_Location(transform));const auto locatedBefore=serialized(located);
            const auto moved=moveThroughHole(located,selected(located,17),gp_Vec(-8,0,0).Transformed(transform));
            require(bool(moved),"Located independent features refused: "+moved.message);
            auto f=features[0];f.x-=8;const auto expected=apply(apply(stock,f),features[1]).Moved(TopLoc_Location(transform));
            require(BRepReconstruction::equivalent(moved.shape,expected)&&serialized(located)==locatedBefore,"Located independent geometry changed incorrectly");
            BRepFeat_SplitShape split(source);
            split.Add(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(54,36,30),gp_Dir(0,0,1)),17)).Edge(),selected(source,17));
            split.Build();require(split.IsDone(),"Multi-feature split fixture failed");
            const auto divided=split.Shape();const auto dividedBefore=serialized(divided);
            const auto splitMove=moveThroughHole(divided,selected(divided,17),gp_Vec(-8,0,0));
            require(bool(splitMove),"Connected split mantle not recognized independently: "+splitMove.message);
            require(BRepReconstruction::equivalent(splitMove.shape,apply(apply(stock,f),features[1]))&&serialized(divided)==dividedBefore,
                "Split independent feature moved incorrectly");
        }
    }
    // A valid solid with intersecting holes is not an independent-feature input.
    const Feature a{54,17,60,false},b{70,18,60,false};
    const auto interacting=apply(apply(stock,a),b);const auto before=serialized(interacting);
    const auto refusedMove=moveThroughHole(interacting,selected(interacting,17),gp_Vec(-8,0,0));
    require(!refusedMove&&refusedMove.shape.IsNull()&&serialized(interacting)==before,"Interacting source geometry was accepted or mutated");
}
void boundaryFollowUpTests() {
    const auto forward=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(10,20,0),gp_Dir(0,0,1)),7,15).Shape();
    const auto reversed=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(10,20,15),gp_Dir(0,0,-1)),7,15).Shape();
    const auto wrongRadius=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(10,20,0),gp_Dir(0,0,1)),8,15).Shape();
    const auto wrongAxis=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(11,20,0),gp_Dir(0,0,1)),7,15).Shape();
    require(BRepReconstruction::sameAnalyticSupport(mantle(forward),mantle(reversed)),"Cylinder parameter-axis reversal changed support identity");
    require(!BRepReconstruction::sameAnalyticSupport(mantle(forward),mantle(wrongRadius))&&
            !BRepReconstruction::sameAnalyticSupport(mantle(forward),mantle(wrongAxis)),"Different cylindrical geometry matched as a support");
    const auto stock=BRepPrimAPI_MakeBox(gp_Pnt(-8,0,0),171,80,60).Shape();
    const auto bore=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(54,48,0),gp_Dir(0,0,1)),17,60).Shape();
    const auto boss=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(98,36,60),gp_Dir(0,0,1)),18,55).Shape();
    const auto source=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(BooleanGeometry::combine<BRepAlgoAPI_Cut>(stock,bore),boss);
    const auto findRadius=[](const TopoDS_Shape& shape,double radius) {
        for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
            const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);
            if(s.GetType()==GeomAbs_Cylinder&&std::abs(s.Cylinder().Radius()-radius)<1.e-7) return f;
        }
        return TopoDS_Face();
    };
    for(auto policy:{BossBoundaryMode::Trim,BossBoundaryMode::Extend}) {
        const auto crossed=moveBoss(source,findRadius(source,18),gp_Vec(0,-29,0),policy);
        require(bool(crossed),"Boundary source setup failed: "+crossed.message);
        const auto before=serialized(crossed.shape);const auto hole=findRadius(crossed.shape,17);
        auto exterior=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(98,7,60),gp_Dir(0,0,1)),18,55).Shape();
        if(policy==BossBoundaryMode::Trim) exterior=BooleanGeometry::combine<BRepAlgoAPI_Common>(exterior,
            BRepPrimAPI_MakeBox(gp_Pnt(-8,0,60),171,80,55).Shape());
        for(double dx:{3.,-54.,-130.}) {
            const auto moved=moveThroughHole(crossed.shape,hole,gp_Vec(dx,0,0));
            require(bool(moved),"Hole poisoned by exterior boss: "+moved.message);
            const auto tool=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(54+dx,48,0),gp_Dir(0,0,1)),17,60).Shape();
            const auto expected=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(BooleanGeometry::combine<BRepAlgoAPI_Cut>(stock,tool),exterior);
            require(BRepReconstruction::equivalent(moved.shape,expected),"Hole edit changed exterior boss material");certified(moved.shape,volume(expected));
            for(int x=-15;x<180;x+=9) for(int y=-20;y<90;y+=9) for(double z:{-1.,15.,59.,61.,90.,114.,116.}) {
                const double px=x+.23,py=y+.31;
                const bool body=px>-8&&px<163&&py>0&&py<80&&z>0&&z<60&&
                    (px-54-dx)*(px-54-dx)+(py-48)*(py-48)>17*17;
                const bool cap=(px-98)*(px-98)+(py-7)*(py-7)<18*18&&z>60&&z<115&&
                    (policy==BossBoundaryMode::Extend||py>0);
                BRepClass3d_SolidClassifier c(moved.shape,gp_Pnt(px,py,z),1.e-7);
                require(c.State()==(body||cap?TopAbs_IN:TopAbs_OUT),"Boundary follow-up occupancy mismatch");
            }
            require(serialized(crossed.shape)==before,"Follow-up hole operation mutated source");
        }
        const auto collision=moveThroughHole(crossed.shape,hole,gp_Vec(44,-41,0));
        require(!collision&&collision.shape.IsNull()&&collision.code==Code::InteractingFeatures,"Hole/exterior collision was accepted");
        TopoDS_Face plane;
        for(TopExp_Explorer it(crossed.shape,TopAbs_FACE);it.More();it.Next()) {
            const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);
            if(s.GetType()==GeomAbs_Plane&&std::abs(s.Plane().Axis().Direction().X())>.99&&s.Plane().Distance(gp_Pnt(163,0,0))<1.e-6) {plane=f;break;}
        }
        for(double distance:{2.,-2.}) {
            QString error;const auto moved=MoveFaceGeometry::move(crossed.shape,plane,distance,error);
            require(!moved.IsNull(),"Boundary result poisoned ordinary planar Move Face: "+error.toStdString());
            const auto changedStock=BRepPrimAPI_MakeBox(gp_Pnt(-8,0,0),171+distance,80,60).Shape();
            const auto expected=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(BooleanGeometry::combine<BRepAlgoAPI_Cut>(changedStock,bore),exterior);
            require(BRepReconstruction::equivalent(moved,expected),"Planar follow-up changed exterior material");certified(moved,volume(expected));
        }
        // Axial faces were recognized but could not reconstruct after either
        // boundary policy. Independent box/cylinder oracles verify actual
        // material, rather than accepting the local algorithm's own sweep.
        for(int which=0;which<3;++which) for(double distance:{2.,-2.}) {
            TopoDS_Face axial;
            const double z=which==0?0:which==1?60:115;
            for(TopExp_Explorer it(crossed.shape,TopAbs_FACE);it.More();it.Next()) {
                const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);
                if(s.GetType()!=GeomAbs_Plane||!s.Plane().Axis().Direction().IsParallel(gp_Dir(0,0,1),1.e-8)||std::abs(s.Plane().Location().Z()-z)>1.e-7) continue;
                gp_Pnt center;gp_Dir n;QString error;require(MoveFaceGeometry::reference(crossed.shape,f,center,n,error),"Axial reference failed");
                if(which!=1||n.Z()>0) {axial=f;break;}
            }
            require(!axial.IsNull(),"Missing axial follow-up target");
            const double bottom=which==0?-distance:0,top=which==1?60+distance:60,cap=which==2?115+distance:115;
            const auto newStock=BRepPrimAPI_MakeBox(gp_Pnt(-8,0,bottom),171,80,top-bottom).Shape();
            const auto newBore=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(54,48,bottom),gp_Dir(0,0,1)),17,top-bottom).Shape();
            auto newBoss=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(98,7,60),gp_Dir(0,0,1)),18,cap-60).Shape();
            if(policy==BossBoundaryMode::Trim) newBoss=BooleanGeometry::combine<BRepAlgoAPI_Common>(newBoss,
                BRepPrimAPI_MakeBox(gp_Pnt(-8,0,60),171,80,cap-60).Shape());
            auto expected=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(BooleanGeometry::combine<BRepAlgoAPI_Cut>(newStock,newBore),newBoss);
            // Lowering the support exposes the boss cylinder down to the new
            // support only over the original attached footprint.
            if(which==1&&distance<0) {
                auto extension=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(98,7,top),gp_Dir(0,0,1)),18,-distance).Shape();
                extension=BooleanGeometry::combine<BRepAlgoAPI_Common>(extension,BRepPrimAPI_MakeBox(gp_Pnt(-8,0,top),171,80,-distance).Shape());
                expected=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(expected,extension);
            }
            QString error;const auto moved=MoveFaceGeometry::move(crossed.shape,axial,distance,error);
            require(!moved.IsNull(),"Axial follow-up failed (policy "+std::to_string(int(policy))+", face "+std::to_string(which)+", offset "+std::to_string(distance)+"): "+error.toStdString());
            require(BRepReconstruction::equivalent(moved,expected),"Axial follow-up differs from independent material oracle");certified(moved,volume(expected));
            const auto nextHole=moveThroughHole(moved,findRadius(moved,17),gp_Vec(2,0,0));
            require(bool(nextHole),"Planar reconstruction disabled unchanged hole (policy "+std::to_string(int(policy))+", face "+std::to_string(which)+", offset "+std::to_string(distance)+"): "+nextHole.message);
            const auto shiftedBore=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(56,48,bottom),gp_Dir(0,0,1)),17,top-bottom).Shape();
            const auto holeOracle=BooleanGeometry::combine<BRepAlgoAPI_Cut>(BooleanGeometry::combine<BRepAlgoAPI_Fuse>(expected,newBore),shiftedBore);
            require(BRepReconstruction::equivalent(nextHole.shape,holeOracle),"Subsequent hole move changed boss/planar material");
            if(which==0&&distance==2) {
                gp_Trsf rotated;rotated.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),.47);rotated.SetTranslationPart(gp_Vec(201,-75,42));
                const auto placed=BRepBuilderAPI_Transform(crossed.shape,rotated,true).Shape();
                const auto placedTarget=TopoDS::Face(BRepBuilderAPI_Transform(axial,rotated,true).Shape());
                // Resolve actual membership geometrically, never pass the
                // separately transformed face as though it were a subshape.
                TopoDS_Face member;
                for(TopExp_Explorer it(placed,TopAbs_FACE);it.More();it.Next())
                    if(BRepReconstruction::sameAnalyticSupport(placedTarget,TopoDS::Face(it.Current()))) {member=TopoDS::Face(it.Current());break;}
                BRepReconstruction::Result local;
                require(BRepReconstruction::movePlanarLocal(placed,member,distance,local,error),"Rotated local planar move failed: "+error.toStdString());
                require(BRepReconstruction::equivalent(local.solid,BRepBuilderAPI_Transform(expected,rotated,true).Shape()),"Rotated local material changed");
                require(!BRepReconstruction::movePlanarLocal(crossed.shape,axial,-100,local,error)&&local.solid.IsNull(),"Sweep through opposite material boundary accepted");
            }
            if(which==1&&distance==2) {
                BRepReconstruction::Result local;
                require(!BRepReconstruction::movePlanarLocal(crossed.shape,axial,100,local,error)&&local.solid.IsNull(),"Sweep past boss cap invented a reversed cylindrical cavity");
            }
            for(const auto p:{gp_Pnt(10,10,-1),gp_Pnt(10,10,61),gp_Pnt(98,-5,80),gp_Pnt(54,48,20),gp_Pnt(98,7,116)}) {
                BRepClass3d_SolidClassifier actual(moved,p,1.e-7),oracle(expected,p,1.e-7);
                require(actual.State()==oracle.State(),"Axial follow-up occupancy mismatch");
            }
        }
        BRepReconstruction::Result rejected;QString reason;
        // Trim's merged front cut plane meets a curved wall across its movement
        // direction. Extend's stock front is still an invariant rectangular patch.
        for(TopExp_Explorer it(crossed.shape,TopAbs_FACE);it.More();it.Next()) {
            const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);
            if(s.GetType()==GeomAbs_Plane&&s.Plane().Axis().Direction().IsParallel(gp_Dir(0,1,0),1.e-8)&&s.Plane().Distance(gp_Pnt())<1.e-7) {
                const bool accepted=BRepReconstruction::movePlanarLocal(crossed.shape,f,2,rejected,reason);
                if(policy==BossBoundaryMode::Trim)
                    require(!accepted&&rejected.solid.IsNull()&&!reason.isEmpty(),"Unsupported transverse intersection accepted");
                else {
                    const auto expanded=BRepPrimAPI_MakeBox(gp_Pnt(-8,-2,0),171,82,60).Shape();
                    const auto expected=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(BooleanGeometry::combine<BRepAlgoAPI_Cut>(expanded,bore),exterior);
                    require(accepted&&BRepReconstruction::equivalent(rejected.solid,expected),"Extend front plane failed independent material check");
                }
            }
        }
        const auto secondBoss=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(20,20,60),gp_Dir(0,0,1)),6,15).Shape();
        const auto multiple=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(crossed.shape,secondBoss);
        const auto multipleStamp=serialized(multiple);
        const auto independent=moveBoss(multiple,findRadius(multiple,6),gp_Vec(5,0,0),policy);
        require(bool(independent),"Boundary boss disabled an independent boss: "+independent.message);
        const auto expectedMultiple=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(crossed.shape,
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(25,20,60),gp_Dir(0,0,1)),6,15).Shape());
        require(BRepReconstruction::equivalent(independent.shape,expectedMultiple),"Independent boss move changed boundary feature");
        certified(independent.shape,volume(expectedMultiple));require(serialized(multiple)==multipleStamp,"Independent boss move mutated its source");
        require(serialized(crossed.shape)==before,"Boundary follow-up mutated original geometry");
    }
}
void localNeighborhoodTests() {
    const auto stock=BRepPrimAPI_MakeBox(163,80,60).Shape();
    const auto hole=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(54,36,0),gp_Dir(0,0,1)),17,60).Shape();
    const auto boss=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(121,36,60),gp_Dir(0,0,1)),18,55).Shape();
    const auto find=[](const TopoDS_Shape& shape,double radius) {
        for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
            const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);
            if(s.GetType()==GeomAbs_Cylinder&&std::abs(s.Cylinder().Radius()-radius)<1.e-7) return f;
        }return TopoDS_Face();
    };
    const auto original=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(BooleanGeometry::combine<BRepAlgoAPI_Cut>(stock,hole),boss);
    const auto opened=moveThroughHole(original,find(original,17),gp_Vec(-54,0,0));
    require(bool(opened),"Cannot open boundary hole for local test");
    const auto notch=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,36,0),gp_Dir(0,0,1)),17,60).Shape();
    const auto base=BooleanGeometry::combine<BRepAlgoAPI_Cut>(stock,notch);
    for(auto policy:{BossBoundaryMode::Trim,BossBoundaryMode::Extend}) {
        auto source=opened.shape;double x=121,y=36;
        for(const auto delta:{gp_Vec(-5,4,0),gp_Vec(-3,0,0)}) {
            const auto before=serialized(source);const auto target=find(source,18);
            BRepReconstruction::Neighborhood n;QString error;
            require(BRepReconstruction::neighborhood(source,target,n,error),"Neighborhood analysis failed: "+error.toStdString());
            require(!n.patch.empty()&&n.neighbors.size()==2&&!n.boundary.empty()&&BRepReconstruction::equivalent(source,n.solid),"Incorrect local topology/copy certificate");
            const auto moved=moveBoss(source,target,delta,policy);
            require(bool(moved),"Local boss after open notch failed: "+moved.message);x+=delta.X();y+=delta.Y();
            const auto expected=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(base,
                BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x,y,60),gp_Dir(0,0,1)),18,55).Shape());
            require(BRepReconstruction::equivalent(moved.shape,expected),"Local edit changed unrelated planar/cylindrical material");certified(moved.shape,volume(expected));
            near(moved.axis.Location().Z(),60,"Local axis anchor is not the actual rim");
            for(int i=-5;i<175;i+=7) for(int j=-4;j<86;j+=7) for(double z:{-1.,15.,59.,61.,90.,114.,116.}) {
                const double px=i+.27,py=j+.39;
                const bool body=px>0&&px<163&&py>0&&py<80&&z>0&&z<60&&(px*px+(py-36)*(py-36)>17*17);
                const bool feature=(px-x)*(px-x)+(py-y)*(py-y)<18*18&&z>60&&z<115;
                BRepClass3d_SolidClassifier c(moved.shape,gp_Pnt(px,py,z),1.e-7);
                require(c.State()==(body||feature?TopAbs_IN:TopAbs_OUT),"Local neighborhood analytic occupancy mismatch");
            }
            require(serialized(source)==before,"Local edit mutated source");source=moved.shape;
        }
        const auto before=serialized(source);
        const auto bad=moveBoss(source,find(source,18),gp_Vec(-x,36-y,0),policy);
        require(!bad&&bad.shape.IsNull()&&serialized(source)==before,"Local interaction was accepted or changed input");
    }
    // Generic acceptance: even a valid candidate must not alter material outside
    // the caller's certified region, and correspondence must name result faces.
    BRepReconstruction::Neighborhood n;QString error;BRepReconstruction::Result finished;
    require(BRepReconstruction::neighborhood(opened.shape,find(opened.shape,18),n,error),"Cannot inspect local mapping");
    auto affected=n.patch;affected.insert(affected.end(),n.neighbors.begin(),n.neighbors.end());gp_Trsf identity;
    require(BRepReconstruction::finishLocal(n,n.solid,boss,affected,{},identity,finished,error),"Identity local certification failed: "+error.toStdString());
    TopTools_IndexedMapOfShape resultFaces;TopExp::MapShapes(finished.solid,TopAbs_FACE,resultFaces);
    for(const auto& mapping:finished.history) for(const auto& face:mapping.modified) require(resultFaces.Contains(face),"Local mapping names obsolete topology");
    const auto wrong=BooleanGeometry::combine<BRepAlgoAPI_Cut>(n.solid,BRepPrimAPI_MakeBox(gp_Pnt(150,65,0),20,20,20).Shape());
    require(!BRepReconstruction::finishLocal(n,wrong,boss,affected,{},identity,finished,error)&&finished.solid.IsNull(),"Outside material change passed local acceptance");
    BRep_Builder builder;TopoDS_Compound loose;builder.MakeCompound(loose);builder.Add(loose,boss);builder.Add(loose,n.neighbors.back());
    require(!BRepReconstruction::finishLocal(n,n.solid,loose,affected,{},identity,finished,error)&&finished.solid.IsNull(),"Local edit region with loose topology was accepted");
    // Global cylinder counts are not a local capability rule.
    auto many=BRepPrimAPI_MakeBox(320,160,40).Shape();
    for(int i=0;i<9;++i) many=BooleanGeometry::combine<BRepAlgoAPI_Cut>(many,
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(20+35*(i%5),25+35*(i/5),0),gp_Dir(0,0,1)),4,40).Shape());
    many=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(many,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(285,120,40),gp_Dir(0,0,1)),8,20).Shape());
    const auto moved=moveBoss(many,find(many,8),gp_Vec(-5,-5,0),BossBoundaryMode::Extend);
    require(bool(moved),"Unrelated cylinder count disabled local capability: "+moved.message);certified(moved.shape,volume(many));
}
void openNotchTests() {
    const auto find=[](const TopoDS_Shape& shape,double radius) {
        for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(face);
            if(s.GetType()==GeomAbs_Cylinder&&std::abs(s.Cylinder().Radius()-radius)<1.e-7) return face;
        }return TopoDS_Face();
    };
    for(int variant=0;variant<3;++variant) {
        auto stock=BRepPrimAPI_MakeBox(163,80,60).Shape();
        if(variant==2) stock=BooleanGeometry::combine<BRepAlgoAPI_Cut>(stock,BRepPrimAPI_MakeBox(gp_Pnt(140,65,0),23,15,60).Shape());
        stock=BooleanGeometry::combine<BRepAlgoAPI_Cut>(stock,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(90,65,0),gp_Dir(0,0,1)),7,60).Shape());
        const double bossX=variant==0?121:variant==1?158:166;
        const auto feature=[&](double x,double height) {
            auto shape=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x,36,60),gp_Dir(0,0,1)),18,height).Shape();
            if(variant==1) shape=BooleanGeometry::combine<BRepAlgoAPI_Common>(shape,BRepPrimAPI_MakeBox(gp_Pnt(0,0,60),163,80,height).Shape());
            return shape;
        };
        const auto body=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(stock,feature(bossX,55));
        const auto oracle=[&](double x,double y) {
            return BooleanGeometry::combine<BRepAlgoAPI_Cut>(body,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x,y,0),gp_Dir(0,0,1)),17,60).Shape());
        };
        gp_Trsf placement;if(variant==2) {placement.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),.57);placement.SetTranslationPart(gp_Vec(200,-80,43));}
        auto source=BRepBuilderAPI_Transform(oracle(54,36),placement,true).Shape();double x=54,y=36;
        TopoDS_Shape open;
        for(const auto delta:{gp_Vec(-54,0,0),gp_Vec(-5,0,0),gp_Vec(0,7,0),gp_Vec(33,0,0),gp_Vec(-28,0,0),gp_Vec(0,-8,0),gp_Vec(6,0,0),gp_Vec(-12,0,0),gp_Vec(30,0,0),gp_Vec(26,45,0),gp_Vec(0,5,0),gp_Vec(5,0,0),gp_Vec(0,-30,0)}) {
            const auto stamp=serialized(source);
            if((x==0&&y==36)||(x==50&&y==80)) {
                const auto reference=moveThroughHole(source,find(source,17),gp_Vec());
                require(bool(reference)&&reference.code==Code::Unchanged&&BRepReconstruction::equivalent(reference.shape,source),"Current notch cannot activate Move Face: "+reference.message);
            }
            const auto moved=moveThroughHole(source,find(source,17),delta.Transformed(placement));
            require(bool(moved),"Repeated notch move failed (variant "+std::to_string(variant)+", x="+std::to_string(x)+", y="+std::to_string(y)+"): "+moved.message);
            x+=delta.X();y+=delta.Y();const auto expected=BRepBuilderAPI_Transform(oracle(x,y),placement,true).Shape();
            require(BRepReconstruction::equivalent(moved.shape,expected),"Notch movement changed independent boss/hole/planar material");
            const double d=std::clamp(std::min({x,163-x,y,80-y}),-17.,17.);
            const double cutArea=17*17*(pi/2+std::asin(d/17))+d*std::sqrt(std::max(0.,17*17-d*d));
            const double bossArea=variant==1?18*18*(pi/2+std::asin(5./18))+5*std::sqrt(18*18-25):pi*18*18;
            certified(moved.shape,163*80*60-(variant==2?23*15*60:0)-pi*7*7*60-cutArea*60+bossArea*55);
            near(moved.radius,17,"Notch radius changed");near(moved.height,60,"Notch axial span changed");
            require(moved.axis.Direction().IsParallel(gp_Dir(0,0,1).Transformed(placement),1.e-8),"Notch axis direction changed");
            for(int i=-20;i<190;i+=11) for(int j=-5;j<90;j+=11) for(double z:{-1.,10.,59.,61.,90.,114.,116.}) {
                const double px=i+.37,py=j+.61;
                const bool bodyIn=px>0&&px<163&&py>0&&py<80&&z>0&&z<60&&
                    !(variant==2&&px>140&&py>65)&&std::hypot(px-90,py-65)>7&&std::hypot(px-x,py-y)>17;
                const bool bossIn=std::hypot(px-bossX,py-36)<18&&z>60&&z<115&&(variant!=1||px<163);
                BRepClass3d_SolidClassifier c(moved.shape,gp_Pnt(px,py,z).Transformed(placement),1.e-7);
                require(c.State()==(bodyIn||bossIn?TopAbs_IN:TopAbs_OUT),"Notch analytic occupancy mismatch");
            }
            require(serialized(source)==stamp,"Notch movement mutated the input B-Rep");source=moved.shape;
            if(x==0&&y==36) open=source;
        }
        // The unaffected boss still supports its existing planar height edit;
        // complete interior bosses also retain lateral translation.
        TopoDS_Face cap;
        for(TopExp_Explorer it(open,TopAbs_FACE);it.More();it.Next()) {
            const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);
            if(s.GetType()==GeomAbs_Plane&&s.Plane().Distance(gp_Pnt(bossX,36,115).Transformed(placement))<1.e-6&&
               s.Plane().Axis().Direction().IsParallel(gp_Dir(0,0,1).Transformed(placement),1.e-8)) {cap=f;break;}
        }
        QString reason;const auto taller=MoveFaceGeometry::move(open,cap,1,reason);
        require(!taller.IsNull(),"Unchanged boss cap became uneditable: "+reason.toStdString());
        const auto openTool=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,36,0),gp_Dir(0,0,1)),17,60).Shape();
        const auto expectedTaller=BooleanGeometry::combine<BRepAlgoAPI_Cut>(BooleanGeometry::combine<BRepAlgoAPI_Fuse>(stock,feature(bossX,56)),openTool);
        require(BRepReconstruction::equivalent(taller,BRepBuilderAPI_Transform(expectedTaller,placement,true).Shape()),"Boss height edit changed the open notch");
        if(variant==0) {
            const auto movedBoss=moveBoss(open,find(open,18),gp_Vec(-3,0,0),BossBoundaryMode::Extend);
            const auto expectedBoss=BooleanGeometry::combine<BRepAlgoAPI_Cut>(BooleanGeometry::combine<BRepAlgoAPI_Fuse>(stock,feature(118,55)),openTool);
            require(bool(movedBoss)&&BRepReconstruction::equivalent(movedBoss.shape,expectedBoss),"Unchanged complete boss lost lateral Move Face or changed the notch");
        }
        const auto stamp=serialized(open);
        const auto corner=moveThroughHole(open,find(open,17),gp_Vec(0,-31,0).Transformed(placement));
        require(bool(corner)&&BRepReconstruction::equivalent(corner.shape,BRepBuilderAPI_Transform(oracle(0,5),placement,true).Shape()),
            "Planar-rim corner transition did not preserve the independently clipped cylinder material");
        const auto cornerReturn=moveThroughHole(corner.shape,find(corner.shape,17),gp_Vec(0,31,0).Transformed(placement));
        require(bool(cornerReturn)&&BRepReconstruction::equivalent(cornerReturn.shape,open),"Current multi-plane cylindrical trims could not move back from the corner");
        for(const auto destination:{gp_Pnt(90,65,0),gp_Pnt(bossX,36,0)}) {
            const auto rejected=moveThroughHole(open,find(open,17),gp_Vec(gp_Pnt(0,36,0),destination).Transformed(placement));
            require(!rejected&&rejected.shape.IsNull()&&!rejected.message.empty()&&serialized(open)==stamp,"Ambiguous feature interaction was accepted or changed input");
        }
        require(!moveThroughHole(open,find(open,17),gp_Vec(0,0,1).Transformed(placement)),"Axial notch movement accepted");
        const auto exited=moveThroughHole(open,find(open,17),gp_Vec(-40,0,0).Transformed(placement));
        require(bool(exited)&&exited.code==Code::HoleExited&&BRepReconstruction::equivalent(exited.shape,BRepBuilderAPI_Transform(body,placement,true).Shape()),"Exited notch did not restore certified material");
        // Imported/current geometry needs no prior operation state.
        const auto imported=BRepBuilderAPI_Transform(oracle(0,36),placement,true).Shape();
        const auto independent=moveThroughHole(imported,find(imported,17),gp_Vec(0,5,0).Transformed(placement));
        require(bool(independent)&&BRepReconstruction::equivalent(independent.shape,BRepBuilderAPI_Transform(oracle(0,41),placement,true).Shape()),"Notch recognition depends on prior operations");
        const auto patch=find(open,17);const BRepAdaptor_Surface surface(patch);
        const double u=surface.FirstUParameter()+.37*(surface.LastUParameter()-surface.FirstUParameter());
        BRepFeat_SplitShape split(open);split.Add(BRepBuilderAPI_MakeEdge(surface.Value(u,surface.FirstVParameter()),surface.Value(u,surface.LastVParameter())).Edge(),patch);split.Build();
        require(split.IsDone(),"Cannot split notch mantle fixture");
        TopTools_IndexedMapOfShape beforeFaces,afterFaces;TopExp::MapShapes(open,TopAbs_FACE,beforeFaces);TopExp::MapShapes(split.Shape(),TopAbs_FACE,afterFaces);
        require(afterFaces.Extent()>beforeFaces.Extent(),"Notch fixture did not create split topology");
        const auto splitResult=moveThroughHole(split.Shape(),find(split.Shape(),17),gp_Vec(0,3,0).Transformed(placement));
        require(bool(splitResult)&&BRepReconstruction::equivalent(splitResult.shape,BRepBuilderAPI_Transform(oracle(0,39),placement,true).Shape()),"Split notch mantle did not reconstruct");
    }
}
void repeatedBossTests() {
    const auto find=[](const TopoDS_Shape& shape,double radius) {
        for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
            const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);
            if(s.GetType()==GeomAbs_Cylinder&&std::abs(s.Cylinder().Radius()-radius)<1.e-7) return f;
        }return TopoDS_Face();
    };
    for(bool rotated:{false,true}) for(auto policy:{BossBoundaryMode::Trim,BossBoundaryMode::Extend}) {
        const auto stock=BooleanGeometry::combine<BRepAlgoAPI_Cut>(BRepPrimAPI_MakeBox(163,80,60).Shape(),
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(54,36,0),gp_Dir(0,0,1)),17,60).Shape());
        const auto body=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(stock,
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(90,65,60),gp_Dir(0,0,1)),7,20).Shape());
        const auto oracle=[&](double x) {
            auto boss=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x,36,60),gp_Dir(0,0,1)),18,55).Shape();
            if(policy==BossBoundaryMode::Trim) boss=BooleanGeometry::combine<BRepAlgoAPI_Common>(boss,BRepPrimAPI_MakeBox(gp_Pnt(0,0,60),163,80,55).Shape());
            return BooleanGeometry::combine<BRepAlgoAPI_Fuse>(body,boss);
        };
        gp_Trsf placement;if(rotated) {placement.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),.57);placement.SetTranslationPart(gp_Vec(200,-80,43));}
        auto source=BRepBuilderAPI_Transform(oracle(121),placement,true).Shape();double x=121;
        for(double destination:{158.,166.,170.,158.,121.,158.}) {
            const auto stamp=serialized(source);
            const auto reference=moveBoss(source,find(source,18),gp_Vec(),policy);
            require(bool(reference)&&reference.code==Code::Unchanged,"Boundary boss activation failed: "+reference.message);
            const auto moved=moveBoss(source,find(source,18),gp_Vec(destination-x,0,0).Transformed(placement),policy);
            require(bool(moved),"Repeated boss move failed at "+std::to_string(destination)+": "+moved.message);
            require(BRepReconstruction::equivalent(moved.shape,BRepBuilderAPI_Transform(oracle(destination),placement,true).Shape()),"Repeated boss changed independent hole/boss material");
            const double a=std::clamp(163-destination,-18.,18.);
            const double area=policy==BossBoundaryMode::Trim?324*(pi/2+std::asin(a/18))+a*std::sqrt(std::max(0.,324-a*a)):pi*324;
            certified(moved.shape,163*80*60-pi*289*60+pi*49*20+area*55);
            near(moved.radius,18,"Repeated boss changed radius");near(moved.height,55,"Repeated boss changed height");
            require(serialized(source)==stamp,"Repeated boss mutated source");source=moved.shape;x=destination;
        }
        for(const auto delta:{gp_Vec(70,0,0),gp_Vec(0,40,0),gp_Vec(-104,0,0),gp_Vec(0,0,1)}) {
            const auto stamp=serialized(source);const auto rejected=moveBoss(source,find(source,18),delta.Transformed(placement),policy);
            require(!rejected&&rejected.shape.IsNull()&&!rejected.message.empty()&&serialized(source)==stamp,"Ambiguous boss destination accepted or mutated source: "+std::to_string(delta.X())+","+std::to_string(delta.Y())+", trim="+std::to_string(policy==BossBoundaryMode::Trim));
        }
    }
}
void repeatedBossDetailTests() {
    const auto find=[](const TopoDS_Shape& shape) {
        for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
            const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);
            if(s.GetType()==GeomAbs_Cylinder&&std::abs(s.Cylinder().Radius()-18)<1.e-7) return f;
        }return TopoDS_Face();
    };
    auto stock=BooleanGeometry::combine<BRepAlgoAPI_Cut>(BRepPrimAPI_MakeBox(163,80,60).Shape(),BRepPrimAPI_MakeBox(gp_Pnt(0,65,0),20,15,60).Shape());
    stock=BooleanGeometry::combine<BRepAlgoAPI_Cut>(stock,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(54,36,0),gp_Dir(0,0,1)),17,60).Shape());
    const auto boss=[](double x) {return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x,36,115),gp_Dir(0,0,-1)),18,55).Shape();};
    const auto clipped=[&](double x){return BooleanGeometry::combine<BRepAlgoAPI_Common>(boss(x),BRepPrimAPI_MakeBox(gp_Pnt(0,0,60),163,80,55).Shape());};
    for(bool trimSource:{false,true}) for(auto policy:{BossBoundaryMode::Trim,BossBoundaryMode::Extend}) {
        // Imported topology, reversed axis, and a split current mantle. Switching
        // policies uses current support geometry, without remembered Trim state.
        auto source=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(stock,trimSource?clipped(158):boss(158));
        const auto patch=find(source);const BRepAdaptor_Surface s(patch);const double u=s.FirstUParameter()+.37*(s.LastUParameter()-s.FirstUParameter());
        BRepFeat_SplitShape split(source);split.Add(BRepBuilderAPI_MakeEdge(s.Value(u,s.FirstVParameter()),s.Value(u,s.LastVParameter())).Edge(),patch);split.Build();
        require(split.IsDone(),"Cannot split boundary boss mantle");source=split.Shape();
        const auto stamp=serialized(source);
        const auto reference=moveBoss(source,find(source),gp_Vec(),policy);
        require(bool(reference)&&reference.code==Code::Unchanged&&BRepReconstruction::equivalent(reference.shape,source),"Zero boss recognition changed current material");
        double x=158;
        for(double destination:{166.,121.}) {
            const auto result=moveBoss(source,find(source),gp_Vec(destination-x,0,0),policy);
            require(bool(result),"Imported/split/reversed boss failed: "+result.message);
            const auto expected=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(stock,policy==BossBoundaryMode::Trim?clipped(destination):boss(destination));
            require(BRepReconstruction::equivalent(result.shape,expected),"Imported repeated boss changed unrelated nonrectangular support");
            for(int i=0;i<190;i+=13) for(int j=0;j<80;j+=13) for(double z:{15.,59.,61.,90.,114.,116.}) {
                const double px=i+.27,py=j+.41;
                const bool body=px<163&&z<60&&!(px<20&&py>65)&&std::hypot(px-54,py-36)>17;
                const bool feature=std::hypot(px-destination,py-36)<18&&z>60&&z<115&&(policy==BossBoundaryMode::Extend||px<163);
                BRepClass3d_SolidClassifier c(result.shape,gp_Pnt(px,py,z),1.e-7);
                require(c.State()==(body||feature?TopAbs_IN:TopAbs_OUT),"Repeated boss independent occupancy mismatch");
            }
            require(result.axis.Direction().IsParallel(gp_Dir(0,0,1),1.e-8),"Repeated boss changed axis");
            if(x==158) require(serialized(source)==stamp,"Imported boss input mutated");source=result.shape;x=destination;
        }
    }
}
void boundaryPullGapTests() {
    const auto find=[](const TopoDS_Shape& shape,double radius) {for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);if(s.GetType()==GeomAbs_Cylinder&&std::abs(s.Cylinder().Radius()-radius)<1.e-7) return f;}return TopoDS_Face();};
    const auto base=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(BooleanGeometry::combine<BRepAlgoAPI_Cut>(BRepPrimAPI_MakeBox(163,80,60).Shape(),
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(54,36,0),gp_Dir(0,0,1)),17,60).Shape()),BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(90,65,60),gp_Dir(0,0,1)),7,20).Shape());
    for(bool trim:{false,true}) for(bool placed:{false,true}) {
        const double x=trim?158:166;gp_Trsf placement;if(placed) {placement.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),.43);placement.SetTranslationPart(gp_Vec(140,-60,35));}
        const auto oracle=[&](double r,double h) {auto boss=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x,36,60),gp_Dir(0,0,1)),r,h).Shape();if(trim) boss=BooleanGeometry::combine<BRepAlgoAPI_Common>(boss,BRepPrimAPI_MakeBox(gp_Pnt(0,0,60),163,80,h).Shape());return BRepBuilderAPI_Transform(BooleanGeometry::combine<BRepAlgoAPI_Fuse>(base,boss),placement,true).Shape();};
        auto source=oracle(18,55);double radius=18,height=55;
        const auto cap=[&](const TopoDS_Shape& shape,double h) {for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);if(s.GetType()!=GeomAbs_Plane) continue;auto n=s.Plane().Axis().Direction();if(f.Orientation()==TopAbs_REVERSED) n.Reverse();if(n.IsEqual(gp_Dir(0,0,1).Transformed(placement),1.e-8)&&s.Plane().Distance(gp_Pnt(x,36,60+h).Transformed(placement))<1.e-6) return f;}return TopoDS_Face();};
        const auto verify=[&](const TopoDS_Shape& shape,double r,double h) {require(BRepReconstruction::equivalent(shape,oracle(r,h)),"Boundary Pull changed unrelated hole/boss material");const double a=std::min(5.,r);const double area=trim?r*r*(pi/2+std::asin(a/r))+a*std::sqrt(std::max(0.,r*r-a*a)):pi*r*r;certified(shape,163*80*60-pi*289*60+pi*49*20+area*h);for(int i=0;i<190;i+=17) for(int j=0;j<80;j+=17) for(double z:{15.,59.,61.,85.,110.,119.}) {const double px=i+.27,py=j+.41;const bool body=px<163&&z<60&&std::hypot(px-54,py-36)>17;const bool other=std::hypot(px-90,py-65)<7&&z>60&&z<80;const bool boss=std::hypot(px-x,py-36)<r&&z>60&&z<60+h&&(!trim||px<163);BRepClass3d_SolidClassifier c(shape,gp_Pnt(px,py,z).Transformed(placement),1.e-7);require(c.State()==(body||other||boss?TopAbs_IN:TopAbs_OUT),"Boundary Pull independent occupancy mismatch");}};
        if(!trim) for(double r:{20.,16.,12.}) {const auto stamp=serialized(source);const auto result=resizeRadius(source,find(source,radius),r);require(bool(result),"Extend radius gap remains: "+result.message);verify(result.shape,r,height);require(serialized(source)==stamp,"Extend radius mutated source");source=result.shape;radius=r;}
        for(double h:{62.,40.,57.}) {const auto target=cap(source,height);const auto reference=bossHeightReference(source,target);require(bool(reference)&&std::abs(reference.height-height)<1.e-6,"Boss cap height reference failed: "+reference.message);const auto stamp=serialized(source);const auto result=resizeBossHeight(source,target,h);require(bool(result),"Boundary boss height failed (trim="+std::to_string(trim)+", placed="+std::to_string(placed)+", height="+std::to_string(h)+"): "+result.message);verify(result.shape,radius,h);require(serialized(source)==stamp,"Boss height mutated source");source=result.shape;height=h;}
        const auto stamp=serialized(source);for(double h:{0.,-.1,std::numeric_limits<double>::quiet_NaN()}) {const auto result=resizeBossHeight(source,cap(source,height),h);require(!result&&result.shape.IsNull()&&!result.message.empty()&&serialized(source)==stamp,"Invalid height was accepted or mutated source");}
        if(!trim) {const auto detached=resizeRadius(source,find(source,radius),2);require(!detached&&detached.shape.IsNull()&&!detached.message.empty(),"Detached overhang radius accepted");}
        // Both split mantle and cap patches are current topology, not features.
        const auto mantle=find(source,radius);const BRepAdaptor_Surface wall(mantle);const double u=wall.FirstUParameter()+.37*(wall.LastUParameter()-wall.FirstUParameter());
        BRepFeat_SplitShape split(source);split.Add(BRepBuilderAPI_MakeEdge(wall.Value(u,wall.FirstVParameter()),wall.Value(u,wall.LastVParameter())).Edge(),mantle);split.Build();require(split.IsDone(),"Cannot split Pull mantle");const auto resized=resizeRadius(split.Shape(),find(split.Shape(),radius),radius+1);require(bool(resized),"Split mantle radial Pull failed: "+resized.message);verify(resized.shape,radius+1,height);
    }
}
void radialPullTests() {
    const auto stock=BRepPrimAPI_MakeBox(163,80,60).Shape();
    const auto bore=[](double r){return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(54,36,-10),gp_Dir(0,0,1)),r,80).Shape();};
    const auto boss=[](double x,double r){return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x,36,60),gp_Dir(0,0,1)),r,55).Shape();};
    const auto original=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(BooleanGeometry::combine<BRepAlgoAPI_Cut>(stock,bore(17)),boss(121,18));
    const auto face=[](const TopoDS_Shape& shape,double r) {
        for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
            const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);
            if(s.GetType()==GeomAbs_Cylinder&&std::abs(s.Cylinder().Radius()-r)<1.e-7) return f;
        }return TopoDS_Face();
    };
    const auto trimmed=moveBoss(original,face(original,18),gp_Vec(37,0,0),BossBoundaryMode::Trim);
    require(bool(trimmed),"Cannot construct post-Trim radius fixture");
    const auto clip=[&](double r) {
        if(r<=5) return boss(158,r);
        return BooleanGeometry::combine<BRepAlgoAPI_Common>(boss(158,r),BRepPrimAPI_MakeBox(gp_Pnt(0,0,60),163,80,55).Shape());
    };
    for(bool partial:{false,true}) for(double r:{4.,5.,5.1,16.,20.,24.}) {
        const auto source=partial?trimmed.shape:original;const auto stamp=serialized(source);
        const auto result=resizeRadius(source,face(source,18),r);
        require(bool(result),"Radius reconstruction failed: "+result.message);
        const auto feature=partial?clip(r):boss(121,r);
        const auto expected=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(BooleanGeometry::combine<BRepAlgoAPI_Cut>(stock,bore(17)),feature);
        require(BRepReconstruction::equivalent(result.shape,expected),"Radial reconstruction changed unrelated material");
        const double a=std::min(r,5.);
        const double area=partial?r*r*(pi/2+std::asin(a/r))+a*std::sqrt(std::max(0.,r*r-a*a)):pi*r*r;
        certified(result.shape,163*80*60-pi*17*17*60+area*55);
        for(double x:{10.23,54.23,120.23,140.23,150.23,157.23,162.23,165.23})
            for(double y:{10.31,20.31,35.31,45.31,60.31}) for(double z:{10.,59.5,60.5,100.,114.5,116.}) {
                const bool body=x<163&&z<60&&std::hypot(x-54,y-36)>17;
                const bool featureIn=std::hypot(x-(partial?158:121),y-36)<r&&z>60&&z<115&&(!partial||x<163);
                BRepClass3d_SolidClassifier c(result.shape,gp_Pnt(x,y,z),1.e-7);
                require(c.State()==(body||featureIn?TopAbs_IN:TopAbs_OUT),"Radial analytic occupancy mismatch");
            }
        require(serialized(source)==stamp,"Radial edit mutated input");
        // A reconstructed partial or now-complete mantle remains editable.
        const auto repeated=resizeRadius(result.shape,face(result.shape,r),r+1);
        if(partial&&r==5) require(bool(repeated)&&BRepReconstruction::equivalent(repeated.shape,BooleanGeometry::combine<BRepAlgoAPI_Fuse>(BooleanGeometry::combine<BRepAlgoAPI_Cut>(stock,bore(17)),boss(158,6))),"A current full boss did not grow as a full supported overhang without remembered Trim history");
        else require(bool(repeated),"Repeated radial edit failed (partial="+std::to_string(partial)+", R="+std::to_string(r)+"): "+repeated.message);
    }
    const auto stamp=serialized(trimmed.shape);
    for(double r:{0.,.005,40.,100.,std::numeric_limits<double>::quiet_NaN()}) {
        const auto rejected=resizeRadius(trimmed.shape,face(trimmed.shape,18),r);
        require(!rejected&&rejected.shape.IsNull()&&!rejected.message.empty()&&serialized(trimmed.shape)==stamp,"Invalid/unsupported radial edit changed input");
    }
    for(double r:{15.,20.}) {
        const auto resized=resizeRadius(trimmed.shape,face(trimmed.shape,17),r);
        const auto expected=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(BooleanGeometry::combine<BRepAlgoAPI_Cut>(stock,bore(r)),clip(18));
        require(bool(resized)&&BRepReconstruction::equivalent(resized.shape,expected),"Independent through-hole radius edit changed trimmed boss");
        certified(resized.shape,volume(expected));
    }
    const auto overhang=moveBoss(original,face(original,18),gp_Vec(45,0,0),BossBoundaryMode::Extend);
    require(bool(overhang),"Cannot create Extend radial source");const auto overhangResize=resizeRadius(overhang.shape,face(overhang.shape,18),20);
    require(bool(overhangResize)&&BRepReconstruction::equivalent(overhangResize.shape,BooleanGeometry::combine<BRepAlgoAPI_Fuse>(BooleanGeometry::combine<BRepAlgoAPI_Cut>(stock,bore(17)),boss(166,20))),"Extend radius reconstruction changed surrounding stock");
    const auto notch=moveThroughHole(original,face(original,17),gp_Vec(-54,0,0));
    require(bool(notch),"Cannot construct planar-trim notch for radial reconstruction");
    const auto notchStamp=serialized(notch.shape);const auto resizedNotch=resizeRadius(notch.shape,face(notch.shape,17),20);
    const auto expectedNotch=BooleanGeometry::combine<BRepAlgoAPI_Cut>(BooleanGeometry::combine<BRepAlgoAPI_Fuse>(stock,boss(121,18)),
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,36,-10),gp_Dir(0,0,1)),20,80).Shape());
    require(bool(resizedNotch)&&BRepReconstruction::equivalent(resizedNotch.shape,expectedNotch)&&serialized(notch.shape)==notchStamp,
        "Planar-rim notch resize failed to preserve its actual exterior material trim");
    certified(resizedNotch.shape,163*80*60-pi*20*20*60/2+pi*18*18*55);
    const auto blind=BooleanGeometry::combine<BRepAlgoAPI_Cut>(stock,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(54,36,20),gp_Dir(0,0,1)),17,40).Shape());
    require(!resizeRadius(blind,face(blind,17),20),"Uncertified blind-hole closure was accepted");
    gp_Trsf rotated;rotated.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),.53);rotated.SetTranslationPart(gp_Vec(120,-74,93));
    const auto placed=BRepBuilderAPI_Transform(trimmed.shape,rotated,true).Shape();
    const auto result=resizeRadius(placed,face(placed,18),20);
    const auto expected=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(BooleanGeometry::combine<BRepAlgoAPI_Cut>(stock,bore(17)),clip(20));
    require(bool(result)&&BRepReconstruction::equivalent(result.shape,BRepBuilderAPI_Transform(expected,rotated,true).Shape()),"Transformed radial reconstruction failed");
}
}
int main(int argc,char** argv) {
    try {
        if(argc>1&&std::string(argv[1])=="--pull-gaps") {boundaryPullGapTests();std::cout<<"Boundary Pull gap geometry passed\n";return 0;}
        if(argc>1&&std::string(argv[1])=="--repeated-boss-detail") {repeatedBossDetailTests();std::cout<<"Imported/split boundary boss geometry passed\n";return 0;}
        if(argc>1&&std::string(argv[1])=="--repeated-boss") {repeatedBossTests();std::cout<<"Repeated boundary boss geometry passed\n";return 0;}
        if(argc>1&&std::string(argv[1])=="--notch") {openNotchTests();std::cout<<"Repeated open-notch geometry passed\n";return 0;}
        if(argc>1&&std::string(argv[1])=="--radius") {radialPullTests();std::cout<<"Certified radial Pull geometry passed\n";return 0;}
        if(argc>1&&std::string(argv[1])=="--local") {
            localNeighborhoodTests();std::cout<<"Local geometric neighborhood tests passed\n";return 0;
        }
        if(argc>1&&std::string(argv[1])=="--boundary") {
            boundaryFollowUpTests();std::cout<<"Boundary follow-up geometry tests passed\n";return 0;
        }
        if(argc>1&&std::string(argv[1])=="--independent") {
            independentFeatureTests();std::cout<<"Independent cylindrical feature geometry tests passed\n";return 0;
        }
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
        const auto repeated=moveThroughHole(notch,mantle(notch),gp_Vec(5,0,0));
        require(bool(repeated),"Open source notch was refused: "+repeated.message);occupancy(repeated.shape,5,40);
        certified(repeated.shape,240000-30*(100*(pi/2+std::asin(.5))+5*std::sqrt(75.)));
        const auto cylinder=BRepPrimAPI_MakeCylinder(10,30).Shape();
        refused(cylinder,mantle(cylinder),gp_Vec(5,0,0),Status::Unsupported);
        const auto boss=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(block(),
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(30,40,30),gp_Dir(0,0,1)),10,20).Shape());
        refused(boss,mantle(boss),gp_Vec(5,0,0),Status::Unsupported);
        const auto two=BooleanGeometry::combine<BRepAlgoAPI_Cut>(source,
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(70,40,-5),gp_Dir(0,0,1)),5,40).Shape());
        const auto movedTwo=moveThroughHole(two,mantle(two),gp_Vec(5,0,0));
        require(bool(movedTwo),"Independent second hole was refused: "+movedTwo.message);certified(movedTwo.shape,240000-3750*pi);
        // A circle crossing both sides can isolate a corner island. A valid
        // multi-solid Boolean is still an invalid Move Face result.
        refused(source,selected,gp_Vec(-22,-32,0),Status::Failure);
        refused(source,selected,gp_Vec(-20,0,0),Status::Failure); // pinched tangent wall
        bossTests();
        std::cout<<"Cylindrical move geometry tests passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
