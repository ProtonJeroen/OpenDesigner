#pragma once
// Internal raw construction steps, shared with the isolated F2.9B experiment.
// These return geometry, NEVER a reconstruction/material certificate.
#include "BooleanGeometry.h"
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Vec.hxx>
namespace BRepReconstruction::detail {
inline TopoDS_Shape sweepPatch(const TopoDS_Face& patch,const gp_Vec& delta) {
    BRepTiming::Event sweptEvent("Prism",patch);
    const auto swept=BRepPrimAPI_MakePrism(patch,delta,true,true).Shape();
    sweptEvent.finish(swept);return swept;
}
inline TopoDS_Shape applySweep(const TopoDS_Shape& source,const TopoDS_Shape& region,double distance) {
    return distance>0?BooleanGeometry::combine<BRepAlgoAPI_Fuse>(source,region)
                     :BooleanGeometry::combine<BRepAlgoAPI_Cut>(source,region);
}
}
