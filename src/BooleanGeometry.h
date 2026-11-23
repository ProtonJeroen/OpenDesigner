#pragma once
#include "BRepTiming.h"
#include "BRepReconstruction.h"
#include "BRepCertification.h"
#include "BRepMeasurement.h"
#include "BRepMeasurementPrivate.h"
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <Precision.hxx>
#include <TopoDS.hxx>
#include <BRep_Tool.hxx>
#include <BRepGProp.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <Adaptor3d_Curve.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS_Compound.hxx>
#include <cmath>
#include <stdexcept>
// Shared, unchanged Boolean validation and normalization used by Unite and
// profile Add Material / Cut Extrude. No Part, widget, history or document ownership here.
namespace BooleanGeometry {
inline bool validSolid(const TopoDS_Shape& shape, double& volume) {
    BRepTiming::Scope timingScope("BooleanGeometry.validSolid");
    if (shape.IsNull() || !TopExp_Explorer(shape,TopAbs_SOLID).More()
        || !BRepCertification::validBRep(shape)) return false;
    if(!BRepCertification::closedShells(shape)) return false;
    BRepMeasurement::Result measured;QString error;
    // The same default-options input validity predicate already passed above;
    // only read-only shell traversal intervenes. This borrows that exact shape,
    // not a root certificate transferable to components/copies or later calls.
    const BRepMeasurement::detail::ValidatedInputVolume checked(shape);
    if(!checked.compute(measured,error)) return false;
    volume=measured.properties.Mass();
    return std::isfinite(volume) && volume > 1.0e-12;
}
// Shared sweep certificate: closed positive solids with outward orientation.
inline bool certifiedSolid(const TopoDS_Shape& shape,double& volume,size_t& count) {
    BRepTiming::Scope timingScope("BooleanGeometry.certifiedSolid");
    count=0;if(!validSolid(shape,volume))return false;
    for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next()) {
        // No mutation occurs between the root certificate and this traversal.
        // IsEqual includes topology, location and orientation; compounds still
        // validate each component independently, even when they hold one solid.
        const bool validatedRoot=shape.ShapeType()==TopAbs_SOLID&&it.Current().IsEqual(shape);
        double v=volume;
        if(validatedRoot) {BRepTiming::Scope reused("BooleanGeometry.certifiedSolid.reusedRootValidation");}
        else if(!validSolid(it.Current(),v))return false;
        if(v<=0)return false;
        BRepClass3d_SolidClassifier classifier(TopoDS::Solid(it.Current()));classifier.PerformInfinitePoint(Precision::Confusion());
        if(classifier.State()!=TopAbs_OUT)return false;++count;
    }
    return count>0;
}
template<class Algorithm>
inline TopoDS_Shape combine(const TopoDS_Shape& a, const TopoDS_Shape& b) {
    BRepTiming::Scope timingScope(typeid(Algorithm).name());
    Algorithm algorithm;
    TopTools_ListOfShape arguments, tools;
    arguments.Append(a); tools.Append(b);
    algorithm.SetArguments(arguments);
    algorithm.SetTools(tools);
    algorithm.SetNonDestructive(Standard_True);
    BRepTiming::Event booleanEvent(std::string("Boolean.")+typeid(Algorithm).name(),a,b);
    algorithm.Build();booleanEvent.finish(algorithm.IsDone()&&!algorithm.HasErrors()?algorithm.Shape():TopoDS_Shape{});
    if (!algorithm.IsDone() || algorithm.HasErrors())
        throw std::runtime_error("Open CASCADE could not complete the Boolean operation.");
    return algorithm.Shape();
}
// Boolean common can include lower-dimensional contacts. Keep only solid volume.
inline TopoDS_Shape solidResult(const TopoDS_Shape& shape, int& count) {
    count = 0;
    if (shape.IsNull()) return {};
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    TopoDS_Shape first;
    for (TopExp_Explorer solids(shape,TopAbs_SOLID);solids.More();solids.Next()) {
        if (count++ == 0) first = solids.Current();
        builder.Add(compound,solids.Current());
    }
    return count == 1 ? first : TopoDS_Shape(compound);
}
inline TopoDS_Shape normalizeBooleanResult(const TopoDS_Shape& shape) {
    // Merge redundant face/edge divisions before presentation and history capture.
    // Face reconstruction's stricter single-shell validator cannot be used here:
    // Boolean Parts may contain disconnected solids and solids with cavities.
    BRepBuilderAPI_Copy copy(shape,Standard_True,Standard_False);
    ShapeUpgrade_UnifySameDomain unify(copy.Shape(),Standard_True,Standard_True,Standard_False);
    unify.SetSafeInputMode(Standard_True);
    unify.Build();
    const auto count=[](const TopoDS_Shape& s,TopAbs_ShapeEnum type) {
        int n=0; for(TopExp_Explorer it(s,type);it.More();it.Next()) ++n;
        return n;
    };
    const auto result=unify.Shape();
    double volume=0;
    if (!validSolid(result,volume)
        || count(shape,TopAbs_SOLID)!=count(result,TopAbs_SOLID)
        || count(shape,TopAbs_SHELL)!=count(result,TopAbs_SHELL)
        || !BRepReconstruction::equivalent(shape,result))
        throw std::runtime_error("Final Boolean unification changed material or invalidated the solid geometry.");
    return result;
}
}
