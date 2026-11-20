#include "SectionVerifier.h"
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS.hxx>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <chrono>
#include <iomanip>
using namespace SectionExperiment;
namespace {
void require(bool x,const char* reason){if(!x)throw std::runtime_error(reason);}
std::string stamp(const TopoDS_Shape& s){std::ostringstream o;BRepTools::Write(s,o);return o.str();}
struct Fixture {TopoDS_Wire wire;std::vector<TopoDS_Edge> uses;};
Fixture fixture(const std::vector<gp_Pnt>& points) {
    BRepBuilderAPI_MakePolygon p;for(const auto& point:points)p.Add(point);p.Close();
    Fixture f{p.Wire(),{}};
    for(BRepTools_WireExplorer it(f.wire);it.More();it.Next())f.uses.push_back(it.Current());
    return f;
}
const gp_Pln plane(gp_Pnt(0,0,0),gp_Dir(0,0,1));
void show(const char* name,const Evidence& e) {
    std::cout<<name<<" outcome="<<int(e.outcome)<<" uses="<<e.edgeUses<<" pairs="<<e.requiredPairs
      <<" assessed="<<e.assessedPairs<<" verified="<<e.verifiedPairs<<" closure="<<int(e.closure)
      <<" intersection="<<int(e.intersection)<<" orientation="<<int(e.orientation)
      <<" arithmetic="<<e.arithmetic<<" reason="<<e.reason<<'\n';
    for(const auto& p:e.pairs)if(p.outcome!=Check::Pass)std::cout<<" pair "<<p.first<<","<<p.second<<": "<<p.reason<<'\n';
}
Evidence exercise(const char* name,const Fixture& f,Outcome expected) {
    const auto before=stamp(f.wire);const Input input(f.wire,f.uses,plane);
    const auto e=verify(input);show(name,e);require(e.outcome==expected,"fixture classification");
    require(e==verify(input)&&checkEvidence(input,e),"nonrepeatable/evidence mismatch");
    const auto verdict=consumeEvidence(input,e);
    require(verdict.outcome==expected&&verdict.evidenceConsistent,"independent consumer classification");
    require(verdict==consumeEvidence(input,e),"nonrepeatable consumer");
    for(int trial=0;trial<3;++trial) {
        const auto begin=std::chrono::steady_clock::now();const auto produced=verify(input);
        const auto middle=std::chrono::steady_clock::now();const auto consumed=consumeEvidence(input,produced);
        const auto end=std::chrono::steady_clock::now();
        require(consumed.outcome==expected&&consumed.evidenceConsistent,"timed consumer regression");
        std::cout<<std::setprecision(9)<<"CONSUMER_TIMING "<<name<<" trial="<<trial
          <<" producer_ms="<<std::chrono::duration<double,std::milli>(middle-begin).count()
          <<" consumer_ms="<<std::chrono::duration<double,std::milli>(end-middle).count()
          <<" verified_pairs="<<consumed.verifiedPairs<<" pair_calculations="<<consumed.pairCalculations<<'\n';
    }
    require(e.assessedPairs==e.requiredPairs&&e.pairs.size()==e.requiredPairs,"incomplete pair ledger");
    require(e.closure==Check::Pass,"closed fixture rejected at closure");
    auto changed=e;changed.requiredPairs++;require(!checkEvidence(input,changed),"tampered count trusted");
    changed=e;changed.pairs.pop_back();require(!checkEvidence(input,changed),"missing pair trusted");
    require(stamp(f.wire)==before,"caller B-Rep mutated");return e;
}
}
void runSectionMetamorphicTests();
void runSectionCapTests();
void runSectionPrismTests();
void runSectionPrismDomainTests();
int main() {
 try {
    const std::vector<gp_Pnt> points={{0,0,0},{6,0,0},{6,2,0},{2,2,0},{2,4,0},{0,4,0}};
    auto a=fixture(points);auto e=exercise("A-nonconvex",a,Outcome::Certified);
    require(e.requiredPairs==15&&e.verifiedPairs==15,"A accounting");
    require(e.signedTwiceArea==32&&e.orientation==Check::Pass&&e.interior==Check::Pass,"independent L-section area/orientation");
    auto b=fixture({{0,0,0},{4,0,0},{4,4,0},{2,4,0},{2,-2,0},{6,-2,0},{6,2,0},{0,2,0}});
    e=exercise("B-crossing",b,Outcome::Rejected);
    bool found=false;for(const auto& p:e.pairs)if(p.first==0&&p.second==3){found=p.outcome==Check::Fail;require(p.determinants[0]*p.determinants[1]<0&&p.determinants[2]*p.determinants[3]<0,"independent crossing signs");}
    require(found&&e.intersection==Check::Fail,"missing exact crossing at (2,0)");
    const double gap=std::ldexp(1.,-24);
    auto c=fixture({{0,0,0},{8,0,0},{8,6,0},{5,6,0},{5,gap,0},{3,gap,0},{3,6,0},{0,6,0}});
    e=exercise("C-near-contact",c,Outcome::Unresolved);
    require(e.intersection==Check::Uncertain&&e.verifiedPairs<e.requiredPairs,"uncertainty promoted to proof");
    found=false;for(const auto& p:e.pairs)if(p.first==0&&p.second==4){found=p.outcome==Check::Uncertain;require(p.determinants[0]>0&&p.determinants[1]>0,"gap is positive, not crossing");}require(found,"near-gap pair evidence missing");
    auto overlapping=fixture({{0,0,0},{6,0,0},{6,4,0},{2,4,0},{2,0,0},{4,0,0},{4,-2,0},{0,-2,0}});
    const auto overlap=exercise("overlap",overlapping,Outcome::Rejected);
    found=false;for(const auto& p:overlap.pairs)if(p.first==0&&p.second==4)found=p.outcome==Check::Fail&&p.reason=="positive-length collinear overlap";
    require(found,"overlapping collinear interval missed");
    auto zero=fixture(points);BRep_Builder zeroBuilder;zeroBuilder.Range(zero.uses[0],0.,0.);
    const auto empty=verify(Input(zero.wire,zero.uses,plane));require(empty.outcome==Outcome::Rejected&&empty.regularity==Check::Fail,"zero-domain line accepted");
    auto duplicate=a.uses;duplicate.back()=duplicate.front();require(verify(Input(a.wire,duplicate,plane)).outcome==Outcome::Rejected,"duplicate use accepted");
    // Deliberately corrupt proof data; consumer never calls the producer.
    const Input original(a.wire,a.uses,plane);const auto validProof=verify(original);
    unsigned attacks=0;
    const auto tamper=[&](auto edit){auto proof=validProof;edit(proof);const auto result=consumeEvidence(original,proof);require(result.outcome==Outcome::Unresolved&&!result.evidenceConsistent,"corrupted evidence certified or labeled invalid geometry");++attacks;};
    tamper([](auto& p){p.pairs.pop_back();});
    tamper([](auto& p){p.pairs[1]=p.pairs[0];});
    tamper([](auto& p){std::swap(p.pairs[0].first,p.pairs[0].second);});
    tamper([](auto& p){p.edges[0].end.x+=1;});
    tamper([](auto& p){p.signedTwiceArea=-p.signedTwiceArea;p.orientation=Check::Fail;});
    tamper([](auto& p){p.pairs[0].reason="exactly disjoint";}); // intended endpoint mislabeled
    tamper([](auto& p){p.pairs[1].reason="exact intended endpoint only (nonoverlap line pair)";});
    tamper([](auto& p){p.pairs[0].determinants[0]+=1;});
    tamper([](auto& p){p.sourceBinding+="changed";});
    tamper([](auto& p){p.policyVersion++;});
    tamper([](auto& p){p.outcome=Outcome::Rejected;});
    require(consumeEvidence(original,validProof,LinePolicyVersion+1).outcome==Outcome::Unresolved,"required policy ignored");
    const Input crossingInput(b.wire,b.uses,plane);auto falseDisjoint=verify(crossingInput);
    falseDisjoint.outcome=Outcome::Certified;
    require(consumeEvidence(crossingInput,falseDisjoint).outcome==Outcome::Rejected,"fake producer Certified trusted for crossing");
    for(auto& p:falseDisjoint.pairs)if(p.first==0&&p.second==3){p.outcome=Check::Pass;p.reason="exactly disjoint";}
    require(consumeEvidence(crossingInput,falseDisjoint).outcome==Outcome::Unresolved,"false disjoint witness accepted");
    const Input nearInput(c.wire,c.uses,plane);auto falseNear=verify(nearInput);falseNear.outcome=Outcome::Certified;
    require(consumeEvidence(nearInput,falseNear).outcome==Outcome::Unresolved,"near-contact promoted by fake flag");
    Evidence emptyProof;emptyProof.outcome=Outcome::Certified;
    require(consumeEvidence(original,emptyProof).outcome==Outcome::Unresolved,"flag-only certificate accepted");
    require(consumeEvidence(crossingInput,validProof).outcome==Outcome::Unresolved,"different section binding accepted");
    const Input identicalCopy(a.wire,a.uses,plane);
    require(consumeEvidence(identicalCopy,validProof).outcome==Outcome::Unresolved,"cross-evaluation receipt accepted");
    auto shiftedPoints=points;for(auto& p:shiftedPoints)p.Translate(gp_Vec(10,0,0));auto shifted=fixture(shiftedPoints);
    const Input shiftedInput(shifted.wire,shifted.uses,plane);auto shiftedProof=validProof;
    require(consumeEvidence(shiftedInput,shiftedProof).outcome==Outcome::Unresolved,"changed geometry accepted");
    const auto actualShiftedProof=verify(shiftedInput);shiftedProof.sourceContext=actualShiftedProof.sourceContext;shiftedProof.sourceBinding=actualShiftedProof.sourceBinding;
    require(consumeEvidence(shiftedInput,shiftedProof).outcome==Outcome::Unresolved,"forged binding bypassed endpoint checks");
    // A real caller mutation cannot mutate the old isolated context, and a fresh
    // context of that edited geometry must reject the old receipt.
    auto mutated=fixture(points);const Input beforeMutation(mutated.wire,mutated.uses,plane);const auto oldProof=verify(beforeMutation);
    BRep_Builder mutation;mutation.Range(mutated.uses[0],0.,5.);
    const auto editedStamp=stamp(mutated.wire);const Input afterMutation(mutated.wire,mutated.uses,plane);
    require(consumeEvidence(afterMutation,oldProof).outcome==Outcome::Unresolved,"stale proof accepted after mutation");
    require(consumeEvidence(beforeMutation,oldProof).outcome==Outcome::Certified,"isolated immutable context was changed through caller alias");
    require(stamp(mutated.wire)==editedStamp,"consumer mutated edited source");
    std::cout<<"Independent consumer adversarial checks passed ("<<attacks<<" direct corruptions plus stale/forged source and fake-status cases).\n";
    auto omitted=a.uses;omitted.pop_back();require(verify(Input(a.wire,omitted,plane)).outcome==Outcome::Rejected,"missing use accepted");
    auto order=a.uses;std::swap(order[1],order[2]);require(verify(Input(a.wire,order,plane)).closure==Check::Fail,"wrong cycle accepted");
    order=a.uses;order[1].Reverse();require(verify(Input(a.wire,order,plane)).outcome==Outcome::Rejected,"single reversed use accepted");
    auto reverse=points;std::reverse(reverse.begin(),reverse.end());auto clockwise=fixture(reverse);
    const auto reversed=verify(Input(clockwise.wire,clockwise.uses,plane));
    require(reversed.outcome==Outcome::Rejected&&reversed.orientation==Check::Fail&&reversed.signedTwiceArea==-32,"clockwise orientation ignored");
    const Input clockwiseInput(clockwise.wire,clockwise.uses,plane);const auto clockwiseProof=verify(clockwiseInput);
    require(consumeEvidence(clockwiseInput,clockwiseProof).outcome==Outcome::Rejected,"consumer ignored clockwise policy");
    auto open=fixture(points);BRep_Builder builder;builder.Remove(open.wire,open.uses.back());open.uses.pop_back();
    require(verify(Input(open.wire,open.uses,plane)).closure==Check::Fail,"open wire accepted");
    runSectionMetamorphicTests();
    runSectionCapTests();
    runSectionPrismTests();
    runSectionPrismDomainTests();
    std::cout<<"Whole-section line-only experiment passed; no production certification used.\n";return 0;
 }catch(const Standard_Failure& e){std::cerr<<e.GetMessageString()<<'\n';return 1;}
  catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
