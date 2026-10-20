#include "FaceTransformGeometry.h"

#include "MoveFaceGeometry.h"
#include "BRepReconstruction.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <Standard_Failure.hxx>
#include <QDebug>
#include <QStringList>
#include <gp_Vec.hxx>
#include <cmath>

namespace {
constexpr double tolerance = 1.e-7;

int count(const TopoDS_Shape& shape, TopAbs_ShapeEnum type) {
    TopTools_IndexedMapOfShape map;
    TopExp::MapShapes(shape,type,map);
    return map.Extent();
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
        if(adjacent) {
            const auto type=BRepAdaptor_Surface(candidate).GetType();
            const QString surface=type==GeomAbs_Plane?QStringLiteral("plane"):
                type==GeomAbs_Cylinder?QStringLiteral("cylinder"):
                type==GeomAbs_Cone?QStringLiteral("cone"):QStringLiteral("other");
            int wires=0; for(TopExp_Explorer w(candidate,TopAbs_WIRE);w.More();w.Next()) ++wires;
            neighbors<<QStringLiteral("%1(wires=%2)").arg(surface).arg(wires);
        }
    }
    int wires=0,edges=0;
    for(TopExp_Explorer w(target,TopAbs_WIRE);w.More();w.Next()) ++wires;
    for(TopExp_Explorer e(target,TopAbs_EDGE);e.More();e.Next()) ++edges;
    return QStringLiteral("solids=%1 shells=%2 faces=%3 edges=%4 vertices=%5; target=%6 wires/%7 edges; neighbors=[%8]")
        .arg(count(solid,TopAbs_SOLID)).arg(count(solid,TopAbs_SHELL)).arg(count(solid,TopAbs_FACE))
        .arg(count(solid,TopAbs_EDGE)).arg(count(solid,TopAbs_VERTEX)).arg(wires).arg(edges)
        .arg(neighbors.join(QStringLiteral(", ")));
}

bool validResult(const TopoDS_Shape& original, const TopoDS_Shape& result,
                 QString& error) {
    Q_UNUSED(original);
    double volume=0;
    if(!BRepReconstruction::validSolid(result,volume)) {
        error=QStringLiteral("the reconstructed result is not one valid, closed, non-self-intersecting solid.");
        return false;
    }
    return true;
}

bool validTarget(const TopoDS_Shape& solid, const TopoDS_Face& face,
                 gp_Pnt& center, gp_Dir& normal, QString& error) {
    if (!MoveFaceGeometry::reference(solid,face,center,normal,error)) {
        error=QStringLiteral("Face operation refused: ")+error;
        return false;
    }
    return true;
}

