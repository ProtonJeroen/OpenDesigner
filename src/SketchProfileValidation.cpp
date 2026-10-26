#include "SketchProfileValidation.h"
#include "SketchSnapping.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace SketchProfileValidation {
namespace {
using namespace Sketch;
long double distance(Point a,Point b) {
    return std::hypot(static_cast<long double>(a.x)-b.x,static_cast<long double>(a.y)-b.y);
}
bool before(Point a,Point b) {return a.x<b.x || (a.x==b.x && a.y<b.y);}
Result failure(Status status,Code code,const QString& message,QStringList ids={},std::optional<Point> point={}) {
    return {status,{},{{code,message,std::move(ids),point}}};
}
// The existing intersection helpers use doubles. Refuse coordinates whose
// representational noise consumes the connection tolerance instead of guessing.
bool reliable(double value,double tolerance) {
    const double magnitude=std::abs(value);
    return std::isfinite(value) && magnitude<std::sqrt(std::numeric_limits<double>::max())/16
        && static_cast<long double>(std::nextafter(magnitude,
        std::numeric_limits<double>::infinity()))-magnitude<=static_cast<long double>(tolerance)/16;
}
bool reliable(Point p,double tolerance) {return reliable(p.x,tolerance) && reliable(p.y,tolerance);}
bool onSegment(const Geometry& segment,Point point,double tolerance) {
    const auto nearest=Sketch::nearestPoint(segment,point);
    return nearest && distance(*nearest,point)<=tolerance;
}
Point representative(const Contour& contour) {
    const auto& g=contour.curves.front().geometry;
    if(const auto* s=std::get_if<Segment>(&g)) return s->start;
    const auto& c=std::get<Circle>(g);return {c.center.x+c.radius,c.center.y};
}
bool inside(Point p,const Contour& contour) {
    if(const auto* c=std::get_if<Circle>(&contour.curves.front().geometry)) return distance(p,c->center)<c->radius;
    bool result=false;
    for(const auto& edge:contour.curves) {
        const auto& s=std::get<Segment>(edge.geometry);
        if((s.start.y>p.y)==(s.end.y>p.y)) continue;
        const long double x=static_cast<long double>(s.start.x)
            +(static_cast<long double>(p.y)-s.start.y)*(static_cast<long double>(s.end.x)-s.start.x)
                /(static_cast<long double>(s.end.y)-s.start.y);
        if(x>p.x) result=!result;
    }
    return result;
}
}
Result validate(const std::vector<Sketch::Entity>& entities,const QString& workplaneId,double tolerance) {
    using namespace Sketch;
    if(workplaneId.isEmpty() || !std::isfinite(tolerance) || tolerance<=0)
        return failure(Status::Invalid,Code::InvalidInput,"Specify a Workplane ID and a finite positive geometry tolerance.");
    std::vector<const Entity*> sources;
    for(const auto& e:entities) if(e.workplaneId==workplaneId && e.role==Role::Profile) sources.push_back(&e);
    if(sources.empty()) return failure(Status::Invalid,Code::EmptyProfile,"No Profile geometry exists on this Workplane.");
    std::sort(sources.begin(),sources.end(),[](const auto* a,const auto* b){return a->id<b->id;});
    std::vector<const Entity*> segments,circles;std::vector<size_t> segmentIndex(sources.size());
    for(size_t i=0;i<sources.size();++i) {
        const auto& e=*sources[i];QString error;
        if(!Sketch::valid(e,error)) return failure(Status::Invalid,Code::InvalidGeometry,error,{e.id});
        if(i && sources[i-1]->id==e.id) return failure(Status::Invalid,Code::DuplicateIdentity,"Profile entities must have distinct stable IDs.",{e.id});
        if(const auto* s=std::get_if<Segment>(&e.geometry)) {
            if(distance(s->start,s->end)<=tolerance)
                return failure(Status::Invalid,Code::DegenerateCurve,"A Profile segment collapses within the geometry tolerance.",{e.id});
            if(!reliable(s->start,tolerance) || !reliable(s->end,tolerance))
                return failure(Status::Unsupported,Code::NumericalFailure,"Profile coordinates cannot resolve the requested tolerance reliably.",{e.id});
            segmentIndex[i]=segments.size();segments.push_back(&e);
        } else if(const auto* c=std::get_if<Circle>(&e.geometry)) {
            if(c->radius<=tolerance) return failure(Status::Invalid,Code::DegenerateCurve,"A Profile circle is smaller than the geometry tolerance.",{e.id});
            if(!reliable(c->center,tolerance) || !reliable(c->radius,tolerance)
                || !reliable(Point{c->center.x+c->radius,c->center.y+c->radius},tolerance)
                || !reliable(Point{c->center.x-c->radius,c->center.y-c->radius},tolerance))
                return failure(Status::Unsupported,Code::NumericalFailure,"Circle coordinates cannot resolve the requested tolerance reliably.",{e.id});
            circles.push_back(&e);
        } else return failure(Status::Unsupported,Code::UnsupportedCurve,
            "Phase 6C.1 supports only straight Profile segments and standalone full circles.",{e.id});
    }
    // Connected components of endpoint proximity, with an explicit diameter
    // check: A~B and B~C must never silently bridge an A-C gap > tolerance.
    std::vector<Point> endpoints;
    for(const auto* e:segments) {const auto& s=std::get<Segment>(e->geometry);endpoints.push_back(s.start);endpoints.push_back(s.end);}
    std::vector<size_t> roots(endpoints.size());std::iota(roots.begin(),roots.end(),0);
    const auto root=[&](size_t n) {while(roots[n]!=n) n=roots[n];return n;};
    for(size_t i=0;i<endpoints.size();++i) for(size_t j=i+1;j<endpoints.size();++j)
        if(distance(endpoints[i],endpoints[j])<=tolerance) roots[root(j)]=root(i);
    for(size_t i=0;i<roots.size();++i) roots[i]=root(i);
    for(size_t i=0;i<endpoints.size();++i) for(size_t j=i+1;j<endpoints.size();++j)
        if(roots[i]==roots[j] && distance(endpoints[i],endpoints[j])>tolerance)
            return failure(Status::Invalid,Code::AmbiguousConnection,"An endpoint cluster exceeds the geometry tolerance; no implicit gap repair is allowed.",
                {segments[i/2]->id,segments[j/2]->id},endpoints[i]);
    for(size_t i=0;i<sources.size();++i) for(size_t j=i+1;j<sources.size();++j) {
        const auto& a=*sources[i];const auto& b=*sources[j];const QStringList ids{a.id,b.id};
        const auto* sa=std::get_if<Segment>(&a.geometry);const auto* sb=std::get_if<Segment>(&b.geometry);
        if(sa && sb) {
            if((distance(sa->start,sb->start)<=tolerance && distance(sa->end,sb->end)<=tolerance)
                || (distance(sa->start,sb->end)<=tolerance && distance(sa->end,sb->start)<=tolerance))
                return failure(Status::Invalid,Code::DuplicateCurve,"Duplicate Profile segments, including reversed copies, are not allowed.",ids);
        }
        const auto hits=Sketch::intersect(a.geometry,b.geometry,tolerance);
        if(hits.kind==IntersectionKind::Coincident)
            return failure(Status::Invalid,sa && sb?Code::Overlap:Code::DuplicateCurve,"Profile curves overlap or duplicate the same boundary.",ids);
        if(hits.kind==IntersectionKind::Unsupported || hits.kind==IntersectionKind::NumericalFailure)
            return failure(Status::Unsupported,Code::NumericalFailure,"Profile intersections could not be certified.",ids);
        if(sa && sb) {
            const size_t ia=segmentIndex[i],ib=segmentIndex[j];
            const auto shared=[&](size_t x,size_t y){return roots[2*ia+x]==roots[2*ib+y];};
            for(size_t x=0;x<2;++x) {
                if(onSegment(b.geometry,endpoints[2*ia+x],tolerance) && !shared(x,0) && !shared(x,1))
                    return failure(Status::Invalid,Code::Branch,"A Profile endpoint touches the interior of another segment (T-branch).",ids,endpoints[2*ia+x]);
                if(onSegment(a.geometry,endpoints[2*ib+x],tolerance) && !shared(0,x) && !shared(1,x))
                    return failure(Status::Invalid,Code::Branch,"A Profile endpoint touches the interior of another segment (T-branch).",ids,endpoints[2*ib+x]);
            }
            for(const auto p:hits.points) {
                if(!onSegment(a.geometry,p,tolerance) || !onSegment(b.geometry,p,tolerance)) continue;
                bool joint=false;
                for(size_t x=0;x<2;++x) for(size_t y=0;y<2;++y)
                    if(shared(x,y) && distance(p,endpoints[2*ia+x])<=tolerance && distance(p,endpoints[2*ib+y])<=tolerance) joint=true;
                if(!joint) {
                    const bool nearA=distance(p,sa->start)<=tolerance || distance(p,sa->end)<=tolerance;
                    const bool nearB=distance(p,sb->start)<=tolerance || distance(p,sb->end)<=tolerance;
                    if(nearA && nearB) return failure(Status::Invalid,Code::AmbiguousConnection,
                        "Near-endpoint contact does not resolve to a shared endpoint within tolerance.",ids,p);
                    return failure(Status::Invalid,Code::SelfIntersection,"Profile segments cross away from a shared endpoint.",ids,p);
                }
            }
        } else if(!hits.points.empty()) return failure(Status::Unsupported,Code::ContourIntersection,
            "Touching or intersecting circle contours require phase 6C.2.",ids,hits.points.front());
    }
    std::vector<std::vector<size_t>> adjacent(endpoints.size());
    for(size_t i=0;i<segments.size();++i) {
        if(roots[2*i]==roots[2*i+1]) return failure(Status::Invalid,Code::DegenerateCurve,"A Profile edge collapses to one endpoint node.",{segments[i]->id});
        adjacent[roots[2*i]].push_back(i);adjacent[roots[2*i+1]].push_back(i);
    }
    for(size_t i=0;i<adjacent.size();++i) if(adjacent[i].size()>2) {
        QStringList ids;for(auto edge:adjacent[i]) ids<<segments[edge]->id;
        return failure(Status::Invalid,Code::Branch,"More than two Profile edges meet at one endpoint.",ids,endpoints[i]);
    }
    for(size_t i=0;i<adjacent.size();++i) if(adjacent[i].size()==1)
        return failure(Status::Invalid,Code::OpenContour,"A Profile endpoint is unconnected within the geometry tolerance.",{segments[adjacent[i][0]]->id},endpoints[i]);
    std::vector<Contour> contours;std::vector<bool> visited(segments.size(),false);
    for(size_t first=0;first<segments.size();++first) if(!visited[first]) {
        Contour contour;const auto& initial=std::get<Segment>(segments[first]->geometry);
        const auto start=roots[2*first+(before(initial.end,initial.start)?1:0)];size_t node=start,edge=first;
        do {
            if(visited[edge]) return failure(Status::Invalid,Code::Branch,"Profile traversal returned to an already used edge.",{segments[edge]->id});
            visited[edge]=true;auto s=std::get<Segment>(segments[edge]->geometry);
            if(roots[2*edge]!=node) std::swap(s.start,s.end);
            contour.curves.push_back({segments[edge]->id,s});node=roots[roots[2*edge]==node?2*edge+1:2*edge];
            if(node!=start) edge=adjacent[node][0]==edge?adjacent[node][1]:adjacent[node][0];
        } while(node!=start);
        // Translation-relative shoelace avoids cancellation at remote origins.
        const auto origin=std::get<Segment>(contour.curves.front().geometry).start;
        long double twiceArea=0,perimeter=0;
        for(size_t i=0;i<contour.curves.size();++i) {
            const auto& s=std::get<Segment>(contour.curves[i].geometry);
            const auto q=std::get<Segment>(contour.curves[(i+1)%contour.curves.size()].geometry).start;
            twiceArea+=(static_cast<long double>(s.start.x)-origin.x)*(static_cast<long double>(q.y)-origin.y)
                -(static_cast<long double>(s.start.y)-origin.y)*(static_cast<long double>(q.x)-origin.x);
            perimeter+=distance(s.start,s.end);
        }
        if(!std::isfinite(twiceArea) || !std::isfinite(perimeter))
            return failure(Status::Unsupported,Code::NumericalFailure,"The Profile area could not be resolved numerically.",{segments[first]->id});
        if(contour.curves.size()<3 || std::abs(twiceArea)<=static_cast<long double>(tolerance)*perimeter)
            return failure(Status::Invalid,Code::DegenerateCurve,"The closed Profile has no resolvable area.",{segments[first]->id});
        contours.push_back(std::move(contour));
    }
    for(const auto* e:circles) contours.push_back({{{e->id,std::get<Circle>(e->geometry)}}});
    // With certified disjoint boundaries, one analytical point establishes
    // containment. No hole classification, approximation or profile repair.
    for(size_t i=0;i<contours.size();++i) for(size_t j=i+1;j<contours.size();++j)
        if(inside(representative(contours[i]),contours[j]) || inside(representative(contours[j]),contours[i]))
            return failure(Status::Unsupported,Code::NestedContours,"Nested contours and holes require phase 6C.2.",
                {contours[i].curves.front().entityId,contours[j].curves.front().entityId});
    return {Status::Valid,std::move(contours),{}};
}
}
