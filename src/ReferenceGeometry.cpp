#include "ReferenceGeometry.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <cmath>

namespace References {
namespace {
constexpr double ZeroLength=1.e-9;
bool fail(QString& error,const QString& message) {error=message;return false;}
const PartSource* part(const Model& model,const QString& id) {
    const auto it=std::find_if(model.parts.begin(),model.parts.end(),[&](const auto& p){return p.id==id;});
    return it==model.parts.end()?nullptr:&*it;
}
const Construction::Object* construction(const Model& model,const QString& id) {
    const auto it=std::find_if(model.constructions.begin(),model.constructions.end(),[&](const auto& o){return o.id==id;});
    return it==model.constructions.end()?nullptr:&*it;
}
const Sketch::Entity* entity(const Model& model,const QString& id) {
    const auto it=std::find_if(model.sketches.begin(),model.sketches.end(),[&](const auto& e){return e.id==id;});
    return it==model.sketches.end()?nullptr:&*it;
}
// Usable Workplane owner of a Sketch entity.
const Construction::Object* owner(const Model& model,const Sketch::Entity& e,QString& error) {
    const auto* wp=construction(model,e.workplaneId);
    if(!wp || wp->kind!=Construction::Kind::Workplane) {error=QStringLiteral("The Workplane of this Sketch entity no longer exists.");return nullptr;}
    if(!wp->visible) {error=QStringLiteral("Sketch geometry on a hidden Workplane cannot supply a reference.");return nullptr;}
    return wp;
}
bool subshape(const Model& model,const Target& target,TopAbs_ShapeEnum type,const PartSource*& source,QString& error) {
    source=part(model,target.ownerId);
    if(!source) return fail(error,QStringLiteral("The referenced Part no longer exists; pick the %1 again.").arg(kindName(target.kind)));
    if(!source->visible) return fail(error,QStringLiteral("Hidden Part %1 cannot supply a reference.").arg(source->name));
    if(target.subshape.IsNull() || target.subshape.ShapeType()!=type) return fail(error,QStringLiteral("The picked shape is not a %1.").arg(kindName(target.kind)));
    TopTools_IndexedMapOfShape members;TopExp::MapShapes(source->shape,type,members);
    if(!members.Contains(target.subshape))
        return fail(error,QStringLiteral("The referenced %1 no longer exists in the current geometry of %2; pick it again.").arg(kindName(target.kind),source->name));
    return true;
}
}
QString kindName(Kind kind) {
    switch(kind) {
    case Kind::Part: return QStringLiteral("Part");
    case Kind::Face: return QStringLiteral("Face");
    case Kind::Edge: return QStringLiteral("Edge");
    case Kind::Vertex: return QStringLiteral("Vertex");
    case Kind::Axis: return QStringLiteral("Axis");
    case Kind::Workplane: return QStringLiteral("Workplane");
    case Kind::Point: return QStringLiteral("Point");
    case Kind::SketchLine: return QStringLiteral("Sketch line");
    case Kind::SketchPoint: return QStringLiteral("Sketch point");
    }
    return {};
}
bool sameTarget(const Target& a,const Target& b) {
    return a.kind==b.kind && a.ownerId==b.ownerId && a.point==b.point
        && (a.subshape.IsNull()==b.subshape.IsNull()) && (a.subshape.IsNull() || a.subshape.IsSame(b.subshape));
}
bool resolve(const Model& model,const Target& target,Resolved& result,QString& error) {
    try {
        Resolved r;r.kind=target.kind;
        switch(target.kind) {
        case Kind::Part: {
            const auto* source=part(model,target.ownerId);
            if(!source) return fail(error,QStringLiteral("The referenced Part no longer exists."));
            if(!source->visible) return fail(error,QStringLiteral("Hidden Part %1 cannot supply a reference.").arg(source->name));
            gp_Ax3 frame;frame.Transform(source->placement);r.plane=frame;r.point=frame.Location();r.label=source->name;break;
        }
        case Kind::Face: {
            const PartSource* source=nullptr;if(!subshape(model,target,TopAbs_FACE,source,error)) return false;
            const auto type=BRepAdaptor_Surface(TopoDS::Face(target.subshape)).GetType();
            Construction::Reference ref;
            if(type==GeomAbs_Plane) {
                if(!Construction::resolve(target.subshape,source->placement,Construction::ReferenceKind::Plane,ref,error)) return false;
                r.plane=ref.frame;
            } else if(type==GeomAbs_Cylinder || type==GeomAbs_Cone) {
                BRepAdaptor_Surface surface(TopoDS::Face(target.subshape));
                const gp_Ax1 axis=type==GeomAbs_Cylinder?surface.Cylinder().Axis():surface.Cone().Axis();
                r.line=axis.Transformed(source->placement);
            } else return fail(error,QStringLiteral("This Face has no unique plane or axis. Use a planar, cylindrical or conical Face."));
            GProp_GProps properties;BRepGProp::SurfaceProperties(target.subshape,properties);
            r.point=properties.Mass()>Precision::Confusion()?properties.CentreOfMass().Transformed(source->placement)
                :(r.plane?r.plane->Location():r.line->Location()); // fallbacks are already world
            r.label=QStringLiteral("%1 Face").arg(source->name);break;
        }
        case Kind::Edge: {
            const PartSource* source=nullptr;if(!subshape(model,target,TopAbs_EDGE,source,error)) return false;
            const auto type=BRepAdaptor_Curve(TopoDS::Edge(target.subshape)).GetType();
            Construction::Reference ref;
            if(type==GeomAbs_Line) {
                if(!Construction::resolve(target.subshape,source->placement,Construction::ReferenceKind::Line,ref,error)) return false;
                r.line=gp_Ax1(ref.frame.Location(),ref.frame.Direction());
                Construction::Reference middle;
                if(Construction::resolve(target.subshape,source->placement,Construction::ReferenceKind::Edge,middle,error)) r.point=middle.midpoint;
                else r.point=ref.frame.Location();
            } else if(type==GeomAbs_Circle) {
                if(!Construction::resolve(target.subshape,source->placement,Construction::ReferenceKind::Circle,ref,error)) return false;
                r.line=gp_Ax1(ref.frame.Location(),ref.frame.Direction());r.point=ref.frame.Location();
            } else return fail(error,QStringLiteral("This Edge has no unique direction. Use a straight or circular Edge."));
            r.label=QStringLiteral("%1 Edge").arg(source->name);break;
        }
        case Kind::Vertex: {
            const PartSource* source=nullptr;if(!subshape(model,target,TopAbs_VERTEX,source,error)) return false;
            r.point=BRep_Tool::Pnt(TopoDS::Vertex(target.subshape)).Transformed(source->placement);
            r.label=QStringLiteral("%1 Vertex").arg(source->name);break;
        }
        case Kind::Axis: case Kind::Workplane: case Kind::Point: {
            const auto* object=construction(model,target.ownerId);
            if(!object) return fail(error,QStringLiteral("The referenced %1 no longer exists.").arg(kindName(target.kind)));
            const auto expected=target.kind==Kind::Axis?Construction::Kind::Axis:target.kind==Kind::Workplane?Construction::Kind::Workplane:Construction::Kind::Point;
            if(object->kind!=expected) return fail(error,QStringLiteral("%1 is not a %2.").arg(object->name,kindName(target.kind)));
            if(!object->visible || !object->selectable) return fail(error,QStringLiteral("Hidden or locked %1 cannot supply a reference.").arg(object->name));
            r.point=object->frame.Location();
            if(target.kind==Kind::Axis) r.line=gp_Ax1(object->frame.Location(),object->frame.Direction());
            if(target.kind==Kind::Workplane) r.plane=object->frame;
            r.label=object->name;break;
        }
        case Kind::SketchLine: {
            const auto* e=entity(model,target.ownerId);
            if(!e) return fail(error,QStringLiteral("The referenced Sketch line no longer exists; pick it again."));
            if(!e->visible || !e->selectable) return fail(error,QStringLiteral("Hidden or locked Sketch entities cannot supply a reference."));
            const auto* wp=owner(model,*e,error);if(!wp) return false;
            Sketch::Point origin,direction;
            if(const auto* s=std::get_if<Sketch::Segment>(&e->geometry)) {origin=s->start;direction={s->end.x-s->start.x,s->end.y-s->start.y};
                r.point=Sketch::worldPoint({(s->start.x+s->end.x)/2,(s->start.y+s->end.y)/2},wp->frame);}
            else if(const auto* l=std::get_if<Sketch::InfiniteLine>(&e->geometry)) {origin=l->origin;direction=l->direction;r.point=Sketch::worldPoint(origin,wp->frame);}
            else return fail(error,QStringLiteral("Only Sketch lines supply a line reference."));
            const gp_Vec world=gp_Vec(wp->frame.XDirection())*direction.x+gp_Vec(wp->frame.YDirection())*direction.y;
            if(!(world.Magnitude()>ZeroLength)) return fail(error,QStringLiteral("The Sketch line has zero length."));
            r.line=gp_Ax1(Sketch::worldPoint(origin,wp->frame),gp_Dir(world));
            r.label=e->name.isEmpty()?QStringLiteral("Sketch line"):e->name;break;
        }
        case Kind::SketchPoint: {
            const auto* e=entity(model,target.point.entity);
            if(!e) return fail(error,QStringLiteral("The referenced Sketch entity no longer exists; pick it again."));
            const auto* wp=owner(model,*e,error);if(!wp) return false;
            const auto local=Sketch::resolvePoint(model.sketches,target.point,error);if(!local) return false;
            r.point=Sketch::worldPoint(*local,wp->frame);r.label=QStringLiteral("Sketch point");break;
        }
        }
        result=r;error.clear();return true;
    } catch(const Standard_Failure& failure) {return fail(error,QString::fromUtf8(failure.GetMessageString()));}
}
bool direction(const Resolved& reference,gp_Dir& result,QString& error) {
    if(reference.line) {result=reference.line->Direction();error.clear();return true;}
    if(reference.plane) {result=reference.plane->Direction();error.clear();return true;}
    return fail(error,QStringLiteral("%1 has no unique direction; use Point to Point with a second point.").arg(kindName(reference.kind)));
}
bool pointToPoint(const Resolved& from,const Resolved& to,gp_Dir& result,double& length,QString& error) {
    if(!from.point || !to.point) return fail(error,QStringLiteral("Point to Point needs two point references."));
    const gp_Vec vector(*from.point,*to.point);length=vector.Magnitude();
    if(!(length>ZeroLength)) return fail(error,QStringLiteral("The two points coincide; the direction is undefined."));
    result=gp_Dir(vector);error.clear();return true;
}
}
