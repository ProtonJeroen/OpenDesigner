#include "SketchOverdraw.h"
#include "SketchCurveOperations.h"
#include "SketchProjection.h"
#include "SketchProfileValidation.h"
#include "SketchProfileBuilder.h"
#include "NativeDocument.h"
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <Geom2dAPI_ProjectPointOnCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <GeomAPI.hxx>
#include <gp_Pln.hxx>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <TopoDS.hxx>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace {
using namespace Sketch;
namespace C=SketchCurveOperations;
constexpr double pi=std::numbers::pi,turn=2*pi;
void check(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
void near(double a,double b,double tol=1.e-9) {if(std::abs(a-b)>tol)throw std::runtime_error("expected "+std::to_string(b)+", got "+std::to_string(a));}
Entity entity(QString id,Geometry g,Role role=Role::Construction) {Entity e;e.id=id;e.name=id;e.workplaneId="wp";e.geometry=std::move(g);e.role=role;return e;}
OverdrawGraph graph(const std::vector<Entity>& input) {
    OverdrawGraph result;QString error;if(!overdrawCandidates(input,"wp",result,error))throw std::runtime_error(error.toStdString());return result;
}
void refused(const std::vector<Entity>& input) {OverdrawGraph out;out.nodes.push_back({1,2});QString error;check(!overdrawCandidates(input,"wp",out,error) && out.pieces.empty() && out.nodes.empty() && !error.isEmpty(),"uncertified graph was not atomic");}
Entity profile(const OverdrawPiece& piece,const QString& id,bool reverse=false) {return entity(id,directedPiece(piece,reverse?piece.end:piece.start),Role::Profile);}
void profileArea(const std::vector<Entity>& entities,double expected,const gp_Ax3& frame=gp_Ax3()) {
    const auto result=SketchProfileValidation::validate(entities,"wp");
    if(result.status!=SketchProfileValidation::Status::Valid)throw std::runtime_error(result.diagnostics.front().message.toStdString());
    const auto built=SketchProfileBuilder::build(result,frame);if(!built.success)throw std::runtime_error(built.diagnostics.front().message.toStdString());
    double area=0;for(const auto& face:built.faces)area+=face.area;near(area,expected,1.e-7);
}
BSpline coeff(const Handle(Geom2d_BSplineCurve)& c,double first,double last,bool reverse=false) {
    BSpline s;s.degree=c->Degree();s.periodic=c->IsPeriodic();s.first=first;s.last=last;s.reversed=reverse;
    for(int i=1;i<=c->NbPoles();++i) {s.poles.push_back({c->Pole(i).X(),c->Pole(i).Y()});s.weights.push_back(c->Weight(i));}
    for(int i=1;i<=c->NbKnots();++i) {s.knots.push_back(c->Knot(i));s.multiplicities.push_back(c->Multiplicity(i));}return s;
}
void ellipses() {
    const Ellipse e{{0,0},5,3,0};const auto construction=entity("ellipse",e);
    const auto result=graph({construction,entity("line",Segment{{-8,0},{8,0}})});
    check(result.pieces.size()==5 && result.nodes.size()==4,"ellipse/line graph did not split exactly");
    std::vector<Entity> whole;
    for(const auto& p:result.pieces) if(const auto* a=std::get_if<EllipticalArc>(&p.geometry)) {
        check(a->ellipse==e,"elliptical coefficients changed");near(std::abs(a->sweepParameter),pi);
        const auto reversed=std::get<EllipticalArc>(directedPiece(p,p.end));near(reversed.sweepParameter,-a->sweepParameter);
        const Point mid=ellipsePoint(e,a->startParameter+a->sweepParameter/2);
        check(contains(reversed,mid),"reversed Overdraw chose complementary ellipse arc");
        check(overdrawHits(result,p.start,{},mid,mid,.01,[](Point q){return q;})==std::vector<size_t>{size_t(&p-result.pieces.data())},"elliptical piece cannot be picked in existing path");
        whole.push_back(profile(p,QString("e%1").arg(whole.size())));
    }
    profileArea(whole,15*pi);
    const auto mixed=graph({construction,entity("circle",Circle{{0,0},4})});check(mixed.pieces.size()==8 && mixed.nodes.size()==4,"circle/ellipse crossing was interpreted as overlap");
    for(auto p:mixed.nodes) {near(p.x*p.x,175./16);near(p.y*p.y,81./16);}
    const auto e2=graph({construction,entity("ellipse2",Ellipse{{0,0},5,3,pi/2})});check(e2.pieces.size()==8,"ellipse/ellipse intersections lost");
    for(auto p:e2.nodes) {near(p.x*p.x,225./34);near(p.y*p.y,225./34);}
    for(double sweep:{.7,-.7,4.8,-4.8}) {
        const EllipticalArc arc{e,5.9,sweep};const auto a=graph({entity("arc",arc)});check(a.pieces.size()==1 && a.pieces[0].geometry==Geometry(arc),"unchanged elliptical arc did not retain its original values");
        const auto r=std::get<EllipticalArc>(directedPiece(a.pieces[0],a.pieces[0].end));near(r.sweepParameter,-sweep);near(r.startParameter,5.9+sweep);
    }
    const auto tangent=graph({construction,entity("tangent",Segment{{-8,3},{8,3}})});check(tangent.pieces.size()==2 && tangent.notice.contains("two distinct"),"single tangent invented a closed ellipse route");
    auto outer=whole;
    for(auto& p:whole) {auto arc=std::get<EllipticalArc>(p.geometry);arc.ellipse.center={20,0};p.id+="island";p.geometry=arc;outer.push_back(p);}
    const Ellipse hole{{0,0},1,.5,0};const auto holes=graph({entity("hole",hole),entity("cut",Segment{{-2,0},{2,0}})});
    for(const auto& p:holes.pieces) if(std::holds_alternative<EllipticalArc>(p.geometry)) outer.push_back(profile(p,QString("hole%1").arg(outer.size())));
    profileArea(outer,30*pi-.5*pi);
}
void overlapsAndReuse() {
    const auto h=entity("h",Segment{{-2,0},{2,0}}),partial=entity("partial",Segment{{-1,0},{3,0}});
    auto copy=h;copy.id="copy";const auto duplicated=graph({h,copy});check(duplicated.pieces.size()==1 && duplicated.notice.contains("coincident"),"duplicate segments not reused");
    check(graph({h,partial}).pieces.size()==3,"partial line overlap was not partitioned/deduplicated");
    const Ellipse e{{0,0},5,3,.2};const EllipticalArc a{e,0,pi},b{e,pi/2,pi};
    const auto arcs=graph({entity("a",a),entity("b",reversedEllipticalArc(b))});check(arcs.pieces.size()==3,"partial elliptic overlap duplicated geometry");
    double span=0;for(const auto& p:arcs.pieces)span+=std::abs(std::get<EllipticalArc>(p.geometry).sweepParameter);near(span,1.5*pi);
    auto flipped=e;flipped.axisAngle+=pi;
    const auto phase=graph({entity("a",a),entity("same",EllipticalArc{flipped,-pi,pi})});check(phase.pieces.size()==1,"equivalent ellipse axes not reused");
    const auto complementary=graph({entity("upper",EllipticalArc{e,0,pi}),entity("lower",EllipticalArc{e,0,-pi})});check(complementary.pieces.size()==2,"complementary half ellipses were collapsed");
    const auto circleLimit=graph({entity("circle",Arc{{0,0},5,0,pi}),entity("ellipse",EllipticalArc{Ellipse{{0,0},5,5,.4},-.4,pi})});check(circleLimit.pieces.size()==1,"circular-limit cross-type equivalence was not certified");
    auto existing=entity("stable-profile",Segment{{-1,0},{1,0}},Role::Profile);existing.visible=false;existing.selectable=false;existing.name="Keep exact metadata";
    const auto before=std::vector<Entity>{h,existing};const auto covered=graph(before);check(covered.pieces.size()==3,"existing Profile bounds did not split candidates");
    check(std::count_if(covered.pieces.begin(),covered.pieces.end(),[](const auto& p){return p.existingProfileId=="stable-profile";})==1,"covered interval would create a duplicate Profile");
    check(before[1]==existing,"graph mutated existing Profile metadata/identity");
    auto whole=h;whole.role=Role::Profile;whole.id="whole";const auto full=graph({h,whole});check(full.pieces.size()==1 && full.pieces[0].existingProfileId=="whole","unchanged existing geometry did not retain canonical identity");
    // Close, differently located supports are not certified as identical.
    refused({h,entity("near",Segment{{-2,5.e-8},{2,5.e-8}})});
}
void splines() {
    const Spline s{{{0,0},{5,4},{10,0}}};const auto c=splineCurve(s);const auto input=entity("spline",s);
    const auto uncut=graph({input});check(uncut.pieces.size()==1 && uncut.pieces[0].geometry==Geometry(s),"whole interpolant did not retain unchanged values");
    const auto reverse=std::get<BSpline>(directedPiece(uncut.pieces[0],uncut.pieces[0].end));check(reverse.reversed,"reversed interpolant was re-interpolated");
    const auto original=coeff(c,c->FirstParameter(),c->LastParameter());check(reverse.poles==original.poles && reverse.knots==original.knots && reverse.weights==original.weights,"reversal changed exact coefficients");
    const auto r=graph({input,entity("line",Segment{{-1,3},{11,3}})});check(r.pieces.size()==6,"spline/line graph failed");
    std::vector<const OverdrawPiece*> pieces;for(const auto& p:r.pieces)if(std::holds_alternative<BSpline>(p.geometry))pieces.push_back(&p);
    check(pieces.size()==3,"spline fragments missing");
    for(const auto* p:pieces) {const auto exact=std::get<BSpline>(p->geometry);check(exact.poles==original.poles && exact.knots==original.knots && exact.weights==original.weights,"spline partition changed support coefficients");}
    const auto middle=std::find_if(pieces.begin(),pieces.end(),[](auto p){return p->start!=p->end && std::abs(bsplineCurve(std::get<BSpline>(p->geometry))->Value(std::get<BSpline>(p->geometry).first).X()-2.5)<1.e-7;});
    check(middle!=pieces.end(),"known middle spline interval missing");auto mp=profile(**middle,"middle");
    const auto ends=C::extensionEnds(mp.geometry);check(ends.status==C::Status::Success,"fragment endpoints missing");near(ends.start.x,2.5);near(ends.end.x,7.5);
    auto chord=entity("chord",Segment{ends.end,ends.start},Role::Profile);profileArea({mp,chord},10./3);
    const auto exact=std::get<BSpline>(mp.geometry);auto second=exact;second.first=(exact.first+exact.last)/2;second.last=c->LastParameter();second.reversed=true;
    const auto overlap=graph({entity("one",exact),entity("two",second)});check(overlap.pieces.size()==3,"coefficient interval overlap was not reused");
    auto reversedCurve=Handle(Geom2d_BSplineCurve)::DownCast(c->Reversed());const auto reversedCoefficients=coeff(reversedCurve,reversedCurve->FirstParameter(),reversedCurve->LastParameter());
    const auto mirrored=graph({input,entity("reversed",reversedCoefficients)});check(mirrored.pieces.size()==1,"exact reversed coefficient support not recognized");
    auto reparameterized=original;for(auto& knot:reparameterized.knots)knot*=2;reparameterized.first*=2;reparameterized.last*=2;
    refused({entity("original",original),entity("other-parameterization",reparameterized)});
    BSpline bow;bow.degree=1;bow.poles={{0,0},{2,2},{0,2},{2,0}};bow.weights={1,1,1,1};bow.knots={0,1,2,3};bow.multiplicities={2,1,1,2};bow.first=0;bow.last=1;
    auto crossing=bow;crossing.first=2;crossing.last=3;
    refused({entity("trim-one",bow),entity("trim-two",crossing)}); // Parameter domains alone cannot prove geometric disjointness on a self-crossing support.
    const Spline closed{{{0,0},{10,0},{10,10},{0,10}},true};const auto periodic=splineCurve(closed);
    const auto closedGraph=graph({entity("periodic",closed),entity("line",Segment{{5,-5},{5,15}})});
    std::vector<Entity> contour;
    for(const auto& p:closedGraph.pieces) if(const auto* b=std::get_if<BSpline>(&p.geometry)) {
        check(b->periodic && b->first>=periodic->FirstParameter() && b->last<=periodic->LastParameter(),"periodic splitting escaped its original domain");
        check(b->poles==coeff(periodic,periodic->FirstParameter(),periodic->LastParameter()).poles,"periodic coefficients changed");contour.push_back(profile(p,QString("closed%1").arg(contour.size())));
    }
    check(contour.size()==3,"periodic retained wrap was not split at the existing seam");
    const auto pv=SketchProfileValidation::validate(contour,"wp");check(pv.status==SketchProfileValidation::Status::Valid && SketchProfileBuilder::build(pv,gp_Ax3()).success,"periodic Overdraw profile was not certified");
    BSpline rational;rational.degree=2;rational.poles={{1,0},{1,1},{0,1}};rational.weights={1,std::sqrt(.5),1};rational.knots={0,1};rational.multiplicities={3,3};
    const auto rat=graph({entity("rational",rational),entity("cut",Segment{{std::sqrt(.5),-1},{std::sqrt(.5),2}})});
    for(const auto& p:rat.pieces)if(const auto* b=std::get_if<BSpline>(&p.geometry))check(b->poles==rational.poles && b->weights==rational.weights,"rational weights changed");
    check(rat.notice.contains("certification limits"),"unsupported rational Profile limitation was hidden");
    const auto unsupported=SketchProfileValidation::validate({entity("rat",rational,Role::Profile)},"wp");check(unsupported.status==SketchProfileValidation::Status::Unsupported,"rational Profile was silently broadened");
}
void obliqueProjection() {
    // World circle in XY projected to a plane tilted 60 degrees about world X.
    // Local X is unchanged, local Y coefficient is cos(60)=1/2.
    Construction::Object wp;wp.id="wp";wp.name="Oblique";wp.kind=Construction::Kind::Workplane;
    wp.frame=gp_Ax3(gp_Pnt(),gp_Dir(0,-std::sqrt(.75),.5),gp_Dir(1,0,0));
    const gp_Circ circle(gp_Ax2(gp_Pnt(),gp_Dir(0,0,1),gp_Dir(1,0,0)),10);
    SketchProjection::Source source{BRepBuilderAPI_MakeEdge(circle).Shape(),{},"world circle"};
    auto projection=SketchProjection::project({source},wp);check(projection.success && projection.additions.size()==1,"oblique circle projection failed");
    const auto ellipse=std::get<Ellipse>(projection.additions[0].geometry);near(ellipse.majorRadius,10);near(ellipse.minorRadius,5);near(ellipse.center.x,0);near(ellipse.center.y,0);near(ellipse.axisAngle,0);
    auto sources=projection.additions;sources.push_back(entity("diameter",Segment{{-12,0},{12,0}}));const auto snapshot=sources;
    const auto g=graph(sources);std::vector<Entity> profileValues;
    for(const auto& p:g.pieces)if(std::holds_alternative<EllipticalArc>(p.geometry)) {
        const auto a=std::get<EllipticalArc>(p.geometry);check(a.ellipse==ellipse,"projected ellipse coefficients changed during Overdraw");
        for(double f:{0.,.2,.5,.8,1.}) {const double t=a.startParameter+f*a.sweepParameter;const auto local=ellipsePoint(ellipse,t);near(local.x,10*std::cos(t));near(local.y,5*std::sin(t));
            const auto world=worldPoint(local,wp.frame);near(world.X(),10*std::cos(t));near(world.Y(),2.5*std::sin(t));near(world.Z(),5*std::sqrt(.75)*std::sin(t));}
        profileValues.push_back(profile(p,QString("projection%1").arg(profileValues.size())));
    }
    check(profileValues.size()==2 && sources==snapshot,"projection Overdraw mutated independent source values");profileArea(profileValues,50*pi,wp.frame);
    // Apply one common rigid transform to source and target; local coefficients
    // remain unchanged and the exact projected curve follows the Workplane.
    gp_Trsf placement;placement.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),.61);placement.SetTranslationPart(gp_Vec(30,-20,100));
    auto transformed=wp;transformed.frame.Transform(placement);source.partPlacement=placement;
    const auto moved=SketchProjection::project({source},transformed);check(moved.success,"placed projection failed");const auto me=std::get<Ellipse>(moved.additions[0].geometry);
    near(me.majorRadius,10);near(me.minorRadius,5);near(me.axisAngle,0);profileArea(profileValues,50*pi,transformed.frame);
    const double start=5.9,sweep=1.2;
    auto arc=TopoDS::Edge(BRepBuilderAPI_MakeEdge(circle,start,start+sweep).Shape());arc.Reverse();source.shape=arc;
    const auto projectedArc=SketchProjection::project({source},transformed);check(projectedArc.success,"reversed oblique arc projection failed");
    const auto ea=std::get<EllipticalArc>(projectedArc.additions[0].geometry);near(std::remainder(ea.startParameter-(start+sweep),turn),0);near(ea.sweepParameter,-sweep);
    const auto ga=graph(projectedArc.additions);check(ga.pieces.size()==1 && ga.pieces[0].geometry==projectedArc.additions[0].geometry,"projected arc domain/traversal changed");
    const auto directed=std::get<EllipticalArc>(directedPiece(ga.pieces[0],ga.pieces[0].end));near(std::remainder(directed.startParameter-start,turn),0);near(directed.sweepParameter,sweep);
    const auto trim=C::trim(ellipse,{Segment{{3,-10},{3,10}}},{10,0});check(trim.status==C::Status::Success,"projected ellipse lost existing Trim compatibility");
    const auto extension=C::extend(EllipticalArc{ellipse,0,pi/4},C::Endpoint::End,Segment{{5,0},{5,10}});check(extension.status==C::Status::Success,"projected ellipse lost existing Extend compatibility");near(std::get<EllipticalArc>(*extension.geometry).sweepParameter,pi/3);
    // Sources are independent: source replacement cannot change returned values.
    const auto retained=projectedArc.additions;source.shape.Nullify();check(projectedArc.additions==retained,"projection retained an associative source link");
    BSpline q;q.degree=2;q.poles={{1,0},{1,1},{0,1}};q.weights={1,std::sqrt(.5),1};q.knots={0,1};q.multiplicities={3,3};
    auto edge=BRepBuilderAPI_MakeEdge(GeomAPI::To3d(bsplineCurve(q),gp_Pln(gp_Ax3())),.1,.9).Shape();edge.Reverse();
    const auto splineProjection=SketchProjection::project({{edge,placement,"placed rational edge"}},transformed);
    check(splineProjection.success && splineProjection.additions.size()==1,"oblique coefficient B-spline projection failed");
    const auto projected=std::get<BSpline>(splineProjection.additions[0].geometry);
    check(projected.degree==2 && projected.reversed && projected.weights==q.weights && projected.knots==q.knots,"projected rational coefficients or reversal changed");
    near(projected.first,.1);near(projected.last,.9);near(projected.poles[0].x,1);near(projected.poles[0].y,0);near(projected.poles[1].x,1);near(projected.poles[1].y,.5);near(projected.poles[2].x,0);near(projected.poles[2].y,.5);
    auto projectedSources=splineProjection.additions;projectedSources.push_back(entity("rational-boundary",Segment{{std::sqrt(.5),-1},{std::sqrt(.5),2}}));
    const auto projectedGraph=graph(projectedSources);int splinePieces=0;
    for(const auto& p:projectedGraph.pieces) if(const auto* exact=std::get_if<BSpline>(&p.geometry)) {
        ++splinePieces;check(exact->reversed && exact->poles==projected.poles && exact->weights==projected.weights && exact->knots==projected.knots && exact->first>=.1 && exact->last<=.9,"projected Overdraw altered coefficient domain or traversal");
        check(!std::get<BSpline>(directedPiece(p,p.end)).reversed,"projected B-spline reverse traversal failed");
    }
    check(splinePieces==2,"projected rational curve did not split at its exact intersection");
    NativeDocumentSnapshot native;native.constructions={transformed};native.sketchEntities=moved.additions;native.sketchEntities.insert(native.sketchEntities.end(),profileValues.begin(),profileValues.end());QTemporaryDir dir;QString error;
    check(NativeDocumentStorage::save(dir.filePath("projected-overdraw.odc"),native,error),qPrintable(error));NativeDocumentSnapshot loaded;
    check(NativeDocumentStorage::load(dir.filePath("projected-overdraw.odc"),loaded,error) && loaded.sketchEntities==native.sketchEntities,"exact projected Overdraw native values changed");
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {std::cout<<"ellipses\n";ellipses();std::cout<<"overlaps\n";overlapsAndReuse();std::cout<<"splines\n";splines();std::cout<<"projection\n";obliqueProjection();std::cout<<"Advanced Overdraw passed\n";return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
