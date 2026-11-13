// v0.5.3 unified reference resolution: every reference type, Part and
// Workplane transforms, stale/hidden refusal and snap/pick identity.
#include "ReferenceGeometry.h"
#include "SketchSnapping.h"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace References;
namespace {
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
bool near(const gp_Pnt& a,const gp_Pnt& b,double t=1.e-9) {return a.Distance(b)<=t;}
TopoDS_Face planarFace(const TopoDS_Shape& shape,const gp_Dir& normal) {
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());BRepAdaptor_Surface s(face);if(s.GetType()!=GeomAbs_Plane) continue;
        gp_Dir n=s.Plane().Axis().Direction();if(face.Orientation()==TopAbs_REVERSED) n.Reverse();
        if(n.IsEqual(normal,1.e-9)) return face;
    }
    throw std::runtime_error("face missing");
}
TopoDS_Edge edgeAlong(const TopoDS_Shape& shape,const gp_Pnt& a,const gp_Pnt& b) {
    for(TopExp_Explorer it(shape,TopAbs_EDGE);it.More();it.Next()) {
        const auto e=TopoDS::Edge(it.Current());
        const auto p=BRep_Tool::Pnt(TopExp::FirstVertex(e)),q=BRep_Tool::Pnt(TopExp::LastVertex(e));
        if((near(p,a)&&near(q,b))||(near(p,b)&&near(q,a))) return e;
    }
    throw std::runtime_error("edge missing");
}
// Independent oracle for an oriented straight edge: first to last vertex,
// respecting edge orientation (TopExp with CumOri=true).
gp_Dir orientedEdge(const TopoDS_Edge& e,const gp_Trsf& placement) {
    const auto p=BRep_Tool::Pnt(TopExp::FirstVertex(e,true)).Transformed(placement),q=BRep_Tool::Pnt(TopExp::LastVertex(e,true)).Transformed(placement);
    return gp_Dir(gp_Vec(p,q));
}
Sketch::Entity entity(const QString& id,const QString& wp,Sketch::Geometry g) {Sketch::Entity e;e.id=id;e.workplaneId=wp;e.geometry=g;e.name=id;return e;}
}
int main() {
 try {
    // Fixture: a placed box and cylinder, one Workplane with Sketch geometry,
    // one Axis and one Point.
    gp_Trsf placement;placement.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(0,0,1)),0.5);placement.SetTranslationPart(gp_Vec(10,20,30));
    const auto box=BRepPrimAPI_MakeBox(100,80,60).Shape();
    const auto cylinder=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(300,0,0),gp_Dir(0,0,1)),20,50).Shape();
    Model model;model.parts={{"box","Box 1",box,placement,true},{"cyl","Cylinder 2",cylinder,gp_Trsf(),true}};
    Construction::Object wp;wp.id="wp";wp.name="WP1";wp.kind=Construction::Kind::Workplane;
    wp.frame=gp_Ax3(gp_Pnt(5,6,7),gp_Dir(1,0,0),gp_Dir(0,1,0)); // local X = world Y, local Y = world Z
    Construction::Object axis;axis.id="ax";axis.name="AX1";axis.kind=Construction::Kind::Axis;axis.frame=gp_Ax3(gp_Pnt(1,2,3),gp_Dir(1,1,0));
    Construction::Object point;point.id="pt";point.name="PT1";point.kind=Construction::Kind::Point;point.frame=gp_Ax3(gp_Pnt(7,8,9),gp_Dir(0,0,1));
    model.constructions={wp,axis,point};
    model.sketches={entity("seg","wp",Sketch::Segment{{0,0},{30,40}}),entity("line","wp",Sketch::InfiniteLine{{0,10},{2,0}}),
                    entity("circle","wp",Sketch::Circle{{50,0},10}),entity("cp","wp",Sketch::ConstructionPoint{{-5,-6}}),
                    entity("cross","wp",Sketch::Segment{{0,40},{40,0}})};
    const Sketch::Ellipse ellipse{{4,5},8,3,.4};
    model.sketches.push_back(entity("ellipse","wp",ellipse));
    const Sketch::Spline spline{{{2,3},{7,8},{12,4}},false,Sketch::Point{2,1},Sketch::Point{3,-1}};
    model.sketches.push_back(entity("spline","wp",spline));
    Resolved r;QString error;
    check(resolve(model,{Kind::SketchLine,"ellipse"},r,error) && r.line && r.line->Direction().IsEqual(gp_Dir(gp_Vec(wp.frame.XDirection())*std::cos(.4)+gp_Vec(wp.frame.YDirection())*std::sin(.4)),1.e-12),"ellipse major direction wrong");
    Target splineEnd{Kind::SketchLine,"spline"};splineEnd.point.feature=Sketch::PointFeature::End;
    check(resolve(model,splineEnd,r,error) && r.line && r.line->Direction().IsEqual(gp_Dir(gp_Vec(wp.frame.XDirection())*3-gp_Vec(wp.frame.YDirection())),1.e-12),"spline end tangent reference wrong");
    // Part: placement frame.
    check(resolve(model,{Kind::Part,"box"},r,error) && r.plane && near(*r.point,gp_Pnt(10,20,30)),"Part reference");
    // Face: planar top face, oriented outward normal and centroid, with placement.
    const auto top=planarFace(box,gp_Dir(0,0,1));
    check(resolve(model,{Kind::Face,"box",top},r,error),qPrintable(error));
    check(r.plane && r.plane->Direction().IsEqual(gp_Dir(0,0,1).Transformed(placement),1.e-12) && near(*r.point,gp_Pnt(50,40,60).Transformed(placement)),"planar Face reference");
    gp_Dir d;check(direction(r,d,error) && d.IsEqual(gp_Dir(0,0,1),1.e-12),"Face direction is not its outward normal");
    // Face: cylindrical wall supplies its axis.
    TopoDS_Face wall;for(TopExp_Explorer it(cylinder,TopAbs_FACE);it.More();it.Next()) if(BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType()==GeomAbs_Cylinder) wall=TopoDS::Face(it.Current());
    check(resolve(model,{Kind::Face,"cyl",wall},r,error) && r.line && r.line->Direction().IsParallel(gp_Dir(0,0,1),1.e-12)
        && r.line->Location().Distance(gp_Pnt(300,0,r.line->Location().Z()))<1.e-9,"cylindrical Face axis reference");
    // Edge: straight edge in its orientation; circular edge gives its axis and center.
    const auto vertical=edgeAlong(box,gp_Pnt(100,0,0),gp_Pnt(100,0,60));
    for(const auto orientation:{TopAbs_FORWARD,TopAbs_REVERSED}) {
        auto e=TopoDS::Edge(vertical.Oriented(orientation));
        check(resolve(model,{Kind::Edge,"box",e},r,error),qPrintable(error));
        check(r.line && r.line->Direction().IsEqual(orientedEdge(e,placement),1.e-12),"straight Edge direction ignores orientation/placement");
        check(near(*r.point,gp_Pnt(100,0,30).Transformed(placement)),"straight Edge midpoint");
    }
    TopoDS_Edge rim;for(TopExp_Explorer it(cylinder,TopAbs_EDGE);it.More();it.Next()) if(BRepAdaptor_Curve(TopoDS::Edge(it.Current())).GetType()==GeomAbs_Circle) {rim=TopoDS::Edge(it.Current());break;}
    check(resolve(model,{Kind::Edge,"cyl",rim},r,error) && r.line && r.line->Direction().IsParallel(gp_Dir(0,0,1),1.e-12)
        && std::abs(r.point->X()-300)<1.e-9 && std::abs(r.point->Y())<1.e-9,"circular Edge center/axis reference");
    // Vertex.
    const auto corner=TopExp::FirstVertex(vertical);
    check(resolve(model,{Kind::Vertex,"box",corner},r,error) && near(*r.point,BRep_Tool::Pnt(corner).Transformed(placement)),"Vertex reference");
    check(!direction(r,d,error) && !error.isEmpty(),"Vertex supplied a direction");
    // Construction objects.
    check(resolve(model,{Kind::Axis,"ax"},r,error) && r.line->Direction().IsEqual(gp_Dir(1,1,0),1.e-12) && near(*r.point,gp_Pnt(1,2,3)),"Axis reference");
    check(resolve(model,{Kind::Workplane,"wp"},r,error) && r.plane->Direction().IsEqual(gp_Dir(1,0,0),1.e-12),"Workplane reference");
    check(direction(r,d,error) && d.IsEqual(gp_Dir(1,0,0),1.e-12),"Workplane direction is not its normal");
    check(resolve(model,{Kind::Point,"pt"},r,error) && near(*r.point,gp_Pnt(7,8,9)),"Point reference");
    check(!resolve(model,{Kind::Point,"ax"},r,error) && !error.isEmpty(),"Axis accepted as Point");
    // Sketch line: Segment start->end and Infinite Line in the Workplane frame.
    check(resolve(model,{Kind::SketchLine,"seg"},r,error),qPrintable(error));
    check(r.line->Direction().IsEqual(gp_Dir(0,0.6,0.8),1.e-12) && near(r.line->Location(),gp_Pnt(5,6,7)) && near(*r.point,gp_Pnt(5,6+15,7+20)),"Sketch Segment reference");
    check(resolve(model,{Kind::SketchLine,"line"},r,error) && r.line->Direction().IsEqual(gp_Dir(0,1,0),1.e-12) && near(r.line->Location(),gp_Pnt(5,6,17)),"Sketch Infinite Line reference");
    check(!resolve(model,{Kind::SketchLine,"circle"},r,error),"circle accepted as Sketch line");
    // Sketch points: features and intersection.
    const auto sketchPoint=[&](const QString& id,Sketch::PointFeature f,QString other={},int index=0) {
        Target t{Kind::SketchPoint,id};t.point={id,other,f,index};
        check(resolve(model,t,r,error),qPrintable(error));return *r.point;
    };
    check(near(sketchPoint("seg",Sketch::PointFeature::End),gp_Pnt(5,36,47)),"Sketch end point");
    check(near(sketchPoint("seg",Sketch::PointFeature::Midpoint),gp_Pnt(5,21,27)),"Sketch midpoint");
    check(near(sketchPoint("circle",Sketch::PointFeature::Center),gp_Pnt(5,56,7)),"Sketch center");
    check(near(sketchPoint("cp",Sketch::PointFeature::Position),gp_Pnt(5,1,1)),"Sketch construction point");
    check(near(sketchPoint("seg",Sketch::PointFeature::Intersection,"cross",0),Sketch::worldPoint({120./7,160./7},wp.frame),1.e-9),"Sketch intersection");
    {Target t{Kind::SketchPoint,"circle"};t.point={"circle",{},Sketch::PointFeature::Midpoint,0};check(!resolve(model,t,r,error) && !error.isEmpty(),"circle midpoint accepted");}
    // Point to Point.
    Resolved a,b;double length=0;
    check(resolve(model,{Kind::Point,"pt"},a,error) && resolve(model,{Kind::Axis,"ax"},b,error),"P2P sources");
    check(pointToPoint(a,b,d,length,error) && d.IsEqual(gp_Dir(gp_Vec(gp_Pnt(7,8,9),gp_Pnt(1,2,3))),1.e-12) && std::abs(length-std::sqrt(108.))<1.e-12,"Point to Point");
    check(!pointToPoint(a,a,d,length,error) && !error.isEmpty(),"zero-length Point to Point accepted");
    // Part transformation: subshape references follow the new placement.
    {
        auto moved=model;gp_Trsf shift;shift.SetTranslation(gp_Vec(0,0,25));moved.parts[0].placement=shift*placement;
        check(resolve(moved,{Kind::Face,"box",top},r,error) && near(*r.point,gp_Pnt(50,40,85).Transformed(placement)),"Face did not follow Part transform");
        check(resolve(moved,{Kind::Edge,"box",vertical},r,error) && r.line->Direction().IsEqual(orientedEdge(vertical,shift*placement),1.e-12),"Edge did not follow Part transform");
    }
    // Workplane transformation: Sketch references follow their owner.
    {
        auto moved=model;moved.constructions[0].frame.Translate(gp_Vec(0,0,100));
        check(resolve(moved,{Kind::SketchLine,"seg"},r,error) && near(r.line->Location(),gp_Pnt(5,6,107)),"Sketch line did not follow Workplane");
    }
    // Stale / deleted / hidden / locked references are refused, never rebound.
    {
        auto edited=model;edited.parts[0].shape=BRepPrimAPI_MakeBox(100,80,60).Shape(); // equal geometry, new topology
        check(!resolve(edited,{Kind::Face,"box",top},r,error) && error.contains("no longer exists"),"stale Face rebound to new topology");
        check(!resolve(edited,{Kind::Edge,"box",vertical},r,error) && error.contains("no longer exists"),"stale Edge rebound");
        check(!resolve(edited,{Kind::Vertex,"box",corner},r,error) && error.contains("no longer exists"),"stale Vertex rebound");
        auto removed=model;removed.parts.erase(removed.parts.begin());
        check(!resolve(removed,{Kind::Face,"box",top},r,error) && !error.isEmpty(),"deleted Part reference accepted");
        auto hidden=model;hidden.parts[0].visible=false;check(!resolve(hidden,{Kind::Edge,"box",vertical},r,error),"hidden Part reference accepted");
        auto noAxis=model;noAxis.constructions.erase(noAxis.constructions.begin()+1);check(!resolve(noAxis,{Kind::Axis,"ax"},r,error),"deleted Axis accepted");
        auto locked=model;locked.constructions[1].selectable=false;check(!resolve(locked,{Kind::Axis,"ax"},r,error),"locked Axis accepted");
        auto noWp=model;noWp.constructions.erase(noWp.constructions.begin());check(!resolve(noWp,{Kind::SketchLine,"seg"},r,error),"Sketch line without Workplane accepted");
        auto noSeg=model;noSeg.sketches.erase(noSeg.sketches.begin());check(!resolve(noSeg,{Kind::SketchLine,"seg"},r,error),"deleted Sketch line accepted");
        auto apart=model;apart.sketches[4].geometry=Sketch::Segment{{100,100},{120,90}};
        Target t{Kind::SketchPoint,"seg"};t.point={"seg","cross",Sketch::PointFeature::Intersection,0};
        check(!resolve(apart,t,r,error) && error.contains("no longer exists"),"vanished intersection rebound");
        check(!resolve(model,{Kind::Face,"box",TopoDS_Shape()},r,error),"null subshape accepted");
        check(!resolve(model,{Kind::Face,"box",vertical},r,error),"Edge accepted as Face");
    }
    // Snapping and picking resolve to the same reference and point.
    {
        const auto targets=Sketch::snapTargets(model.sketches,"wp",{1000,1000},{},true);
        size_t referenced=0;
        for(const auto& target:targets) {
            if(target.kind==Sketch::SnapKind::Nearest || target.kind==Sketch::SnapKind::Tangent) {check(!target.reference,"cursor-dependent snap carries a reference");continue;}
            check(target.reference.has_value(),"snap target without reference");++referenced;
            const auto local=Sketch::resolvePoint(model.sketches,*target.reference,error);
            check(local && local->x==target.point.x && local->y==target.point.y,"snap reference does not re-evaluate to the snap point");
            Target t{Kind::SketchPoint,target.reference->entity};t.point=*target.reference;
            check(resolve(model,t,r,error) && near(*r.point,Sketch::worldPoint(target.point,wp.frame),0),"resolved reference differs from snapped point");
        }
        check(referenced>=18,"expected endpoint/midpoint/center/position/intersection snap references");
    }
    std::cout<<"Reference resolution, transforms, stale refusal and snap identity passed.\n";return 0;
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
