// Read-only F2.4 bridge. No production modeling or acceptance dependencies.
#include "SectionCapCorrespondence.h"
#include "SectionExact.h"
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Face.hxx>
#include <BRepTopAdaptor_FClass2d.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <Geom_Plane.hxx>
#include <Geom_Line.hxx>
#include <Geom2d_Line.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <chrono>
#include <algorithm>
#include <array>
#include <set>
#include <sstream>
namespace SectionExperiment {
namespace {
using Arithmetic::exact;
using Vec=std::array<Rational,3>;
Vec value(const gp_XYZ& p){return {exact(p.X()),exact(p.Y()),exact(p.Z())};}
Vec plus(Vec a,const Vec& b){for(int i=0;i<3;++i)a[i]+=b[i];return a;}
Vec times(Vec a,const Rational& s){for(auto& v:a)v*=s;return a;}
Vec cross(const Vec& a,const Vec& b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
struct Refusal {Outcome outcome;const char* reason;};
void need(bool b,const char* reason,Outcome o=Outcome::Unresolved){if(!b)throw Refusal{o,reason};}
void rigid(const gp_Trsf& t) {
    // Exact signed-permutation linear part, not approximate orthogonality.
    std::set<int> columns;
    for(int i=1;i<=3;++i){int count=0;for(int j=1;j<=3;++j){const double x=t.Value(i,j);need(x==0||x==1||x==-1,"unproved floating rigid transform (signed-permutation subset only)");if(x!=0){++count;need(columns.insert(j).second,"ambiguous rigid axis");}}need(count==1,"invalid rigid row");}
    Vec a,b,c;for(int i=0;i<3;++i){a[i]=exact(t.Value(i+1,1));b[i]=exact(t.Value(i+1,2));c[i]=exact(t.Value(i+1,3));}
    need(cross(a,b)==c,"reflection unsupported");
}
Vec map(const gp_Trsf& t,const Vec& p,bool direction=false){rigid(t);Vec r{};for(int i=0;i<3;++i){r[i]=direction?0:exact(t.Value(i+1,4));for(int j=0;j<3;++j)r[i]+=exact(t.Value(i+1,j+1))*p[j];}return r;}
Vec vertex(const TopoDS_Vertex& v){auto local=v;local.Location(TopLoc_Location());return map(v.Location().Transformation(),value(BRep_Tool::Pnt(local).XYZ()));}
double binary(const Rational& x){const double d=x.numerator().convert_to<double>()/x.denominator().convert_to<double>();need(std::isfinite(d)&&exact(d)==x,"local coordinate conversion is not exact binary64");return d;}
gp_Pnt localPoint(const Vec& p){return gp_Pnt(binary(p[0]),binary(p[1]),binary(p[2]));}
}
CapInput::CapInput(const TopoDS_Shape& source,const TopoDS_Face& face,bool allowReversed):allowReversed_(allowReversed) {
    size_t index=0;for(TopExp_Explorer f(source,TopAbs_FACE);f.More();f.Next(),++index)if(f.Current().IsEqual(face)){member_=true;break;}
    BRepBuilderAPI_Copy copy(source,true,false);source_=copy.Shape();
    if(member_){face_=TopoDS::Face(copy.ModifiedShape(face));face_.Orientation(face.Orientation());}
    std::ostringstream out;BRepTools::Write(source_,out);out<<"cap-index="<<index;binding_=out.str();
}
CapExtraction extractCap(const CapInput& input) {
    CapExtraction out;out.claim.context=input.context_;out.claim.binding=input.binding_;
    try {
        need(input.member_,"selected oriented Face is not in the source",Outcome::Rejected);
        need(input.face_.Orientation()==TopAbs_FORWARD||(input.allowReversed_&&input.face_.Orientation()==TopAbs_REVERSED),"initial bridge supports forward carrier-oriented caps only");
        const bool reversed=input.face_.Orientation()==TopAbs_REVERSED;
        need(BRepCheck_Analyzer(input.source_).IsValid(),"source B-Rep is invalid",Outcome::Rejected);
        TopLoc_Location location;const auto surface=BRep_Tool::Surface(input.face_,location);
        const auto plane=Handle(Geom_Plane)::DownCast(surface);need(!plane.IsNull(),"cap is not an exact plane");
        const auto placement=location.Transformation();rigid(placement);
        const Vec origin=map(placement,value(plane->Location().XYZ()));
        const Vec u=map(placement,value(plane->Position().XDirection().XYZ()),true);
        const Vec v=times(map(placement,value(plane->Position().YDirection().XYZ()),true),reversed?-1:1);
        const Vec n=times(map(placement,value(plane->Position().Direction().XYZ()),true),reversed?-1:1);
        need(cross(u,v)==n,"carrier frame orientation not exact");
        // Restrict the carrier axes too, avoiding normalization/rotation proofs.
        for(const auto& axis:{u,v,n}){int unit=0;for(const auto& x:axis){need(x==0||x==1||x==-1,"unproved carrier-axis arithmetic");if(x!=0)++unit;}need(unit==1,"carrier axis is not an exact unit coordinate axis");}
        for(const auto& a:{origin,u,v,n})out.claim.frame.insert(out.claim.frame.end(),a.begin(),a.end());
        std::vector<TopoDS_Wire> wires;for(TopExp_Explorer w(input.face_,TopAbs_WIRE);w.More();w.Next())wires.push_back(TopoDS::Wire(w.Current()));
        need(wires.size()==1,"cap must have exactly one wire (no inner wires)");
        need(wires.front().IsSame(BRepTools::OuterWire(input.face_)),"wire is not the cap outer wire");
        std::vector<TopoDS_Edge> census,uses;
        for(TopExp_Explorer e(wires.front(),TopAbs_EDGE);e.More();e.Next())census.push_back(TopoDS::Edge(e.Current()));
        for(BRepTools_WireExplorer it(wires.front(),input.face_);it.More();it.Next())uses.push_back(it.Current());
        need(uses.size()>=3&&uses.size()<=64&&uses.size()==census.size(),"cap traversal does not exhaust bounded edge census");
        TopTools_IndexedMapOfShape faceIds,vertexIds;TopExp::MapShapes(input.source_,TopAbs_FACE,faceIds);
        out.faceIndex=faceIds.FindIndex(input.face_);out.edgeCount=uses.size();
        TopTools_IndexedDataMapOfShapeListOfShape adjacency;TopExp::MapShapesAndAncestors(input.source_,TopAbs_EDGE,TopAbs_FACE,adjacency);
        BRep_Builder builder;TopoDS_Wire normalized;builder.MakeWire(normalized);std::vector<TopoDS_Vertex> localVertices;std::vector<TopoDS_Edge> localUses;
        std::vector<bool> seen(census.size(),false);
        for(size_t i=0;i<uses.size();++i) {
            const auto edge=uses[i];size_t k=0;while(k<census.size()&&(seen[k]||!edge.IsEqual(census[k])))++k;
            need(k<census.size(),"cap has duplicate/omitted oriented uses");seen[k]=true;
            need(edge.Orientation()==TopAbs_FORWARD||edge.Orientation()==TopAbs_REVERSED,"ambiguous cap edge orientation");
            need(!BRep_Tool::Degenerated(edge),"degenerate cap edge",Outcome::Rejected);
            const auto& owners=adjacency.FindFromKey(edge);need(owners.Extent()==2,"cap edge must have two source Face owners");
            size_t other=0;bool found=false;
            for(const auto& owner:owners){found|=owner.IsSame(input.face_);if(!owner.IsSame(input.face_)){other=faceIds.FindIndex(owner);TopLoc_Location l;need(!Handle(Geom_Plane)::DownCast(BRep_Tool::Surface(TopoDS::Face(owner),l)).IsNull(),"nonplanar adjacent Face outside initial prism scope");}}
            need(found&&other!=0,"edge-to-Face adjacency does not bind the cap");out.adjacentFaces.emplace_back(out.faceIndex,other);
            double first,last,pfirst,plast;TopLoc_Location eloc;
            const auto line=Handle(Geom_Line)::DownCast(BRep_Tool::Curve(edge,eloc,first,last));
            const auto pc=Handle(Geom2d_Line)::DownCast(BRep_Tool::CurveOnSurface(edge,input.face_,pfirst,plast));
            need(!line.IsNull()&&!pc.IsNull(),"only exact line carriers and line pcurves are supported");
            need(std::isfinite(first)&&std::isfinite(last)&&first<last&&exact(first)==exact(pfirst)&&exact(last)==exact(plast),"3D/pcurve trim domains differ or are invalid");
            const Vec a2={exact(pc->Location().X()),exact(pc->Location().Y())*(reversed?-1:1),0};
            const Vec d2={exact(pc->Direction().X()),exact(pc->Direction().Y())*(reversed?-1:1),0};
            need((d2[0]==0&&(d2[1]==1||d2[1]==-1))||(d2[1]==0&&(d2[0]==1||d2[0]==-1)),"non-axis local line normalization not proved");
            const auto lift=[&](const Vec& q){return plus(origin,plus(times(u,q[0]),times(v,q[1])));};
            const auto worldOrigin=map(eloc.Transformation(),value(line->Lin().Location().XYZ()));
            const auto worldDirection=map(eloc.Transformation(),value(line->Lin().Direction().XYZ()),true);
            need(lift(a2)==worldOrigin&&plus(times(u,d2[0]),times(v,d2[1]))==worldDirection,"full pcurve/3D carrier coefficient identity failed");
            Vec a=plus(a2,times(d2,exact(first))),b=plus(a2,times(d2,exact(last)));
            if(edge.Orientation()==TopAbs_REVERSED)std::swap(a,b);
            TopoDS_Vertex va,vb;TopExp::Vertices(edge,va,vb,true);need(!va.IsNull()&&!vb.IsNull(),"missing cap vertex");
            need(lift(a)==vertex(va)&&lift(b)==vertex(vb),"ambiguous exact carrier/vertex endpoint identity");
            const int ia=vertexIds.Add(va),ib=vertexIds.Add(vb);
            const double ta=BRep_Tool::Tolerance(va),tb=BRep_Tool::Tolerance(vb),te=BRep_Tool::Tolerance(edge),tf=BRep_Tool::Tolerance(input.face_);
            for(double t:{ta,tb,te,tf})need(std::isfinite(t)&&t>=0&&t<=1.e-6,"topology tolerance outside existing ceiling");
            const double guard=std::max({ta,tb,te});
            out.actualEdges.push_back({{a[0],a[1],0},{b[0],b[1],0},exact(first),exact(last),exact(guard),ia,ib});
            const auto localVertex=[&](int id,const Vec& p,double t){
                if(size_t(id)>localVertices.size()){need(size_t(id)==localVertices.size()+1,"vertex numbering gap");auto vtx=BRepBuilderAPI_MakeVertex(localPoint(p)).Vertex();builder.UpdateVertex(vtx,t);localVertices.push_back(vtx);}
                return localVertices.at(id-1);
            };
            auto la=localVertex(ia,a,ta),lb=localVertex(ib,b,tb);
            if(edge.Orientation()==TopAbs_REVERSED)std::swap(la,lb);
            Handle(Geom_Line) c=new Geom_Line(localPoint(a2),gp_Dir(binary(d2[0]),binary(d2[1]),0));
            auto local=BRepBuilderAPI_MakeEdge(c,la,lb,first,last).Edge();local.Orientation(edge.Orientation());builder.UpdateEdge(local,te);
            builder.Add(normalized,local);localUses.push_back(local);out.claim.order.push_back(i);
        }
        // Engineering checks for actual planar Face trim semantics, in addition
        // to the exact coefficient/Jordan boundary proof. Not a new material proof.
        const auto trimFace=TopoDS::Face(input.face_.Oriented(TopAbs_FORWARD));
        BRepCheck_Face check(trimFace);
        need(check.IntersectWires()==BRepCheck_NoError&&check.ClassifyWires()==BRepCheck_NoError&&check.OrientationOfWires()==BRepCheck_NoError,"OCCT Face trim classification/orientation failed");
        need(BRepTopAdaptor_FClass2d(trimFace,BRep_Tool::Tolerance(input.face_)).PerformInfinitePoint()==TopAbs_OUT,"Face bounded-interior classification uncertain");
        out.claim.section=std::make_shared<const Input>(normalized,localUses,gp_Pln(gp_Pnt(),gp_Dir(0,0,1)));
        out.outcome=Outcome::Certified;out.reason="extracted exact linear cap correspondence; section proof still required";return out;
    }catch(const Refusal& r){out.outcome=r.outcome;out.reason=r.reason;return out;}
     catch(const Standard_Failure&){out.reason="OCCT cap extraction failed";return out;}
     catch(const std::exception&){out.reason="cap extraction arithmetic/domain failed";return out;}
}
CapConsumption consumeCap(const CapInput& input,const CapClaim& claim) {
    CapConsumption r;const auto start=std::chrono::steady_clock::now();
    const auto fail=[&](const char* reason){r.reason=reason;r.correspondenceMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()-r.sectionConsumptionMs;return r;};
    if(claim.context!=input.context_||claim.binding!=input.binding_||!claim.section)return fail("cap/source context binding mismatch");
    const auto t=std::chrono::steady_clock::now();const auto section=consumeEvidence(*claim.section,claim.proof);
    r.sectionConsumptionMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();
    if(section.outcome!=Outcome::Certified||!section.evidenceConsistent)return fail("section lacks complete independent certification");
    const auto actual=extractCap(input);
    if(actual.outcome!=Outcome::Certified)return fail("actual cap no longer has supported extraction evidence");
    if(claim.frame!=actual.claim.frame||claim.order.size()!=actual.edgeCount||claim.proof.edges.size()!=actual.edgeCount)return fail("cap frame/boundary census mismatch");
    std::set<size_t> mapped;
    for(size_t i=0;i<claim.order.size();++i){const size_t j=claim.order[i];if(j>=actual.edgeCount||!mapped.insert(j).second||claim.proof.edges[i]!=actual.actualEdges[j])return fail("cap edge/trim/vertex map is incomplete or inconsistent");++r.matchedEdges;}
    r.outcome=Outcome::Certified;return fail("complete independently certified boundary matches actual cap and bounded Face trim policy");
}
}
