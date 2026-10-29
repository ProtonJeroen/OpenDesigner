#include "SketchProfileExtrusion.h"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <numbers>
#include <iostream>
#include <stdexcept>
using namespace Sketch;
namespace EX=SketchProfileExtrusion;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
Entity e(QString id,Geometry g){Entity x;x.id=id;x.workplaneId="wp";x.geometry=g;return x;}
std::vector<Entity> box(double x=0,double y=0,double w=100,double h=50){return {e("a",Segment{{x,y},{x+w,y}}),e("b",Segment{{x+w,y},{x+w,y+h}}),e("c",Segment{{x+w,y+h},{x,y+h}}),e("d",Segment{{x,y+h},{x,y}})};}
auto faces(const std::vector<Entity>& input,const gp_Ax3& frame=gp_Ax3()){return SketchProfileBuilder::build(SketchProfileValidation::validate(input,"wp"),frame);}
double volume(const TopoDS_Shape& s){GProp_GProps p;BRepGProp::VolumeProperties(s,p);return p.Mass();}
void valid(const EX::Result& r,double v,size_t count=1){if(!r.success){for(const auto& d:r.diagnostics)std::cerr<<d.message.toStdString()<<'\n';}check(r.success && r.solidCount==count,"extrusion failed");check(std::abs(r.volume-v)<std::max(1.e-7,v*1.e-9),"wrong volume");check(BRepCheck_Analyzer(r.shape).IsValid(),"invalid B-Rep");for(TopExp_Explorer it(r.shape,TopAbs_SHELL);it.More();it.Next())check(BRep_Tool::IsClosed(it.Current()),"open shell");}
void failed(const EX::Result& r){check(!r.success && r.shape.IsNull() && r.solidCount==0 && !r.diagnostics.empty(),"partial/invalid success");}
int main(){try{
 const double pi=std::numbers::pi;
 const auto input=box(),saved=input;const auto f=faces(input);valid(EX::extrude(f,gp_Dir(0,0,1),20),100000);valid(EX::extrude(f,gp_Dir(0,0,1),-20),100000);check(input==saved,"source changed");
 valid(EX::extrude(faces({e("circle",Circle{{0,0},10})}),gp_Dir(0,0,1),20),2000*pi);
 auto hole=box();hole.push_back(e("hole",Circle{{50,25},10}));valid(EX::extrude(faces(hole),gp_Dir(0,0,1),20),100000-2000*pi);
 valid(EX::extrude(faces({e("outer",Circle{{0,0},10}),e("inner",Circle{{0,0},5})}),gp_Dir(0,0,1),20),1500*pi);
 valid(EX::extrude(faces({e("a",Arc{{0,0},10,0,pi}),e("b",Segment{{-10,0},{10,0}})}),gp_Dir(0,0,1),20),1000*pi);
 hole.push_back(e("island",Circle{{50,25},2}));hole.push_back(e("separate",Circle{{150,25},5}));valid(EX::extrude(faces(hole),gp_Dir(0,0,1),20),100000-2000*pi+80*pi+500*pi,3);
 const gp_Ax3 rotated(gp_Pnt(10,-5,70),gp_Dir(1,2,3),gp_Dir(2,-1,0));valid(EX::extrude(faces(input,rotated),rotated.Direction(),20),100000);
 for(double d:{0.,1.e-9,1.5e-7,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN(),1.e10})failed(EX::extrude(f,gp_Dir(0,0,1),d));
 failed(EX::extrude({},gp_Dir(0,0,1),20));failed(EX::extrude(f,gp_Dir(1,0,0),20));
 auto open=box();open.pop_back();failed(EX::extrude(faces(open),gp_Dir(0,0,1),20));
 const auto target=BRepPrimAPI_MakeBox(100,50,10).Shape();const double before=volume(target);
 const gp_Ax3 top(gp_Pnt(0,0,10),gp_Dir(0,0,1));const auto tool=EX::extrude(faces(box(10,10,20,20),top),top.Direction(),20);
 valid(EX::addMaterial(tool,target,gp_Trsf()),58000);check(volume(target)==before,"target changed during fusion");
 valid(EX::addMaterial(EX::extrude(faces({e("boss",Circle{{50,25},5})},top),top.Direction(),20),target,gp_Trsf()),50000+500*pi);
 gp_Trsf placement;placement.SetTranslation(gp_Vec(30,40,0));const gp_Ax3 moved(gp_Pnt(30,40,10),gp_Dir(0,0,1));
 valid(EX::addMaterial(EX::extrude(faces(box(10,10,20,20),moved),moved.Direction(),20),target,placement),58000);
 gp_Trsf tilt;tilt.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,1,0)),.6);tilt.SetTranslationPart(gp_Vec(15,20,30));const auto tilted=top.Transformed(tilt);
 valid(EX::addMaterial(EX::extrude(faces(box(10,10,20,20),tilted),tilted.Direction(),20),target,tilt),58000);
 const auto first=EX::addMaterial(tool,target,gp_Trsf());valid(EX::addMaterial(EX::extrude(faces(box(40,10,10,10),top),top.Direction(),20),first.shape,gp_Trsf()),60000);
 failed(EX::addMaterial(EX::extrude(faces(box(200,200,10,10),top),top.Direction(),20),target,gp_Trsf()));
 failed(EX::addMaterial(EX::extrude(faces(box(10,10,10,10)),gp_Dir(0,0,1),5),target,gp_Trsf()));failed(EX::addMaterial({},target,gp_Trsf()));check(volume(target)==before,"failed fusion changed target");
 std::cout<<"Profile extrusion geometry passed\n";return 0;
}catch(const std::exception& x){std::cerr<<x.what()<<'\n';return 1;}}
