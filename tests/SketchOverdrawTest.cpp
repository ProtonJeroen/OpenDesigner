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
bool near(Point a,Point b,double tol=1.e-7) {return std::hypot(a.x-b.x,a.y-b.y)<tol;}
Entity source(Geometry geometry,const QString& id) {Entity e;e.id=id;e.name=id;e.workplaneId="wp";e.role=Role::Construction;e.geometry=geometry;return e;}
OverdrawGraph graph(const std::vector<Entity>& sources,size_t count) {
    OverdrawGraph result;QString error;
    if(!overdrawCandidates(sources,"wp",result,error)) throw std::runtime_error(("Candidate graph: "+error).toStdString());
    if(result.pieces.size()!=count) throw std::runtime_error(QString("Expected %1 pieces, got %2 from %3 sources").arg(count).arg(result.pieces.size()).arg(sources.size()).toStdString());
    for(const auto& piece:result.pieces) {
        check(piece.start<result.nodes.size() && piece.end<result.nodes.size() && piece.start!=piece.end,"invalid node relation");
        check(!near(result.nodes[piece.start],result.nodes[piece.end]),"zero-sized piece");
        check(contains(piece.geometry,result.nodes[piece.start]) && contains(piece.geometry,result.nodes[piece.end]),"piece endpoints outside its exact domain");
        const auto original=std::find_if(sources.begin(),sources.end(),[&](const auto& s){return s.id==piece.sourceId;});
        check(original!=sources.end() && contains(original->geometry,result.nodes[piece.start]) && contains(original->geometry,result.nodes[piece.end]),"piece escaped its source domain");
        Entity p;p.workplaneId="wp";p.geometry=piece.geometry;check(valid(p,error),qPrintable(error));
    }
    return result;
}
void refused(const std::vector<Entity>& sources,const QString& reason) {
    OverdrawGraph output;output.pieces.push_back({Segment{},0,1,"stale"});QString error;
    check(!overdrawCandidates(sources,"wp",output,error) && !error.isEmpty() && output.pieces.empty() && output.nodes.empty(),"failure returned partial graph");
    check(error.contains(reason,Qt::CaseInsensitive),qPrintable(error));
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        const auto h=source(Segment{{-2,0},{2,0}},"horizontal"),v=source(Segment{{0,-2},{0,2}},"vertical"),c=source(Circle{{0,0},1},"circle");
        const auto cross=graph({h,v},4);check(cross.nodes.size()==5,"cross node clustering failed");
        const auto circleLine=graph({h,c},5);check(circleLine.nodes.size()==4,"circle-line intersection nodes incorrect");
        check(std::count_if(circleLine.pieces.begin(),circleLine.pieces.end(),[](const auto& p){return std::holds_alternative<Arc>(p.geometry);})==2,"circle not split into individual arcs");
        auto tangent=h;tangent.id="tangent";tangent.geometry=Segment{{-2,1},{2,1}};
        auto contact=graph({tangent,c},2);check(contact.notice.contains("two distinct"),"single-contact circle limit not explained");
        graph({tangent,h,c},8); // Two lines split at 3 circle nodes: 2+3 segments, 3 arcs.
        auto c2=c;c2.id="circle2";c2.geometry=Circle{{1,0},1};const auto circles=graph({c,c2},4);
        check(circles.nodes.size()==2,"circle-circle intersections not shared");
        c2.geometry=Circle{{2,0},1};refused({c,c2},"two distinct");
        auto inner=c2;inner.geometry=Circle{{0.5,0},0.5};refused({c,inner},"two distinct");
        auto arc=source(Arc{{0,0},1,350*pi/180,40*pi/180},"arc");
        graph({arc,source(InfiniteLine{{0,0},{1,0}},"line")},2);
        arc.geometry=Arc{{0,0},1,40*pi/180,-80*pi/180};
        auto negative=graph({arc,source(InfiniteLine{{0,0},{1,0}},"line")},2);
        for(const auto& p:negative.pieces) check(std::get<Arc>(p.geometry).sweepAngle<0,"negative arc orientation lost");
        auto reversed=std::get<Arc>(directedPiece(negative.pieces[0],negative.pieces[0].end));
        check(reversed.sweepAngle>0 && reversed.radius==1,"reverse traversal changed arc support");
        // Both graph endpoints acquire the same bounded locus. Reversal keeps
        // the selected short/long portion instead of choosing its complement.
        for(double sweep:{20.0,-20.0,340.0,-340.0}) {
            const Arc original{{0,0},50,350*pi/180,sweep*pi/180};
            const auto g=graph({source(original,"directed-arc")},1);const auto& piece=g.pieces.front();
            const Point middle{50*std::cos(original.startAngle+original.sweepAngle/2),50*std::sin(original.startAngle+original.sweepAngle/2)};
            const double complement=original.sweepAngle+(sweep>0?-2*pi:2*pi);
            const Point outside{50*std::cos(original.startAngle+complement/2),50*std::sin(original.startAngle+complement/2)};
            for(const auto from:{piece.start,piece.end}) {
                const auto hits=overdrawHits(g,from,{},middle,middle,0.1,[](Point p){return p;});check(hits==std::vector<size_t>{0},"arc cannot be acquired from both endpoints");
                const auto oriented=std::get<Arc>(directedPiece(piece,from));
                check(std::abs(std::abs(oriented.sweepAngle)-std::abs(original.sweepAngle))<1.e-12
                    && contains(oriented,middle) && !contains(oriented,outside),"reverse traversal selected complementary short/long arc");
                check(near({oriented.center.x+oriented.radius*std::cos(oriented.startAngle),oriented.center.y+oriented.radius*std::sin(oriented.startAngle)},g.nodes[from]),"reverse arc does not begin at current graph endpoint");
            }
        }
        const auto infinite=source(InfiniteLine{{0,0},{1,0}},"infinite");
        auto left=source(InfiniteLine{{-1,0},{0,1}},"left"),right=left;right.id="right";right.geometry=InfiniteLine{{1,0},{0,1}};
        auto bounded=graph({infinite,left,right},1);check(bounded.nodes.size()==2 && bounded.notice.contains("unbounded"),"infinite tails were bounded artificially");
        const auto span=std::get<Segment>(bounded.pieces[0].geometry);check(span.start==Point{-1,0} && span.end==Point{1,0},"wrong infinite-line interval");
        refused({infinite,left},"No finite");refused({c},"two distinct");
        refused({source(Segment{{},{}} ,"zero")},"degenerate");
        refused({source(Segment{{0,0},{1.e-8,0}},"tiny")},"No finite");
        auto duplicate=h;duplicate.id="duplicate";graph({h,duplicate},1);
        duplicate.geometry=Segment{{-1,0},{3,0}};graph({h,duplicate},3);
        duplicate=c;duplicate.id="duplicate";refused({c,duplicate},"two distinct");
        duplicate=arc;duplicate.id="duplicate";graph({arc,duplicate},1);
        duplicate=h;duplicate.geometry=Segment{{0,0},{0,1}};refused({h,duplicate},"identity");
        auto a=source(Arc{{0,0},2,0,pi/2},"a"),b=a;b.id="b";b.geometry=Arc{{0,0},2,pi/2,pi/2};graph({a,b},2);
        auto hidden=v;hidden.visible=false;auto locked=v;locked.selectable=false;auto profile=v;profile.role=Role::Profile;auto other=v;other.workplaneId="other";
        graph({h,hidden,locked,profile,other},1);
        graph({h,v,source(Segment{{-2,-2},{2,2}},"diagonal")},6);
        auto translated=source(Circle{{1.e8,-1.e8},1000},"translated");
        auto large=graph({translated,source(InfiniteLine{{1.e8,-1.e8},{1,0}},"large")},3);
        check(large.nodes.size()==2,"large translated nodes lost accuracy");
        // Circle alternatives are never collapsed to a complete route.
        const auto node=std::find_if(circleLine.nodes.begin(),circleLine.nodes.end(),[](auto p){return near(p,{1,0});})-circleLine.nodes.begin();
        auto project=[](Point p){return Point{p.x*100,p.y*100};};
        const auto ambiguous=overdrawHits(circleLine,size_t(node),{}, {1,0},{100,0},10,project);
        check(ambiguous.size()==4,"ambiguous meeting point silently chose a route");
        const auto upper=overdrawHits(circleLine,size_t(node),{}, {0,1},{0,100},10,project);
        check(upper.size()==1 && std::holds_alternative<Arc>(circleLine.pieces[upper[0]].geometry),"separate arc portion cannot be acquired");
        check(overdrawHits(circleLine,size_t(node),upper,{0,1},{0,100},10,project).empty(),"same piece can be selected twice");
        for(double zoom:{10.0,100.0,1000.0}) {
            const auto projection=[&](Point p){return Point{p.x*zoom,p.y*zoom};};
            auto hits=overdrawHits(bounded,0,{}, {0,0},{0,8},10,projection);check(hits.size()==1,"acquisition is not screen-pixel based");
            check(overdrawHits(bounded,0,{}, {0,0},{0,11},10,projection).empty(),"acquisition escaped pixel radius");
        }
        OverdrawGraph invalid;QString error;check(!overdrawCandidates({h},"wp",invalid,error,0),"zero tolerance accepted");
        check(!nearestPoint(Segment{{},{1,0}},{std::numeric_limits<double>::quiet_NaN(),0}),"nonfinite nearest input accepted");
        // Same schema v3; source and independently directed profile values roundtrip exactly.
        NativeDocumentSnapshot snapshot;Construction::Object wp;wp.id="wp";wp.name="WP1";wp.kind=Construction::Kind::Workplane;
        wp.frame=gp_Ax3(gp_Pnt(10,20,30),gp_Dir(1,2,3),gp_Dir(2,-1,0));snapshot.constructions={wp};snapshot.sketchEntities={h,c};
        for(const auto& p:circleLine.pieces) {Entity e;e.workplaneId="wp";e.geometry=directedPiece(p,p.end);snapshot.sketchEntities.push_back(e);}
        QTemporaryDir dir;
        if(!NativeDocumentStorage::save(dir.filePath("overdraw.odc"),snapshot,error)) throw std::runtime_error(("Save: "+error).toStdString());
        NativeDocumentSnapshot loaded;
        if(!NativeDocumentStorage::load(dir.filePath("overdraw.odc"),loaded,error)) throw std::runtime_error(("Load: "+error).toStdString());
        check(loaded.sketchEntities==snapshot.sketchEntities,"Overdraw values did not roundtrip exactly");
        const auto savedProfile=loaded.sketchEntities.back();loaded.sketchEntities[0].geometry=Segment{{0,0},{5,5}};
        check(loaded.sketchEntities.back()==savedProfile,"profile depends on construction source");
        std::cout<<"Overdraw boundaries, analytic pieces, ambiguity and storage checks passed.\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
