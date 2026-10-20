#pragma once
// User-supplied exact B-Reps from the Fedora Rotate +30 / Move +49 report.
// The expected material is computed independently of the reconstruction engine.
#include "BRepReconstruction.h"
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepGProp.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <cmath>
#include <stdexcept>
#include <string>
namespace RotateMoveFixture {
inline void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
inline TopoDS_Shape load(const char* name) {
    TopoDS_Shape shape;BRep_Builder builder;
    const auto path=std::string(OPENDESIGNER_FIXTURE_DIR "/rotate30-move49/")+name;
    check(BRepTools::Read(shape,path.c_str(),builder),"Cannot load exact Fedora B-Rep fixture.");return shape;
}
inline TopoDS_Face face(const TopoDS_Shape& shape,int index) {
    TopTools_IndexedMapOfShape map;TopExp::MapShapes(shape,TopAbs_FACE,map);
    check(index>0&&index<=map.Extent(),"Fixture target face index is missing.");return TopoDS::Face(map(index));
}
inline double top(double y,double distance) {return 60+(y-80)/std::sqrt(3.)+2*distance/std::sqrt(3.);}
inline double volume(double distance) {return 344000+8000*(top(0,distance)+top(80,distance))/2;}
inline TopoDS_Shape expected(double distance) {
    check(top(0,distance)>0,"Prism oracle requires a positive front height.");
    BRepBuilderAPI_MakePolygon polygon;
    for(const auto& p:{gp_Pnt(125,0,0),gp_Pnt(125,80,0),gp_Pnt(125,80,top(80,distance)),gp_Pnt(125,0,top(0,distance))}) polygon.Add(p);
    polygon.Close();
    const auto upper=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(polygon.Wire()).Face(),gp_Vec(100,0,0)).Shape();
    BRepAlgoAPI_Fuse unite(BRepPrimAPI_MakeBox(gp_Pnt(0,16,0),125,64,43).Shape(),upper);
    check(unite.IsDone(),"Independent fixture oracle failed.");return unite.Shape();
}
inline void verify(const TopoDS_Shape& shape,double distance=49) {
    double actual=0;check(BRepReconstruction::validSolid(shape,actual),"Rotate +30 / Move +49 result is not one valid closed solid.");
    check(std::abs(actual-volume(distance))<1.e-7*volume(distance),"Rotate +30 / Move +49 has incorrect material volume.");
    check(BRepReconstruction::equivalent(shape,expected(distance)),"Rotate +30 / Move +49 differs from the independently bounded material.");
    BRepClass3d_SolidClassifier classifier(shape);
    const auto probe=[&](const gp_Pnt& p) {
        const bool low=p.X()>0&&p.X()<125&&p.Y()>16&&p.Y()<80&&p.Z()>0&&p.Z()<43;
        const bool high=p.X()>125&&p.X()<225&&p.Y()>0&&p.Y()<80&&p.Z()>0&&p.Z()<top(p.Y(),distance);
        classifier.Perform(p,1.e-7);
        check(classifier.State()==((low||high)?TopAbs_IN:TopAbs_OUT),"Material sample detected extra triangular material or incorrect adjacent trimming.");
    };
    for(double x=-2.31;x<230;x+=13.7) for(double y=-51.17;y<85;y+=7.1)
        for(double z=-2.19;z<top(80,distance)+3;z+=6.7) probe(gp_Pnt(x,y,z));
    // Entire former triangular wing, including samples close to the real front.
    for(double x=126.3;x<225;x+=11.7) for(double y=-47.4;y<0;y+=2.3)
        for(double z=43.01;z<top(0,distance);z+=2.7) probe(gp_Pnt(x,y,z));
    for(double y:{-0.001,0.001}) for(double z:{42.9,50.,top(0,distance)-0.01}) probe(gp_Pnt(175,y,z));
    for(double y=0.27;y<80;y+=3.1) for(double delta:{-0.001,0.001}) probe(gp_Pnt(175,y,top(y,distance)+delta));
    for(double z:{42.999,43.001}) for(double y:{15.999,16.001,79.999,80.001}) probe(gp_Pnt(62,y,z));
}
}
