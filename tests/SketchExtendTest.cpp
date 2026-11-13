#include "SketchCurveOperations.h"
#include "SketchProfileValidation.h"
#include "SketchProfileBuilder.h"
#include <QCoreApplication>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace {
using namespace Sketch;
namespace C=SketchCurveOperations;
constexpr double pi=std::numbers::pi;
void check(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
void near(double a,double b,double tolerance=1.e-9) {if(std::abs(a-b)>tolerance) throw std::runtime_error("independent assertion: expected "+std::to_string(b)+", got "+std::to_string(a));}
Geometry vertical(double x,double bottom=-20,double top=20) {return Segment{{x,bottom},{x,top}};}
C::ExtendResult certifiedExtend(const Geometry& s,C::Endpoint endpoint,const Geometry& b) {
    const auto result=C::extend(s,endpoint,b);
    if(result.status!=C::Status::Success) throw std::runtime_error(result.message.toStdString());
    check(result.geometry.has_value(),"successful Extend has no candidate");return result;
}
void lines() {
    const Segment s{{0,0},{4,0}};const auto original=s;
    const auto end=certifiedExtend(s,C::Endpoint::End,vertical(10));const auto e=std::get<Segment>(*end.geometry);
    check(e.start==s.start,"line Extend changed fixed endpoint");near(e.end.x,10);near(e.end.y,0);near(end.targetParameter,10);
    const auto start=certifiedExtend(s,C::Endpoint::Start,vertical(-3));const auto a=std::get<Segment>(*start.geometry);
    check(a.end==s.end,"start Extend changed fixed endpoint");near(a.start.x,-3);near(start.targetParameter,-3);
    const auto reversed=certifiedExtend(Segment{s.end,s.start},C::Endpoint::Start,vertical(10));
    near(std::get<Segment>(*reversed.geometry).start.x,10);near(std::get<Segment>(*reversed.geometry).end.x,0);
    const auto circle=certifiedExtend(s,C::Endpoint::End,Circle{{0,0},10});near(std::get<Segment>(*circle.geometry).end.x,10); // other root is on the opposite side
    const auto filtered=certifiedExtend(Segment{{-10,0},{0,0}},C::Endpoint::End,Circle{{0,0},5});near(std::get<Segment>(*filtered.geometry).end.x,5); // one root lies inside source
    check(s==original,"Extend modified input line");
    // Unique selected finite boundary excludes the remote intersection.
    const auto finite=certifiedExtend(s,C::Endpoint::End,EllipticalArc{Ellipse{{10,0},3,2,0},pi/2,pi});near(std::get<Segment>(*finite.geometry).end.x,7);
}
void conics() {
    const Arc a{{0,0},5,0,pi/4};
    const auto upper=certifiedExtend(a,C::Endpoint::End,Segment{{-1,3},{1,5}}); // line y=x+4, root (sqrt(34)/2-2, sqrt(34)/2+2)
    const auto arc=std::get<Arc>(*upper.geometry);
    const double x=-2+std::sqrt(34.)/2,y=2+std::sqrt(34.)/2;
    near(arc.startAngle,0);near(arc.sweepAngle,std::atan2(y,x));near(arc.radius,5);
    const auto start=certifiedExtend(a,C::Endpoint::Start,Segment{{-1,-6},{1,-4}});
    check(std::get<Arc>(*start.geometry).startAngle<0,"arc start extension changed traversal");near(std::get<Arc>(*start.geometry).startAngle+std::get<Arc>(*start.geometry).sweepAngle,pi/4);
    const auto reversed=certifiedExtend(reversedArc(a),C::Endpoint::Start,Segment{{-1,3},{1,5}});
    const auto reverse=std::get<Arc>(*reversed.geometry);near(reverse.startAngle,arc.startAngle+arc.sweepAngle);near(reverse.sweepAngle,-arc.sweepAngle);
    const auto insideIgnored=std::get<Arc>(*certifiedExtend(a,C::Endpoint::End,Segment{{-10,3},{10,3}}).geometry);
    near(insideIgnored.sweepAngle,pi-std::asin(.6)); // near root lies inside the existing arc
    const Arc seamSource{{0,0},5,3*pi/2,pi/4};
    const auto seam=certifiedExtend(seamSource,C::Endpoint::End,Segment{{3,-1},{3,5}}); // (3,4), lower root excluded by finite boundary
    const auto seamArc=std::get<Arc>(*seam.geometry);near(seamArc.startAngle,3*pi/2);near(seamArc.startAngle+seamArc.sweepAngle,2*pi+std::atan2(4.,3.));
    const Ellipse ellipse{{0,0},5,3,0};const EllipticalArc ea{ellipse,0,pi/4};
    const auto elliptic=certifiedExtend(ea,C::Endpoint::End,vertical(3,0,5));const auto e=std::get<EllipticalArc>(*elliptic.geometry);
    check(e.ellipse==ellipse,"elliptical support changed");near(e.startParameter,0);near(e.sweepParameter,std::acos(.6));
    const auto ends=ellipticalArcEndpoints(e);near(ends[1].x,3);near(ends[1].y,2.4);
    const auto reverseEllipse=certifiedExtend(reversedEllipticalArc(ea),C::Endpoint::Start,vertical(3,0,5));const auto re=std::get<EllipticalArc>(*reverseEllipse.geometry);
    near(re.startParameter,e.sweepParameter);near(re.sweepParameter,-e.sweepParameter);
    const double angle=.37;const Point shift{11,-7};const auto transform=[&](Point p){return Point{shift.x+std::cos(angle)*p.x-std::sin(angle)*p.y,shift.y+std::sin(angle)*p.x+std::cos(angle)*p.y};};
    const auto rotated=certifiedExtend(EllipticalArc{Ellipse{shift,5,3,angle},0,pi/4},C::Endpoint::End,Segment{transform({3,0}),transform({3,5})});
    const auto er=std::get<EllipticalArc>(*rotated.geometry);near(er.sweepParameter,e.sweepParameter);check(er.ellipse.center==shift,"rotated Extend lost local center");
}
void splines() {
    BSpline s;s.degree=2;s.poles={{0,0},{5,10},{10,0}};s.weights={1,1,1};s.knots={0,1};s.multiplicities={3,3};s.first=.25;s.last=.5;
    const auto original=s;const auto result=certifiedExtend(s,C::Endpoint::End,vertical(7.5));const auto e=std::get<BSpline>(*result.geometry);
    near(e.first,.25);near(e.last,.75);check(e.poles==s.poles && e.weights==s.weights && e.knots==s.knots && e.multiplicities==s.multiplicities,"coefficient Extend changed support");
    const auto c=bsplineCurve(e);near(c->Value(e.last).X(),7.5);near(c->Value(e.last).Y(),3.75); // x=10t, y=20t(1-t)
    auto reversed=s;reversed.reversed=true;const auto r=std::get<BSpline>(*certifiedExtend(reversed,C::Endpoint::Start,vertical(7.5)).geometry);
    check(r.reversed,"coefficient Extend lost reversed traversal");near(r.last,.75);
    const auto left=std::get<BSpline>(*certifiedExtend(s,C::Endpoint::Start,vertical(1)).geometry);near(left.first,.1);near(left.last,.5);
    check(s==original,"coefficient Extend mutated input");
    BSpline rational;rational.degree=2;rational.poles={{1,0},{1,1},{0,1}};rational.weights={1,std::sqrt(.5),1};rational.knots={0,1};rational.multiplicities={3,3};rational.first=.2;rational.last=.4;
    const auto rationalResult=std::get<BSpline>(*certifiedExtend(rational,C::Endpoint::End,vertical(std::sqrt(.5),0,2)).geometry);
    near(rationalResult.last,.5);check(rationalResult.weights==rational.weights && rationalResult.poles==rational.poles,"rational continuation changed coefficients");
    const auto periodic=splineCurve(Spline{{{0,0},{10,0},{10,10},{0,10}},true});
    BSpline p;p.degree=periodic->Degree();p.periodic=true;p.first=5;p.last=10;
    for(int i=1;i<=periodic->NbPoles();++i) {p.poles.push_back({periodic->Pole(i).X(),periodic->Pole(i).Y()});p.weights.push_back(periodic->Weight(i));}
    for(int i=1;i<=periodic->NbKnots();++i) {p.knots.push_back(periodic->Knot(i));p.multiplicities.push_back(periodic->Multiplicity(i));}
    const auto continued=std::get<BSpline>(*certifiedExtend(p,C::Endpoint::End,vertical(5,5,20)).geometry);
    near(continued.first,5);near(periodic->Value(continued.last).X(),5);
    check(continued.last>10 && continued.last<periodic->LastParameter() && periodic->Value(continued.last).Y()>10,"periodic continuation chose the wrong boundary portion");
    check(continued.poles==p.poles && continued.knots==p.knots,"periodic continuation changed support");
    const auto atSeam=std::get<BSpline>(*certifiedExtend(p,C::Endpoint::End,Segment{{-1,-1},{1,1}}).geometry);
    near(atSeam.first,5);near(atSeam.last,periodic->LastParameter());
    p.reversed=true;
    const auto reversedSeam=std::get<BSpline>(*certifiedExtend(p,C::Endpoint::End,Segment{{-1,-1},{1,1}}).geometry);
    check(reversedSeam.reversed,"periodic seam continuation lost reversal");near(reversedSeam.first,periodic->FirstParameter());near(reversedSeam.last,10);
}
void profiles() {
    Entity source;source.id="source";source.workplaneId="wp";source.geometry=Segment{{0,0},{2,0}};
    auto boundary=source;boundary.id="boundary";boundary.geometry=Segment{{4,0},{4,3}};
    source.geometry=*certifiedExtend(source.geometry,C::Endpoint::End,boundary.geometry).geometry;
    auto top=source;top.id="top";top.geometry=Segment{{4,3},{0,3}};auto left=source;left.id="left";left.geometry=Segment{{0,3},{0,0}};
    const auto valid=SketchProfileValidation::validate({source,boundary,top,left},"wp");check(valid.status==SketchProfileValidation::Status::Valid,"extended rectangular profile failed validation");
    near(double(std::abs(valid.contours[0].signedArea)),12);
    const auto built=SketchProfileBuilder::build(valid,gp_Ax3(gp_Pnt(11,13,17),gp_Dir(1,2,3),gp_Dir(2,-1,0)));
    check(built.success && built.faces.size()==1,"extended rectangular profile failed certification");near(built.faces[0].area,12,1.e-7);
    check(SketchProfileValidation::validate({source},"wp").status==SketchProfileValidation::Status::Invalid,"open Sketch extension was incorrectly certified as closed Profile");
    const Ellipse ellipse{{0,0},5,3,0};source.geometry=*certifiedExtend(EllipticalArc{ellipse,0,pi/4},C::Endpoint::End,vertical(3,0,5)).geometry;
    const auto arc=std::get<EllipticalArc>(source.geometry);const auto ends=ellipticalArcEndpoints(arc);boundary.geometry=Segment{ends[1],ends[0]};
    const auto pv=SketchProfileValidation::validate({source,boundary},"wp");check(pv.status==SketchProfileValidation::Status::Valid,"extended ellipse/chord profile failed");
    const double area=7.5*(arc.sweepParameter-std::sin(arc.sweepParameter));near(double(std::abs(pv.contours[0].signedArea)),area,1.e-9);
    const auto face=SketchProfileBuilder::build(pv,gp_Ax3());check(face.success,"extended ellipse/chord wire failed");near(face.faces[0].area,area,1.e-7);
}
void refusals() {
    const auto refuse=[](const Geometry& s,C::Endpoint endpoint,const Geometry& boundary,C::Status expected) {
        const auto r=C::extend(s,endpoint,boundary);if(r.status!=expected) throw std::runtime_error((QString("Unexpected refusal for %1 / %2 (status %3, expected %4): ").arg(typeName(s),typeName(boundary)).arg(int(r.status)).arg(int(expected))+r.message).toStdString());check(!r.geometry,"refusal exposed partial candidate");
    };
    const Segment s{{0,0},{4,0}};
    refuse(s,C::Endpoint::End,vertical(2),C::Status::Unsupported);
    refuse(s,C::Endpoint::End,vertical(-3),C::Status::Unsupported);
    refuse(s,C::Endpoint::End,Segment{{10,1},{10,5}},C::Status::Unsupported); // boundary support crossing is outside its finite domain
    refuse(s,C::Endpoint::End,Circle{{10,0},2},C::Status::Ambiguous);
    refuse(s,C::Endpoint::End,Circle{{10,2},2},C::Status::Ambiguous);
    refuse(s,C::Endpoint::End,Segment{{8,0},{12,0}},C::Status::Overlap);
    refuse(Arc{{0,0},5,0,pi/4},C::Endpoint::End,Arc{{0,0},5,pi,pi/4},C::Status::Overlap);
    refuse(Arc{{0,0},5,0,pi/8},C::Endpoint::End,Segment{{-10,3},{10,3}},C::Status::Ambiguous);
    refuse(Arc{{0,0},5,3*pi/2,pi/4},C::Endpoint::End,vertical(5,-1,1),C::Status::Ambiguous);
    refuse(Circle{{0,0},5},C::Endpoint::End,vertical(7),C::Status::Unsupported);
    refuse(Spline{{{0,0},{5,4},{10,0}}},C::Endpoint::End,vertical(12),C::Status::Unsupported);
    BSpline b;b.degree=2;b.poles={{0,0},{5,10},{10,0}};b.weights={1,1,1};b.knots={0,1};b.multiplicities={3,3};b.first=.25;b.last=1;
    refuse(b,C::Endpoint::End,vertical(12),C::Status::Unsupported);
    b.last=.5;refuse(b,C::Endpoint::End,vertical(12),C::Status::Unsupported);
    const auto invalid=C::extend(s,C::Endpoint::End,vertical(10),0);check(invalid.status==C::Status::Invalid && !invalid.geometry,"invalid tolerance accepted");
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {std::cout<<"lines\n";lines();std::cout<<"conics\n";conics();std::cout<<"splines\n";splines();std::cout<<"profiles\n";profiles();std::cout<<"refusals\n";refusals();std::cout<<"Extend geometry passed\n";return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
