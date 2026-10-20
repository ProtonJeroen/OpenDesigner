#pragma once
#include <QString>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pln.hxx>
#include <vector>

// Geometry-only preparation and acceptance policy shared by all face commands.
namespace BRepReconstruction {
struct FaceMapping { TopoDS_Face original; std::vector<TopoDS_Face> modified; };
struct Prepared {
    TopoDS_Shape solid;
    TopoDS_Face target;
    std::vector<FaceMapping> history;
};
bool prepare(const TopoDS_Shape& source, const TopoDS_Face& target,
             Prepared& result, QString& error);
bool validSolid(const TopoDS_Shape& shape, double& volume);
bool supports(const TopoDS_Shape& source, const TopoDS_Shape& result,
              const TopoDS_Face& target, const gp_Pln& expected, QString& error);
}
