#include "SketchCurveOperations.h"
#include "SketchSnapping.h"
#include <Geom2d_Ellipse.hxx>
#include <Geom2d_Circle.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom2dAPI_ProjectPointOnCurve.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace SketchCurveOperations {
namespace {
using namespace Sketch;
constexpr double pi=std::numbers::pi,turn=2*pi;
double distance(Point a,Point b) {return std::hypot(a.x-b.x,a.y-b.y);}
Point point(const gp_Pnt2d& p) {return {p.X(),p.Y()};}
bool reliable(double x,double tolerance) {
    const double a=std::abs(x);
    return std::isfinite(x) && a<std::sqrt(std::numeric_limits<double>::max())/16
        && std::nextafter(a,std::numeric_limits<double>::infinity())-a<=tolerance/16;
}
struct Curve {
    Handle(Geom2d_Curve) support,bounded;
    double start=0,end=0,period=0;
    double tolerance=1.e-7;
    bool closed=false,finite=true;
    double sign() const {return end>=start?1:-1;}
    double length() const {return std::abs(end-start);}
    double progress(double u) const {return sign()*(u-start);}
    double parameter(double d) const {return start+sign()*d;}
    double roundoff() const {
        return 64*std::numeric_limits<double>::epsilon()*std::max({1.,std::abs(start),std::abs(end)});
    }
    // Normalize only periodic aliases. Never clamp an out-of-domain root.
    double normalize(double u) const {
        if(period>0) {
            double d=std::fmod(progress(u),period);if(d<0) d+=period;
            if((period-d<=roundoff() || d<=roundoff())
               && support->Value(parameter(d)).Distance(support->Value(start))<=tolerance/16) d=0;
            return parameter(d);
        }
        return u;
    }
    bool contains(double u) const {
        return !finite || (progress(u)>=-roundoff() && progress(u)<=length()+roundoff());
    }
};
Curve adapt(const Geometry& g,double tolerance,bool precisionGuard=true) {
    Entity e;e.id="curve";e.workplaneId="local";e.role=Role::Construction;e.geometry=g;
    QString error;if(!valid(e,error)) throw std::invalid_argument(error.toStdString());
    Curve c;c.tolerance=tolerance;c.bounded=boundedCurve(g);c.support=c.bounded;
    if(c.support.IsNull()) throw std::invalid_argument("Unsupported curve type.");
    if(const auto t=Handle(Geom2d_TrimmedCurve)::DownCast(c.support);!t.IsNull()) c.support=t->BasisCurve();
    c.start=c.bounded->FirstParameter();c.end=c.bounded->LastParameter();
    if(const auto* a=std::get_if<Arc>(&g)) {c.start=a->startAngle;c.end=c.start+a->sweepAngle;c.period=turn;}
    if(const auto* a=std::get_if<EllipticalArc>(&g)) {c.start=a->startParameter;c.end=c.start+a->sweepParameter;c.period=turn;}
    if(std::holds_alternative<Circle>(g) || std::holds_alternative<Ellipse>(g)) {c.period=turn;c.closed=true;}
    if(const auto* s=std::get_if<Spline>(&g)) {c.closed=s->closed;if(c.closed)c.period=c.length();}
    if(const auto* s=std::get_if<BSpline>(&g)) {
        c.start=s->reversed?s->last:s->first;c.end=s->reversed?s->first:s->last;
        if(c.support->IsPeriodic()) {
            c.period=c.support->Period();c.closed=std::abs(c.length()-c.period)<=c.roundoff();
        }
    }
    if(std::holds_alternative<InfiniteLine>(g)) c.finite=false;
    // Exact coefficient/domain comparisons need validated values and explicit
    // residual certificates, not the arithmetic headroom heuristic used by
    // editable curve operations. Existing callers retain that heuristic.
    if(!precisionGuard) return c;
    const auto check=[&](gp_Pnt2d p) {
        if(!reliable(p.X(),tolerance) || !reliable(p.Y(),tolerance)) throw std::runtime_error("Coordinates cannot resolve the geometric tolerance.");
    };
    if(const auto s=Handle(Geom2d_BSplineCurve)::DownCast(c.support);!s.IsNull()) {
        for(int i=1;i<=s->NbPoles();++i) check(s->Pole(i));
        for(int i=1;i<=s->NbKnots();++i) if(!reliable(s->Knot(i),c.length()*1.e-12))
            throw std::runtime_error("Spline knot parameters cannot be resolved.");
    }
    if(c.finite) {
        for(double u:{c.start,c.end}) {
            gp_Pnt2d p;gp_Vec2d d;c.support->D1(u,p,d);
            const double speed=d.Magnitude();
            if(!std::isfinite(speed) || !reliable(u,tolerance/std::max(1.,speed)))
                throw std::runtime_error("Curve parameters cannot resolve the geometric tolerance.");
        }
        check(c.support->Value(c.start));check(c.support->Value(c.end));
    }
    // Conic coefficients and their extrema must resolve local model tolerance.
    if(const auto a=Handle(Geom2d_Ellipse)::DownCast(c.support);!a.IsNull()) {
        const double angle=std::holds_alternative<Ellipse>(g)?std::get<Ellipse>(g).axisAngle:std::get<EllipticalArc>(g).ellipse.axisAngle;
        if(!reliable(angle,tolerance/std::max(1.,a->MajorRadius()))) throw std::runtime_error("Ellipse orientation cannot resolve the geometric tolerance.");
        for(double u:{0.,pi/2,pi,3*pi/2}) check(a->Value(u));
        if(a->MinorRadius()<=tolerance) throw std::runtime_error("Ellipse collapses within tolerance.");
    }
    if(const auto a=Handle(Geom2d_Circle)::DownCast(c.support);!a.IsNull()) {
        for(double u:{0.,pi/2,pi,3*pi/2}) check(a->Value(u));
        if(a->Radius()<=tolerance) throw std::runtime_error("Circle collapses within tolerance.");
    }
    if(const auto a=Handle(Geom2d_Line)::DownCast(c.support);!a.IsNull()) check(a->Location());
    return c;
}
// A toleranced OCCT intersection segment alone is not proof of coincidence.
// Only identical analytic supports / identical B-spline coefficients certify it.
// Equivalent supports with other parameterizations may conservatively refuse.
bool sameSupport(const Curve& a,const Curve& b) {
    const auto la=Handle(Geom2d_Line)::DownCast(a.support),lb=Handle(Geom2d_Line)::DownCast(b.support);
    if(!la.IsNull() && !lb.IsNull()) return la->Direction().IsParallel(lb->Direction(),1.e-14)
        && la->Lin2d().Distance(lb->Location())==0;
    const auto ca=Handle(Geom2d_Circle)::DownCast(a.support),cb=Handle(Geom2d_Circle)::DownCast(b.support);
    if(!ca.IsNull() && !cb.IsNull()) return ca->Location().IsEqual(cb->Location(),0) && ca->Radius()==cb->Radius();
    const auto ea=Handle(Geom2d_Ellipse)::DownCast(a.support),eb=Handle(Geom2d_Ellipse)::DownCast(b.support);
    if(!ea.IsNull() && !eb.IsNull()) return ea->Location().IsEqual(eb->Location(),0)
        && ea->MajorRadius()==eb->MajorRadius() && ea->MinorRadius()==eb->MinorRadius()
        && (ea->MajorRadius()==ea->MinorRadius() || ea->XAxis().Direction().IsParallel(eb->XAxis().Direction(),1.e-14));
    const auto sa=Handle(Geom2d_BSplineCurve)::DownCast(a.support),sb=Handle(Geom2d_BSplineCurve)::DownCast(b.support);
    if(sa.IsNull() || sb.IsNull() || sa->Degree()!=sb->Degree() || sa->IsPeriodic()!=sb->IsPeriodic()
       || sa->NbPoles()!=sb->NbPoles() || sa->NbKnots()!=sb->NbKnots()) return false;
    for(int i=1;i<=sa->NbPoles();++i) if(!sa->Pole(i).IsEqual(sb->Pole(i),0) || sa->Weight(i)!=sb->Weight(i)) return false;
    for(int i=1;i<=sa->NbKnots();++i) if(sa->Knot(i)!=sb->Knot(i) || sa->Multiplicity(i)!=sb->Multiplicity(i)) return false;
    return true;
}
Intersections failed(Status s,const QString& message) {return {s,{},message};}
}
Handle(Geom2d_Curve) boundedCurve(const Sketch::Geometry& g) {
    using namespace Sketch;
    Handle(Geom2d_Curve) support;double first=0,last=2*pi;bool trimmed=false;
    if(const auto* s=std::get_if<Spline>(&g)) return splineCurve(*s);
    if(const auto* s=std::get_if<BSpline>(&g)) return new Geom2d_TrimmedCurve(bsplineCurve(*s),s->first,s->last,true,false);
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

}
Intersections intersect(const Sketch::Geometry& first,const Sketch::Geometry& second,double tolerance) {
    if(!std::isfinite(tolerance) || tolerance<=0) return failed(Status::Invalid,"Specify a finite positive tolerance.");
    if(std::holds_alternative<Sketch::ConstructionPoint>(first) || std::holds_alternative<Sketch::ConstructionPoint>(second))
        return failed(Status::Unsupported,"A point has no curve domain.");
    try {
        const auto a=adapt(first,tolerance),b=adapt(second,tolerance);
        Geom2dAPI_InterCurveCurve intersection(a.bounded,b.bounded,tolerance);
        if(!intersection.Intersector().IsDone()) return failed(Status::NumericalFailure,"OCCT intersection did not complete.");
        if(intersection.NbSegments()) {
            const auto relation=supportRelation(first,second,tolerance);
            if(relation.status!=Status::Success) return failed(relation.status,relation.message);
            if(relation.common) {
                if(relation.overlap) return failed(Status::Overlap,"Common curve intervals cannot be split.");
                // Coincident supports can have disjoint finite interiors and
                // certified endpoint-only contacts (e.g. complementary arcs).
                Intersections result{Status::Success,{}, {}};
                for(auto q:relation.boundaries) {
                    const gp_Pnt2d p(q.x,q.y);
                    Geom2dAPI_ProjectPointOnCurve ap(p,a.support),bp(p,b.support);
                    if(ap.NbPoints()==0 || bp.NbPoints()==0) return failed(Status::NumericalFailure,"Common-support endpoint parameters could not be evaluated.");
                    const double u=a.normalize(ap.LowerDistanceParameter()),v=b.normalize(bp.LowerDistanceParameter());
                    if(!a.contains(u) || !b.contains(v)) continue;
                    if(a.support->Value(u).Distance(p)>tolerance || b.support->Value(v).Distance(p)>tolerance)
                        return failed(Status::NumericalFailure,"Common-support contact residual exceeds tolerance.");
                    bool duplicate=false;
                    for(const auto& old:result.hits) if(distance(old.point,q)<=tolerance) {
                        if(std::abs(old.firstParameter-u)>a.roundoff() || std::abs(old.secondParameter-v)>b.roundoff())
                            return failed(Status::Ambiguous,"Common-support contacts cannot be separated within tolerance.");
                        duplicate=true;break;
                    }
                    if(!duplicate) result.hits.push_back({q,u,v,Contact::Tangency});
                }
                std::sort(result.hits.begin(),result.hits.end(),[&](const Hit& x,const Hit& y){return a.progress(x.firstParameter)<a.progress(y.firstParameter);});
                for(int i=1;i<=intersection.NbPoints();++i) {
                    const auto& p=intersection.Intersector().Point(i);
                    const double u=a.normalize(p.ParamOnFirst()),v=b.normalize(p.ParamOnSecond());
                    if(std::none_of(result.hits.begin(),result.hits.end(),[&](const Hit& h){return std::abs(h.firstParameter-u)<=a.roundoff()
                        && std::abs(h.secondParameter-v)<=b.roundoff() && distance(h.point,point(p.Value()))<=tolerance;}))
                        return failed(Status::Ambiguous,"Common-support contacts include another unresolved intersection.");
                }
                return result;
            }
            // OCCT may report an isolated analytic tangency as a small segment
            // whose points lie within Tol of both supports. Reuse the existing
            // analytic solver, but only accept its single point when residuals
            // and tangent directions agree at floating-point roundoff, rather
            // than merely within the much larger model tolerance.
            const auto analytic=Sketch::intersect(first,second,tolerance);
            if(analytic.kind==Sketch::IntersectionKind::Finite && analytic.points.size()==1) {
                const auto p=analytic.points.front();
                Geom2dAPI_ProjectPointOnCurve ap(gp_Pnt2d(p.x,p.y),a.bounded),bp(gp_Pnt2d(p.x,p.y),b.bounded);
                if(ap.NbPoints()>0 && bp.NbPoints()>0) {
                    const double u=a.normalize(ap.LowerDistanceParameter()),v=b.normalize(bp.LowerDistanceParameter());
                    gp_Pnt2d pa,pb;gp_Vec2d da,db;a.support->D1(u,pa,da);b.support->D1(v,pb,db);
                    const double scale=da.Magnitude()*db.Magnitude();
                    const double roundoff=64*std::numeric_limits<double>::epsilon()*std::max({1.,std::abs(p.x),std::abs(p.y),da.Magnitude(),db.Magnitude()});
                    if(a.contains(u) && b.contains(v) && roundoff<=tolerance/16 && ap.LowerDistance()<=roundoff && bp.LowerDistance()<=roundoff
                       && std::isfinite(scale) && scale>0 && std::abs(da.Crossed(db))<=64*std::numeric_limits<double>::epsilon()*scale)
                        return {Status::Success,{{p,u,v,Contact::Tangency}}, {}};
                }
            }
            return failed(Status::Ambiguous,"Tolerance-indistinguishable curve intervals cannot be split.");
        }
        Intersections result{Status::Success,{}, {}};
        for(int i=1;i<=intersection.NbPoints();++i) {
            const auto& ip=intersection.Intersector().Point(i);
            const double u=a.normalize(ip.ParamOnFirst()),v=b.normalize(ip.ParamOnSecond());
            const auto p=ip.Value();
            if(!a.contains(u) || !b.contains(v) || !reliable(p.X(),tolerance) || !reliable(p.Y(),tolerance)
               || a.support->Value(u).Distance(p)>tolerance || b.support->Value(v).Distance(p)>tolerance)
                return failed(Status::NumericalFailure,"Intersection domain or geometric residual could not be certified.");
            gp_Pnt2d pa,pb;gp_Vec2d da,db;a.support->D1(u,pa,da);b.support->D1(v,pb,db);
            const double scale=da.Magnitude()*db.Magnitude();
            if(!std::isfinite(scale) || scale<=std::numeric_limits<double>::min()) return failed(Status::Ambiguous,"Singular intersection derivative.");
            const double sine=std::abs(da.Crossed(db))/scale;
            const auto& ta=ip.TransitionOfFirst();const auto& tb=ip.TransitionOfSecond();
            const bool decided=ta.TransitionType()!=IntRes2d_Undecided && tb.TransitionType()!=IntRes2d_Undecided;
            const bool tangent=decided && (ta.IsTangent() || tb.IsTangent() || ta.TransitionType()==IntRes2d_Touch || tb.TransitionType()==IntRes2d_Touch);
            if(!decided || (tangent?sine>1.e-12:sine<=1.e-12))
                return failed(Status::Ambiguous,"Intersection transition and derivative directions cannot be certified together.");
            const Hit hit{point(p),u,v,tangent?Contact::Tangency:Contact::Crossing};
            bool duplicate=false;
            for(const auto& old:result.hits) if(distance(old.point,hit.point)<=tolerance) {
                if(std::abs(old.firstParameter-u)>a.roundoff() || std::abs(old.secondParameter-v)>b.roundoff() || old.contact!=hit.contact)
                    return failed(Status::Ambiguous,"Distinct intersection parameters are spatially unresolved.");
                duplicate=true;break;
            }
            if(!duplicate) result.hits.push_back(hit);
        }
        std::sort(result.hits.begin(),result.hits.end(),[&](const Hit& x,const Hit& y) {
            if(a.progress(x.firstParameter)!=a.progress(y.firstParameter)) return a.progress(x.firstParameter)<a.progress(y.firstParameter);
            return b.progress(x.secondParameter)<b.progress(y.secondParameter);
        });
        return result;
    } catch(const std::invalid_argument& e) {return failed(Status::Invalid,QString::fromUtf8(e.what()));}
      catch(const Standard_Failure&) {return failed(Status::NumericalFailure,"OCCT curve evaluation failed.");}
      catch(const std::exception& e) {return failed(Status::NumericalFailure,QString::fromUtf8(e.what()));}
}
SegmentSelection classifySegment(const Sketch::Geometry& geometry,const std::vector<double>& cuts,Sketch::Point cursor,double tolerance) {
    const auto fail=[](Status s,const QString& m) {return SegmentSelection{s,{},0,m};};
    if(!std::isfinite(tolerance) || tolerance<=0 || !reliable(cursor.x,tolerance) || !reliable(cursor.y,tolerance))
        return fail(Status::Invalid,"Invalid cursor or tolerance.");
    if(std::holds_alternative<Sketch::ConstructionPoint>(geometry)) return fail(Status::Unsupported,"A point has no curve domain.");
    try {
        const auto c=adapt(geometry,tolerance);
        if(!c.finite) return fail(Status::Unsupported,"Segment selection requires a finite curve.");
        std::vector<double> boundaries;
        for(double u:cuts) {
            if(!std::isfinite(u)) return fail(Status::Invalid,"Nonfinite cut parameter.");
            u=c.normalize(u);if(!c.contains(u)) return fail(Status::Invalid,"Cut lies outside the curve domain.");
            boundaries.push_back(c.progress(u));
        }
        if(!c.closed) {boundaries.push_back(0);boundaries.push_back(c.length());}
        std::sort(boundaries.begin(),boundaries.end());
        // Exact duplicate cuts are harmless. Distinct nearby cuts must reach
        // the ambiguity check below instead of being silently coalesced.
        boundaries.erase(std::unique(boundaries.begin(),boundaries.end()),boundaries.end());
        for(size_t i=1;i<boundaries.size();++i) if(c.support->Value(c.parameter(boundaries[i-1])).Distance(c.support->Value(c.parameter(boundaries[i])))<=tolerance)
            return fail(Status::Ambiguous,"Distinct boundaries are spatially unresolved.");
        if(c.closed && boundaries.size()>1 && c.support->Value(c.parameter(boundaries.front())).Distance(c.support->Value(c.parameter(boundaries.back())))<=tolerance)
            return fail(Status::Ambiguous,"Periodic boundaries are spatially unresolved across the seam.");
        Geom2dAPI_ProjectPointOnCurve projection(gp_Pnt2d(cursor.x,cursor.y),c.support,std::min(c.start,c.end),std::max(c.start,c.end));
        if(c.closed && projection.NbPoints()==0) return fail(Status::Ambiguous,"Closed-curve nearest parameter cannot be resolved.");
        std::vector<std::pair<double,double>> candidates;
        for(int i=1;i<=projection.NbPoints();++i) {
            const double u=c.normalize(projection.Parameter(i));
            if(c.contains(u)) candidates.emplace_back(projection.Distance(i),u);
        }
        for(double u:{c.start,c.end}) candidates.emplace_back(c.support->Value(u).Distance(gp_Pnt2d(cursor.x,cursor.y)),c.normalize(u));
        std::sort(candidates.begin(),candidates.end());
        const auto [best,u]=candidates.front();
        for(const auto& [d,v]:candidates) if(d-best<=tolerance && std::abs(u-v)>c.roundoff())
            return fail(Status::Ambiguous,"Cursor has multiple indistinguishable nearest parameters.");
        for(double d:boundaries) if(c.support->Value(u).Distance(c.support->Value(c.parameter(d)))<=tolerance)
            return fail(Status::Ambiguous,"Pick lies at a segment boundary.");
        if(boundaries.empty()) return {Status::Success,{c.start,c.end},u,{}};
        const double picked=c.progress(u);
        const auto upper=std::upper_bound(boundaries.begin(),boundaries.end(),picked);
        double lo,hi;
        if(c.closed) {
            lo=upper==boundaries.begin()?boundaries.back()-c.length():*(upper-1);
            hi=upper==boundaries.end()?boundaries.front()+c.length():*upper;
        } else {
            if(upper==boundaries.begin() || upper==boundaries.end()) return fail(Status::Ambiguous,"Pick has no interior interval.");
            lo=*(upper-1);hi=*upper;
        }
        return {Status::Success,{c.parameter(lo),c.parameter(hi)},u,{}};
    } catch(const std::invalid_argument& e) {return fail(Status::Invalid,QString::fromUtf8(e.what()));}
      catch(const Standard_Failure&) {return fail(Status::NumericalFailure,"OCCT segment classification failed.");}
      catch(const std::exception& e) {return fail(Status::NumericalFailure,QString::fromUtf8(e.what()));}
}
TrimResult trim(const Sketch::Geometry& source,const std::vector<Sketch::Geometry>& boundaries,Sketch::Point cursor,double tolerance) {
    const auto fail=[](Status status,const QString& message) {return TrimResult{status,{}, {},message};};
    if(!std::isfinite(tolerance) || tolerance<=0) return fail(Status::Invalid,"Invalid Trim tolerance.");
    if(std::holds_alternative<ConstructionPoint>(source) || std::holds_alternative<InfiniteLine>(source))
        return fail(Status::Unsupported,"Trim requires a finite curve; infinite construction lines can serve as boundaries.");
    try {
        const auto c=adapt(source,tolerance);
        const auto spline=Handle(Geom2d_BSplineCurve)::DownCast(c.support);
        if(!spline.IsNull()) {
            Geom2dAPI_InterCurveCurve self(c.bounded,tolerance);
            if(!self.Intersector().IsDone() || self.NbSegments()) return fail(Status::Ambiguous,"The spline is self-overlapping or cannot be certified.");
            for(int i=1;i<=self.NbPoints();++i) {
                const auto& hit=self.Intersector().Point(i);
                if(!c.closed || std::abs(c.normalize(hit.ParamOnFirst())-c.start)>c.roundoff()
                   || std::abs(c.normalize(hit.ParamOnSecond())-c.start)>c.roundoff())
                    return fail(Status::Ambiguous,"Self-intersecting splines do not supply an unambiguous Trim segment.");
            }
        }
        std::vector<double> cuts;bool tangent=false;
        for(const auto& boundary:boundaries) {
            if(std::holds_alternative<ConstructionPoint>(boundary)) continue;
            const auto result=SketchCurveOperations::intersect(source,boundary,tolerance);
            if(result.status!=Status::Success) return fail(result.status,"Trim boundary: "+result.message);
            for(const auto& hit:result.hits) {
                if(hit.contact==Contact::Crossing) cuts.push_back(hit.firstParameter);
                else tangent=true;
            }
        }
        std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
        const auto interior=[&](double u){return c.progress(u)>c.roundoff() && c.progress(u)<c.length()-c.roundoff();};
        if((c.closed && cuts.size()<2) || (!c.closed && std::none_of(cuts.begin(),cuts.end(),interior)))
            return fail(Status::Unsupported,tangent?"Trim needs crossing boundaries; tangent contact does not bound a removable segment."
                :c.closed?"A closed curve needs at least two distinct crossing intersections for Trim."
                :"No interior crossing intersection bounds a removable segment. Add a boundary curve.");
        const auto selected=classifySegment(source,cuts,cursor,tolerance);
        if(selected.status!=Status::Success) return fail(selected.status,selected.message);
        std::vector<Interval> kept;
        if(c.closed) kept.push_back({selected.interval.end,selected.interval.start+c.sign()*c.length()});
        else {
            if(selected.interval.start!=c.start) kept.push_back({c.start,selected.interval.start});
            if(selected.interval.end!=c.end) kept.push_back({selected.interval.end,c.end});
        }
        TrimResult result{Status::Success,{},selected.interval,{}};
        const auto append=[&](Interval interval) {
            const auto start=c.support->Value(interval.start),end=c.support->Value(interval.end);
            if(start.Distance(end)<=tolerance) throw std::runtime_error("A retained Trim fragment has unresolved or coincident endpoints.");
            Geometry fragment;
            if(std::holds_alternative<Segment>(source)) fragment=Segment{point(start),point(end)};
            else if(const auto* circle=std::get_if<Circle>(&source)) fragment=Arc{circle->center,circle->radius,interval.start,interval.end-interval.start};
            else if(const auto* arc=std::get_if<Arc>(&source)) fragment=Arc{arc->center,arc->radius,interval.start,interval.end-interval.start};
            else if(const auto* ellipse=std::get_if<Ellipse>(&source)) fragment=EllipticalArc{*ellipse,interval.start,interval.end-interval.start};
            else if(const auto* arc=std::get_if<EllipticalArc>(&source)) fragment=EllipticalArc{arc->ellipse,interval.start,interval.end-interval.start};
            else {
                BSpline exact;
                if(const auto* original=std::get_if<BSpline>(&source)) exact=*original;
                else {
                    exact.degree=spline->Degree();exact.periodic=spline->IsPeriodic();
                    for(int i=1;i<=spline->NbPoles();++i) {exact.poles.push_back(point(spline->Pole(i)));exact.weights.push_back(spline->Weight(i));}
                    for(int i=1;i<=spline->NbKnots();++i) {exact.knots.push_back(spline->Knot(i));exact.multiplicities.push_back(spline->Multiplicity(i));}
                }
                exact.first=std::min(interval.start,interval.end);exact.last=std::max(interval.start,interval.end);exact.reversed=interval.end<interval.start;
                fragment=std::move(exact);
            }
            Entity candidate;candidate.id="trim";candidate.workplaneId="local";candidate.role=Role::Construction;candidate.geometry=fragment;
            QString error;if(!valid(candidate,error)) throw std::runtime_error(error.toStdString());
            // Construct the exact bounded curve before exposing any candidate.
            const auto bounded=boundedCurve(fragment);
            const double lo=std::min(interval.start,interval.end),hi=std::max(interval.start,interval.end);
            if(bounded.IsNull() || (std::holds_alternative<Segment>(fragment)
               ? bounded->Value(bounded->FirstParameter()).Distance(start)>tolerance || bounded->Value(bounded->LastParameter()).Distance(end)>tolerance
               : bounded->Value(lo).Distance(c.support->Value(lo))>tolerance || bounded->Value(hi).Distance(c.support->Value(hi))>tolerance))
                throw std::runtime_error("Retained Trim fragment does not reproduce its exact support endpoints.");
            result.remaining.push_back(std::move(fragment));
        };
        for(auto interval:kept) {
            if(spline.IsNull() || !c.closed) {append(interval);continue;}
            // A periodic coefficient value stores one canonical knot-domain
            // interval. Keep its coefficients, splitting only at the existing
            // seam when the complementary retained path wraps across it.
            const double lo=spline->FirstParameter(),hi=spline->LastParameter(),period=c.period;
            while(interval.start<lo) {interval.start+=period;interval.end+=period;}
            while(interval.start>hi) {interval.start-=period;interval.end-=period;}
            if(interval.end>hi) {if(interval.start<hi) append({interval.start,hi});append({lo,interval.end-period});}
            else if(interval.end<lo) {if(interval.start>lo) append({interval.start,lo});append({hi,interval.end+period});}
            else append(interval);
        }
        if(result.remaining.empty()) return fail(Status::Ambiguous,"Trim did not retain a finite portion.");
        return result;
    } catch(const std::invalid_argument& e) {return fail(Status::Invalid,QString::fromUtf8(e.what()));}
      catch(const Standard_Failure&) {return fail(Status::NumericalFailure,"OCCT could not certify the Trim candidate.");}
      catch(const std::exception& e) {return fail(Status::NumericalFailure,QString::fromUtf8(e.what()));}
}

