#include "SectionCapCorrespondence.h"
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <chrono>
#include <iostream>
#include <sstream>
#include <cmath>
using namespace SectionExperiment;
namespace {
void require(bool b,const char* reason){if(!b)throw std::runtime_error(reason);}
std::string stamp(const TopoDS_Shape& s){std::ostringstream o;BRepTools::Write(s,o);return o.str();}
struct Fixture {TopoDS_Shape solid;TopoDS_Face cap;};
TopoDS_Wire polygon(const std::vector<gp_Pnt>& p){BRepBuilderAPI_MakePolygon m;for(auto q:p)m.Add(q);m.Close();return m.Wire();}
Fixture prism(const std::vector<gp_Pnt>& p,bool hole=false) {
    BRepBuilderAPI_MakeFace face(gp_Pln(gp_Pnt(),gp_Dir(0,0,1)),polygon(p),true);
    if(hole){auto inner=polygon({{1,1,0},{2,1,0},{2,2,0},{1,2,0}});inner.Reverse();face.Add(inner);}
    BRepPrimAPI_MakePrism sweep(face.Face(),gp_Vec(0,0,20),true,true);return {sweep.Shape(),TopoDS::Face(sweep.LastShape())};
}
Fixture copied(const Fixture& f){BRepBuilderAPI_Copy c(f.solid,true,false);auto cap=TopoDS::Face(c.ModifiedShape(f.cap));cap.Orientation(f.cap.Orientation());return {c.Shape(),cap};}
Fixture placed(const Fixture& f,const gp_Trsf& t){TopLoc_Location l(t);return {f.solid.Moved(l),TopoDS::Face(f.cap.Moved(l))};}
TopoDS_Edge edge(const TopoDS_Face& f){return TopoDS::Edge(TopExp_Explorer(f,TopAbs_EDGE).Current());}
CapClaim accepted(const CapInput& input,const char* label) {
    const auto t0=std::chrono::steady_clock::now();auto extraction=extractCap(input);
    const auto t1=std::chrono::steady_clock::now();
    if(extraction.outcome!=Outcome::Certified)std::cout<<"CAP extraction refusal "<<label<<": "<<extraction.reason<<'\n';
    require(extraction.outcome==Outcome::Certified&&extraction.claim.section,"cap extraction failed");
    auto claim=extraction.claim;claim.proof=verify(*claim.section);const auto t2=std::chrono::steady_clock::now();
    require(claim.proof.outcome==Outcome::Certified,"extracted section not certified");
    const auto result=consumeCap(input,claim);const auto t3=std::chrono::steady_clock::now();
    if(result.outcome!=Outcome::Certified)std::cout<<"CAP correspondence refusal "<<label<<": "<<result.reason<<'\n';
    require(result.outcome==Outcome::Certified&&result.matchedEdges==extraction.edgeCount,"cap correspondence failed");
    require(extraction.adjacentFaces.size()==extraction.edgeCount,"missing actual adjacency");
    std::cout<<"CAP_TIMING "<<label<<" edges="<<extraction.edgeCount<<" pairs="<<claim.proof.requiredPairs
      <<" extraction_ms="<<std::chrono::duration<double,std::milli>(t1-t0).count()
      <<" production_ms="<<std::chrono::duration<double,std::milli>(t2-t1).count()
      <<" consumption_ms="<<result.sectionConsumptionMs<<" correspondence_ms="<<result.correspondenceMs
      <<" total_ms="<<std::chrono::duration<double,std::milli>(t3-t0).count()<<'\n';
    return claim;
}
}
void runSectionCapTests() {
    const auto box=prism({{0,0,0},{6,0,0},{6,4,0},{0,4,0}});
    const auto nonconvex=prism({{0,0,0},{6,0,0},{6,2,0},{2,2,0},{2,4,0},{0,4,0}});
    double low=1.e30,high=-1.e30;
    for(TopExp_Explorer it(nonconvex.solid,TopAbs_VERTEX);it.More();it.Next()){const auto z=BRep_Tool::Pnt(TopoDS::Vertex(it.Current())).Z();low=std::min(low,z);high=std::max(high,z);}
    require(low==0&&high==20,"independent initial prism height");
    const auto initial=stamp(nonconvex.solid);
    const CapInput context(nonconvex.solid,nonconvex.cap);
    auto good=accepted(context,"nonconvex-XY");require(good.proof.signedTwiceArea==32,"independent section area");
    const CapInput rectangle(box.solid,box.cap);accepted(rectangle,"rectangle-XY");
    gp_Trsf exact;exact.SetValues(1,0,0,16, 0,0,-1,-8, 0,1,0,32);
    const auto rotated=placed(nonconvex,exact);const auto rotatedBefore=stamp(rotated.solid);
    const CapInput rotatedContext(rotated.solid,rotated.cap);auto rotatedGood=accepted(rotatedContext,"rotated-90-X-translated");
    require(rotatedGood.proof.signedTwiceArea==good.proof.signedTwiceArea,"placement changed local section");
    require(rotatedGood.frame.size()==12&&rotatedGood.frame[9]==0&&rotatedGood.frame[10]==-1&&rotatedGood.frame[11]==0,"placed cap normal incorrectly assumed global Z");
    // A second small deterministic trial, not a benchmark loop.
    accepted(context,"nonconvex-XY-repeat");accepted(rotatedContext,"rotated-repeat");
    std::vector<gp_Pnt> subdivided;
    for(int i=0;i<4;++i)subdivided.emplace_back(i,0,0);
    for(int i=0;i<4;++i)subdivided.emplace_back(4,i,0);
    for(int i=0;i<4;++i)subdivided.emplace_back(4-i,4,0);
    for(int i=0;i<4;++i)subdivided.emplace_back(0,4-i,0);
    const auto many=prism(subdivided);const CapInput larger(many.solid,many.cap);require(accepted(larger,"16-edge-XY").proof.requiredPairs==120,"16-edge coverage");
    unsigned rejected=0;
    const auto refuse=[&](const CapInput& source,const CapClaim& claim,const char* name){const auto r=consumeCap(source,claim);std::cout<<"CAP_REFUSAL "<<name<<" outcome="<<int(r.outcome)<<" reason="<<r.reason<<'\n';require(r.outcome!=Outcome::Certified,"corrupt cap claim certified");++rejected;};
    auto bad=good;bad.order.pop_back();refuse(context,bad,"missing-cap-edge");
    bad=good;bad.order.back()=bad.order.front();refuse(context,bad,"duplicate-cap-use");
    const CapInput reversed(nonconvex.solid,TopoDS::Face(nonconvex.cap.Reversed()));
    require(extractCap(reversed).outcome==Outcome::Rejected,"reversed oriented cap accepted as source member");++rejected;
    const auto holed=prism({{0,0,0},{8,0,0},{8,6,0},{0,6,0}},true);const CapInput inner(holed.solid,holed.cap);
    require(BRepCheck_Analyzer(holed.solid).IsValid(),"invalid inner-wire fixture");
    const auto innerResult=extractCap(inner);require(innerResult.outcome!=Outcome::Certified&&innerResult.reason=="cap must have exactly one wire (no inner wires)","unexpected inner wire accepted or wrong refusal stage");++rejected;
    std::cout<<"CAP_REFUSAL inner-wire reason="<<innerResult.reason<<'\n';
    auto missingActual=copied(nonconvex);auto outer=TopoDS::Wire(TopExp_Explorer(missingActual.cap,TopAbs_WIRE).Current());
    BRep_Builder edit;outer.Free(true);edit.Remove(outer,edge(missingActual.cap));const CapInput absent(missingActual.solid,missingActual.cap);
    require(extractCap(absent).outcome!=Outcome::Certified,"actual omitted cap edge accepted");++rejected;
    auto duplicateActual=copied(nonconvex);outer=TopoDS::Wire(TopExp_Explorer(duplicateActual.cap,TopAbs_WIRE).Current());outer.Free(true);edit.Add(outer,edge(duplicateActual.cap));
    const CapInput repeated(duplicateActual.solid,duplicateActual.cap);require(extractCap(repeated).outcome!=Outcome::Certified,"actual duplicate cap edge accepted");++rejected;
    auto trimmed=copied(nonconvex);BRep_Builder builder;builder.Range(edge(trimmed.cap),0.,1.);
    const CapInput wrongTrim(trimmed.solid,trimmed.cap);const auto trim=extractCap(wrongTrim);
    require(trim.outcome!=Outcome::Certified,"incorrect actual trim accepted");++rejected;std::cout<<"CAP_REFUSAL actual-trim reason="<<trim.reason<<'\n';
    // Same number of edges and area 16, translated section, valid independent
    // certificate: scalar area/count equality cannot establish correspondence.
    BRepBuilderAPI_MakePolygon shifted;for(gp_Pnt p:std::vector<gp_Pnt>{{0,0,0},{6,0,0},{6,2,0},{2,2,0},{2,4,0},{0,4,0}})shifted.Add(p.Translated(gp_Vec(10,0,0)));shifted.Close();
    std::vector<TopoDS_Edge> uses;for(BRepTools_WireExplorer it(shifted.Wire());it.More();it.Next())uses.push_back(it.Current());
    bad=good;bad.section=std::make_shared<const Input>(shifted.Wire(),uses,gp_Pln(gp_Pnt(),gp_Dir(0,0,1)));bad.proof=verify(*bad.section);
    require(bad.proof.outcome==Outcome::Certified&&bad.proof.signedTwiceArea==32,"equal-area negative fixture invalid");refuse(context,bad,"equal-area-different-section");
    bad=rotatedGood;bad.frame=good.frame;refuse(rotatedContext,bad,"inconsistent-transformed-frame");
    refuse(rotatedContext,good,"stale-unplaced-cap-claim");
    auto ambiguous=copied(nonconvex);auto v=TopoDS::Vertex(TopExp_Explorer(ambiguous.cap,TopAbs_VERTEX).Current());
    builder.UpdateVertex(v,BRep_Tool::Pnt(v).Translated(gp_Vec(std::ldexp(1.,-30),0,0)),1.e-7);
    const CapInput unclear(ambiguous.solid,ambiguous.cap);const auto ambiguity=extractCap(unclear);
    require(ambiguity.outcome==Outcome::Unresolved&&ambiguity.reason=="ambiguous exact carrier/vertex endpoint identity","ambiguous endpoint identity accepted or wrong refusal stage");++rejected;
    std::cout<<"CAP_REFUSAL ambiguous-endpoint reason="<<ambiguity.reason<<'\n';
    gp_Trsf rounded;rounded.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,0,0)),std::acos(-1.)/6);rounded.SetTranslationPart(gp_Vec(16,-8,32));
    const auto general=placed(nonconvex,rounded);const CapInput unsupported(general.solid,general.cap);const auto unproven=extractCap(unsupported);
    require(unproven.outcome==Outcome::Unresolved,"unproved general rigid transform accepted");++rejected;
    std::cout<<"CAP_GENERAL_ROTATION refusal="<<unproven.reason<<'\n';
    require(stamp(nonconvex.solid)==initial&&stamp(rotated.solid)==rotatedBefore,"cap experiment mutated source");
    require(consumeCap(context,good).outcome==Outcome::Certified,"cap result is not repeatable");
    std::cout<<"CAP_SUMMARY adversarial_refusals="<<rejected<<" height=20 source_immutable=1\n";
}
