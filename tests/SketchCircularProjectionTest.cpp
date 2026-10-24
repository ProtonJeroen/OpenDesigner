#include "SketchProjection.h"
#include "SketchSnapping.h"
#include "SketchConstruction.h"
#include "SketchOverdraw.h"
#include "NativeDocument.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Geom_Circle.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Iterator.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
using namespace SketchProjection;
using namespace Sketch;
constexpr double pi=std::numbers::pi,turn=2*pi;
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
bool near(double a,double b,double tolerance=1.e-10) {return std::abs(a-b)<=tolerance;}
bool near(Point a,Point b,double tolerance=1.e-10) {return std::hypot(a.x-b.x,a.y-b.y)<=tolerance;}
Construction::Object plane() {Construction::Object p;p.id="target";p.name="WP1";p.kind=Construction::Kind::Workplane;return p;}
gp_Circ circle(gp_Pnt center=gp_Pnt(),double radius=10,gp_Dir normal=gp_Dir(0,0,1)) {return {gp_Ax2(center,normal,gp_Dir(1,0,0)),radius};}
Source full(gp_Circ c=circle()) {return {BRepBuilderAPI_MakeEdge(c).Shape(),{},"circle"};}
Source arc(double start,double sweep,gp_Circ c=circle()) {
    auto shape=sweep>0?BRepBuilderAPI_MakeEdge(c,start,start+sweep).Shape():BRepBuilderAPI_MakeEdge(c,start+sweep,start).Shape().Reversed();
    return {shape,{},"arc"};
}
void ok(const Result& result,size_t count) {
    if(!result.success) for(const auto& d:result.diagnostics) std::cerr<<int(d.code)<<": "<<d.message.toStdString()<<'\n';
    check(result.success && result.additions.size()==count,"unexpected circular projection result");
}
void refused(const Result& result,Code code) {
    check(!result.success && result.additions.empty() && !result.changed(),"invalid circular batch was partly applied");
    bool found=false;for(const auto& d:result.diagnostics) if(d.code==code && d.sourceIndex && !d.message.isEmpty()) found=true;
    if(!found) {std::cerr<<"Expected diagnostic "<<int(code)<<'\n';for(const auto& d:result.diagnostics) std::cerr<<int(d.code)<<": "<<d.message.toStdString()<<'\n';}
    check(found,"missing circular source diagnostic");
}
Point at(const Arc& a,bool end=false) {const auto t=a.startAngle+(end?a.sweepAngle:0);return {a.center.x+a.radius*std::cos(t),a.center.y+a.radius*std::sin(t)};}
Point local(const gp_Pnt& p,const Construction::Object& wp) {const gp_Vec d(wp.frame.Location(),p);return {d.Dot(gp_Vec(wp.frame.XDirection())),d.Dot(gp_Vec(wp.frame.YDirection()))};}
void endpoints(const Source& source,const Construction::Object& wp,const Arc& result) {
    BRepAdaptor_Curve curve(TopoDS::Edge(source.shape));const bool reversed=source.shape.Orientation()==TopAbs_REVERSED;
    const auto a=curve.Value(reversed?curve.LastParameter():curve.FirstParameter()).Transformed(source.partPlacement);
    const auto b=curve.Value(reversed?curve.FirstParameter():curve.LastParameter()).Transformed(source.partPlacement);
    check(near(at(result),local(a,wp)) && near(at(result,true),local(b,wp)),"arc endpoints were mirrored, changed or reversed incorrectly");
}
gp_Trsf rigid() {gp_Trsf t;t.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),0.73);t.SetTranslationPart(gp_Vec(12,-30,50));return t;}
std::string brep(const TopoDS_Shape& s) {std::ostringstream out;BRepTools::Write(s,out);return out.str();}
void geometryAndOrientation() {
    auto wp=plane();
    for(double z:{0.,-100.,250.}) {
        auto source=full(circle({3.125,-4.25,z},12.5));
        for(bool reverse:{false,true}) {
            if(reverse) source.shape.Reverse();const auto result=project({source},wp);ok(result,1);
            const auto g=std::get<Circle>(result.additions[0].geometry);check(g.center==Point{3.125,-4.25} && g.radius==12.5,"circle center/radius changed");
        }
    }
    for(double start:{0.,0.37,350*pi/180,-30*pi/180}) for(double sweep:{pi/2,pi,3*pi/2,-pi/2,-pi,-3*pi/2}) for(bool reverse:{false,true}) {
        auto source=arc(start,sweep,circle({2,3,19}));if(reverse) source.shape.Reverse();
        const auto result=project({source},wp);ok(result,1);const auto g=std::get<Arc>(result.additions[0].geometry);
        check(near(g.sweepAngle,reverse?-sweep:sweep),"CW/CCW or 90/180/270 sweep changed");endpoints(source,wp,g);
        auto opposite=wp;opposite.frame=gp_Ax3(gp_Pnt(0,0,-7),gp_Dir(0,0,-1),gp_Dir(1,0,0));
        const auto flipped=project({source},opposite);ok(flipped,1);const auto f=std::get<Arc>(flipped.additions[0].geometry);
        check(near(f.sweepAngle,-g.sweepAngle),"reversed Workplane normal did not reverse local sweep");endpoints(source,opposite,f);
    }
    auto negativeNormal=arc(0.4,pi/2,circle({1,2,4},8,gp_Dir(0,0,-1)));
    const auto negative=project({negativeNormal},wp);ok(negative,1);check(std::get<Arc>(negative.additions[0].geometry).sweepAngle<0,"source circle basis handedness ignored");
    endpoints(negativeNormal,wp,std::get<Arc>(negative.additions[0].geometry));
    // An almost full edge with a genuine parameter gap must remain an Arc.
    auto almost=arc(0,turn-1.e-8,circle({},1));auto result=project({almost},wp);ok(result,1);
    check(std::holds_alternative<Arc>(result.additions[0].geometry),"almost full arc silently became a circle");
    auto seam=Source{BRepBuilderAPI_MakeEdge(circle(),0.7,0.7+turn).Shape(),{},"full shifted period"};
    result=project({seam},wp);ok(result,1);check(std::holds_alternative<Circle>(result.additions[0].geometry),"full period with nonzero seam became an arc");
    // Geometric closure does not require the two endpoint vertices to share ID.
    BRep_Builder builder;TopoDS_Edge open;Handle(Geom_Circle) exact=new Geom_Circle(circle());
    builder.MakeEdge(open,exact,1.e-7);builder.Range(open,0,turn);
    auto first=BRepBuilderAPI_MakeVertex(gp_Pnt(10,0,0)).Vertex(),last=BRepBuilderAPI_MakeVertex(gp_Pnt(10,0,0)).Vertex();
    first.Orientation(TopAbs_FORWARD);last.Orientation(TopAbs_REVERSED);
    builder.Add(open,first);builder.Add(open,last);builder.UpdateVertex(first,0,open,1.e-7);builder.UpdateVertex(last,turn,open,1.e-7);
    check(!BRep_Tool::IsClosed(open),"geometric-closure fixture unexpectedly shares its endpoint vertices");
    result=project({{open,{},"geometrically closed circle"}},wp);ok(result,1);
    check(std::holds_alternative<Circle>(result.additions[0].geometry),"geometric full circle depended on source topology history");
}
void placements() {
    auto wp=plane();const auto transform=rigid();wp.frame.Transform(transform);
    const auto displaced=Sketch::worldPoint({12,-7},wp.frame).Translated(gp_Vec(wp.frame.Direction())*83);
    const auto worldCircle=full(gp_Circ(gp_Ax2(displaced,wp.frame.Direction(),wp.frame.XDirection()),9));
    auto independent=project({worldCircle},wp);ok(independent,1);
    check(near(std::get<Circle>(independent.additions[0].geometry).center,{12,-7}),"independently transformed Workplane projection incorrect");
    for(auto source:{full(circle({3,4,21},7)),arc(5.8,-3*pi/2,circle({3,4,21},7))}) {
        source.partPlacement=transform;const auto base=source;
        auto result=project({source},wp);ok(result,1);
        if(const auto* a=std::get_if<Arc>(&result.additions[0].geometry)) endpoints(source,wp,*a);
        else check(near(std::get<Circle>(result.additions[0].geometry).center,{3,4}),"rotated Part/Workplane circle center incorrect");
        // Nested compound locations are consumed once, in addition to Part placement.
        gp_Trsf child;child.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(0,0,1)),0.31);child.SetTranslationPart(gp_Vec(5,6,9));
        gp_Trsf parent;parent.SetTranslation(gp_Vec(-4,7,-20));BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);
        builder.Add(compound,source.shape.Moved(TopLoc_Location(child)));compound.Move(TopLoc_Location(parent));
        source.shape=TopoDS_Iterator(compound).Value();result=project({source},wp);ok(result,1);
        if(const auto* a=std::get_if<Arc>(&result.additions[0].geometry)) endpoints(source,wp,*a);
        else check(near(std::get<Circle>(result.additions[0].geometry).center,local(gp_Pnt(3,4,21).Transformed(child).Transformed(parent).Transformed(transform),wp)),"nested circular location doubled/ignored");
        auto baked=source;baked.shape=baked.shape.Moved(TopLoc_Location(transform));baked.partPlacement=gp_Trsf();
        const auto world=project({baked},wp);ok(world,1);
        if(const auto* a=std::get_if<Arc>(&world.additions[0].geometry)) endpoints(baked,wp,*a);
        // Circle's stored curve location (not just TopoDS edge location).
        auto stored=base;auto edge=TopoDS::Edge(stored.shape);TopLoc_Location location;double f,l;
        const auto curve=BRep_Tool::Curve(edge,location,f,l);const auto shift=gp_Trsf(child);
        builder.UpdateEdge(edge,curve,TopLoc_Location(shift),1.e-7);
        for(auto vertex:{TopExp::FirstVertex(edge),TopExp::LastVertex(edge)}) {
            const auto p=BRep_Tool::Pnt(vertex);builder.UpdateVertex(vertex,p.Transformed(shift),1.e-7);
            if(TopExp::FirstVertex(edge).IsSame(TopExp::LastVertex(edge))) break;
        }
        result=project({stored},wp);ok(result,1);if(const auto* a=std::get_if<Arc>(&result.additions[0].geometry)) endpoints(stored,wp,*a);
    }
    // External reflection changes local handedness; uniform scale changes radius.
    for(bool reflection:{false,true}) {
        auto source=arc(0.2,pi/2);source.partPlacement.SetScale(gp_Pnt(),2);
        if(reflection) {gp_Trsf mirror;mirror.SetMirror(gp_Ax2(gp_Pnt(),gp_Dir(1,0,0)));source.partPlacement=mirror*source.partPlacement;}
        const auto result=project({source},plane());ok(result,1);const auto a=std::get<Arc>(result.additions[0].geometry);
        check(near(a.radius,20) && near(a.sweepAngle,reflection?-pi/2:pi/2),"scale/reflection circular orientation incorrect");endpoints(source,plane(),a);
    }
    auto distant=plane();distant.frame.SetLocation({1.e20,-1.e20,1.e20});auto source=full(circle({1,2,3},10));
    source.partPlacement.SetTranslation(gp_Vec(1.e20,-1.e20,1.e20));const auto result=project({source},distant);ok(result,1);
    check(std::get<Circle>(result.additions[0].geometry).center==Point{1,2},"large common translations erased circular local coordinates");
}
void duplicateAndOverlap() {
    const auto wp=plane();auto c=full();auto reverse=c;reverse.shape.Reverse();
    auto result=project({c,reverse,full(circle({0,0,300}))},wp);ok(result,1);check(result.diagnostics.size()==2,"full circle duplicates not skipped");
    for(double sweep:{pi/2,pi,3*pi/2}) {
        auto a=arc(5.8,sweep);auto b=a;b.shape.Reverse();auto r=project({a,b},wp);ok(r,1);
        const auto existing=r.additions;r=project({b},wp,existing);ok(r,0);check(!r.changed() && r.diagnostics.front().existingEntityId==existing.front().id,"reversed arc not duplicate existing Construction");
    }
    const auto a=arc(0,pi),b=arc(pi/2,pi),contained=arc(pi/4,pi/4);
    for(const auto& sources:{std::vector{a,b},std::vector{b,a},std::vector{a,contained},std::vector{c,a},std::vector{a,c},std::vector{arc(350*pi/180,40*pi/180),arc(0,pi/4)}})
        refused(project(sources,wp),Code::PartialOverlap);
    auto existing=project({c},wp).additions;existing[0].visible=false;existing[0].selectable=false;
    refused(project({a},wp,existing),Code::PartialOverlap);ok(project({reverse},wp,existing),0);
    existing=project({a},wp).additions;refused(project({c},wp,existing),Code::PartialOverlap);
    auto profile=existing;profile[0].role=Role::Profile;ok(project({a},wp,profile),1);
    existing[0].workplaneId="another-plane";ok(project({a},wp,existing),1);
    // Opposite semicircles share endpoints but not the same geometric domain.
    ok(project({arc(0,pi),arc(pi,pi)},wp),2);
    ok(project({arc(350*pi/180,10*pi/180),arc(0,pi/2)},wp),2);
    ok(project({arc(0,pi/2),arc(pi,pi/2)},wp),2);
    ok(project({full(circle()),full(circle({0,0,0},12)),full(circle({25,0,0},10))},wp),3);
    refused(project({arc(0,pi),arc(pi-1.e-9,pi/2)},wp),Code::PartialOverlap); // Less than linear tolerance, still real overlap.
    auto shifted=c;shifted.partPlacement.SetTranslation(gp_Vec(0.5e-7,0,0));ok(project({c,shifted},wp),1);
    shifted.partPlacement.SetTranslation(gp_Vec(2.e-7,0,0));ok(project({c,shifted},wp),2);
    // A tiny arc must not be mistaken for its almost-full complement.
    auto small=arc(0,1.e-6),large=arc(1.e-6,turn-1.e-6);ok(project({small,large},wp),2);
}
void failuresAndTransactions() {
    const auto wp=plane();auto tilted=full(circle({0,0,0},10,gp_Dir(0,1,1)));
    for(auto bad:{tilted,arc(0,pi/2,circle({0,0,0},10,gp_Dir(0,1,1))),full(circle({0,0,0},10,gp_Dir(0,1,0))),full(circle({0,0,0},10,gp_Dir(0,1.e-10,1)))}) {
        refused(project({bad},wp),Code::NonCircularProjection); // Including near-parallel but genuinely oblique.
        Source vertex{BRepBuilderAPI_MakeVertex(gp_Pnt(1,2,3)).Shape(),{},"vertex"};
        refused(project({vertex,full(),bad},wp),Code::NonCircularProjection);
    }
    auto flagged=arc(0,pi/2);BRep_Builder builder;builder.Degenerated(TopoDS::Edge(flagged.shape),true);refused(project({flagged},wp),Code::DegenerateEdge);
    refused(project({full(circle({},1.e25,gp_Dir(0,1.e-15,1)))},wp),Code::NumericalFailure);
    auto malformed=arc(0,pi/2);builder.UpdateVertex(TopExp::FirstVertex(TopoDS::Edge(malformed.shape)),gp_Pnt(100,200,300),1.e-7);
    refused(project({malformed},wp),Code::InvalidBRep);
    refused(project({full(circle({},0.5e-7))},wp),Code::DegenerateEdge);
    refused(project({arc(0,1.e-6)},wp,{},1.e-4),Code::DegenerateEdge);
    auto invalid=full();invalid.partPlacement.SetTranslation(gp_Vec(std::numeric_limits<double>::quiet_NaN(),0,0));refused(project({invalid},wp),Code::InvalidPlacement);
    auto internal=full();internal.shape.Orientation(TopAbs_INTERNAL);refused(project({internal},wp),Code::InvalidOrientation);
    auto noncircle=Source{BRepBuilderAPI_MakeEdge(gp_Elips(gp_Ax2(),10,5)).Shape(),{},"ellipse"};refused(project({noncircle},wp),Code::NonlinearEdge);
    Source vertex{BRepBuilderAPI_MakeVertex(gp_Pnt(2,4,8)).Shape(),{},"Part A / vertex"};
    Source line{BRepBuilderAPI_MakeEdge(gp_Pnt(-20,-20,8),gp_Pnt(20,-20,10)).Shape(),{},"Part B / line"};
    auto a=arc(0,pi/2,circle({40,0,5}));auto c=full();
    const auto before=brep(a.shape);auto result=project({vertex,line,c,a},wp);ok(result,4);const auto saved=result.additions;
    std::set<QString> ids;for(const auto& e:saved) check(e.role==Role::Construction && e.workplaneId==wp.id && !QUuid(e.id).isNull() && ids.insert(e.id).second,"circular result identity/role/ownership invalid");
    check(brep(a.shape)==before,"circular source was modified");a.shape.Nullify();c.partPlacement.SetTranslation(gp_Vec(100,200,300));check(result.additions==saved,"projected values depend on source edits/deletion");
    QString error;QTemporaryDir temp;NativeDocumentSnapshot doc;doc.constructions={wp};doc.sketchEntities=saved;NativeDocumentSnapshot loaded;
    check(NativeDocumentStorage::save(temp.filePath("mixed.odc"),doc,error) && NativeDocumentStorage::load(temp.filePath("mixed.odc"),loaded,error) && loaded.sketchEntities==saved,"mixed circular native v4 roundtrip changed exact values");
    doc.schemaVersion=3;doc.sketchEntities.erase(doc.sketchEntities.begin());
    check(NativeDocumentStorage::save(temp.filePath("curves.odc"),doc,error) && NativeDocumentStorage::load(temp.filePath("curves.odc"),loaded,error) && loaded.sketchEntities==doc.sketchEntities,"circular native v3 roundtrip changed exact values");
    const auto& curve=saved[2].geometry;const auto& curvedArc=saved[3].geometry;
    check(!snapTargets(saved,wp.id,{10,0}).empty() && !tangentPoints({20,0},curve).empty(),"projected circular values do not support snapping/tangency");
    Geometry offset;check(!offsetConstructionLine(curvedArc,5,LineOutput::Infinite,{},offset,error),"projected arc entered line-only offset tool");
    std::vector<CommonTangent> tangents;check(commonTangents(curve,curvedArc,tangents,error) && !tangents.empty(),"projected circles/arcs rejected by common tangent tool");
    OverdrawGraph graph;check(overdrawCandidates(saved,wp.id,graph,error),"projected circular geometry rejected by Overdraw");
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {geometryAndOrientation();placements();duplicateAndOverlap();failuresAndTransactions();std::cout<<"Exact circular projection, orientation, overlap, independence and native storage passed.\n";return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
