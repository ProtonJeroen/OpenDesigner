#pragma once
#include "ConstructionGeometry.h"
#include <variant>
#include <Geom2d_BSplineCurve.hxx>
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
// Append new alternatives: persisted tags 0–4 must keep their existing meaning.
struct ConstructionPoint { Point position; bool operator==(const ConstructionPoint&) const = default; };
// Exact local conic: center + a*cos(t)*u + b*sin(t)*v; u is rotated by
// axisAngle and v is its CCW perpendicular. t is not the polar angle.
struct Ellipse {
    Point center;
    double majorRadius=2, minorRadius=1, axisAngle=0;
    bool operator==(const Ellipse&) const = default;
};
struct EllipticalArc {
    Ellipse ellipse;
    double startParameter=0, sweepParameter=1; // Positive = CCW; no full turns.
    bool operator==(const EllipticalArc&) const = default;
};
// Interpolation data remains editable; the evaluated curve is derived on demand.
// Closed curves are periodic and omit a repeated last point. End tangents are
// optional directed vectors for open curves; their magnitudes are respected.
struct Spline {
    std::vector<Point> points;
    bool closed=false;
    std::optional<Point> startTangent, endTangent;
    bool operator==(const Spline&) const = default;
};
// Exact projected B-spline coefficients; no fitted through-points or source link.
struct BSpline {
    int degree=3;
    std::vector<Point> poles;
    std::vector<double> weights,knots;
    std::vector<int> multiplicities;
    bool periodic=false,reversed=false;
    double first=0,last=1;
    bool operator==(const BSpline&) const = default;
};
Handle(Geom2d_BSplineCurve) bsplineCurve(const BSpline& spline);
using Geometry = std::variant<Segment, InfiniteLine, Circle, Arc, ConstructionPoint, Ellipse, EllipticalArc, Spline, BSpline>;
// Throws on invalid or unresolved interpolation; callers stage candidates.
Handle(Geom2d_BSplineCurve) splineCurve(const Spline& spline);
bool validSpline(const Spline& spline,QString& error);
double directedArcSweep(double start,double end,bool clockwise);
Arc reversedArc(const Arc& arc);
Point ellipsePoint(const Ellipse& ellipse,double parameter);
Point ellipseDerivative(const Ellipse& ellipse,double parameter);
// Inverse support parameter; the center has no unique parameter. This does not
// certify that point lies on the curve (use its evaluated residual for that).
std::optional<double> ellipseParameter(const Ellipse& ellipse,Point point);
bool ellipticalArcContainsParameter(const EllipticalArc& arc,double parameter,double angularTolerance=1.e-12);
std::array<Point,2> ellipticalArcEndpoints(const EllipticalArc& arc);
EllipticalArc reversedEllipticalArc(const EllipticalArc& arc);
bool isElliptical(const Geometry& geometry);
// Phase 6A.1 curve tools only support the original analytic line/circle types.
bool supportedSketchToolCurve(const Geometry& geometry);
struct Entity {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString workplaneId, name;
    Geometry geometry = Segment{};
    Role role = Role::Profile;
    bool visible=true, selectable=true;
    bool operator==(const Entity&) const = default;
};
bool hasConstructionPoints(const std::vector<Entity>& entities);
bool hasEllipses(const std::vector<Entity>& entities);
QString typeName(const Geometry& geometry);
bool valid(const Entity& entity, QString& error);
bool validate(const std::vector<Entity>& entities, const std::vector<Construction::Object>& owners,
              const QStringList& otherIds, QString& error);
gp_Pnt worldPoint(const Point& point, const gp_Ax3& frame);
// Infinite lines are clipped for display only; persisted values remain unbounded.
TopoDS_Shape shape(const Entity& entity, const gp_Ax3& frame, double displayExtent=100);
}
