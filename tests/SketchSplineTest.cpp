#include "SketchGeometry.h"
#include "SketchSnapping.h"
#include "SketchOverdraw.h"
#include "SketchProfileValidation.h"
#include "SketchProfileBuilder.h"
#include "SketchProfileExtrusion.h"
#include "SketchProfileRevolution.h"
#include "SketchDisplayBounds.h"
#include "SketchProjection.h"
#include <GeomAPI.hxx>
#include <gp_Pln.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include "NativeDocument.h"
#include <Geom2dAPI_ProjectPointOnCurve.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopoDS.hxx>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace {
void check(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
Sketch::Entity entity(QString id,Sketch::Geometry g) {Sketch::Entity e;e.id=id;e.workplaneId="wp";e.geometry=std::move(g);return e;}
void testSpline() {
    using namespace Sketch;
    Spline open{{{0,0},{5,4},{10,0}}};const auto original=open;const auto c=splineCurve(open);
    for(auto p:open.points) {
        Geom2dAPI_ProjectPointOnCurve projection(gp_Pnt2d(p.x,p.y),c);
        check(projection.NbPoints()>0 && projection.LowerDistance()<1.e-10,"interpolation missed through-point");
    }
    auto e=entity("spline",open);QString error;check(valid(e,error),qPrintable(error));
    open.startTangent=Point{2,1};open.endTangent=Point{3,-1};
    const auto constrained=splineCurve(open);gp_Pnt2d p;gp_Vec2d v;
    constrained->D1(constrained->FirstParameter(),p,v);check(v.IsEqual(gp_Vec2d(2,1),1.e-9,1.e-9),"start tangent magnitude/direction changed");
    constrained->D1(constrained->LastParameter(),p,v);check(v.IsEqual(gp_Vec2d(3,-1),1.e-9,1.e-9),"end tangent changed");
    Spline periodic{{{0,0},{10,0},{10,10},{0,10}},true};
    const auto closed=splineCurve(periodic);check(closed->IsPeriodic(),"closure is not periodic");
    gp_Pnt2d a,b;gp_Vec2d da,db;closed->D1(closed->FirstParameter(),a,da);closed->D1(closed->LastParameter(),b,db);
    check(a.Distance(b)<1.e-10 && da.IsEqual(db,1.e-9,1.e-9),"periodic seam is not C1");
    for(auto bad:std::vector<Spline>{Spline{},Spline{{{0,0},{0,0}}},Spline{{{0,0},{1,1}},true},Spline{{{0,0},{1,1},{0,0}},true}})
        check(!valid(entity("bad",bad),error),"invalid spline accepted");
    auto bad=periodic;bad.startTangent=Point{1,0};check(!valid(entity("bad",bad),error),"periodic tangent ambiguity accepted");
    auto construction=e;construction.role=Role::Construction;OverdrawGraph graph;
    check(overdrawCandidates({construction},"wp",graph,error) && graph.pieces.size()==1 && graph.pieces[0].geometry==e.geometry,"whole spline Overdraw changed its exact original values");
    const auto targets=snapTargets({e},"wp",{5,5});size_t through=0;
    for(const auto& target:targets) if(target.reference) {
        const auto resolved=resolvePoint({e},*target.reference,error);check(resolved && *resolved==target.point,"spline snap/reference mismatch");
        if(target.reference->feature==PointFeature::ThroughPoint) ++through;
    }
    check(through==3,"through-point references missing");
    const auto bounds=finiteBounds(e.geometry);check(bounds && bounds->maxY>=4,"spline display bounds clipped interpolation");
    const auto profile=std::vector<Entity>{e,entity("chord",Segment{{10,0},{0,0}})};const auto saved=profile;
    const auto analysis=SketchProfileValidation::validate(profile,"wp");
    if(analysis.status!=SketchProfileValidation::Status::Valid) for(auto d:analysis.diagnostics) std::cerr<<d.message.toStdString()<<'\n';
    check(analysis.status==SketchProfileValidation::Status::Valid,"spline/chord profile refused");
    const auto faces=SketchProfileBuilder::build(analysis,gp_Ax3());
    if(!faces.success) for(auto d:faces.diagnostics) std::cerr<<d.message.toStdString()<<'\n';
    check(faces.success && faces.faces.size()==1,"spline profile construction failed");
    // Three equally spaced through-points produce y=4 - .16*(x-5)^2.
    const double area=80./3;
    check(std::abs(faces.faces[0].area-area)<1.e-7,"spline area failed independent polynomial integral");
    const auto solid=SketchProfileExtrusion::extrude(faces,gp_Dir(0,0,1),7);
    check(solid.success && std::abs(solid.volume-7*area)<1.e-7,"spline extrusion volume changed");
    auto shifted=original;for(auto& p:shifted.points) p.x+=3;
    const auto shiftedAnalysis=SketchProfileValidation::validate({entity("spline",shifted),entity("chord",Segment{{13,0},{3,0}})},"wp");
    const auto shiftedFaces=SketchProfileBuilder::build(shiftedAnalysis,gp_Ax3());
    const auto revolved=SketchProfileRevolution::revolve(shiftedFaces,gp_Ax1(gp_Pnt(),gp_Dir(0,1,0)),90);
    check(revolved.success && std::abs(revolved.volume-area*8*std::numbers::pi/2)<1.e-7,"spline Revolve failed independent centroid-volume oracle");
    Spline crossing{{{0,0},{10,10},{0,10},{10,0}}};
    const auto invalid=SketchProfileValidation::validate({entity("crossing",crossing),entity("close",Segment{{10,0},{0,0}})},"wp");
    check(invalid.status!=SketchProfileValidation::Status::Valid && invalid.contours.empty(),"crossing spline yielded usable profile");
    const auto closedAnalysis=SketchProfileValidation::validate({entity("closed",periodic)},"wp");
    if(closedAnalysis.status!=SketchProfileValidation::Status::Valid) for(auto d:closedAnalysis.diagnostics) std::cerr<<d.message.toStdString()<<'\n';
    check(closedAnalysis.status==SketchProfileValidation::Status::Valid,"simple periodic spline refused");
    const auto closedFaces=SketchProfileBuilder::build(closedAnalysis,gp_Ax3());
    if(!closedFaces.success) for(const auto& d:closedFaces.diagnostics) std::cerr<<d.message.toStdString()<<'\n';
    check(closedFaces.success,"periodic spline wire not certified");
    const auto closedSolid=SketchProfileExtrusion::extrude(closedFaces,gp_Dir(0,0,1),7);
    if(!closedSolid.success) for(const auto& d:closedSolid.diagnostics) std::cerr<<d.message.toStdString()<<'\n';
    if(!closedSolid.success || std::abs(closedSolid.volume-closedFaces.faces[0].area*7)>=1.e-7) std::cerr<<"periodic area "<<closedFaces.faces[0].area<<" volume "<<closedSolid.volume<<" expected "<<closedFaces.faces[0].area*7<<'\n';
    check(closedSolid.success && std::abs(closedSolid.volume-closedFaces.faces[0].area*7)<1.e-7,"periodic spline extrusion disagrees with certified area");
    auto offsetPeriodic=periodic;for(auto& p:offsetPeriodic.points) p.x+=20;
    const auto offsetAnalysis=SketchProfileValidation::validate({entity("closed-offset",offsetPeriodic)},"wp");
    const auto offsetFaces=SketchProfileBuilder::build(offsetAnalysis,gp_Ax3());
    const auto closedRevolve=SketchProfileRevolution::revolve(offsetFaces,gp_Ax1(gp_Pnt(),gp_Dir(0,1,0)),90);
    if(!closedRevolve.success) for(const auto& d:closedRevolve.diagnostics) std::cerr<<d.message.toStdString()<<'\n';
    check(closedRevolve.success && closedRevolve.solidCount==1 && BRepCheck_Analyzer(closedRevolve.shape).IsValid(),"periodic spline Revolve failed certification");
    std::vector<Entity> withHole{entity("bottom",Segment{{-20,-20},{40,-20}}),entity("right",Segment{{40,-20},{40,40}}),entity("top",Segment{{40,40},{-20,40}}),entity("left",Segment{{-20,40},{-20,-20}}),entity("hole",periodic)};
    const auto holeAnalysis=SketchProfileValidation::validate(withHole,"wp");
    const auto holeFaces=SketchProfileBuilder::build(holeAnalysis,gp_Ax3());
    check(holeFaces.success && holeFaces.faces.size()==1 && holeFaces.faces[0].holes.size()==1 && std::abs(holeFaces.faces[0].area-(3600-closedFaces.faces[0].area))<1.e-7,"periodic spline hole lost nesting or area");
    check(profile==saved && original==std::get<Spline>(e.geometry),"spline modeling mutated source");
    NativeDocumentSnapshot doc;Construction::Object wp;wp.id="wp";wp.name="WP1";wp.kind=Construction::Kind::Workplane;doc.constructions={wp};
    doc.sketchEntities={e,entity("periodic",periodic),entity("tangents",open)};QTemporaryDir temporary;
    const auto file=temporary.filePath("spline.odc");NativeDocumentSnapshot loaded;
    check(NativeDocumentStorage::save(file,doc,error),qPrintable(error));check(NativeDocumentStorage::load(file,loaded,error),qPrintable(error));
    check(loaded.sketchEntities==doc.sketchEntities && NativeDocumentStorage::sketchSchemaVersion(loaded.sketchEntities)==6,"spline values/IDs changed on Save/Open");
    QFile before(file);check(before.open(QIODevice::ReadOnly),"cannot inspect staged native file");const auto bytes=before.readAll();before.close();
    auto oldSchema=doc;oldSchema.schemaVersion=5;check(!NativeDocumentStorage::save(file,oldSchema,error),"old schema silently discarded spline");
    QFile after(file);check(after.open(QIODevice::ReadOnly) && after.readAll()==bytes,"failed native save replaced existing document");
    // Exact rational cubic: projection keeps coefficients and parameterization.
    BSpline coefficients;coefficients.poles={{0,0},{2,5},{8,-3},{10,1}};coefficients.weights={1,2,.75,1};coefficients.knots={0,1};coefficients.multiplicities={4,4};
    const auto sourceCurve=bsplineCurve(coefficients);gp_Trsf placement;placement.SetTranslation(gp_Vec(3,-4,7));
    const auto edge=BRepBuilderAPI_MakeEdge(GeomAPI::To3d(sourceCurve,gp_Pln(gp_Ax3())),.1,.9).Edge();
    const auto projected=SketchProjection::project({{edge,placement,"rational cubic"}},wp);
    if(!projected.success) for(auto d:projected.diagnostics) std::cerr<<d.message.toStdString()<<'\n';
    check(projected.success && projected.additions.size()==1 && std::holds_alternative<BSpline>(projected.additions[0].geometry),"exact B-spline projection failed");
    const auto data=std::get<BSpline>(projected.additions[0].geometry);const auto evaluated=bsplineCurve(data);
    check(data.weights==coefficients.weights && data.knots==coefficients.knots && data.multiplicities==coefficients.multiplicities && data.first==.1 && data.last==.9,"projection changed B-spline weights/knots/trim");
    for(int i=0;i<=20;++i) {
        const double t=.1+.8*i/20;const auto p=sourceCurve->Value(t),q=evaluated->Value(t);
        check(std::hypot(q.X()-p.X()-3,q.Y()-p.Y()+4)<1.e-10,"projected curve is not exact affine image");
    }
    const auto reverse=SketchProjection::project({{edge.Reversed(),placement,{}}},wp);
    check(reverse.success && std::get<BSpline>(reverse.additions.front().geometry).reversed,"projected B-spline lost reversed traversal");
    gp_Trsf location;location.SetTranslation(gp_Vec(2,3,4));gp_Trsf rotated;rotated.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),.6);rotated.SetTranslationPart(gp_Vec(20,-10,7));
    auto target=wp;target.frame=gp_Ax3(gp_Pnt(8,9,10),gp_Dir(2,1,3),gp_Dir(1,-2,0));
    const auto placed=SketchProjection::project({{edge.Moved(TopLoc_Location(location)),rotated,{}}},target);
    check(placed.success && placed.additions.size()==1,"located B-spline projection failed");
    const auto placedCurve=bsplineCurve(std::get<BSpline>(placed.additions.front().geometry));
    for(int i=0;i<=10;++i) {
        const double t=.1+.8*i/10;const auto p=sourceCurve->Value(t);gp_Pnt world(p.X(),p.Y(),0);world.Transform(location);world.Transform(rotated);
        const gp_Vec delta(target.frame.Location(),world);const auto q=placedCurve->Value(t);
        check(std::abs(q.X()-delta.Dot(gp_Vec(target.frame.XDirection())))<1.e-9 && std::abs(q.Y()-delta.Dot(gp_Vec(target.frame.YDirection())))<1.e-9,"B-spline locations were omitted or applied twice");
    }
    auto overlapping=entity("uncertain",Segment{{0,0},{20,0}});overlapping.role=Role::Construction;
    const auto ambiguous=SketchProjection::project({{edge,placement,{}}},wp,{overlapping});
    check(!ambiguous.success && ambiguous.additions.empty(),"uncertified B-spline overlap exposed partial output");
    const auto duplicate=SketchProjection::project({{edge,placement,{}}},wp,projected.additions);
    check(duplicate.success && duplicate.additions.empty(),"duplicate B-spline projection was not reused");
    doc.sketchEntities=projected.additions;
    check(NativeDocumentStorage::save(file,doc,error) && NativeDocumentStorage::load(file,loaded,error) && loaded.sketchEntities==doc.sketchEntities,"projected B-spline native roundtrip lost exact coefficients");
    auto coefficientProfile=projected.additions;coefficientProfile.front().role=Role::Profile;
    const auto refused=SketchProfileValidation::validate(coefficientProfile,"wp");
    check(refused.status==SketchProfileValidation::Status::Unsupported && refused.contours.empty() && !refused.diagnostics.empty() && refused.diagnostics.front().code==SketchProfileValidation::Code::UnsupportedCurve,"coefficient B-spline bypassed Profile certification");

}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {testSpline();std::cout<<"Spline interpolation, references, profiles, extrusion and native roundtrip passed\n";return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
