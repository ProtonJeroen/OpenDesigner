#pragma once
#include "BRepTopology.h"
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
namespace SplineFixture {
// x=60t; upper/lower y=20 +/- (12t+6t²-18t³).
// Integral of the gap: 60*2*(6+2-4.5) = 420 mm².
inline constexpr double area=420;
inline Handle(Geom_BSplineCurve) curve(bool upper) {
    const double sign=upper?1:-1;
    TColgp_Array1OfPnt p(1,4);p(1)=gp_Pnt(0,20,0);p(2)=gp_Pnt(20,20+sign*4,0);
    p(3)=gp_Pnt(40,20+sign*10,0);p(4)=gp_Pnt(60,20,0);
    TColStd_Array1OfReal k(1,2);k(1)=0;k(2)=1;
    TColStd_Array1OfInteger m(1,2);m(1)=m(2)=4;
    return new Geom_BSplineCurve(p,k,m,3);
}
inline TopoDS_Face profile() {
    BRepBuilderAPI_MakeWire wire;wire.Add(BRepBuilderAPI_MakeEdge(curve(false)).Edge());
    auto edge=BRepBuilderAPI_MakeEdge(curve(true)).Edge();edge.Reverse();wire.Add(edge);
    return BRepBuilderAPI_MakeFace(wire.Wire()).Face();
}
inline TopoDS_Shape solid(double height=20) {return BRepPrimAPI_MakePrism(profile(),gp_Vec(0,0,height),true,true).Shape();}
inline TopoDS_Face cap(const TopoDS_Shape& shape,double z) {
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());BRepAdaptor_Surface surface(face);
        if(surface.GetType()==GeomAbs_Plane&&surface.Plane().Axis().Direction().IsParallel(gp_Dir(0,0,1),1.e-8)
           &&surface.Plane().Distance(gp_Pnt(0,20,z))<1.e-7) return face;
    }
    return {};
}
}
