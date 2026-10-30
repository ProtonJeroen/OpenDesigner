#pragma once
#include "BRepReconstruction.h"
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRep_Tool.hxx>
#include <BRepGProp.hxx>
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
    if (shape.IsNull() || !TopExp_Explorer(shape,TopAbs_SOLID).More()
        || !BRepCheck_Analyzer(shape).IsValid()) return false;
    for(TopExp_Explorer shell(shape,TopAbs_SHELL);shell.More();shell.Next())
        if(!BRep_Tool::IsClosed(shell.Current())) return false;
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape,properties);
    volume = properties.Mass();
    return std::isfinite(volume) && volume > 1.0e-12;
}
template<class Algorithm>
inline TopoDS_Shape combine(const TopoDS_Shape& a, const TopoDS_Shape& b) {
    Algorithm algorithm;
    TopTools_ListOfShape arguments, tools;
    arguments.Append(a); tools.Append(b);
    algorithm.SetArguments(arguments);
    algorithm.SetTools(tools);
    algorithm.SetNonDestructive(Standard_True);
    algorithm.Build();
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
