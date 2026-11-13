#include "SketchGeometry.h"
#include "SketchSnapping.h"
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
    check(SketchProfileBuilder::build(closedAnalysis,gp_Ax3()).success,"periodic spline wire not certified");
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
