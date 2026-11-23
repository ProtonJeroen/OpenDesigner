#include "SectionPrismVerifier.h"
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeWedge.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <Geom_Plane.hxx>
#include <Geom2d_Line.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shell.hxx>
#include <algorithm>
#include <iostream>
#include <sstream>
using namespace SectionExperiment;
namespace {
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
std::string stamp(const TopoDS_Shape& s){std::ostringstream o;BRepTools::Write(s,o);return o.str();}
struct Fixture {TopoDS_Shape solid;TopoDS_Face top;};
Fixture prism(const std::vector<gp_Pnt>& points,double height=20) {
    BRepBuilderAPI_MakePolygon wire;for(auto p:points)wire.Add(p);wire.Close();
    BRepBuilderAPI_MakeFace face(gp_Pln(gp_Pnt(),gp_Dir(0,0,1)),wire.Wire(),true);
    BRepPrimAPI_MakePrism sweep(face.Face(),gp_Vec(0,0,height),true,true);return {sweep.Shape(),TopoDS::Face(sweep.LastShape())};
}
Fixture clone(const Fixture& f){BRepBuilderAPI_Copy c(f.solid,true,false);auto top=TopoDS::Face(c.ModifiedShape(f.top));top.Orientation(f.top.Orientation());return {c.Shape(),top};}
TopoDS_Shell shell(const Fixture& f){return TopoDS::Shell(TopExp_Explorer(f.solid,TopAbs_SHELL).Current());}
TopoDS_Face wall(const Fixture& f){for(TopExp_Explorer it(f.solid,TopAbs_FACE);it.More();it.Next()){const auto face=TopoDS::Face(it.Current());if(face.IsSame(f.top))continue;TopLoc_Location l;const auto p=Handle(Geom_Plane)::DownCast(BRep_Tool::Surface(face,l));if(!p.IsNull()&&std::abs(p->Axis().Direction().Z())<.5)return face;}throw std::runtime_error("wall fixture not found");}
double median(std::vector<double> x){std::sort(x.begin(),x.end());return x[x.size()/2];}
void accepted(const char* name,const Fixture& f,size_t n) {
    const auto before=stamp(f.solid);const PrismInput input(f.solid,f.top);std::vector<PrismTimes> times;
    for(int trial=0;trial<3;++trial) {
        auto e=producePrism(input);const auto result=consumePrism(input,e);
        if(e.proposalOutcome!=Outcome::Certified||result.boundary!=Outcome::Certified)std::cout<<"PRISM_FAILURE "<<name<<" proposal="<<e.reason<<" consumer="<<result.reason<<'\n';
        require(e.proposalOutcome==Outcome::Certified&&result.boundary==Outcome::Certified,"positive prism boundary refused");
        require(result.material==Outcome::Unresolved,"unproven complete material claim");
        require(result.caps==2&&result.walls==n&&result.edges==3*n&&result.vertices==2*n&&result.edgeUses==6*n,"boundary coverage counts wrong");
        e.times.verificationMs=result.verificationMs;times.push_back(e.times);
        auto fake=e;fake.proposalOutcome=Outcome::Rejected;require(consumePrism(input,fake).boundary==Outcome::Certified,"producer success flag was trusted");
        fake=e;std::swap(fake.bottomByTop[0],fake.bottomByTop[1]);require(consumePrism(input,fake).boundary!=Outcome::Certified,"wrong top/bottom correspondence accepted");
        fake=e;fake.walls.pop_back();require(consumePrism(input,fake).boundary!=Outcome::Certified,"omitted wall claim accepted");
        fake=e;fake.walls[0].face=fake.walls[1].face;require(consumePrism(input,fake).boundary!=Outcome::Certified,"wrong wall claim accepted");
        fake=e;std::swap(fake.walls[0].bottomEdge,fake.walls[1].bottomEdge);require(consumePrism(input,fake).boundary!=Outcome::Certified,"wrong cap-edge wall connection accepted");
        fake=e;fake.walls[0].lateralEdges[0]=fake.walls[0].topEdge;require(consumePrism(input,fake).boundary!=Outcome::Certified,"wrong lateral geometry evidence accepted");
        fake=e;fake.faces.push_back(fake.faces.front());require(consumePrism(input,fake).boundary!=Outcome::Certified,"extra Face claim accepted");
        fake=e;fake.binding+="changed";require(consumePrism(input,fake).boundary!=Outcome::Certified,"source binding corruption accepted");
        fake=e;++fake.edgeUses;require(consumePrism(input,fake).boundary!=Outcome::Certified,"false full coverage counter accepted");
        fake=e;fake.faces[e.top].proof.outcome=Outcome::Certified;fake.faces[e.top].proof.pairs.pop_back();require(consumePrism(input,fake).boundary!=Outcome::Certified,"incomplete cap certificate accepted");
    }
    const auto m=[&](auto member){std::vector<double> v;for(const auto& t:times)v.push_back(t.*member);return median(v);};
    std::cout<<"PRISM_MEDIAN "<<name<<" caps=2 walls="<<n<<" edges="<<3*n<<" vertices="<<2*n<<" uses="<<6*n
      <<" cap_extract_ms="<<m(&PrismTimes::capExtractionMs)<<" cap_cert_ms="<<m(&PrismTimes::capCertificationMs)
      <<" wall_extract_ms="<<m(&PrismTimes::wallExtractionMs)<<" wall_correspond_ms="<<m(&PrismTimes::wallCorrespondenceMs)
      <<" independent_verify_ms="<<m(&PrismTimes::verificationMs)<<" material=Unresolved\n";
    const PrismInput otherContext(f.solid,f.top);const auto proof=producePrism(input);
    require(consumePrism(otherContext,proof).boundary==Outcome::Unresolved,"cross-context product evidence accepted");
    require(stamp(f.solid)==before,"source mutated");
}
}
void runSectionPrismTests() {
    const std::vector<gp_Pnt> rectangle={{0,0,0},{6,0,0},{6,4,0},{0,4,0}};
    const std::vector<gp_Pnt> polygon={{0,0,0},{6,0,0},{6,2,0},{2,2,0},{2,4,0},{0,4,0}};
    const auto box=prism(rectangle),nonconvex=prism(polygon);
    accepted("rectangle",box,4);accepted("nonconvex",nonconvex,6);
    gp_Trsf t;t.SetValues(1,0,0,16,0,0,-1,-8,0,1,0,32);TopLoc_Location l(t);
    accepted("90-X-translated",{nonconvex.solid.Moved(l),TopoDS::Face(nonconvex.top.Moved(l))},6);
    unsigned refused=0;const auto refusal=[&](const char* name,const Fixture& f){const auto before=stamp(f.solid);const PrismInput input(f.solid,f.top);const auto proposal=producePrism(input);const auto verdict=consumePrism(input,proposal);std::cout<<"PRISM_REFUSAL "<<name<<" boundary="<<int(verdict.boundary)<<" proposal="<<proposal.reason<<" consumer="<<verdict.reason<<'\n';require(verdict.boundary!=Outcome::Certified,"invalid/unsupported product certified");auto fake=proposal;fake.proposalOutcome=Outcome::Certified;require(consumePrism(input,fake).boundary!=Outcome::Certified,"fake success flag accepted");require(stamp(f.solid)==before,"negative source mutated");++refused;};
    BRep_Builder b;
    auto missing=clone(box);auto sh=shell(missing);sh.Free(true);b.Remove(sh,wall(missing));sh.Closed(true);refusal("missing-sidewall",missing);
    auto duplicate=clone(box);sh=shell(duplicate);sh.Free(true);b.Add(sh,wall(duplicate));sh.Closed(true);refusal("duplicate-sidewall",duplicate);
    auto connection=clone(box);auto w=wall(connection);auto wire=TopoDS::Wire(TopExp_Explorer(w,TopAbs_WIRE).Current());wire.Free(true);auto wrongEdge=TopExp_Explorer(connection.top,TopAbs_EDGE).Current();b.Remove(wire,TopExp_Explorer(w,TopAbs_EDGE).Current());b.Add(wire,wrongEdge);shell(connection).Closed(true);refusal("incorrect-wall-connection",connection);
    // Wrong top correspondence is checked by explicit evidence corruption in
    // every positive trial; additionally select a side Face as the purported cap.
    refusal("wrong-selected-top",{box.solid,wall(box)});
    auto reversed=clone(box);sh=shell(reversed);w=wall(reversed);sh.Free(true);b.Remove(sh,w);b.Add(sh,w.Reversed());sh.Closed(true);refusal("reversed-sidewall",reversed);
    auto extra=clone(box);sh=shell(extra);sh.Free(true);auto patch=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(50,0,0),gp_Dir(1,0,0)),0,1,0,1).Face();b.Add(sh,patch);sh.Closed(true);refusal("unexpected-Face",extra);
    auto tilted=clone(box);gp_Trsf turn;turn.SetValues(1,0,0,0,0,0,-1,0,0,1,0,0);auto tiltedTop=TopoDS::Face(tilted.top.Moved(TopLoc_Location(turn)));sh=shell(tilted);sh.Free(true);b.Remove(sh,tilted.top);b.Add(sh,tiltedTop);sh.Closed(true);tilted.top=tiltedTop;refusal("nonparallel-cap",tilted);
    // Valid line/polyhedral source with no parallel opposite Face for its
    // selected X cap. It is an unsupported negative, not an invalid OCCT solid.
    const auto wedge=BRepPrimAPI_MakeWedge(6,4,20,3).Shape();require(BRepCheck_Analyzer(wedge).IsValid(),"invalid wedge adversary");
    TopoDS_Face xcap;bool sloping=false;
    for(TopExp_Explorer f(wedge,TopAbs_FACE);f.More();f.Next()){TopLoc_Location loc;const auto plane=Handle(Geom_Plane)::DownCast(BRep_Tool::Surface(TopoDS::Face(f.Current()),loc));require(!plane.IsNull(),"wedge is not planar");const auto axis=plane->Axis().Direction();if(std::abs(axis.X())==1&&plane->Location().X()==0)xcap=TopoDS::Face(f.Current());if(axis.X()!=0&&std::abs(axis.X())!=1)sloping=true;}
    require(!xcap.IsNull()&&sloping,"nonparallel opposite-cap fixture missing");refusal("valid-nonparallel-opposite-cap",{wedge,xcap});
    refusal("wrong-height-21",prism(rectangle,21));
    auto trim=clone(box);b.Range(TopoDS::Edge(TopExp_Explorer(trim.top,TopAbs_EDGE).Current()),0.,1.);refusal("invalid-trim",trim);
    auto pcurve=clone(box);const auto pe=TopoDS::Edge(TopExp_Explorer(pcurve.top,TopAbs_EDGE).Current());double a,z;const auto pc=Handle(Geom2d_Line)::DownCast(BRep_Tool::CurveOnSurface(pe,pcurve.top,a,z));require(!pc.IsNull(),"pcurve fixture not line");
    Handle(Geom2d_Line) changed=new Geom2d_Line(pc->Location().Translated(gp_Vec2d(1,0)),pc->Direction());b.UpdateEdge(pe,changed,pcurve.top,1.e-7);refusal("invalid-pcurve",pcurve);
    auto disconnected=clone(box);auto other=prism(rectangle);gp_Trsf shift;shift.SetTranslation(gp_Vec(50,0,0));const auto otherShell=TopoDS::Shell(shell(other).Moved(TopLoc_Location(shift)));disconnected.solid.Free(true);b.Add(disconnected.solid,otherShell);refusal("additional-disconnected-shell",disconnected);
    std::cout<<"PRISM_SUMMARY adverse_refusals="<<refused<<" positive_fixtures=3 trials_each=3 material_certificate=Unresolved\n";
}
