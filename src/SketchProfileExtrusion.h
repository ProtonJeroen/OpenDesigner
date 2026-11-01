#pragma once
#include "SketchProfileSweep.h"
namespace SketchProfileExtrusion {
using SketchProfileSweep::Code;
using SketchProfileSweep::Diagnostic;
using SketchProfileSweep::Result;
Result extrude(const SketchProfileBuilder::Result& faces,const gp_Dir& workplaneNormal,double signedDistance);
// target is canonical local geometry; its placement is applied exactly once.
// Returns the fused world result. Callers inverse-transform once for local Part storage.
// Through All uses the target world bounds projected onto the chosen normal.
Result cut(const SketchProfileBuilder::Result& faces,const gp_Ax3& frame,
           const TopoDS_Shape& target,const gp_Trsf& placement,
           double depth,bool reversed=false,bool throughAll=false);
// Material application accepts either certified sweep tool; target is local and
// placement is applied once. The returned Boolean result is world-space.
Result addMaterial(const Result& tool,const TopoDS_Shape& target,const gp_Trsf& placement);
Result removeMaterial(const Result& tool,const TopoDS_Shape& target,const gp_Trsf& placement);
}
