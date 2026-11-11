#pragma once
#include "BRepReconstruction.h"
#include <BRepBuilderAPI_Copy.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <Standard_Failure.hxx>
#include <cmath>
namespace EdgeBlendValidation {
inline bool input(const TopoDS_Shape& source,const TopoDS_Edge& edge,double distance,
                  TopTools_IndexedDataMapOfShapeListOfShape& adjacent,QString& error) {
    if(!std::isfinite(distance) || distance<=0) {
        error=QStringLiteral("Enter a finite positive value in millimeters.");return false;
    }
    double volume=0;
    if(!BRepReconstruction::validSolid(source,volume)) {
        error=QStringLiteral("Select an edge on one valid closed solid.");return false;
    }
    TopExp::MapShapesAndUniqueAncestors(source,TopAbs_EDGE,TopAbs_FACE,adjacent);
    if(edge.IsNull() || !adjacent.Contains(edge) || BRep_Tool::Degenerated(edge)
       || adjacent.FindFromKey(edge).Extent()!=2) {
        error=QStringLiteral("Select one non-degenerate manifold edge with two distinct adjacent faces.");return false;
    }
    for(const auto& face:adjacent.FindFromKey(edge))
        if(BRep_Tool::IsClosed(edge,TopoDS::Face(face))) {
            error=QStringLiteral("Seam edges cannot be blended. Select a boundary between two faces.");return false;
        }
    return true;
}
}
