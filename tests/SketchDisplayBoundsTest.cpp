#include "SketchDisplayBounds.h"
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Edge.hxx>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

using namespace Sketch;
namespace {
constexpr double pi=std::numbers::pi;
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void near(double a,double b) {check(std::abs(a-b)<=1.e-8,"incorrect analytic bound");}
void rectangle(const Geometry& g,DisplayRectangle expected) {
    const auto r=finiteBounds(g);check(r.has_value(),"finite bound missing");
    near(r->minX,expected.minX);near(r->minY,expected.minY);near(r->maxX,expected.maxX);near(r->maxY,expected.maxY);
}
void endpoint(Point p,Point q) {near(p.x,q.x);near(p.y,q.y);}
}
int main() {
    try {
        rectangle(Segment{{8,-3},{-2,5}},{-2,-3,8,5});
        rectangle(ConstructionPoint{{10,-20}},{10,-20,10,-20});
        rectangle(Circle{{8,-3},5},{3,-8,13,2});
        check(!finiteBounds(InfiniteLine{{1.e12,-1.e12},{0,1}}),"infinite line enlarged frame");
        // Quarter, major, negative and seam-crossing domains; reversing must
        // produce exactly the same bounds without using the support circle.
        for(const auto& arc:std::vector<Arc>{{{0,0},10,0,pi/2},{{0,0},10,pi/4,1.5*pi},{{0,0},10,pi/4,-pi/2},{{0,0},10,1.75*pi,pi/2}}) {
            const auto a=finiteBounds(arc).value(),b=finiteBounds(reversedArc(arc)).value();
            near(a.minX,b.minX);near(a.minY,b.minY);near(a.maxX,b.maxX);near(a.maxY,b.maxY);
        }
        rectangle(Arc{{0,0},10,1.75*pi,pi/2},{std::sqrt(50.),-std::sqrt(50.),10,std::sqrt(50.)});
        rectangle(Arc{{0,0},10,0,-1.5*pi},{-10,-10,10,10});
        const Ellipse e{{100,-50},20,3,pi/4};
        const double radius=std::sqrt(204.5);
        rectangle(e,{100-radius,-50-radius,100+radius,-50+radius});
        // Elliptic extrema are not at the major/minor axis endpoints.
        const double tx=std::atan2(-3.,20.);
        const EllipticalArc a{e,tx-0.1,0.2};const auto r=finiteBounds(a).value();
        near(r.maxX,100+radius);near(r.minX,std::min(ellipsePoint(e,a.startParameter).x,ellipsePoint(e,a.startParameter+a.sweepParameter).x));
        const auto reverse=finiteBounds(reversedEllipticalArc(a)).value();
        near(reverse.minX,r.minX);near(reverse.maxX,r.maxX);near(reverse.minY,r.minY);near(reverse.maxY,r.maxY);
        rectangle(EllipticalArc{{{0,0},20,3,pi/2},1.75*pi,pi/2},{-3/std::sqrt(2.),20/std::sqrt(2.),3/std::sqrt(2.),20});
        rectangle(EllipticalArc{{{0,0},20,3,pi/2},2.25*pi,-pi/2},{-3/std::sqrt(2.),20/std::sqrt(2.),3/std::sqrt(2.),20});
        const Ellipse narrow{{-1.e9,1.e9},1000,1.e-6,0};
        rectangle(narrow,{-1.e9-1000,1.e9-1.e-6,-1.e9+1000,1.e9+1.e-6});
        Construction::Object wp;wp.id="wp";wp.kind=Construction::Kind::Workplane;wp.extent=100;wp.displayOffset=gp_Vec(13,-8,0);
        check(displayRectangle(wp,{})==DisplayRectangle{-87,-108,113,92},"empty default extent/offset changed");
        Entity entity;entity.workplaneId=wp.id;entity.geometry=Segment{{1000,3000},{1100,3020}};
        entity.visible=false;entity.selectable=false;entity.role=Role::Construction;
        auto other=entity;other.workplaneId="other";other.geometry=Circle{{-1.e8,0},1.e7};
        auto infinite=entity;infinite.geometry=InfiniteLine{{-1.e12,0},{1,0}};
        const auto bounds=displayRectangle(wp,{entity,other,infinite});
        check(bounds==DisplayRectangle{990,2998,1110,3022},"roles/visibility/owner filtering or 10% margin incorrect");
        entity.role=Role::Profile;check(displayRectangle(wp,{entity})==bounds,"Profile ignored");
        auto point=entity;point.geometry=ConstructionPoint{{1.e12,-1.e12}};
        check(displayRectangle(wp,{point})==DisplayRectangle{1.e12-10,-1.e12-10,1.e12+10,-1.e12+10},"remote point minimum size");
        wp.frame=gp_Ax3(gp_Pnt(11,22,33),gp_Dir(1,2,3),gp_Dir(2,-1,0));
        const auto corners=displayCorners(bounds,wp.frame);
        check(corners[0].IsEqual(worldPoint({990,2998},wp.frame),1.e-9),"rotated/translated Workplane frame lost");
        const DisplayRectangle clip{-10,-5,30,15};
        auto line=InfiniteLine{{100000,0},{1,0}};auto segment=clipLine(line,clip).value();
        endpoint(segment.start,{-10,0});endpoint(segment.end,{30,0});
        const auto remote=clipLine({{1.e30,0},{1,0}},clip).value();endpoint(remote.start,{-10,0});endpoint(remote.end,{30,0});
        const auto almostVertical=clipLine({{0,0},{1.e-14,1}},clip).value();near(almostVertical.start.y,-5);near(almostVertical.end.y,15);
        const auto original=line;auto shape=displayShape(Entity{.geometry=line},wp.frame,clip);
        check(line==original,"display clipping modified infinite geometry");
        check(BRep_Tool::Pnt(TopExp::FirstVertex(TopoDS::Edge(shape))).IsEqual(worldPoint(segment.start,wp.frame),1.e-9),"display clip not in owner frame");
        segment=clipLine({{0,100000},{0,-1}},clip).value();endpoint(segment.start,{0,15});endpoint(segment.end,{0,-5});
        segment=clipLine({{0,0},{std::sqrt(0.5),std::sqrt(0.5)}},clip).value();endpoint(segment.start,{-5,-5});endpoint(segment.end,{15,15});
        check(!clipLine({{0,16},{1,0}},clip),"outside parallel line should be empty");
        check(!clipLine({{-10,15},{-std::sqrt(0.5),-std::sqrt(0.5)}},clip),"corner-only contact made edge");
        segment=clipLine({{0,15},{1,0}},clip).value();endpoint(segment.start,{-10,15});endpoint(segment.end,{30,15});
        segment=clipLine({{1.e12,-1.e12},{1,0}},{1.e12-10,-1.e12-10,1.e12+10,-1.e12+10}).value();
        endpoint(segment.start,{1.e12-10,-1.e12});endpoint(segment.end,{1.e12+10,-1.e12});
        std::cout<<"Analytic Workplane bounds and rectangular infinite-line clipping passed.\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
