#pragma once
#include "SketchProfileBuilder.h"
namespace SketchProfileExtrusion {
enum class Code { InvalidInput, InvalidFace, InvalidSolid, NumericalFailure, Disconnected, Unchanged };
struct Diagnostic {Code code;QString message;std::optional<size_t> region;};
struct Result {
    bool success=false;
    TopoDS_Shape shape; // Solid or compound containing only solids; world space.
    size_t solidCount=0;
    double volume=0;
    std::vector<Diagnostic> diagnostics;
};
Result extrude(const SketchProfileBuilder::Result& faces,const gp_Dir& workplaneNormal,double signedDistance);
// target is canonical local geometry; its placement is applied exactly once.
// Returns the fused world result. Callers inverse-transform once for local Part storage.
Result addMaterial(const Result& tool,const TopoDS_Shape& target,const gp_Trsf& placement);
}
