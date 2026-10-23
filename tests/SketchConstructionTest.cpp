#include "SketchConstruction.h"
#include "SketchOverdraw.h"
#include "NativeDocument.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace Sketch;
namespace {
constexpr double pi=3.14159265358979323846;
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
bool near(Point a,Point b,double tolerance=1.e-7) {return std::hypot(a.x-b.x,a.y-b.y)<=tolerance;}
Entity entity(Geometry geometry) {Entity e;e.workplaneId="wp";e.role=Role::Construction;e.geometry=geometry;return e;}
Geometry offset(const Geometry& source,double distance,LineOutput type,std::optional<LineInterval> interval={}) {
    Geometry result;QString error;
    if(!offsetConstructionLine(source,distance,type,interval,result,error)) throw std::runtime_error(error.toStdString());return result;
}
void noOffset(const Geometry& source,double distance,LineOutput type,std::optional<LineInterval> interval={}) {
    Geometry result=Circle{{10,20},30},before=result;QString error;
    check(!offsetConstructionLine(source,distance,type,interval,result,error) && !error.isEmpty() && result==before,"failed Offset changed output or lacked explanation");
}
std::vector<CommonTangent> tangents(const Geometry& a,const Geometry& b,size_t expected,size_t finite) {
    std::vector<CommonTangent> result;QString error;
    const bool success=commonTangents(a,b,result,error);
    check(success==(expected>0),"common tangent success status incorrect");
    if(result.size()!=expected) throw std::runtime_error(QString("Expected %1 tangents, got %2: %3").arg(expected).arg(result.size()).arg(error).toStdString());
    if(!success) check(!error.isEmpty(),"missing no-solution explanation");
    size_t finiteCount=0;
    for(size_t i=0;i<result.size();++i) {
        const auto& t=result[i];Geometry line;
        check(contains(a,t.first) && contains(b,t.second),"contacts outside exact source domains");
        check(commonTangentLine(t,LineOutput::Infinite,line,error) && std::holds_alternative<InfiniteLine>(line)
            && contains(line,t.first) && contains(line,t.second),"invalid infinite tangent");
        if(commonTangentLine(t,LineOutput::Finite,line,error)) {
            ++finiteCount;const auto& segment=std::get<Segment>(line);check(segment.start==t.first && segment.end==t.second,"finite span does not preserve exact contacts");
        } else check(error.contains("coincide"),"unexpected finite refusal");
        const auto center=[](const Geometry& g){return std::visit([](const auto& value)->Point {
            using T=std::decay_t<decltype(value)>;if constexpr(std::is_same_v<T,Circle> || std::is_same_v<T,Arc>) return value.center;else return {};
        },g);};
        for(auto [contact,c]:{std::pair{t.first,center(a)},std::pair{t.second,center(b)}})
            check(std::abs((contact.x-c.x)*t.direction.x+(contact.y-c.y)*t.direction.y)<1.e-7,"line is not perpendicular to radius at contact");
        for(size_t j=0;j<i;++j) check(!near(t.first,result[j].first) || !near(t.second,result[j].second),"duplicate tangent contacts");
    }
    check(finiteCount==finite,"incorrect finite tangent count");return result;
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        auto axes=missingConstructionAxes({},"wp",true,true);
        check(axes.size()==2 && axes[0].geometry==Geometry(InfiniteLine{{0,0},{1,0}})
            && axes[1].geometry==Geometry(InfiniteLine{{0,0},{0,1}}),"axes do not use exact local coordinates");
        check(axes[0].id!=axes[1].id && axes[0].role==Role::Construction,"axes identity or role incorrect");
        axes[0].visible=false;axes[1].selectable=false;
        check(missingConstructionAxes(axes,"wp",true,true).empty(),"hidden/locked axes duplicated");
        auto reversedAxis=entity(InfiniteLine{{50,0},{-1,0}});
        auto missing=missingConstructionAxes({reversedAxis},"wp",true,true);
        check(missing.size()==1 && missing[0].geometry==axes[1].geometry,"translated/reversed same axis duplicated");
        check(missingConstructionAxes(axes,"another-wp",true,true).size()==2,"axes on another owner suppressed creation");
        check(missingConstructionAxes({},"wp",true,false).size()==1
            && missingConstructionAxes({},"wp",false,true).size()==1,"single axis choice ignored");
        auto visibleAxes=missingConstructionAxes({},"wp",true,true);visibleAxes.push_back(entity(Circle{{},10}));
        OverdrawGraph axesGraph;QString axesError;
        check(overdrawCandidates(visibleAxes,"wp",axesGraph,axesError) && axesGraph.pieces.size()==8,"axes unavailable as Overdraw sources");
        const auto axesSnaps=snapTargets(visibleAxes,"wp",{});
        check(std::any_of(axesSnaps.begin(),axesSnaps.end(),[](const auto& t){return t.kind==SnapKind::Intersection && t.point==Point{0,0};}),"axis intersection is not a snap target");
        const Segment source{{2,3},{5,7}};
        for(double d:{2.5,-2.5,1.e-6,12345.678901234}) {
            const auto output=std::get<Segment>(offset(source,d,LineOutput::Finite));
            const Point shift{-0.8*d,0.6*d};
            check(near(output.start,{2+shift.x,3+shift.y},1.e-10) && near(output.end,{5+shift.x,7+shift.y},1.e-10),"finite offset changed direction, length or perpendicular distance");
            const auto infinite=std::get<InfiniteLine>(offset(source,d,LineOutput::Infinite));
            check(near(infinite.direction,{0.6,0.8},1.e-14) && near(infinite.origin,output.start,1.e-12),"infinite offset origin/direction incorrect");
        }
        const InfiniteLine infinite{{10,-20},{0.6,0.8}};
        const auto span=std::get<Segment>(offset(infinite,-4,LineOutput::Finite,LineInterval{-7,25}));
        check(near(span.start,{9,-28},1.e-12) && near(span.end,{24,-8},1.e-12),"explicit finite interval placement incorrect");
        noOffset(infinite,4,LineOutput::Finite);noOffset(infinite,4,LineOutput::Finite,LineInterval{0,0});
        noOffset(infinite,4,LineOutput::Finite,LineInterval{0,-1});noOffset(infinite,4,LineOutput::Finite,LineInterval{std::numeric_limits<double>::infinity(),2});
        noOffset(source,0,LineOutput::Finite);noOffset(source,1.e-8,LineOutput::Finite);noOffset(source,std::numeric_limits<double>::quiet_NaN(),LineOutput::Infinite);
        noOffset(Circle{{},2},2,LineOutput::Infinite);noOffset(Segment{{},{0,0}},2,LineOutput::Finite);
        // A reversed source reverses the sign convention, but not the geometric distance.
        const auto reversed=std::get<Segment>(offset(Segment{source.end,source.start},-2.5,LineOutput::Finite));
        const auto forward=std::get<Segment>(offset(source,2.5,LineOutput::Finite));check(near(reversed.start,forward.end) && near(reversed.end,forward.start),"source direction sign convention inconsistent");
        const Circle a{{0,0},2},b{{10,0},3};
        const auto all=tangents(a,b,4,4);check(std::count_if(all.begin(),all.end(),[](const auto& t){return t.internal;})==2,"internal/external classification incorrect");
        tangents(Circle{{},2},Circle{{10,0},2},4,4);
        tangents(a,Circle{{5,0},3},3,2); // External contact: one unique infinite-only internal tangent.
        tangents(a,Circle{{4,0},3},2,2); // Overlap: only external tangents.
        tangents(Circle{{},3},Circle{{1,0},2},1,0); // Internal contact.
        tangents(Circle{{},3},Circle{{0.5,0},2},0,0);tangents(a,Circle{{},3},0,0);tangents(a,a,0,0);
        tangents(Circle{{},3},Circle{{1-1.e-10,0},2},0,0); // Near miss must not be clamped into a tangent.
        tangents(Circle{{},2},Circle{{5-1.e-10,0},3},2,2);
        tangents(Arc{{},2,0,pi},Arc{{10,0},3,0,pi},1,1);
        tangents(Arc{{},2,0,-pi},Arc{{10,0},3,0,-pi},1,1);
        tangents(Arc{{},2,pi/2,0.1},Arc{{10,0},2,pi/2,-0.1},1,1); // Exact arc endpoint contacts.
        tangents(Arc{{},2,0,0.1},Arc{{10,0},3,0,0.1},0,0);
        tangents(Arc{{},2,0,pi/2},Arc{{},2,pi/2,pi/2},1,0);
        tangents(Arc{{},2,0,pi},Arc{{},2,pi,pi},2,0);
        tangents(Arc{{},2,0,pi/2},Arc{{},2,pi,pi/2},0,0);
        tangents(Arc{{},2,0,pi},Arc{{},2,pi/2,pi},0,0); // Overlapping domains: an infinite family.
        tangents(Arc{{},2,0,pi},Arc{{},2,pi-1.e-9,pi/2},0,0); // Even a sub-tolerance overlap is not a unique endpoint tangent.
        noOffset(InfiniteLine{{1.e16,0},{1,0}},3,LineOutput::Finite,LineInterval{1,2.5}); // Unrepresentable requested position/length.
        const auto original=tangents(a,b,4,4),swapped=tangents(b,a,4,4);
        for(const auto& t:original) check(std::any_of(swapped.begin(),swapped.end(),[&](const auto& s){return near(t.first,s.second) && near(t.second,s.first);}),"operand order changed tangent locus");
        // Rigidly rotate/translate in local XY, including large coordinates and unequal radii.
        const double angle=0.831;const auto place=[&](Point p,double x,double y){return Point{x+p.x*std::cos(angle)-p.y*std::sin(angle),y+p.x*std::sin(angle)+p.y*std::cos(angle)};};
        for(const auto origin:{Point{123,-456},Point{1.e8,-1.e8}}) {
            const auto changed=tangents(Circle{place(a.center,origin.x,origin.y),a.radius},Circle{place(b.center,origin.x,origin.y),b.radius},4,4);
            for(const auto& t:original) check(std::any_of(changed.begin(),changed.end(),[&](const auto& s){return near(place(t.first,origin.x,origin.y),s.first) && near(place(t.second,origin.x,origin.y),s.second);}),"local rigid transform changed tangent solution");
        }
        for(int i=1;i<=25;++i) tangents(Circle{{},0.7*i},Circle{{50,17},0.3*i},4,4);
        std::vector<CommonTangent> invalid{{}};QString error;
        check(!commonTangents(Segment{},a,invalid,error) && invalid.empty(),"noncircular input accepted");
        check(!commonTangents(a,b,invalid,error,0) && invalid.empty(),"invalid tolerance accepted");
        Geometry stale=Circle{{},5},output=stale;check(!commonTangentLine({{},{},{1,0},false},LineOutput::Finite,output,error) && output==stale,"zero finite tangent changed output");
        // Ordinary schema-v3 values, with a rigid Workplane frame and no source links.
        NativeDocumentSnapshot snapshot;Construction::Object wp;wp.id="wp";wp.name="WP1";wp.kind=Construction::Kind::Workplane;
        wp.frame=gp_Ax3(gp_Pnt(10,20,30),gp_Dir(1,2,3),gp_Dir(2,-1,0));snapshot.constructions={wp};
        snapshot.sketchEntities={entity(source),entity(offset(source,-2.5,LineOutput::Finite)),entity(infinite),entity(offset(infinite,7,LineOutput::Infinite)),entity(a),entity(b)};
        for(const auto& t:original) for(const auto type:{LineOutput::Finite,LineOutput::Infinite}) {
            check(commonTangentLine(t,type,output,error),"cannot materialize tangent");snapshot.sketchEntities.push_back(entity(output));
        }
        const auto p=std::get<Segment>(snapshot.sketchEntities[1].geometry);
        check(std::abs(worldPoint(p.start,wp.frame).Distance(worldPoint(source.start,wp.frame))-2.5)<1.e-10,"rotated WP changed offset distance");
        QTemporaryDir dir;check(NativeDocumentStorage::save(dir.filePath("construction-lines.odc"),snapshot,error),"construction Save failed");
        NativeDocumentSnapshot loaded;check(NativeDocumentStorage::load(dir.filePath("construction-lines.odc"),loaded,error) && loaded.sketchEntities==snapshot.sketchEntities,"exact construction line roundtrip failed");
        const auto saved=loaded.sketchEntities[1];loaded.sketchEntities.erase(loaded.sketchEntities.begin());check(loaded.sketchEntities.front()==saved,"offset depends on deleted source");
        // New lines are normal snapping and Overdraw sources, including tangent contacts.
        const auto base=entity(Segment{{-10,0},{10,0}}),shifted=entity(offset(Segment{{-10,0},{10,0}},2,LineOutput::Finite));
        const auto cutting=entity(InfiniteLine{{0,0},{0,1}});OverdrawGraph graph;
        check(overdrawCandidates({base,shifted,cutting},"wp",graph,error) && graph.pieces.size()==5,"offset line unavailable to Overdraw");
        check(commonTangentLine(original[0],LineOutput::Finite,output,error),"finite tangent missing");
        check(overdrawCandidates({entity(a),entity(b),entity(output)},"wp",graph,error) && !graph.pieces.empty(),"common tangent unavailable to Overdraw");
        const auto targets=snapTargets({shifted},"wp",{});check(std::any_of(targets.begin(),targets.end(),[&](const auto& target){return target.kind==SnapKind::Endpoint && near(target.point,{-10,2});}),"offset did not enter existing snapping");
        std::cout<<"Analytic offset/tangent geometry, degeneracies, storage, snapping and Overdraw passed.\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
