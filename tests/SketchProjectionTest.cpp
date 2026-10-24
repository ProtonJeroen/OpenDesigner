#include "SketchProjection.h"
#include "NativeDocument.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Geom_Line.hxx>
#include <Poly_Polygon3D.hxx>
#include <Precision.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Iterator.hxx>
#include <gp_Circ.hxx>
#include <gp_Hypr.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
using namespace SketchProjection;
using Sketch::Point;
using Sketch::Segment;
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
Construction::Object plane() {
    Construction::Object wp;wp.kind=Construction::Kind::Workplane;wp.id="target-plane";wp.name="WP1";return wp;
}
Source vertex(double x,double y,double z) {return {BRepBuilderAPI_MakeVertex(gp_Pnt(x,y,z)).Shape(),{},"vertex"};}
Source edge(gp_Pnt a,gp_Pnt b) {return {BRepBuilderAPI_MakeEdge(a,b).Shape(),{},"edge"};}
gp_Trsf rigid(gp_Dir axis,double angle,gp_Vec translation) {
    gp_Trsf t;t.SetRotation(gp_Ax1(gp_Pnt(),axis),angle);t.SetTranslationPart(translation);return t;
}
bool near(Point a,Point b,double tolerance=1.e-11) {return std::hypot(a.x-b.x,a.y-b.y)<=tolerance;}
Point position(const Result& r,size_t i=0) {return std::get<Sketch::ConstructionPoint>(r.additions.at(i).geometry).position;}
Segment span(const Result& r,size_t i=0) {return std::get<Segment>(r.additions.at(i).geometry);}
void ok(const Result& r,size_t count) {
    if(!r.success) for(const auto& d:r.diagnostics) std::cerr<<d.message.toStdString()<<'\n';
    check(r.success && r.additions.size()==count,"unexpected projection result");
}
void refused(const Result& r,Code code,size_t source=0) {
    check(!r.success && !r.changed() && r.additions.empty(),"failed batch returned partial geometry");
    check(std::any_of(r.diagnostics.begin(),r.diagnostics.end(),[&](const auto& d){return d.code==code && d.sourceIndex==source && d.isError() && !d.message.isEmpty();}),"missing source-specific failure diagnostic");
}
Point inverse(const gp_Pnt& p,const Construction::Object& wp) {
    gp_Trsf local;local.SetTransformation(wp.frame);const auto q=p.Transformed(local);return {q.X(),q.Y()};
}
std::string brep(const TopoDS_Shape& s) {std::ostringstream out;BRepTools::Write(s,out);return out.str();}
void basics() {
    const auto wp=plane();
    auto result=project({vertex(1.234567891234567,-7.987654321234567,0),vertex(5,6,1234),vertex(-3,9,-85)},wp);ok(result,3);
    check(position(result)==Point{1.234567891234567,-7.987654321234567} && position(result,1)==Point{5,6} && position(result,2)==Point{-3,9},"vertices lost exact XY or used normal distance");
    auto line=edge({1,2,5},{11,2,5});auto reversed=line;reversed.shape.Reverse();
    result=project({line},wp);ok(result,1);check(span(result)==Segment{{1,2},{11,2}},"parallel edge projection incorrect");
    result=project({reversed},wp);ok(result,1);check(span(result)==Segment{{11,2},{1,2}},"reversed edge orientation lost");
    result=project({edge({-8,3,-11},{12,10,84})},wp);ok(result,1);check(near(span(result).start,{-8,3}) && near(span(result).end,{12,10}),"inclined edge projection incorrect");
    refused(project({edge({2,3,-10},{2,3,40})},wp),Code::CollapsedProjection);
    refused(project({edge({0,0,0},{1.e-8,0,100})},wp),Code::CollapsedProjection);
    Source negative{BRepBuilderAPI_MakeEdge(gp_Lin(gp_Pnt(3,4,8),gp_Dir(1,0,0)),-8,12).Shape(),{},"negative parameters"};
    result=project({negative},wp);ok(result,1);check(span(result)==Segment{{-5,4},{15,4}},"trimmed line parameters ignored");
    // Neither a fake polygon nor its samples may become model data.
    BRep_Builder builder;TColgp_Array1OfPnt samples(1,2);samples(1)=gp_Pnt(1000,2000,3000);samples(2)=gp_Pnt(4000,5000,6000);
    builder.UpdateEdge(TopoDS::Edge(line.shape),new Poly_Polygon3D(samples));
    const auto before=brep(line.shape);result=project({line},wp);ok(result,1);
    check(span(result)==Segment{{1,2},{11,2}} && brep(line.shape)==before,"projection used or changed tessellation/source B-Rep");
    for(const auto& e:result.additions) check(e.role==Sketch::Role::Construction && e.workplaneId==wp.id && !e.id.isEmpty() && e.visible && e.selectable,"projected entity properties incorrect");
}
void placements() {
    const auto base=plane();auto wp=base;
    const auto common=rigid(gp_Dir(1,2,3),0.73,gp_Vec(35,-42,17));wp.frame.Transform(common);wp.displayOffset=gp_Vec(800,-300,0);wp.extent=0.1;
    auto v=vertex(12.125,-7.75,39);auto e=edge({-15,25,10},{30,-5,75});v.partPlacement=common;e.partPlacement=common;
    auto result=project({v,e},wp);ok(result,2);
    check(near(position(result),{12.125,-7.75}) && near(span(result,1).start,{-15,25}) && near(span(result,1).end,{30,-5}),"rigid equivariance or display-offset independence failed");
    // Compound traversal accumulates child + ancestor locations, independently
    // of the outer Part placement (same convention as AIS owner references).
    const auto child=rigid(gp_Dir(0,1,0),0.31,gp_Vec(5,9,13));
    const auto parent=rigid(gp_Dir(1,0,0),-0.51,gp_Vec(-12,24,8));
    const auto part=rigid(gp_Dir(0,0,1),1.1,gp_Vec(70,-5,36));
    const gp_Pnt a(1,2,3),b(9,7,-4),p(-4,11,8);
    BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);
    builder.Add(compound,BRepBuilderAPI_MakeVertex(p).Shape().Moved(TopLoc_Location(child)));
    builder.Add(compound,BRepBuilderAPI_MakeEdge(a,b).Shape().Moved(TopLoc_Location(child)));
    compound.Move(TopLoc_Location(parent));TopoDS_Iterator it(compound);
    Source locatedVertex{it.Value(),part,"Part A / vertex"};it.Next();Source locatedEdge{it.Value(),part,"Part A / edge"};
    const auto shapeLocation=locatedEdge.shape.Location();const auto before=brep(locatedEdge.shape);
    result=project({locatedVertex,locatedEdge},wp);ok(result,2);
    const auto world=[&](gp_Pnt q){return q.Transformed(child).Transformed(parent).Transformed(part);};
    check(near(position(result),inverse(world(p),wp)) && near(span(result,1).start,inverse(world(a),wp))
        && near(span(result,1).end,inverse(world(b),wp)),"Part/subshape placement applied in wrong order or more than once");
    check(locatedEdge.shape.Location()==shapeLocation && brep(locatedEdge.shape)==before,"projection mutated source location/B-Rep");
    auto baked=locatedEdge;baked.shape=baked.shape.Moved(TopLoc_Location(part));baked.partPlacement=gp_Trsf();
    const auto worldResult=project({baked},wp);ok(worldResult,1);
    check(near(span(worldResult).start,span(result,1).start) && near(span(worldResult).end,span(result,1).end),"explicit world-space/identity contract failed");
    // Curve representation locations are also consumed by OCCT, not reapplied.
    auto internal=edge({0,0,0},{10,0,0});const auto localEdge=TopoDS::Edge(internal.shape);
    TopLoc_Location ignored;double first,last;const auto curve=BRep_Tool::Curve(localEdge,ignored,first,last);
    const gp_Trsf curveShift=rigid(gp_Dir(0,0,1),0.4,gp_Vec(4,8,12));
    builder.UpdateEdge(localEdge,curve,TopLoc_Location(curveShift),1.e-7);
    builder.UpdateVertex(TopExp::FirstVertex(localEdge),gp_Pnt(0,0,0).Transformed(curveShift),1.e-7);
    builder.UpdateVertex(TopExp::LastVertex(localEdge),gp_Pnt(10,0,0).Transformed(curveShift),1.e-7);
    internal.shape.Move(TopLoc_Location(child));internal.partPlacement=part;
    result=project({internal},wp);ok(result,1);
    check(near(span(result).start,inverse(gp_Pnt(0,0,0).Transformed(curveShift).Transformed(child).Transformed(part),wp))
        && near(span(result).end,inverse(gp_Pnt(10,0,0).Transformed(curveShift).Transformed(child).Transformed(part),wp)),"stored curve location was doubled/ignored");
    // A second Part uses its own placement in the same all-or-nothing batch.
    auto second=vertex(3,9,14);second.partPlacement=rigid(gp_Dir(1,0,1),-0.6,gp_Vec(-50,12,90));
    result=project({locatedVertex,locatedEdge,second},wp);ok(result,3);
    check(near(position(result,2),inverse(gp_Pnt(3,9,14).Transformed(second.partPlacement),wp)),"mixed Part placements leaked between sources");
    auto scaled=vertex(2,3,4);scaled.partPlacement.SetScale(gp_Pnt(),2);
    result=project({scaled},base);ok(result,1);check(position(result)==Point{4,6},"uniform outer Part placement lost");
    // Preserve local distances even when world-space doubles cannot represent
    // a one-millimeter increment at the shared absolute placement.
    for(double shift:{1.e16,1.e20}) {
        auto far=base;far.frame.SetLocation(gp_Pnt(shift,-shift,shift));
        auto point=vertex(1,2,3);auto line=edge({0,0,1},{10,5,4});
        point.partPlacement.SetTranslation(gp_Vec(shift,-shift,shift));line.partPlacement=point.partPlacement;
        result=project({point,line},far);ok(result,2);
        check(position(result)==Point{1,2} && near(span(result,1).start,{0,0}) && near(span(result,1).end,{10,5}),"common large placements erased small local geometry");
    }
}
void duplicatesAndOverlaps() {
    const auto wp=plane();auto a=edge({0,0,7},{10,0,7});auto reverse=a;reverse.shape.Reverse();
    auto result=project({vertex(2,3,8),vertex(2,3,-80),a,reverse,edge({0,0,-25},{10,0,-25})},wp);ok(result,2);
    check(result.diagnostics.size()==3 && std::all_of(result.diagnostics.begin(),result.diagnostics.end(),[](const auto& d){return d.code==Code::Duplicate && !d.isError() && d.sourceIndex && d.relatedSourceIndex;}),"batch duplicate diagnostics incomplete");
    auto existing=result.additions;existing[0].visible=false;existing[1].selectable=false;
    const auto original=existing;result=project({vertex(2,3,444),reverse},wp,existing);ok(result,0);
    check(!result.changed() && existing==original && result.diagnostics.size()==2 && !result.diagnostics[0].existingEntityId.isEmpty(),"fully deduplicated batch changed existing data");
    auto other=existing;for(auto& entity:other) entity.workplaneId="other-owner";
    ok(project({vertex(2,3,8),a},wp,other),2);
    auto profile=existing[1];profile.role=Sketch::Role::Profile;ok(project({a},wp,{profile}),1);
    ok(project({},wp,existing),0);
    auto close=vertex(2+0.5e-7,3,0);ok(project({close},wp,existing),0);
    ok(project({vertex(2+2e-7,3,0)},wp,existing),1);
    for(const auto& overlapEdge:{edge({5,0,9},{15,0,9}),edge({2,0,9},{8,0,9}),edge({-5,0,9},{15,0,9}),edge({10-1.e-9,0,9},{20,0,9})}) {
        auto batch=project({a,overlapEdge},wp);refused(batch,Code::PartialOverlap,1);
        check(batch.diagnostics.back().relatedSourceIndex==0,"missing related overlap source");
        batch=project({overlapEdge},wp,existing);refused(batch,Code::PartialOverlap);
        check(batch.diagnostics.back().existingEntityId==existing[1].id,"missing existing overlap identity");
    }
    ok(project({a,edge({10,0,0},{20,0,0}),edge({5,-5,0},{5,5,0}),vertex(5,0,3)},wp),4);
    ok(project({a,edge({0,1.e-5,0},{10,1.e-5,0})},wp),2);
    Sketch::Entity axis;axis.workplaneId=wp.id;axis.role=Sketch::Role::Construction;axis.geometry=Sketch::InfiniteLine{{},{1,0}};
    refused(project({a},wp,{axis}),Code::PartialOverlap);
    // A no-op remains a no-op even if existing references already overlap.
    auto overlaps=existing;auto extra=existing[1];extra.id="existing-overlap";extra.geometry=Segment{{5,0},{15,0}};overlaps.push_back(extra);
    ok(project({a},wp,overlaps),0);
    // Non-axis-aligned contacts must remain contacts, not numerical overlaps.
    ok(project({edge({0,0,0},{3,4,0}),edge({3,4,0},{6,8,0})},wp),2);
    refused(project({edge({0,0,0},{3,4,0}),edge({1.5,2,0},{6,8,0})},wp),Code::PartialOverlap,1);
}
void failures() {
    const auto wp=plane();auto valid=vertex(1,2,3);
    Source circle{BRepBuilderAPI_MakeEdge(gp_Hypr(gp_Ax2(gp_Pnt(),gp_Dir(0,0,1)),10,5),-1,1).Shape(),{},"unsupported hyperbola source"};
    Source unbounded{BRepBuilderAPI_MakeEdge(gp_Lin(gp_Pnt(),gp_Dir(1,0,0))).Shape(),{},"infinite source"};
    Source half{BRepBuilderAPI_MakeEdge(gp_Lin(gp_Pnt(),gp_Dir(1,0,0)),0,Precision::Infinite()).Shape(),{},"half-infinite source"};
    BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);
    Source unsupported{compound,{},"compound source"};
    TopoDS_Edge empty;builder.MakeEdge(empty);Source missing{empty,{},"missing 3D curve"};
    auto degenerate=edge({0,0,0},{10,0,0});builder.Degenerated(TopoDS::Edge(degenerate.shape),true);
    auto oriented=edge({0,0,0},{10,0,0});oriented.shape.Orientation(TopAbs_INTERNAL);
    auto malformed=edge({0,0,0},{10,0,0});builder.UpdateVertex(TopExp::FirstVertex(TopoDS::Edge(malformed.shape)),gp_Pnt(3,4,5),1.e-7);
    auto transform=valid;transform.partPlacement.SetTranslation(gp_Vec(std::numeric_limits<double>::quiet_NaN(),0,0));
    std::vector<Source> inputs{valid,{},circle,unbounded,half,unsupported,missing,degenerate,oriented,malformed,transform};
    const auto result=project(inputs,wp);refused(result,Code::NullSource,1);refused(result,Code::NonlinearEdge,2);
    refused(result,Code::UnboundedEdge,3);refused(result,Code::UnboundedEdge,4);refused(result,Code::UnsupportedShape,5);
    refused(result,Code::MissingCurve,6);refused(result,Code::DegenerateEdge,7);refused(result,Code::InvalidOrientation,8);
    refused(result,Code::InvalidBRep,9);refused(result,Code::InvalidPlacement,10);
    check(result.diagnostics.size()==inputs.size()-1 && result.diagnostics[1].sourceLabel==circle.label,"not all invalid sources were diagnosed");
    for(const auto& sources:{std::vector{valid,circle},std::vector{circle,valid}}) refused(project(sources,wp),Code::NonlinearEdge,sources[0].label==circle.label?0:1);
    auto invalidPlane=wp;invalidPlane.kind=Construction::Kind::Axis;
    auto global=project({valid},invalidPlane);check(!global.success && global.additions.empty() && global.diagnostics.front().code==Code::InvalidWorkplane && !global.diagnostics.front().sourceIndex,"wrong target accepted");
    invalidPlane=wp;invalidPlane.frame.SetLocation(gp_Pnt(std::numeric_limits<double>::infinity(),0,0));
    check(!project({valid},invalidPlane).success,"non-finite target accepted");
    for(double tolerance:{0.0,-1.0,1.e-12,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
        global=project({valid},wp,{},tolerance);check(!global.success && global.diagnostics.front().code==Code::InvalidTolerance,"invalid tolerance accepted");
    }
    auto existing=project({valid},wp).additions;const auto retained=existing;existing[0].role=Sketch::Role::Profile;
    check(!project({valid},wp,existing).success && existing[0].role==Sketch::Role::Profile,"invalid existing collection accepted/mutated");
    existing=retained;existing.push_back(existing.front());check(!project({valid},wp,existing).success,"duplicate existing IDs accepted");
}
void independenceAndStorage() {
    auto wp=plane();auto sourceVertex=vertex(12.345678901234567,-7.987654321234567,48);auto sourceLine=edge({-5,20,14},{15,30,84});
    const auto originalVertex=brep(sourceVertex.shape),originalLine=brep(sourceLine.shape);
    const auto result=project({sourceVertex,sourceLine},wp);ok(result,2);
    check(brep(sourceVertex.shape)==originalVertex && brep(sourceLine.shape)==originalLine,"projection modified source geometry");
    auto copy=result.additions;std::set<QString> ids;
    for(const auto& e:copy) check(ids.insert(e.id).second && !QUuid(e.id).isNull() && e.role==Sketch::Role::Construction && e.workplaneId==wp.id,"new values lack independent stable identities/ownership");
    BRep_Builder builder;builder.UpdateVertex(TopoDS::Vertex(sourceVertex.shape),gp_Pnt(500,600,700),1.e-7);
    sourceLine.shape.Nullify();sourceVertex.shape.Nullify();wp.frame.SetLocation(gp_Pnt(9,8,7));
    check(result.additions==copy,"source deletion/edit or owner movement changed stored local values");
    QTemporaryDir directory;QString error;NativeDocumentSnapshot document;document.constructions={wp};document.sketchEntities=copy;
    auto path=directory.filePath("projected-v4.odc");NativeDocumentSnapshot loaded;
    check(NativeDocumentStorage::save(path,document,error) && NativeDocumentStorage::load(path,loaded,error)
        && loaded.sketchEntities==copy,"independent projected points/lines lost exact native values");
    auto repeated=project({vertex(12.345678901234567,-7.987654321234567,100)},plane(),copy);ok(repeated,0);
    document.sketchEntities.erase(document.sketchEntities.begin());document.schemaVersion=3;
    path=directory.filePath("projected-line-v3.odc");check(NativeDocumentStorage::save(path,document,error)
        && NativeDocumentStorage::load(path,loaded,error) && loaded.sketchEntities==document.sketchEntities,"projected lines needlessly required schema v4");
    check(NativeDocumentStorage::sketchSchemaVersion(copy)==4 && NativeDocumentStorage::sketchSchemaVersion(document.sketchEntities)==3,"vertex/line projection changed required native schemas");
}
}
int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    try {
        basics();placements();duplicatesAndOverlaps();failures();independenceAndStorage();
        std::cout<<"Exact vertex/line projection, transformations, diagnostics, deduplication and storage passed.\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
