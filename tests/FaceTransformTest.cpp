#include "FaceTransformGeometry.h"
#include "MoveFaceGeometry.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <sstream>

namespace {
void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
void near(double actual,double expected,const char* message) {
    require(std::abs(actual-expected)<1.e-5*std::max(1.0,std::abs(expected)),message);
}
int count(const TopoDS_Shape& shape,TopAbs_ShapeEnum type) {
    int n=0; for(TopExp_Explorer it(shape,type);it.More();it.Next()) ++n; return n;
}
TopoDS_Face topFace(const TopoDS_Shape& shape) {
    TopoDS_Face result;
    double z=-1.e100;
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());
        if(BRepAdaptor_Surface(face).GetType()!=GeomAbs_Plane) continue;
        const double candidate=BRepAdaptor_Surface(face).Plane().Location().Z();
        if(candidate>z) {z=candidate;result=face;}
    }
    return result;
}
gp_Ax1 straightAxis(const TopoDS_Face& face) {
    for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()) {
        const BRepAdaptor_Curve curve(TopoDS::Edge(it.Current()));
        if(curve.GetType()==GeomAbs_Line) return curve.Line().Position();
    }
    throw std::runtime_error("planar face has no straight edge");
}
gp_Ax1 perpendicularStraightAxis(const TopoDS_Face& face,const gp_Ax1& first) {
    for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()) {
        const BRepAdaptor_Curve curve(TopoDS::Edge(it.Current()));
        if(curve.GetType()==GeomAbs_Line
           && std::abs(curve.Line().Direction().Dot(first.Direction()))<0.1) return curve.Line().Position();
    }
    throw std::runtime_error("planar face has no second straight direction");
}
TopoDS_Face transitionFace(const TopoDS_Shape& shape) {
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());
        if(BRepAdaptor_Surface(face).GetType()!=GeomAbs_Plane) continue;
        const auto plane=BRepAdaptor_Surface(face).Plane();
        if(std::abs(std::abs(plane.Axis().Direction().X())-1.0)>1.e-7) continue;
        GProp_GProps properties; BRepGProp::SurfaceProperties(face,properties);
        const auto center=properties.CentreOfMass();
        if(center.X()>59.9 && center.X()<60.1 && center.Z()>69.9 && center.Z()<70.1) return face;
    }
    return {};
}
gp_Ax1 yAxis(const TopoDS_Face& face) {
    for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()) {
        const BRepAdaptor_Curve curve(TopoDS::Edge(it.Current()));
        if(curve.GetType()==GeomAbs_Line && std::abs(curve.Line().Direction().Y())>0.99)
            return curve.Line().Position();
    }
    throw std::runtime_error("transition face has no straight Y edge");
}
double volume(const TopoDS_Shape& shape) { GProp_GProps p;BRepGProp::VolumeProperties(shape,p);return p.Mass(); }
std::string shapeStamp(const TopoDS_Shape& shape) { std::ostringstream out;BRepTools::Write(shape,out);return out.str(); }
void geometry() {
    const auto box=BRepPrimAPI_MakeBox(100,80,60).Shape();
    const auto face=topFace(box);
    const auto axis=straightAxis(face);
    QString error;
    const double originalVolume=volume(box);
    for(const double degrees : {12.0,-12.0,25.0,-25.0}) {
        const auto result=FaceTransformGeometry::rotate(box,face,axis,degrees,error);
        require(!result.IsNull(),error.toStdString().c_str());
        require(BRepCheck_Analyzer(result).IsValid(),"rotated Box is invalid");
        require(count(result,TopAbs_FACE)==count(box,TopAbs_FACE)
                &&count(result,TopAbs_EDGE)==count(box,TopAbs_EDGE)
                &&count(result,TopAbs_VERTEX)==count(box,TopAbs_VERTEX),
                "Rotate Face added or removed Box topology");
        gp_Pln expected=BRepAdaptor_Surface(face).Plane();
        gp_Trsf transform; transform.SetRotation(axis,degrees*std::acos(-1.0)/180.0);
        expected.Transform(transform);
        bool targetPlaneFound=false;
        for(TopExp_Explorer f(result,TopAbs_FACE);f.More();f.Next()) {
            const auto candidate=TopoDS::Face(f.Current());
            if(BRepAdaptor_Surface(candidate).GetType()!=GeomAbs_Plane) continue;
            const auto plane=BRepAdaptor_Surface(candidate).Plane();
            if(plane.Axis().Direction().IsParallel(expected.Axis().Direction(),1.e-5)
               &&plane.Distance(expected.Location())<1.e-4) targetPlaneFound=true;
        }
        require(targetPlaneFound,"Rotate Face did not place the selected support at the requested angle");
        require(std::abs(volume(result)-originalVolume)>1.0,"face rotation did not change geometry");
    }
    const auto otherAxis=perpendicularStraightAxis(face,axis);
    const auto otherAxisResult=FaceTransformGeometry::rotate(box,face,otherAxis,17,error);
    require(!otherAxisResult.IsNull(),error.toStdString().c_str());
    const auto priorEdit=FaceTransformGeometry::rotate(box,face,axis,8,error);
    require(!priorEdit.IsNull(),error.toStdString().c_str());
    TopoDS_Face tiltedFace;
    for(TopExp_Explorer f(priorEdit,TopAbs_FACE);f.More();f.Next()) {
        const auto candidate=TopoDS::Face(f.Current());
        if(BRepAdaptor_Surface(candidate).GetType()==GeomAbs_Plane
           && std::abs(BRepAdaptor_Surface(candidate).Plane().Axis().Direction().Z())>0.5
           && std::abs(BRepAdaptor_Surface(candidate).Plane().Axis().Direction().Z())<0.999) {tiltedFace=candidate;break;}
    }
    require(!tiltedFace.IsNull(),"tilted GeneralBRep face was not retained");
    const auto generalResult=FaceTransformGeometry::rotate(priorEdit,tiltedFace,straightAxis(tiltedFace),-5,error);
    require(!generalResult.IsNull(),error.toStdString().c_str());
    require(FaceTransformGeometry::rotate(box,face,gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1)),10,error).IsNull(),
            "axis outside the selected face was accepted");

    const auto farReference=gp_Pln(gp_Pnt(0,0,75),gp_Dir(0,0,1));
    const auto coplanar=FaceTransformGeometry::align(box,face,farReference,true,error);
    require(!coplanar.IsNull(),error.toStdString().c_str());
    require(BRepCheck_Analyzer(coplanar).IsValid(),"coplanar result is invalid");
    require(std::abs(BRepAdaptor_Surface(topFace(coplanar)).Plane().Location().Z()-75.0)<1.e-5,"target face did not become coplanar");
    require(count(coplanar,TopAbs_FACE)==count(box,TopAbs_FACE),"Align Face added Box faces");
    const auto parallel=FaceTransformGeometry::align(box,face,farReference,false,error);
    require(!parallel.IsNull(),error.toStdString().c_str());
    require(std::abs(volume(parallel)-originalVolume)<1.e-5,"Parallel changed the target offset");
    const auto samePartReference=FaceTransformGeometry::align(box,face,
        gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),false,error);
    require(!samePartReference.IsNull(),error.toStdString().c_str());
    require(std::abs(volume(samePartReference)-originalVolume)<1.e-5,"same-Part Parallel changed geometry");

    const auto otherPlane=gp_Pln(gp_Pnt(100,0,0),gp_Dir(0.5,0,0.866025403784));
    const auto tilted=FaceTransformGeometry::align(box,face,otherPlane,false,error);
    require(!tilted.IsNull(),error.toStdString().c_str());
    require(BRepCheck_Analyzer(tilted).IsValid(),"parallel-aligned target is invalid");

    // Boolean Unite staircase: two overlapping boxes with different heights.
    // The target is the narrow vertical riser left at x=60 (z=50..90).
    const auto low=BRepPrimAPI_MakeBox(100,80,50).Shape();
    const auto high=BRepPrimAPI_MakeBox(gp_Pnt(60,0,0),100,80,90).Shape();
    BRepAlgoAPI_Fuse fuse(low,high); fuse.Build();
    require(fuse.IsDone(),"Boolean Unite setup failed");
    const auto boolean=fuse.Shape();
    const auto riser=transitionFace(boolean);
    require(!riser.IsNull(),"Boolean Unite transition face was not found");
    const double uniteVolume=volume(boolean);
    const auto uniteBefore=shapeStamp(boolean);
    gp_Pnt riserCenter; gp_Dir riserNormal;
    require(MoveFaceGeometry::reference(boolean,riser,riserCenter,riserNormal,error),error.toStdString().c_str());
    for(const double distance : {5.0,-5.0}) {
        const auto moved=MoveFaceGeometry::move(boolean,riser,distance,error);
        require(moved.IsNull(),"Boolean Unite riser with changed edge/vertex topology was incorrectly reconstructed");
        require(std::abs(volume(boolean)-uniteVolume)<1.e-8,"refused Boolean Move Face modified its source");
        require(shapeStamp(boolean)==uniteBefore,"refused Boolean Move Face changed source B-Rep data");
    }
    for(const double angle : {8.0,-8.0}) {
        const auto rotated=FaceTransformGeometry::rotate(boolean,riser,yAxis(riser),angle,error);
        require(rotated.IsNull(),"Boolean Unite riser with changed topology was incorrectly rotated");
        require(std::abs(volume(boolean)-uniteVolume)<1.e-8,"refused Boolean Rotate Face modified its source");
        require(shapeStamp(boolean)==uniteBefore,"refused Boolean Rotate Face changed source B-Rep data");
    }
    const auto alignedRiser=FaceTransformGeometry::align(boolean,riser,
        gp_Pln(gp_Pnt(55,0,0),gp_Dir(1,0,0)),true,error);
    require(alignedRiser.IsNull(),"Align Face accepted a transition requiring unsupported topology changes");
    require(std::abs(volume(boolean)-uniteVolume)<1.e-8,"refused Boolean Align Face modified its source");
    require(shapeStamp(boolean)==uniteBefore,"refused Boolean Align Face changed source B-Rep data");

    // The shared face tooling also accepts a simple planar face on a Boolean cut.
    const auto cutter=BRepPrimAPI_MakeBox(gp_Pnt(30,20,20),30,40,40).Shape();
    BRepAlgoAPI_Cut cut(low,cutter); cut.Build();
    require(cut.IsDone(),"Boolean Subtract setup failed");
    const auto cutTop=topFace(cut.Shape());
    const auto movedCut=MoveFaceGeometry::move(cut.Shape(),cutTop,3,error);
    require(!movedCut.IsNull(),error.toStdString().c_str());
    require(BRepCheck_Analyzer(movedCut).IsValid(),"moved Boolean Subtract face is invalid");
    require(count(movedCut,TopAbs_FACE)==count(cut.Shape(),TopAbs_FACE),
            "Boolean Subtract Move Face added faces");
    near(volume(movedCut)-volume(cut.Shape()),3.0*(8000-30*40),"Boolean Subtract adjacent faces were not trimmed at the moved plane");
    const auto rotatedCut=FaceTransformGeometry::rotate(cut.Shape(),cutTop,straightAxis(cutTop),5,error);
    if(!rotatedCut.IsNull()) {
        require(BRepCheck_Analyzer(rotatedCut).IsValid(),"rotated Boolean Subtract face is invalid");
        require(count(rotatedCut,TopAbs_FACE)==count(cut.Shape(),TopAbs_FACE),
                "Boolean Subtract Rotate Face added faces");
    }

    const auto b=BRepPrimAPI_MakeBox(gp_Pnt(60,15,0),60,50,60).Shape();
    BRepAlgoAPI_Fuse fuseSimple(box,b); fuseSimple.Build();
    require(fuseSimple.IsDone(),"Boolean setup failed");
    const auto simpleBoolean=fuseSimple.Shape();
    TopoDS_Shape booleanResult;
    for(TopExp_Explorer f(simpleBoolean,TopAbs_FACE);f.More();f.Next()) {
        const auto planar=TopoDS::Face(f.Current());
        if(BRepAdaptor_Surface(planar).GetType()!=GeomAbs_Plane) continue;
        const auto probe=FaceTransformGeometry::rotate(simpleBoolean,planar,straightAxis(planar),8,error);
        if(!probe.IsNull()) { booleanResult=probe; break; }
    }
    require(!booleanResult.IsNull(),"no planar face on the Boolean result supported a simple rotation");
    require(BRepCheck_Analyzer(booleanResult).IsValid(),"Boolean face rotation is invalid");

    const auto untouched=FaceTransformGeometry::rotate(box,face,axis,180,error);
    require(untouched.IsNull(),"unsupported rotation angle was accepted");
    require(std::abs(volume(box)-originalVolume)<1.e-8,"a refused operation mutated its source");
}
}
int main() {
    try { geometry();std::cout<<"Face transform geometry passed.\n";return 0; }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
