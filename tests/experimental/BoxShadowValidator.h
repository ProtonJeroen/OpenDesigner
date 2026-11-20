#pragma once
// Shared F2.7 test implementation. Only development observers may include this;
// it is never an acceptance policy, preview receipt or modeling API.
#include "BRepCertification.h"
#include "BRepTiming.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <Geom2d_Line.hxx>
#include <Precision.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <sstream>
#include <stdexcept>
namespace ExperimentalBoxShadow {
namespace C = BRepCertification;
using Point = std::array<double,3>;
enum class Verdict { Certified, Rejected, Unresolved };
struct Result { Verdict verdict; std::string reason; };
inline Result yes() { return {Verdict::Certified,"all scoped obligations passed"}; }
inline Result no(std::string why) { return {Verdict::Rejected,std::move(why)}; }
inline Result unsure(std::string why) { return {Verdict::Unresolved,std::move(why)}; }
inline const char* name(Verdict v) { return v==Verdict::Certified?"Certified":v==Verdict::Rejected?"Rejected":"Unresolved"; }
inline void require(bool ok,const std::string& why) { if(!ok) throw std::runtime_error(why); }
inline std::string fingerprint(const TopoDS_Shape& s) { std::ostringstream out; BRepTools::Write(s,out); return out.str(); }
inline TopoDS_Shape copy(const TopoDS_Shape& s) { return BRepBuilderAPI_Copy(s,true,false).Shape(); }
inline Point point(const gp_Pnt& p) { return {p.X(),p.Y(),p.Z()}; }
inline TopTools_IndexedMapOfShape map(const TopoDS_Shape& s,TopAbs_ShapeEnum t) {
    TopTools_IndexedMapOfShape m;TopExp::MapShapes(s,t,m);return m;
}
struct Box { Point lo,hi; TopoDS_Face top,bottom; };
// Refusal-only floating-point sensitivity screen. This is not validated
// arithmetic and never supplies positive evidence. One representable coordinate
// step in either evaluated endpoint can move a residual across its tolerance.
// Such a boundary must not become a confident geometric rejection/acceptance.
inline double ulp(double value) {
    return std::abs(std::nextafter(value,INFINITY)-value);
}
inline bool unresolvedDistance(const gp_Pnt& a,const gp_Pnt& b,double tolerance) {
    const double distance=a.Distance(b);double resolution=ulp(distance);
    if(!std::isfinite(distance)||!std::isfinite(tolerance)) return true;
    for(int i=1;i<=3;++i) resolution+=ulp(a.Coord(i))+ulp(b.Coord(i));
    return !std::isfinite(resolution)||std::abs(distance-tolerance)<=resolution;
}
inline bool parameterBoundaryUncertain(const TopoDS_Shape& shape) {
    BRepTiming::Scope scope("Experimental.BoxParameterBoundary");
    for(TopExp_Explorer f(shape,TopAbs_FACE);f.More();f.Next()) {
        auto face=TopoDS::Face(f.Current());BRepAdaptor_Surface surface(face);
        if(surface.GetType()!=GeomAbs_Plane||!face.Location().IsIdentity()) continue;
        for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()) {
            auto edge=TopoDS::Edge(it.Current());BRepAdaptor_Curve curve(edge);
            if(curve.GetType()!=GeomAbs_Line||!edge.Location().IsIdentity()) continue;
            double first,last;auto pc=BRep_Tool::CurveOnSurface(edge,face,first,last);
            if(pc.IsNull()||Handle(Geom2d_Line)::DownCast(pc).IsNull()) continue;
            // A demonstrably inconsistent domain is a separate conclusive
            // representation error, not uncertainty about a distance boundary.
            if(first!=curve.FirstParameter()||last!=curve.LastParameter()) continue;
            const double edgeTolerance=std::max(BRep_Tool::Tolerance(edge),1.e-7);
            TopoDS_Vertex a,b;auto forward=edge;forward.Orientation(TopAbs_FORWARD);TopExp::Vertices(forward,a,b,true);
            for(int end=0;end<2;++end) {
                const double t=end?last:first;const auto p=curve.Value(t);auto uv=pc->Value(t);
                if(unresolvedDistance(p,surface.Value(uv.X(),uv.Y()),edgeTolerance)) return true;
                const auto vertex=end?b:a;
                if(!vertex.IsNull()&&unresolvedDistance(p,BRep_Tool::Pnt(vertex),BRep_Tool::Tolerance(vertex))) return true;
            }
        }
    }
    return false;
}

