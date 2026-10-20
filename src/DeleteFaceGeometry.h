#pragma once
#include "BRepReconstruction.h"

namespace DeleteFaceGeometry {
// Immutable, all-or-nothing deletion. Only certified analytic feature removal
// and bounded planar support/step reconstruction are accepted.
bool remove(const TopoDS_Shape& source, const std::vector<TopoDS_Face>& faces,
            BRepReconstruction::Result& result, QString& error);
}
