#include "MoveFaceGeometry.h"
#include "BRepReconstruction.h"
#include "CylindricalMoveGeometry.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepLib.hxx>
#include <BRep_Tool.hxx>
#include <BRepOffset_MakeOffset.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
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
#include <cmath>

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
bool supported(const TopoDS_Shape& shape) {
    if (count(shape,TopAbs_FACE)>64) return false;
    for (TopExp_Explorer f(shape,TopAbs_FACE); f.More(); f.Next()) {
        if (count(f.Current(),TopAbs_WIRE)<1) return false;
        const auto type = BRepAdaptor_Surface(TopoDS::Face(f.Current())).GetType();
        if (type!=GeomAbs_Plane && type!=GeomAbs_Cylinder && type!=GeomAbs_Cone) return false;
    }
    return true;
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
        if (!supported(solid) || !BRepReconstruction::validSolid(solid,volume)) {
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
static TopoDS_Shape offsetMove(const TopoDS_Shape& source, const TopoDS_Face& sourceFace,
                                   double distance, QString& error) {
    gp_Pnt center; gp_Dir normal;
    if (!std::isfinite(distance)) { error=QStringLiteral("Enter a finite face offset."); return {}; }
    if (!MoveFaceGeometry::reference(source,sourceFace,center,normal,error)) return {};
    if (std::abs(distance)<tolerance) return source;
    BRepReconstruction::Prepared prepared;
    if(!BRepReconstruction::prepare(source,sourceFace,prepared,error)) return {};
    const auto& solid=prepared.solid;
    const auto& face=prepared.target;
    try {
        // Work on a deep copy: the offset algorithm must never modify document topology.
        BRepBuilderAPI_Copy copy(solid,true,false);
        auto copiedFace=TopoDS::Face(copy.ModifiedShape(face));
        copiedFace.Orientation(face.Orientation());
        BRepOffset_MakeOffset offset;
        // Intersection mode is essential here: the neighboring face supports
        // stay fixed while their trimming curves are recomputed against the
        // translated target plane.
        // Boolean edits may wrap their single solid in a compound. The offset
        // builder requires the certified solid, not its presentation container.
        TopExp_Explorer solidInput(copy.Shape(),TopAbs_SOLID);
        offset.Initialize(solidInput.Current(),0,tolerance,BRepOffset_Skin,true,false,GeomAbs_Intersection);
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
        if (result.IsNull() || !supported(result) || !BRepReconstruction::validSolid(result,volume)) {
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
        gp_Pln expected=BRepAdaptor_Surface(face).Plane();
        expected.Translate(gp_Vec(normal)*distance);
        if(!BRepReconstruction::supports(solid,result,face,expected,error)) return {};
        BRepReconstruction::Result finished;
        if(!BRepReconstruction::finish(source,result,sourceFace,expected,finished,error)) return {};
        return finished.solid;
    } catch (const Standard_Failure&) {
        error=QStringLiteral("Open CASCADE could not move this face. Original geometry was kept."); return {};
    }
}

TopoDS_Shape MoveFaceGeometry::move(const TopoDS_Shape& source,const TopoDS_Face& face,
                                  double distance,QString& error) {
    const auto offset=offsetMove(source,face,distance,error);
    if(!offset.IsNull()) return offset;
    const QString offsetError=error;
    gp_Pnt center;gp_Dir normal;
    if(!std::isfinite(distance)||!reference(source,face,center,normal,error)) return {};
    BRepReconstruction::Result local;QString localError;
    if(BRepReconstruction::movePlanarLocal(source,face,distance,local,localError)) {error.clear();return local.solid;}
    const auto preserved=CylindricalMoveGeometry::movePlanarPreservingExterior(source,face,distance);
    if(preserved) {error.clear();return preserved.shape;}
    auto expected=BRepAdaptor_Surface(face).Plane();expected.Translate(gp_Vec(normal)*distance);
    BRepReconstruction::Result rebuilt;
    if(!BRepReconstruction::rebuildPlanar(source,face,expected,rebuilt,error)) {
        error=QStringLiteral("%1 Local route: %2 Planar-cell route: %3").arg(offsetError,localError,error);return {};
    }
    GProp_GProps before,after;BRepGProp::VolumeProperties(source,before);BRepGProp::VolumeProperties(rebuilt.solid,after);
    if((after.Mass()-before.Mass())*distance<=0) {
        error=QStringLiteral("Reconstructed volume did not change in the requested direction.");return {};
    }
    return rebuilt.solid;
}
