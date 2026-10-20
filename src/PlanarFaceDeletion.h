#pragma once
#include "BRepReconstruction.h"

// Internal, bounded certificate for trimming planar sections across a deleted
// transition patch. This does not accept arbitrary defeaturing candidates.
namespace PlanarFaceDeletion {
bool rebuild(const TopoDS_Shape& source, const std::vector<TopoDS_Face>& faces,
             BRepReconstruction::Result& result, QString& error);
}
