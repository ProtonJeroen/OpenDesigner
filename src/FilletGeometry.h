#pragma once
#include <QString>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Edge.hxx>
namespace FilletGeometry {
// Millimeters, one manifold edge. Null on failure; source is never modified.
TopoDS_Shape apply(const TopoDS_Shape& source, const TopoDS_Edge& edge,
                   double distance, QString& error);
}
