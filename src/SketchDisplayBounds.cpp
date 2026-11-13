#include "SketchDisplayBounds.h"
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pln.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <type_traits>

namespace Sketch {
namespace {
constexpr double pi=std::numbers::pi;
void include(DisplayRectangle& r,Point p) {
    r.minX=std::min(r.minX,p.x);r.maxX=std::max(r.maxX,p.x);
    r.minY=std::min(r.minY,p.y);r.maxY=std::max(r.maxY,p.y);
}
DisplayRectangle at(Point p) {return {p.x,p.y,p.x,p.y};}
Point closestLinePoint(const InfiniteLine& line,const DisplayRectangle& base) {
    const long double x=std::midpoint(static_cast<long double>(base.minX),static_cast<long double>(base.maxX));
    const long double y=std::midpoint(static_cast<long double>(base.minY),static_cast<long double>(base.maxY));
    const long double nx=-line.direction.y,ny=line.direction.x;
    const auto normalCoordinate=[&](long double px,long double py) {
        const long double a=nx*px,b=ny*py;
        // Compensate the two products before subtracting nearly equal terms:
        // translating the stored origin far along the line must not add extent.
        return (a+b)+(std::fma(nx,px,-a)+std::fma(ny,py,-b));
    };
    const long double distance=(normalCoordinate(line.origin.x,line.origin.y)-normalCoordinate(x,y))/(nx*nx+ny*ny);
    return {double(std::fma(nx,distance,x)),double(std::fma(ny,distance,y))};
}
DisplayRectangle conicBounds(const Ellipse& e,std::optional<std::pair<double,double>> domain) {
    const auto point=[&](double t) {return ellipsePoint(e,t);};
    const double c=std::cos(e.axisAngle),s=std::sin(e.axisAngle);
    if(!domain) {
        const double x=std::hypot(e.majorRadius*c,e.minorRadius*s);
        const double y=std::hypot(e.majorRadius*s,e.minorRadius*c);
        return {e.center.x-x,e.center.y-y,e.center.x+x,e.center.y+y};
    }
    const auto [start,sweep]=*domain;
    auto r=at(point(start));include(r,point(start+sweep));
    const double tx=std::atan2(-e.minorRadius*s,e.majorRadius*c);
    const double ty=std::atan2(e.minorRadius*c,e.majorRadius*s);
    for(double t:{tx,tx+pi,ty,ty+pi})
        if(ellipticalArcContainsParameter({e,start,sweep},t)) include(r,point(t));
    return r;
}
// Long-double intermediate differences avoid overflow/cancellation in bounds
// at remote local coordinates. Padding also keeps zero-width data drawable.
void expand(double& low,double& high) {
    const long double span=static_cast<long double>(high)-low;
    const long double half=std::max(10.L,span*0.6L); // 20 mm minimum; 10% on each side
    const long double center=static_cast<long double>(low)+span/2;
    const auto limit=static_cast<long double>(std::numeric_limits<double>::max());
    low=static_cast<double>(std::max(-limit,center-half));
    high=static_cast<double>(std::min(limit,center+half));
    // At very large coordinates, 10 mm may be smaller than a double ULP.
    if(low==high) {
        low=std::nextafter(low,-std::numeric_limits<double>::infinity());
        high=std::nextafter(high,std::numeric_limits<double>::infinity());
    }
}
}
namespace {
void minimumOpening(double& low,double& high) {
    const long double span=static_cast<long double>(high)-low;
    if(!(span<WorkplaneMinimumOpening)) return;
    const long double center=static_cast<long double>(low)+span/2,half=WorkplaneMinimumOpening/2;
    const auto limit=static_cast<long double>(std::numeric_limits<double>::max());
    low=static_cast<double>(std::max(-limit,center-half));high=static_cast<double>(std::min(limit,center+half));
}
DisplayRectangle withMinimumOpening(DisplayRectangle r) {minimumOpening(r.minX,r.maxX);minimumOpening(r.minY,r.maxY);return r;}
}
std::optional<DisplayRectangle> finiteBounds(const Geometry& geometry) {
    return std::visit([](const auto& g)->std::optional<DisplayRectangle> {
        using T=std::decay_t<decltype(g)>;
        if constexpr(std::is_same_v<T,InfiniteLine>) return {};
        else if constexpr(std::is_same_v<T,Segment>) {auto r=at(g.start);include(r,g.end);return r;}
        else if constexpr(std::is_same_v<T,ConstructionPoint>) return at(g.position);
        else if constexpr(std::is_same_v<T,Circle>) return conicBounds({g.center,g.radius,g.radius,0},{});
        else if constexpr(std::is_same_v<T,Arc>) return conicBounds({g.center,g.radius,g.radius,0},{{g.startAngle,g.sweepAngle}});
        else if constexpr(std::is_same_v<T,Ellipse>) return conicBounds(g,{});
        else if constexpr(std::is_same_v<T,EllipticalArc>) return conicBounds(g.ellipse,{{g.startParameter,g.sweepParameter}});
        else {static_assert(!sizeof(T),"Handle every Sketch type explicitly.");}
    },geometry);
}
DisplayRectangle displayRectangle(const Construction::Object& owner,const std::vector<Entity>& entities) {
    std::optional<DisplayRectangle> bounds;
    for(const auto& e:entities) if(e.workplaneId==owner.id) if(const auto r=finiteBounds(e.geometry)) {
        if(bounds) {include(*bounds,{r->minX,r->minY});include(*bounds,{r->maxX,r->maxY});} else bounds=r;
    }
    const bool finite=bounds.has_value();
    if(!bounds) bounds=DisplayRectangle{owner.displayOffset.X()-owner.extent,owner.displayOffset.Y()-owner.extent,
                                       owner.displayOffset.X()+owner.extent,owner.displayOffset.Y()+owner.extent};
    auto base=*bounds;
    if(finite) {expand(base.minX,base.maxX);expand(base.minY,base.maxY);}
    base=withMinimumOpening(base);
    bool outside=false;
    for(const auto& e:entities) if(e.workplaneId==owner.id) if(const auto* line=std::get_if<InfiniteLine>(&e.geometry)) {
        if(clipLine(*line,base)) continue; // Already visible, even on a long/narrow frame.
        // Use one fixed base for every line: order-independent and no repeated
        // padding. Only the perpendicular offset contributes, never its origin.
        include(*bounds,closestLinePoint(*line,base));outside=true;
    }
    if(!outside) return base;
    expand(bounds->minX,bounds->maxX);expand(bounds->minY,bounds->maxY);return withMinimumOpening(*bounds);
}
std::array<gp_Pnt,4> displayCorners(const DisplayRectangle& r,const gp_Ax3& frame) {
    return {worldPoint({r.minX,r.minY},frame),worldPoint({r.maxX,r.minY},frame),
            worldPoint({r.maxX,r.maxY},frame),worldPoint({r.minX,r.maxY},frame)};
}
DisplayRectangle borderedDisplayRectangle(const DisplayRectangle& r) {
    return {r.minX-WorkplaneBorderWidth,r.minY-WorkplaneBorderWidth,r.maxX+WorkplaneBorderWidth,r.maxY+WorkplaneBorderWidth};
}
DisplayRectangle displayOuterRectangle(const Construction::Object& owner,const std::vector<Entity>& entities) {
    return borderedDisplayRectangle(displayRectangle(owner,entities));
}
TopoDS_Shape workplaneBorder(const DisplayRectangle& opening,const gp_Ax3& frame) {
    const auto wire=[&](const DisplayRectangle& r) {
        BRepBuilderAPI_MakePolygon polygon;
        for(const auto& p:displayCorners(r,frame)) polygon.Add(p);
        polygon.Close();return polygon.Wire();
    };
    BRepBuilderAPI_MakeFace border(gp_Pln(frame),wire(borderedDisplayRectangle(opening)),Standard_True);
    border.Add(TopoDS::Wire(wire(opening).Reversed()));
    return border.Shape();
}
std::optional<Segment> clipLine(const InfiniteLine& line,const DisplayRectangle& r) {
    // Re-anchor near the rectangle before computing slab parameters. A remote
    // line origin must not collapse the finite t interval through cancellation.
    long double x=line.origin.x,y=line.origin.y;
    if(std::abs(line.direction.x)>=std::abs(line.direction.y) && line.direction.x!=0) {
        const long double ratio=static_cast<long double>(line.direction.y)/line.direction.x;
        x=std::midpoint(static_cast<long double>(r.minX),static_cast<long double>(r.maxX));
        y=std::fma(x,ratio,std::fma(-static_cast<long double>(line.origin.x),ratio,static_cast<long double>(line.origin.y)));
    } else if(line.direction.y!=0) {
        const long double ratio=static_cast<long double>(line.direction.x)/line.direction.y;
        y=std::midpoint(static_cast<long double>(r.minY),static_cast<long double>(r.maxY));
        x=std::fma(y,ratio,std::fma(-static_cast<long double>(line.origin.y),ratio,static_cast<long double>(line.origin.x)));
    } else return {};
    long double first=-std::numeric_limits<long double>::infinity(),last=-first;
    const auto slab=[&](long double origin,double direction,double low,double high) {
        if(direction==0) return origin>=low && origin<=high;
        long double a=(static_cast<long double>(low)-origin)/direction;
        long double b=(static_cast<long double>(high)-origin)/direction;
        if(a>b) std::swap(a,b);
        first=std::max(first,a);last=std::min(last,b);return first<last;
    };
    if(!slab(x,line.direction.x,r.minX,r.maxX) || !slab(y,line.direction.y,r.minY,r.maxY)
       || !std::isfinite(first) || !std::isfinite(last)) return {};
    const auto point=[&](long double t) {return Point{
        std::clamp(double(std::fma(t,static_cast<long double>(line.direction.x),x)),r.minX,r.maxX),
        std::clamp(double(std::fma(t,static_cast<long double>(line.direction.y),y)),r.minY,r.maxY)};};
    const auto a=point(first),b=point(last);
    if(std::hypot(b.x-a.x,b.y-a.y)<=1.e-7) return {};
    return Segment{a,b};
}
TopoDS_Shape displayShape(const Entity& entity,const gp_Ax3& frame,const DisplayRectangle& rectangle) {
    if(const auto* line=std::get_if<InfiniteLine>(&entity.geometry)) {
        if(const auto clipped=clipLine(*line,rectangle))
            return BRepBuilderAPI_MakeEdge(worldPoint(clipped->start,frame),worldPoint(clipped->end,frame)).Shape();
        // Empty compound is safe for AIS and retains the stable entity/owner.
        TopoDS_Compound empty;BRep_Builder builder;builder.MakeCompound(empty);return empty;
    }
    return shape(entity,frame);
}
}
