#include "FaceTransformGeometry.h"

#include "MoveFaceGeometry.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <BOPAlgo_ArgumentAnalyzer.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <Standard_Failure.hxx>
#include <QDebug>
#include <QStringList>
#include <gp_Vec.hxx>
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
    if (result.IsNull() || count(result,TopAbs_SOLID)!=1 || count(result,TopAbs_SHELL)!=1
        || !BRepCheck_Analyzer(result).IsValid()) {
        error=QStringLiteral("the result is not one valid closed solid (solids=%1, shells=%2, faces=%3, edges=%4, vertices=%5).")
            .arg(count(result,TopAbs_SOLID)).arg(count(result,TopAbs_SHELL)).arg(count(result,TopAbs_FACE))
            .arg(count(result,TopAbs_EDGE)).arg(count(result,TopAbs_VERTEX));
        return false;
    }
    TopExp_Explorer solid(result,TopAbs_SOLID);
    for(const auto type : {TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX}) {
        if(count(result,type)!=count(solid.Current(),type)) {
            error=QStringLiteral("the result contains topology outside its solid.");
            return false;
        }
    }
    // DraftAngle may legitimately split/merge faces on Boolean results. BRepCheck
    // and self-interference validation below are the safety boundary.
    GProp_GProps volume;
    BRepGProp::VolumeProperties(result,volume);
    if (!std::isfinite(volume.Mass()) || volume.Mass()<=1.e-12) {
        error=QStringLiteral("the result has no positive finite volume.");
        return false;
    }
    BRepBuilderAPI_Copy isolated(result,true,false);
    BOPAlgo_ArgumentAnalyzer selfCheck;
    selfCheck.SetShape1(isolated.Shape());
    selfCheck.SelfInterMode()=true;
    selfCheck.StopOnFirstFaulty()=true;
    selfCheck.Perform();
    if (selfCheck.HasErrors() || selfCheck.HasFaulty()) {
        error=QStringLiteral("the result is self-intersecting or invalid.");
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
bool sameSupport(const TopoDS_Face& original,const TopoDS_Face& result,
                 bool selected,const gp_Pln& expectedTarget) {
    const BRepAdaptor_Surface a(original),b(result);
    if(a.GetType()!=b.GetType()) return false;
    if(a.GetType()==GeomAbs_Plane) {
        const gp_Pln plane=selected?expectedTarget:a.Plane();
        return plane.Axis().Direction().IsParallel(b.Plane().Axis().Direction(),1.e-7)
            && plane.Distance(b.Plane().Location())<1.e-5;
    }
    if(a.GetType()==GeomAbs_Cylinder) {
        const auto x=a.Cylinder(),y=b.Cylinder();
        return x.Axis().IsCoaxial(y.Axis(),1.e-7,1.e-5)&&std::abs(x.Radius()-y.Radius())<1.e-5;
    }
    if(a.GetType()==GeomAbs_Cone) {
        const auto x=a.Cone(),y=b.Cone();
        return x.Axis().Direction().IsParallel(y.Axis().Direction(),1.e-7)
            &&x.Apex().Distance(y.Apex())<1.e-5
            &&std::abs(std::abs(x.SemiAngle())-std::abs(y.SemiAngle()))<1.e-7;
    }
    return false;
}
bool preservesTopologyAndSupports(const TopoDS_Shape& original,const TopoDS_Shape& result,
                                  const TopoDS_Face& target,const gp_Pln& expected,QString& error) {
    for(const auto type : {TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX}) {
        if(count(original,type)!=count(result,type)) {
            error=QStringLiteral("the operation changes face/edge/vertex topology (%1 -> %2).")
                .arg(count(original,type)).arg(count(result,type));
            return false;
        }
    }
    std::vector<TopoDS_Face> remaining;
    for(TopExp_Explorer f(result,TopAbs_FACE);f.More();f.Next()) remaining.push_back(TopoDS::Face(f.Current()));
    for(TopExp_Explorer f(original,TopAbs_FACE);f.More();f.Next()) {
        const auto source=TopoDS::Face(f.Current());
        const bool selected=source.IsSame(target);
        const auto match=std::find_if(remaining.begin(),remaining.end(),[&](const auto& candidate) {
            int sourceWires=0,candidateWires=0,sourceEdges=0,candidateEdges=0;
            for(TopExp_Explorer w(source,TopAbs_WIRE);w.More();w.Next()) ++sourceWires;
            for(TopExp_Explorer w(candidate,TopAbs_WIRE);w.More();w.Next()) ++candidateWires;
            for(TopExp_Explorer e(source,TopAbs_EDGE);e.More();e.Next()) ++sourceEdges;
            for(TopExp_Explorer e(candidate,TopAbs_EDGE);e.More();e.Next()) ++candidateEdges;
            return sourceWires==candidateWires&&sourceEdges==candidateEdges
                &&sameSupport(source,candidate,selected,expected);
        });
        if(match==remaining.end()) {
            error=QStringLiteral("OCCT changed an adjacent support surface or face connectivity.");
            return false;
        }
        remaining.erase(match);
    }
    return true;
}

TopoDS_Shape draft(const TopoDS_Shape& solid, const TopoDS_Face& face,
                   const gp_Ax1& axis, double angleDegrees, QString& error,
                   TopoDS_Face* modifiedTarget = nullptr) {
    gp_Pnt center;
    gp_Dir normal;
    if (!validTarget(solid,face,center,normal,error)) return {};
    if (!std::isfinite(angleDegrees) || std::abs(angleDegrees)>=179.0) {
        error=QStringLiteral("enter a finite angle with magnitude below 179 degrees.");
        return {};
    }
    const gp_Pln facePlane=BRepAdaptor_Surface(face).Plane();
    if (std::abs(facePlane.Distance(axis.Location()))>1.e-5
        || std::abs(axis.Direction().Dot(normal))>1.e-6) {
        error=QStringLiteral("the rotation axis must be a straight line in the selected planar face.");
        return {};
    }
    if (std::abs(angleDegrees)<1.e-10) return solid;

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
                for(TopExp_Explorer it(result,TopAbs_FACE);it.More();it.Next()) {
                    const auto candidate=TopoDS::Face(it.Current());
                    const BRepAdaptor_Surface candidateSurface(candidate);
                    if(candidateSurface.GetType()!=GeomAbs_Plane) continue;
                    const auto resultPlane=candidateSurface.Plane();
                    if(resultPlane.Axis().Direction().IsParallel(expected.Axis().Direction(),1.e-5)
                       && resultPlane.Distance(expected.Location())<=1.e-4) { changedTarget=candidate; break; }
                }
                if(changedTarget.IsNull()) { lastFailure=QStringLiteral("the requested rotated target plane was not present in the result."); continue; }
                if(!preservesTopologyAndSupports(solid,result,face,expected,validationError)) {
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
