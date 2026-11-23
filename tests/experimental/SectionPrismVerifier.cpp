#include "SectionPrismVerifier.h"
#include "SectionExact.h"
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <algorithm>
#include <chrono>
#include <set>
#include <sstream>
namespace SectionExperiment {
namespace {
using Vec=std::array<Rational,3>;using Clock=std::chrono::steady_clock;
struct Refusal {Outcome outcome;std::string reason;};
void need(bool b,const std::string& why,Outcome o=Outcome::Unresolved){if(!b)throw Refusal{o,why};}
double elapsed(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
Vec add(Vec a,const Vec& b){for(int i=0;i<3;++i)a[i]+=b[i];return a;}
Vec scale(Vec a,const Rational& s){for(auto& x:a)x*=s;return a;}
Vec sub(const Vec& a,const Vec& b){return add(a,scale(b,-1));}
Rational dot(const Vec& a,const Vec& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
Vec cross(const Vec& a,const Vec& b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
Vec frame(const CapClaim& c,int i){need(c.frame.size()==12,"missing Face frame");return {c.frame[i*3],c.frame[i*3+1],c.frame[i*3+2]};}
Vec lift(const CapClaim& c,const ExactPoint& p){return add(frame(c,0),add(scale(frame(c,1),p.x),scale(frame(c,2),p.y)));}
Vec vertex(const TopoDS_Vertex& v) {
    auto local=v;local.Location(TopLoc_Location());const auto p=BRep_Tool::Pnt(local);const auto t=v.Location().Transformation();
    const Vec q={Arithmetic::exact(p.X()),Arithmetic::exact(p.Y()),Arithmetic::exact(p.Z())};Vec r{};
    for(int i=0;i<3;++i){r[i]=Arithmetic::exact(t.Value(i+1,4));for(int j=0;j<3;++j)r[i]+=Arithmetic::exact(t.Value(i+1,j+1))*q[j];}return r;
}
struct Use {size_t edge,va,vb;Vec a,b;};
struct Topology {
    TopTools_IndexedMapOfShape faces,edges,vertices;
    std::vector<std::vector<Use>> uses;
    std::vector<std::vector<size_t>> owners;
    size_t useCount=0;
};
void sourceGate(const TopoDS_Shape& source) {
    need(source.ShapeType()==TopAbs_SOLID,"source must be exactly one solid",Outcome::Rejected);
    size_t shells=0;for(TopoDS_Iterator it(source);it.More();it.Next()){need(it.Value().ShapeType()==TopAbs_SHELL,"loose source topology",Outcome::Rejected);++shells;}
    need(shells==1,"additional/disconnected shell",Outcome::Rejected);
    const auto shell=TopExp_Explorer(source,TopAbs_SHELL).Current();need(BRep_Tool::IsClosed(shell),"source shell is not closed",Outcome::Rejected);
    size_t occurrences=0;for(TopExp_Explorer it(source,TopAbs_FACE);it.More();it.Next())++occurrences;
    TopTools_IndexedMapOfShape unique;TopExp::MapShapes(source,TopAbs_FACE,unique);
    need(occurrences==size_t(unique.Extent()),"duplicated source Face occurrence",Outcome::Rejected);
    need(occurrences>=5&&occurrences<=66,"bounded polygon-prism face budget");
    need(BRepCheck_Analyzer(source).IsValid(),"source B-Rep validity failed",Outcome::Rejected);
}
Topology topology(const TopoDS_Shape& source,const std::vector<CapClaim>& claims) {
    Topology t;TopExp::MapShapes(source,TopAbs_FACE,t.faces);TopExp::MapShapes(source,TopAbs_EDGE,t.edges);TopExp::MapShapes(source,TopAbs_VERTEX,t.vertices);
    need(claims.size()==size_t(t.faces.Extent()),"missing or additional Face claims");
    t.uses.resize(claims.size());t.owners.resize(t.edges.Extent());
    std::set<size_t> ownedVertices;
    for(size_t i=0;i<claims.size();++i) {
        const auto face=TopoDS::Face(t.faces(i+1));std::vector<TopoDS_Wire> wires;
        for(TopExp_Explorer w(face,TopAbs_WIRE);w.More();w.Next())wires.push_back(TopoDS::Wire(w.Current()));
        need(wires.size()==1,"Face has missing/additional wires");
        size_t j=0;
        for(BRepTools_WireExplorer it(wires.front(),face);it.More();it.Next(),++j) {
            need(j<claims[i].proof.edges.size(),"actual Face edge omitted");const auto edge=it.Current();
            TopoDS_Vertex a,b;TopExp::Vertices(edge,a,b,true);need(!a.IsNull()&&!b.IsNull(),"missing actual source vertex");
            Use u{size_t(t.edges.FindIndex(edge)),size_t(t.vertices.FindIndex(a)),size_t(t.vertices.FindIndex(b)),vertex(a),vertex(b)};
            const auto& proof=claims[i].proof.edges[j];
            need(u.a==lift(claims[i],proof.start)&&u.b==lift(claims[i],proof.end),"Face proof does not bind actual global vertices");
            need(u.edge&&u.va&&u.vb,"unowned edge or vertex");
            t.uses[i].push_back(u);t.owners[u.edge-1].push_back(i);ownedVertices.insert(u.va);ownedVertices.insert(u.vb);++t.useCount;
        }
        need(j==claims[i].proof.edges.size(),"additional Face proof edges");
    }
    need(ownedVertices.size()==size_t(t.vertices.Extent()),"loose source vertices",Outcome::Rejected);
    for(const auto& owners:t.owners)need(owners.size()==2&&owners[0]!=owners[1],"edge must have exactly two distinct Face owners",Outcome::Rejected);
    for(size_t e=0;e<t.owners.size();++e) {
        const auto find=[&](size_t f)->Use {for(const auto& u:t.uses[f])if(u.edge==e+1)return u;throw Refusal{Outcome::Rejected,"missing edge use"};};
        const auto a=find(t.owners[e][0]),b=find(t.owners[e][1]);
        need(a.va==b.vb&&a.vb==b.va,"adjacent Face edge-use orientations disagree",Outcome::Rejected);
    }
    // Vertex-link connectivity complements edge ownership; no disconnected fan.
    for(size_t v=1;v<=size_t(t.vertices.Extent());++v) {
        std::set<size_t> incident;
        for(size_t f=0;f<t.uses.size();++f)for(const auto& u:t.uses[f])if(u.va==v||u.vb==v)incident.insert(f);
        need(!incident.empty(),"unowned vertex");std::set<size_t> reached{*incident.begin()};bool changed=true;
        while(changed){changed=false;for(size_t e=0;e<t.owners.size();++e){const auto& owners=t.owners[e];bool touches=false;for(const auto& u:t.uses[owners[0]])if(u.edge==e+1&&(u.va==v||u.vb==v))touches=true;if(touches&&(reached.contains(owners[0])||reached.contains(owners[1]))){changed|=reached.insert(owners[0]).second;changed|=reached.insert(owners[1]).second;}}}
        need(reached==incident,"nonmanifold disconnected vertex fan",Outcome::Rejected);
    }
    return t;
}
size_t opposite(const std::vector<CapClaim>& faces,size_t top) {
    const Vec d=frame(faces.at(top),3);size_t bottom=faces.size();
    for(size_t i=0;i<faces.size();++i)if(i!=top){const auto n=frame(faces[i],3);if(n==scale(d,-1)){need(bottom==faces.size(),"more than one opposite cap");bottom=i;}else need(dot(n,d)==0,"unexpected nonparallel/transition Face");}
    need(bottom<faces.size(),"opposite cap not established");return bottom;
}
size_t other(const Topology& t,size_t e,size_t f){const auto& o=t.owners.at(e-1);need(o[0]==f||o[1]==f,"edge owner inconsistent");return o[0]==f?o[1]:o[0];}
}
PrismInput::PrismInput(const TopoDS_Shape& source,const TopoDS_Face& top,Rational height):requestedHeight_(height) {
    bool found=false;for(TopExp_Explorer f(source,TopAbs_FACE);f.More();f.Next())found|=f.Current().IsEqual(top);
    BRepBuilderAPI_Copy copy(source,true,false);source_=copy.Shape();TopTools_IndexedMapOfShape faces;TopExp::MapShapes(source_,TopAbs_FACE,faces);
    if(found)top_=faces.FindIndex(copy.ModifiedShape(top));
    for(int i=1;i<=faces.Extent();++i)faceInputs_.push_back(std::make_shared<const CapInput>(source_,TopoDS::Face(faces(i)),true));
    std::ostringstream out;BRepTools::Write(source_,out);out<<"top="<<top_<<"height="<<requestedHeight_;binding_=out.str();
}
PrismEvidence producePrism(const PrismInput& input) {
    PrismEvidence e;e.context=input.context_;e.binding=input.binding_;
    try {
        sourceGate(input.source_);need(input.top_>0,"selected top is not an oriented source member",Outcome::Rejected);e.top=input.top_-1;
        std::vector<double> extracted,certified;
        for(const auto& face:input.faceInputs_) {
            const auto t=Clock::now();auto x=extractCap(*face);const double ms=elapsed(t);
            need(x.outcome==Outcome::Certified,"Face extraction: "+x.reason,x.outcome);
            e.faces.push_back(std::move(x.claim));extracted.push_back(ms);
        }
        e.bottom=opposite(e.faces,e.top);
        for(size_t i=0;i<e.faces.size();++i) {
            const auto t=Clock::now();e.faces[i].proof=verify(*e.faces[i].section);const auto result=consumeCap(*input.faceInputs_[i],e.faces[i]);
            need(result.outcome==Outcome::Certified,"Face certification: "+result.reason,result.outcome);certified.push_back(elapsed(t));
            if(i==e.top||i==e.bottom){e.times.capExtractionMs+=extracted[i];e.times.capCertificationMs+=certified[i];}
            else {e.times.wallExtractionMs+=extracted[i];e.times.wallCorrespondenceMs+=certified[i];}
        }
        const auto t=Clock::now();const auto graph=topology(input.source_,e.faces);const auto d=frame(e.faces[e.top],3);
        e.height=dot(sub(frame(e.faces[e.top],0),frame(e.faces[e.bottom],0)),d);
        const auto& top=graph.uses[e.top];const auto& bottom=graph.uses[e.bottom];
        for(const auto& a:top) {
            size_t index=bottom.size();for(size_t j=0;j<bottom.size();++j)if(bottom[j].a==sub(a.b,scale(d,e.height))&&bottom[j].b==sub(a.a,scale(d,e.height))){need(index==bottom.size(),"ambiguous opposite edge");index=j;}
            need(index<bottom.size(),"top/bottom boundary mismatch");e.bottomByTop.push_back(index);
            const auto wall=other(graph,a.edge,e.top);std::vector<size_t> lateral;
            for(const auto& u:graph.uses[wall])if(u.edge!=a.edge&&u.edge!=bottom[index].edge)lateral.push_back(u.edge);
            need(lateral.size()==2,"wall lacks exactly two lateral edges");std::sort(lateral.begin(),lateral.end());
            e.walls.push_back({wall,a.edge,bottom[index].edge,{lateral[0],lateral[1]}});
        }
        e.edges=graph.edges.Extent();e.vertices=graph.vertices.Extent();e.edgeUses=graph.useCount;e.times.wallCorrespondenceMs+=elapsed(t);
        e.proposalOutcome=Outcome::Certified;e.reason="boundary proposal collected; independent product consumption required";
    }catch(const Refusal& r){e.proposalOutcome=r.outcome;e.reason=r.reason;}
     catch(const Standard_Failure&){e.reason="OCCT proposal extraction failed";}
     catch(const std::exception&){e.reason="bounded prism proposal failed";}
    return e;
}
PrismVerdict consumePrism(const PrismInput& input,const PrismEvidence& e) {
    PrismVerdict r;const auto start=Clock::now();
    const auto finish=[&](Outcome o,const std::string& why){r.boundary=o;r.reason=why;r.verificationMs=elapsed(start);return r;};
    try {
        need(e.context==input.context_&&e.binding==input.binding_,"immutable product context binding mismatch");
        sourceGate(input.source_);need(input.top_>0&&e.top==input.top_-1,"selected top correspondence is wrong");
        need(e.faces.size()==input.faceInputs_.size(),"complete source Face inventory is missing");
        // Independent line consumers and actual Face/trim checks: do not trust
        // proposalOutcome or producer success flags.
        for(size_t i=0;i<e.faces.size();++i){const auto c=consumeCap(*input.faceInputs_[i],e.faces[i]);need(c.outcome==Outcome::Certified,"independent Face consumption: "+c.reason,c.outcome);}
        const auto graph=topology(input.source_,e.faces);const auto bottom=opposite(e.faces,e.top);need(e.bottom==bottom,"opposite cap correspondence is wrong");
        const auto d=frame(e.faces[e.top],3);const Rational h=dot(sub(frame(e.faces[e.top],0),frame(e.faces[bottom],0)),d);
        need(h>0&&h==input.requestedHeight_&&e.height==h,"positive cap height differs from requested certificate");
        const auto& top=graph.uses[e.top];const auto& lower=graph.uses[bottom];const size_t n=top.size();
        need(lower.size()==n&&e.bottomByTop.size()==n&&e.walls.size()==n&&e.faces.size()==n+2,"complete cap/wall census differs");
        std::set<size_t> bottomUsed,wallsUsed,laterals,topVertices,bottomVertices;
        for(size_t i=0;i<n;++i) {
            const auto& a=top[i];const size_t j=e.bottomByTop[i];need(j<n&&bottomUsed.insert(j).second,"duplicate/missing bottom edge correspondence");const auto& b=lower[j];
            need(b.a==sub(a.b,scale(d,h))&&b.b==sub(a.a,scale(d,h)),"top/bottom edges are not the same translated section");
            topVertices.insert(a.va);bottomVertices.insert(b.vb);
            const auto& w=e.walls[i];need(w.face<e.faces.size()&&w.face!=e.top&&w.face!=bottom&&wallsUsed.insert(w.face).second,"duplicate/unexpected/missing wall Face");
            need(w.topEdge==a.edge&&w.bottomEdge==b.edge&&other(graph,a.edge,e.top)==w.face&&other(graph,b.edge,bottom)==w.face,"sidewall connects incorrect cap edges");
            const auto& uses=graph.uses[w.face];need(uses.size()==4,"sidewall is not one complete quadrilateral");
            std::set<Vec> actualCorners,expectedCorners={a.a,a.b,b.a,b.b};std::vector<size_t> actualLateral;
            for(const auto& u:uses){actualCorners.insert(u.a);actualCorners.insert(u.b);if(u.edge!=a.edge&&u.edge!=b.edge){actualLateral.push_back(u.edge);need((u.a==a.a&&u.b==b.b)||(u.b==a.a&&u.a==b.b)||(u.a==a.b&&u.b==b.a)||(u.b==a.b&&u.a==b.a),"incorrect longitudinal wall geometry/connection");laterals.insert(u.edge);}}
            std::sort(actualLateral.begin(),actualLateral.end());need(actualLateral.size()==2&&w.lateralEdges==std::array<size_t,2>{actualLateral[0],actualLateral[1]}&&actualCorners==expectedCorners,"wall trim/corner/lateral accounting differs");
            const auto outward=cross(sub(a.b,a.a),d),normal=frame(e.faces[w.face],3);
            need(cross(outward,normal)==Vec{}&&dot(outward,normal)>0,"sidewall material-side orientation is reversed");
        }
        need(wallsUsed.size()==n&&laterals.size()==n&&topVertices.size()==n&&bottomVertices.size()==n,"product vertex/lateral coverage incomplete");
        need(size_t(graph.edges.Extent())==3*n&&size_t(graph.vertices.Extent())==2*n&&graph.useCount==6*n,"unaccounted topology outside product boundary");
        need(e.edges==3*n&&e.vertices==2*n&&e.edgeUses==6*n,"producer edge-use/vertex evidence counters differ");
        r.caps=2;r.walls=n;r.edges=3*n;r.edgeUses=6*n;r.vertices=2*n;
        return finish(Outcome::Certified,"complete oriented nominal product boundary verified; numerical Face semantics remain; material certificate Unresolved");
    }catch(const Refusal& x){return finish(x.outcome,x.reason);}
     catch(const Standard_Failure&){return finish(Outcome::Unresolved,"OCCT independent boundary extraction failed");}
     catch(const std::exception&){return finish(Outcome::Unresolved,"independent boundary evidence incomplete");}
}
}
