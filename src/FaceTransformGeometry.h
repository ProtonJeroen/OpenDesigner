#pragma once

#include <QString>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Pln.hxx>

namespace FaceTransformGeometry {

// Rotation angle is in degrees. Axis coordinates are in the source B-Rep frame.
TopoDS_Shape rotate(const TopoDS_Shape& solid, const TopoDS_Face& face,
                    const gp_Ax1& axis, double angleDegrees, QString& error);

// Align the target face to a reference plane. Parallel preserves the target
// face centroid; Coplanar also moves it onto the reference plane.
TopoDS_Shape align(const TopoDS_Shape& solid, const TopoDS_Face& target,
                   const gp_Pln& referencePlane, bool coplanar, QString& error);

}
