#include "FilletGeometry.h"
#include "BRepReconstruction.h"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include "EdgeBlendFeatures.h"
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <iostream>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <sstream>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <cmath>
#include <limits>
#include <stdexcept>
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
double volume(const TopoDS_Shape& shape) {GProp_GProps p;BRepGProp::VolumeProperties(shape,p);return p.Mass();}
int main(int argc,char**) {try {
    QString error;double measured=0;
    if(argc==1) {
    auto source=BRepPrimAPI_MakeBox(20,20,20).Shape();
    auto edge=TopoDS::Edge(TopExp_Explorer(source,TopAbs_EDGE).Current());
    const double before=volume(source);
    std::ostringstream originalBytes;BRepTools::Write(source,originalBytes);
    auto result=FilletGeometry::apply(source,edge,2,error);
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
    }
    // Deterministic circular supports: both convex rims, a hole, and a boss foot.
    auto cylinder=BRepPrimAPI_MakeCylinder(10,20).Shape();
    auto hole=BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(gp_Pnt(-20,-20,0),40,40,20).Shape(),cylinder).Shape();
    auto boss=BRepAlgoAPI_Fuse(BRepPrimAPI_MakeBox(gp_Pnt(-20,-20,-10),40,40,10).Shape(),cylinder).Shape();
    for(int fixture=0;fixture<5;++fixture) {
        auto stock=fixture<2?cylinder:fixture==3?boss:hole;
        TopoDS_Edge rim;
        for(TopExp_Explorer it(stock,TopAbs_EDGE);it.More();it.Next()) {
            BRepAdaptor_Curve c(TopoDS::Edge(it.Current()));
            if(c.GetType()==GeomAbs_Circle&&std::abs(c.Circle().Location().Z()-(fixture==0||fixture==4?20:0))<1.e-6) rim=TopoDS::Edge(it.Current());
        }
        check(!rim.IsNull(),"missing circular fixture edge");
        std::ostringstream bytes;BRepTools::Write(stock,bytes);
        using namespace EdgeBlendFeatures;
        State recipe;TopoDS_Shape rounded;
        check(create(stock,{},rim,Kind::Fillet,2.345,recipe,rounded,error),error.toUtf8().constData());
        check(BRepReconstruction::validSolid(rounded,measured),"circular solid invalid");
        const double r=2.345,pi=std::acos(-1.);
        const double area=r*r*(1-pi/4),moment=r*r*r*(5./6-pi/4);
        const double delta=2*pi*(10*area+(fixture<2?-moment:moment));
        check(std::abs(volume(rounded)-(volume(stock)+(fixture==3?delta:-delta)))<1.e-6,"incorrect analytic circular material volume");
        check(selected(recipe,recipe.features[0].faces.front())==0,"circular recognition missing");
        TopoDS_Face support;
        for(TopExp_Explorer it(rounded,TopAbs_FACE);it.More();it.Next()) if(BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType()==GeomAbs_Plane) {support=TopoDS::Face(it.Current());break;}
        State refused;TopoDS_Shape refusal;
        for(bool keep:{false,true}) {
            // The top through-hole cap cannot retain its round: replay has
            // no unique support edge. Keep that geometric refusal explicit.
            if(BRepAdaptor_Surface(support).Plane().Axis().Direction().IsParallel(gp_Dir(0,0,1),1.e-8)
                &&!(fixture==4&&keep)) {
                if(!move(rounded,recipe,support,1,keep,refused,refusal,error)||!BRepReconstruction::validSolid(refusal,measured))
                    throw std::runtime_error("circular cap fixture "+std::to_string(fixture)+" keep "+std::to_string(keep)+": "+error.toStdString());
                check(volume(refusal)>volume(rounded),"cap offset did not add material");
                if(keep) check(refused.features.size()==1&&std::abs(refused.features[0].size-r)<1.e-10,"cap Keep changed radius");
            } else check(!move(rounded,recipe,support,1,keep,refused,refusal,error)&&refusal.IsNull()&&!error.isEmpty(),"uncertified circular support move accepted");
            check(!rotate(rounded,recipe,support,gp_Ax1(gp_Pnt(),gp_Dir(1,0,0)),5,keep,refused,refusal,error)&&refusal.IsNull(),"unsupported circular Keep rotate accepted");
        }
        State imported;check(!resolve(rounded,imported,error),"unproven imported torus recognized");
        State loaded=decode(encode(recipe));check(resolve(rounded,loaded,error),error.toUtf8().constData());
        auto tampered=loaded;tampered.certified.Nullify();
        for(auto& c:tampered.features[0].cylinders) if(c) c->SetRadius(c->Radius()+1);
        check(!resolve(rounded,tampered,error),"tampered circular carrier trusted");
        if(fixture==0) {
            TopoDS_Edge bottom;
            for(TopExp_Explorer it(rounded,TopAbs_EDGE);it.More();it.Next()) {
                BRepAdaptor_Curve c(TopoDS::Edge(it.Current()));
                if(c.GetType()==GeomAbs_Circle&&std::abs(c.Circle().Location().Z())<1.e-6) bottom=TopoDS::Edge(it.Current());
            }
            State pair,changedPair;TopoDS_Shape both,changedShape;
            check(create(rounded,recipe,bottom,Kind::Fillet,1,pair,both,error),error.toUtf8().constData());
            check(edit(both,pair,pair.features[0].id,1.5,changedPair,changedShape,error)&&changedPair.features[1].id==pair.features[1].id
                &&changedPair.features[1].size==1,"circular edit lost unrelated rim");
            check(remove(both,pair,pair.features[0].faces,changedPair,changedShape,error)&&changedPair.features.size()==1
                &&changedPair.features[0].id==pair.features[1].id,"circular deletion lost unrelated rim");
        }
        State edited;TopoDS_Shape changed;
        check(edit(rounded,loaded,loaded.features[0].id,1.25,edited,changed,error),error.toUtf8().constData());
        check(edited.features[0].id==recipe.features[0].id,"circular identity lost");
        check(remove(rounded,recipe,recipe.features[0].faces,edited,changed,error)&&BRepReconstruction::equivalent(stock,changed),"circular sharp restoration failed");
        for(double radius:{0.,-1.,100.}) check(FilletGeometry::apply(stock,rim,radius,error).IsNull(),"invalid circular radius accepted");
        std::ostringstream unchanged;BRepTools::Write(stock,unchanged);check(bytes.str()==unchanged.str(),"circular input mutated");
    }
    for(TopExp_Explorer it(cylinder,TopAbs_EDGE);it.More();it.Next()) {
        auto e=TopoDS::Edge(it.Current());if(BRepAdaptor_Curve(e).GetType()==GeomAbs_Line)
            check(FilletGeometry::apply(cylinder,e,1,error).IsNull(),"cylinder seam accepted");
    }
    auto sphere=BRepPrimAPI_MakeSphere(10).Shape();bool degenerate=false;
    for(TopExp_Explorer it(sphere,TopAbs_EDGE);it.More();it.Next()) {
        auto e=TopoDS::Edge(it.Current());if(BRep_Tool::Degenerated(e)) {
            degenerate=true;check(FilletGeometry::apply(sphere,e,1,error).IsNull(),"degenerate edge accepted");
        }
    }
    check(degenerate,"sphere fixture has no degenerate edge");
    // Fuse two exact half-cylinders without unifying their analytic carriers.
    // ShapeUpgrade_ShapeDivideClosed introduces 1e-5 edge tolerances in this OCCT
    // version, outside the production policy, so it is unsuitable as a fixture.
    auto half=BRepPrimAPI_MakeCylinder(10,20,std::acos(-1.)).Shape();
    for(TopExp_Explorer it(half,TopAbs_EDGE);it.More();it.Next()) {
        auto e=TopoDS::Edge(it.Current());if(BRepAdaptor_Curve(e).GetType()==GeomAbs_Circle) {
            check(FilletGeometry::apply(half,e,1,error).IsNull(),"incomplete circular contour accepted");break;
        }
    }
    auto otherHalf=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(),gp_Dir(0,0,1),gp_Dir(-1,0,0)),10,20,std::acos(-1.)).Shape();
    auto split=BRepAlgoAPI_Fuse(half,otherHalf).Shape();bool segmented=false;
    for(TopExp_Explorer it(split,TopAbs_EDGE);it.More();it.Next()) {
        auto e=TopoDS::Edge(it.Current());BRepAdaptor_Curve c(e);
        if(c.GetType()!=GeomAbs_Circle||c.LastParameter()-c.FirstParameter()>6) continue;
        segmented=true;std::ostringstream originalSplit;BRepTools::Write(split,originalSplit);
        auto rounded=FilletGeometry::apply(split,e,1,error);
        std::ostringstream unchangedSplit;BRepTools::Write(split,unchangedSplit);
        check(originalSplit.str()==unchangedSplit.str(),"segmented source mutated");
        check(!rounded.IsNull(),error.toUtf8().constData());
        check(BRepReconstruction::validSolid(rounded,measured),"segmented contour result invalid");
        const double expected=volume(split)-2*std::acos(-1.)*(10*(1-std::acos(-1.)/4)-(5./6-std::acos(-1.)/4));
        check(std::abs(measured-expected)<1.e-6,"segmented contour was only partially blended");break;
    }
    check(segmented,"split-circle fixture has no segments");
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
