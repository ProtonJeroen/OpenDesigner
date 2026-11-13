#include "SketchSnapping.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <Geom2d_Ellipse.hxx>
#include <Geom2dAPI_ProjectPointOnCurve.hxx>
#include <Standard_Failure.hxx>
#include <gp_Ax2d.hxx>

namespace Sketch {
namespace {
constexpr double pi=3.14159265358979323846, turn=2*pi;
Point add(Point a,Point b) {return {a.x+b.x,a.y+b.y};}
Point sub(Point a,Point b) {return {a.x-b.x,a.y-b.y};}
Point mul(Point a,double k) {return {a.x*k,a.y*k};}
double dot(Point a,Point b) {return a.x*b.x+a.y*b.y;}
double cross(Point a,Point b) {return a.x*b.y-a.y*b.x;}
double length(Point p) {return std::hypot(p.x,p.y);}
double normalized(double a) {a=std::fmod(a,turn);return a<0?a+turn:a;}
struct Line {Point origin,direction;double end=std::numeric_limits<double>::infinity();bool finite=false;};
std::optional<Line> line(const Geometry& curve) {
    if(const auto* s=std::get_if<Segment>(&curve)) {
        const auto d=sub(s->end,s->start);const double size=length(d);
        if(!std::isfinite(size) || size<=1.e-12) return {};
        return Line{s->start,mul(d,1/size),size,true};
    }
    if(const auto* l=std::get_if<InfiniteLine>(&curve)) {
        const double size=length(l->direction);if(!std::isfinite(size) || size<=1.e-12) return {};
        return Line{l->origin,mul(l->direction,1/size)};
    }
    return {};
}
std::optional<Circle> circle(const Geometry& curve) {
    const auto validCircle=[](Point p,double r) {return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(r) && r>0;};
    if(const auto* c=std::get_if<Circle>(&curve);c && validCircle(c->center,c->radius)) return *c;
    if(const auto* a=std::get_if<Arc>(&curve);a && validCircle(a->center,a->radius) && std::isfinite(a->startAngle)
        && std::isfinite(a->sweepAngle) && std::abs(a->sweepAngle)>0 && std::abs(a->sweepAngle)<turn) return Circle{a->center,a->radius};
    return {};
}
Point onCircle(Circle c,double angle) {return {c.center.x+c.radius*std::cos(angle),c.center.y+c.radius*std::sin(angle)};}
std::optional<Ellipse> ellipse(const Geometry& g) {
    std::optional<Ellipse> e;
    if(const auto* support=std::get_if<Ellipse>(&g)) e=*support;
    if(const auto* arc=std::get_if<EllipticalArc>(&g)) {
        if(!std::isfinite(arc->startParameter) || !std::isfinite(arc->sweepParameter)
            || std::abs(arc->sweepParameter)<=1.e-12 || std::abs(arc->sweepParameter)>=turn) return {};
        e=arc->ellipse;
    }
    if(!e || !std::isfinite(e->center.x) || !std::isfinite(e->center.y) || !std::isfinite(e->axisAngle)
        || !std::isfinite(e->majorRadius) || !std::isfinite(e->minorRadius) || e->minorRadius<=1.e-9 || e->majorRadius<e->minorRadius) return {};
    return e;
}
std::array<long double,2> principal(const Ellipse& e,Point p,bool direction=false) {
    const long double x=static_cast<long double>(p.x)-(direction?0:e.center.x),y=static_cast<long double>(p.y)-(direction?0:e.center.y);
    const long double c=std::cos(e.axisAngle),s=std::sin(e.axisAngle);
    return {c*x+s*y,-s*x+c*y};
}
bool onArc(const Arc& a,Point p,double tolerance) {
    const double angle=std::atan2(p.y-a.center.y,p.x-a.center.x);
    const double progress=normalized(a.sweepAngle>0?angle-a.startAngle:a.startAngle-angle);
    const double angular=tolerance/std::max(a.radius,tolerance);
    return progress<=std::abs(a.sweepAngle)+angular || turn-progress<=angular;
}
void unique(std::vector<Point>& points,Point p,double tolerance) {
    if(std::isfinite(p.x) && std::isfinite(p.y) && std::none_of(points.begin(),points.end(),[&](Point q){return length(sub(p,q))<=tolerance;})) points.push_back(p);
}
std::vector<std::pair<double,double>> intervals(const Geometry& curve) {
    if(const auto* arc=std::get_if<Arc>(&curve)) {
        const double start=normalized(arc->sweepAngle>0?arc->startAngle:arc->startAngle+arc->sweepAngle);
        const double end=start+std::abs(arc->sweepAngle);
        if(end>turn) return {{start,turn},{0,end-turn}};
        return {{start,end}};
    }
    return {{0,turn}};
}
}
bool contains(const Geometry& curve,Point p,double tolerance) {
    if(!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(tolerance) || tolerance<=0) return false;
    if(const auto* point=std::get_if<ConstructionPoint>(&curve)) return length(sub(point->position,p))<=tolerance;
    if(const auto e=ellipse(curve)) {
        const auto t=ellipseParameter(*e,p);if(!t || length(sub(ellipsePoint(*e,*t),p))>tolerance) return false;
        if(const auto* arc=std::get_if<EllipticalArc>(&curve)) {
            const auto ends=ellipticalArcEndpoints(*arc);
            return ellipticalArcContainsParameter(*arc,*t,0) || length(sub(p,ends[0]))<=tolerance || length(sub(p,ends[1]))<=tolerance;
        }
        return true;
    }
    if(const auto l=line(curve)) {
        const auto d=sub(p,l->origin);const double t=dot(d,l->direction);
        return std::abs(cross(d,l->direction))<=tolerance && (!l->finite || (t>=-tolerance && t<=l->end+tolerance));
    }
    if(const auto c=circle(curve)) {
        if(std::abs(length(sub(p,c->center))-c->radius)>tolerance) return false;
        if(const auto* a=std::get_if<Arc>(&curve)) return onArc(*a,p,tolerance);
        return true;
    }
    return false;
}
Intersections intersect(const Geometry& a,const Geometry& b,double tolerance) {
    Intersections result;if(!std::isfinite(tolerance) || tolerance<=0) return result;
    // Standalone points supply snaps, not curve cuts or Overdraw boundaries.
    if(std::holds_alternative<ConstructionPoint>(a) || std::holds_alternative<ConstructionPoint>(b)) return result;
    const auto la=line(a),lb=line(b);const auto ca=circle(a),cb=circle(b);
    const auto take=[&](Point p) {if(contains(a,p,tolerance) && contains(b,p,tolerance)) unique(result.points,p,tolerance);};
    if(isElliptical(a) || isElliptical(b)) {
        const auto e=ellipse(isElliptical(a)?a:b);const auto l=isElliptical(a)?lb:la;
        if(!e) {result.kind=IntersectionKind::NumericalFailure;return result;}
        if(!l) {result.kind=IntersectionKind::Unsupported;return result;}
        const auto o=principal(*e,l->origin),d=principal(*e,l->direction,true);
        const long double x=o[0]/e->majorRadius,y=o[1]/e->minorRadius,dx=d[0]/e->majorRadius,dy=d[1]/e->minorRadius;
        const long double metric=std::hypot(dx,dy),perpendicular=(x*dy-y*dx)/metric;
        if(!std::isfinite(metric) || metric==0 || !std::isfinite(perpendicular)) {result.kind=IntersectionKind::NumericalFailure;return result;}
        long double h2=(1-std::abs(perpendicular))*(1+std::abs(perpendicular));
        const long double noise=64*std::numeric_limits<double>::epsilon()*std::max(1.L,std::abs(perpendicular));
        if(std::abs(h2)<=noise) h2=0;
        if(h2>=-noise) {
            const long double middle=-(x*dx+y*dy)/(metric*metric),half=std::sqrt(std::max(0.L,h2))/metric;
            for(long double t:{middle-half,middle+half}) {
                const Point p{double(static_cast<long double>(l->origin.x)+t*l->direction.x),double(static_cast<long double>(l->origin.y)+t*l->direction.y)};
                if(!std::isfinite(p.x) || !std::isfinite(p.y)) {result.points.clear();result.kind=IntersectionKind::NumericalFailure;return result;}
                take(p);
            }
        }
        result.kind=result.points.empty()?IntersectionKind::None:IntersectionKind::Finite;return result;
    }
    if(la && lb) {
        const long double denominator=static_cast<long double>(la->direction.x)*lb->direction.y-static_cast<long double>(la->direction.y)*lb->direction.x;const auto delta=sub(lb->origin,la->origin);
        if(std::abs(denominator)>64*std::numeric_limits<long double>::epsilon()) take(add(la->origin,mul(la->direction,double((static_cast<long double>(delta.x)*lb->direction.y-static_cast<long double>(delta.y)*lb->direction.x)/denominator))));
        else if(std::abs(cross(delta,la->direction))<=tolerance) {
            if(!la->finite || !lb->finite) result.kind=IntersectionKind::Coincident;
            else {
                const double t0=dot(delta,la->direction),t1=dot(sub(add(lb->origin,mul(lb->direction,lb->end)),la->origin),la->direction);
                const double low=std::max(0.0,std::min(t0,t1)),high=std::min(la->end,std::max(t0,t1));
                if(high-low>tolerance) result.kind=IntersectionKind::Coincident;
                else if(high>=low-tolerance) take(add(la->origin,mul(la->direction,low)));
            }
        }
    } else if((la && cb) || (lb && ca)) {
        const auto l=la?*la:*lb;const auto c=cb?*cb:*ca;
        const auto d=sub(c.center,l.origin);
        const long double perpendicular=static_cast<long double>(d.x)*l.direction.y-static_cast<long double>(d.y)*l.direction.x;
        const long double r=c.radius;
        long double height2=(r-std::abs(perpendicular))*(r+std::abs(perpendicular));
        if(std::abs(perpendicular)<=r+tolerance) {
            height2=std::max(0.L,height2);const double t=dot(d,l.direction),h=double(std::sqrt(height2));
            take(add(l.origin,mul(l.direction,t-h)));take(add(l.origin,mul(l.direction,t+h)));
        }
    } else if(ca && cb) {
        const auto delta=sub(cb->center,ca->center);const double d=length(delta);
        if(d<=tolerance && std::abs(ca->radius-cb->radius)<=tolerance) {
            const double angular=tolerance/std::max(ca->radius,tolerance);
            for(const auto& i:intervals(a)) for(const auto& j:intervals(b)) {
                const double low=std::max(i.first,j.first),high=std::min(i.second,j.second);
                if(high-low>angular) result.kind=IntersectionKind::Coincident;
                else if(high>=low-angular) take(onCircle(*ca,low));
            }
            // Periodic seam is also a shared endpoint.
            take(onCircle(*ca,0));
        } else if(d>tolerance && d<=ca->radius+cb->radius+tolerance && d>=std::abs(ca->radius-cb->radius)-tolerance) {
            const long double r=ca->radius,s=cb->radius,dist=d;
            const long double along=((r-s)*(r+s)+dist*dist)/(2*dist);
            const double height=double(std::sqrt(std::max(0.L,(r-along)*(r+along))));
            const auto direction=mul(delta,1/d),base=add(ca->center,mul(direction,double(along))),offset=Point{-direction.y*height,direction.x*height};
            take(add(base,offset));take(sub(base,offset));
        }
    }
    if(result.kind==IntersectionKind::Coincident) result.points.clear();
    else result.kind=result.points.empty()?IntersectionKind::None:IntersectionKind::Finite;
    return result;
}
std::vector<Point> tangentPoints(Point anchor,const Geometry& curve,double tolerance) {
    std::vector<Point> points;const auto c=circle(curve);if(!c) return points;
    const auto q=sub(anchor,c->center);const double d=length(q),r=c->radius;
    if(d<r-tolerance || d<=tolerance) return points;
    const double along=r*r/(d*d),height=r*std::sqrt(std::max(0.0,(d-r)*(d+r)))/(d*d);
    const auto base=add(c->center,mul(q,along)),offset=Point{-q.y*height,q.x*height};
    for(Point p:{add(base,offset),sub(base,offset)}) if(contains(curve,p,tolerance)) unique(points,p,tolerance);
    return points;
}
std::optional<Point> nearestPoint(const Geometry& curve,Point point) {
    if(!std::isfinite(point.x) || !std::isfinite(point.y)) return {};
    if(const auto e=ellipse(curve)) {
        const auto local=principal(*e,point);const double x=double(local[0]),y=double(local[1]);
        if(!std::isfinite(x) || !std::isfinite(y)) return {};
        std::vector<double> parameters;
        if(x==0 && y==0) parameters={0,pi/2,pi,3*pi/2};
        else try {
            const Handle(Geom2d_Ellipse) support=new Geom2d_Ellipse(gp_Ax2d(gp_Pnt2d(0,0),gp_Dir2d(1,0)),e->majorRadius,e->minorRadius);
            Geom2dAPI_ProjectPointOnCurve projection(gp_Pnt2d(x,y),support);
            if(projection.NbPoints()==0) return {};
            for(int i=1;i<=projection.NbPoints();++i) parameters.push_back(projection.Parameter(i));
        } catch(const Standard_Failure&) {return {};}
        std::optional<Point> best;long double distance=std::numeric_limits<long double>::infinity();
        const auto consider=[&](double t) {
            const auto p=ellipsePoint(*e,t);if(!std::isfinite(p.x) || !std::isfinite(p.y)) return;
            const long double dx=static_cast<long double>(p.x)-point.x,dy=static_cast<long double>(p.y)-point.y,dist=std::hypot(dx,dy);
            if(dist<distance) {distance=dist;best=p;}
        };
        for(double t:parameters) if(!std::holds_alternative<EllipticalArc>(curve) || ellipticalArcContainsParameter(std::get<EllipticalArc>(curve),t,0)) consider(t);
        if(const auto* arc=std::get_if<EllipticalArc>(&curve)) {consider(arc->startParameter);consider(arc->startParameter+arc->sweepParameter);}
        return best;
    }
    if(const auto* p=std::get_if<ConstructionPoint>(&curve)) {
        if(std::isfinite(p->position.x) && std::isfinite(p->position.y)) return p->position;
        return {};
    }
    if(const auto l=line(curve)) {
        double t=dot(sub(point,l->origin),l->direction);if(l->finite) t=std::clamp(t,0.0,l->end);
        return add(l->origin,mul(l->direction,t));
    }
    if(const auto c=circle(curve)) {
        const auto delta=sub(point,c->center);const double distance=length(delta);
        if(distance>1.e-12) {const auto p=add(c->center,mul(delta,c->radius/distance));if(contains(curve,p)) return p;}
        if(const auto* a=std::get_if<Arc>(&curve)) {
            const auto first=onCircle(*c,a->startAngle),last=onCircle(*c,a->startAngle+a->sweepAngle);
            return length(sub(first,point))<=length(sub(last,point))?first:last;
        }
    }
    return {};
}
namespace {
struct FeaturePoint { Point point; SnapKind kind; const char* label; PointFeature feature; };
bool snapEligible(const Entity& e) {return supportedSketchToolCurve(e.geometry) || isElliptical(e.geometry);}
// Cursor-independent features, in the historical snap order.
std::vector<FeaturePoint> featurePoints(const Entity& e) {
    std::vector<FeaturePoint> result;
    if(const auto* point=std::get_if<ConstructionPoint>(&e.geometry)) {
        if(std::isfinite(point->position.x) && std::isfinite(point->position.y))
            result.push_back({point->position,SnapKind::ConstructionPoint,"Construction point",PointFeature::Position});
        return result;
    }
    if(!snapEligible(e)) return result;
    if(const auto support=ellipse(e.geometry)) {
        result.push_back({support->center,SnapKind::Center,"Ellipse center",PointFeature::Center});
        if(const auto* arc=std::get_if<EllipticalArc>(&e.geometry)) {
            const auto ends=ellipticalArcEndpoints(*arc);
            result.push_back({ends[0],SnapKind::Endpoint,"Elliptical arc start",PointFeature::Start});
            result.push_back({ends[1],SnapKind::Endpoint,"Elliptical arc end",PointFeature::End});
            result.push_back({ellipsePoint(*support,arc->startParameter+arc->sweepParameter/2),SnapKind::Midpoint,"Elliptical arc parametric midpoint",PointFeature::Midpoint});
        }
    }
    if(const auto* s=std::get_if<Segment>(&e.geometry)) {
        result.push_back({s->start,SnapKind::Endpoint,"Endpoint",PointFeature::Start});
        result.push_back({s->end,SnapKind::Endpoint,"Endpoint",PointFeature::End});
        result.push_back({mul(add(s->start,s->end),0.5),SnapKind::Midpoint,"Midpoint",PointFeature::Midpoint});
    }
    if(const auto* arc=std::get_if<Arc>(&e.geometry)) {
        const Circle c{arc->center,arc->radius};
        result.push_back({onCircle(c,arc->startAngle),SnapKind::Endpoint,"Arc endpoint",PointFeature::Start});
        result.push_back({onCircle(c,arc->startAngle+arc->sweepAngle),SnapKind::Endpoint,"Arc endpoint",PointFeature::End});
        result.push_back({onCircle(c,arc->startAngle+arc->sweepAngle/2),SnapKind::Midpoint,"Arc midpoint",PointFeature::Midpoint});
    }
    if(const auto c=circle(e.geometry)) result.push_back({c->center,SnapKind::Center,"Center",PointFeature::Center});
    return result;
}
}
std::optional<Point> resolvePoint(const std::vector<Entity>& entities,const PointReference& reference,QString& error) {
    const auto find=[&](const QString& id)->const Entity* {
        const auto it=std::find_if(entities.begin(),entities.end(),[&](const Entity& e){return e.id==id;});
        return it==entities.end()?nullptr:&*it;
    };
    const auto* entity=find(reference.entity);
    if(!entity) {error=QStringLiteral("The referenced Sketch entity no longer exists; pick it again.");return {};}
    if(!entity->visible || !entity->selectable) {error=QStringLiteral("Hidden or locked Sketch entities cannot supply a reference.");return {};}
    if(reference.feature==PointFeature::Intersection) {
        const auto* other=find(reference.other);
        if(!other || other->workplaneId!=entity->workplaneId) {error=QStringLiteral("The referenced intersection curve no longer exists; pick it again.");return {};}
        if(!other->visible || !other->selectable) {error=QStringLiteral("Hidden or locked Sketch entities cannot supply a reference.");return {};}
        if(!snapEligible(*entity) || !snapEligible(*other)) {error=QStringLiteral("These Sketch curves do not supply intersections.");return {};}
        const auto points=intersect(entity->geometry,other->geometry).points;
        if(reference.index<0 || size_t(reference.index)>=points.size()) {error=QStringLiteral("The referenced intersection no longer exists; pick it again.");return {};}
        error.clear();return points[size_t(reference.index)];
    }
    for(const auto& f:featurePoints(*entity)) if(f.feature==reference.feature) {error.clear();return f.point;}
    error=QStringLiteral("The referenced Sketch point no longer exists on this entity; pick it again.");return {};
}
std::vector<SnapTarget> snapTargets(const std::vector<Entity>& entities,const QString& workplaneId,Point cursor,std::optional<Point> anchor,bool intersections) {
    std::vector<SnapTarget> targets;std::vector<const Entity*> eligible;
    const auto addTarget=[&](Point p,SnapKind kind,const char* label){targets.push_back({p,kind,QString::fromLatin1(label)});};
    for(const auto& e:entities) if(e.workplaneId==workplaneId && e.visible && e.selectable) {
        for(const auto& f:featurePoints(e)) targets.push_back({f.point,f.kind,QString::fromLatin1(f.label),PointReference{e.id,{},f.feature,0}});
        if(!snapEligible(e)) continue;
        eligible.push_back(&e);
        if(const auto c=circle(e.geometry)) {
            if(anchor) {
                for(const auto p:tangentPoints(*anchor,e.geometry)) addTarget(p,SnapKind::Tangent,"Tangent");
                if(contains(e.geometry,*anchor)) {
                    const auto radial=sub(*anchor,c->center);const auto tangent=Point{-radial.y/c->radius,radial.x/c->radius};
                    const double t=dot(sub(cursor,*anchor),tangent);
                    if(std::abs(t)>1.e-7) addTarget(add(*anchor,mul(tangent,t)),SnapKind::Tangent,"Tangent direction");
                }
            }
        }
        if(const auto p=nearestPoint(e.geometry,cursor)) addTarget(*p,SnapKind::Nearest,circle(e.geometry) || isElliptical(e.geometry)?"On curve":"On line");
    }
    if(intersections) for(size_t i=0;i<eligible.size();++i) for(size_t j=i+1;j<eligible.size();++j) {
        const auto points=intersect(eligible[i]->geometry,eligible[j]->geometry).points;
        for(size_t k=0;k<points.size();++k)
            targets.push_back({points[k],SnapKind::Intersection,QStringLiteral("Intersection"),PointReference{eligible[i]->id,eligible[j]->id,PointFeature::Intersection,int(k)}});
    }
    return targets;
}
std::optional<SnapTarget> closestSnap(const std::vector<SnapTarget>& targets,Point cursor,double radius,const Projector& project) {
    if(!std::isfinite(radius) || radius<=0) return {};
    std::optional<SnapTarget> best;double distance=radius;bool strong=false;
    for(const auto& target:targets) {
        const auto screen=project(target.point);const double d=length(sub(screen,cursor));
        const bool isStrong=target.kind!=SnapKind::Nearest;
        if(d>radius || !std::isfinite(d)) continue;
        if(!best || (isStrong && !strong) || (isStrong==strong && (d<distance-1.e-9
           || (std::abs(d-distance)<1.e-9 && int(target.kind)<int(best->kind))))) {
            best=target;distance=d;strong=isStrong;
        }
    }
    return best;
}
}
