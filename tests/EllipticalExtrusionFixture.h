#pragma once
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepLib.hxx>
#include <Geom_Ellipse.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <gp_Circ.hxx>
#include <gp_Cylinder.hxx>
#include <TopoDS.hxx>
#include <numbers>

namespace EllipticalFixture {
inline TopoDS_Face profile(double z=0,bool hole=false) {
    const Handle(Geom_Ellipse) ellipse=new Geom_Ellipse(gp_Elips(gp_Ax2(gp_Pnt(0,0,z),gp_Dir(0,0,1)),50,30));
    const auto edge=BRepBuilderAPI_MakeEdge(ellipse).Edge();
    BRepBuilderAPI_MakeFace face(BRepBuilderAPI_MakeWire(edge).Wire());
    if(hole) {
        auto inner=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(0,0,z),gp_Dir(0,0,1)),10)).Edge()).Wire();
        inner.Reverse();face.Add(inner);
    }
    return face.Face();
}
inline TopoDS_Shape prism(double height=20,bool hole=false) {
    return BRepPrimAPI_MakePrism(profile(0,hole),gp_Vec(0,0,height)).Shape();
}
// Independent B-Rep construction: explicit exact surface, independent cap wires,
// bounded face trims and a closed sewn shell. No extrusion command or metadata.
inline TopoDS_Shape assembled() {
    const Handle(Geom_Ellipse) basis=new Geom_Ellipse(gp_Elips(gp_Ax2(gp_Pnt(0,0,-7),gp_Dir(0,0,1)),50,30));
    const Handle(Geom_SurfaceOfLinearExtrusion) wall=new Geom_SurfaceOfLinearExtrusion(basis,gp_Dir(0,0,1));
    const auto side=BRepBuilderAPI_MakeFace(wall,0,2*std::numbers::pi,7,27,1.e-7).Face();
    auto bottom=profile();bottom.Reverse();
    BRepBuilderAPI_Sewing sewing(1.e-7);sewing.Add(bottom);sewing.Add(profile(20));sewing.Add(side);sewing.Perform();
    auto solid=BRepBuilderAPI_MakeSolid(TopoDS::Shell(sewing.SewedShape())).Solid();
    if(!BRepLib::OrientClosedSolid(solid)) throw Standard_Failure("Synthetic elliptical shell orientation failed.");
    return solid;
}
}
