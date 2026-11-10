#include "ChamferGeometry.h"
#include "BRepReconstruction.h"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <iostream>
#include <cmath>
#include <limits>
#include <stdexcept>
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
double volume(const TopoDS_Shape& shape) {GProp_GProps p;BRepGProp::VolumeProperties(shape,p);return p.Mass();}
int main() {try {
    auto source=BRepPrimAPI_MakeBox(20,20,20).Shape();
    auto edge=TopoDS::Edge(TopExp_Explorer(source,TopAbs_EDGE).Current());QString error;
    const double before=volume(source);
    auto result=ChamferGeometry::apply(source,edge,2,error);
    double measured=0;
    check(!result.IsNull(),error.toUtf8().constData());
    check(BRepReconstruction::validSolid(result,measured),"invalid result topology");
    check(std::abs(measured-(7960.0))<1.e-6,"incorrect material volume");
    for(double value:{0.0,-1.0,100.0,std::numeric_limits<double>::infinity()}) {
        check(ChamferGeometry::apply(source,edge,value,error).IsNull()&&!error.isEmpty(),"invalid size accepted");
    }
    auto foreign= BRepBuilderAPI_MakeEdge(gp_Pnt(),gp_Pnt(1,0,0)).Edge();
    for(const auto& invalid:{TopoDS_Edge(),foreign})
        check(ChamferGeometry::apply(source,invalid,2,error).IsNull()&&!error.isEmpty(),"invalid edge accepted");
    check(ChamferGeometry::apply({},edge,2,error).IsNull(),"null source accepted");
    check(std::abs(volume(source)-before)<1.e-10,"source modified");
    check(BRepReconstruction::validSolid(source,measured),"source invalidated");
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
