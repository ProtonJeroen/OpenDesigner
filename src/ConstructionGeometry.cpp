#include "ConstructionGeometry.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <TopoDS.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Circ.hxx>
#include <gp_Cylinder.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Construction {
namespace {
constexpr double pi = 3.14159265358979323846;
void require(bool condition, const char* error) { if (!condition) throw std::runtime_error(error); }
bool finite(const gp_XYZ& p) { return std::isfinite(p.X()) && std::isfinite(p.Y()) && std::isfinite(p.Z()); }
gp_Ax3 frame(const gp_Pnt& p, const gp_Vec& z, const gp_Vec& x) {
    require(z.Magnitude() > Precision::Angular(), "The direction must be nonzero.");
    const gp_Dir normal(z);
    const gp_Vec projected = x - gp_Vec(normal)*x.Dot(gp_Vec(normal));
    require(projected.Magnitude() > Precision::Angular(), "The reference direction is parallel to the normal.");
    return gp_Ax3(p, normal, gp_Dir(projected));
}
gp_Ax3 normalFrame(const gp_Pnt& p, const gp_Vec& z) {
    require(z.Magnitude()>Precision::Confusion(), "The direction must be nonzero.");
    return gp_Ax3(p,gp_Dir(z));
}
gp_Vec canonicalNormal(const gp_Dir& direction) {
    gp_Vec normal(direction);
    int dominant=1;
    for(int i=2;i<=3;++i) if(std::abs(normal.Coord(i))>std::abs(normal.Coord(dominant))) dominant=i;
    if(normal.Coord(dominant)<0.0) normal.Reverse();
    return normal;
}
gp_Ax3 midplane(const gp_Ax3& a,const gp_Ax3& b,bool alternative) {
    // Canonical signs make the choices independent of Face orientation.
    const gp_Vec n=canonicalNormal(a.Direction());gp_Vec m=canonicalNormal(b.Direction());
    if(n.Dot(m)<-Precision::Angular()) m.Reverse();
    const gp_Vec delta(a.Location(),b.Location());
    const gp_Pnt center=a.Location().Translated(delta*0.5);
    const double separation=m.Dot(delta);
    const bool parallel=n.Crossed(m).Magnitude()<=Precision::Angular();
    if(parallel) {
        require(std::abs(separation)>Precision::Confusion(),"Coincident planes do not define a unique midplane.");
        require(!alternative,"Parallel planes have only one midplane. Select Between planes / smaller angle.");
    }
    // Equidistance locus: E1 = -E2 (sum) or E1 = E2 (difference),
    // where both plane equations use unit normals and a common local origin.
    const gp_Vec raw=alternative?n-m:n+m;
    require(raw.Magnitude()>Precision::Angular(),"These planes do not define this angle bisector.");
    const double rhs=(alternative?-separation:separation)/raw.Magnitude();
    const gp_Vec normal=raw.Normalized();
    const auto origin=center.Translated(normal*(rhs-normal.Dot(delta*0.5)));
    gp_Vec x(a.XDirection());
    if((x-normal*x.Dot(normal)).Magnitude()<=Precision::Angular()) x=gp_Vec(a.YDirection());
    return frame(origin,normal,x);
}
gp_Ax3 intersectPlanes(const gp_Ax3& a, const gp_Ax3& b) {
    const gp_Vec n(a.Direction()), m(b.Direction()), cross = n.Crossed(m);
    require(cross.SquareMagnitude()>Precision::Angular()*Precision::Angular(),
            "Parallel or coincident planes have no unique intersection line.");
    // Closest point on the line to a.Location(); avoids large world-origin products.
    const double d = m.Dot(gp_Vec(a.Location(),b.Location()));
    const gp_Vec shift = cross.Crossed(n) * (d/cross.SquareMagnitude());
    return normalFrame(a.Location().Translated(shift),cross);
}
}
const std::vector<Definition>& definitions() {
    using M=Method; using K=Kind; using R=ReferenceKind;
    static const std::vector<Definition> list={
        {M::PlaneThreePoints,K::Workplane,"Through three points",{{"Origin",R::Point},{"X point",R::Point},{"Plane point",R::Point}},{},{},{}},
        {M::PlaneNormalAxisPoint,K::Workplane,"Normal to axis through point",{{"Axis",R::Axis},{"Point",R::Point}},{},{},{}},
        {M::PlaneThroughAxisPoint,K::Workplane,"Through axis and point",{{"Axis",R::Axis},{"Point",R::Point}},{},{},{}},
        {M::PlaneFace,K::Workplane,"From planar face",{{"Plane",R::Plane}},{},{},{}},
        {M::PlaneMidplane,K::Workplane,"Midplane Workplane",{{"First face / workplane",R::Plane},{"Second face / workplane",R::Plane}},{},{},{}},
        {M::PlaneOffset,K::Workplane,"Offset plane / planar face",{{"Plane",R::Plane}},{},{},"Offset (mm)"},
        {M::PlaneEdgeFace,K::Workplane,"Through edge, normal to face",{{"Straight edge",R::Line},{"Plane",R::Plane}},{},{},{}},
        {M::PlaneEdgeAngle,K::Workplane,"Edge and angle",{{"Straight edge",R::Line},{"Zero plane",R::Plane}},{},{},"Angle (degrees)"},
        {M::PlaneAxisAngle,K::Workplane,"Axis and angle",{{"Axis",R::Axis},{"Zero plane",R::Plane}},{},{},"Angle (degrees)"},
        {M::PlaneParallelPoint,K::Workplane,"Parallel through point",{{"Plane",R::Plane},{"Point",R::Point}},{},{},{}},
        {M::PlaneXYZ,K::Workplane,"XYZ and orientation",{},"Origin (mm)","World X/Y/Z angles (degrees)",{}},
        {M::PlaneXY,K::Workplane,"XY Workplane",{},{},{},"Offset along normal (mm)"},
        {M::PlaneXZ,K::Workplane,"XZ Workplane",{},{},{},"Offset along normal (mm)"},
        {M::PlaneYZ,K::Workplane,"YZ Workplane",{},{},{},"Offset along normal (mm)"},
        {M::PlaneMove,K::Workplane,"Move workplane",{{"Workplane",R::Plane}},"World displacement (mm)",{},{},true},
        {M::PlaneRotate,K::Workplane,"Rotate workplane",{{"Workplane",R::Plane}},"World axis direction",{},"Angle (degrees)",true},
        {M::AxisTwoPoints,K::Axis,"Through two points",{{"Origin",R::Point},{"Direction point",R::Point}},{},{},{}},
        {M::AxisPlanes,K::Axis,"Intersection of workplanes",{{"First plane",R::Plane},{"Second plane",R::Plane}},{},{},{}},
        {M::AxisFaces,K::Axis,"Intersection of planar faces",{{"First plane",R::Plane},{"Second plane",R::Plane}},{},{},{}},
        {M::AxisEdge,K::Axis,"From straight edge",{{"Edge",R::Line}},{},{},{}},
        {M::AxisCylinder,K::Axis,"From cylindrical / conical face",{{"Cylinder / cone",R::Cylinder}},{},{},{}},
        {M::AxisPointDirection,K::Axis,"Point and direction",{{"Origin",R::Point}},"World direction",{},{}},
        {M::AxisNormalPlane,K::Axis,"Normal to workplane",{{"Plane",R::Plane}},{},{},{}},
        {M::AxisFaceNormal,K::Axis,"Face / workplane normal at point",{{"Plane",R::Plane},{"Point",R::Point}},{},{},{}},
        {M::AxisMove,K::Axis,"Move axis",{{"Axis",R::Axis}},"World displacement (mm)",{},{},true},
        {M::AxisRotate,K::Axis,"Rotate axis",{{"Axis",R::Axis}},"World axis direction",{},"Angle (degrees)",true},
        {M::PointXYZ,K::Point,"XYZ coordinates",{},"Position (mm)",{},{}},
        {M::PointVertex,K::Point,"From vertex / point",{{"Point",R::Point}},{},{},{}},
        {M::PointEdgeMiddle,K::Point,"Midpoint of edge",{{"Edge",R::Edge}},{},{},{}},
        {M::PointOnAxis,K::Point,"On axis",{{"Axis",R::Axis}},{},{},"Distance from origin (mm)"},
        {M::PointAxisPlane,K::Point,"Axis / workplane intersection",{{"Axis",R::Axis},{"Plane",R::Plane}},{},{},{}},
        {M::PointAxes,K::Point,"Intersection of axes",{{"First axis",R::Axis},{"Second axis",R::Axis}},{},{},{}},
        {M::PointCircleCenter,K::Point,"Center of circle",{{"Circle edge",R::Circle}},{},{},{}},
        {M::PointOffset,K::Point,"Offset from point",{{"Point",R::Point}},"World offset (mm)",{},{}},
        {M::PointProject,K::Point,"Project onto workplane",{{"Point",R::Point},{"Plane",R::Plane}},{},{},{}}
    };
    return list;
}
const Definition& definition(Method method) {
    for(const auto& entry:definitions()) if(entry.method==method) return entry;
    throw std::runtime_error("Unknown construction method.");
}
QString kindName(Kind kind) {
    switch(kind) { case Kind::Workplane:return "Workplane";case Kind::Axis:return "Axis";case Kind::Point:return "Point"; }
    return {};
}
QString namePrefix(Kind kind) {
    switch(kind) {case Kind::Workplane:return "WP";case Kind::Axis:return "AX";case Kind::Point:return "PT";}
    return {};
}
void advanceNameCounters(const std::vector<Object>& objects,std::array<int,3>& counters) {
    for(const auto& object:objects) {
        const auto prefix=namePrefix(object.kind);
        if(prefix.isEmpty() || !object.name.startsWith(prefix)) continue;
        bool ok=false;const int number=object.name.mid(prefix.size()).toInt(&ok);
        if(ok && number>0) counters[size_t(object.kind)]=std::max(counters[size_t(object.kind)],number);
    }
}
gp_Pnt displayCenter(const Object& object) {
    return object.frame.Location().Translated(gp_Vec(object.frame.XDirection())*object.displayOffset.X()
        +gp_Vec(object.frame.YDirection())*object.displayOffset.Y()+gp_Vec(object.frame.Direction())*object.displayOffset.Z());
}
std::vector<gp_Pnt> displayBounds(const Object& object) {
    const auto center=displayCenter(object);
    if(object.kind==Kind::Point) return {center};
    if(object.kind==Kind::Axis) return {center.Translated(gp_Vec(object.frame.Direction())*(-object.extent)),
        center.Translated(gp_Vec(object.frame.Direction())*object.extent)};
    std::vector<gp_Pnt> corners;
    for(double x:{-1.0,1.0}) for(double y:{-1.0,1.0}) corners.push_back(center.Translated(
        gp_Vec(object.frame.XDirection())*(x*object.extent)+gp_Vec(object.frame.YDirection())*(y*object.extent)));
    return corners;
}
bool valid(const Object& object,QString& error) {
    if (kindName(object.kind).isEmpty() || object.id.isEmpty() || object.name.trimmed().isEmpty()
        || !finite(object.frame.Location().XYZ()) || !finite(object.frame.Direction().XYZ())
        || !finite(object.frame.XDirection().XYZ()) || !object.frame.Direct()
        || std::abs(object.frame.Direction().Dot(object.frame.XDirection()))>1.e-10
        || !finite(object.displayOffset.XYZ())
        || (object.kind==Kind::Workplane && std::abs(object.displayOffset.Z())>Precision::Confusion())
        || (object.kind==Kind::Axis && std::hypot(object.displayOffset.X(),object.displayOffset.Y())>Precision::Confusion())
        || (object.kind==Kind::Point && object.displayOffset.Magnitude()>Precision::Confusion())
        || !std::isfinite(object.extent) || object.extent<=Precision::Confusion()) {
        error="Invalid construction identity, frame or display extent.";return false;
    }
    error.clear();return true;
}
bool equivalent(const Object& a,const Object& b) {
    return a.id==b.id && a.name==b.name && a.kind==b.kind && a.visible==b.visible && a.selectable==b.selectable
        && a.extent==b.extent && (a.displayOffset-b.displayOffset).Magnitude()<1.e-9 && a.frame.Location().IsEqual(b.frame.Location(),1.e-9)
        && a.frame.Direction().IsEqual(b.frame.Direction(),1.e-12)
        && a.frame.XDirection().IsEqual(b.frame.XDirection(),1.e-12);
}
Reference reference(const Object& object) {
    Reference result;
    result.kind=object.kind==Kind::Workplane?ReferenceKind::Plane:object.kind==Kind::Axis?ReferenceKind::Axis:ReferenceKind::Point;
    result.frame=object.frame;result.midpoint=object.frame.Location();result.anchor=displayCenter(object);
    result.bounds=displayBounds(object);result.displayExtent=object.extent;return result;
}
bool resolve(const TopoDS_Shape& shape,const gp_Trsf& placement,ReferenceKind expected,Reference& result,QString& error) {
    try {
        Reference r; r.kind=expected;
        if(shape.ShapeType()==TopAbs_VERTEX && expected==ReferenceKind::Point) {
            r.frame.SetLocation(BRep_Tool::Pnt(TopoDS::Vertex(shape)).Transformed(placement));
        } else if(shape.ShapeType()==TopAbs_FACE && (expected==ReferenceKind::Plane || expected==ReferenceKind::Cylinder)) {
            BRepAdaptor_Surface surface(TopoDS::Face(shape));
            if(expected==ReferenceKind::Plane) {
                require(surface.GetType()==GeomAbs_Plane,"Select a planar face.");
                r.frame=surface.Plane().Position().Transformed(placement);
                if(shape.Orientation()==TopAbs_REVERSED) { r.frame.ZReverse();r.frame.YReverse(); }
            } else {
                require(surface.GetType()==GeomAbs_Cylinder || surface.GetType()==GeomAbs_Cone,"Select a cylindrical or conical face.");
                r.frame=(surface.GetType()==GeomAbs_Cylinder?surface.Cylinder().Position():surface.Cone().Position()).Transformed(placement);
            }
        } else if(shape.ShapeType()==TopAbs_EDGE && (expected==ReferenceKind::Line || expected==ReferenceKind::Circle || expected==ReferenceKind::Edge)) {
            BRepAdaptor_Curve curve(TopoDS::Edge(shape));
            const double first=curve.FirstParameter(),last=curve.LastParameter();
            require(std::isfinite(first) && std::isfinite(last),"The edge must be finite.");
            if(expected==ReferenceKind::Line) {
                require(curve.GetType()==GeomAbs_Line,"Select a straight edge.");
                const gp_Pnt a=curve.Value(first).Transformed(placement),b=curve.Value(last).Transformed(placement);
                r.frame=normalFrame(shape.Orientation()==TopAbs_REVERSED?b:a,
                    shape.Orientation()==TopAbs_REVERSED?gp_Vec(b,a):gp_Vec(a,b));
            } else if(expected==ReferenceKind::Circle) {
                require(curve.GetType()==GeomAbs_Circle,"Select a circular edge.");
                r.frame=gp_Ax3(curve.Circle().Position()).Transformed(placement);
            } else {
                const double length=GCPnts_AbscissaPoint::Length(curve,first,last);
                require(length>Precision::Confusion(),"The edge has zero length.");
                r.closed=curve.Value(first).IsEqual(curve.Value(last),Precision::Confusion());
                require(!r.closed,"A closed edge has no unique midpoint. Use Center of circle where applicable.");
                GCPnts_AbscissaPoint middle(curve,length*0.5,first);
                require(middle.IsDone(),"Could not determine the arc-length midpoint.");
                r.midpoint=curve.Value(middle.Parameter()).Transformed(placement);
                r.frame.SetLocation(r.midpoint);
            }
        } else throw std::runtime_error("This object cannot supply the requested reference.");
        r.anchor=r.frame.Location();
        if(shape.ShapeType()==TopAbs_FACE || shape.ShapeType()==TopAbs_EDGE) {
            GProp_GProps properties;
            if(shape.ShapeType()==TopAbs_FACE) BRepGProp::SurfaceProperties(shape,properties);
            else BRepGProp::LinearProperties(shape,properties);
            if(properties.Mass()>Precision::Confusion()) r.anchor=properties.CentreOfMass().Transformed(placement);
            Bnd_Box box;BRepBndLib::Add(shape,box,Standard_False);
            if(!box.IsVoid() && !box.IsOpen()) {
                double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
                for(double x:{x0,x1}) for(double y:{y0,y1}) for(double z:{z0,z1}) r.bounds.push_back(gp_Pnt(x,y,z).Transformed(placement));
            }
        }
        result=r;error.clear();return true;
    } catch(const Standard_Failure& e) {error=QString::fromUtf8(e.GetMessageString());}
      catch(const std::exception& e) {error=QString::fromUtf8(e.what());}
    return false;
}
bool build(const Request& q,Object& result,QString& error) {
    try {
        const auto& d=definition(q.method);
        require(q.references.size()==d.roles.size(),"Select every required reference.");
        require(finite(q.vector.XYZ()) && finite(q.angles.XYZ()) && std::isfinite(q.value),"Enter finite numeric parameters.");
        for(size_t i=0;i<q.references.size();++i) {
            require(q.references[i].kind==d.roles[i].kind,"Reference type does not match its role.");
            require(finite(q.references[i].frame.Location().XYZ()),"Invalid reference position.");
        }
        const gp_Ax3 a=q.references.empty()?gp_Ax3():q.references[0].frame;
        const gp_Ax3 b=q.references.size()<2?gp_Ax3():q.references[1].frame;
        gp_Ax3 out=a;
        const auto point=[](const gp_Pnt& p) {gp_Ax3 axes;axes.SetLocation(p);return axes;};
        switch(q.method) {
        case Method::PlaneThreePoints: {
            gp_Vec x(a.Location(),b.Location()),y(a.Location(),q.references[2].frame.Location());
            require(x.Magnitude()>Precision::Confusion() && y.Magnitude()>Precision::Confusion(),"Choose three distinct, non-collinear points.");
            x.Normalize();y.Normalize();out=frame(a.Location(),x.Crossed(y),x);break;
        }
        case Method::PlaneNormalAxisPoint: out=a;out.SetLocation(b.Location());break;
        case Method::PlaneThroughAxisPoint: {
            gp_Vec normal=gp_Vec(a.Direction()).Crossed(gp_Vec(a.Location(),b.Location()));
            require(normal.Magnitude()>Precision::Confusion(),"A point on the axis does not determine a unique plane.");
            normal.Normalize();out=frame(a.Location(),normal,gp_Vec(a.Direction()));break;
        }
        case Method::PlaneFace: break;
        case Method::PlaneMidplane: out=midplane(a,b,q.alternativeBisector);break;
        case Method::PlaneOffset: out.SetLocation(a.Location().Translated(gp_Vec(a.Direction())*q.value));break;
        case Method::PlaneEdgeFace:
            require(std::abs(a.Direction().Dot(b.Direction()))<=1.e-9,"The edge direction must be parallel to the reference face plane.");
            out=frame(a.Location(),gp_Vec(a.Direction()).Crossed(gp_Vec(b.Direction())),gp_Vec(a.Direction()));break;
        case Method::PlaneEdgeAngle: case Method::PlaneAxisAngle: {
            require(std::abs(a.Direction().Dot(b.Direction()))<=1.e-9,"The rotation axis must be parallel to the zero plane.");
            out=frame(a.Location(),gp_Vec(b.Direction()),gp_Vec(a.Direction()));
            gp_Trsf rotation;rotation.SetRotation(gp_Ax1(a.Location(),a.Direction()),q.value*pi/180.0);out.Transform(rotation);break;
        }
        case Method::PlaneParallelPoint: out.SetLocation(b.Location());break;
        case Method::PlaneXYZ: {
            out=gp_Ax3();
            gp_Trsf rx,ry,rz; rx.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,0,0)),q.angles.X()*pi/180.0);
            ry.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(0,1,0)),q.angles.Y()*pi/180.0);
            rz.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(0,0,1)),q.angles.Z()*pi/180.0);
            out.Transform(rz*ry*rx);out.SetLocation(gp_Pnt(q.vector.XYZ()));break;
        }
        case Method::PlaneXY: out=gp_Ax3(gp_Pnt(),gp_Dir(0,0,1),gp_Dir(1,0,0));break;
        case Method::PlaneXZ: out=gp_Ax3(gp_Pnt(),gp_Dir(0,-1,0),gp_Dir(1,0,0));break;
        case Method::PlaneYZ: out=gp_Ax3(gp_Pnt(),gp_Dir(1,0,0),gp_Dir(0,1,0));break;
        case Method::PlaneMove: case Method::AxisMove: out.SetLocation(a.Location().Translated(q.vector));break;
        case Method::PlaneRotate: case Method::AxisRotate: {
            require(q.vector.Magnitude()>Precision::Confusion(),"The rotation axis must be nonzero.");
            gp_Trsf rotation;rotation.SetRotation(gp_Ax1(a.Location(),gp_Dir(q.vector)),q.value*pi/180.0);out.Transform(rotation);break;
        }
        case Method::AxisTwoPoints: out=normalFrame(a.Location(),gp_Vec(a.Location(),b.Location()));break;
        case Method::AxisPlanes: case Method::AxisFaces: out=intersectPlanes(a,b);break;
        case Method::AxisEdge: case Method::AxisCylinder: case Method::AxisNormalPlane: break;
        case Method::AxisPointDirection: out=normalFrame(a.Location(),q.vector);break;
        case Method::AxisFaceNormal: {
            // Orthogonal projection of the point onto the infinite support plane.
            const gp_Vec normal(a.Direction());
            out=normalFrame(b.Location().Translated(normal*(-gp_Vec(a.Location(),b.Location()).Dot(normal))),normal);break;
        }
        case Method::PointXYZ: out=point(gp_Pnt(q.vector.XYZ()));break;
        case Method::PointVertex: out=point(a.Location());break;
        case Method::PointEdgeMiddle: require(!q.references[0].closed,"A closed edge has no unique midpoint.");out=point(q.references[0].midpoint);break;
        case Method::PointOnAxis: out=point(a.Location().Translated(gp_Vec(a.Direction())*q.value));break;
        case Method::PointAxisPlane: {
            const double dot=a.Direction().Dot(b.Direction());
            require(std::abs(dot)>Precision::Angular(),"The axis is parallel to or contained in the plane; no unique intersection.");
            out=point(a.Location().Translated(gp_Vec(a.Direction())*(gp_Vec(a.Location(),b.Location()).Dot(gp_Vec(b.Direction()))/dot)));break;
        }
        case Method::PointAxes: {
            const gp_Vec u(a.Direction()),v(b.Direction()),w(a.Location(),b.Location());
            const double dot=u.Dot(v),denom=u.Crossed(v).SquareMagnitude();
            require(denom>Precision::Angular()*Precision::Angular(),"Parallel or coincident axes have no unique intersection.");
            const double t=(w.Dot(u)-dot*w.Dot(v))/denom;
            const double s=(dot*w.Dot(u)-w.Dot(v))/denom;
            const gp_Pnt p=a.Location().Translated(u*t),r=b.Location().Translated(v*s);
            require(p.Distance(r)<=Precision::Confusion(),"The axes are skew and do not intersect.");
            out=point(p);break;
        }
        case Method::PointCircleCenter: out=point(a.Location());break;
        case Method::PointOffset: out=point(a.Location().Translated(q.vector));break;
        case Method::PointProject: out=point(a.Location().Translated(gp_Vec(b.Direction())*(-gp_Vec(b.Location(),a.Location()).Dot(gp_Vec(b.Direction())))));break;
        }
        if(q.reverse && d.kind!=Kind::Point) {out.ZReverse();out.YReverse();}
        if(q.method==Method::PlaneXY || q.method==Method::PlaneXZ || q.method==Method::PlaneYZ)
            out.SetLocation(gp_Pnt().Translated(gp_Vec(out.Direction())*q.value));
        require(finite(out.Location().XYZ()) && out.Direct(),"The computed construction frame is invalid.");
        Object candidate=result;candidate.kind=d.kind;candidate.frame=out;
        if(!d.edit) {
            candidate.displayOffset=gp_Vec();candidate.extent=100.0;
            if(d.kind!=Kind::Point && !q.references.empty()) {
                gp_XYZ average;
                for(const auto& source:q.references) average+=(source.anchor.value_or(source.frame.Location())).XYZ();
                const gp_Vec offset(out.Location(),gp_Pnt(average/double(q.references.size())));
                if(d.kind==Kind::Workplane) {
                    candidate.displayOffset=gp_Vec(offset.Dot(gp_Vec(out.XDirection())),offset.Dot(gp_Vec(out.YDirection())),0);
                    double radius=0;
                    for(const auto& source:q.references) {
                        auto bounds=source.bounds;if(bounds.empty()) bounds.push_back(source.anchor.value_or(source.frame.Location()));
                        for(const auto& p:bounds) {
                            const gp_Vec delta(displayCenter(candidate),p);
                            radius=std::max({radius,std::abs(delta.Dot(gp_Vec(out.XDirection()))),std::abs(delta.Dot(gp_Vec(out.YDirection())))});
                        }
                    }
                    candidate.extent=std::max(10.0,radius+std::max(1.0,radius*0.1));
                    if((q.method==Method::PlaneFace || q.method==Method::PlaneOffset || q.method==Method::PlaneParallelPoint)
                        && q.references.front().displayExtent>0) candidate.extent=q.references.front().displayExtent;
                } else {
                    double low=0,high=0;bool first=true;
                    for(const auto& source:q.references) {
                        auto bounds=source.bounds;if(bounds.empty()) bounds.push_back(source.anchor.value_or(source.frame.Location()));
                        for(const auto& p:bounds) {
                            const double t=gp_Vec(out.Location(),p).Dot(gp_Vec(out.Direction()));
                            if(first) {low=high=t;first=false;} else {low=std::min(low,t);high=std::max(high,t);}
                        }
                    }
                    candidate.displayOffset.SetZ((low+high)*0.5);
                    if(high-low>Precision::Confusion()) candidate.extent=(high-low)*0.5+std::max(1.0,(high-low)*0.1);
                }
            }
        }
        require(finite(candidate.displayOffset.XYZ()) && std::isfinite(candidate.extent),"The display bounds are invalid.");
        result=candidate;
        error.clear();return true;
    } catch(const Standard_Failure& e) {error=QString::fromUtf8(e.GetMessageString());}
      catch(const std::exception& e) {error=QString::fromUtf8(e.what());}
    return false;
}
}
