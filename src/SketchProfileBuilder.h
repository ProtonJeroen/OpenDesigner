#pragma once
#include "SketchProfileValidation.h"
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Face.hxx>

namespace SketchProfileBuilder {
enum class Code {
    InvalidValidation, InvalidPolicy, NumericalFailure, ClosureFailure,
    ToleranceExceeded, AmbiguousVertices, EdgeFailure, WireFailure, FaceFailure, AreaMismatch
};
struct Diagnostic {
    Code code;
    QString message;
    QStringList entityIds;
    std::optional<size_t> contour,region;
};
struct Policy { double maximumVertexTolerance=1.e-7; };
struct Edge { QString entityId; TopoDS_Edge shape; };
struct Wire {
    size_t contour;
    TopoDS_Wire shape;
    std::vector<Edge> edges; // Ordered source traversal, before face orientation.
    double maximumEndpointGap=0;
};
struct Face {
    size_t region,outer;
    std::vector<size_t> holes;
    QStringList entityIds;
    TopoDS_Face shape;
    double area=0;
};
struct Result {
    bool success=false;
    std::vector<Wire> wires;
    std::vector<Face> faces;
    double effectiveVertexTolerance=0;
    std::vector<Diagnostic> diagnostics;
};
// Read-only, all-or-nothing. The validation's recorded Sketch tolerance is an
// upper bound, never permission to heal curves. Source curves remain analytic;
// shared topology vertices are anchored at outgoing source endpoints and use
// exactly Policy::maximumVertexTolerance (at least OCCT Precision::Confusion).
// Independently rechecks topology, OCCT closure/orientation/validity and area.
Result build(const SketchProfileValidation::Result& profile,const gp_Ax3& frame,
             Policy policy={});
}
