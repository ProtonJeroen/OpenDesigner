#include "SketchSnapping.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

using namespace Sketch;
namespace {
constexpr double pi=std::numbers::pi;
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
double distance(Point a,Point b) {return std::hypot(a.x-b.x,a.y-b.y);}
void nearest(Geometry curve,Point query) {
    const auto point=nearestPoint(curve,query);check(point.has_value(),"ellipse nearest solver failed");
    const auto* arc=std::get_if<EllipticalArc>(&curve);const auto e=arc?arc->ellipse:std::get<Ellipse>(curve);
    check(contains(curve,*point,1.e-8),"nearest point outside support or arc domain");
    double sampled=std::numeric_limits<double>::infinity();
    // Independent distance bound only, never used by application/model data.
    for(int i=0;i<=10000;++i) sampled=std::min(sampled,distance(query,ellipsePoint(e,arc?arc->startParameter+arc->sweepParameter*i/10000:2*pi*i/10000)));
    check(distance(query,*point)<=sampled+1.e-9,"nearest point is radial or not the Euclidean global minimum");
    const auto ends=arc?ellipticalArcEndpoints(*arc):std::array<Point,2>{};
    if(!arc || (distance(*point,ends[0])>1.e-7 && distance(*point,ends[1])>1.e-7)) {
        const auto t=ellipseParameter(e,*point);check(t.has_value(),"nearest inverse parameter missing");
        const auto v=ellipseDerivative(e,*t);
        check(std::abs(v.x*(query.x-point->x)+v.y*(query.y-point->y))<1.e-6,"nearest point normal is not perpendicular to tangent");
    }
}
}
int main() {
    try {
        const Ellipse e{{},10,2,0};
        for(Point p:{Point{8,6},Point{0,0},Point{1,0},Point{-20,4},Point{0,9},Point{10,0}}) {
            nearest(e,p);nearest(EllipticalArc{e,5.8,1.8},p);nearest(EllipticalArc{e,0.5,-4.5},p);
        }
        nearest(Ellipse{{30,-40},12,3,0.7},{42,-19});nearest(Ellipse{{},4,4,1.2},{0,0});
        check(!nearestPoint(Ellipse{{},1,0,0},{0,0}),"degenerate ellipse nearest accepted");
        check(!nearestPoint(e,{std::numeric_limits<double>::infinity(),0}),"infinite query accepted");
        auto r=intersect(e,InfiniteLine{{0,0},{1,0}});check(r.kind==IntersectionKind::Finite && r.points.size()==2,"line ellipse crossings missing");
        check(distance(r.points[0],{-10,0})<1.e-10 && distance(r.points[1],{10,0})<1.e-10,"line ellipse values inaccurate");
        r=intersect(InfiniteLine{{0,2},{1,0}},e);check(r.kind==IntersectionKind::Finite && r.points.size()==1 && distance(r.points[0],{0,2})<1.e-10,"ellipse tangency incorrect");
        check(intersect(e,InfiniteLine{{0,3},{1,0}}).kind==IntersectionKind::None,"disjoint line intersects ellipse");
        check(intersect(e,Segment{{-11,0},{0,0}}).points.size()==1,"ellipse ignored finite segment domain");
        check(intersect(EllipticalArc{e,5.8,1.0},InfiniteLine{{0,0},{1,0}}).points.size()==1,"line intersection ignored elliptical arc seam/domain");
        check(intersect(EllipticalArc{e,0.5,-1.0},InfiniteLine{{0,0},{1,0}}).points.size()==1,"CW arc intersection incorrect");
        const Ellipse rotated{{27,-40},15,6,0.73};const auto tangentPoint=ellipsePoint(rotated,0.4),tangent=ellipseDerivative(rotated,0.4);
        const double size=std::hypot(tangent.x,tangent.y);
        r=intersect(rotated,InfiniteLine{tangentPoint,{tangent.x/size,tangent.y/size}});
        check(r.points.size()==1 && contains(rotated,r.points[0],1.e-7),"rotated ellipse tangent lost or duplicated within numerical precision");
        for(const auto& other:std::vector<Geometry>{Circle{{},10},Arc{{},10,0,1},e,EllipticalArc{e,0,1}})
            check(intersect(e,other).kind==IntersectionKind::Unsupported,"unsupported conic-conic intersection reported as disjoint");
        check(intersect(Ellipse{{},1,-1,0},InfiniteLine{}).kind==IntersectionKind::NumericalFailure,"invalid elliptic intersection not diagnosed");
        check(tangentPoints({30,30},e).empty(),"unsupported ellipse tangent fabricated");
        Entity a;a.workplaneId="wp";a.geometry=e;
        auto b=a;b.id="arc";b.geometry=EllipticalArc{e,5.8,-1.2};b.role=Role::Construction;
        const auto targets=snapTargets({a,b},"wp",{6,4});
        for(const auto kind:{SnapKind::Center,SnapKind::Endpoint,SnapKind::Midpoint,SnapKind::Nearest})
            check(std::any_of(targets.begin(),targets.end(),[&](const auto& t){return t.kind==kind;}),"elliptic basic snap target missing");
        const auto exact=ellipticalArcEndpoints(std::get<EllipticalArc>(b.geometry))[0];
        for(double zoom:{0.1,1.0,10.0,100.0}) {
            const auto project=[&](Point p){return Point{100+p.x*zoom,100+p.y*zoom};};const auto p=project(exact);
            const auto caught=closestSnap({{exact,SnapKind::Endpoint,{}}},{p.x+4,p.y},8,project);
            check(caught && caught->point==exact && !closestSnap({{exact,SnapKind::Endpoint,{}}},{p.x+9,p.y},8,project),"ellipse snap radius lost pixel semantics/exact stored position");
        }
        a.visible=false;b.selectable=false;check(snapTargets({a,b},"wp",{}).empty(),"hidden/locked conic snapped");
        check(snapTargets({a,b},"other",{}).empty(),"conic from wrong Workplane snapped");
        std::cout<<"Elliptic nearest, line intersections, domain filtering, basic snaps and explicit limits passed.\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
