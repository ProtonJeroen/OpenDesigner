#include "SketchProfileValidation.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
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
        for(size_t i=0;i<contour.curves.size();++i) {
            const auto& edge=contour.curves[i];++count;
            const auto source=std::find_if(entities.begin(),entities.end(),[&](const auto& e){return e.id==edge.entityId;});
            check(source!=entities.end() && source->role==Role::Profile && source->workplaneId==owner,"wrong source/role/Workplane in contour");
            if(const auto* s=std::get_if<Segment>(&edge.geometry)) {
                const auto& originalSegment=std::get<Segment>(source->geometry);
                check(*s==originalSegment || *s==Segment{originalSegment.end,originalSegment.start},"output silently snapped/repaired a segment");
                const auto& next=std::get<Segment>(contour.curves[(i+1)%contour.curves.size()].geometry);
                check(std::hypot(s->end.x-next.start.x,s->end.y-next.start.y)<=tolerance,"returned contour is not ordered/connected");
            } else check(contour.curves.size()==1 && std::get<Circle>(edge.geometry)==std::get<Circle>(source->geometry),"circle changed or mixed into polygon");
        }
    }
    check(count==size_t(std::count_if(entities.begin(),entities.end(),[&](const auto& e){return e.role==Role::Profile && e.workplaneId==owner;})),"source curve lost or repeated");
    return result;
}
void refused(const std::vector<Entity>& entities,PV::Status status,PV::Code code,QString owner="wp",double tol=tolerance) {
    const auto original=entities;const auto result=PV::validate(entities,owner,tol);
    check(entities==original,"refusal modified sources");
    if(result.status!=status || result.diagnostics.empty() || result.diagnostics.front().code!=code) {
        if(!result.diagnostics.empty()) std::cerr<<result.diagnostics.front().message.toStdString()<<'\n';
        throw std::runtime_error("unexpected validation status/diagnostic");
    }
    check(result.contours.empty(),"failure exposed partially usable contours");
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
    for(const Geometry& g:std::vector<Geometry>{Arc{},Ellipse{},EllipticalArc{}}) {
        auto input=rectangle();input.push_back(entity("unsupported",g));
        refused(input,PV::Status::Unsupported,PV::Code::UnsupportedCurve);
    }
    auto disjoint=rectangle();const auto second=rectangle(30,0,"second");disjoint.insert(disjoint.end(),second.begin(),second.end());
    disjoint.push_back(entity("separate-circle",Circle{{60,0},2}));valid(disjoint,3);
    auto nested=rectangle();auto inner=polygon({{2,1},{3,1},{3,2},{2,2}},"inner");nested.insert(nested.end(),inner.begin(),inner.end());
    refused(nested,PV::Status::Unsupported,PV::Code::NestedContours);
    nested=rectangle();nested.push_back(entity("hole",Circle{{3,2},0.5}));refused(nested,PV::Status::Unsupported,PV::Code::NestedContours);
    nested=rectangle();nested.push_back(entity("outer",Circle{{5,2},20}));refused(nested,PV::Status::Unsupported,PV::Code::NestedContours);
    refused({entity("a",Circle{{0,0},5}),entity("b",Circle{{0,0},2})},PV::Status::Unsupported,PV::Code::NestedContours);
    refused({entity("a",Circle{{0,0},2}),entity("b",Circle{{4,0},2})},PV::Status::Unsupported,PV::Code::ContourIntersection);
    refused({entity("a",Circle{{0,0},2}),entity("b",Circle{{3,0},2})},PV::Status::Unsupported,PV::Code::ContourIntersection);
    refused({entity("a",Circle{{0,0},2}),entity("b",Circle{{0,0},2})},PV::Status::Invalid,PV::Code::DuplicateCurve);
    auto crossing=rectangle();crossing.push_back(entity("circle",Circle{{0,0},2}));refused(crossing,PV::Status::Unsupported,PV::Code::ContourIntersection);
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
    try {ordinaryContours();ownershipAndUnsupported();malformed();std::cout<<"Closed Profile validation tests passed\n";return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
