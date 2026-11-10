#include "FilletGeometry.h"
#include "EdgeBlendFeatures.h"
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepTools.hxx>
#include <sstream>
#include <QJsonDocument>
#include <limits>
#include "ChamferGeometry.h"
#include "MoveFaceGeometry.h"
#include "DeleteFaceGeometry.h"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <iostream>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRep_Tool.hxx>
#include <stdexcept>
namespace {
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
TopoDS_Edge edgeAt(const TopoDS_Shape& s,const gp_Pnt& p) {
 for(TopExp_Explorer it(s,TopAbs_EDGE);it.More();it.Next()) {BRepAdaptor_Curve c(TopoDS::Edge(it.Current()));if(c.Value((c.FirstParameter()+c.LastParameter())/2).Distance(p)<1.e-6)return TopoDS::Edge(it.Current());}return {};
}
TopoDS_Face planeAt(const TopoDS_Shape& s,const gp_Pnt& p,const gp_Dir& n) {
 for(TopExp_Explorer it(s,TopAbs_FACE);it.More();it.Next()){BRepAdaptor_Surface a(TopoDS::Face(it.Current()));if(a.GetType()==GeomAbs_Plane&&a.Plane().Axis().Direction().IsParallel(n,1.e-8)&&a.Plane().Distance(p)<1.e-6)return TopoDS::Face(it.Current());}return {};
}
}
int main(int argc,char** argv){try{
 auto box=BRepPrimAPI_MakeBox(100,80,60).Shape();QString error;
 auto f=FilletGeometry::apply(box,edgeAt(box,gp_Pnt(0,0,30)),5,error);check(!f.IsNull(),"fixture fillet");
 auto both=ChamferGeometry::apply(f,edgeAt(f,gp_Pnt(100,80,30)),3,error);check(!both.IsNull(),"fixture chamfer");
 if(argc>1&&std::string(argv[1])=="--baseline") {
 for(const auto& s:{box,f,both}) {
  std::cout<<"tolerances (face/edge/vertex): ";
  for(auto type:{TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX}) {double t=0;for(TopExp_Explorer it(s,type);it.More();it.Next())t=std::max(t,type==TopAbs_FACE?BRep_Tool::Tolerance(TopoDS::Face(it.Current())):type==TopAbs_EDGE?BRep_Tool::Tolerance(TopoDS::Edge(it.Current())):BRep_Tool::Tolerance(TopoDS::Vertex(it.Current())));std::cout<<t<<" ";}std::cout<<'\n';
 }
 BRepBuilderAPI_Copy isolated(f,true,false);auto e=edgeAt(isolated.Shape(),gp_Pnt(100,80,30));BRepFilletAPI_MakeChamfer symmetric(isolated.Shape());symmetric.Add(3,e);symmetric.Build();
 double raw=0;for(TopExp_Explorer it(symmetric.Shape(),TopAbs_VERTEX);it.More();it.Next())raw=std::max(raw,BRep_Tool::Tolerance(TopoDS::Vertex(it.Current())));std::cout<<"raw symmetric chamfer vertex tolerance: "<<raw<<'\n';
 for(const auto& s:{f,both}) {
  auto p=planeAt(s,gp_Pnt(0,0,60),gp_Dir(0,0,1));auto moved=MoveFaceGeometry::move(s,p,20,error);
  std::cout<<"ordinary face move: "<<!moved.IsNull()<<" "<<error.toStdString()<<'\n';
  for(TopExp_Explorer it(s,TopAbs_FACE);it.More();it.Next()){
   auto face=TopoDS::Face(it.Current());BRepAdaptor_Surface a(face);
   if(a.GetType()!=GeomAbs_Cylinder&&!(a.GetType()==GeomAbs_Plane&&std::abs(a.Plane().Axis().Direction().X())>0.1&&std::abs(a.Plane().Axis().Direction().Y())>0.1))continue;
   BRepReconstruction::Result result;bool removed=DeleteFaceGeometry::remove(s,{face},result,error);
   std::cout<<"transition delete: "<<removed<<" "<<error.toStdString()<<'\n';
  }
 }
 return 0;
 }
 using namespace EdgeBlendFeatures;
 auto volume=[](const TopoDS_Shape& s){GProp_GProps p;BRepGProp::VolumeProperties(s,p);return p.Mass();};
 auto valid=[&](const TopoDS_Shape& s){double v=0;check(BRepReconstruction::validSolid(s,v),"invalid reconstructed solid");};
 State first,second;TopoDS_Shape one,two;
 check(create(box,{},edgeAt(box,gp_Pnt(0,0,30)),Kind::Fillet,5,first,one,error),error.toUtf8().constData());
 check(create(one,first,edgeAt(one,gp_Pnt(100,80,30)),Kind::Chamfer,3,second,two,error),error.toUtf8().constData());
 check(second.features.size()==2,"missing recipes");
 auto original=second;std::ostringstream source;BRepTools::Write(two,source);
 const auto fid=second.features[0].id,cid=second.features[1].id;
 check(selected(second,second.features[0].faces.front())==0&&selected(second,second.features[1].faces.front())==1,"feature selection not bound to current topology");
 State imported;check(resolve(both,imported,error)&&imported.features.size()==2,"geometric recognition failed");
 check(std::abs(imported.features[0].size-3)<1.e-6||std::abs(imported.features[0].size-5)<1.e-6,"import size wrong");
 for(const auto& shape:{one,ChamferGeometry::apply(box,edgeAt(box,gp_Pnt(100,80,30)),3,error)}) {
  State probe;check(resolve(shape,probe,error)&&probe.features.size()==1,"single feature recognition failed");
 }
 State changed;TopoDS_Shape result;
 check(edit(two,second,fid,7,changed,result,error),error.toUtf8().constData());valid(result);
 check(std::abs(changed.features[0].size-7)<1.e-10&&changed.features[1].id==cid,"radius edit lost independent chamfer");
 check(edit(two,second,cid,6,changed,result,error),error.toUtf8().constData());valid(result);
 check(std::abs(changed.features[1].size-6)<1.e-10&&changed.features[0].id==fid,"chamfer edit lost fillet");
 auto top=planeAt(two,gp_Pnt(0,0,60),gp_Dir(0,0,1));
 check(move(two,second,top,20,true,changed,result,error),error.toUtf8().constData());valid(result);
 check(std::abs(volume(result)-volume(two)*80./60.)<1.e-5,"Keep ON incorrect swept material");
 check(changed.features.size()==2&&changed.features[0].size==5&&changed.features[1].size==3,"Keep ON changed dimensions");
 check(move(two,second,top,20,false,changed,result,error),error.toUtf8().constData());valid(result);
 check(changed.features.empty()&&std::abs(volume(result)-100.*80.*80.)<1.e-5,"cap Keep OFF did not remove exactly its dependent transitions");
 auto side=planeAt(two,gp_Pnt(),gp_Dir(1,0,0));
 check(move(two,second,side,20,false,changed,result,error),error.toUtf8().constData());valid(result);
 check(changed.features.size()==1&&changed.features[0].id==cid&&changed.features[0].size==3,"Keep OFF removed unrelated transition");
 check(std::abs(volume(result)-(120.*80.*60.-0.5*9*60))<1.e-5,"Keep OFF sharp reconstruction wrong");
 check(move(two,second,side,20,true,changed,result,error),error.toUtf8().constData());valid(result);
 check(changed.features.size()==2&&changed.features[0].size==5,"Keep ON supporting face failed");
 check(remove(two,second,{second.features[0].faces.front()},changed,result,error),error.toUtf8().constData());valid(result);
 auto onlyChamfer=ChamferGeometry::apply(box,edgeAt(box,gp_Pnt(100,80,30)),3,error);
 check(BRepReconstruction::equivalent(result,onlyChamfer)&&changed.features.size()==1&&changed.features[0].id==cid,"Delete Fillet changed unrelated material");
 check(remove(two,second,{second.features[1].faces.front()},changed,result,error),error.toUtf8().constData());
 check(BRepReconstruction::equivalent(result,one)&&changed.features.size()==1,"Delete Chamfer failed sharp closure");
 gp_Ax1 capAxis(gp_Pnt(0,0,60),gp_Dir(1,0,0));
 check(rotate(two,second,top,capAxis,5,true,changed,result,error),error.toUtf8().constData());valid(result);
 GProp_GProps originalMass;BRepGProp::VolumeProperties(two,originalMass);
 double expectedVolume=volume(two)+volume(two)/60.*originalMass.CentreOfMass().Y()*std::tan(5*std::acos(-1.)/180.);
 check(std::abs(volume(result)-expectedVolume)<1.e-4&&changed.features.size()==2&&changed.features[0].size==5&&changed.features[1].size==3,"Rotate Keep ON changed dimensions or material");
 auto rotatedState=decode(encode(changed));check(resolve(result,rotatedState,error),"rotated supports did not survive recipe roundtrip");
 check(rotate(two,second,top,capAxis,5,false,changed,result,error),error.toUtf8().constData());valid(result);
 check(changed.features.empty()&&std::abs(volume(result)-(480000.+8000.*40*std::tan(5*std::acos(-1.)/180.)))<1.e-4,"Rotate Keep OFF did not restore sharp stock");
 auto stored=encode(second);auto restored=decode(stored);
 check(resolve(two,restored,error)&&restored.features[0].id==fid&&restored.features[1].id==cid,"persisted supports not certified");
 for(double invalid:{0.,-1.,1000.,std::numeric_limits<double>::quiet_NaN()})
  check(!edit(two,second,fid,invalid,changed,result,error)&&result.IsNull()&&!error.isEmpty(),"invalid dimension accepted");
 State broken=second;broken.features[0].supports[0]=gp_Pln(gp_Pnt(500,0,0),gp_Dir(1,0,0));broken.certified.Nullify();
 check(resolve(two,broken,error),"invalid metadata should safely re-recognize current rectangular geometry");
 check(broken.features[0].id!=fid,"invalid metadata retained authoritative identity");
 State none;check(!resolve(box,none,error),"ordinary stock misclassified as transition");
 check(!remove(two,second,{top},changed,result,error)&&result.IsNull(),"ordinary face misclassified as transition");
 check(!move(two,second,planeAt(box,gp_Pnt(),gp_Dir(1,0,0)),1,true,changed,result,error),"stale face accepted");
 std::ostringstream after;BRepTools::Write(two,after);check(source.str()==after.str(),"source geometry mutated");
 check(second.certified.IsSame(two)&&second.features[0].size==5,"source metadata mutated");
 std::cout<<"Certified recognition, dimension edits, sharp deletion, Keep ON/OFF and metadata checks passed.\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
