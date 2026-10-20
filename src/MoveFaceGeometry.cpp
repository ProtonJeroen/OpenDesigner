#include "MoveFaceGeometry.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepLib.hxx>
#include <BRep_Tool.hxx>
#include <BRepOffset_MakeOffset.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BOPAlgo_ArgumentAnalyzer.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Solid.hxx>
#include <Standard_Failure.hxx>
#include <QDebug>
#include <QStringList>
#include <gp_Pln.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Cone.hxx>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr double tolerance = 1.e-7;
int count(const TopoDS_Shape& shape, TopAbs_ShapeEnum type) {
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(shape,type,map);
    return map.Extent();
}
QString surfaceName(const TopoDS_Face& face) {
    switch(BRepAdaptor_Surface(face).GetType()) {
    case GeomAbs_Plane:return QStringLiteral("plane");
    case GeomAbs_Cylinder:return QStringLiteral("cylinder");
    case GeomAbs_Cone:return QStringLiteral("cone");
    default:return QStringLiteral("unsupported");
    }
}
QString topologyDetails(const TopoDS_Shape& solid,const TopoDS_Face& target) {
    QStringList neighbors;
    for(TopExp_Explorer f(solid,TopAbs_FACE);f.More();f.Next()) {
        const auto candidate=TopoDS::Face(f.Current());
        if(candidate.IsSame(target)) continue;
        bool adjacent=false;
        for(TopExp_Explorer te(target,TopAbs_EDGE);te.More() && !adjacent;te.Next())
            for(TopExp_Explorer ce(candidate,TopAbs_EDGE);ce.More();ce.Next())
                if(te.Current().IsSame(ce.Current())) { adjacent=true; break; }
        if(adjacent) neighbors<<QStringLiteral("%1(wires=%2)").arg(surfaceName(candidate)).arg(count(candidate,TopAbs_WIRE));
    }
    return QStringLiteral("solids=%1 shells=%2 faces=%3 edges=%4 vertices=%5; target=%6 wires/%7 edges; neighbors=[%8]")
        .arg(count(solid,TopAbs_SOLID)).arg(count(solid,TopAbs_SHELL)).arg(count(solid,TopAbs_FACE))
        .arg(count(solid,TopAbs_EDGE)).arg(count(solid,TopAbs_VERTEX)).arg(count(target,TopAbs_WIRE))
        .arg(count(target,TopAbs_EDGE)).arg(neighbors.join(QStringLiteral(", ")));
}
bool valid(const TopoDS_Shape& shape, double& volume) {
    if (shape.IsNull() || count(shape,TopAbs_SOLID)!=1 || count(shape,TopAbs_SHELL)!=1
        || !BRepCheck_Analyzer(shape).IsValid()) return false;
    TopExp_Explorer solids(shape,TopAbs_SOLID);
    for (const auto type : {TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX})
        if (count(shape,type)!=count(solids.Current(),type)) return false;
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape,props);
    volume = props.Mass();
    if (!std::isfinite(volume) || volume<=1.e-12) return false;
    BOPAlgo_ArgumentAnalyzer check;
    BRepBuilderAPI_Copy isolated(shape,true,false);
    check.SetShape1(isolated.Shape());
    check.SelfInterMode() = true;
    check.StopOnFirstFaulty() = true;
    check.Perform();
    return !check.HasErrors() && !check.HasFaulty();
}
bool supported(const TopoDS_Shape& shape) {
    if (count(shape,TopAbs_FACE)>64) return false;
    for (TopExp_Explorer f(shape,TopAbs_FACE); f.More(); f.Next()) {
        if (count(f.Current(),TopAbs_WIRE)<1) return false;
        const auto type = BRepAdaptor_Surface(TopoDS::Face(f.Current())).GetType();
        if (type!=GeomAbs_Plane && type!=GeomAbs_Cylinder && type!=GeomAbs_Cone) return false;
    }
    return true;
}
bool sameSupport(const TopoDS_Face& original,const TopoDS_Face& result,
                 const gp_Vec& displacement) {
    BRepAdaptor_Surface a(original),b(result);
    if(a.GetType()!=b.GetType()) return false;
    if(a.GetType()==GeomAbs_Plane) {
        auto plane=a.Plane(); plane.Translate(displacement);
        return plane.Axis().Direction().IsParallel(b.Plane().Axis().Direction(),tolerance)
            && plane.Distance(b.Plane().Location())<1.e-5;
    }
    if(a.GetType()==GeomAbs_Cylinder) {
        const auto x=a.Cylinder(),y=b.Cylinder();
        return x.Axis().IsCoaxial(y.Axis(),tolerance,1.e-5) && std::abs(x.Radius()-y.Radius())<1.e-5;
    }
    if(a.GetType()==GeomAbs_Cone) {
        const auto x=a.Cone(),y=b.Cone();
        return x.Axis().Direction().IsParallel(y.Axis().Direction(),tolerance)
            && x.Apex().Distance(y.Apex())<1.e-5
            && std::abs(std::abs(x.SemiAngle())-std::abs(y.SemiAngle()))<tolerance;
    }
    return false;
}
}
bool MoveFaceGeometry::reference(const TopoDS_Shape& solid, const TopoDS_Face& face,
                                 gp_Pnt& center, gp_Dir& outward, QString& error) {
    error.clear();
    try {
        bool member=false;
        for (TopExp_Explorer f(solid,TopAbs_FACE);f.More();f.Next())
            if (f.Current().IsSame(face)) member=true;
        if (!member || face.IsNull() || BRepAdaptor_Surface(face).GetType()!=GeomAbs_Plane) {
            error=QStringLiteral("Select one planar face that belongs to the solid.");
            qWarning().noquote()<<"Move Face unsupported target:"<<error<<topologyDetails(solid,face);
            return false;
        }
        double volume=0;
        if (!supported(solid) || !valid(solid,volume)) {
            error=QStringLiteral("Move Face needs one valid solid and shell (up to 64 faces); faces must be planes, cylinders or cones.");
            qWarning().noquote()<<"Move Face unsupported topology:"<<error<<topologyDetails(solid,face)
                               <<"valid="<<BRepCheck_Analyzer(solid).IsValid();
            return false;
        }
        GProp_GProps props;
        BRepGProp::SurfaceProperties(face,props);
        center=props.CentreOfMass();
        outward=BRepAdaptor_Surface(face).Plane().Axis().Direction();
        if (face.Orientation()==TopAbs_REVERSED) outward.Reverse();
        return true;
    } catch (const Standard_Failure&) {
        error=QStringLiteral("Open CASCADE could not analyze this face safely."); return false;
    }
}
TopoDS_Shape MoveFaceGeometry::move(const TopoDS_Shape& solid, const TopoDS_Face& face,
                                   double distance, QString& error) {
    gp_Pnt center; gp_Dir normal;
    if (!std::isfinite(distance)) { error=QStringLiteral("Enter a finite face offset."); return {}; }
    if (!reference(solid,face,center,normal,error)) return {};
    if (std::abs(distance)<tolerance) return solid;
    try {
        // Work on a deep copy: the offset algorithm must never modify document topology.
        BRepBuilderAPI_Copy copy(solid,true,false);
        auto copiedFace=TopoDS::Face(copy.ModifiedShape(face));
        copiedFace.Orientation(face.Orientation());
        BRepOffset_MakeOffset offset;
        // Intersection mode is essential here: the neighboring face supports
        // stay fixed while their trimming curves are recomputed against the
        // translated target plane.
        offset.Initialize(copy.Shape(),0,tolerance,BRepOffset_Skin,true,false,GeomAbs_Intersection);
        offset.SetOffsetOnFace(copiedFace,distance);
        offset.MakeOffsetShape();
        const auto fail=[&](const QString& reason) -> TopoDS_Shape {
            error=QStringLiteral("Move Face refused: ")+reason;
            qWarning().noquote()<<error<<topologyDetails(solid,face);
            return {};
        };
        if (!offset.IsDone()) {
            qWarning().noquote()<<"Move Face OCCT offset failed: error="<<static_cast<int>(offset.Error())
                               <<"face wires="<<count(face,TopAbs_WIRE)<<"face edges="<<count(face,TopAbs_EDGE)
                               <<"adjacent source topology faces="<<count(solid,TopAbs_FACE);
        }
        TopoDS_Shape result=offset.Shape();
        if(!offset.IsDone() || result.IsNull()) {
            qWarning().noquote()<<"Move Face OCCT returned a null offset result: error="<<static_cast<int>(offset.Error())
                               <<"face wires="<<count(face,TopAbs_WIRE)<<"face edges="<<count(face,TopAbs_EDGE);
            return fail(QStringLiteral("Open CASCADE could not rebuild the translated face and its adjacent intersections (kernel status %1).")
                        .arg(static_cast<int>(offset.Error())));
        }
        // OCCT sometimes returns the closed offset shell instead of its solid.
        // Promote only that single closed shell; never sew or heal an open result.
        if (!result.IsNull() && result.ShapeType()==TopAbs_SHELL && BRep_Tool::IsClosed(result)) {
            auto solidResult=BRepBuilderAPI_MakeSolid(TopoDS::Shell(result)).Solid();
            if (!BRepLib::OrientClosedSolid(solidResult)) return fail(QStringLiteral("the offset shell cannot bound a solid."));
            result=solidResult;
        }
        double volume=0;
        if (result.IsNull() || !supported(result) || !valid(result,volume)) {
            const QString details=QStringLiteral("solids=%1 shells=%2 faces=%3 edges=%4 vertices=%5 valid=%6 supported=%7")
                .arg(count(result,TopAbs_SOLID)).arg(count(result,TopAbs_SHELL)).arg(count(result,TopAbs_FACE))
                .arg(count(result,TopAbs_EDGE)).arg(count(result,TopAbs_VERTEX))
                .arg(!result.IsNull() && BRepCheck_Analyzer(result).IsValid())
                .arg(!result.IsNull() && supported(result));
            qWarning().noquote()<<"Move Face candidate rejected:"<<details;
            return fail(QStringLiteral("the offset result is not a valid supported single solid (%1). ").arg(details));
        }
        GProp_GProps before;
        BRepGProp::VolumeProperties(solid,before);
        if ((volume-before.Mass())*distance<=0) return fail(QStringLiteral("volume did not change in the requested direction."));
        if(count(result,TopAbs_FACE)!=count(solid,TopAbs_FACE)
           || count(result,TopAbs_EDGE)!=count(solid,TopAbs_EDGE)
           || count(result,TopAbs_VERTEX)!=count(solid,TopAbs_VERTEX))
            return fail(QStringLiteral("the edit changes face/edge/vertex topology (faces %1/%2, edges %3/%4, vertices %5/%6); that case is not supported safely.")
                        .arg(count(solid,TopAbs_FACE)).arg(count(result,TopAbs_FACE))
                        .arg(count(solid,TopAbs_EDGE)).arg(count(result,TopAbs_EDGE))
                        .arg(count(solid,TopAbs_VERTEX)).arg(count(result,TopAbs_VERTEX)));
        // Require each original analytic support to remain, with only the
        // selected plane translated. Trim boundaries may move along those
        // supports, but no bridge faces or new support surfaces are accepted.
        std::vector<TopoDS_Face> remaining;
        for(TopExp_Explorer f(result,TopAbs_FACE);f.More();f.Next()) remaining.push_back(TopoDS::Face(f.Current()));
        for(TopExp_Explorer f(solid,TopAbs_FACE);f.More();f.Next()) {
            const auto original=TopoDS::Face(f.Current());
            const gp_Vec delta=original.IsSame(face)?gp_Vec(normal)*distance:gp_Vec(0,0,0);
            const auto match=std::find_if(remaining.begin(),remaining.end(),[&](const auto& candidate) {
                return count(original,TopAbs_WIRE)==count(candidate,TopAbs_WIRE)
                    && count(original,TopAbs_EDGE)==count(candidate,TopAbs_EDGE)
                    && sameSupport(original,candidate,delta);
            });
            if(match==remaining.end()) return fail(QStringLiteral("OCCT changed an adjacent support surface or its face connectivity."));
            remaining.erase(match);
        }
        gp_Pln expected=BRepAdaptor_Surface(face).Plane();
        expected.Translate(gp_Vec(normal)*distance);
        bool targetSupportFound=false;
        for (TopExp_Explorer f(result,TopAbs_FACE);f.More();f.Next()) {
            const auto candidate=TopoDS::Face(f.Current());
            if(BRepAdaptor_Surface(candidate).GetType()!=GeomAbs_Plane) continue;
            const auto plane=BRepAdaptor_Surface(candidate).Plane();
            if(plane.Axis().Direction().IsParallel(expected.Axis().Direction(),tolerance)
               && plane.Distance(expected.Location())<1.e-5) { targetSupportFound=true; break; }
        }
        if(!targetSupportFound) return fail(QStringLiteral("the translated target plane is absent after adjacent-surface intersection."));
        return result;
    } catch (const Standard_Failure&) {
        error=QStringLiteral("Open CASCADE could not move this face. Original geometry was kept."); return {};
    }
}
