#pragma once
#include "ConstructionGeometry.h"
#include <variant>
#include <QStringList>
#include <QUuid>

// Exact document values, independent of AIS, widgets and creation history.
namespace Sketch {
enum class Role { Construction, Profile };
struct Point { double x=0, y=0; bool operator==(const Point&) const = default; };
struct Segment { Point start, end{1,0}; bool operator==(const Segment&) const = default; };
struct InfiniteLine { Point origin, direction{1,0}; bool operator==(const InfiniteLine&) const = default; };
struct Circle { Point center; double radius=1; bool operator==(const Circle&) const = default; };
// Radians, signed sweep: positive is counterclockwise in Workplane XY.
struct Arc { Point center; double radius=1, startAngle=0, sweepAngle=1; bool operator==(const Arc&) const = default; };
using Geometry = std::variant<Segment, InfiniteLine, Circle, Arc>;
double directedArcSweep(double start,double end,bool clockwise);
Arc reversedArc(const Arc& arc);
struct Entity {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString workplaneId, name;
    Geometry geometry = Segment{};
    Role role = Role::Profile;
    bool visible=true, selectable=true;
    bool operator==(const Entity&) const = default;
};
QString typeName(const Geometry& geometry);
bool valid(const Entity& entity, QString& error);
bool validate(const std::vector<Entity>& entities, const std::vector<Construction::Object>& owners,
              const QStringList& otherIds, QString& error);
gp_Pnt worldPoint(const Point& point, const gp_Ax3& frame);
// Infinite lines are clipped for display only; persisted values remain unbounded.
TopoDS_Shape shape(const Entity& entity, const gp_Ax3& frame, double displayExtent=100);
}