// Intentionally strict stored-data contract: axis-aligned nominal coordinates,
// six unsplit rectangular Faces. Equivalent split/rounded representations refuse.
inline Result inspect(const TopoDS_Shape& s,Box& box) {
    if(s.IsNull()) return no("null shape");
    if(!C::withinTolerance(s,1.e-6)) return no("topology tolerance ceiling");
    // Establish conclusive edge ownership/fan errors before numerical ambiguity.
    if(!C::manifold(s)) return no("manifold edge ownership / vertex fan");
    if(parameterBoundaryUncertain(s)) return unsure("parameter/endpoint residual at floating-point tolerance boundary");
    double volume=0;
    if(!C::singleSolid(s,volume)) return no("single closed valid manifold solid / SI / volume");
    BRepClass3d_SolidClassifier classifier(s);classifier.PerformInfinitePoint(Precision::Confusion());
    if(classifier.State()!=TopAbs_OUT) return no("outward orientation");
    auto faces=map(s,TopAbs_FACE),edges=map(s,TopAbs_EDGE),vertices=map(s,TopAbs_VERTEX);
    if(faces.Extent()!=6||edges.Extent()!=12||vertices.Extent()!=8)
        return unsure("outside strict six-Face/twelve-Edge/eight-vertex Box contract: "+std::to_string(faces.Extent())+"/"+std::to_string(edges.Extent())+"/"+std::to_string(vertices.Extent()));
    box.lo={INFINITY,INFINITY,INFINITY};box.hi={-INFINITY,-INFINITY,-INFINITY};
    std::set<Point> actual;
    for(int i=1;i<=vertices.Extent();++i) {
        Point p=point(BRep_Tool::Pnt(TopoDS::Vertex(vertices(i))));
        for(int j=0;j<3;++j) { if(!std::isfinite(p[j])) return no("nonfinite coordinate");
            box.lo[j]=std::min(box.lo[j],p[j]);box.hi[j]=std::max(box.hi[j],p[j]); }
        actual.insert(p);
    }
    std::set<Point> expected;
    for(int mask=0;mask<8;++mask) { Point p;for(int j=0;j<3;++j) p[j]=(mask&(1<<j))?box.hi[j]:box.lo[j];expected.insert(p); }
    for(int j=0;j<3;++j) if(box.hi[j]-box.lo[j]<=1.e-6) return unsure("unresolved small extent");
    if(actual!=expected) return unsure("non-axis-aligned corner grid outside strict Box contract");
    std::set<std::pair<Point,Point>> edgeGrid;
    for(int i=1;i<=edges.Extent();++i) {
        auto e=TopoDS::Edge(edges(i));BRepAdaptor_Curve c(e);
        if(c.GetType()!=GeomAbs_Line||!e.Location().IsIdentity()) return unsure("unsupported edge carrier/location");
        TopoDS_Vertex a,b;TopExp::Vertices(e,a,b);
        if(a.IsNull()||b.IsNull()) return no("missing edge endpoints");
        auto p=point(BRep_Tool::Pnt(a)),q=point(BRep_Tool::Pnt(b));int varying=0,axis=-1;
        for(int j=0;j<3;++j) if(p[j]!=q[j]) {++varying;axis=j;}
        if(varying!=1) return no("non-grid edge connection");
        auto dir=c.Line().Direction();for(int j=0;j<3;++j)
            if(std::abs(dir.Coord(j+1))!=(j==axis?1.:0.)) return unsure("nonexact axis line");
        const double first=c.FirstParameter(),last=c.LastParameter();
        if(!std::isfinite(first)||!std::isfinite(last)||first>=last) return no("invalid edge domain");
        auto x=point(c.Value(first)),y=point(c.Value(last));
        if(!((x==p&&y==q)||(x==q&&y==p))) return unsure("edge trim/endpoints not stored-data identical");
        if(q<p) std::swap(p,q);
        if(!edgeGrid.insert({p,q}).second) return no("duplicate geometric edge");
    }
    std::set<std::pair<int,int>> supports;
    for(int i=1;i<=faces.Extent();++i) {
        auto f=TopoDS::Face(faces(i));BRepAdaptor_Surface surface(f);
        if(surface.GetType()!=GeomAbs_Plane||!f.Location().IsIdentity()) return unsure("unsupported Face carrier/location");
        auto plane=surface.Plane();auto normal=plane.Axis().Direction();
        if(f.Orientation()==TopAbs_REVERSED) normal.Reverse();
        else if(f.Orientation()!=TopAbs_FORWARD) return no("unsupported Face use orientation");
        int axis=-1;for(int j=0;j<3;++j) if(std::abs(normal.Coord(j+1))==1.) axis=j;
        if(axis<0) return unsure("nonexact axis plane");
        for(int j=0;j<3;++j) if(j!=axis&&normal.Coord(j+1)!=0.) return unsure("nonexact plane normal");
        double level=plane.Location().Coord(axis+1);int side=level==box.lo[axis]?0:level==box.hi[axis]?1:-1;
        if(side<0||normal.Coord(axis+1)!=(side?1.:-1.)) return no("support or material-side mismatch");
        if(!supports.insert({axis,side}).second) return no("duplicate support");
        if(axis==2) (side?box.top:box.bottom)=f;
        int edgeUses=0;for(TopExp_Explorer uses(f,TopAbs_EDGE);uses.More();uses.Next()) ++edgeUses;
        if(edgeUses!=4||map(f,TopAbs_EDGE).Extent()!=4||map(f,TopAbs_VERTEX).Extent()!=4) return no("unexpected Face boundary topology");
        auto wires=map(f,TopAbs_WIRE);if(wires.Extent()!=1) return no("inner/unaccounted wire");
        auto wire=BRepTools::OuterWire(f);if(wire.IsNull()||!wire.IsSame(wires(1))) return no("outer wire mismatch");
        std::set<Point> wanted,found;for(const auto& p:expected) if(p[axis]==level) wanted.insert(p);
        std::set<int> used;TopoDS_Vertex start,previous;int count=0;
        for(BRepTools_WireExplorer it(wire,f);it.More();it.Next()) {
            auto e=it.Current();int id=edges.FindIndex(e);
            if(!id||!used.insert(id).second) return no("duplicate/unowned Face edge");
            TopoDS_Vertex a,b;TopExp::Vertices(e,a,b,true);
            if(count==0) start=a;else if(!previous.IsSame(a)) return no("broken oriented wire cycle");
            previous=b;++count;found.insert(point(BRep_Tool::Pnt(a)));found.insert(point(BRep_Tool::Pnt(b)));
            // An affine pcurve on an exact planar carrier is fixed by its two
            // endpoint images. Numerical endpoint agreement uses OCCT's existing
            // edge/vertex tolerances; this is not formal real-arithmetic equality.
            double first,last;auto pc=BRep_Tool::CurveOnSurface(e,f,first,last);
            if(pc.IsNull()||Handle(Geom2d_Line)::DownCast(pc).IsNull()) return unsure("nonlinear/missing pcurve");
            BRepAdaptor_Curve curve(e);if(first!=curve.FirstParameter()||last!=curve.LastParameter()) return no("pcurve domain mismatch");
            for(double t:{first,last}) { auto uv=pc->Value(t);double tol=std::max(BRep_Tool::Tolerance(e),1.e-7);
                if(surface.Value(uv.X(),uv.Y()).Distance(curve.Value(t))>tol) return no("inconsistent affine pcurve"); }
        }
        if(count!=4||used.size()!=4||!previous.IsSame(start)||found!=wanted) return no("incomplete rectangular Face domain");
    }
    if(supports.size()!=6) return no("incomplete six-support accounting");
    const double expectedVolume=(box.hi[0]-box.lo[0])*(box.hi[1]-box.lo[1])*(box.hi[2]-box.lo[2]);
    if(!C::areaMatches(expectedVolume,volume,1.e-8)) return no("independent Box volume");
    return yes();
}

// Source must have passed inspect in this same immutable synchronous evaluation.
inline Result candidate(const Box& source,const TopoDS_Shape& result,double delta) {
    if(!std::isfinite(delta)||source.hi[2]+delta<=source.lo[2]) return no("invalid final height");
    Box edited;auto r=inspect(result,edited);if(r.verdict!=Verdict::Certified)return r;
    auto hi=source.hi;hi[2]+=delta;
    if(source.lo!=edited.lo||hi!=edited.hi) return no("requested displacement / opposite cap / dimensions mismatch");
    return yes();
}
}
