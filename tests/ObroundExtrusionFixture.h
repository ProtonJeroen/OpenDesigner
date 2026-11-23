#pragma once
#include "BooleanGeometry.h"
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepLib.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>
#include <gp_Cylinder.hxx>
#include <numbers>

namespace ObroundFixture {
inline double area() {return 2400+300*std::numbers::pi;}
inline gp_Trsf frame() {
    gp_Trsf rotation;rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,0,0)),std::numbers::pi/6);
    return rotation;
}
inline gp_Pnt world(double x,double y,double z) {return gp_Pnt(x,y,z).Transformed(frame());}
inline gp_Dir axis() {return gp_Dir(0,0,1).Transformed(frame());}
inline TopoDS_Face localProfile(double z=0) {
    const gp_Pnt a(-30,-20,z),b(30,-20,z),c(30,20,z),d(-30,20,z);
    const Handle(Geom_TrimmedCurve) right=GC_MakeArcOfCircle(b,gp_Pnt(50,0,z),c).Value();
    const Handle(Geom_TrimmedCurve) left=GC_MakeArcOfCircle(d,gp_Pnt(-50,0,z),a).Value();
    BRepBuilderAPI_MakeWire outer;
    outer.Add(BRepBuilderAPI_MakeEdge(a,b).Edge());outer.Add(BRepBuilderAPI_MakeEdge(right).Edge());
    outer.Add(BRepBuilderAPI_MakeEdge(c,d).Edge());outer.Add(BRepBuilderAPI_MakeEdge(left).Edge());
    if(!outer.IsDone()) throw Standard_Failure("Synthetic obround outer wire failed.");
    auto inner=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(0,0,z),gp_Dir(0,0,1)),10)).Edge()).Wire();
    inner.Reverse();BRepBuilderAPI_MakeFace face(outer.Wire());face.Add(inner);
    if(!face.IsDone()) throw Standard_Failure("Synthetic obround profile failed.");
    return face.Face();
}
inline TopoDS_Face profile(double z=0) {
    return TopoDS::Face(BRepBuilderAPI_Transform(localProfile(z),frame(),true).Shape());
}
inline TopoDS_Shape prism(double height=20) {
    return BRepPrimAPI_MakePrism(profile(),gp_Vec(axis())*height,true,true).Shape();
}
// A second eligible route: independently bounded planes/cylinders and caps,
// assembled into a shell. No whole-profile prism or Boolean construction.
inline TopoDS_Shape assembled(double height=20) {
    BRepBuilderAPI_Sewing sewing(1.e-7);
    auto bottom=localProfile();bottom.Reverse();sewing.Add(bottom);sewing.Add(localProfile(height));
    for(double y:{-20.,20.}) {
        const double from=y<0?-30:30,to=-from;
        BRepBuilderAPI_MakePolygon polygon;
        polygon.Add(gp_Pnt(from,y,0));polygon.Add(gp_Pnt(to,y,0));
        polygon.Add(gp_Pnt(to,y,height));polygon.Add(gp_Pnt(from,y,height));polygon.Close();
        sewing.Add(BRepBuilderAPI_MakeFace(polygon.Wire()).Face());
    }
    const double pi=std::numbers::pi;
    sewing.Add(BRepBuilderAPI_MakeFace(gp_Cylinder(gp_Ax3(gp_Pnt(30,0,0),gp_Dir(0,0,1)),20),-pi/2,pi/2,0,height).Face());
    sewing.Add(BRepBuilderAPI_MakeFace(gp_Cylinder(gp_Ax3(gp_Pnt(-30,0,0),gp_Dir(0,0,1)),20),pi/2,3*pi/2,0,height).Face());
    auto hole=BRepBuilderAPI_MakeFace(gp_Cylinder(gp_Ax3(gp_Pnt(0,0,0),gp_Dir(0,0,1)),10),0,2*pi,0,height).Face();
    hole.Reverse();sewing.Add(hole);sewing.Perform();
    auto solid=BRepBuilderAPI_MakeSolid(TopoDS::Shell(sewing.SewedShape())).Solid();
    if(!BRepLib::OrientClosedSolid(solid)) throw Standard_Failure("Synthetic obround shell orientation failed.");
    return BRepBuilderAPI_Transform(solid,frame(),true).Shape();
}
// Independent source/material oracle: rectangle plus two complete cylinders,
// followed by a central cylindrical subtraction and material-certified cleanup.
inline TopoDS_Shape booleanSolid(double height=20) {
    TopoDS_Shape shape=BRepPrimAPI_MakeBox(gp_Pnt(-30,-20,0),60,40,height).Shape();
    for(double x:{-30.,30.})
        shape=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(shape,
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x,0,0),gp_Dir(0,0,1)),20,height).Shape());
    shape=BooleanGeometry::combine<BRepAlgoAPI_Cut>(shape,BRepPrimAPI_MakeCylinder(10,height).Shape());
    shape=BooleanGeometry::normalizeBooleanResult(shape);
    return BRepBuilderAPI_Transform(shape,frame(),true).Shape();
}
}
