#include "SketchCurveOperations.h"
#include "SketchProfileValidation.h"
#include "SketchProfileBuilder.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <Geom2d_BSplineCurve.hxx>
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
void near(double a,double b,double tolerance=1.e-9) {check(std::abs(a-b)<=tolerance,"independent numerical assertion failed");}
C::TrimResult trim(const Geometry& source,const std::vector<Geometry>& boundaries,Point pick,size_t count) {
    const auto r=C::trim(source,boundaries,pick);
    if(r.status!=C::Status::Success) throw std::runtime_error(r.message.toStdString());
    check(r.remaining.size()==count,"wrong retained fragment count");return r;
}
Geometry vertical(double x) {return Segment{{x,-20},{x,20}};}
void lines() {
    const Segment s{{0,0},{10,0}};const auto original=s;
    const auto middle=trim(s,{vertical(3),vertical(7)},{5,0},2);
    const auto a=std::get<Segment>(middle.remaining[0]),b=std::get<Segment>(middle.remaining[1]);
    near(a.start.x,0);near(a.end.x,3);near(b.start.x,7);near(b.end.x,10);
    const auto tail=trim(s,{vertical(3)},{1,0},1);near(std::get<Segment>(tail.remaining[0]).start.x,3);
    const auto reverse=trim(Segment{s.end,s.start},{vertical(3),vertical(7)},{5,0},2);
    near(std::get<Segment>(reverse.remaining[0]).start.x,10);near(std::get<Segment>(reverse.remaining[0]).end.x,7);
    near(std::get<Segment>(reverse.remaining[1]).start.x,3);near(std::get<Segment>(reverse.remaining[1]).end.x,0);
    check(s==original,"Trim mutated source");
}
void conics() {
    const Circle c{{0,0},5};
    const auto result=trim(c,{vertical(3)},{5,0},1);const auto arc=std::get<Arc>(result.remaining[0]);
    near(arc.startAngle,std::acos(.6));near(arc.sweepAngle,2*pi-2*std::acos(.6));check(arc.sweepAngle>pi,"wrong complementary circular arc");
    const auto semicircle=trim(c,{Segment{{-8,0},{8,0}}},{0,5},1);near(std::get<Arc>(semicircle.remaining[0]).sweepAngle,pi);
    const Arc clockwise{{0,0},5,pi,-pi};
    const auto reversed=trim(clockwise,{vertical(0)},{-3,4},1);const auto cw=std::get<Arc>(reversed.remaining[0]);near(cw.startAngle,pi/2);near(cw.sweepAngle,-pi/2);
    const Ellipse e{{0,0},5,3,0};
    const auto ellipse=trim(e,{vertical(3)},{5,0},1);const auto ea=std::get<EllipticalArc>(ellipse.remaining[0]);
    near(ea.startParameter,std::acos(.6));near(ea.sweepParameter,2*pi-2*std::acos(.6));
    const auto end=ellipticalArcEndpoints(ea);near(end[0].x,3);near(end[0].y,2.4);near(end[1].x,3);near(end[1].y,-2.4);
    const EllipticalArc source{e,3*pi/2,pi};
    const auto seam=trim(source,{Segment{{-8,0},{8,0}}},ellipsePoint(e,7*pi/4),1);
    const auto kept=std::get<EllipticalArc>(seam.remaining[0]);near(kept.startParameter,2*pi);near(kept.sweepParameter,pi/2);
    const auto reversedEllipse=trim(reversedEllipticalArc(source),{Segment{{-8,0},{8,0}}},ellipsePoint(e,7*pi/4),1);
    const auto rk=std::get<EllipticalArc>(reversedEllipse.remaining[0]);near(rk.startParameter,5*pi/2);near(rk.sweepParameter,-pi/2);
    // Rotate and translate all local input values: unchanged exact parameter route.
    const double angle=.43;const Point shift{17,-9};
    const auto transform=[&](Point p){return Point{shift.x+p.x*std::cos(angle)-p.y*std::sin(angle),shift.y+p.x*std::sin(angle)+p.y*std::cos(angle)};};
    const auto rotated=trim(Ellipse{shift,5,3,angle},{Segment{transform({3,-10}),transform({3,10})}},transform({5,0}),1);
    const auto ra=std::get<EllipticalArc>(rotated.remaining[0]);near(ra.startParameter,ea.startParameter);near(ra.sweepParameter,ea.sweepParameter);
}
void splinesAndProfiles() {
    const Spline s{{{0,0},{5,4},{10,0}}};const auto original=splineCurve(s);
    const auto left=trim(s,{vertical(2.5)},{1,1.44},1);
    const auto middle=trim(left.remaining.front(),{vertical(7.5)},{9,1.44},1);
    const auto exact=std::get<BSpline>(middle.remaining.front());const auto curve=bsplineCurve(exact);
    check(exact.degree==original->Degree() && exact.periodic==original->IsPeriodic(),"spline support attributes changed");
    for(int i=1;i<=original->NbPoles();++i) {
        check(original->Pole(i).Distance(curve->Pole(i))==0,"spline fragment was re-interpolated");near(original->Weight(i),curve->Weight(i),0);
    }
    for(int i=1;i<=original->NbKnots();++i) {near(original->Knot(i),curve->Knot(i),0);check(original->Multiplicity(i)==curve->Multiplicity(i),"knots changed");}
    for(int i=0;i<=8;++i) {const double u=exact.first+(exact.last-exact.first)*i/8;check(curve->Value(u).Distance(original->Value(u))==0,"exact spline support changed");}
    near(curve->Value(exact.first).X(),2.5);near(curve->Value(exact.last).X(),7.5);
    auto reversed=exact;reversed.reversed=true;
    const auto rev=trim(reversed,{vertical(5)},{6,3.84},1);check(std::get<BSpline>(rev.remaining[0]).reversed,"reversed spline Trim lost traversal");
    Entity edge;edge.id="fragment";edge.workplaneId="wp";edge.geometry=exact;
    Entity chord=edge;chord.id="chord";chord.geometry=Segment{{7.5,3},{2.5,3}};
    const std::vector<Entity> profile{edge,chord};const auto validated=SketchProfileValidation::validate(profile,"wp");
    if(validated.status!=SketchProfileValidation::Status::Valid) throw std::runtime_error(validated.diagnostics.front().message.toStdString());
    near(double(std::abs(validated.contours[0].signedArea)),10./3,1.e-8);
    const gp_Ax3 frame(gp_Pnt(7,11,19),gp_Dir(1,2,3),gp_Dir(2,-1,0));
    const auto built=SketchProfileBuilder::build(validated,frame);
    if(!built.success) throw std::runtime_error(built.diagnostics.front().message.toStdString());
    check(built.faces.size()==1 && built.wires[0].edges.size()==2,"trimmed spline profile wire changed edge count");near(built.faces[0].area,10./3,1.e-7);
    for(const auto& builtEdge:built.wires[0].edges) check(BRep_Tool::Tolerance(builtEdge.shape)<=1.e-7,"builder increased edge tolerance");
    auto nested=profile;
    const Point corners[]={{-2,-2},{12,-2},{12,8},{-2,8}};
    for(int i=0;i<4;++i) {auto side=chord;side.id=QString("outer%1").arg(i);side.geometry=Segment{corners[i],corners[(i+1)%4]};nested.push_back(side);}
    const auto nestedProfile=SketchProfileValidation::validate(nested,"wp");
    check(nestedProfile.status==SketchProfileValidation::Status::Valid && nestedProfile.regions.size()==1 && nestedProfile.regions[0].holes.size()==1,"trimmed spline hole containment failed");
    const auto nestedBuilt=SketchProfileBuilder::build(nestedProfile,frame);check(nestedBuilt.success,"trimmed spline hole certification failed");near(nestedBuilt.faces[0].area,140-10./3,1.e-7);
    auto opposite=profile;std::get<BSpline>(opposite[0].geometry).reversed=true;
    const auto oppositeProfile=SketchProfileValidation::validate(opposite,"wp");
    check(oppositeProfile.status==SketchProfileValidation::Status::Valid && SketchProfileBuilder::build(oppositeProfile,frame).success,"reversed coefficient profile failed");
    auto open=profile;open.pop_back();check(SketchProfileValidation::validate(open,"wp").status==SketchProfileValidation::Status::Invalid,"open coefficient profile accepted");
    BSpline rational;rational.degree=2;rational.poles={{1,0},{1,1},{0,1}};rational.weights={1,std::sqrt(.5),1};rational.knots={0,1};rational.multiplicities={3,3};
    const auto rationalTrim=trim(rational,{vertical(std::sqrt(.5))},{.95,.3122498999},1);
    const auto rationalFragment=std::get<BSpline>(rationalTrim.remaining.front());check(rationalFragment.weights==rational.weights,"rational Trim changed weights");
    edge.geometry=rationalFragment;const auto refused=SketchProfileValidation::validate({edge},"wp");
    check(refused.status==SketchProfileValidation::Status::Unsupported && refused.diagnostics[0].code==SketchProfileValidation::Code::UnsupportedCurve,"rational coefficient profile was not explicitly refused");
    // Independent cubic certificate: x=3t, y=27t^3, hence y=x^3.
    BSpline cubic;cubic.degree=3;cubic.poles={{0,0},{1,0},{2,0},{3,27}};cubic.weights={1,1,1,1};cubic.knots={0,1};cubic.multiplicities={4,4};
    const auto leftCubic=trim(cubic,{vertical(.75)},{.3,.027},1);
    const auto midCubic=trim(leftCubic.remaining.front(),{vertical(2.25)},{2.8,21.952},1);
    edge.geometry=midCubic.remaining.front();chord.geometry=Segment{{2.25,std::pow(2.25,3)},{.75,std::pow(.75,3)}};
    const auto cubicProfile=SketchProfileValidation::validate({edge,chord},"wp");
    check(cubicProfile.status==SketchProfileValidation::Status::Valid,"trimmed cubic profile failed validation");
    const double expected=1.5*(std::pow(.75,3)+std::pow(2.25,3))/2-(std::pow(2.25,4)-std::pow(.75,4))/4;
    near(double(std::abs(cubicProfile.contours[0].signedArea)),expected,1.e-9);
    const auto cubicBuilt=SketchProfileBuilder::build(cubicProfile,frame);check(cubicBuilt.success,"trimmed cubic profile failed wire certification");near(cubicBuilt.faces[0].area,expected,1.e-7);
}
void periodic() {
    const Spline source{{{0,0},{10,0},{10,10},{0,10}},true};
    const auto original=splineCurve(source);
    // Remove the right side, keeping a path that crosses the existing seam.
    const auto r=trim(source,{vertical(5)},{11.25,5},2);
    for(const auto& piece:r.remaining) {
        const auto exact=std::get<BSpline>(piece);const auto c=bsplineCurve(exact);
        check(exact.periodic && exact.first>=original->FirstParameter() && exact.last<=original->LastParameter(),"periodic fragment lost its canonical domain");
        for(int i=1;i<=c->NbPoles();++i) check(c->Pole(i).Distance(original->Pole(i))==0,"periodic coefficients changed");
    }
    auto reversed=std::get<BSpline>(r.remaining[0]);reversed.first=original->FirstParameter();reversed.last=original->LastParameter();reversed.reversed=true;
    const auto reverseTrim=trim(reversed,{vertical(5)},{11.25,5},2);
    for(size_t i=0;i<2;++i) {
        const auto backwards=std::get<BSpline>(reverseTrim.remaining[i]),forwards=std::get<BSpline>(r.remaining[1-i]);
        check(backwards.reversed,"periodic Trim lost reversed traversal");near(backwards.first,forwards.first);near(backwards.last,forwards.last);
        check(backwards.poles==forwards.poles && backwards.knots==forwards.knots && backwards.weights==forwards.weights,"reversed periodic Trim changed support coefficients");
    }
    Entity a;a.id="a";a.workplaneId="wp";a.geometry=r.remaining[0];auto b=a;b.id="b";b.geometry=r.remaining[1];auto line=a;line.id="line";
    const auto ac=bsplineCurve(std::get<BSpline>(a.geometry)),bc=bsplineCurve(std::get<BSpline>(b.geometry));
    const auto start=ac->Value(std::get<BSpline>(a.geometry).first),end=bc->Value(std::get<BSpline>(b.geometry).last);
    line.geometry=Segment{{end.X(),end.Y()},{start.X(),start.Y()}};
    const auto pv=SketchProfileValidation::validate({a,b,line},"wp");
    if(pv.status!=SketchProfileValidation::Status::Valid) throw std::runtime_error(pv.diagnostics.front().message.toStdString());
    const auto built=SketchProfileBuilder::build(pv,gp_Ax3());if(!built.success) throw std::runtime_error(built.diagnostics.front().message.toStdString());
    check(built.wires[0].edges.size()==3,"periodic Trim introduced missing or duplicate edges");
}
void refusals() {
    const auto refuse=[](const Geometry& s,const std::vector<Geometry>& b,Point p) {const auto r=C::trim(s,b,p);check(r.status!=C::Status::Success && r.remaining.empty(),"unsafe Trim exposed a partial candidate");};
    refuse(Segment{{0,0},{10,0}},{},{5,0});
    refuse(Circle{{0,0},5},{Segment{{-10,5},{10,5}}},{0,-5});
    refuse(Circle{{0,0},5},{},{5,0});
    refuse(Segment{{0,0},{10,0}},{Segment{{2,0},{8,0}}},{5,0});
    refuse(Segment{{0,0},{10,0}},{vertical(3)},{3,0});
    refuse(Segment{{0,0},{10,0}},{vertical(3),vertical(3+1.e-8)},{1,0});
    refuse(InfiniteLine{{0,0},{1,0}},{vertical(3)},{0,0});
    BSpline bow;bow.degree=1;bow.poles={{0,0},{2,2},{0,2},{2,0}};bow.weights={1,1,1,1};bow.knots={0,1,2,3};bow.multiplicities={2,1,1,2};bow.last=3;
    refuse(bow,{vertical(1)},{.5,.5});
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {std::cout<<"lines\n";lines();std::cout<<"conics\n";conics();std::cout<<"spline/profile\n";splinesAndProfiles();std::cout<<"periodic\n";periodic();std::cout<<"refusals\n";refusals();std::cout<<"Trim geometry passed\n";return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
