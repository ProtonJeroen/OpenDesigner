#pragma once
#include <QString>
#include <gp_Ax3.hxx>
#include <gp_Vec.hxx>
#include <TopoDS_Shape.hxx>
#include <vector>
#include <optional>
#include <array>

namespace Construction {
enum class Kind { Workplane, Axis, Point };
struct Object {
    QString id, name;
    Kind kind = Kind::Point;
    gp_Ax3 frame;
    bool visible = true, selectable = true;
    double extent = 100.0; // Presentation only; never limits analytic geometry.
    gp_Vec displayOffset; // Local frame coordinates; analytic origin remains unchanged.
};
enum class ReferenceKind { Point, Plane, Axis, Line, Edge, Circle, Cylinder };
struct Reference {
    ReferenceKind kind = ReferenceKind::Point;
    gp_Ax3 frame;
    gp_Pnt midpoint;
    bool closed = false;
    std::optional<gp_Pnt> anchor;
    std::vector<gp_Pnt> bounds;
    double displayExtent = 0.0;
};
enum class Method {
    PlaneThreePoints, PlaneNormalAxisPoint, PlaneThroughAxisPoint, PlaneFace,
    PlaneOffset, PlaneEdgeFace, PlaneEdgeAngle, PlaneAxisAngle, PlaneParallelPoint,
    PlaneXYZ, PlaneMove, PlaneRotate,
    AxisTwoPoints, AxisPlanes, AxisFaces, AxisEdge, AxisCylinder, AxisPointDirection,
    AxisNormalPlane, AxisMove, AxisRotate,
    PointXYZ, PointVertex, PointEdgeMiddle, PointOnAxis, PointAxisPlane, PointAxes,
    PointCircleCenter, PointOffset, PointProject, PlaneMidplane,
    PlaneXY, PlaneXZ, PlaneYZ,
    AxisFaceNormal // v0.5.3: normal of a plane through the projection of a point
};
struct Role { QString name; ReferenceKind kind; };
struct Definition {
    Method method;
    Kind kind;
    QString name;
    std::vector<Role> roles;
    QString vectorLabel, anglesLabel, valueLabel;
    bool edit = false;
};
struct Request {
    Method method = Method::PointXYZ;
    std::vector<Reference> references;
    gp_Vec vector, angles;
    double value = 0.0;
    bool reverse = false;
    bool alternativeBisector = false;
};
const std::vector<Definition>& definitions();
const Definition& definition(Method method);
QString kindName(Kind kind);
QString namePrefix(Kind kind);
void advanceNameCounters(const std::vector<Object>& objects, std::array<int,3>& counters);
gp_Pnt displayCenter(const Object& object);
std::vector<gp_Pnt> displayBounds(const Object& object);
bool valid(const Object& object, QString& error);
bool equivalent(const Object& a, const Object& b);
Reference reference(const Object& object);
bool resolve(const TopoDS_Shape& shape, const gp_Trsf& placement,
             ReferenceKind expected, Reference& result, QString& error);
bool build(const Request& request, Object& result, QString& error);
}
