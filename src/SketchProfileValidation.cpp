#include "SketchProfileValidation.h"
#include "SketchSnapping.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <numbers>
#include <stdexcept>
#include <Geom2d_Ellipse.hxx>
#include <Geom2d_Circle.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom2dAPI_ProjectPointOnCurve.hxx>
#include <Standard_Failure.hxx>

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
    if(const auto* a=std::get_if<EllipticalArc>(&g)) {const auto e=ellipticalArcEndpoints(*a);return {e[0],e[1]};}
    if(const auto* s=std::get_if<Spline>(&g)) return {s->points.front(),s->closed?s->points.front():s->points.back()};
    const auto& a=std::get<Arc>(g);return {arcPoint(a,a.startAngle),arcPoint(a,a.startAngle+a.sweepAngle)};
}
// Used only for Profile certification. Snapping retains its explicit conic limits.
Intersections profileIntersections(const Geometry& a,const Geometry& b,double tolerance) {
    auto result=Sketch::intersect(a,b,tolerance);
    if(result.kind!=IntersectionKind::Unsupported) return result;
    const auto curve=[](const Geometry& g)->Handle(Geom2d_Curve) {
        Handle(Geom2d_Curve) support;double first=0,last=2*pi;bool trimmed=false;
        if(const auto* s=std::get_if<Spline>(&g)) return splineCurve(*s);
        if(const auto* s=std::get_if<Segment>(&g)) {
            const gp_Pnt2d a(s->start.x,s->start.y),b(s->end.x,s->end.y);
            return new Geom2d_TrimmedCurve(new Geom2d_Line(a,gp_Dir2d(gp_Vec2d(a,b))),0,a.Distance(b));
        }
        if(const auto* l=std::get_if<InfiniteLine>(&g)) return new Geom2d_Line(gp_Pnt2d(l->origin.x,l->origin.y),gp_Dir2d(l->direction.x,l->direction.y));
        if(isElliptical(g)) {
            Ellipse e;
            if(const auto* arc=std::get_if<EllipticalArc>(&g)) {
                e=arc->ellipse;first=std::min(arc->startParameter,arc->startParameter+arc->sweepParameter);
                last=first+std::abs(arc->sweepParameter);trimmed=true;
            } else e=std::get<Ellipse>(g);
            support=new Geom2d_Ellipse(gp_Ax2d(gp_Pnt2d(e.center.x,e.center.y),gp_Dir2d(std::cos(e.axisAngle),std::sin(e.axisAngle))),e.majorRadius,e.minorRadius);
        } else {
            Circle c;
            if(const auto* arc=std::get_if<Arc>(&g)) {
                c={arc->center,arc->radius};first=std::min(arc->startAngle,arc->startAngle+arc->sweepAngle);
                last=first+std::abs(arc->sweepAngle);trimmed=true;
            } else if(const auto* circle=std::get_if<Circle>(&g)) c=*circle;
            else return {};
            support=new Geom2d_Circle(gp_Ax2d(gp_Pnt2d(c.center.x,c.center.y),gp_Dir2d(1,0)),c.radius);
        }
        if(trimmed) return new Geom2d_TrimmedCurve(support,first,last);
        return support;
    };
    try {
        const auto ca=curve(a),cb=curve(b);if(ca.IsNull() || cb.IsNull()) return result;
        Geom2dAPI_InterCurveCurve intersection(ca,cb,tolerance);
        if(!intersection.Intersector().IsDone()) return {IntersectionKind::NumericalFailure,{}};
        result={};
        if(intersection.NbSegments()) {result.kind=IntersectionKind::Coincident;return result;}
        for(int i=1;i<=intersection.NbPoints();++i) {
            const auto p=intersection.Point(i);Point q{p.X(),p.Y()};
            if(!onCurve(a,q,tolerance) || !onCurve(b,q,tolerance)) return {IntersectionKind::NumericalFailure,{}};
            result.points.push_back(q);
        }
        result.kind=result.points.empty()?IntersectionKind::None:IntersectionKind::Finite;
        return result;
    } catch(const Standard_Failure&) {return {IntersectionKind::NumericalFailure,{}};}
}
Spline reversedSpline(Spline s) {
    std::reverse(s.points.begin(),s.points.end());
    if(s.closed) std::rotate(s.points.begin(),s.points.end()-1,s.points.end());
    std::swap(s.startTangent,s.endTangent);
    for(auto* t:{&s.startTangent,&s.endTangent}) if(*t) **t={-(**t).x,-(**t).y};
    return s;
}
long double splineIntegral(const Spline& s,Point origin) {
    const auto c=splineCurve(s);
    if(c->IsRational() || c->Degree()>3) throw std::runtime_error("Spline area requires nonrational degree <= 3.");
    long double integral=0;
    for(int k=1;k<c->NbKnots();++k) {
        const double lo=std::max(c->FirstParameter(),c->Knot(k)),hi=std::min(c->LastParameter(),c->Knot(k+1));
        if(hi<=lo) continue;
        for(int i=0;i<3;++i) {
            const double x=i==0?-std::sqrt(3./5):i==1?0:std::sqrt(3./5),w=i==1?8./9:5./9;
            gp_Pnt2d p;gp_Vec2d v;c->D1((lo+hi)/2+(hi-lo)*x/2,p,v);
            integral+=((static_cast<long double>(p.X())-origin.x)*v.Y()-(static_cast<long double>(p.Y())-origin.y)*v.X())*((hi-lo)*w/2);
        }
    }
    return integral;
}
Geometry geometry(const Curve& c) {return std::visit([](const auto& g)->Geometry{return g;},c.geometry);}
Point representative(const Contour& contour) {
    const auto g=geometry(contour.curves.front());
    if(const auto* c=std::get_if<Circle>(&g)) return {c->center.x+c->radius,c->center.y};
    if(const auto* e=std::get_if<Ellipse>(&g)) return ellipsePoint(*e,0);
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
    if(const auto* e=std::get_if<Ellipse>(&contour.curves.front().geometry)) {
        if(onCurve(*e,p,tolerance)) return {};
        const long double c=std::cos(e->axisAngle),s=std::sin(e->axisAngle);
        const long double x=static_cast<long double>(p.x)-e->center.x,y=static_cast<long double>(p.y)-e->center.y;
        return std::hypot((c*x+s*y)/e->majorRadius,(-s*x+c*y)/e->minorRadius)<1;
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
            } else if(const auto* spline=std::get_if<Spline>(&g)) {
                const auto hits=profileIntersections(g,InfiniteLine{p,{co,si}},tolerance);
                if(hits.kind==IntersectionKind::Unsupported || hits.kind==IntersectionKind::NumericalFailure || hits.kind==IntersectionKind::Coincident) {ambiguous=true;break;}
                const auto c=splineCurve(*spline);
                for(auto hit:hits.points) {
                    Geom2dAPI_ProjectPointOnCurve projection(gp_Pnt2d(hit.x,hit.y),c);
                    if(projection.NbPoints()==0) return {};
                    gp_Pnt2d point;gp_Vec2d derivative;c->D1(projection.LowerDistanceParameter(),point,derivative);
                    if(std::abs(derivative.Y()*co-derivative.X()*si)<=1.e-10*std::max(1.,derivative.Magnitude())) {ambiguous=true;break;}
                    const auto q=local(hit);if(std::abs(q.x)<=tolerance) {ambiguous=true;break;}
                    if(q.x>0) result=!result;
                }
                if(ambiguous) break;
            } else if(const auto* elliptic=std::get_if<EllipticalArc>(&g)) {
                const auto center=local(elliptic->ellipse.center);
                const double theta=elliptic->ellipse.axisAngle-angle;
                const double radius=std::hypot(elliptic->ellipse.majorRadius*std::sin(theta),elliptic->ellipse.minorRadius*std::cos(theta));
                if(std::abs(std::abs(center.y)-radius)<=tolerance) {ambiguous=true;break;}
                const auto hits=Sketch::intersect(g,InfiniteLine{p,{co,si}},tolerance);
                if(hits.kind==IntersectionKind::Unsupported || hits.kind==IntersectionKind::NumericalFailure) return {};
                for(auto hit:hits.points) {
                    const auto q=local(hit);
                    if(std::abs(q.x)<=tolerance) {ambiguous=true;break;}
                    if(q.x>0) result=!result;
                }
                if(ambiguous) break;
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
    try {
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
        if(const auto* spline=std::get_if<Spline>(&e.geometry)) {
            const auto c=splineCurve(*spline);
            for(int pole=1;pole<=c->NbPoles();++pole) if(!reliable(Point{c->Pole(pole).X(),c->Pole(pole).Y()},tolerance))
                return failure(Status::Unsupported,Code::NumericalFailure,"Spline poles cannot resolve the requested tolerance.",{e.id});
            Geom2dAPI_InterCurveCurve self(c,tolerance);
            if(!self.Intersector().IsDone()) return failure(Status::Unsupported,Code::NumericalFailure,"Spline self-intersections could not be certified.",{e.id});
            if(self.NbSegments()) return failure(Status::Invalid,Code::SelfIntersection,"A spline overlaps itself.",{e.id});
            for(int hit=1;hit<=self.NbPoints();++hit) {
                const auto& intersection=self.Intersector().Point(hit);
                const double a=intersection.ParamOnFirst(),b=intersection.ParamOnSecond();
                // OCCT can return seam parameters a few ULPs outside the domain.
                // Accept only arithmetic roundoff at the two known periodic ends.
                const double noise=64*std::numeric_limits<double>::epsilon()*std::max({1.,std::abs(c->FirstParameter()),std::abs(c->LastParameter())});
                const auto seam=[&](double u,double v) {
                    return std::abs(u-c->FirstParameter())<=noise && std::abs(v-c->LastParameter())<=noise
                        && c->Value(u).Distance(c->StartPoint())<=tolerance && c->Value(v).Distance(c->EndPoint())<=tolerance;
                };
                if(!spline->closed || !(seam(a,b) || seam(b,a)))
                    return failure(Status::Invalid,Code::SelfIntersection,QString("A spline intersects itself (parameters %1, %2; domain %3, %4).").arg(a,0,'g',17).arg(b,0,'g',17).arg(c->FirstParameter(),0,'g',17).arg(c->LastParameter(),0,'g',17),{e.id});
            }
        }
        if(isElliptical(e.geometry)) {
            const auto ellipse=std::holds_alternative<Ellipse>(e.geometry)?std::get<Ellipse>(e.geometry):std::get<EllipticalArc>(e.geometry).ellipse;
            if(ellipse.minorRadius<=tolerance) return failure(Status::Invalid,Code::DegenerateCurve,"Ellipse minor radius is smaller than the geometry tolerance.",{e.id});
            for(double t:{0.,pi/2,pi,3*pi/2}) if(!reliable(ellipsePoint(ellipse,t),tolerance))
                return failure(Status::Unsupported,Code::NumericalFailure,"Ellipse coordinates cannot resolve the requested tolerance.",{e.id});
            if(!reliable(ellipse.axisAngle,tolerance/std::max(1.,ellipse.majorRadius)))
                return failure(Status::Unsupported,Code::NumericalFailure,"Ellipse orientation cannot resolve the requested tolerance.",{e.id});
            if(const auto* a=std::get_if<EllipticalArc>(&e.geometry);a && !reliable(a->startParameter,tolerance/std::max(1.,ellipse.majorRadius)))
                return failure(Status::Unsupported,Code::NumericalFailure,"Ellipse parameter cannot resolve the requested tolerance.",{e.id});
        }
        if(std::holds_alternative<Segment>(e.geometry) || std::holds_alternative<Arc>(e.geometry) || std::holds_alternative<EllipticalArc>(e.geometry) || (std::holds_alternative<Spline>(e.geometry) && !std::get<Spline>(e.geometry).closed)) {
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
        } else if(std::holds_alternative<Spline>(e.geometry)) {
            circles.push_back(&e);
        } else if(const auto* ellipse=std::get_if<Ellipse>(&e.geometry)) {
            if(ellipse->minorRadius<=tolerance) return failure(Status::Invalid,Code::DegenerateCurve,"Ellipse minor radius is smaller than the geometry tolerance.",{e.id});
            circles.push_back(&e);
        } else if(const auto* c=std::get_if<Circle>(&e.geometry)) {
            if(c->radius<=tolerance) return failure(Status::Invalid,Code::DegenerateCurve,"A Profile circle is smaller than the geometry tolerance.",{e.id});
            if(!reliable(c->center,tolerance) || !reliable(c->radius,tolerance)
                || !reliable(Point{c->center.x+c->radius,c->center.y+c->radius},tolerance)
                || !reliable(Point{c->center.x-c->radius,c->center.y-c->radius},tolerance))
                return failure(Status::Unsupported,Code::NumericalFailure,"Circle coordinates cannot resolve the requested tolerance reliably.",{e.id});
            circles.push_back(&e);
        } else return failure(Status::Unsupported,Code::UnsupportedCurve,
            "Profile supports segments, circles, ellipses, their arcs and interpolating splines; projected coefficient B-splines require separate certification.",{e.id});
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
        const auto hits=profileIntersections(a.geometry,b.geometry,tolerance);
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
        if(!std::holds_alternative<Circle>(a.geometry) && !std::holds_alternative<Ellipse>(a.geometry) && !(std::holds_alternative<Spline>(a.geometry) && std::get<Spline>(a.geometry).closed) && !std::holds_alternative<Circle>(b.geometry) && !std::holds_alternative<Ellipse>(b.geometry) && !(std::holds_alternative<Spline>(b.geometry) && std::get<Spline>(b.geometry).closed)) {
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
                else if(const auto* a=std::get_if<Arc>(&g)) g=reversedArc(*a);
                else if(const auto* a=std::get_if<EllipticalArc>(&g)) g=reversedEllipticalArc(*a);
                else g=reversedSpline(std::get<Spline>(g));
            }
            if(const auto* s=std::get_if<Segment>(&g)) contour.curves.push_back({finiteCurves[edge]->id,*s,reverse});
            else if(const auto* a=std::get_if<Arc>(&g)) contour.curves.push_back({finiteCurves[edge]->id,*a,reverse});
            else if(const auto* a=std::get_if<EllipticalArc>(&g)) contour.curves.push_back({finiteCurves[edge]->id,*a,reverse});
            else contour.curves.push_back({finiteCurves[edge]->id,std::get<Spline>(g),reverse});
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
            } else if(const auto* arc=std::get_if<EllipticalArc>(&g)) {
                const long double sweep=arc->sweepParameter;
                twiceArea+=static_cast<long double>(arc->ellipse.majorRadius)*arc->ellipse.minorRadius*(sweep-std::sin(sweep));
                perimeter+=arc->ellipse.majorRadius*std::abs(sweep); // conservative area uncertainty bound
            } else if(const auto* spline=std::get_if<Spline>(&g)) {
                twiceArea+=splineIntegral(*spline,origin)-cross(p,q);
                const auto c=splineCurve(*spline);
                for(int pole=2;pole<=c->NbPoles();++pole) perimeter+=c->Pole(pole).Distance(c->Pole(pole-1));
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
        Contour contour;
        if(const auto* spline=std::get_if<Spline>(&e->geometry)) {
            contour.curves.push_back({e->id,*spline});contour.signedArea=splineIntegral(*spline,spline->points.front())/2;
            const auto c=splineCurve(*spline);long double perimeter=0;
            for(int pole=2;pole<=c->NbPoles();++pole) perimeter+=c->Pole(pole).Distance(c->Pole(pole-1));
            if(!std::isfinite(contour.signedArea) || 2*std::abs(contour.signedArea)<=tolerance*perimeter)
                return failure(Status::Invalid,Code::DegenerateCurve,"Closed spline area is unresolved.",{e->id});
            contour.orientation=contour.signedArea<0?Orientation::Clockwise:Orientation::Counterclockwise;
        } else if(const auto* ellipse=std::get_if<Ellipse>(&e->geometry)) {
            contour.curves.push_back({e->id,*ellipse});
            contour.signedArea=std::numbers::pi_v<long double>*ellipse->majorRadius*ellipse->minorRadius;
        } else {
            const auto& c=std::get<Circle>(e->geometry);contour.curves.push_back({e->id,c});
            contour.signedArea=std::numbers::pi_v<long double>*c.radius*c.radius;
        }
        contours.push_back(std::move(contour));
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
    Result result;result.connectionTolerance=tolerance;result.status=Status::Valid;result.contours=std::move(contours);
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
    } catch(const Standard_Failure& e) {return failure(Status::Unsupported,Code::NumericalFailure,QString::fromUtf8(e.GetMessageString()));}
      catch(const std::exception& e) {return failure(Status::Unsupported,Code::NumericalFailure,QString::fromUtf8(e.what()));}
}
}
