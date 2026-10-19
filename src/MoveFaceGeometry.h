#pragma once
#include <QString>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>

namespace MoveFaceGeometry {
// All coordinates and distances are in the canonical shape's coordinate system.
bool reference(const TopoDS_Shape& solid, const TopoDS_Face& face,
               gp_Pnt& center, gp_Dir& outward, QString& error);
TopoDS_Shape move(const TopoDS_Shape& solid, const TopoDS_Face& face,
                  double distance, QString& error);
}
