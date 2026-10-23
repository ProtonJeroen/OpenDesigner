#include "SketchConstruction.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Sketch {
namespace {
Point add(Point a,Point b) {return {a.x+b.x,a.y+b.y};}
Point sub(Point a,Point b) {return {a.x-b.x,a.y-b.y};}
Point mul(Point a,double s) {return {a.x*s,a.y*s};}
double length(Point a) {return std::hypot(a.x,a.y);}
double cross(Point a,Point b) {return a.x*b.y-a.y*b.x;}
bool finite(Point a) {return std::isfinite(a.x) && std::isfinite(a.y);}
bool validCurve(const Geometry& geometry,QString& error) {
    Entity e;e.workplaneId="calculation";e.role=Role::Construction;e.geometry=geometry;return valid(e,error);
}
bool validSettings(LineOutput output,double tolerance,QString& error) {
    if((output!=LineOutput::Infinite && output!=LineOutput::Finite) || !std::isfinite(tolerance) || tolerance<=0) {
        error="Invalid construction-line output or tolerance.";return false;
    }
    return true;
}
Circle support(const Geometry& g) {
    if(const auto* c=std::get_if<Circle>(&g)) return *c;
    const auto& a=std::get<Arc>(g);return {a.center,a.radius};
}
}
bool straightReference(const Geometry& g) {return std::holds_alternative<Segment>(g) || std::holds_alternative<InfiniteLine>(g);}
bool circularReference(const Geometry& g) {return std::holds_alternative<Circle>(g) || std::holds_alternative<Arc>(g);}
std::vector<Entity> missingConstructionAxes(const std::vector<Entity>& entities,const QString& owner,bool localX,bool localY) {
    std::vector<Entity> result;
    if(owner.isEmpty()) return result;
    for(bool x:{true,false}) {
        if(!(x?localX:localY)) continue;
        const InfiniteLine axis{{0,0},x?Point{1,0}:Point{0,1}};
        const bool exists=std::any_of(entities.begin(),entities.end(),[&](const Entity& e) {
            return e.workplaneId==owner && e.role==Role::Construction
                && std::holds_alternative<InfiniteLine>(e.geometry)
                && intersect(e.geometry,axis).kind==IntersectionKind::Coincident;
        });
        if(exists) continue;
        Entity entity;entity.workplaneId=owner;entity.role=Role::Construction;entity.geometry=axis;
        entity.name=x?QStringLiteral("Local X axis"):QStringLiteral("Local Y axis");result.push_back(entity);
    }
    return result;
}
bool offsetConstructionLine(const Geometry& source,double distance,LineOutput output,
                            std::optional<LineInterval> interval,Geometry& result,QString& error,double tolerance) {
    error.clear();
    if(!validSettings(output,tolerance,error) || !validCurve(source,error)) return false;
    if(!straightReference(source)) {error="Offset needs a straight 2D line reference.";return false;}
    if(!std::isfinite(distance) || std::abs(distance)<=tolerance) {error="Enter a finite nonzero offset larger than the geometric tolerance.";return false;}
    Point origin,direction;
    const auto* segment=std::get_if<Segment>(&source);
    if(segment) {origin=segment->start;const auto d=sub(segment->end,origin);direction=mul(d,1/length(d));}
    else {const auto& line=std::get<InfiniteLine>(source);origin=line.origin;direction=line.direction;}
    const auto shift=Point{-direction.y*distance,direction.x*distance};
    Geometry candidate;
    if(output==LineOutput::Infinite) candidate=InfiniteLine{add(origin,shift),direction};
    else if(segment) candidate=Segment{add(segment->start,shift),add(segment->end,shift)};
    else {
        if(!interval || !std::isfinite(interval->start) || !std::isfinite(interval->length) || interval->length<=tolerance) {
            error="A finite offset of an infinite source needs an explicit start along the source and a positive length.";return false;
        }
        const auto start=add(add(origin,shift),mul(direction,interval->start));
        candidate=Segment{start,add(start,mul(direction,interval->length))};
    }
    if(!validCurve(candidate,error)) return false;
    // Refuse loss of the requested displacement at coordinates beyond double precision.
    const auto p=std::holds_alternative<Segment>(candidate)?std::get<Segment>(candidate).start:std::get<InfiniteLine>(candidate).origin;
    bool precise=std::abs(cross(direction,sub(p,origin))-distance)<=tolerance;
    if(const auto* span=std::get_if<Segment>(&candidate)) {
        precise=precise && std::abs(cross(direction,sub(span->end,origin))-distance)<=tolerance;
        const auto wanted=segment?sub(segment->end,segment->start):mul(direction,interval->length);
        precise=precise && length(sub(sub(span->end,span->start),wanted))<=tolerance;
        if(!segment) precise=precise && std::abs((p.x-origin.x)*direction.x+(p.y-origin.y)*direction.y-interval->start)<=tolerance;
    }
    if(!precise) {
        error="Offset cannot be represented within the geometric tolerance at these coordinates.";return false;
    }
    result=candidate;return true;
}
bool commonTangents(const Geometry& first,const Geometry& second,std::vector<CommonTangent>& result,QString& error,double tolerance) {
    result.clear();error.clear();
    if(!validSettings(LineOutput::Infinite,tolerance,error) || !validCurve(first,error) || !validCurve(second,error)) return false;
    if(!circularReference(first) || !circularReference(second)) {error="Common tangent needs two Construction circles or arcs.";return false;}
    const auto a=support(first),b=support(second);const auto delta=sub(b.center,a.center);const double distance=length(delta);
    if(!std::isfinite(distance)) {error="Circle separation is outside the supported numeric range.";return false;}
    if(distance<=tolerance) {
        // On an exactly shared support, trimmed arcs can meet only at endpoints.
        // Reuse the analytic domain intersection rather than assuming every
        // coincident support always has a continuum of valid tangent contacts.
        if(a.center==b.center && a.radius==b.radius) {
            // A short overlap still has infinitely many tangent contacts. Do
            // not collapse it to an endpoint using the normal acquisition
            // tolerance; classify overlap at angular machine precision first.
            const double domainTolerance=std::min(tolerance,std::numeric_limits<double>::epsilon()*a.radius);
            if(intersect(first,second,domainTolerance).kind==IntersectionKind::Coincident) {
                error="Coincident circles or overlapping arc domains have no unique common tangent solution.";return false;
            }
            const auto contacts=intersect(first,second,tolerance);
            if(contacts.kind==IntersectionKind::Finite) {
                for(auto p:contacts.points) {
                    const auto radial=sub(p,a.center);const double size=length(radial);
                    result.push_back({p,p,{-radial.y/size,radial.x/size},false});
                }
                if(!result.empty()) return true;
            }
            if(contacts.kind==IntersectionKind::None) {error="The arcs share a circular support but have no common contact within both domains.";return false;}
        }
        error=std::abs(a.radius-b.radius)<=tolerance
            ?"Coincident circular supports have no unique common tangent solution."
            :"Concentric circles have no common tangent.";return false;
    }
    const auto u=mul(delta,1/distance);std::vector<CommonTangent> candidates;
    // Unit line normal n satisfies n·(C2-C1)=r1-sign*r2.
    // Long-double products avoid cancellation near internal/external contact.
    for(int sign:{1,-1}) {
        const long double radius=static_cast<long double>(a.radius)-sign*static_cast<long double>(b.radius),d=distance;
        const long double h2=(d-radius)*(d+radius);
        if(h2<0) continue; // Do not invent a tangent for a near miss.
        const double along=double(radius/d),height=double(std::sqrt(h2)/d);
        for(int side:{1,-1}) {
            if(side==-1 && height==0) continue;
            const Point normal{along*u.x-side*height*u.y,along*u.y+side*height*u.x};
            const auto p=add(a.center,mul(normal,a.radius)),q=add(b.center,mul(normal,sign*b.radius));
            if(!finite(p) || !finite(q) || !contains(first,p,tolerance) || !contains(second,q,tolerance)) continue;
            const auto span=sub(q,p);const double size=length(span);
            Point direction{-normal.y,normal.x};
            if(size>tolerance) direction=mul(span,1/size);
            const Geometry line=InfiniteLine{p,direction};
            const double radialA=cross(direction,sub(a.center,p)),radialB=cross(direction,sub(b.center,p));
            if(!validCurve(line,error) || !contains(line,q,tolerance)
                || std::abs(std::abs(radialA)-a.radius)>tolerance || std::abs(std::abs(radialB)-b.radius)>tolerance) continue;
            const bool duplicate=std::any_of(candidates.begin(),candidates.end(),[&](const auto& c) {
                return length(sub(c.first,p))<=tolerance && length(sub(c.second,q))<=tolerance
                    && std::abs(cross(c.direction,direction))<=tolerance/std::max({a.radius,b.radius,tolerance});
            });
            if(!duplicate) candidates.push_back({p,q,direction,sign==-1});
        }
    }
    if(candidates.empty()) {error="No unique common tangent lies on both curve domains (nested circles, restricted arcs or unresolved numeric precision).";return false;}
    result=std::move(candidates);error.clear();return true;
}
bool commonTangentLine(const CommonTangent& tangent,LineOutput output,Geometry& result,QString& error,double tolerance) {
    error.clear();if(!validSettings(output,tolerance,error)) return false;
    if(!finite(tangent.first) || !finite(tangent.second)) {error="Invalid tangent contact points.";return false;}
    Geometry candidate;
    if(output==LineOutput::Infinite) {
        candidate=InfiniteLine{tangent.first,tangent.direction};
        if(!validCurve(candidate,error) || !contains(candidate,tangent.second,tolerance)) {error="Invalid common tangent direction.";return false;}
    } else {
        if(length(sub(tangent.second,tangent.first))<=tolerance) {error="The contact points coincide: this solution has no finite length. Choose Infinite.";return false;}
        candidate=Segment{tangent.first,tangent.second};
    }
    if(!validCurve(candidate,error)) return false;
    result=candidate;return true;
}
}
