#include "FilletGeometry.h"
#include "BRepReconstruction.h"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <iostream>
#include <BRepTools.hxx>
#include <sstream>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <cmath>
#include <limits>
#include <stdexcept>
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
double volume(const TopoDS_Shape& shape) {GProp_GProps p;BRepGProp::VolumeProperties(shape,p);return p.Mass();}
int main() {try {
    auto source=BRepPrimAPI_MakeBox(20,20,20).Shape();
    auto edge=TopoDS::Edge(TopExp_Explorer(source,TopAbs_EDGE).Current());QString error;
    const double before=volume(source);
    std::ostringstream originalBytes;BRepTools::Write(source,originalBytes);
    auto result=FilletGeometry::apply(source,edge,2,error);
    double measured=0;
    check(!result.IsNull(),error.toUtf8().constData());
    check(BRepReconstruction::validSolid(result,measured),"invalid result topology");
    check(std::abs(measured-(8000.0-(1.0-std::acos(-1.0)/4.0)*4.0*20.0))<1.e-6,"incorrect material volume");
    for(double value:{0.0,-1.0,100.0,std::numeric_limits<double>::infinity()}) {
        check(FilletGeometry::apply(source,edge,value,error).IsNull()&&!error.isEmpty(),"invalid size accepted");
    }
    auto foreign= BRepBuilderAPI_MakeEdge(gp_Pnt(),gp_Pnt(1,0,0)).Edge();
    for(const auto& invalid:{TopoDS_Edge(),foreign})
        check(FilletGeometry::apply(source,invalid,2,error).IsNull()&&!error.isEmpty(),"invalid edge accepted");
    check(FilletGeometry::apply({},edge,2,error).IsNull(),"null source accepted");
    std::ostringstream finalBytes;BRepTools::Write(source,finalBytes);
    check(originalBytes.str()==finalBytes.str(),"source topology/geometry modified");
    check(std::abs(volume(source)-before)<1.e-10,"source modified");
    check(BRepReconstruction::validSolid(source,measured),"source invalidated");
    auto notch=BRepAlgoAPI_Cut(source,BRepPrimAPI_MakeBox(gp_Pnt(10,10,0),10,10,20).Shape()).Shape();
    TopoDS_Edge concave;
    for(TopExp_Explorer it(notch,TopAbs_EDGE);it.More();it.Next()) {
        BRepAdaptor_Curve c(TopoDS::Edge(it.Current()));auto p=c.Value((c.FirstParameter()+c.LastParameter())/2);
        if(p.Distance(gp_Pnt(10,10,10))<1.e-8) concave=TopoDS::Edge(it.Current());
    }
    check(!concave.IsNull(),"concave fixture missing");
    auto concaveResult=FilletGeometry::apply(notch,concave,2,error);
    check(!concaveResult.IsNull(),error.toUtf8().constData());
    check(BRepReconstruction::validSolid(concaveResult,measured) && measured>volume(notch),"concave blend did not add valid material");
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
