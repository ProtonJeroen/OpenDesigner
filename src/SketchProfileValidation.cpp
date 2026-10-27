#include "SketchProfileValidation.h"
#include "SketchSnapping.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <numbers>

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
bool onCurve(const Geometry& curve,Point point,double tolerance) {
    if(const auto* arc=std::get_if<Arc>(&curve)) {
        const auto endpoint=[&](double t) {return Point{arc->center.x+arc->radius*std::cos(t),arc->center.y+arc->radius*std::sin(t)};};
        if(distance(point,endpoint(arc->startAngle))<=tolerance || distance(point,endpoint(arc->startAngle+arc->sweepAngle))<=tolerance) return true;
        return Sketch::contains(curve,point,tolerance);
    }
    const auto nearest=Sketch::nearestPoint(curve,point);
    return nearest && distance(*nearest,point)<=tolerance;
}
constexpr double pi=std::numbers::pi;
Point arcPoint(const Arc& a,double t) {return {a.center.x+a.radius*std::cos(t),a.center.y+a.radius*std::sin(t)};}
std::pair<Point,Point> ends(const Geometry& g) {
    if(const auto* s=std::get_if<Segment>(&g)) return {s->start,s->end};
    const auto& a=std::get<Arc>(g);return {arcPoint(a,a.startAngle),arcPoint(a,a.startAngle+a.sweepAngle)};
}
Geometry geometry(const Curve& c) {return std::visit([](const auto& g)->Geometry{return g;},c.geometry);}
Point representative(const Contour& contour) {
    const auto g=geometry(contour.curves.front());
    if(const auto* c=std::get_if<Circle>(&g)) return {c->center.x+c->radius,c->center.y};
    return ends(g).first;
}
// Exact analytic ray intersections, never curve sampling. Choose a ray which
// avoids vertices and support-circle tangencies, so no endpoint parity guessing
// is needed. If no numerically separated ray exists, containment is unsupported.
std::optional<bool> inside(Point p,const Contour& contour,double tolerance) {
    if(const auto* c=std::get_if<Circle>(&contour.curves.front().geometry)) {
        const auto d=distance(p,c->center);
        if(std::abs(d-c->radius)<=tolerance) return {};
        return d<c->radius;
    }
    const size_t attempts=8*contour.curves.size()+16;
    for(size_t trial=0;trial<attempts;++trial) {
        const double angle=pi*double(trial)/double(attempts),co=std::cos(angle),si=std::sin(angle);
        const auto local=[&](Point q) {const long double x=static_cast<long double>(q.x)-p.x,y=static_cast<long double>(q.y)-p.y;
            return Point{double(x*co+y*si),double(y*co-x*si)};};
        bool ambiguous=false,result=false;
        for(const auto& curve:contour.curves) {
            const auto g=geometry(curve);const auto [start,end]=ends(g);const auto a=local(start),b=local(end);
            if(std::abs(a.y)<=tolerance || std::abs(b.y)<=tolerance) {ambiguous=true;break;}
            if(std::holds_alternative<Segment>(g)) {
                if((a.y>0)==(b.y>0)) continue;
                const long double x=static_cast<long double>(a.x)-static_cast<long double>(a.y)*(b.x-a.x)/(static_cast<long double>(b.y)-a.y);
                if(std::abs(x)<=tolerance) {ambiguous=true;break;}
                if(x>0) result=!result;
            } else {
                const auto& arc=std::get<Arc>(g);const auto center=local(arc.center);
                if(std::abs(std::abs(center.y)-arc.radius)<=tolerance) {ambiguous=true;break;}
                if(std::abs(center.y)>arc.radius) continue;
                const long double r=arc.radius,y=center.y;
                const double dx=double(std::sqrt((r-y)*(r+y)));
                for(double x:{center.x-dx,center.x+dx}) {
                    const Point hit{p.x+x*co,p.y+x*si};
                    if(!Sketch::contains(g,hit,tolerance)) continue;
                    if(std::abs(x)<=tolerance) {ambiguous=true;break;}
                    if(x>0) result=!result;
                }
                if(ambiguous) break;
            }
        }
        if(!ambiguous) return result;
    }
    return {};
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
    std::vector<const Entity*> finiteCurves,circles;std::vector<size_t> curveIndex(sources.size());
    for(size_t i=0;i<sources.size();++i) {
        const auto& e=*sources[i];QString error;
        if(!Sketch::valid(e,error)) return failure(Status::Invalid,Code::InvalidGeometry,error,{e.id});
        if(i && sources[i-1]->id==e.id) return failure(Status::Invalid,Code::DuplicateIdentity,"Profile entities must have distinct stable IDs.",{e.id});
        if(std::holds_alternative<Segment>(e.geometry) || std::holds_alternative<Arc>(e.geometry)) {
            const auto [start,end]=ends(e.geometry);
            const auto* arc=std::get_if<Arc>(&e.geometry);
            if(distance(start,end)<=tolerance || (arc && (arc->radius<=tolerance || arc->radius*std::abs(arc->sweepAngle)<=tolerance)))
                return failure(Status::Invalid,Code::DegenerateCurve,"A finite Profile curve collapses within the geometry tolerance.",{e.id});
            if(!reliable(start,tolerance) || !reliable(end,tolerance)
                || (arc && (!reliable(arc->center,tolerance) || !reliable(arc->radius,tolerance)
                || !reliable(arc->startAngle,tolerance/std::max(arc->radius,1.0))
                || !reliable(Point{arc->center.x+arc->radius,arc->center.y+arc->radius},tolerance)
                || !reliable(Point{arc->center.x-arc->radius,arc->center.y-arc->radius},tolerance))))
                return failure(Status::Unsupported,Code::NumericalFailure,"Profile coordinates cannot resolve the requested tolerance reliably.",{e.id});
            curveIndex[i]=finiteCurves.size();finiteCurves.push_back(&e);
        } else if(const auto* c=std::get_if<Circle>(&e.geometry)) {
            if(c->radius<=tolerance) return failure(Status::Invalid,Code::DegenerateCurve,"A Profile circle is smaller than the geometry tolerance.",{e.id});
            if(!reliable(c->center,tolerance) || !reliable(c->radius,tolerance)
                || !reliable(Point{c->center.x+c->radius,c->center.y+c->radius},tolerance)
                || !reliable(Point{c->center.x-c->radius,c->center.y-c->radius},tolerance))
                return failure(Status::Unsupported,Code::NumericalFailure,"Circle coordinates cannot resolve the requested tolerance reliably.",{e.id});
            circles.push_back(&e);
        } else return failure(Status::Unsupported,Code::UnsupportedCurve,
            "Only Profile segments, circular arcs and standalone full circles are supported.",{e.id});
    }
    // Connected components of endpoint proximity, with an explicit diameter
    // check: A~B and B~C must never silently bridge an A-C gap > tolerance.
    std::vector<Point> endpoints;
    for(const auto* e:finiteCurves) {const auto [a,b]=ends(e->geometry);endpoints.push_back(a);endpoints.push_back(b);}
    std::vector<size_t> roots(endpoints.size());std::iota(roots.begin(),roots.end(),0);
    const auto root=[&](size_t n) {while(roots[n]!=n) n=roots[n];return n;};
    for(size_t i=0;i<endpoints.size();++i) for(size_t j=i+1;j<endpoints.size();++j)
        if(distance(endpoints[i],endpoints[j])<=tolerance) roots[root(j)]=root(i);
    for(size_t i=0;i<roots.size();++i) roots[i]=root(i);
    for(size_t i=0;i<endpoints.size();++i) for(size_t j=i+1;j<endpoints.size();++j)
        if(roots[i]==roots[j] && distance(endpoints[i],endpoints[j])>tolerance)
            return failure(Status::Invalid,Code::AmbiguousConnection,"An endpoint cluster exceeds the geometry tolerance; no implicit gap repair is allowed.",
                {finiteCurves[i/2]->id,finiteCurves[j/2]->id},endpoints[i]);
    for(size_t i=0;i<sources.size();++i) for(size_t j=i+1;j<sources.size();++j) {
        const auto& a=*sources[i];const auto& b=*sources[j];const QStringList ids{a.id,b.id};
        const auto* sa=std::get_if<Segment>(&a.geometry);const auto* sb=std::get_if<Segment>(&b.geometry);
        if(sa && sb) {
            if((distance(sa->start,sb->start)<=tolerance && distance(sa->end,sb->end)<=tolerance)
                || (distance(sa->start,sb->end)<=tolerance && distance(sa->end,sb->start)<=tolerance))
                return failure(Status::Invalid,Code::DuplicateCurve,"Duplicate Profile segments, including reversed copies, are not allowed.",ids);
        }
        const auto hits=Sketch::intersect(a.geometry,b.geometry,tolerance);
        if(hits.kind==IntersectionKind::Coincident) {
            bool duplicate=false;
            if(const auto* aa=std::get_if<Arc>(&a.geometry)) if(const auto* ab=std::get_if<Arc>(&b.geometry)) {
                const auto [a0,a1]=ends(a.geometry);const auto [b0,b1]=ends(b.geometry);
                duplicate=std::abs(aa->radius*std::abs(aa->sweepAngle)-ab->radius*std::abs(ab->sweepAngle))<=tolerance
                    && ((distance(a0,b0)<=tolerance && distance(a1,b1)<=tolerance)
                    || (distance(a0,b1)<=tolerance && distance(a1,b0)<=tolerance));
            }
            if(std::holds_alternative<Circle>(a.geometry) && std::holds_alternative<Circle>(b.geometry)) duplicate=true;
            return failure(Status::Invalid,duplicate?Code::DuplicateCurve:Code::Overlap,"Profile curves overlap or duplicate the same boundary.",ids);
        }
        if(hits.kind==IntersectionKind::Unsupported || hits.kind==IntersectionKind::NumericalFailure)
            return failure(Status::Unsupported,Code::NumericalFailure,"Profile intersections could not be certified.",ids);
        if(!std::holds_alternative<Circle>(a.geometry) && !std::holds_alternative<Circle>(b.geometry)) {
            const size_t ia=curveIndex[i],ib=curveIndex[j];
            const auto shared=[&](size_t x,size_t y){return roots[2*ia+x]==roots[2*ib+y];};
            for(size_t x=0;x<2;++x) {
                if(onCurve(b.geometry,endpoints[2*ia+x],tolerance) && !shared(x,0) && !shared(x,1))
                    return failure(Status::Invalid,Code::Branch,"A Profile endpoint touches the interior of another curve (T-branch).",ids,endpoints[2*ia+x]);
                if(onCurve(a.geometry,endpoints[2*ib+x],tolerance) && !shared(0,x) && !shared(1,x))
                    return failure(Status::Invalid,Code::Branch,"A Profile endpoint touches the interior of another curve (T-branch).",ids,endpoints[2*ib+x]);
            }
            for(const auto p:hits.points) {
                if(!onCurve(a.geometry,p,tolerance) || !onCurve(b.geometry,p,tolerance)) continue;
                bool joint=false;
                for(size_t x=0;x<2;++x) for(size_t y=0;y<2;++y)
                    if(shared(x,y) && distance(p,endpoints[2*ia+x])<=tolerance && distance(p,endpoints[2*ib+y])<=tolerance) joint=true;
                if(!joint) {
                    const bool nearA=distance(p,endpoints[2*ia])<=tolerance || distance(p,endpoints[2*ia+1])<=tolerance;
                    const bool nearB=distance(p,endpoints[2*ib])<=tolerance || distance(p,endpoints[2*ib+1])<=tolerance;
                    if(nearA && nearB) return failure(Status::Invalid,Code::AmbiguousConnection,
                        "Near-endpoint contact does not resolve to a shared endpoint within tolerance.",ids,p);
                    return failure(Status::Invalid,Code::SelfIntersection,"Profile curves cross away from a shared endpoint.",ids,p);
                }
            }
        } else if(!hits.points.empty()) return failure(Status::Unsupported,Code::ContourIntersection,
            "Touching or crossing full-circle boundaries do not define unambiguous material regions.",ids,hits.points.front());
    }
    std::vector<std::vector<size_t>> adjacent(endpoints.size());
    for(size_t i=0;i<finiteCurves.size();++i) {
        if(roots[2*i]==roots[2*i+1]) return failure(Status::Invalid,Code::DegenerateCurve,"A Profile edge collapses to one endpoint node.",{finiteCurves[i]->id});
        adjacent[roots[2*i]].push_back(i);adjacent[roots[2*i+1]].push_back(i);
    }
    for(size_t i=0;i<adjacent.size();++i) if(adjacent[i].size()>2) {
        QStringList ids;for(auto edge:adjacent[i]) ids<<finiteCurves[edge]->id;
        return failure(Status::Invalid,Code::Branch,"More than two Profile edges meet at one endpoint.",ids,endpoints[i]);
    }
    for(size_t i=0;i<adjacent.size();++i) if(adjacent[i].size()==1)
        return failure(Status::Invalid,Code::OpenContour,"A Profile endpoint is unconnected within the geometry tolerance.",{finiteCurves[adjacent[i][0]]->id},endpoints[i]);
    std::vector<Contour> contours;std::vector<bool> visited(finiteCurves.size(),false);
    for(size_t first=0;first<finiteCurves.size();++first) if(!visited[first]) {
        Contour contour;const auto [initialStart,initialEnd]=ends(finiteCurves[first]->geometry);
        const auto start=roots[2*first+(before(initialEnd,initialStart)?1:0)];size_t node=start,edge=first;
        do {
            if(visited[edge]) return failure(Status::Invalid,Code::Branch,"Profile traversal returned to an already used edge.",{finiteCurves[edge]->id});
            visited[edge]=true;auto g=finiteCurves[edge]->geometry;const bool reverse=roots[2*edge]!=node;
            if(reverse) {
                if(auto* s=std::get_if<Segment>(&g)) std::swap(s->start,s->end);
                else g=reversedArc(std::get<Arc>(g));
            }
            if(const auto* s=std::get_if<Segment>(&g)) contour.curves.push_back({finiteCurves[edge]->id,*s,reverse});
            else contour.curves.push_back({finiteCurves[edge]->id,std::get<Arc>(g),reverse});
            node=roots[roots[2*edge]==node?2*edge+1:2*edge];
            if(node!=start) edge=adjacent[node][0]==edge?adjacent[node][1]:adjacent[node][0];
        } while(node!=start);
        // Green's theorem: endpoint shoelace plus each exact circular segment.
        // Tiny tolerated joints are included only in analysis, never repaired.
        const auto origin=representative(contour);
        long double twiceArea=0,perimeter=0;
        for(size_t i=0;i<contour.curves.size();++i) {
            const auto g=geometry(contour.curves[i]);const auto [p,q]=ends(g);
            const auto next=ends(geometry(contour.curves[(i+1)%contour.curves.size()])).first;
            const auto cross=[&](Point a,Point b) {return (static_cast<long double>(a.x)-origin.x)*(static_cast<long double>(b.y)-origin.y)
                -(static_cast<long double>(a.y)-origin.y)*(static_cast<long double>(b.x)-origin.x);};
            twiceArea+=cross(p,q)+cross(q,next);
            contour.maximumEndpointGap=std::max(contour.maximumEndpointGap,double(distance(q,next)));
            if(const auto* arc=std::get_if<Arc>(&g)) {
                const long double sweep=arc->sweepAngle,r=arc->radius;
                twiceArea+=r*r*(sweep-std::sin(sweep));perimeter+=r*std::abs(sweep);
            } else perimeter+=distance(p,q);
        }
        if(!std::isfinite(twiceArea) || !std::isfinite(perimeter))
            return failure(Status::Unsupported,Code::NumericalFailure,"The Profile area could not be resolved numerically.",{finiteCurves[first]->id});
        if(std::abs(twiceArea)<=static_cast<long double>(tolerance)*perimeter)
            return failure(Status::Invalid,Code::DegenerateCurve,"The closed Profile has no resolvable area.",{finiteCurves[first]->id});
        contour.signedArea=twiceArea/2;
        contour.orientation=twiceArea<0?Orientation::Clockwise:Orientation::Counterclockwise;
        contours.push_back(std::move(contour));
    }
    for(const auto* e:circles) {
        const auto& c=std::get<Circle>(e->geometry);Contour contour;contour.curves.push_back({e->id,c});
        contour.signedArea=std::numbers::pi_v<long double>*c.radius*c.radius;contours.push_back(std::move(contour));
    }
    std::vector<std::vector<bool>> contains(contours.size(),std::vector<bool>(contours.size(),false));
    for(size_t i=0;i<contours.size();++i) for(size_t j=0;j<contours.size();++j) if(i!=j) {
        const auto containment=inside(representative(contours[j]),contours[i],tolerance);
        if(!containment) return failure(Status::Unsupported,Code::NumericalFailure,"Contour containment cannot be certified at this tolerance.",
            {contours[i].curves.front().entityId,contours[j].curves.front().entityId});
        contains[i][j]=*containment;
    }
    for(size_t i=0;i<contours.size();++i) {
        size_t depth=0;for(size_t j=0;j<contours.size();++j) if(contains[j][i]) ++depth;
        contours[i].depth=depth;
        for(size_t j=0;j<contours.size();++j) if(contains[j][i]) {
            if(contains[i][j]) return failure(Status::Unsupported,Code::NumericalFailure,"Mutual contour containment is ambiguous.");
            bool direct=true;
            for(size_t k=0;k<contours.size();++k) if(contains[j][k] && contains[k][i]) direct=false;
            if(direct) {
                if(contours[i].parent) return failure(Status::Unsupported,Code::NumericalFailure,"Contour has multiple possible parents.");
                contours[i].parent=j;
            }
        }
        if(depth>0 && !contours[i].parent) return failure(Status::Unsupported,Code::NumericalFailure,"Contour containment hierarchy is inconsistent.");
    }
    Result result;result.status=Status::Valid;result.contours=std::move(contours);
    for(size_t i=0;i<result.contours.size();++i) {
        const auto& contour=result.contours[i];
        if(contour.depth%2==0) {
            Region region{i,{}};
            for(size_t j=0;j<result.contours.size();++j) if(result.contours[j].parent==i) region.holes.push_back(j);
            result.regions.push_back(std::move(region));
        }
        if(contour.maximumEndpointGap>0) result.constructionDiagnostics.push_back({Code::TolerantClosure,
            "Contour endpoints connect within tolerance but are not exactly coincident; no gap repair was performed.",
            {contour.curves.front().entityId},{}});
    }
    result.constructionDiagnostics.push_back({Code::WireNotCertified,
        "Profile analysis is valid; exact OCCT wire closure and B-Rep validity have not been certified. A construction adapter must validate them before extrusion.",{}, {}});
    return result;
}
}
