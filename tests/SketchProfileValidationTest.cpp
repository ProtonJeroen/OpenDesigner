#include "SketchProfileValidation.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <numbers>
#include <gp_Trsf.hxx>
#include <stdexcept>

using namespace Sketch;
namespace PV=SketchProfileValidation;
namespace {
constexpr double tolerance=1.e-7;
void check(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
Entity entity(QString id,Geometry geometry,QString owner="wp",Role role=Role::Profile) {
    Entity e;e.id=id;e.workplaneId=owner;e.role=role;e.geometry=std::move(geometry);return e;
}
std::vector<Entity> polygon(std::vector<Point> points,QString prefix="r",QString owner="wp") {
    std::vector<Entity> result;
    for(size_t i=0;i<points.size();++i)
        result.push_back(entity(prefix+QString::number(i),Segment{points[i],points[(i+1)%points.size()]},owner));
    return result;
}
std::vector<Entity> rectangle(double x=0,double y=0,QString prefix="r",QString owner="wp") {
    return polygon({{x,y},{x+10,y},{x+10,y+5},{x,y+5}},prefix,owner);
}
std::pair<Point,Point> endpoints(const PV::Curve& curve) {
    if(const auto* s=std::get_if<Segment>(&curve.geometry)) return {s->start,s->end};
    if(const auto* a=std::get_if<EllipticalArc>(&curve.geometry)) {const auto ends=ellipticalArcEndpoints(*a);return {ends[0],ends[1]};}
    const auto& a=std::get<Arc>(curve.geometry);
    const auto at=[&](double t) {return Point{a.center.x+a.radius*std::cos(t),a.center.y+a.radius*std::sin(t)};};
    return {at(a.startAngle),at(a.startAngle+a.sweepAngle)};
}
PV::Result valid(const std::vector<Entity>& entities,size_t contours=1,QString owner="wp") {
    const auto original=entities;const auto result=PV::validate(entities,owner);
    check(entities==original,"validation modified source values/IDs");
    if(result.status!=PV::Status::Valid) {
        if(!result.diagnostics.empty()) std::cerr<<result.diagnostics.front().message.toStdString()<<'\n';
        throw std::runtime_error("expected valid Profile");
    }
    check(result.contours.size()==contours && result.diagnostics.empty(),"wrong contour count/diagnostics");
    size_t count=0;
    for(const auto& contour:result.contours) {
        check(!contour.curves.empty(),"empty returned contour");
        check((contour.signedArea<0)==(contour.orientation==PV::Orientation::Clockwise),"wrong contour orientation");
        if(contour.parent) check(result.contours[*contour.parent].depth+1==contour.depth,"inconsistent nesting depth");
        for(size_t i=0;i<contour.curves.size();++i) {
            const auto& edge=contour.curves[i];++count;
            if(!std::holds_alternative<Circle>(edge.geometry) && !std::holds_alternative<Ellipse>(edge.geometry)) {
                const auto last=endpoints(edge).second,first=endpoints(contour.curves[(i+1)%contour.curves.size()]).first;
                check(std::hypot(last.x-first.x,last.y-first.y)<=tolerance,"returned contour not ordered/connected");
            }
            const auto source=std::find_if(entities.begin(),entities.end(),[&](const auto& e){return e.id==edge.entityId;});
            check(source!=entities.end() && source->role==Role::Profile && source->workplaneId==owner,"wrong source/role/Workplane in contour");
            if(const auto* s=std::get_if<Segment>(&edge.geometry)) {
                const auto& originalSegment=std::get<Segment>(source->geometry);
                check(*s==originalSegment || *s==Segment{originalSegment.end,originalSegment.start},"output silently snapped/repaired a segment");

            } else if(const auto* a=std::get_if<Arc>(&edge.geometry)) {
                const auto& originalArc=std::get<Arc>(source->geometry);
                check(*a==originalArc || *a==reversedArc(originalArc),"arc source parameters changed");
                check(edge.reversed?*a==reversedArc(originalArc):*a==originalArc,"arc reversal flag incorrect");
            } else if(const auto* e=std::get_if<Ellipse>(&edge.geometry)) {
                check(contour.curves.size()==1 && *e==std::get<Ellipse>(source->geometry),"ellipse support changed");
            } else if(const auto* a=std::get_if<EllipticalArc>(&edge.geometry)) {
                const auto& originalArc=std::get<EllipticalArc>(source->geometry);
                check(edge.reversed?*a==reversedEllipticalArc(originalArc):*a==originalArc,"elliptical arc source or traversal changed");
            } else check(contour.curves.size()==1 && std::get<Circle>(edge.geometry)==std::get<Circle>(source->geometry),"circle changed or mixed into polygon");
        }
    }
    check(!result.regions.empty(),"valid result lacks material regions");
    for(const auto& region:result.regions) {
        check(result.contours[region.outer].depth%2==0,"hole returned as material region");
        for(size_t hole:region.holes) check(result.contours[hole].parent==region.outer && result.contours[hole].depth%2==1,"region has indirect hole");
    }
    check(result.wireReadiness==PV::WireReadiness::NotCertified && !result.constructionDiagnostics.empty(),"wire readiness not diagnosed");
    check(count==size_t(std::count_if(entities.begin(),entities.end(),[&](const auto& e){return e.role==Role::Profile && e.workplaneId==owner;})),"source curve lost or repeated");
    return result;
}
void refused(const std::vector<Entity>& entities,PV::Status status,PV::Code code,QString owner="wp",double tol=tolerance) {
    const auto original=entities;const auto result=PV::validate(entities,owner,tol);
    check(entities==original,"refusal modified sources");
    if(result.status!=status || result.diagnostics.empty() || result.diagnostics.front().code!=code) {
        if(!result.diagnostics.empty()) std::cerr<<result.diagnostics.front().message.toStdString()<<'\n';
        std::cerr<<"expected code "<<int(code)<<" actual "<<(result.diagnostics.empty()?-1:int(result.diagnostics.front().code))<<" sources ";for(const auto& e:entities) std::cerr<<e.id.toStdString()<<',';std::cerr<<'\n';
        throw std::runtime_error("unexpected validation status/diagnostic");
    }
    check(result.contours.empty() && result.regions.empty(),"failure exposed partially usable contours/regions");
    check(!result.diagnostics.front().message.isEmpty(),"failure has no readable diagnostic");
}
void ordinaryContours() {
    const auto box=rectangle();valid(box);
    // All 24 orders and 16 direction combinations preserve ordinary validity.
    std::vector<size_t> order{0,1,2,3};
    do {
        for(unsigned mask=0;mask<16;++mask) {
            std::vector<Entity> input;
            for(size_t i:order) {
                auto e=box[i];if(mask&(1U<<i)) {auto& s=std::get<Segment>(e.geometry);std::swap(s.start,s.end);}
                input.push_back(e);
            }
            valid(input);
        }
    } while(std::next_permutation(order.begin(),order.end()));
    valid({entity("circle",Circle{{3,4},2})});
    valid(polygon({{0,0},{5,0},{10,0},{10,5},{5,2},{0,5}})); // Concave, collinear adjacent edges.
    valid(rectangle(1.e6,-1.e6)); // Area must not cancel at translated origins.
    auto hidden=box;for(auto& e:hidden) {e.visible=false;e.selectable=false;}valid(hidden);
    auto joined=box;std::get<Segment>(joined.back().geometry).end={0,0.5*tolerance};valid(joined);
    std::get<Segment>(joined.back().geometry).end={0,tolerance};valid(joined);
    std::get<Segment>(joined.back().geometry).end={0,2*tolerance};refused(joined,PV::Status::Invalid,PV::Code::OpenContour);
    std::get<Segment>(joined.back().geometry).end={-0.8*tolerance,0.8*tolerance};refused(joined,PV::Status::Invalid,PV::Code::AmbiguousConnection);
    auto open=box;open.pop_back();refused(open,PV::Status::Invalid,PV::Code::OpenContour);
    refused(polygon({{0,0},{10,10},{0,10},{10,0}}),PV::Status::Invalid,PV::Code::SelfIntersection);
    refused(polygon({{0,0},{10,0},{20,0}}),PV::Status::Invalid,PV::Code::Overlap);
    auto duplicate=box;duplicate.push_back(entity("copy",std::get<Segment>(box.front().geometry)));
    refused(duplicate,PV::Status::Invalid,PV::Code::DuplicateCurve);
    auto& reversed=std::get<Segment>(duplicate.back().geometry);std::swap(reversed.start,reversed.end);
    refused(duplicate,PV::Status::Invalid,PV::Code::DuplicateCurve);
    auto overlap=box;overlap.push_back(entity("overlap",Segment{{5,0},{15,0}}));refused(overlap,PV::Status::Invalid,PV::Code::Overlap);
    auto branch=box;branch.push_back(entity("branch",Segment{{5,0},{5,-3}}));refused(branch,PV::Status::Invalid,PV::Code::Branch);
    branch.back().geometry=Segment{{0,0},{-5,-3}};refused(branch,PV::Status::Invalid,PV::Code::Branch);
    refused({entity("a",Segment{{0,0},{5,0}}),entity("b",Segment{{0.75*tolerance,0},{3,3}}),
             entity("c",Segment{{1.5*tolerance,0},{-3,3}})},PV::Status::Invalid,PV::Code::AmbiguousConnection);
}
void ownershipAndUnsupported() {
    auto mixed=rectangle();mixed.back().role=Role::Construction;
    refused(mixed,PV::Status::Invalid,PV::Code::OpenContour);
    auto other=rectangle(0,0,"other","other-wp");mixed.insert(mixed.end(),other.begin(),other.end());
    refused(mixed,PV::Status::Invalid,PV::Code::OpenContour);valid(mixed,1,"other-wp");
    auto onlyConstruction=rectangle();for(auto& e:onlyConstruction) e.role=Role::Construction;
    refused(onlyConstruction,PV::Status::Invalid,PV::Code::EmptyProfile);
    auto box=rectangle();box.push_back(entity("ignored",Arc{},"wp",Role::Construction));
    box.push_back(entity("ignored-invalid",Segment{{0,0},{0,0}},"other-wp"));valid(box);
    auto ellipticHole=rectangle();ellipticHole.push_back(entity("ellipse",Ellipse{{5,2},1,.5,.3}));
    valid(ellipticHole,2);
    refused({entity("open-ellipse",EllipticalArc{})},PV::Status::Invalid,PV::Code::OpenContour);
    auto unsupported=rectangle();unsupported.push_back(entity("infinite",InfiniteLine{}));
    refused(unsupported,PV::Status::Invalid,PV::Code::InvalidGeometry);
    auto disjoint=rectangle();const auto second=rectangle(30,0,"second");disjoint.insert(disjoint.end(),second.begin(),second.end());
    disjoint.push_back(entity("separate-circle",Circle{{60,0},2}));valid(disjoint,3);
    auto nested=rectangle();auto inner=polygon({{2,1},{3,1},{3,2},{2,2}},"inner");nested.insert(nested.end(),inner.begin(),inner.end());
    valid(nested,2);
    nested=rectangle();nested.push_back(entity("hole",Circle{{3,2},0.5}));valid(nested,2);
    nested=rectangle();nested.push_back(entity("outer",Circle{{5,2},20}));valid(nested,2);
    valid({entity("a",Circle{{0,0},5}),entity("b",Circle{{0,0},2})},2);
    refused({entity("a",Circle{{0,0},2}),entity("b",Circle{{4,0},2})},PV::Status::Unsupported,PV::Code::ContourIntersection);
    refused({entity("a",Circle{{0,0},2}),entity("b",Circle{{3,0},2})},PV::Status::Unsupported,PV::Code::ContourIntersection);
    refused({entity("a",Circle{{0,0},2}),entity("b",Circle{{0,0},2})},PV::Status::Invalid,PV::Code::DuplicateCurve);
    auto crossing=rectangle();crossing.push_back(entity("circle",Circle{{0,0},2}));refused(crossing,PV::Status::Unsupported,PV::Code::ContourIntersection);
}
constexpr double pi=std::numbers::pi;
Point at(const Arc& a,double t) {return {a.center.x+a.radius*std::cos(t),a.center.y+a.radius*std::sin(t)};}
void circularContoursAndRegions() {
    const std::vector<Entity> d{entity("a",Arc{{0,0},5,0,pi}),entity("b",Segment{{-5,0},{5,0}})};
    auto result=valid(d);check(std::abs(std::abs(result.contours[0].signedArea)-12.5*pi)<1.e-10,"wrong semicircle area");
    // Every source reversal/order combination, including complementary semicircles.
    for(const auto& source:std::vector<std::vector<Entity>>{d,
        {entity("a",Arc{{0,0},5,0,pi}),entity("b",Arc{{0,0},5,pi,pi})},
        {entity("a",Arc{{0,0},5,1.5*pi,pi}),entity("b",Arc{{0,0},5,.5*pi,pi})}}) {
        for(unsigned mask=0;mask<4;++mask) {
            auto input=source;
            for(size_t i=0;i<2;++i) if(mask&(1U<<i)) {
                if(auto* a=std::get_if<Arc>(&input[i].geometry)) *a=reversedArc(*a);
                else {auto& l=std::get<Segment>(input[i].geometry);std::swap(l.start,l.end);}
            }
            valid(input);std::reverse(input.begin(),input.end());valid(input);
        }
    }
    // Capsule: tangent connections within one simple contour are valid.
    auto capsule=std::vector<Entity>{entity("a",Segment{{0,-2},{6,-2}}),entity("b",Arc{{6,0},2,-pi/2,pi}),
        entity("c",Segment{{6,2},{0,2}}),entity("d",Arc{{0,0},2,pi/2,pi})};
    result=valid(capsule);check(std::abs(std::abs(result.contours[0].signedArea)-(24+4*pi))<1.e-10,"wrong mixed area");
    capsule.push_back(entity("hole",Circle{{3,0},.5}));
    result=valid(capsule,2);check(result.regions.size()==1 && result.regions[0].holes.size()==1,"mixed arc containment failed");
    auto outer=rectangle();outer.push_back(entity("hole",Circle{{5,2.5},2}));
    outer.push_back(entity("island",Circle{{5,2.5},.5}));
    result=valid(outer,3);
    check(result.regions.size()==2,"island not a separate material region");
    size_t holes=0,maxDepth=0;for(const auto& region:result.regions) holes+=region.holes.size();
    for(const auto& contour:result.contours) maxDepth=std::max(maxDepth,contour.depth);
    check(holes==1 && maxDepth==2,"even-odd hierarchy incorrect");
    auto two=rectangle();two.push_back(entity("hole1",Circle{{5,2},1}));
    const auto second=rectangle(30,0,"second");two.insert(two.end(),second.begin(),second.end());
    two.push_back(entity("hole2",Circle{{35,2},1}));result=valid(two,4);
    check(result.regions.size()==2 && result.regions[0].holes.size()==1 && result.regions[1].holes.size()==1,"disconnected regions lost direct holes");
    auto annulus=std::vector<Entity>{entity("outer",Circle{{0,0},5}),entity("hole",Circle{{0,0},2})};
    result=valid(annulus,2);check(result.regions.size()==1 && result.regions[0].holes.size()==1,"annulus classification failed");
    // Workplane frames change world geometry but never local topology/IDs.
    const gp_Ax3 frames[]{gp_Ax3(gp_Pnt(20,-40,70),gp_Dir(0,0,1)),gp_Ax3(gp_Pnt(5,9,2),gp_Dir(1,2,3),gp_Dir(2,-1,0))};
    for(const auto& frame:frames) {
        auto moved=d;for(auto& e:moved) e.workplaneId="rotated-wp";
        const auto transformed=valid(moved,1,"rotated-wp");
        check(std::abs(transformed.contours[0].signedArea-valid(d).contours[0].signedArea)<1.e-12,"Workplane changed local topology");
        const auto point=worldPoint({5,0},frame);check(std::abs(point.Distance(frame.Location())-5)<1.e-12,"Workplane frame test incorrect");
    }
    auto tolerant=rectangle();std::get<Segment>(tolerant.back().geometry).end={0,.5*tolerance};
    result=valid(tolerant);check(result.contours[0].maximumEndpointGap>0,"gap lost");
    check(std::any_of(result.constructionDiagnostics.begin(),result.constructionDiagnostics.end(),[](const auto& d){return d.code==PV::Code::TolerantClosure;}),"tolerant closure not exposed");
    // An open arc alone, source-preserving and atomic on every failure.
    refused({d.front()},PV::Status::Invalid,PV::Code::OpenContour);
    refused({entity("a",Arc{{0,0},5,0,pi}),entity("b",Arc{{0,0},5,pi/2,pi})},PV::Status::Invalid,PV::Code::Overlap);
    refused({d.front(),entity("copy",reversedArc(std::get<Arc>(d.front().geometry)))},PV::Status::Invalid,PV::Code::DuplicateCurve);
    refused({entity("a",Arc{{0,0},5,0,pi}),entity("b",Arc{{3,0},5,0,pi})},PV::Status::Invalid,PV::Code::SelfIntersection);
    auto touch=rectangle();touch.push_back(entity("touch",Circle{{5,2},2}));
    refused(touch,PV::Status::Unsupported,PV::Code::ContourIntersection);
    auto duplicateLoop=d;auto copy=d;for(auto& e:copy)e.id+="-copy";duplicateLoop.insert(duplicateLoop.end(),copy.begin(),copy.end());
    refused(duplicateLoop,PV::Status::Invalid,PV::Code::DuplicateCurve);
    auto branch=d;branch.push_back(entity("branch",Segment{{5,0},{8,0}}));refused(branch,PV::Status::Invalid,PV::Code::Branch);
    auto gap=d;std::get<Segment>(gap[1].geometry).end.x-=2*tolerance;refused(gap,PV::Status::Invalid,PV::Code::OpenContour);
    auto crossed=d;crossed.push_back(entity("ellipse",Ellipse{}));refused(crossed,PV::Status::Unsupported,PV::Code::ContourIntersection);
    // Major/minor routes and seam crossings must retain source sweep and area.
    for(double sweep:{1.5*pi,-1.5*pi,.5*pi,-.5*pi}) {
        const Arc arc{{2,3},4,1.8*pi,sweep};
        const auto input=std::vector<Entity>{entity("arc",arc),entity("closing",Segment{at(arc,arc.startAngle+arc.sweepAngle),at(arc,arc.startAngle)})};
        const auto route=valid(input);
        check(std::abs(std::abs(route.contours[0].signedArea)-8*std::abs(sweep-std::sin(sweep)))<1.e-10,"arc route/area changed");
    }
    // Arc-only outer with an arc-only hole exercises containment in both directions.
    std::vector<Entity> arcAnnulus{entity("outer1",Arc{{0,0},5,0,pi}),entity("outer2",Arc{{0,0},5,pi,pi}),
        entity("inner1",Arc{{0,0},2,pi,-pi}),entity("inner2",Arc{{0,0},2,0,-pi})};
    result=valid(arcAnnulus,2);check(result.regions.size()==1 && result.regions[0].holes.size()==1,"arc-only annulus failed");
    // A foreign contour tangent to an arc interior must not become a valid hole.
    auto touchingArc=capsule;touchingArc.back().geometry=Circle{{7,0},1};
    refused(touchingArc,PV::Status::Unsupported,PV::Code::ContourIntersection);
    // No partial result when a valid region precedes an unrelated open chain.
    auto atomic=rectangle();atomic.push_back(entity("z-open",Arc{{50,0},5,0,pi}));
    refused(atomic,PV::Status::Invalid,PV::Code::OpenContour);
    refused({entity("small",Arc{{0,0},.75*tolerance,0,pi})},PV::Status::Invalid,PV::Code::DegenerateCurve);
    // Containment is local and translation-stable, including ray/vertex alignment.
    auto shifted=arcAnnulus;
    for(auto& e:shifted) {auto& a=std::get<Arc>(e.geometry);a.center={1.e6,-1.e6};}
    valid(shifted,2);
    auto zeroArea=std::vector<Entity>{entity("a",Arc{{0,0},5,0,1.e-8}),entity("b",Segment{at(Arc{{0,0},5,0,1.e-8},1.e-8),{5,0}})};
    refused(zeroArea,PV::Status::Invalid,PV::Code::DegenerateCurve);
}

void malformed() {
    refused({},PV::Status::Invalid,PV::Code::EmptyProfile);
    refused(rectangle(),PV::Status::Invalid,PV::Code::InvalidInput,"");
    for(double tol:{0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        refused(rectangle(),PV::Status::Invalid,PV::Code::InvalidInput,"wp",tol);
    refused({entity("point",Segment{{0,0},{0,0}})},PV::Status::Invalid,PV::Code::InvalidGeometry);
    refused({entity("short",Segment{{0,0},{tolerance/2,0}})},PV::Status::Invalid,PV::Code::DegenerateCurve);
    refused({entity("tiny",Circle{{0,0},tolerance/2})},PV::Status::Invalid,PV::Code::DegenerateCurve);
    refused({entity("inf",Circle{{0,0},std::numeric_limits<double>::infinity()})},PV::Status::Invalid,PV::Code::InvalidGeometry);
    const auto nan=entity("nan",Circle{{0,0},std::numeric_limits<double>::quiet_NaN()});
    const auto invalid=PV::validate({nan},"wp"); // NaN does not compare equal to a copy of itself.
    check(invalid.status==PV::Status::Invalid && invalid.contours.empty() && invalid.diagnostics.front().code==PV::Code::InvalidGeometry
        && std::isnan(std::get<Circle>(nan.geometry).radius),"non-finite source was accepted/changed");
    refused({entity("infinite",InfiniteLine{})},PV::Status::Invalid,PV::Code::InvalidGeometry);
    refused({entity("point",ConstructionPoint{})},PV::Status::Invalid,PV::Code::InvalidGeometry);
    auto repeatedId=rectangle();repeatedId.back().id=repeatedId.front().id;refused(repeatedId,PV::Status::Invalid,PV::Code::DuplicateIdentity);
    refused(rectangle(1.e12,1.e12),PV::Status::Unsupported,PV::Code::NumericalFailure);
}
}
int main() {
    try {ordinaryContours();ownershipAndUnsupported();circularContoursAndRegions();malformed();std::cout<<"Closed Profile validation tests passed\n";return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
