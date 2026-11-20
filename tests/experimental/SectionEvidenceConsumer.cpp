// Independent F2.2 consumer: no calls to verify(), producer pair(), or producer
// geometry extraction. Shared code is limited to exact binary64 decoding/types.
#include "SectionVerifier.h"
#include "SectionExact.h"
#include "SectionTiming.h"
#include <BRep_Tool.hxx>
#include <Geom_Line.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <algorithm>
#include <set>
namespace SectionExperiment {
namespace {
using Arithmetic::exact;
ExactPoint point(const gp_Pnt& p){return {exact(p.X()),exact(p.Y()),exact(p.Z())};}
Rational det(const Rational& x,const Rational& y,const Rational& u,const Rational& v){return x*v-y*u;}
Rational orient(const ExactPoint& a,const ExactPoint& b,const ExactPoint& c){return det(b.x-a.x,b.y-a.y,c.x-a.x,c.y-a.y);}
struct Decision {Check state;const char* reason;};
Decision relation(const EdgeEvidence& a,const EdgeEvidence& b,bool adjacent) {
    const Rational rx=a.end.x-a.start.x,ry=a.end.y-a.start.y;
    const Rational sx=b.end.x-b.start.x,sy=b.end.y-b.start.y;
    const Rational qx=b.start.x-a.start.x,qy=b.start.y-a.start.y;
    const Rational denominator=det(rx,ry,sx,sy);
    bool hit=false,overlap=false;
    if(denominator!=0) {
        // Solve the line parameters independently of the producer's four-sign test.
        const Rational t=det(qx,qy,sx,sy)/denominator;
        const Rational u=det(qx,qy,rx,ry)/denominator;
        hit=t>=0&&t<=1&&u>=0&&u<=1;
        if(hit&&adjacent&&!( (t==0||t==1)&&(u==0||u==1) ))return {Check::Fail,"adjacent endpoints do not meet"};
    } else if(det(qx,qy,rx,ry)==0) {
        const Rational t0=rx!=0?qx/rx:qy/ry;
        const Rational t1=rx!=0?(b.end.x-a.start.x)/rx:(b.end.y-a.start.y)/ry;
        const Rational lower=std::max(Rational(0),std::min(t0,t1));
        const Rational upper=std::min(Rational(1),std::max(t0,t1));
        overlap=lower<upper;hit=lower<=upper;
    }
    if(overlap)return {Check::Fail,"positive-length collinear overlap"};
    if(!adjacent&&hit)return {Check::Fail,"forbidden nonadjacent intersection/contact"};
    if(adjacent&&!hit)return {Check::Fail,"adjacent endpoints do not meet"};
    if(adjacent)return {Check::Pass,"exact intended endpoint only (nonoverlap line pair)"}; // Exact closure below establishes designated joint.
    const Rational margin=a.guard+b.guard;
    const Rational aa[2][2]={{a.start.x,a.end.x},{a.start.y,a.end.y}};
    const Rational bb[2][2]={{b.start.x,b.end.x},{b.start.y,b.end.y}};
    for(int axis=0;axis<2;++axis)
        if(std::max(aa[axis][0],aa[axis][1])+margin<std::min(bb[axis][0],bb[axis][1])
          ||std::max(bb[axis][0],bb[axis][1])+margin<std::min(aa[axis][0],aa[axis][1]))return {Check::Pass,"exactly disjoint"};
    return {Check::Uncertain,"nominally disjoint; recorded-tolerance AABB separation unresolved"};
}
}
Consumption consumeEvidence(const Input& input,const Evidence& proof,unsigned version,EvaluationTiming* timing) {
    EvaluationMeasurement measurement(timing);
    Consumption answer;
    const auto unresolved=[&](const char* reason){answer.outcome=Outcome::Unresolved;answer.reason=reason;return answer;};
    const auto rejected=[&](const char* reason){answer.outcome=Outcome::Rejected;answer.reason=reason;return answer;};
    try {
        if(proof.arithmetic!=Evidence{}.arithmetic)return unresolved("arithmetic evidence label differs from policy");
        if(version!=LinePolicyVersion||proof.policyVersion!=version)return unresolved("unsupported/mismatched proof policy");
        if(proof.sourceContext!=input.context_||proof.sourceBinding!=input.binding_)return unresolved("source/evaluation binding mismatch");
        const size_t n=input.uses_.size();
        if(n<3||n>64)return unresolved("outside bounded consumer scope");
        const auto normal=input.plane_.Axis().Direction();
        if(normal.X()!=0||normal.Y()!=0||normal.Z()!=1||input.plane_.Location().Z()!=0)return unresolved("unsupported plane frame");
        std::vector<TopoDS_Edge> census;
        for(TopExp_Explorer it(input.wire_,TopAbs_EDGE);it.More();it.Next())census.push_back(TopoDS::Edge(it.Current()));
        if(census.size()!=n)return rejected("actual use census inconsistent with supplied order");
        std::vector<bool> seen(n,false);TopTools_IndexedMapOfShape vertices;std::vector<EdgeEvidence> geometry;
        for(const auto& edge:input.uses_) {
            size_t at=0;while(at<n&&(seen[at]||!edge.IsEqual(census[at])))++at;
            if(at==n)return rejected("actual oriented use missing/duplicated");seen[at]=true;
            if(edge.Orientation()!=TopAbs_FORWARD&&edge.Orientation()!=TopAbs_REVERSED)return unresolved("unsupported edge orientation");
            if(BRep_Tool::Degenerated(edge))return rejected("actual degenerate edge");
            double first,last;TopLoc_Location loc;const auto curve=BRep_Tool::Curve(edge,loc,first,last);
            const auto line=Handle(Geom_Line)::DownCast(curve);
            if(line.IsNull()||!loc.IsIdentity())return unresolved("unsupported carrier/location");
            if(!std::isfinite(first)||!std::isfinite(last))return unresolved("nonfinite parameter domain");
            if(first>=last)return rejected("actual empty line domain");
            const auto support=line->Lin();const auto base=point(support.Location());const auto axis=support.Direction();
            const auto eval=[&](double value){const Rational t=exact(value);return ExactPoint{base.x+t*exact(axis.X()),base.y+t*exact(axis.Y()),base.z+t*exact(axis.Z())};};
            auto a=eval(first),b=eval(last);if(edge.Orientation()==TopAbs_REVERSED)std::swap(a,b);
            if(a.z!=0||b.z!=0)return rejected("actual line is not planar in XY");
            if(a==b)return rejected("actual zero extent");
            TopoDS_Vertex start,end;TopExp::Vertices(edge,start,end,true);
            if(start.IsNull()||end.IsNull())return unresolved("missing actual endpoint");
            if(!(a==point(BRep_Tool::Pnt(start)))||!(b==point(BRep_Tool::Pnt(end))))return unresolved("carrier/vertex identity unproved");
            const double tol=std::max({BRep_Tool::Tolerance(edge),BRep_Tool::Tolerance(start),BRep_Tool::Tolerance(end)});
            if(!std::isfinite(tol)||tol<0||tol>1.e-6)return unresolved("outside recorded tolerance policy");
            geometry.push_back({a,b,exact(first),exact(last),exact(tol),vertices.Add(start),vertices.Add(end)});
        }
        Rational area=0;std::set<int> starts;
        for(size_t i=0;i<n;++i) {
            const auto& a=geometry[i];const auto& b=geometry[(i+1)%n];
            if(a.endVertex!=b.startVertex||!(a.end==b.start))return rejected("actual directed cycle not closed");
            if(!starts.insert(a.startVertex).second)return rejected("actual repeated vertex");
            area+=(a.start.x+a.end.x)*(a.end.y-a.start.y);
        }
        if(proof.edges!=geometry)return unresolved("endpoint/domain/vertex/guard witness differs from source");
        const size_t count=n*(n-1)/2;
        if(proof.edgeUses!=n||proof.requiredPairs!=count||proof.assessedPairs!=count||proof.pairs.size()!=count)return unresolved("incomplete pair census/counters");
        std::vector<const PairEvidence*> ledger(n*n,nullptr);
        for(const auto& pair:proof.pairs) {
            if(pair.first>=pair.second||pair.second>=n||ledger[pair.first*n+pair.second])return unresolved("invalid/swapped/duplicate pair identifiers");
            ledger[pair.first*n+pair.second]=&pair;
        }
        measurement.beginPairs();
        bool invalid=false,uncertain=false;
        for(size_t i=0;i<n;++i)for(size_t j=i+1;j<n;++j) {
            const auto* p=ledger[i*n+j];if(!p)return unresolved("missing required pair witness");
            const auto& a=geometry[i];const auto& b=geometry[j];
            const std::vector<Rational> determinants={orient(a.start,a.end,b.start),orient(a.start,a.end,b.end),orient(b.start,b.end,a.start),orient(b.start,b.end,a.end)};
            if(p->determinants!=determinants)return unresolved("pair determinant witness mismatch");
            const auto decision=relation(a,b,j==i+1||(i==0&&j==n-1));const auto status=decision.state;++answer.pairCalculations;
            if(p->outcome!=status)return unresolved("false pair classification");
            // Relation text is part of the evidence: cannot disguise a shared
            // endpoint as disjoint or an overlap as an endpoint-only contact.
            if(p->reason!=decision.reason)return unresolved("incorrect intersection/endpoint/overlap witness kind");
            invalid|=status==Check::Fail;uncertain|=status==Check::Uncertain;
            if(status!=Check::Uncertain)++answer.verifiedPairs;
        }
        const Check intersection=invalid?Check::Fail:uncertain?Check::Uncertain:Check::Pass;
        const Outcome computed=invalid?Outcome::Rejected:uncertain?Outcome::Unresolved:area<=0?Outcome::Rejected:Outcome::Certified;
        const Check orientation=invalid||uncertain?Check::NotChecked:area<=0?Check::Fail:Check::Pass;
        if(proof.planarity!=Check::Pass||proof.regularity!=Check::Pass||proof.closure!=Check::Pass
          ||proof.intersection!=intersection||proof.orientation!=orientation
          ||proof.interior!=(computed==Outcome::Certified?Check::Pass:Check::NotChecked)
          ||proof.signedTwiceArea!=area||proof.verifiedPairs!=answer.verifiedPairs)return unresolved("summary/orientation/interior witness mismatch");
        answer.outcome=computed;answer.evidenceConsistent=proof.outcome==computed;
        answer.reason=invalid?"independently established invalid intersections":uncertain?"independently established unresolved pair margins":area<=0?"independently established wrong orientation":"independently verified simple positive Jordan cycle";
        // Ignore the producer status in deriving computed. A contradictory
        // status cannot turn a complete negative/uncertain proof into Certified.
        if(!answer.evidenceConsistent&&computed==Outcome::Certified)return unresolved("producer status contradicts complete geometric evidence");
        return answer;
    }catch(const Standard_Failure&){return unresolved("consumer OCCT extraction failed");}
     catch(const std::exception&){return unresolved("consumer exact extraction/arithmetic failed");}
}
}
