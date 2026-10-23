#include "SketchSnapping.h"
#include <algorithm>
#include <cmath>
#include <limits>

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
    const auto la=line(a),lb=line(b);const auto ca=circle(a),cb=circle(b);
    const auto take=[&](Point p) {if(contains(a,p,tolerance) && contains(b,p,tolerance)) unique(result.points,p,tolerance);};
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
std::vector<SnapTarget> snapTargets(const std::vector<Entity>& entities,const QString& workplaneId,Point cursor,std::optional<Point> anchor,bool intersections) {
    std::vector<SnapTarget> targets;std::vector<const Entity*> eligible;
    const auto addTarget=[&](Point p,SnapKind kind,const char* label){targets.push_back({p,kind,QString::fromLatin1(label)});};
    for(const auto& e:entities) if(e.workplaneId==workplaneId && e.visible && e.selectable) {
        eligible.push_back(&e);
        if(const auto* s=std::get_if<Segment>(&e.geometry)) {
            addTarget(s->start,SnapKind::Endpoint,"Endpoint");addTarget(s->end,SnapKind::Endpoint,"Endpoint");
            addTarget(mul(add(s->start,s->end),0.5),SnapKind::Midpoint,"Midpoint");
        }
        if(const auto* arc=std::get_if<Arc>(&e.geometry)) {
            const Circle c{arc->center,arc->radius};
            addTarget(onCircle(c,arc->startAngle),SnapKind::Endpoint,"Arc endpoint");addTarget(onCircle(c,arc->startAngle+arc->sweepAngle),SnapKind::Endpoint,"Arc endpoint");
            addTarget(onCircle(c,arc->startAngle+arc->sweepAngle/2),SnapKind::Midpoint,"Arc midpoint");
        }
        if(const auto c=circle(e.geometry)) {
            addTarget(c->center,SnapKind::Center,"Center");
            if(anchor) {
                for(const auto p:tangentPoints(*anchor,e.geometry)) addTarget(p,SnapKind::Tangent,"Tangent");
                if(contains(e.geometry,*anchor)) {
                    const auto radial=sub(*anchor,c->center);const auto tangent=Point{-radial.y/c->radius,radial.x/c->radius};
                    const double t=dot(sub(cursor,*anchor),tangent);
                    if(std::abs(t)>1.e-7) addTarget(add(*anchor,mul(tangent,t)),SnapKind::Tangent,"Tangent direction");
                }
            }
        }
        if(const auto p=nearestPoint(e.geometry,cursor)) addTarget(*p,SnapKind::Nearest,circle(e.geometry)?"On curve":"On line");
    }
    if(intersections) for(size_t i=0;i<eligible.size();++i) for(size_t j=i+1;j<eligible.size();++j)
        for(Point p:intersect(eligible[i]->geometry,eligible[j]->geometry).points) addTarget(p,SnapKind::Intersection,"Intersection");
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
