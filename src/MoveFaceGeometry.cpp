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
#include <gp_Cylinder.hxx>
#include <gp_Cone.hxx>
#include <gp_Pln.hxx>
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
        if (count(f.Current(),TopAbs_WIRE)!=1) return false;
        const auto type = BRepAdaptor_Surface(TopoDS::Face(f.Current())).GetType();
        if (type!=GeomAbs_Plane && type!=GeomAbs_Cylinder && type!=GeomAbs_Cone) return false;
    }
    return true;
}
bool sameSupport(const TopoDS_Face& original, const TopoDS_Face& result,
                 const gp_Vec& displacement) {
    BRepAdaptor_Surface a(original), b(result);
    if (a.GetType()!=b.GetType()) return false;
    if (a.GetType()==GeomAbs_Plane) {
        auto plane = a.Plane(); plane.Translate(displacement);
        return plane.Axis().Direction().IsParallel(b.Plane().Axis().Direction(),tolerance)
            && plane.Distance(b.Plane().Location())<1.e-5;
    }
    if (a.GetType()==GeomAbs_Cylinder) {
        const auto x=a.Cylinder(), y=b.Cylinder();
        return x.Axis().IsCoaxial(y.Axis(),tolerance,1.e-5) && std::abs(x.Radius()-y.Radius())<1.e-5;
    }
    if (a.GetType()==GeomAbs_Cone) {
        const auto x=a.Cone(), y=b.Cone();
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
            error=QStringLiteral("Select one planar face of the solid."); return false;
        }
        double volume=0;
        if (!supported(solid) || !valid(solid,volume)) {
            error=QStringLiteral("Move Face supports one valid solid with one shell, single-wire faces and only plane/cylinder/cone surfaces (up to 64 faces).");
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
        offset.Initialize(copy.Shape(),0,tolerance,BRepOffset_Skin,false,false,GeomAbs_Intersection);
        offset.SetOffsetOnFace(copiedFace,distance);
        offset.MakeOffsetShape();
        const auto fail=[&](const QString& reason) -> TopoDS_Shape {
            error=QStringLiteral("Move Face refused: ")+reason;
            return {};
        };
        if (!offset.IsDone()) return fail(QStringLiteral("Open CASCADE could not intersect the offset and neighbouring surfaces."));
        TopoDS_Shape result=offset.Shape();
        // OCCT sometimes returns the closed offset shell instead of its solid.
        // Promote only that single closed shell; never sew or heal an open result.
        if (!result.IsNull() && result.ShapeType()==TopAbs_SHELL && BRep_Tool::IsClosed(result)) {
            auto solidResult=BRepBuilderAPI_MakeSolid(TopoDS::Shell(result)).Solid();
            if (!BRepLib::OrientClosedSolid(solidResult)) return fail(QStringLiteral("the offset shell cannot bound a solid."));
            result=solidResult;
        }
        double volume=0;
        if (result.IsNull() || !supported(result) || !valid(result,volume)) return fail(QStringLiteral("the result is not a valid supported single solid."));
        for (const auto type : {TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX})
            if (count(result,type)!=count(solid,type)) return fail(QStringLiteral("this distance changes topology (faces, edges or vertices)."));
        GProp_GProps before;
        BRepGProp::VolumeProperties(solid,before);
        if ((volume-before.Mass())*distance<=0) return fail(QStringLiteral("volume did not change in the requested direction."));
        // Check the geometric contract, not only IsDone: every unselected support
        // surface stays fixed; exactly the selected plane is translated.
        std::vector<TopoDS_Face> faces;
        for (TopExp_Explorer f(result,TopAbs_FACE);f.More();f.Next()) faces.push_back(TopoDS::Face(f.Current()));
        for (TopExp_Explorer f(solid,TopAbs_FACE);f.More();f.Next()) {
            const auto original=TopoDS::Face(f.Current());
            const gp_Vec delta=original.IsSame(face) ? gp_Vec(normal)*distance : gp_Vec(0,0,0);
            const auto match=std::find_if(faces.begin(),faces.end(),[&](const auto& candidate) {
                return sameSupport(original,candidate,delta);
            });
            if (match==faces.end()) return fail(QStringLiteral("the requested supporting surfaces were not preserved."));
            faces.erase(match);
        }
        return result;
    } catch (const Standard_Failure&) {
        error=QStringLiteral("Open CASCADE could not move this face. Original geometry was kept."); return {};
    }
}
