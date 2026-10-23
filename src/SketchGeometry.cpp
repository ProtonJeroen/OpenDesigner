#include "SketchGeometry.h"
#include <QSet>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <gp_Circ.hxx>
#include <cmath>
#include <type_traits>

namespace Sketch {
namespace {
constexpr double pi=3.14159265358979323846;
bool finite(Point p) {return std::isfinite(p.x) && std::isfinite(p.y);}
}
double directedArcSweep(double start,double end,bool clockwise) {
    double sweep=std::fmod(clockwise?start-end:end-start,2*pi);
    if(sweep<0) sweep+=2*pi;
    return clockwise?-sweep:sweep;
}
Arc reversedArc(const Arc& arc) {return {arc.center,arc.radius,arc.startAngle+arc.sweepAngle,-arc.sweepAngle};}
QString typeName(const Geometry& geometry) {
    return std::visit([](const auto& g)->QString {
        using T=std::decay_t<decltype(g)>;
        if constexpr(std::is_same_v<T,Segment>) return QStringLiteral("Line");
        if constexpr(std::is_same_v<T,InfiniteLine>) return QStringLiteral("Infinite Line");
        if constexpr(std::is_same_v<T,Circle>) return QStringLiteral("Circle");
        return QStringLiteral("Arc");
    },geometry);
}
bool valid(const Entity& entity,QString& error) {
    if(entity.id.isEmpty() || entity.workplaneId.isEmpty()
       || (entity.role!=Role::Construction && entity.role!=Role::Profile)) {
        error=QStringLiteral("Invalid 2D identity, Workplane or geometry role.");return false;
    }
    const bool ok=std::visit([&](const auto& g) {
        using T=std::decay_t<decltype(g)>;
        if constexpr(std::is_same_v<T,Segment>) {
            const double distance=std::hypot(g.end.x-g.start.x,g.end.y-g.start.y);
            return finite(g.start) && finite(g.end) && std::isfinite(distance) && distance>1.e-9;
        } else if constexpr(std::is_same_v<T,InfiniteLine>) {
            return entity.role==Role::Construction && finite(g.origin) && finite(g.direction)
                && std::abs(std::hypot(g.direction.x,g.direction.y)-1)<1.e-12;
        } else {
            bool result=finite(g.center) && std::isfinite(g.radius) && g.radius>1.e-9;
            if constexpr(std::is_same_v<T,Arc>) result=result && std::isfinite(g.startAngle)
                && std::isfinite(g.sweepAngle) && std::abs(g.sweepAngle)>1.e-12 && std::abs(g.sweepAngle)<2*pi;
            return result;
        }
    },entity.geometry);
    if(!ok) {error=QStringLiteral("Invalid or degenerate 2D geometry (infinite lines must be Construction).");return false;}
    error.clear();return true;
}
bool validate(const std::vector<Entity>& entities,const std::vector<Construction::Object>& owners,
              const QStringList& otherIds,QString& error) {
    QSet<QString> ids(otherIds.begin(),otherIds.end()),planes;
    for(const auto& object:owners) if(object.kind==Construction::Kind::Workplane) planes.insert(object.id);
    for(const auto& entity:entities) {
        if(!valid(entity,error)) return false;
        if(!planes.contains(entity.workplaneId)) {error=QStringLiteral("2D geometry has no owning Workplane.");return false;}
        if(ids.contains(entity.id)) {error=QStringLiteral("Duplicate document object ID.");return false;}
        ids.insert(entity.id);
    }
    error.clear();return true;
}
gp_Pnt worldPoint(const Point& p,const gp_Ax3& frame) {
    return frame.Location().Translated(gp_Vec(frame.XDirection())*p.x+gp_Vec(frame.YDirection())*p.y);
}
TopoDS_Shape shape(const Entity& entity,const gp_Ax3& frame,double displayExtent) {
    return std::visit([&](const auto& g)->TopoDS_Shape {
        using T=std::decay_t<decltype(g)>;
        if constexpr(std::is_same_v<T,Segment>) return BRepBuilderAPI_MakeEdge(worldPoint(g.start,frame),worldPoint(g.end,frame)).Shape();
        else if constexpr(std::is_same_v<T,InfiniteLine>) {
            const Point a{g.origin.x-displayExtent*g.direction.x,g.origin.y-displayExtent*g.direction.y};
            const Point b{g.origin.x+displayExtent*g.direction.x,g.origin.y+displayExtent*g.direction.y};
            return BRepBuilderAPI_MakeEdge(worldPoint(a,frame),worldPoint(b,frame)).Shape();
        } else {
            gp_Circ circle(gp_Ax2(worldPoint(g.center,frame),frame.Direction(),frame.XDirection()),g.radius);
            if constexpr(std::is_same_v<T,Circle>) return BRepBuilderAPI_MakeEdge(circle).Shape();
            else {
                const auto edge=g.sweepAngle>0 ? BRepBuilderAPI_MakeEdge(circle,g.startAngle,g.startAngle+g.sweepAngle).Shape()
                    : BRepBuilderAPI_MakeEdge(circle,g.startAngle+g.sweepAngle,g.startAngle).Shape().Reversed();
                return edge;
            }
        }
    },entity.geometry);
}
}