TopoDS_Shape draft(const TopoDS_Shape& source, const TopoDS_Face& sourceFace,
                   const gp_Ax1& axis, double angleDegrees, QString& error,
                   TopoDS_Face* modifiedTarget = nullptr) {
    gp_Pnt center;
    gp_Dir normal;
    if (!validTarget(source,sourceFace,center,normal,error)) return {};
    if (!std::isfinite(angleDegrees) || std::abs(angleDegrees)>=179.0) {
        error=QStringLiteral("enter a finite angle with magnitude below 179 degrees.");
        return {};
    }
    const gp_Pln facePlane=BRepAdaptor_Surface(sourceFace).Plane();
    if (std::abs(facePlane.Distance(axis.Location()))>1.e-5
        || std::abs(axis.Direction().Dot(normal))>1.e-6) {
        error=QStringLiteral("the rotation axis must be a straight line in the selected planar face.");
        return {};
    }
    if (std::abs(angleDegrees)<1.e-10) return source;
    BRepReconstruction::Prepared prepared;
    if(!BRepReconstruction::prepare(source,sourceFace,prepared,error)) return {};
    const auto& solid=prepared.solid;
    const auto& face=prepared.target;


    // DraftAngle's Direction/Flag pair specifies which side of its neutral
    // plane is material. Try both valid side conventions and retain only the
    // candidate whose target plane matches the requested signed rotation.
    const gp_Dir neutralNormal(gp_Vec(axis.Direction()).Crossed(gp_Vec(normal)));
    const gp_Pln neutralPlane(axis.Location(),neutralNormal);
    gp_Pln expected=facePlane;
    gp_Trsf requestedRotation;
    requestedRotation.SetRotation(axis,angleDegrees*std::acos(-1.0)/180.0);
    expected.Transform(requestedRotation);
    int lastStatus=0;
    QString lastFailure;
    for(const gp_Dir direction : {normal,gp_Dir(-normal.X(),-normal.Y(),-normal.Z()),neutralNormal,
                                  gp_Dir(-neutralNormal.X(),-neutralNormal.Y(),-neutralNormal.Z())}) {
        for(const bool flag : {Standard_True,Standard_False}) {
            try {
                BRepBuilderAPI_Copy copy(solid,true,false);
                const auto copiedFace=TopoDS::Face(copy.ModifiedShape(face));
                BRepOffsetAPI_DraftAngle modifier(copy.Shape());
                modifier.Add(copiedFace,direction,angleDegrees*std::acos(-1.0)/180.0,neutralPlane,flag);
                if(!modifier.AddDone()) { lastStatus=static_cast<int>(modifier.Status()); continue; }
                modifier.Build();
                if(modifier.IsDone()) modifier.CorrectWires();
                if(!modifier.IsDone()) { lastStatus=static_cast<int>(modifier.Status()); continue; }
                const auto result=modifier.Shape();
                QString validationError;
                if(!validResult(solid,result,validationError)) { lastFailure=validationError; continue; }
                TopoDS_Face changedTarget;
                int targetMatches=0;
                for(TopExp_Explorer it(result,TopAbs_FACE);it.More();it.Next()) {
                    const auto candidate=TopoDS::Face(it.Current());
                    const BRepAdaptor_Surface candidateSurface(candidate);
                    if(candidateSurface.GetType()!=GeomAbs_Plane) continue;
                    const auto resultPlane=candidateSurface.Plane();
                    if(resultPlane.Axis().Direction().IsParallel(expected.Axis().Direction(),1.e-5)
                       && resultPlane.Distance(expected.Location())<=1.e-4) { changedTarget=candidate; ++targetMatches; }
                }
                if(changedTarget.IsNull()) { lastFailure=QStringLiteral("the requested rotated target plane was not present in the result."); continue; }
                if(modifiedTarget && targetMatches!=1) {
                    lastFailure=QStringLiteral("the aligned target split into ambiguous patches."); continue;
                }
                if(!BRepReconstruction::supports(solid,result,face,expected,validationError)) {
                    lastFailure=validationError; continue;
                }
                if(modifiedTarget) *modifiedTarget=changedTarget;
                return result;
            } catch(const Standard_Failure&) {
                continue;
            }
        }
    }
    error=QStringLiteral("Open CASCADE could not rotate this face while constructing a valid closed solid (kernel status %1: %2). Original geometry was kept.").arg(lastStatus).arg(lastFailure);
    qWarning().noquote()<<"Rotate Face rejected:"<<error<<topologyDetails(solid,face);
    return {};
}
}

TopoDS_Shape FaceTransformGeometry::rotate(const TopoDS_Shape& solid,
        const TopoDS_Face& face, const gp_Ax1& axis, double angleDegrees, QString& error) {
    error.clear();
    return draft(solid,face,axis,angleDegrees,error);
}

TopoDS_Shape FaceTransformGeometry::align(const TopoDS_Shape& solid,
        const TopoDS_Face& target, const gp_Pln& referencePlane,
        bool coplanar, QString& error) {
    error.clear();
    gp_Pnt center;
    gp_Dir normal;
    if (!validTarget(solid,target,center,normal,error)) return {};
    try {
        gp_Dir referenceNormal=referencePlane.Axis().Direction();
        if(normal.Dot(referenceNormal)<0.0) referenceNormal.Reverse();
        const gp_Vec cross=gp_Vec(normal).Crossed(gp_Vec(referenceNormal));
        TopoDS_Shape result=solid;
        TopoDS_Face alignedFace=target;
        if (cross.Magnitude()>1.e-7) {
            const gp_Ax1 axis(center,gp_Dir(cross));
            result=draft(solid,target,axis,
                std::atan2(cross.Magnitude(),normal.Dot(referenceNormal))*180.0/std::acos(-1.0),
                error,&alignedFace);
            if (result.IsNull()) return {};
        }
        if (!coplanar) return result;

        GProp_GProps targetProperties;
        BRepGProp::SurfaceProperties(alignedFace,targetProperties);
        const gp_Pnt alignedCenter=targetProperties.CentreOfMass();
        gp_Dir outward=BRepAdaptor_Surface(alignedFace).Plane().Axis().Direction();
        if (alignedFace.Orientation()==TopAbs_REVERSED) outward.Reverse();
        const double distance=gp_Vec(alignedCenter,referencePlane.Location()).Dot(gp_Vec(outward));
        if (std::abs(distance)<=tolerance) return result;
        const auto moved=MoveFaceGeometry::move(result,alignedFace,distance,error);
        if (moved.IsNull()) {
            error=QStringLiteral("Align Face refused: ")+error;
            return {};
        }
        if (!validResult(solid,moved,error)) return {};
        return moved;
    } catch (const Standard_Failure&) {
        error=QStringLiteral("Open CASCADE refused this face alignment; the original solid is unchanged.");
        return {};
    }
}
