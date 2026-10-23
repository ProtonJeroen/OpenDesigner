#include "SketchSnapping.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <iostream>
using namespace Sketch;
namespace {
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
bool near(Point a,Point b,double tolerance=1.e-7) {return std::hypot(a.x-b.x,a.y-b.y)<=tolerance;}
void result(const Geometry& a,const Geometry& b,IntersectionKind kind,std::vector<Point> expected) {
    for(const auto& pair:{std::pair{a,b},std::pair{b,a}}) {
        const auto actual=intersect(pair.first,pair.second);
        check(actual.kind==kind && actual.points.size()==expected.size(),"intersection classification/count mismatch");
        for(const auto p:expected) {
            check(std::any_of(actual.points.begin(),actual.points.end(),[&](Point q){return near(p,q);}),"wrong intersection location");
            check(contains(a,p) && contains(b,p),"expected point outside trim domain");
        }
    }
}
}
int main() {
    try {
        constexpr double pi=3.14159265358979323846;
        result(Segment{{-2,0},{2,0}},Segment{{0,-2},{0,2}},IntersectionKind::Finite,{{0,0}});
        result(Segment{{0,0},{1,0}},InfiniteLine{{2,0},{0,1}},IntersectionKind::None,{});
        result(InfiniteLine{{0,0},{1,0}},InfiniteLine{{0,1},{1,0}},IntersectionKind::None,{});
        result(InfiniteLine{{0,0},{1,0}},InfiniteLine{{-3,0},{-1,0}},IntersectionKind::Coincident,{});
        result(Segment{{0,0},{2,0}},Segment{{3,0},{1,0}},IntersectionKind::Coincident,{});
        result(Segment{{0,0},{1,0}},Segment{{1,0},{2,0}},IntersectionKind::Finite,{{1,0}});
        result(Segment{{0,0},{1,0}},Segment{{2,0},{3,0}},IntersectionKind::None,{});
        result(InfiniteLine{{0,0},{1,0}},Circle{{0,0},1},IntersectionKind::Finite,{{-1,0},{1,0}});
        result(InfiniteLine{{0,1},{1,0}},Circle{{0,0},1},IntersectionKind::Finite,{{0,1}});
        result(InfiniteLine{{0,2},{1,0}},Circle{{0,0},1},IntersectionKind::None,{});
        result(Segment{{0,0},{2,0}},Circle{{0,0},1},IntersectionKind::Finite,{{1,0}});
        result(Circle{{0,0},1},Circle{{1,0},1},IntersectionKind::Finite,{{0.5,std::sqrt(0.75)},{0.5,-std::sqrt(0.75)}});
        result(Circle{{0,0},1},Circle{{2,0},1},IntersectionKind::Finite,{{1,0}});
        result(Circle{{0,0},2},Circle{{1,0},1},IntersectionKind::Finite,{{2,0}});
        result(Circle{{0,0},2},Circle{{0,0},1},IntersectionKind::None,{});
        result(Circle{{0,0},1},Circle{{0,0},1},IntersectionKind::Coincident,{});
        result(Circle{{0,0},1},Circle{{3,0},1},IntersectionKind::None,{});
        result(Arc{{0,0},1,0,pi/2},InfiniteLine{{0,0},{1,0}},IntersectionKind::Finite,{{1,0}});
        result(Arc{{0,0},1,0,-pi/2},InfiniteLine{{0,0.5},{1,0}},IntersectionKind::None,{});
        result(Arc{{0,0},1,5.5,1.5},InfiniteLine{{0,0},{1,0}},IntersectionKind::Finite,{{1,0}});
        result(Arc{{0,0},1,0,1},Arc{{0,0},1,2,1},IntersectionKind::None,{});
        result(Arc{{0,0},1,0,1},Arc{{0,0},1,1,1},IntersectionKind::Finite,{{std::cos(1),std::sin(1)}});
        result(Arc{{0,0},1,0,1},Arc{{0,0},1,1,-1},IntersectionKind::Coincident,{});
        result(Arc{{0,0},1,5.8,1},Arc{{0,0},1,0,0.2},IntersectionKind::Coincident,{});
        result(Segment{{0,0},{0,0}},Circle{{0,0},1},IntersectionKind::None,{});
        result(InfiniteLine{{0,0},{0,0}},Circle{{0,0},1},IntersectionKind::None,{});
        result(Circle{{0,0},0},InfiniteLine{{0,0},{1,0}},IntersectionKind::None,{});
        const auto almostParallel=intersect(InfiniteLine{{0,0},{1,0}},InfiniteLine{{0,-1},{1,1.e-10}});
        check(almostParallel.kind==IntersectionKind::Finite && almostParallel.points.size()==1
            && std::abs(almostParallel.points[0].x/1.e10-1)<1.e-12,"near-parallel lines lost unique intersection");
        const auto large=intersect(Circle{{1.e8,-1.e8},1000},InfiniteLine{{1.e8,-1.e8},{1,0}});
        check(large.points.size()==2 && near(large.points[0],{1.e8-1000,-1.e8}),"translated coordinates lost precision");
        const auto tangents=tangentPoints({2,0},Circle{{0,0},1});check(tangents.size()==2,"missing circle tangents");
        for(Point p:tangents) check(std::abs(p.x*(p.x-2)+p.y*p.y)<1.e-12 && contains(Circle{{0,0},1},p),"tangent is not perpendicular to radius");
        check(tangentPoints({2,0},Arc{{0,0},1,0,pi}).size()==1,"tangent ignored arc trim");
        check(tangentPoints({0,0},Circle{{0,0},1}).empty() && tangentPoints({0.5,0},Circle{{0,0},1}).empty(),"inside-circle tangent invented");
        check(tangentPoints({1,0},Circle{{0,0},1}).size()==1,"on-circle tangent degeneracy incorrect");
        Entity a;a.workplaneId="wp";a.geometry=Segment{{0,0},{10,0}};
        Entity b;b.workplaneId="wp";b.role=Role::Construction;b.geometry=Circle{{0,0},2};
        const auto targets=snapTargets({a,b},"wp",{3,0});
        check(std::any_of(targets.begin(),targets.end(),[](const auto& t){return t.kind==SnapKind::Midpoint && near(t.point,{5,0});}),"midpoint missing");
        check(std::any_of(targets.begin(),targets.end(),[](const auto& t){return t.kind==SnapKind::Intersection && near(t.point,{2,0});}),"mixed role intersection missing");
        b.visible=false;check(snapTargets({b},"wp",{}).empty(),"hidden geometry snapped");b.visible=true;b.selectable=false;
        check(snapTargets({b},"wp",{}).empty() && snapTargets({a},"other",{}).empty(),"locked or other-plane geometry snapped");
        b.selectable=true;b.geometry=Circle{{0,0},1};
        const auto directional=snapTargets({b},"wp",{1.01,8},Point{1,0});
        check(std::any_of(directional.begin(),directional.end(),[](const auto& t){return t.kind==SnapKind::Tangent && near(t.point,{1,8});}),"on-circle tangent direction missing");
        for(double zoom:{0.01,0.1,1.0,10.0,1000.0}) {
            const Point exact{1.234567891,7.654321987};const auto project=[&](Point p){return Point{p.x*zoom+100,p.y*zoom-20};};
            const auto projected=project(exact);const auto close=closestSnap({{exact,SnapKind::Endpoint,"Endpoint"}},{projected.x+8,projected.y},10,project);
            check(close && close->point==exact,"zoom changed stored snap position");
            check(!closestSnap({{exact,SnapKind::Endpoint,{}}},{projected.x+11,projected.y},10,project),"snap radius is not in pixels");
        }
        const auto preferred=closestSnap({{{0,0},SnapKind::Nearest,"On line"},{{8,0},SnapKind::Endpoint,"Endpoint"}},{0,0},10,[](Point p){return p;});
        check(preferred && preferred->kind==SnapKind::Endpoint,"nearest curve swallowed a meaningful snap target");
        check(!contains(Circle{{},1},{std::numeric_limits<double>::quiet_NaN(),0}),"NaN point accepted");
        std::cout<<"Analytic intersections and snapping checks passed.\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
