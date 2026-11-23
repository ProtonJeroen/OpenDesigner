// Deterministic F2.3 geometry/evidence stress cases; no production algorithms.
#include "SectionVerifier.h"
#include "SectionExact.h"
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopExp_Explorer.hxx>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <set>
#include <sstream>
using namespace SectionExperiment;
namespace {
void demand(bool b,const char* why){if(!b)throw std::runtime_error(why);}
std::string stamp(const TopoDS_Shape& s){std::ostringstream out;BRepTools::Write(s,out);return out.str();}
struct Shape {TopoDS_Wire wire;std::vector<TopoDS_Edge> uses;};
const gp_Pln plane(gp_Pnt(),gp_Dir(0,0,1));
const std::vector<gp_Pnt> base={{0,0,0},{6,0,0},{6,2,0},{2,2,0},{2,4,0},{0,4,0}};
Shape polygon(const std::vector<gp_Pnt>& p) {
    BRepBuilderAPI_MakePolygon maker;for(const auto& point:p)maker.Add(point);maker.Close();
    demand(maker.IsDone(),"fixture could not be constructed");Shape result{maker.Wire(),{}};
    for(BRepTools_WireExplorer it(result.wire);it.More();it.Next())result.uses.push_back(it.Current());return result;
}
Shape occurrencePolygon(const std::vector<gp_Pnt>& points,bool shareJoints=true) {
    // Keep same-coordinate nonadjacent occurrences distinct: do not weld them.
    BRep_Builder b;Shape s;b.MakeWire(s.wire);std::vector<TopoDS_Vertex> vertices;
    for(const auto& p:points)vertices.push_back(BRepBuilderAPI_MakeVertex(p));
    for(size_t i=0;i<points.size();++i) {
        const auto edge=shareJoints?BRepBuilderAPI_MakeEdge(vertices[i],vertices[(i+1)%points.size()]).Edge()
                                 :BRepBuilderAPI_MakeEdge(points[i],points[(i+1)%points.size()]).Edge();
        b.Add(s.wire,edge);s.uses.push_back(edge);
    }
    return s;
}
const char* name(Outcome o){return o==Outcome::Certified?"Certified":o==Outcome::Rejected?"Rejected":"Unresolved";}
struct Totals {unsigned cases=0;double producerMax=0,consumerMax=0;std::string producerCase,consumerCase;} totals;
Evidence evaluate(const char* label,const Shape& shape,Outcome expected,bool fullPairs=true) {
    const auto before=stamp(shape.wire);const Input input(shape.wire,shape.uses,plane);
    EvaluationTiming producerStages,consumerStages;
    const auto t0=std::chrono::steady_clock::now();const auto proof=verify(input,&producerStages);
    const auto t1=std::chrono::steady_clock::now();const auto result=consumeEvidence(input,proof,LinePolicyVersion,&consumerStages);
    const auto t2=std::chrono::steady_clock::now();
    const double producer=std::chrono::duration<double,std::milli>(t1-t0).count();
    const double consumer=std::chrono::duration<double,std::milli>(t2-t1).count();
    ++totals.cases;if(producer>totals.producerMax){totals.producerMax=producer;totals.producerCase=label;}
    if(consumer>totals.consumerMax){totals.consumerMax=consumer;totals.consumerCase=label;}
    std::cout<<"F23 "<<label<<" expected="<<name(expected)<<" producer="<<name(proof.outcome)
      <<" consumer="<<name(result.outcome)<<" evidence_consistent="<<result.evidenceConsistent
      <<" pairs="<<proof.assessedPairs<<'/'<<proof.requiredPairs<<" verified="<<result.verifiedPairs
      <<" producer_ms="<<producer<<" consumer_ms="<<consumer
      <<" producer_prepare_ms="<<producerStages.preparationMs<<" producer_pairs_ms="<<producerStages.pairsMs
      <<" consumer_prepare_ms="<<consumerStages.preparationMs<<" consumer_pairs_ms="<<consumerStages.pairsMs<<" reason="<<proof.reason<<'\n';
    if(proof.outcome!=result.outcome)std::cout<<"F23 DISAGREEMENT "<<label<<" consumer_reason="<<result.reason<<'\n';
    demand(proof.outcome==expected&&result.outcome==expected,"metamorphic/adversarial outcome disagreement");
    if(fullPairs) {
        demand(proof.assessedPairs==proof.requiredPairs&&proof.pairs.size()==proof.requiredPairs,"missing full pair census");
        demand(result.evidenceConsistent,"complete geometric evidence disagrees with consumer");
    }
    std::set<std::pair<size_t,size_t>> pairs;
    for(const auto& pair:proof.pairs)demand(pair.first<pair.second&&pair.second<shape.uses.size()&&pairs.emplace(pair.first,pair.second).second,"pair duplicated or not canonical");
    if(shape.uses.size()<=64)demand(pairs.size()==proof.requiredPairs,"unchecked pairs disappeared");
    if(expected==Outcome::Certified){demand(proof.closure==Check::Pass&&proof.orientation==Check::Pass&&proof.interior==Check::Pass,"missing acceptance guarantees");demand(result.verifiedPairs==proof.requiredPairs,"accepted unresolved pair");}
    demand(proof==verify(input)&&result==consumeEvidence(input,proof),"repeatability failure");
    demand(stamp(shape.wire)==before,"source mutated during experiment");return proof;
}
}
void runSectionMetamorphicTests() {
    const auto original=polygon(base);const auto reference=evaluate("base",original,Outcome::Certified);
    auto transform=[&](double scale,double x,double y){auto p=base;for(auto& q:p)q=gp_Pnt(q.X()*scale+x,q.Y()*scale+y,0);return polygon(p);};
    for(const auto& move:std::vector<std::pair<double,double>>{{16,-8},{-32,64},{std::ldexp(1.,40),-std::ldexp(1.,40)}}) {
        const auto proof=evaluate(move.first>1000?"large-translation-2^40":"XY-translation",transform(1,move.first,move.second),Outcome::Certified);
        demand(proof.signedTwiceArea==32,"translation changed independent area");
    }
    for(double scale:{8.,.125,std::ldexp(1.,30),std::ldexp(1.,-20)}) {
        const auto proof=evaluate(scale>1000?"scale-2^30":scale<.001?"small-scale-2^-20":"positive-scaling",transform(scale,0,0),Outcome::Certified);
        demand(proof.signedTwiceArea==32*Arithmetic::exact(scale)*Arithmetic::exact(scale),"scale area law failed");
    }
    evaluate("small-scale-2^-24",transform(std::ldexp(1.,-24),0,0),Outcome::Unresolved);
    Shape reversed{TopoDS::Wire(original.wire.Reversed()),{}};
    for(auto it=original.uses.rbegin();it!=original.uses.rend();++it)reversed.uses.push_back(TopoDS::Edge(it->Reversed()));
    const auto reverseProof=evaluate("whole-wire-reversal",reversed,Outcome::Rejected);demand(reverseProof.signedTwiceArea==-32,"whole reversal area wrong");
    for(size_t offset=1;offset<original.uses.size();++offset) {
        auto rotated=original;std::rotate(rotated.uses.begin(),rotated.uses.begin()+offset,rotated.uses.end());
        demand(evaluate("cyclic-start-rotation",rotated,Outcome::Certified).signedTwiceArea==reference.signedTwiceArea,"cyclic start changed region");
    }
    auto split=base;split.insert(split.begin()+1,gp_Pnt(3,0,0));
    demand(evaluate("split-collinear-edge",polygon(split),Outcome::Certified).signedTwiceArea==32,"split changed area");
    split.erase(split.begin()+1);demand(evaluate("merge-collinear-edges",polygon(split),Outcome::Certified).signedTwiceArea==32,"merge changed area");
    auto fresh=occurrencePolygon(base);evaluate("fresh-valid-vertex-identities",fresh,Outcome::Certified);
    Shape storage;BRep_Builder b;b.MakeWire(storage.wire);storage.uses=fresh.uses;
    for(size_t i:{3u,0u,4u,1u,5u,2u})b.Add(storage.wire,storage.uses[i]);
    evaluate("permuted-wire-storage-valid-use-order",storage,Outcome::Certified);
    auto wrong=original;std::swap(wrong.uses[1],wrong.uses[2]);evaluate("wrong-directed-use-order",wrong,Outcome::Rejected,false);
    evaluate("coincident-but-unshared-joint-vertices",occurrencePolygon(base,false),Outcome::Rejected,false);
    evaluate("exact-crossing",polygon({{0,0,0},{4,0,0},{4,4,0},{2,4,0},{2,-2,0},{6,-2,0},{6,2,0},{0,2,0}}),Outcome::Rejected);
    evaluate("nonadjacent-vertex-contact",occurrencePolygon({{0,0,0},{2,0,0},{2,2,0},{0,2,0},{0,0,0},{-2,0,0},{-2,-2,0},{0,-2,0}}),Outcome::Rejected);
    evaluate("collinear-overlap",polygon({{0,0,0},{6,0,0},{6,4,0},{2,4,0},{2,0,0},{4,0,0},{4,-2,0},{0,-2,0}}),Outcome::Rejected);
    auto duplicate=original;duplicate.uses.back()=duplicate.uses.front();evaluate("duplicate-use",duplicate,Outcome::Rejected,false);
    auto zero=polygon(base);b.Range(zero.uses[0],0.,0.);evaluate("zero-length-domain",zero,Outcome::Rejected,false);
    for(int exponent:{-20,-24,-40}) {
        const double gap=std::ldexp(1.,exponent);
        const auto proof=evaluate(exponent==-20?"resolved-small-clearance":exponent==-24?"nearly-coincident":"tiny-nonzero-clearance-2^-40",
          polygon({{0,0,0},{8,0,0},{8,6,0},{5,6,0},{5,gap,0},{3,gap,0},{3,6,0},{0,6,0}}),exponent==-20?Outcome::Certified:Outcome::Unresolved);
        demand(proof.edges[4].start.y==Arithmetic::exact(gap)&&proof.edges[4].end.y==Arithmetic::exact(gap),"nominal positive clearance not preserved");
    }
    const auto flat=evaluate("zero-signed-area",polygon({{0,0,0},{2,0,0},{4,0,0}}),Outcome::Rejected);demand(flat.signedTwiceArea==0,"flat area oracle");
    auto coarse=polygon(base);TopExp_Explorer vertex(coarse.wire,TopAbs_VERTEX);b.UpdateVertex(TopoDS::Vertex(vertex.Current()),1.e-5);
    evaluate("outside-tolerance-contract",coarse,Outcome::Unresolved,false);
    evaluate("rounded-diagonal-carrier-identity",polygon({{0,0,0},{3,4,0},{0,4,0}}),Outcome::Unresolved,false);
    // All 64 uses/2016 pairs: one bounded stress case, not a benchmark loop.
    std::vector<gp_Pnt> square;
    for(int i=0;i<16;++i)square.emplace_back(i,0,0);
    for(int i=0;i<16;++i)square.emplace_back(16,i,0);
    for(int i=0;i<16;++i)square.emplace_back(16-i,16,0);
    for(int i=0;i<16;++i)square.emplace_back(0,16-i,0);
    demand(evaluate("64-use-boundary",polygon(square),Outcome::Certified).requiredPairs==2016,"upper-budget pair count");
    square.insert(square.begin()+1,gp_Pnt(.5,0,0));
    evaluate("65-use-outside-budget",polygon(square),Outcome::Unresolved,false);
    // Fresh contexts/ordering must never silently reuse previous receipts.
    const Input source(original.wire,original.uses,plane);const auto good=verify(source);
    unsigned attacks=0;
    const auto refuse=[&](const Input& input,const Evidence& proof){const auto result=consumeEvidence(input,proof);demand(result.outcome!=Outcome::Certified,"F23 tampered certificate accepted");++attacks;};
    auto bad=good;bad.pairs[0]=good.pairs[2];bad.pairs[0].first=0;bad.pairs[0].second=1;refuse(source,bad);
    bad=good;bad.pairs.erase(bad.pairs.begin());refuse(source,bad);
    bad=good;bad.pairs.push_back(bad.pairs.front());refuse(source,bad);
    bad=good;++bad.policyVersion;refuse(source,bad);
    auto rotated=original;std::rotate(rotated.uses.begin(),rotated.uses.begin()+1,rotated.uses.end());
    const Input reordered(rotated.wire,rotated.uses,plane);refuse(reordered,good);
    const auto changed=transform(1,32,16);const Input changedInput(changed.wire,changed.uses,plane);refuse(changedInput,good);
    const auto unrelated=polygon({{0,0,0},{10,0,0},{10,10,0},{0,10,0}});const Input unrelatedInput(unrelated.wire,unrelated.uses,plane);refuse(unrelatedInput,good);
    // Evidence order is not geometry order: complete unique pair records may
    // be permuted harmlessly; the independent ledger must still verify them.
    auto permuted=good;std::reverse(permuted.pairs.begin(),permuted.pairs.end());demand(consumeEvidence(source,permuted).outcome==Outcome::Certified,"pair ledger incorrectly assumes producer record order");
    std::cout<<"F23 SUMMARY cases="<<totals.cases<<" disagreements=0 tamper_refusals="<<attacks
      <<" producer_max_ms="<<totals.producerMax<<" producer_case="<<totals.producerCase
      <<" consumer_max_ms="<<totals.consumerMax<<" consumer_case="<<totals.consumerCase<<'\n';
}
