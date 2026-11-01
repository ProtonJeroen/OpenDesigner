#pragma once
#include "SketchProfileBuilder.h"

// Shared headless sweep result; no Part, UI or history ownership.
namespace SketchProfileSweep {
enum class Code { InvalidInput, InvalidFace, InvalidSolid, NumericalFailure, Disconnected, Unchanged, UnsupportedAxis, SelfIntersection };
struct Diagnostic {Code code;QString message;std::optional<size_t> region;};
struct Result {
    bool success=false;
    TopoDS_Shape shape; // Solid or compound containing only solids; world space.
    size_t solidCount=0;
    double volume=0;
    std::vector<Diagnostic> diagnostics;
};
}
