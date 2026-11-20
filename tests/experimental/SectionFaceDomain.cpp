#include "SectionFaceDomain.h"
#include "SectionExact.h"
#include <BRep_Tool.hxx>
#include <BRepTopAdaptor_FClass2d.hxx>
#include <BRepTools.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <chrono>
#include <map>
#include <set>
namespace SectionExperiment {
struct FaceDomainAccess {
    struct Facts {bool member,natural;int face,wire;double tolerance;size_t wires;};
    static Facts facts(const CapInput& i){Facts f{i.member_,false,int(i.face_.Orientation()),0,0,0};if(!i.member_)return f;f.natural=BRep_Tool::NaturalRestriction(i.face_);f.tolerance=BRep_Tool::Tolerance(i.face_);for(TopExp_Explorer w(i.face_,TopAbs_WIRE);w.More();w.Next()){++f.wires;f.wire=int(w.Current().Orientation());}return f;}
    static TopAbs_State classify(const CapInput& i,const Rational& x,const Rational& y) {
        const auto number=[](const Rational& r){double d=r.numerator().convert_to<double>()/r.denominator().convert_to<double>();if(!std::isfinite(d)||Arithmetic::exact(d)!=r)throw std::runtime_error("unrepresentable classifier point");return d;};
        const Rational rawY=i.face_.Orientation()==TopAbs_REVERSED?-y:y;
        const auto face=TopoDS::Face(i.face_.Oriented(TopAbs_FORWARD));
        return BRepTopAdaptor_FClass2d(face,BRep_Tool::Tolerance(face)).Perform(gp_Pnt2d(number(x),number(rawY)));
    }
};
namespace {
using Clock=std::chrono::steady_clock;
struct Refusal{Outcome outcome;std::string reason;};
void need(bool x,const std::string& why,Outcome o=Outcome::Unresolved){if(!x)throw Refusal{o,why};}
std::vector<Rational> xs(const std::vector<EdgeEvidence>& edges){std::set<Rational> s;for(const auto& e:edges){s.insert(e.start.x);s.insert(e.end.x);}return {s.begin(),s.end()};}
bool rectilinear(const EdgeEvidence& e){return (e.start.x==e.end.x)!=(e.start.y==e.end.y);}
int winding(const std::vector<EdgeEvidence>& edges,const Rational& x,const Rational& y) {
    int degree=0;
    // Independent consumer: horizontal ray crossing vertical edges, half-open Y.
    for(const auto& e:edges)if(e.start.x==e.end.x&&e.start.x>x){if(e.start.y<=y&&y<e.end.y)++degree;else if(e.end.y<=y&&y<e.start.y)--degree;}
    return degree;
}
}
FaceDomainEvidence produceFaceDomain(const CapInput& input,const CapClaim& claim) {
    FaceDomainEvidence e;e.context=claim.context;e.binding=claim.binding;
    try {
        const auto f=FaceDomainAccess::facts(input);need(f.member&&f.wires==1,"actual Face must contain one complete wire");
        e.faceOrientation=f.face;e.wireOrientation=f.wire;e.naturalRestriction=f.natural;
        need(!f.natural,"natural-restriction domain outside explicit trimmed-plane contract");
        need(claim.proof.outcome==Outcome::Certified,"section proposal is incomplete");
        e.breaks=xs(claim.proof.edges);need(e.breaks.size()>=2&&e.breaks.size()<=64,"domain slab budget");
        for(const auto& edge:claim.proof.edges)need(rectilinear(edge),"nonrectilinear Face domain unsupported");
        for(size_t i=0;i+1<e.breaks.size();++i) {
            const auto a=e.breaks[i],b=e.breaks[i+1];std::map<Rational,int> events;
            for(const auto& edge:claim.proof.edges)if(edge.start.y==edge.end.y&&std::min(edge.start.x,edge.end.x)<=a&&std::max(edge.start.x,edge.end.x)>=b)
                events[edge.start.y]+=edge.end.x>edge.start.x?1:-1;
            int w=0;for(auto it=events.begin();it!=events.end();++it){w+=it->second;need(w==0||w==1,"invalid winding/material domain",Outcome::Rejected);auto next=std::next(it);if(next!=events.end()&&w==1){e.cells.push_back({a,b,it->first,next->first});e.area+=(b-a)*(next->first-it->first);}}
            need(w==0,"unbounded winding domain",Outcome::Rejected);
        }
        need(!e.cells.empty()&&e.cells.size()<=4096,"empty/overbudget domain cover");e.proposalOutcome=Outcome::Certified;e.reason="nominal bounded winding-one cell cover proposed";
    }catch(const Refusal& x){e.proposalOutcome=x.outcome;e.reason=x.reason;}
     catch(const Standard_Failure&){e.reason="OCCT domain proposal extraction failed";}
     catch(const std::exception&){e.reason="domain proposal arithmetic failed";}
    return e;
}
FaceDomainVerdict consumeFaceDomain(const CapInput& input,const CapClaim& claim,const FaceDomainEvidence& e) {
    FaceDomainVerdict r;const auto begin=Clock::now();auto domainStart=begin;bool domainStarted=false;
    const auto finish=[&](Outcome o,const std::string& why){r.nominalDomain=o;r.reason=why;r.verificationMs=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();if(domainStarted)r.domainChecksMs=std::chrono::duration<double,std::milli>(Clock::now()-domainStart).count();else r.bindingVerificationMs=r.verificationMs;return r;};
    try {
        need(e.context==claim.context&&e.binding==claim.binding,"domain evidence source binding mismatch");
        const auto f=FaceDomainAccess::facts(input);need(f.member&&f.wires==1,"actual Face domain is missing or has inner wires",Outcome::Rejected);
        need(f.face==e.faceOrientation&&f.wire==e.wireOrientation&&f.natural==e.naturalRestriction,"actual Face/wire orientation/domain flags differ");
        need(!f.natural,"unproven alternative natural Face domain");
        const auto cap=consumeCap(input,claim);need(cap.outcome==Outcome::Certified,"actual cap/pcurve/section binding: "+cap.reason,cap.outcome);
        r.bindingVerificationMs=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();domainStart=Clock::now();domainStarted=true;
        const auto& edges=claim.proof.edges;for(const auto& edge:edges)need(rectilinear(edge),"unsupported domain segment");
        const auto breaks=xs(edges);need(breaks==e.breaks&&breaks.size()>=2&&breaks.size()<=64,"incomplete/corrupt domain X partition");
        std::vector<DomainCell> expected;Rational area=0;
        for(size_t i=0;i+1<breaks.size();++i) {
            const Rational a=breaks[i],b=breaks[i+1],mid=(a+b)/2;std::set<Rational> heights;
            for(const auto& edge:edges)if(edge.start.y==edge.end.y&&std::min(edge.start.x,edge.end.x)<mid&&mid<std::max(edge.start.x,edge.end.x))heights.insert(edge.start.y);
            need(heights.size()>=2,"incomplete slab boundary");
            need(winding(edges,mid,*heights.begin()-1)==0&&winding(edges,mid,*heights.rbegin()+1)==0,"unbounded domain",Outcome::Rejected);
            for(auto y=heights.begin();std::next(y)!=heights.end();++y) {
                const auto next=std::next(y);const int w=winding(edges,mid,(*y+*next)/2);
                need(w==0||w==1,"invalid interior winding",Outcome::Rejected);
                // All line endpoints are X breaks; each horizontal/vertical
                // event is constant over this whole open slab, not just at mid.
                if(w==1){expected.push_back({a,b,*y,*next});area+=(b-a)*(*next-*y);}
            }
        }
        need(expected==e.cells&&!expected.empty()&&area==e.area&&area*2==claim.proof.signedTwiceArea,"domain cover/area witness differs from exact winding cells");
        for(const auto& c:expected){need(c.left<c.right&&c.low<c.high,"invalid cell");const Rational clearance=std::min(c.right-c.left,c.high-c.low)/2;need(clearance>Arithmetic::exact(f.tolerance),"domain cell not resolvable at recorded Face tolerance");need(FaceDomainAccess::classify(input,(c.left+c.right)/2,(c.low+c.high)/2)==TopAbs_IN,"OCCT domain interior consistency is inconclusive");++r.kernelChecks;}
        const Rational x=breaks.back()+1;Rational maxY=expected.front().high;for(const auto& c:expected)maxY=std::max(maxY,c.high);
        need(FaceDomainAccess::classify(input,x,maxY+1)==TopAbs_OUT,"OCCT domain exterior consistency is inconclusive");++r.kernelChecks;
        r.cells=expected.size();
        return finish(Outcome::Certified,"exact nominal oriented trim domain covered; kernel consistency probes are not all-point semantics proof");
    }catch(const Refusal& x){return finish(x.outcome,x.reason);}
     catch(const Standard_Failure&){return finish(Outcome::Unresolved,"OCCT domain classification failed");}
     catch(const std::exception&){return finish(Outcome::Unresolved,"domain arithmetic/representation inconclusive");}
}
}
