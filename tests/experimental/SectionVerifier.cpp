#include "SectionVerifier.h"
#include "SectionExact.h"
#include "SectionTiming.h"
#include <BRepTools.hxx>
#include <sstream>
#include <iomanip>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRep_Tool.hxx>
#include <Geom_Line.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <bit>
#include <cstdint>
#include <limits>
#include <cmath>
#include <algorithm>
#include <stdexcept>
namespace SectionExperiment {
namespace {
using Arithmetic::exact;
struct Point {Rational x,y,z;bool operator==(const Point&) const=default;};
Point point(const gp_Pnt& p){return {exact(p.X()),exact(p.Y()),exact(p.Z())};}
Rational cross(const Point& a,const Point& b,const Point& c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
bool between(const Rational& x,const Rational& a,const Rational& b){return x>=std::min(a,b)&&x<=std::max(a,b);}
bool on(const Point& p,const Point& a,const Point& b){return cross(a,b,p)==0&&between(p.x,a.x,b.x)&&between(p.y,a.y,b.y);}
int sign(const Rational& x){return x>0?1:x<0?-1:0;}
struct Edge {Point a,b;int va,vb;Rational guard;};
PairEvidence pair(size_t i,size_t j,const std::vector<Edge>& edges) {
    const auto& a=edges[i];const auto& b=edges[j];
    PairEvidence p{i,j,Check::Pass,"exactly disjoint",{cross(a.a,a.b,b.a),cross(a.a,a.b,b.b),cross(b.a,b.b,a.a),cross(b.a,b.b,a.b)}};
    const bool adjacent=j==i+1||(i==0&&j+1==edges.size());
    bool overlap=false,hit=false;
    if(p.determinants[0]==0&&p.determinants[1]==0&&p.determinants[2]==0&&p.determinants[3]==0) {
        const bool x=a.a.x!=a.b.x;
        const Rational lo=std::max(std::min(x?a.a.x:a.a.y,x?a.b.x:a.b.y),std::min(x?b.a.x:b.a.y,x?b.b.x:b.b.y));
        const Rational hi=std::min(std::max(x?a.a.x:a.a.y,x?a.b.x:a.b.y),std::max(x?b.a.x:b.a.y,x?b.b.x:b.b.y));
        overlap=lo<hi;hit=lo<=hi;
    } else hit=(sign(p.determinants[0])*sign(p.determinants[1])<0&&sign(p.determinants[2])*sign(p.determinants[3])<0)
        ||on(a.a,b.a,b.b)||on(a.b,b.a,b.b)||on(b.a,a.a,a.b)||on(b.b,a.a,a.b);
    if(overlap) {p.outcome=Check::Fail;p.reason="positive-length collinear overlap";}
    else if(hit&&!adjacent){p.outcome=Check::Fail;p.reason="forbidden nonadjacent intersection/contact";}
    else if(hit) {p.reason="exact intended endpoint only (nonoverlap line pair)";}
    else if(adjacent) {p.outcome=Check::Fail;p.reason="adjacent endpoints do not meet";}
    else {
        // Test-only conservative policy, not a production tolerance or material tube.
        // Exact nominal disjointness is known; unresolved robustness near recorded
        // B-Rep tolerances is deliberately not labeled geometric invalidity.
        const Rational g=a.guard+b.guard;
        const bool separated=std::max(a.a.x,a.b.x)+g<std::min(b.a.x,b.b.x)||std::max(b.a.x,b.b.x)+g<std::min(a.a.x,a.b.x)
          ||std::max(a.a.y,a.b.y)+g<std::min(b.a.y,b.b.y)||std::max(b.a.y,b.b.y)+g<std::min(a.a.y,a.b.y);
        if(!separated){p.outcome=Check::Uncertain;p.reason="nominally disjoint; recorded-tolerance AABB separation unresolved";}
    }
    return p;
}
}
Input::Input(const TopoDS_Wire& w,const std::vector<TopoDS_Edge>& uses,const gp_Pln& plane):plane_(plane) {
    BRepBuilderAPI_Copy copy(w,true,false);wire_=TopoDS::Wire(copy.Shape());
    for(const auto& e:uses){auto mapped=TopoDS::Edge(copy.ModifiedShape(e));mapped.Orientation(e.Orientation());uses_.push_back(mapped);}
    // Full snapshot/ordered-use encoding, not a volume or lossy hash. The
    // private context additionally prevents cross-evaluation receipt transfer.
    std::ostringstream binding;BRepTools::Write(wire_,binding);
    for(const auto& e:uses_)BRepTools::Write(e,binding);
    binding<<std::hexfloat;
    const auto origin=plane_.Location();const auto normal=plane_.Axis().Direction();
    binding<<origin.X()<<','<<origin.Y()<<','<<origin.Z()<<','<<normal.X()<<','<<normal.Y()<<','<<normal.Z();
    binding_=binding.str();
}
Evidence verify(const Input& input,EvaluationTiming* timing) {
    EvaluationMeasurement measurement(timing);
    Evidence r;r.sourceContext=input.context_;r.sourceBinding=input.binding_;r.edgeUses=input.uses_.size();r.requiredPairs=r.edgeUses*(r.edgeUses?r.edgeUses-1:0)/2;
    const auto stop=[&](Outcome o,const std::string& why){
        r.outcome=o;r.reason=why;
        // Structural refusal never silently treats unchecked pairs as disjoint.
        if(r.edgeUses<=64&&r.pairs.empty())
            for(size_t i=0;i<r.edgeUses;++i)for(size_t j=i+1;j<r.edgeUses;++j)
                r.pairs.push_back({i,j,Check::NotChecked,"blocked by structural/scope refusal",{}});
        return r;
    };
    try {
        if(r.edgeUses<3||r.edgeUses>64)return stop(Outcome::Unresolved,"scope requires 3..64 finite line uses");
        const auto n=input.plane_.Axis().Direction();
        if(n.X()!=0||n.Y()!=0||n.Z()!=1||input.plane_.Location().Z()!=0)
            return stop(Outcome::Unresolved,"initial exact plane contract is global XY with +Z normal");
        TopTools_IndexedMapOfShape vertices;
        std::vector<TopoDS_Edge> inventory;
        for(TopExp_Explorer it(input.wire_,TopAbs_EDGE);it.More();it.Next())inventory.push_back(TopoDS::Edge(it.Current()));
        if(inventory.size()!=r.edgeUses){r.closure=Check::Fail;return stop(Outcome::Rejected,"edge-use census mismatch");}
        std::vector<bool> consumed(inventory.size(),false);std::vector<Edge> edges;
        for(const auto& use:input.uses_) {
            size_t k=0;while(k<inventory.size()&&(consumed[k]||!use.IsEqual(inventory[k])))++k;
            if(k==inventory.size()){r.closure=Check::Fail;return stop(Outcome::Rejected,"duplicate, missing or inconsistent oriented use");}consumed[k]=true;
            if(use.Orientation()!=TopAbs_FORWARD&&use.Orientation()!=TopAbs_REVERSED)return stop(Outcome::Unresolved,"unsupported use orientation");
            if(BRep_Tool::Degenerated(use)){r.regularity=Check::Fail;return stop(Outcome::Rejected,"degenerate edge");}
            double first,last;TopLoc_Location location;const auto c=BRep_Tool::Curve(use,location,first,last);
            const auto line=Handle(Geom_Line)::DownCast(c);
            if(line.IsNull()||!location.IsIdentity())return stop(Outcome::Unresolved,"only identity-located Geom_Line carriers supported");
            if(!std::isfinite(first)||!std::isfinite(last))return stop(Outcome::Unresolved,"nonfinite domain");
            if(first>=last){r.regularity=Check::Fail;return stop(Outcome::Rejected,"empty or inverted line domain");}
            const auto origin=point(line->Lin().Location());const auto d=line->Lin().Direction();
            const auto at=[&](double t){const auto q=exact(t);return Point{origin.x+q*exact(d.X()),origin.y+q*exact(d.Y()),origin.z+q*exact(d.Z())};};
            Point a=at(first),b=at(last);if(use.Orientation()==TopAbs_REVERSED)std::swap(a,b);
            if(a.z!=0||b.z!=0){r.planarity=Check::Fail;return stop(Outcome::Rejected,"line not in specified plane");}
            if(a==b){r.regularity=Check::Fail;return stop(Outcome::Rejected,"zero-length edge");}
            TopoDS_Vertex va,vb;TopExp::Vertices(use,va,vb,true);
            if(va.IsNull()||vb.IsNull())return stop(Outcome::Unresolved,"missing endpoint vertices");
            if(!(a==point(BRep_Tool::Pnt(va)))||!(b==point(BRep_Tool::Pnt(vb))))return stop(Outcome::Unresolved,"nominal carrier endpoints and vertices are not exactly identical");
            const double tolerance=std::max({BRep_Tool::Tolerance(use),BRep_Tool::Tolerance(va),BRep_Tool::Tolerance(vb)});
            if(!std::isfinite(tolerance)||tolerance<0||tolerance>1.e-6)return stop(Outcome::Unresolved,"outside recorded topology-tolerance policy");
            edges.push_back({a,b,vertices.Add(va),vertices.Add(vb),exact(tolerance)});
            r.edges.push_back({{a.x,a.y,a.z},{b.x,b.y,b.z},exact(first),exact(last),exact(tolerance),vertices.FindIndex(va),vertices.FindIndex(vb)});
        }
        r.planarity=Check::Pass;r.regularity=Check::Pass;
        std::vector<int> starts;
        for(size_t i=0;i<edges.size();++i) {
            const auto& a=edges[i];const auto& b=edges[(i+1)%edges.size()];
            if(a.vb!=b.va||!(a.b==b.a)){r.closure=Check::Fail;return stop(Outcome::Rejected,"ordered cycle has an open or reversed joint");}
            if(std::find(starts.begin(),starts.end(),a.va)!=starts.end()){r.closure=Check::Fail;return stop(Outcome::Rejected,"ambiguous repeated vertex");}starts.push_back(a.va);
            r.signedTwiceArea+=a.a.x*a.b.y-a.a.y*a.b.x;
        }
        r.closure=Check::Pass;
        measurement.beginPairs();
        bool invalid=false,uncertain=false;
        for(size_t i=0;i<edges.size();++i)for(size_t j=i+1;j<edges.size();++j) {
            auto p=pair(i,j,edges);++r.assessedPairs;
            if(p.outcome!=Check::Uncertain)++r.verifiedPairs;
            invalid|=p.outcome==Check::Fail;uncertain|=p.outcome==Check::Uncertain;r.pairs.push_back(std::move(p));
        }
        r.intersection=invalid?Check::Fail:uncertain?Check::Uncertain:Check::Pass;
        if(invalid)return stop(Outcome::Rejected,"exact forbidden contact or overlap; inspect pair witnesses");
        if(uncertain)return stop(Outcome::Unresolved,"pair robustness evidence incomplete; nominal disjointness alone not promoted");
        if(r.signedTwiceArea<=0){r.orientation=Check::Fail;return stop(Outcome::Rejected,"boundary orientation inconsistent with +Z plane");}
        r.orientation=Check::Pass;r.interior=Check::Pass;
        return stop(Outcome::Certified,"complete simple directed cycle; exact Jordan-boundary evidence, positive signed area");
    }catch(const Standard_Failure&){return stop(Outcome::Unresolved,"OCCT extraction failed");}
     catch(const std::exception&){return stop(Outcome::Unresolved,"unsupported/nonfinite extraction");}
}
bool checkEvidence(const Input& input,const Evidence& proof){return consumeEvidence(input,proof).evidenceConsistent;}
}
