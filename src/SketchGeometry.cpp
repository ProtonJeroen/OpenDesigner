#include "SketchGeometry.h"
#include <QSet>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRep_Builder.hxx>
#include <Precision.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <Geom_Ellipse.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Edge.hxx>
#include <stdexcept>
#include <cmath>
#include <type_traits>
#include <algorithm>

namespace Sketch {
namespace {
constexpr double pi=3.14159265358979323846;
bool finite(Point p) {return std::isfinite(p.x) && std::isfinite(p.y);}
template<class> constexpr bool unhandled=false;
bool validEllipse(const Ellipse& e) {
    return finite(e.center) && std::isfinite(e.majorRadius) && std::isfinite(e.minorRadius)
        && std::isfinite(e.axisAngle) && e.minorRadius>1.e-9 && e.majorRadius>=e.minorRadius;
}
double normalized(double t) {t=std::fmod(t,2*pi);return t<0?t+2*pi:t;}
}
double directedArcSweep(double start,double end,bool clockwise) {
    double sweep=std::fmod(clockwise?start-end:end-start,2*pi);
    if(sweep<0) sweep+=2*pi;
    return clockwise?-sweep:sweep;
}
Arc reversedArc(const Arc& arc) {return {arc.center,arc.radius,arc.startAngle+arc.sweepAngle,-arc.sweepAngle};}
Point ellipsePoint(const Ellipse& e,double t) {
    const double x=e.majorRadius*std::cos(t),y=e.minorRadius*std::sin(t);
    const double c=std::cos(e.axisAngle),s=std::sin(e.axisAngle);
    return {e.center.x+c*x-s*y,e.center.y+s*x+c*y};
}
Point ellipseDerivative(const Ellipse& e,double t) {
    const double x=-e.majorRadius*std::sin(t),y=e.minorRadius*std::cos(t);
    const double c=std::cos(e.axisAngle),s=std::sin(e.axisAngle);
    return {c*x-s*y,s*x+c*y};
}
std::optional<double> ellipseParameter(const Ellipse& e,Point point) {
    if(!validEllipse(e) || !finite(point)) return {};
    const long double dx=static_cast<long double>(point.x)-e.center.x,dy=static_cast<long double>(point.y)-e.center.y;
    const long double c=std::cos(e.axisAngle),s=std::sin(e.axisAngle);
    const long double x=(c*dx+s*dy)/e.majorRadius,y=(-s*dx+c*dy)/e.minorRadius;
    if(!std::isfinite(x) || !std::isfinite(y) || (x==0 && y==0)) return {};
    return normalized(double(std::atan2(y,x)));
}
bool ellipticalArcContainsParameter(const EllipticalArc& arc,double t,double tolerance) {
    if(!std::isfinite(t) || !std::isfinite(arc.startParameter) || !std::isfinite(arc.sweepParameter)
       || !std::isfinite(tolerance) || tolerance<0 || std::abs(arc.sweepParameter)<=1.e-12
       || std::abs(arc.sweepParameter)>=2*pi) return false;
    const double delta=normalized(t)-normalized(arc.startParameter);
    const double progress=normalized(arc.sweepParameter>0?delta:-delta);
    return progress<=std::abs(arc.sweepParameter)+tolerance || 2*pi-progress<=tolerance;
}
std::array<Point,2> ellipticalArcEndpoints(const EllipticalArc& arc) {
    return {ellipsePoint(arc.ellipse,arc.startParameter),ellipsePoint(arc.ellipse,arc.startParameter+arc.sweepParameter)};
}
EllipticalArc reversedEllipticalArc(const EllipticalArc& arc) {
    return {arc.ellipse,arc.startParameter+arc.sweepParameter,-arc.sweepParameter};
}
bool isElliptical(const Geometry& geometry) {
    return std::holds_alternative<Ellipse>(geometry) || std::holds_alternative<EllipticalArc>(geometry);
}
bool supportedSketchToolCurve(const Geometry& geometry) {
    return std::holds_alternative<Segment>(geometry) || std::holds_alternative<InfiniteLine>(geometry)
        || std::holds_alternative<Circle>(geometry) || std::holds_alternative<Arc>(geometry);
}
bool hasConstructionPoints(const std::vector<Entity>& entities) {
    return std::any_of(entities.begin(),entities.end(),[](const auto& e){return std::holds_alternative<ConstructionPoint>(e.geometry);});
}
bool hasEllipses(const std::vector<Entity>& entities) {
    return std::any_of(entities.begin(),entities.end(),[](const auto& e){return isElliptical(e.geometry);});
}
QString typeName(const Geometry& geometry) {
    return std::visit([](const auto& g)->QString {
        using T=std::decay_t<decltype(g)>;
        if constexpr(std::is_same_v<T,Segment>) return QStringLiteral("Line");
        else if constexpr(std::is_same_v<T,InfiniteLine>) return QStringLiteral("Infinite Line");
        else if constexpr(std::is_same_v<T,Circle>) return QStringLiteral("Circle");
        else if constexpr(std::is_same_v<T,ConstructionPoint>) return QStringLiteral("Construction Point");
        else if constexpr(std::is_same_v<T,Arc>) return QStringLiteral("Arc");
        else if constexpr(std::is_same_v<T,Ellipse>) return QStringLiteral("Ellipse");
        else if constexpr(std::is_same_v<T,EllipticalArc>) return QStringLiteral("Elliptical Arc");
        else static_assert(unhandled<T>,"Handle the new Sketch type explicitly.");
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
        } else if constexpr(std::is_same_v<T,ConstructionPoint>) {
            return entity.role==Role::Construction && finite(g.position);
        } else if constexpr(std::is_same_v<T,Ellipse>) {
            return validEllipse(g);
        } else if constexpr(std::is_same_v<T,EllipticalArc>) {
            const double end=g.startParameter+g.sweepParameter;
            return validEllipse(g.ellipse) && std::isfinite(g.startParameter) && std::isfinite(g.sweepParameter)
                && std::abs(g.sweepParameter)>1.e-12 && std::abs(g.sweepParameter)<2*pi
                && std::isfinite(end) && end!=g.startParameter;
        } else if constexpr(std::is_same_v<T,Circle> || std::is_same_v<T,Arc>) {
            bool result=finite(g.center) && std::isfinite(g.radius) && g.radius>1.e-9;
            if constexpr(std::is_same_v<T,Arc>) result=result && std::isfinite(g.startAngle)
                && std::isfinite(g.sweepAngle) && std::abs(g.sweepAngle)>1.e-12 && std::abs(g.sweepAngle)<2*pi;
            return result;
        } else static_assert(unhandled<T>,"Handle the new Sketch type explicitly.");
    },entity.geometry);
    if(!ok) {error=QStringLiteral("Invalid or degenerate 2D geometry (infinite lines and points must be Construction).");return false;}
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
        } else if constexpr(std::is_same_v<T,ConstructionPoint>) {
            return BRepBuilderAPI_MakeVertex(worldPoint(g.position,frame)).Shape();
        } else if constexpr(std::is_same_v<T,Ellipse> || std::is_same_v<T,EllipticalArc>) {
            const Ellipse& e=[&]() -> const Ellipse& {if constexpr(std::is_same_v<T,Ellipse>) return g;else return g.ellipse;}();
            if(!validEllipse(e)) throw std::invalid_argument("Invalid ellipse support.");
            const gp_Vec axis=gp_Vec(frame.XDirection())*std::cos(e.axisAngle)+gp_Vec(frame.YDirection())*std::sin(e.axisAngle);
            const gp_Elips ellipse(gp_Ax2(worldPoint(e.center,frame),frame.Direction(),gp_Dir(axis)),e.majorRadius,e.minorRadius);
            if constexpr(std::is_same_v<T,Ellipse>) return BRepBuilderAPI_MakeEdge(ellipse).Shape();
            else {
                QString error;if(!valid(entity,error)) throw std::invalid_argument(error.toStdString());
                const double first=std::min(g.startParameter,g.startParameter+g.sweepParameter);
                const double last=std::max(g.startParameter,g.startParameter+g.sweepParameter);
                const Handle(Geom_Ellipse) curve=new Geom_Ellipse(ellipse);
                // MakeEdge can classify nearly-full intervals as closed and
                // merge endpoints. Build this exact trimmed presentation edge
                // explicitly; the document's proper arc must remain an arc.
                auto a=BRepBuilderAPI_MakeVertex(curve->Value(first)).Vertex();
                auto b=BRepBuilderAPI_MakeVertex(curve->Value(last)).Vertex();
                BRep_Builder builder;TopoDS_Edge edge;
                builder.MakeEdge(edge,curve,Precision::Confusion());
                builder.Range(edge,first,last);
                a.Orientation(TopAbs_FORWARD);b.Orientation(TopAbs_REVERSED);
                builder.Add(edge,a);builder.Add(edge,b);
                builder.UpdateVertex(a,first,edge,Precision::Confusion());
                builder.UpdateVertex(b,last,edge,Precision::Confusion());
                return g.sweepParameter>0?edge:edge.Reversed();
            }
        } else if constexpr(std::is_same_v<T,Circle> || std::is_same_v<T,Arc>) {
            gp_Circ circle(gp_Ax2(worldPoint(g.center,frame),frame.Direction(),frame.XDirection()),g.radius);
            if constexpr(std::is_same_v<T,Circle>) return BRepBuilderAPI_MakeEdge(circle).Shape();
            else {
                const auto edge=g.sweepAngle>0 ? BRepBuilderAPI_MakeEdge(circle,g.startAngle,g.startAngle+g.sweepAngle).Shape()
                    : BRepBuilderAPI_MakeEdge(circle,g.startAngle+g.sweepAngle,g.startAngle).Shape().Reversed();
                return edge;
            }
        } else static_assert(unhandled<T>,"Handle the new Sketch type explicitly.");
    },entity.geometry);
}
}
