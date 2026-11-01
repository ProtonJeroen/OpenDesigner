#pragma once
#include "SketchProfileSweep.h"
#include <gp_Ax1.hxx>

namespace SketchProfileRevolution {
using SketchProfileSweep::Code;
using SketchProfileSweep::Diagnostic;
using SketchProfileSweep::Result;

// Certified world-space planar faces and a world-space axis in their plane.
// All regions must occupy the same closed half-plane bounded by the axis.
// Signed degrees: 1 <= abs(angle) <= 360, right-hand rule about axis.Direction().
// Read-only/all-or-nothing. Returns world solids, never Parts or Boolean results.
Result revolve(const SketchProfileBuilder::Result& faces, const gp_Ax1& axis,
               double signedDegrees);
}
