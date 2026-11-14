#include "SketchCurveOperations.h"
#include <Geom2dAPI_ProjectPointOnCurve.hxx>
#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace {
using namespace Sketch;
namespace C=SketchCurveOperations;
constexpr double pi=std::numbers::pi;
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void near(double actual,double expected,double tolerance=1.e-9) {check(std::abs(actual-expected)<=tolerance,"independent numerical assertion failed");}
C::Intersections hits(const Geometry& a,const Geometry& b,size_t count) {
    const auto result=C::intersect(a,b);
    if(result.status!=C::Status::Success) throw std::runtime_error((typeName(a)+" / "+typeName(b)+": "+result.message).toStdString());
    check(result.hits.size()==count,"intersection count differs");
    return result;
}
std::vector<double> cuts(const C::Intersections& intersections) {
    std::vector<double> result;for(const auto& h:intersections.hits) result.push_back(h.firstParameter);return result;
}
void testAnalytic() {
    const Ellipse ellipse{{0,0},5,3,0};
    const Segment horizontal{{-8,0},{8,0}};
    const auto line=hits(horizontal,ellipse,2);
    near(line.hits[0].point.x,-5);near(line.hits[1].point.x,5);
    for(const auto& h:line.hits) {near(h.point.y,0);check(h.contact==C::Contact::Crossing,"line/ellipse crossing misclassified");}
    const auto reversed=hits(Segment{horizontal.end,horizontal.start},ellipse,2);
    near(reversed.hits[0].point.x,5);near(reversed.hits[1].point.x,-5);
    near(reversed.hits[0].firstParameter,3);
    const auto circle=hits(Circle{{0,0},4},ellipse,4);
    // x²+y²=16; x²/25+y²/9=1 => x²=175/16, y²=81/16.
    for(const auto& h:circle.hits) {near(h.point.x*h.point.x,175./16);near(h.point.y*h.point.y,81./16);check(h.contact==C::Contact::Crossing,"conic crossing misclassified");}
    const auto pair=hits(ellipse,Ellipse{{0,0},5,3,pi/2},4);
    for(const auto& h:pair.hits) {near(h.point.x*h.point.x,225./34);near(h.point.y*h.point.y,225./34);}
    const auto tangent=hits(Segment{{-10,3},{10,3}},ellipse,1);
    near(tangent.hits[0].point.x,0);near(tangent.hits[0].point.y,3);
    check(tangent.hits[0].contact==C::Contact::Tangency,"tangent classified as crossing");
    hits(Segment{{-10,3.001},{10,3.001}},ellipse,0);
    const auto ct=hits(Circle{{10,0},5},Circle{{0,0},5},1);
    check(ct.hits[0].contact==C::Contact::Tangency,"circle tangent classified as crossing");
    hits(Segment{{6,-1},{6,1}},ellipse,0);
    const auto swapped=hits(ellipse,horizontal,2);
    for(const auto& h:swapped.hits) {
        const auto found=std::find_if(line.hits.begin(),line.hits.end(),[&](const auto& other){return std::hypot(h.point.x-other.point.x,h.point.y-other.point.y)<1.e-10;});
        check(found!=line.hits.end(),"pair order changed intersection set");near(h.firstParameter,found->secondParameter);near(h.secondParameter,found->firstParameter);
    }
}
void testDomainsAndSeams() {
    const Ellipse e{{0,0},5,3,0};
    const EllipticalArc arc{e,3*pi/2,pi}; // wraps through 2pi, right half
    const auto forward=hits(arc,Segment{{-8,0},{8,0}},1);
    near(forward.hits[0].firstParameter,2*pi);
    const auto backward=hits(reversedEllipticalArc(arc),Segment{{-8,0},{8,0}},1);
    near(backward.hits[0].firstParameter,2*pi);
    const auto semi=hits(EllipticalArc{e,0,pi},Segment{{-8,0},{8,0}},2);
    near(semi.hits[0].firstParameter,0);near(semi.hits[1].firstParameter,pi);
    const auto seam=hits(e,Segment{{4,-4},{4,4}},2);
    for(const auto& h:seam.hits) near(std::abs(h.point.y),1.8);
    const auto atSeam=hits(e,Segment{{-8,0},{8,0}},2);
    near(atSeam.hits[0].firstParameter,0);near(atSeam.hits[1].firstParameter,pi);
    const auto selected=C::classifySegment(e,cuts(seam),{5,0});
    check(selected.status==C::Status::Success,"periodic seam segment selection failed");
    near(selected.interval.start,-std::acos(.8));near(selected.interval.end,std::acos(.8));
    const auto reversed=C::classifySegment(reversedEllipticalArc(arc),{2*pi},ellipsePoint(e,7*pi/4));
    check(reversed.status==C::Status::Success,"reversed arc selection failed");
    near(reversed.interval.start,2*pi);near(reversed.interval.end,3*pi/2);
    const auto open=C::classifySegment(Segment{{10,0},{0,0}},{3,7},{5,1});
    check(open.status==C::Status::Success,"segment interior selection failed");near(open.interval.start,3);near(open.interval.end,7);near(open.pickedParameter,5);
    check(C::classifySegment(e,cuts(atSeam),{5,0}).status==C::Status::Ambiguous,"boundary pick accepted");
    const auto whole=C::classifySegment(e,{},ellipsePoint(e,.5));
    check(whole.status==C::Status::Success,"uncut closed curve failed");near(whole.interval.start,0);near(whole.interval.end,2*pi);
    const auto single=C::classifySegment(e,{0},ellipsePoint(e,pi));
    check(single.status==C::Status::Success,"one-cut periodic segment failed");near(single.interval.start,0);near(single.interval.end,2*pi);
    check(C::classifySegment(e,{},Point{0,0}).status==C::Status::Ambiguous,"multiple nearest ellipse parameters accepted");
    check(C::classifySegment(Circle{{0,0},5},{},Point{0,0}).status==C::Status::Ambiguous,"circle center supplied a unique nearest parameter");
    check(C::classifySegment(e,{1.e-9,2*pi-1.e-9},{0,3}).status==C::Status::Ambiguous,"unresolved seam cut cluster accepted");
    const auto permutation=C::classifySegment(e,{seam.hits[1].firstParameter,seam.hits[0].firstParameter,seam.hits[0].firstParameter},{5,0});
    check(permutation.status==C::Status::Success,"duplicate or permuted cuts changed selection");near(permutation.interval.start,selected.interval.start);near(permutation.interval.end,selected.interval.end);
}
BSpline coefficients(const Handle(Geom2d_BSplineCurve)& c,double first,double last,bool reversed=false) {
    BSpline s;s.degree=c->Degree();s.periodic=c->IsPeriodic();s.first=first;s.last=last;s.reversed=reversed;
    for(int i=1;i<=c->NbPoles();++i) {s.poles.push_back({c->Pole(i).X(),c->Pole(i).Y()});s.weights.push_back(c->Weight(i));}
    for(int i=1;i<=c->NbKnots();++i) {s.knots.push_back(c->Knot(i));s.multiplicities.push_back(c->Multiplicity(i));}return s;
}
void testSplines() {
    // Three evenly spaced through-points give y=4-.16*(x-5)^2.
    const Spline s{{{0,0},{5,4},{10,0}}};const auto original=s;
    const auto result=hits(s,Segment{{-1,3},{11,3}},2);
    near(result.hits[0].point.x,2.5);near(result.hits[1].point.x,7.5);
    for(const auto& h:result.hits) {near(h.point.y,3);check(h.contact==C::Contact::Crossing,"spline crossing misclassified");}
    const auto curve=splineCurve(s);
    const auto exact=coefficients(curve,result.hits.front().firstParameter,result.hits.back().firstParameter);
    const auto snapshot=exact;
    const auto trim=hits(exact,Segment{{5,-1},{5,6}},1);near(trim.hits[0].point.x,5);near(trim.hits[0].point.y,4);
    const auto selected=C::classifySegment(exact,{},Point{5,4});
    check(selected.status==C::Status::Success,"exact trimmed spline selection failed");near(selected.interval.start,exact.first);near(selected.interval.end,exact.last);
    auto rev=exact;rev.reversed=true;
    const auto reverse=C::classifySegment(rev,{},Point{5,4});
    check(reverse.status==C::Status::Success,"reversed exact spline selection failed");near(reverse.interval.start,exact.last);near(reverse.interval.end,exact.first);
    const auto revHits=hits(coefficients(curve,curve->FirstParameter(),curve->LastParameter(),true),Segment{{-1,3},{11,3}},2);
    near(revHits.hits[0].point.x,7.5);near(revHits.hits[1].point.x,2.5);
    check(s==original && exact==snapshot,"spline input coefficients changed");
    const auto bounded=C::boundedCurve(exact);
    for(double u:{exact.first,(exact.first+exact.last)/2,exact.last}) check(bounded->Value(u).Distance(curve->Value(u))<1.e-12,"trimmed spline was re-interpolated");
    const auto tangent=hits(s,Segment{{-1,4},{11,4}},1);
    check(tangent.hits[0].contact==C::Contact::Tangency,"spline tangent classified as crossing");
    const Spline periodic{{{0,0},{10,0},{10,10},{0,10}},true};
    const auto seam=hits(periodic,Segment{{-1,-1},{1,1}},1);
    near(seam.hits.front().firstParameter,splineCurve(periodic)->FirstParameter());
    const auto selectedPeriodic=C::classifySegment(periodic,cuts(seam),{10,5});
    check(selectedPeriodic.status==C::Status::Success,"periodic spline classification failed");
    const auto pc=splineCurve(periodic);
    const auto periodicReverse=coefficients(pc,pc->FirstParameter(),pc->LastParameter(),true);
    const auto reverseSeam=hits(periodicReverse,Segment{{-1,-1},{1,1}},1);
    near(reverseSeam.hits.front().firstParameter,pc->LastParameter());
    const auto reversePeriodicSelection=C::classifySegment(periodicReverse,cuts(reverseSeam),{10,5});
    check(reversePeriodicSelection.status==C::Status::Success && reversePeriodicSelection.interval.start>reversePeriodicSelection.interval.end,"reversed periodic domain lost traversal");
    BSpline rational;rational.degree=2;rational.poles={{1,0},{1,1},{0,1}};rational.weights={1,std::sqrt(.5),1};
    rational.knots={0,1};rational.multiplicities={3,3};rational.first=.2;rational.last=.8;
    const auto rationalOriginal=rational;
    const auto rationalHits=hits(rational,Segment{{std::sqrt(.5),0},{std::sqrt(.5),2}},1);
    near(rationalHits.hits[0].point.x,std::sqrt(.5));near(rationalHits.hits[0].point.y,std::sqrt(.5));near(rationalHits.hits[0].firstParameter,.5);
    check(rational==rationalOriginal,"rational coefficients changed");
}
void testRefusals() {
    const Ellipse e{{0,0},5,3,0};
    const auto overlap=C::intersect(e,EllipticalArc{e,0,pi});
    check(overlap.status==C::Status::Overlap && overlap.hits.empty(),"coincident ellipse interval accepted");
    check(C::intersect(Segment{{0,0},{10,0}},Segment{{5,0},{15,0}}).status==C::Status::Overlap,"overlapping line accepted");
    const auto touch=hits(Segment{{0,0},{5,0}},Segment{{5,0},{10,0}},1);
    check(touch.hits[0].contact==C::Contact::Tangency,"collinear endpoint not classified as contact");
    const Spline s{{{0,0},{5,4},{10,0}}};
    check(C::intersect(s,s).status==C::Status::Overlap,"identical spline accepted");
    BSpline bow;bow.degree=1;bow.poles={{0,0},{2,2},{0,2},{2,0}};bow.weights={1,1,1,1};
    bow.knots={0,1,2,3};bow.multiplicities={2,1,1,2};bow.first=0;bow.last=3;
    const auto ambiguous=C::intersect(bow,Segment{{1,.5},{1,1.5}});
    check(ambiguous.status==C::Status::Ambiguous && ambiguous.hits.empty(),"distinct spline parameters at one point accepted");
    check(C::classifySegment(bow,{},Point{1,1}).status==C::Status::Ambiguous,"self-crossing spline pick guessed a parameter");
    const auto close=C::intersect(Segment{{0,0},{10,0}},Segment{{0,5.e-8},{10,5.e-8}});
    check(close.status==C::Status::Ambiguous && close.hits.empty(),"nearly overlapping supports accepted");
    const auto disjoint=hits(EllipticalArc{e,0,pi/3},EllipticalArc{e,pi,pi/3},0);
    check(disjoint.status==C::Status::Success,"disjoint intervals on common support refused");
    check(C::intersect(EllipticalArc{e,0,pi},reversedEllipticalArc(EllipticalArc{e,pi/2,pi})).status==C::Status::Overlap,"reversed elliptic overlap accepted");
    check(C::classifySegment(Segment{{0,0},{10,0}},{5,5+1.e-8},{2,0}).status==C::Status::Ambiguous,"unresolved cuts accepted");
    check(C::classifySegment(Segment{{0,0},{10,0}},{11},{2,0}).status==C::Status::Invalid,"out-of-domain cut accepted");
    check(C::intersect(e,e,0).status==C::Status::Invalid,"invalid tolerance accepted");
    check(C::intersect(Segment{{0,0},{0,0}},e).status==C::Status::Invalid,"invalid curve accepted");
    check(C::intersect(ConstructionPoint{{0,0}},e).status==C::Status::Unsupported,"point treated as curve");
    check(C::classifySegment(InfiniteLine{{0,0},{1,0}},{1,2},{1.5,0}).status==C::Status::Unsupported,"unbounded classification accepted");
    check(C::intersect(Ellipse{{1.e16,0},5,3,0},Segment{{-10,0},{10,0}}).status==C::Status::NumericalFailure,"unresolvable coordinates accepted");
    check(C::intersect(EllipticalArc{Ellipse{{0,0},1.e6,5.e5,0},1.e5,1},Segment{{-10,0},{10,0}}).status==C::Status::NumericalFailure,"unresolvable conic parameters accepted");
}
}
int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    try {std::cout<<"analytic\n";testAnalytic();std::cout<<"domains\n";testDomainsAndSeams();std::cout<<"splines\n";testSplines();std::cout<<"refusals\n";testRefusals();std::cout<<"Sketch curve operations: all cases passed\n";return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