ExtensionEnds extensionEnds(const Sketch::Geometry& source,double tolerance) {
    const auto fail=[](Status status,const QString& message) {return ExtensionEnds{status,{}, {},message};};
    if(!std::isfinite(tolerance) || tolerance<=0) return fail(Status::Invalid,"Invalid Extend tolerance.");
    try {
        const auto c=adapt(source,tolerance);
        if(std::holds_alternative<Spline>(source)) return fail(Status::Unsupported,
            "Interpolating splines have no certified continuation beyond their interpolation domain. Trimmed coefficient fragments can reveal existing support only.");
        if(!std::holds_alternative<Segment>(source) && !std::holds_alternative<Arc>(source)
           && !std::holds_alternative<EllipticalArc>(source) && !std::holds_alternative<BSpline>(source))
            return fail(Status::Unsupported,"Extend requires a finite open segment, arc or trimmed coefficient B-spline; complete closed curves have no extension endpoints.");
        if(c.closed || distance(point(c.support->Value(c.start)),point(c.support->Value(c.end)))<=tolerance)
            return fail(Status::Unsupported,"Closed or unresolved curves have no distinct extension endpoints.");
        return {Status::Success,point(c.support->Value(c.start)),point(c.support->Value(c.end)),{}};
    } catch(const std::invalid_argument& e) {return fail(Status::Invalid,QString::fromUtf8(e.what()));}
      catch(const Standard_Failure&) {return fail(Status::NumericalFailure,"OCCT could not resolve the extension endpoints.");}
      catch(const std::exception& e) {return fail(Status::NumericalFailure,QString::fromUtf8(e.what()));}
}
ExtendResult extend(const Sketch::Geometry& source,Endpoint endpoint,const Sketch::Geometry& boundary,double tolerance) {
    const auto fail=[](Status status,const QString& message) {return ExtendResult{status,{},0,message};};
    if(endpoint!=Endpoint::Start && endpoint!=Endpoint::End) return fail(Status::Invalid,"Choose the start or end endpoint explicitly.");
    const auto ends=extensionEnds(source,tolerance);
    if(ends.status!=Status::Success) return fail(ends.status,ends.message);
    try {
        const auto c=adapt(source,tolerance),b=adapt(boundary,tolerance);
        if(sameSupport(c,b)) return fail(Status::Overlap,"The source and boundary share a coincident support; Extend requires an isolated crossing.");
        Geometry support;
        if(std::holds_alternative<Segment>(source)) {
            const auto line=Handle(Geom2d_Line)::DownCast(c.support);
            support=InfiniteLine{point(line->Location()),{line->Direction().X(),line->Direction().Y()}};
        } else if(const auto* a=std::get_if<Arc>(&source)) support=Circle{a->center,a->radius};
        else if(const auto* a=std::get_if<EllipticalArc>(&source)) support=a->ellipse;
        else {
            auto exact=std::get<BSpline>(source);const auto spline=bsplineCurve(exact);
            const double at=endpoint==Endpoint::Start?c.start:c.end;
            const double direction=c.sign()*(endpoint==Endpoint::Start?-1:1);
            if((direction>0 && at>=spline->LastParameter()-c.roundoff()) || (direction<0 && at<=spline->FirstParameter()+c.roundoff()))
                return fail(Status::Unsupported,"This B-spline endpoint already reaches its knot-domain limit; polynomial/rational extrapolation is not certified.");
            exact.first=spline->FirstParameter();exact.last=spline->LastParameter();support=std::move(exact);
        }
        const auto intersections=SketchCurveOperations::intersect(support,boundary,tolerance);
        if(intersections.status!=Status::Success) return fail(intersections.status,"Extend boundary: "+intersections.message);
        std::vector<std::pair<double,Point>> candidates;
        for(const auto& hit:intersections.hits) {
            double u=hit.firstParameter;
            if(std::holds_alternative<Arc>(source) || std::holds_alternative<EllipticalArc>(source)) {
                u=c.normalize(u);
                if(c.contains(u)) continue; // Original finite arc, including its ends.
                if(endpoint==Endpoint::Start) u-=c.sign()*c.period;
            } else {
                if(const auto* exact=std::get_if<BSpline>(&source);exact && exact->periodic && c.period>0) {
                    const double lo=c.support->FirstParameter(),hi=c.support->LastParameter();
                    if((std::abs(u-lo)<=c.roundoff() || std::abs(u-hi)<=c.roundoff())
                       && c.support->Value(lo).Distance(gp_Pnt2d(hit.point.x,hit.point.y))<=tolerance/16) {
                        // The intersector deduplicates a full periodic seam.
                        // Recover the alias on the explicitly selected side,
                        // still within the existing canonical knot domain.
                        u=c.sign()*(endpoint==Endpoint::Start?-1:1)>0?hi:lo;
                    }
                }
                const double progress=c.progress(u);
                if(endpoint==Endpoint::Start?progress>=-c.roundoff():progress<=c.length()+c.roundoff()) continue;
            }
            const auto old=endpoint==Endpoint::Start?ends.start:ends.end;
            if(distance(hit.point,old)<=tolerance) return fail(Status::Ambiguous,"The extension intersection cannot be separated from the selected endpoint within tolerance.");
            if(hit.contact!=Contact::Crossing) return fail(Status::Ambiguous,"Tangential boundary contact does not certify an Extend target. Select a crossing boundary.");
            if(c.support->Value(u).Distance(gp_Pnt2d(hit.point.x,hit.point.y))>tolerance)
                return fail(Status::NumericalFailure,"The extension target does not reproduce the original support within tolerance.");
            candidates.emplace_back(u,hit.point);
        }
        if(candidates.empty()) return fail(Status::Unsupported,"No crossing lies beyond the selected endpoint on the available support and finite boundary. Intersections inside the existing curve are ignored.");
        if(candidates.size()!=1) return fail(Status::Ambiguous,
            "Several crossings lie beyond the selected endpoint. Select a boundary portion with one crossing; Extend will not choose a distant solution.");
        const auto [u,target]=candidates.front();Geometry candidate=source;
        const double start=endpoint==Endpoint::Start?u:c.start,end=endpoint==Endpoint::End?u:c.end;
        if(auto* s=std::get_if<Segment>(&candidate)) {if(endpoint==Endpoint::Start)s->start=target;else s->end=target;}
        else if(auto* a=std::get_if<Arc>(&candidate)) {a->startAngle=start;a->sweepAngle=end-start;}
        else if(auto* a=std::get_if<EllipticalArc>(&candidate)) {a->startParameter=start;a->sweepParameter=end-start;}
        else {auto& s=std::get<BSpline>(candidate);s.first=std::min(start,end);s.last=std::max(start,end);}
        // Reuse the same validity, precision and domain checks used by Trim and
        // intersections. No support is fitted, re-interpolated or extrapolated.
        const auto checked=adapt(candidate,tolerance);
        // Segment parameters restart at the new first endpoint; all other
        // types retain their original support parameterization.
        if(checked.closed || (!std::holds_alternative<Segment>(candidate)
           && (checked.support->Value(checked.start).Distance(c.support->Value(start))>tolerance
               || checked.support->Value(checked.end).Distance(c.support->Value(end))>tolerance)))
            return fail(Status::NumericalFailure,"Extended curve endpoints could not be certified on their original support.");
        const auto fixedOriginal=endpoint==Endpoint::Start?ends.end:ends.start;
        const auto fixedNew=point(checked.support->Value(endpoint==Endpoint::Start?checked.end:checked.start));
        const auto changedNew=point(checked.support->Value(endpoint==Endpoint::Start?checked.start:checked.end));
        if(distance(fixedOriginal,fixedNew)>tolerance || distance(target,changedNew)>tolerance)
            return fail(Status::NumericalFailure,"Extend changed the fixed endpoint or missed its selected boundary.");
        return {Status::Success,std::move(candidate),u,{}};
    } catch(const std::invalid_argument& e) {return fail(Status::Invalid,QString::fromUtf8(e.what()));}
      catch(const Standard_Failure&) {return fail(Status::NumericalFailure,"OCCT could not certify the extension candidate.");}
      catch(const std::exception& e) {return fail(Status::NumericalFailure,QString::fromUtf8(e.what()));}
}

