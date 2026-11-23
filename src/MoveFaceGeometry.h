#pragma once
#include <QString>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>

namespace MoveFaceGeometry {
enum class ReconstructionPolicy { Existing, AxialEllipticalCap, AxialInvariantCap };
// All coordinates and distances are in the canonical shape's coordinate system.
bool reference(const TopoDS_Shape& solid, const TopoDS_Face& face,
               gp_Pnt& center, gp_Dir& outward, QString& error);
// Lightweight command reference for the certified spline Move route only.
// Success is eligibility, NOT source/material certification or a preview receipt.
bool referenceForMove(const TopoDS_Shape& solid, const TopoDS_Face& face,
                      gp_Pnt& center, gp_Dir& outward, QString& error);
TopoDS_Shape move(const TopoDS_Shape& solid, const TopoDS_Face& face,
                  double distance, QString& error,
                  ReconstructionPolicy policy = ReconstructionPolicy::Existing);
}