namespace {
BSpline coefficients(const Handle(Geom2d_BSplineCurve)& curve,double start,double end) {
    BSpline result;result.degree=curve->Degree();result.periodic=curve->IsPeriodic();
    result.first=std::min(start,end);result.last=std::max(start,end);result.reversed=end<start;
    for(int i=1;i<=curve->NbPoles();++i) {result.poles.push_back(point(curve->Pole(i)));result.weights.push_back(curve->Weight(i));}
    for(int i=1;i<=curve->NbKnots();++i) {result.knots.push_back(curve->Knot(i));result.multiplicities.push_back(curve->Multiplicity(i));}
    return result;
}
// Returns the parameter direction of B relative to A, or zero when a common
// support has not been proven. Reversal is a coefficient operation, not fitting.
int commonSupport(const Curve& a,const Curve& b) {
    if(sameSupport(a,b)) {
        const auto la=Handle(Geom2d_Line)::DownCast(a.support),lb=Handle(Geom2d_Line)::DownCast(b.support);
        if(!la.IsNull() && !lb.IsNull()) return la->Direction().Dot(lb->Direction())>0?1:-1;
        return 1;
    }
    const auto ca=Handle(Geom2d_Circle)::DownCast(a.support),cb=Handle(Geom2d_Circle)::DownCast(b.support);
    const auto ea=Handle(Geom2d_Ellipse)::DownCast(a.support),eb=Handle(Geom2d_Ellipse)::DownCast(b.support);
    const auto circleEllipse=[](const Handle(Geom2d_Circle)& c,const Handle(Geom2d_Ellipse)& e) {
        return !c.IsNull() && !e.IsNull() && c->Radius()==e->MajorRadius() && e->MajorRadius()==e->MinorRadius() && c->Location().IsEqual(e->Location(),0);
    };
    if(circleEllipse(ca,eb) || circleEllipse(cb,ea)) return 1;
    const auto sa=Handle(Geom2d_BSplineCurve)::DownCast(a.support),sb=Handle(Geom2d_BSplineCurve)::DownCast(b.support);
    if(!sa.IsNull() && !sb.IsNull()) {
        Curve reverse=b;reverse.support=sb->Reversed();
        if(sameSupport(a,reverse)) return -1;
    }
    return 0;
}
std::optional<double> parameter(const Curve& c,Point p,double tolerance) {
    Geom2dAPI_ProjectPointOnCurve projection(gp_Pnt2d(p.x,p.y),c.support);
    std::optional<double> result;
    for(int i=1;i<=projection.NbPoints();++i) if(projection.Distance(i)<=tolerance) {
        const double u=c.normalize(projection.Parameter(i));
        if(c.finite && !c.contains(u)) continue;
        if(result && std::abs(*result-u)>c.roundoff()) throw std::runtime_error("A boundary has multiple unresolved parameters on its source curve.");
        result=u;
    }
    return result;
}
}
SupportRelation supportRelation(const Sketch::Geometry& first,const Sketch::Geometry& second,double tolerance) {
    const auto fail=[](Status status,const QString& message) {return SupportRelation{status,false,false,false,false,{},message};};
    if(!std::isfinite(tolerance) || tolerance<=0) return fail(Status::Invalid,"Invalid support comparison tolerance.");
    try {
        const auto a=adapt(first,tolerance,false),b=adapt(second,tolerance,false);
        const int direction=commonSupport(a,b);
        SupportRelation result{Status::Success,false,false,false,false,{}, {}};
        if(!direction) return result;
        result.common=true;
        for(const auto* c:{&a,&b}) if(c->finite && !c->closed) {
            result.boundaries.push_back(point(c->support->Value(c->start)));result.boundaries.push_back(point(c->support->Value(c->end)));
        }
        if(!a.finite || a.closed) result.firstContainsSecond=true;
        if(!b.finite || b.closed) result.secondContainsFirst=true;
        if(!a.finite || !b.finite || a.closed || b.closed) {result.overlap=true;return result;}
        // Map one exact endpoint to the other support. Equal coefficient
        // parameterizations have unit scale, with a possible sign reversal.
        const auto p=point(b.support->Value(b.start));
        Curve unbounded=a;unbounded.finite=false;
        const auto mapped=parameter(unbounded,p,tolerance);
        if(!mapped) return fail(Status::Ambiguous,"Common support endpoint mapping cannot be certified.");
        const double a0=std::min(a.start,a.end),a1=std::max(a.start,a.end);
        double b0=std::min(*mapped,*mapped+direction*(b.end-b.start)),b1=std::max(*mapped,*mapped+direction*(b.end-b.start));
        if(a.period>0) {
            const double shift=std::floor((a0-b0)/a.period)*a.period;b0+=shift;b1+=shift;
        }
        const double noise=64*std::numeric_limits<double>::epsilon()*std::max({1.,std::abs(a0),std::abs(a1),std::abs(b0),std::abs(b1)});
        for(int k=a.period>0?-1:0;k<=(a.period>0?1:0);++k) {
            const double lo=b0+k*a.period,hi=b1+k*a.period;
            if(std::min(a1,hi)-std::max(a0,lo)>noise) result.overlap=true;
            if(lo>=a0-noise && hi<=a1+noise) result.firstContainsSecond=true;
            if(a0>=lo-noise && a1<=hi+noise) result.secondContainsFirst=true;
        }
        return result;
    } catch(const std::invalid_argument& e) {return fail(Status::Invalid,QString::fromUtf8(e.what()));}
      catch(const Standard_Failure&) {return fail(Status::NumericalFailure,"OCCT support comparison failed.");}
      catch(const std::exception& e) {return fail(Status::Ambiguous,QString::fromUtf8(e.what()));}
}
SplitResult split(const Sketch::Geometry& source,const std::vector<Sketch::Point>& boundaries,double tolerance) {
    const auto fail=[](Status status,const QString& message) {return SplitResult{status,{}, {},message};};
    if(!std::isfinite(tolerance) || tolerance<=0) return fail(Status::Invalid,"Invalid curve splitting tolerance.");
    try {
        // Preserve the established analytic Overdraw range. New conics/splines
        // retain the stricter editable-curve precision headroom checks.
        const auto c=adapt(source,tolerance,!supportedSketchToolCurve(source));
        const auto spline=Handle(Geom2d_BSplineCurve)::DownCast(c.support);
        if(!spline.IsNull()) {
            // Domain-based support reuse requires an injective support. Check
            // the complete coefficient support once per source, including
            // intersections outside a stored trim. The known periodic seam
            // is the only permitted repeated support point.
            Geom2dAPI_InterCurveCurve self(c.support,tolerance);
            if(!self.Intersector().IsDone() || self.NbSegments()) return fail(Status::Ambiguous,"Spline self-overlap cannot be certified for Overdraw.");
            for(int i=1;i<=self.NbPoints();++i) {
                const auto& hit=self.Intersector().Point(i);
                const double first=spline->FirstParameter(),last=spline->LastParameter();
                const double noise=64*std::numeric_limits<double>::epsilon()*std::max({1.,std::abs(first),std::abs(last)});
                const auto seam=[&](double a,double b){return std::abs(a-first)<=noise && std::abs(b-last)<=noise
                    && spline->Value(a).Distance(spline->Value(b))<=tolerance;};
                if(!spline->IsPeriodic() || !(seam(hit.ParamOnFirst(),hit.ParamOnSecond()) || seam(hit.ParamOnSecond(),hit.ParamOnFirst())))
                    return fail(Status::Ambiguous,"Self-intersecting spline supports, including outside a stored trim, do not supply certified Overdraw pieces.");
            }
        }
        if(c.finite && !c.closed && boundaries.empty() && distance(point(c.support->Value(c.start)),point(c.support->Value(c.end)))<=tolerance)
            return {Status::Success,{},"Tolerance-sized pieces excluded.",{}};
        std::vector<double> cuts;
        for(auto p:boundaries) {
            const auto resolvable=[&](double x) {
                if(!supportedSketchToolCurve(source)) return reliable(x,tolerance);
                const double a=std::abs(x);return std::isfinite(a) && std::nextafter(a,std::numeric_limits<double>::infinity())-a<=tolerance;
            };
            if(!resolvable(p.x) || !resolvable(p.y)) return fail(Status::NumericalFailure,"Overdraw boundary coordinates cannot resolve tolerance.");
            if(const auto u=parameter(c,p,tolerance)) cuts.push_back(*u);
        }
        if(c.finite && !c.closed) {cuts.push_back(c.start);cuts.push_back(c.end);}
        const auto progress=[&](double u){return c.finite?c.progress(u):u;};
        std::sort(cuts.begin(),cuts.end(),[&](double a,double b){return progress(a)<progress(b);});
        double scale=1;for(double u:cuts)scale=std::max(scale,std::abs(u));
        const double noise=64*std::numeric_limits<double>::epsilon()*scale;
        std::vector<double> unique;
        for(double u:cuts) {
            if(!unique.empty() && std::abs(u-unique.back())<=noise) {
                if(c.support->Value(u).Distance(c.support->Value(unique.back()))>tolerance) return fail(Status::Ambiguous,"Near-equal parameters have unresolved geometric separation.");
                continue;
            }
            if(!unique.empty() && c.support->Value(u).Distance(c.support->Value(unique.back()))<=tolerance) return fail(Status::Ambiguous,"Distinct Overdraw boundaries cannot be separated within tolerance.");
            unique.push_back(u);
        }
        if(c.closed && unique.size()>1 && c.support->Value(unique.front()).Distance(c.support->Value(unique.back()))<=tolerance)
            return fail(Status::Ambiguous,"Periodic Overdraw boundaries cannot be separated at the seam.");
        SplitResult result{Status::Success,{}, {},{}};
        if(c.closed && unique.size()<2) {result.notice="Closed curves need at least two distinct boundary points; full curves are not invented from one contact.";return result;}
        if(!c.finite) result.notice="unbounded line tails excluded; Workplane borders are not geometric limits.";
        if(c.closed) unique.push_back(unique.front()+c.sign()*c.period);
        const auto append=[&](double start,double end) {
            const Point a=point(c.support->Value(start)),b=point(c.support->Value(end));
            if(distance(a,b)<=tolerance) {result.notice="Tolerance-sized pieces excluded.";return;}
            Geometry geometry;
            if(start==c.start && end==c.end && c.finite && !c.closed) geometry=source;
            else if(std::holds_alternative<Segment>(source) || std::holds_alternative<InfiniteLine>(source)) geometry=Segment{a,b};
            else if(const auto* circle=std::get_if<Circle>(&source)) geometry=Arc{circle->center,circle->radius,start,end-start};
            else if(const auto* arc=std::get_if<Arc>(&source)) geometry=Arc{arc->center,arc->radius,start,end-start};
            else if(const auto* ellipse=std::get_if<Ellipse>(&source)) geometry=EllipticalArc{*ellipse,start,end-start};
            else if(const auto* arc=std::get_if<EllipticalArc>(&source)) geometry=EllipticalArc{arc->ellipse,start,end-start};
            else {auto exact=std::get_if<BSpline>(&source)?std::get<BSpline>(source):coefficients(spline,start,end);exact.first=std::min(start,end);exact.last=std::max(start,end);exact.reversed=end<start;geometry=std::move(exact);}
            Entity candidate;candidate.id="piece";candidate.workplaneId="local";candidate.geometry=geometry;QString error;
            if(!valid(candidate,error)) throw std::runtime_error(error.toStdString());
            if(!contains(geometry,a,tolerance) || !contains(geometry,b,tolerance) || !contains(source,a,tolerance) || !contains(source,b,tolerance))
                throw std::runtime_error("Overdraw piece endpoints escaped their original exact domain.");
            result.pieces.push_back({std::move(geometry),{start,end},a,b});
        };
        for(size_t i=1;i<unique.size();++i) {
            double start=unique[i-1],end=unique[i];
            if(spline.IsNull() || !c.closed) {append(start,end);continue;}
            const double lo=spline->FirstParameter(),hi=spline->LastParameter();
            while(start<lo) {start+=c.period;end+=c.period;}
            while(start>hi) {start-=c.period;end-=c.period;}
            if(end>hi) {if(start<hi)append(start,hi);append(lo,end-c.period);}
            else if(end<lo) {if(start>lo)append(start,lo);append(hi,end+c.period);}
            else append(start,end);
        }
        return result;
    } catch(const std::invalid_argument& e) {return fail(Status::Invalid,QString::fromUtf8(e.what()));}
      catch(const Standard_Failure&) {return fail(Status::NumericalFailure,"OCCT curve splitting failed.");}
      catch(const std::exception& e) {return fail(Status::Ambiguous,QString::fromUtf8(e.what()));}
}
Sketch::Geometry reversed(const Sketch::Geometry& geometry) {
    if(const auto* s=std::get_if<Segment>(&geometry)) return Segment{s->end,s->start};
    if(const auto* a=std::get_if<Arc>(&geometry)) return reversedArc(*a);
    if(const auto* a=std::get_if<EllipticalArc>(&geometry)) return reversedEllipticalArc(*a);
    if(const auto* s=std::get_if<BSpline>(&geometry)) {auto result=*s;result.reversed=!result.reversed;return result;}
    if(const auto* s=std::get_if<Spline>(&geometry)) {const auto curve=splineCurve(*s);return coefficients(curve,curve->LastParameter(),curve->FirstParameter());}
    throw std::invalid_argument("Overdraw traversal requires a bounded open curve piece.");
}

}
