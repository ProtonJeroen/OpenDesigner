#include "EdgeBlendFeatures.h"
#include "FilletGeometry.h"
#include "ChamferGeometry.h"
#include "BRepReconstruction.h"
#include "MoveFaceGeometry.h"
#include "FaceTransformGeometry.h"
#include <BRepAdaptor_Surface.hxx>
#include <gp_Lin.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <optional>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopoDS.hxx>
#include <Standard_Failure.hxx>
#include <QUuid>
#include <QJsonArray>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace EdgeBlendFeatures {
namespace {
constexpr double linear=1.e-6,angular=1.e-8;
bool planeEqual(const gp_Pln& a,const gp_Pln& b) {
    return a.Axis().Direction().IsParallel(b.Axis().Direction(),angular)&&a.Distance(b.Location())<linear;
}
bool member(const TopoDS_Shape& shape,const TopoDS_Face& face) {
    if(face.IsNull()) return false;
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) if(it.Current().IsSame(face)) return true;
    return false;
}
bool strictSharp(const TopoDS_Shape& shape) {
    double volume=0;
    if(!BRepReconstruction::validSolid(shape,volume)) return false;
    for(auto type:{TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX}) for(TopExp_Explorer it(shape,type);it.More();it.Next()) {
        const double t=type==TopAbs_FACE?BRep_Tool::Tolerance(TopoDS::Face(it.Current())):
            type==TopAbs_EDGE?BRep_Tool::Tolerance(TopoDS::Edge(it.Current())):BRep_Tool::Tolerance(TopoDS::Vertex(it.Current()));
        if(!std::isfinite(t)||t>linear) return false;
        if(type==TopAbs_FACE) {
            const auto surface=BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType();
            if(surface!=GeomAbs_Plane&&surface!=GeomAbs_Cylinder&&surface!=GeomAbs_Cone) return false;
        }
    }
    return true;
}
// Cone carriers are double cones: equal apex, axis line and |half-angle| is one surface.
bool coneEqual(const gp_Cone& a,const gp_Cone& b) {
    return a.Axis().Direction().IsParallel(b.Axis().Direction(),angular)&&a.Apex().Distance(b.Apex())<linear
        &&std::abs(std::abs(a.SemiAngle())-std::abs(b.SemiAngle()))<angular;
}
TopoDS_Face supportFace(const TopoDS_Shape& shape,const gp_Pln& plane) {
    TopoDS_Face match;
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        BRepAdaptor_Surface s(TopoDS::Face(it.Current()));
        if(s.GetType()==GeomAbs_Plane&&planeEqual(plane,s.Plane())) {
            if(!match.IsNull()) return {}; // Split or duplicate support: ambiguous.
            match=TopoDS::Face(it.Current());
        }
    }
    return match;
}
TopoDS_Face featureSupport(const TopoDS_Shape& shape,const Feature& f,int k) {
    if(f.cones[k]) {
        TopoDS_Face match;
        for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
            auto face=TopoDS::Face(it.Current());BRepAdaptor_Surface s(face);
            if(s.GetType()!=GeomAbs_Cone||!coneEqual(*f.cones[k],s.Cone())) continue;
            if(!match.IsNull()) return {};
            match=face;
        }
        return match;
    }
    if(!f.cylinders[k]) return supportFace(shape,f.supports[k]);
    TopoDS_Face match;const auto& c=*f.cylinders[k];
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        auto face=TopoDS::Face(it.Current());BRepAdaptor_Surface s(face);
        if(s.GetType()!=GeomAbs_Cylinder) continue;
        auto b=s.Cylinder();
        if(std::abs(c.Radius()-b.Radius())<linear&&c.Axis().Direction().IsParallel(b.Axis().Direction(),angular)
           &&gp_Lin(c.Axis()).Distance(b.Location())<linear) {
            if(!match.IsNull()) return {};
            match=face;
        }
    }
    return match;
}
TopoDS_Edge supportEdge(const TopoDS_Shape& shape,const Feature& f) {
    auto a=featureSupport(shape,f,0),b=featureSupport(shape,f,1);
    if(a.IsNull()||b.IsNull()) return {};
    TopoDS_Edge match;
    for(TopExp_Explorer x(a,TopAbs_EDGE);x.More();x.Next()) for(TopExp_Explorer y(b,TopAbs_EDGE);y.More();y.Next())
        if(x.Current().IsSame(y.Current())) {
            if(!match.IsNull()&&!match.IsSame(x.Current())) {
                BRepAdaptor_Curve a(match),b(TopoDS::Edge(x.Current()));
                if(!circular(f)||a.GetType()!=GeomAbs_Circle||b.GetType()!=GeomAbs_Circle
                   ||a.Circle().Location().Distance(b.Circle().Location())>linear
                   ||std::abs(a.Circle().Radius()-b.Circle().Radius())>linear) return {};
            }
            match=TopoDS::Edge(x.Current());
        }
    if(!match.IsNull()&&BRepAdaptor_Curve(match).GetType()!=(circular(f)?GeomAbs_Circle:GeomAbs_Line)) return {};
    return match;
}
std::vector<TopoDS_Face> neighbors(const TopoDS_Shape& shape,const TopoDS_Face& face) {
    TopTools_IndexedDataMapOfShapeListOfShape map;
    TopExp::MapShapesAndUniqueAncestors(shape,TopAbs_EDGE,TopAbs_FACE,map);
    std::vector<TopoDS_Face> result;
    for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()) for(const auto& other:map.FindFromKey(it.Current()))
        if(!other.IsSame(face)&&std::none_of(result.begin(),result.end(),[&](const auto& f){return f.IsSame(other);})) result.push_back(TopoDS::Face(other));
    return result;
}
bool touches(const TopoDS_Shape& a,const TopoDS_Shape& b) {
    for(TopExp_Explorer x(a,TopAbs_VERTEX);x.More();x.Next())
        for(TopExp_Explorer y(b,TopAbs_VERTEX);y.More();y.Next()) if(x.Current().IsSame(y.Current())) return true;
    return false;
}
bool transitionSupport(const TopoDS_Face& a,const TopoDS_Face& b) {
    BRepAdaptor_Surface x(a),y(b);
    if(x.GetType()==GeomAbs_Torus&&y.GetType()==GeomAbs_Torus) {
        auto p=x.Torus(),q=y.Torus();
        return p.Location().Distance(q.Location())<linear&&p.Axis().Direction().IsParallel(q.Axis().Direction(),angular)
            &&std::abs(p.MajorRadius()-q.MajorRadius())<linear&&std::abs(p.MinorRadius()-q.MinorRadius())<linear;
    }
    return BRepReconstruction::sameAnalyticSupport(a,b);
}
bool bind(State& state,const TopoDS_Shape& current,QString& error) {
    // Prototypes from replay are carrier geometry, never saved topology IDs.
    for(auto& feature:state.features) {
        auto prototypes=feature.faces;feature.faces.clear();
        for(TopExp_Explorer it(current,TopAbs_FACE);it.More();it.Next()) {
            auto f=TopoDS::Face(it.Current());
            if(std::any_of(prototypes.begin(),prototypes.end(),[&](const auto& p){return BRepAdaptor_Surface(p).GetType()==GeomAbs_Plane&&BRepAdaptor_Surface(f).GetType()==GeomAbs_Plane
                    ?planeEqual(BRepAdaptor_Surface(p).Plane(),BRepAdaptor_Surface(f).Plane()):transitionSupport(p,f);})) feature.faces.push_back(f);
        }
        if(feature.faces.size()!=1) {error=QStringLiteral("Transition is missing, split or ambiguous; its recipe was invalidated.");return false;}
    }
    for(const auto& feature:state.features) for(const auto& other:state.features) if(feature.id!=other.id)
        for(const auto& f:feature.faces) for(const auto& g:other.faces) if(touches(f,g)) {
            error=QStringLiteral("Interacting transition junctions are not supported. Use separated edges.");return false;
        }
    state.certified=current;return true;
}
bool rebuild(State& state,TopoDS_Shape& result,QString& error) {
    result.Nullify();error.clear();
    if(!strictSharp(state.sharp)) {error=QStringLiteral("The sharp supporting solid must be planar/cylindrical/conical, valid, and within the existing 1e-6 tolerance limit.");return false;}
    auto shape=state.sharp;
    for(auto& feature:state.features) {
        if(!std::isfinite(feature.size)||feature.size<=0) {error=QStringLiteral("Enter a finite positive transition dimension.");return false;}
        const auto edge=supportEdge(shape,feature);
        if(edge.IsNull()) {error=QStringLiteral("Transition support edge is missing, split, curved or consumed by another transition.");return false;}
        auto next=feature.kind==Kind::Fillet?FilletGeometry::apply(shape,edge,feature.size,error):ChamferGeometry::apply(shape,edge,feature.size,error);
        if(next.IsNull()) return false;
        feature.faces.clear();
        for(TopExp_Explorer it(next,TopAbs_FACE);it.More();it.Next()) {
            auto candidate=TopoDS::Face(it.Current());bool old=false;
            for(TopExp_Explorer prev(shape,TopAbs_FACE);prev.More();prev.Next())
                if(transitionSupport(candidate,TopoDS::Face(prev.Current()))) {old=true;break;}
            BRepAdaptor_Surface surface(candidate);
            // A circular Chamfer is a coaxial conical band; a straight one is planar.
            if(!old&&((feature.kind==Kind::Fillet&&(surface.GetType()==GeomAbs_Cylinder||surface.GetType()==GeomAbs_Torus))
                ||(feature.kind==Kind::Chamfer&&surface.GetType()==(circular(feature)?GeomAbs_Cone:GeomAbs_Plane)))) feature.faces.push_back(candidate);
        }
        if(feature.faces.size()!=1) {error=QStringLiteral("Only one unambiguous cylindrical/toroidal Fillet or planar/conical Chamfer face can be reconstructed.");return false;}
        shape=next;
    }
    if(!bind(state,shape,error)) return false;
    result=shape;return true;
}
bool recover(const TopoDS_Shape& source,State& state,QString& error) {
    // Conservative import path: six axis-aligned stock planes, plus separated
    // tangent cylinders / equal-distance planar transitions. Full material
    // equivalence with replay is mandatory; surface type alone proves nothing.
    std::array<std::vector<double>,3> bounds;
    State candidate;
    for(TopExp_Explorer it(source,TopAbs_FACE);it.More();it.Next()) {
        auto face=TopoDS::Face(it.Current());BRepAdaptor_Surface s(face);
        if(s.GetType()==GeomAbs_Plane) {
            auto plane=s.Plane();int axis=-1;
            for(int k=0;k<3;++k) if(std::abs(plane.Axis().Direction().Coord(k+1))>1-angular) axis=k;
            if(axis>=0) {auto& values=bounds[axis];const double value=plane.Location().Coord(axis+1);
                if(std::none_of(values.begin(),values.end(),[&](double a){return std::abs(a-value)<linear;})) values.push_back(value);
                continue;
            }
        } else if(s.GetType()!=GeomAbs_Cylinder) return false;
        Feature f;f.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        f.kind=s.GetType()==GeomAbs_Cylinder?Kind::Fillet:Kind::Chamfer;
        std::vector<TopoDS_Face> supports;
        for(const auto& n:neighbors(source,face)) {
            BRepAdaptor_Surface a(n);if(a.GetType()!=GeomAbs_Plane) return false;
            bool side=false;
            if(f.kind==Kind::Fillet) {
                const auto cylinder=s.Cylinder();
                side=std::abs(a.Plane().Axis().Direction().Dot(cylinder.Axis().Direction()))<angular
                    &&std::abs(a.Plane().Distance(cylinder.Location())-cylinder.Radius())<linear;
                f.size=cylinder.Radius();
            } else {
                side=std::abs(std::abs(a.Plane().Axis().Direction().Dot(s.Plane().Axis().Direction()))-std::sqrt(0.5))<angular;
            }
            if(side) supports.push_back(n);
        }
        if(supports.size()!=2) return false;
        for(int k=0;k<2;++k) f.supports[k]=BRepAdaptor_Surface(supports[k]).Plane();
        if(std::abs(f.supports[0].Axis().Direction().Dot(f.supports[1].Axis().Direction()))>angular) return false;
        if(f.kind==Kind::Chamfer) {
            std::array<double,2> size{};
            for(int k=0;k<2;++k) {
                bool found=false;
                for(TopExp_Explorer a(face,TopAbs_EDGE);a.More();a.Next()) for(TopExp_Explorer b(supports[k],TopAbs_EDGE);b.More();b.Next())
                    if(a.Current().IsSame(b.Current())) {
                        BRepAdaptor_Curve c(TopoDS::Edge(a.Current()));if(c.GetType()!=GeomAbs_Line) return false;
                        size[k]=f.supports[1-k].Distance(c.Value((c.FirstParameter()+c.LastParameter())/2));found=true;
                    }
                if(!found) return false;
            }
            if(size[0]<=0||std::abs(size[0]-size[1])>linear) return false;
            f.size=size[0];
        }
        f.faces={face};candidate.features.push_back(f);
    }
    if(candidate.features.empty()||candidate.features.size()>8) return false;
    double sourceVolume=0;
    if(!BRepReconstruction::validSolid(source,sourceVolume)) return false;
    for(auto& values:bounds) {if(values.size()!=2) return false;std::sort(values.begin(),values.end());}
    candidate.sharp=BRepPrimAPI_MakeBox(gp_Pnt(bounds[0][0],bounds[1][0],bounds[2][0]),gp_Pnt(bounds[0][1],bounds[1][1],bounds[2][1])).Shape();
    TopoDS_Shape replay;
    if(!rebuild(candidate,replay,error)||!BRepReconstruction::equivalent(source,replay)||!bind(candidate,source,error)) return false;
    state=std::move(candidate);return true;
}
// ---- Conical solids of revolution (v0.5.2) ---------------------------------
// Meridian frame: axial coordinate t and radius rho about the body cone axis.
struct Meridian {
    gp_Ax1 axis;
    double axial(const gp_Pnt& p) const {return gp_Vec(axis.Location(),p).Dot(gp_Vec(axis.Direction()));}
    // Signed radius of a coaxial cone at axial t; negative means the opposite nappe.
    double radius(const gp_Cone& c,double t) const {
        const auto p=axis.Location().Translated(gp_Vec(axis.Direction())*t);
        return c.RefRadius()+gp_Vec(c.Location(),p).Dot(gp_Vec(c.Axis().Direction()))*std::tan(c.SemiAngle());
    }
    bool coaxial(const gp_Ax1& other) const {
        return axis.Direction().IsParallel(other.Direction(),angular)&&gp_Lin(axis).Distance(other.Location())<linear;
    }
};
// One complete boundary circle between two faces: every shared edge is a coaxial
// circle of the same axial position and radius, together sweeping exactly 2*pi.
bool sharedCircle(const Meridian& m,const TopoDS_Face& a,const TopoDS_Face& b,double& t,double& rho) {
    double sweep=0;bool first=true;
    for(TopExp_Explorer x(a,TopAbs_EDGE);x.More();x.Next()) {
        bool shared=false;
        for(TopExp_Explorer y(b,TopAbs_EDGE);y.More()&&!shared;y.Next()) shared=x.Current().IsSame(y.Current());
        if(!shared) continue;
        const auto edge=TopoDS::Edge(x.Current());
        if(BRep_Tool::Degenerated(edge)) return false;
        BRepAdaptor_Curve curve(edge);if(curve.GetType()!=GeomAbs_Circle) return false;
        const auto circle=curve.Circle();
        if(!circle.Axis().Direction().IsParallel(m.axis.Direction(),angular)||gp_Lin(m.axis).Distance(circle.Location())>linear) return false;
        const double et=m.axial(circle.Location()),er=circle.Radius();
        if(first) {t=et;rho=er;first=false;}
        else if(std::abs(t-et)>linear||std::abs(rho-er)>linear) return false;
        sweep+=curve.LastParameter()-curve.FirstParameter();
    }
    // Each shared edge was visited once per occurrence in face a; seam-free circles occur once.
    return !first&&std::abs(sweep-2*std::acos(-1.))<1.e-8;
}
// Torus between cap plane (axial tp) and body cone: tube centre at distance r from
// both supports, with both full tangency circles at the feet of those normals.
bool certifyFillet(const Meridian& m,const TopoDS_Face& cap,double tp,const TopoDS_Face& body,const gp_Cone& cone,
                   const TopoDS_Face& blend,Feature& f,QString& why) {
    BRepAdaptor_Surface s(blend);
    if(s.GetType()!=GeomAbs_Torus) return false;
    const auto torus=s.Torus();
    if(!m.coaxial(torus.Axis())) {why=QStringLiteral("toroidal transition is not coaxial with the body cone");return false;}
    const double r=torus.MinorRadius(),tc=m.axial(torus.Location()),rc=torus.MajorRadius();
    if(!(r>linear)||std::abs(std::abs(tc-tp)-r)>linear) {why=QStringLiteral("toroidal transition is not tangent to its cap plane");return false;}
    // Body generator in the meridian half-plane: rho = r0 + k*t.
    const double r0=m.radius(cone,0),k=m.radius(cone,1)-r0,n=std::sqrt(1+k*k);
    const double signedDistance=(rc-r0-k*tc)/n;
    if(std::abs(std::abs(signedDistance)-r)>linear) {why=QStringLiteral("toroidal transition is not tangent to the body cone");return false;}
    const double ft=tc+k*signedDistance/n,fr=rc-signedDistance/n; // Foot of the normal on the generator.
    double ct,cr,bt,br;
    if(!sharedCircle(m,blend,cap,ct,cr)||!sharedCircle(m,blend,body,bt,br)) {why=QStringLiteral("toroidal transition is not bounded by two complete circles");return false;}
    if(std::abs(ct-tp)>linear||std::abs(cr-rc)>linear||std::abs(bt-ft)>linear||std::abs(br-fr)>linear) {
        why=QStringLiteral("toroidal transition boundaries are not its tangency circles");return false;
    }
    if(!(m.radius(cone,tp)>linear)) {why=QStringLiteral("cap plane and body cone have no unique circular sharp edge");return false;}
    f.kind=Kind::Fillet;f.size=r;return true;
}
// Conical band between cap plane and body cone. Distances are measured along each
// support from the reconstructed sharp circle: radially on the cap, along the body
// generator on the cone. Only equal distances certify a Chamfer.
bool certifyChamfer(const Meridian& m,const TopoDS_Face& cap,double tp,const TopoDS_Face& body,const gp_Cone& cone,
                    const TopoDS_Face& blend,Feature& f,QString& why) {
    BRepAdaptor_Surface s(blend);
    if(s.GetType()!=GeomAbs_Cone) return false;
    const auto band=s.Cone();
    if(!m.coaxial(band.Axis())) {why=QStringLiteral("conical band is not coaxial with the body cone");return false;}
    const double kb=m.radius(band,1)-m.radius(band,0),kc=m.radius(cone,1)-m.radius(cone,0);
    if(std::abs(std::atan(kb)-std::atan(kc))<angular) {why=QStringLiteral("conical band has the body cone's half-angle");return false;}
    double ct,cr,bt,br;
    if(!sharedCircle(m,blend,cap,ct,cr)||!sharedCircle(m,blend,body,bt,br)) {why=QStringLiteral("conical band is not bounded by two complete circles");return false;}
    if(std::abs(ct-tp)>linear||std::abs(m.radius(band,ct)-cr)>linear||std::abs(m.radius(band,bt)-br)>linear
       ||std::abs(m.radius(cone,bt)-br)>linear) {why=QStringLiteral("conical band boundaries do not lie on their supports");return false;}
    const double sharp=m.radius(cone,tp);
    if(!(sharp>linear)) {why=QStringLiteral("cap plane and body cone have no unique circular sharp edge");return false;}
    const double alongCap=std::abs(sharp-cr),alongBody=std::hypot(bt-tp,br-sharp);
    if(!(alongCap>linear)||std::abs(alongCap-alongBody)>linear) {
        why=QStringLiteral("conical band distances %1 / %2 mm are unequal (stepped cone)").arg(alongCap,0,'g',10).arg(alongBody,0,'g',10);return false;
    }
    f.kind=Kind::Chamfer;f.size=alongCap;return true;
}
// Two planar caps, one body cone and at most one Fillet/Chamfer per cap. Every
// interpretation of the face chain is certified; exactly one must survive, and
// the replayed sharp frustum plus recipes must be material-equivalent to source.
bool recoverConical(const TopoDS_Shape& source,State& state,QString& error) {
    std::vector<TopoDS_Face> faces;
    for(TopExp_Explorer it(source,TopAbs_FACE);it.More();it.Next()) {
        const auto type=BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType();
        if(type!=GeomAbs_Plane&&type!=GeomAbs_Cone&&type!=GeomAbs_Torus) return false;
        faces.push_back(TopoDS::Face(it.Current()));
    }
    if(faces.size()<3||faces.size()>5) return false;
    // Linear chain cap - [blend] - body - [blend] - cap.
    std::vector<std::vector<TopoDS_Face>> adjacent;
    for(const auto& f:faces) adjacent.push_back(neighbors(source,f));
    std::vector<size_t> ends;
    for(size_t i=0;i<faces.size();++i) {
        if(adjacent[i].size()==1) ends.push_back(i);
        else if(adjacent[i].size()!=2) return false;
    }
    if(ends.size()!=2) return false;
    std::vector<TopoDS_Face> chain{faces[ends[0]]};
    while(chain.size()<faces.size()) {
        const auto& last=chain.back();size_t index=0;
        for(;index<faces.size()&&!faces[index].IsSame(last);++index) {}
        TopoDS_Face next;
        for(const auto& n:adjacent[index]) if(chain.size()<2||!n.IsSame(chain[chain.size()-2])) next=n;
        if(next.IsNull()||std::any_of(chain.begin(),chain.end(),[&](const auto& c){return c.IsSame(next);})) return false;
        chain.push_back(next);
    }
    for(const auto& cap:{chain.front(),chain.back()}) if(BRepAdaptor_Surface(cap).GetType()!=GeomAbs_Plane) return false;
    double sourceVolume=0;
    if(!BRepReconstruction::validSolid(source,sourceVolume)) return false;
    QString why;std::vector<State> interpretations;
    for(size_t b=1;b+1<chain.size();++b) {
        BRepAdaptor_Surface bodySurface(chain[b]);
        if(bodySurface.GetType()!=GeomAbs_Cone) continue;
        const auto cone=bodySurface.Cone();Meridian m{cone.Axis()};
        if(b>2||chain.size()-1-b>2) continue; // At most one blend face per cap.
        bool valid=true;double tCap[2]{};State candidate;
        for(int side=0;side<2&&valid;++side) {
            const auto& cap=side?chain.back():chain.front();
            const auto plane=BRepAdaptor_Surface(cap).Plane();
            if(!plane.Axis().Direction().IsParallel(m.axis.Direction(),angular)) {valid=false;break;}
            tCap[side]=m.axial(plane.Location());
            const bool blended=side?chain.size()-1-b==2:b==2;
            if(!blended) continue;
            const auto& blend=side?chain[b+1]:chain[b-1];
            Feature f;f.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
            if(!certifyFillet(m,cap,tCap[side],chain[b],cone,blend,f,why)&&!certifyChamfer(m,cap,tCap[side],chain[b],cone,blend,f,why)) {valid=false;break;}
            f.supports[0]=plane;f.cones[1]=cone;f.faces={blend};
            candidate.features.push_back(f);
        }
        if(!valid||candidate.features.empty()||std::abs(tCap[0]-tCap[1])<linear) continue;
        const double ta=std::min(tCap[0],tCap[1]),tb=std::max(tCap[0],tCap[1]);
        const double ra=m.radius(cone,ta),rb=m.radius(cone,tb);
        if(!(ra>linear)||!(rb>linear)||std::abs(ra-rb)<linear) continue;
        candidate.sharp=BRepPrimAPI_MakeCone(gp_Ax2(m.axis.Location().Translated(gp_Vec(m.axis.Direction())*ta),m.axis.Direction()),ra,rb,tb-ta).Shape();
        TopoDS_Shape replay;QString replayError;
        if(!rebuild(candidate,replay,replayError)||!BRepReconstruction::equivalent(source,replay)||!bind(candidate,source,replayError)) {
            why=replayError.isEmpty()?QStringLiteral("replayed sharp cone and transitions differ from the current material"):replayError;continue;
        }
        interpretations.push_back(std::move(candidate));
    }
    if(interpretations.size()>1) {
        error=QStringLiteral("Ambiguous conical geometry: more than one Fillet/Chamfer interpretation is valid. It is treated as ordinary geometry.");return false;
    }
    if(interpretations.empty()) {
        if(!why.isEmpty()) error=QStringLiteral("No certified conical transition: %1. The faces are treated as ordinary geometry.").arg(why);
        return false;
    }
    state=std::move(interpretations.front());return true;
}
bool affected(const Feature& feature,const TopoDS_Shape& source,const TopoDS_Face& target) {
    for(const auto& f:feature.faces) for(const auto& n:neighbors(source,f)) if(n.IsSame(target)) return true;
    return false;
}
template<class Operation> bool changeSupport(const TopoDS_Shape& current,const State& prior,const TopoDS_Face& target,
        bool keep,State& after,TopoDS_Shape& result,QString& error,Operation operation,const gp_Pln& expected,
        bool translate=false) {
    result.Nullify();State state=prior;
    if(!resolve(current,state,error)||!member(current,target)||BRepAdaptor_Surface(target).GetType()!=GeomAbs_Plane) return false;
    auto original=BRepAdaptor_Surface(target).Plane();auto sharpFace=supportFace(state.sharp,original);
    if(sharpFace.IsNull()) {error=QStringLiteral("Choose a sharp supporting face, or use Edit Fillet / Chamfer to change a transition dimension.");return false;}
    // Rotating a cap breaks coaxiality with a cylinder/cone, which is the whole
    // certification basis of a circular transition: refuse rather than guess.
    for(const auto& f:state.features) if(!translate&&EdgeBlendFeatures::circular(f)) {
        error=QStringLiteral("Supporting-face edits on circular Fillets/Chamfers are not yet certified (rotation breaks coaxiality); remove the transition first.");return false;
    }
    if(translate) for(const auto& f:state.features) for(int k=0;k<2;++k) {
        const auto axis=f.cylinders[k]?std::optional<gp_Dir>(f.cylinders[k]->Axis().Direction())
            :f.cones[k]?std::optional<gp_Dir>(f.cones[k]->Axis().Direction()):std::nullopt;
        if(axis&&!original.Axis().Direction().IsParallel(*axis,angular)) {
            error=QStringLiteral("Circular transition support translation requires a cap perpendicular to its cylinder/cone axis.");return false;
        }
    }
    const bool circular=std::any_of(state.features.begin(),state.features.end(),[](const auto& f){return EdgeBlendFeatures::circular(f);});
    if(!keep) std::erase_if(state.features,[&](const auto& f){return affected(f,current,target);});
    auto base=operation(state.sharp,sharpFace);
    if(base.IsNull()) return false;
    // Local cap sweeps may retain cylindrical/conical bands. Recipes require one
    // unambiguous carrier per support; use the existing material-certified
    // same-domain normalization, then enforce strictSharp again in rebuild.
    if(translate&&circular) {
        TopoDS_Shape normalized;
        if(!BRepReconstruction::normalize(base,normalized,error)) return false;
        base=normalized;
    }
    if(!supportFace(base,expected).IsNull()) {
        for(auto& f:state.features) for(auto& plane:f.supports) if(planeEqual(plane,original)) plane=expected;
    } else {error=QStringLiteral("The requested face operation lost its supporting plane.");return false;}
    state.sharp=base;
    if(!rebuild(state,result,error)) return false;
    after=std::move(state);return true;
}
}

bool circular(const Feature& f) {return f.cylinders[0]||f.cylinders[1]||f.cones[0]||f.cones[1];}
bool resolve(const TopoDS_Shape& current,State& state,QString& error) {
    error.clear();
    try {
        if(!current.IsNull()&&!state.sharp.IsNull()&&!state.features.empty()&&state.certified.IsSame(current)) return true;
        if(!state.sharp.IsNull()&&!state.features.empty()) {
            double volume=0;
            if(!BRepReconstruction::validSolid(current,volume)) {
                state={};error=QStringLiteral("Transition editing requires one valid closed solid without stray topology.");return false;
            }
            auto candidate=state;TopoDS_Shape replay;
            if(rebuild(candidate,replay,error)&&BRepReconstruction::equivalent(current,replay)&&bind(candidate,current,error)) {state=std::move(candidate);return true;}
        }
        state={};
        if(recover(current,state,error)) {error.clear();return true;}
        state={};QString conical;
        if(recoverConical(current,state,conical)) {error.clear();return true;}
        state={};
        if(!conical.isEmpty()) {error=conical;return false;}
        for(TopExp_Explorer it(current,TopAbs_FACE);it.More();it.Next()) {
            const auto type=BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType();
            if(type==GeomAbs_Cone||type==GeomAbs_Torus) {
                error=QStringLiteral("No certified transition recipe: only coaxial cap Fillets/Chamfers on a conical solid with two planar caps are recognized without a recipe; recipe-free toroidal transitions elsewhere cannot be reconstructed. An ordinary cone is not a Chamfer.");return false;
            }
        }
        error=QStringLiteral("No certified separated edge transitions were found. Imported recognition requires rectangular local stock and verified tangent/equal-distance supports.");return false;
    } catch(const Standard_Failure& e) {state={};error=QStringLiteral("Transition analysis failed: %1").arg(QString::fromUtf8(e.GetMessageString()));return false;}
}
int selected(const State& state,const TopoDS_Face& face) {
    if(!member(state.certified,face)) return -1;
    int index=-1;
    for(size_t i=0;i<state.features.size();++i) for(const auto& f:state.features[i].faces) if(f.IsSame(face)) {if(index>=0&&index!=int(i)) return -1;index=int(i);}
    return index;
}
bool create(const TopoDS_Shape& current,const State& prior,const TopoDS_Edge& edge,Kind kind,double size,State& after,TopoDS_Shape& result,QString& error) {
    result.Nullify();State state=prior;
    try {
        if(!resolve(current,state,error)) {state={};state.sharp=current;}
        TopTools_IndexedDataMapOfShapeListOfShape adjacency;TopExp::MapShapesAndUniqueAncestors(current,TopAbs_EDGE,TopAbs_FACE,adjacency);
        if(edge.IsNull()||!adjacency.Contains(edge)||adjacency.FindFromKey(edge).Extent()!=2) {error=QStringLiteral("Select one manifold sharp edge.");return false;}
        Feature f;f.kind=kind;f.size=size;f.id=QUuid::createUuid().toString(QUuid::WithoutBraces);int k=0;
        for(const auto& face:adjacency.FindFromKey(edge)) {
            BRepAdaptor_Surface s(TopoDS::Face(face));
            if(s.GetType()==GeomAbs_Plane) f.supports[k]=s.Plane();
            else if(kind==Kind::Fillet&&s.GetType()==GeomAbs_Cylinder&&BRepAdaptor_Curve(edge).GetType()==GeomAbs_Circle) f.cylinders[k]=s.Cylinder();
            else if(s.GetType()==GeomAbs_Cone&&BRepAdaptor_Curve(edge).GetType()==GeomAbs_Circle) f.cones[k]=s.Cone();
            else {error=QStringLiteral("Editable transitions require planar supports or a circular plane/cylinder (Fillet) or plane/cone intersection.");return false;}
            ++k;
        }
        if(int(bool(f.cylinders[0]))+int(bool(f.cylinders[1]))+int(bool(f.cones[0]))+int(bool(f.cones[1]))>1) {error=QStringLiteral("Two curved supports are not certified.");return false;}
        if(EdgeBlendFeatures::circular(f)) {
            // The curved support must be coaxial with the selected circle; the
            // plane must be perpendicular to it (one unique circular sharp edge).
            const auto circle=BRepAdaptor_Curve(edge).Circle();const int c=f.cylinders[0]||f.cones[0]?0:1;
            const gp_Ax1 axis=f.cones[c]?f.cones[c]->Axis():f.cylinders[c]->Axis();
            if(!axis.Direction().IsParallel(circle.Axis().Direction(),angular)||gp_Lin(axis).Distance(circle.Location())>linear
               ||!f.supports[1-c].Axis().Direction().IsParallel(axis.Direction(),angular)) {
                error=QStringLiteral("Circular transitions require a cap plane perpendicular to a coaxial cylinder or cone.");return false;
            }
        }
        if(state.features.size()>=8) {error=QStringLiteral("At most eight separated transitions can be certified on one Part.");return false;}
        for(const auto& old:state.features) for(const auto& face:old.faces) if(touches(edge,face)) {
            error=QStringLiteral("The selected edge touches an existing transition; junction reconstruction is unsupported.");return false;
        }
        state.features.push_back(f);
        if(!rebuild(state,result,error)) return false;
        after=std::move(state);return true;
    } catch(const Standard_Failure& e) {error=QStringLiteral("Transition creation failed: %1").arg(QString::fromUtf8(e.GetMessageString()));return false;}
}
bool edit(const TopoDS_Shape& current,const State& prior,const QString& id,double size,State& after,TopoDS_Shape& result,QString& error) {
    result.Nullify();State state=prior;
    if(!resolve(current,state,error)) return false;
    auto it=std::find_if(state.features.begin(),state.features.end(),[&](const auto& f){return f.id==id;});
    if(it==state.features.end()) {error=QStringLiteral("The selected transition recipe is no longer valid; select its current face again.");return false;}
    it->size=size;
    if(!rebuild(state,result,error)) return false;
    after=std::move(state);return true;
}
bool remove(const TopoDS_Shape& current,const State& prior,const std::vector<TopoDS_Face>& faces,State& after,TopoDS_Shape& result,QString& error) {
    result.Nullify();State state=prior;
    if(!resolve(current,state,error)||faces.empty()) return false;
    std::vector<QString> ids;
    for(const auto& f:faces) {int i=selected(state,f);if(i<0) {error=QStringLiteral("Select only complete recognized transitions for sharp restoration.");return false;}ids.push_back(state.features[i].id);}
    std::erase_if(state.features,[&](const auto& f){return std::find(ids.begin(),ids.end(),f.id)!=ids.end();});
    if(!rebuild(state,result,error)) return false;
    after=std::move(state);return true;
}
bool move(const TopoDS_Shape& current,const State& prior,const TopoDS_Face& face,double distance,bool keep,State& after,TopoDS_Shape& result,QString& error) {
    result.Nullify();if(!member(current,face)||BRepAdaptor_Surface(face).GetType()!=GeomAbs_Plane||!std::isfinite(distance)) {error=QStringLiteral("Choose a current planar support and enter a finite distance.");return false;}
    auto expected=BRepAdaptor_Surface(face).Plane();auto normal=expected.Axis().Direction();if(face.Orientation()==TopAbs_REVERSED) normal.Reverse();expected.Translate(gp_Vec(normal)*distance);
    return changeSupport(current,prior,face,keep,after,result,error,[&](const auto& s,const auto& f){return MoveFaceGeometry::move(s,f,distance,error);},expected,true);
}
bool rotate(const TopoDS_Shape& current,const State& prior,const TopoDS_Face& face,const gp_Ax1& axis,double degrees,bool keep,State& after,TopoDS_Shape& result,QString& error) {
    result.Nullify();if(!member(current,face)||BRepAdaptor_Surface(face).GetType()!=GeomAbs_Plane||!std::isfinite(degrees)) {error=QStringLiteral("Choose a current planar support and enter a finite angle.");return false;}
    auto expected=BRepAdaptor_Surface(face).Plane();expected.Rotate(axis,degrees*std::acos(-1.)/180.);
    return changeSupport(current,prior,face,keep,after,result,error,[&](const auto& s,const auto& f){return FaceTransformGeometry::rotate(s,f,axis,degrees,error);},expected);
}
QJsonObject encode(const State& state) {
    if(state.sharp.IsNull()||state.features.empty()) return {};
    std::ostringstream stream;BRepTools::Write(state.sharp,stream);
    QJsonArray features;
    for(const auto& f:state.features) {
        QJsonArray planes;for(const auto& p:f.supports) {auto n=p.Axis().Direction();planes.append(QJsonArray{n.X(),n.Y(),n.Z(),gp_Vec(gp_Pnt(),p.Location()).Dot(gp_Vec(n))});}
        QJsonArray cylinders;for(const auto& c:f.cylinders) {
            if(!c) {cylinders.append(QJsonValue());continue;}
            auto p=c->Location();auto n=c->Axis().Direction();
            cylinders.append(QJsonArray{p.X(),p.Y(),p.Z(),n.X(),n.Y(),n.Z(),c->Radius()});
        }
        QJsonObject feature{{"cylinders",cylinders},{"id",f.id},{"kind",f.kind==Kind::Fillet?"fillet":"chamfer"},{"size",f.size},{"supports",planes}};
        if(f.cones[0]||f.cones[1]) {
            QJsonArray cones;for(const auto& c:f.cones) {
                if(!c) {cones.append(QJsonValue());continue;}
                auto p=c->Location();auto n=c->Axis().Direction();
                cones.append(QJsonArray{p.X(),p.Y(),p.Z(),n.X(),n.Y(),n.Z(),c->RefRadius(),c->SemiAngle()});
            }
            feature.insert("cones",cones);
        }
        features.append(feature);
    }
    // Recipe version 3 adds conical supports; older readers refuse it safely,
    // and version 1/2 recipes stay byte-identical when no cone is involved.
    const bool conical=std::any_of(state.features.begin(),state.features.end(),[](const auto& f){return f.cones[0]||f.cones[1];});
    const bool cylindrical=std::any_of(state.features.begin(),state.features.end(),[](const auto& f){return f.cylinders[0]||f.cylinders[1];});
    return {{"version",conical?3:cylindrical?2:1},{"sharp",QString::fromStdString(stream.str())},{"features",features}};
}
State decode(const QJsonObject& object) {
    State state;
    try {
        const int version=object.value("version").toInt();if(version<1||version>3) return {};
        auto data=object.value("sharp").toString().toStdString();if(data.size()>4*1024*1024) return {};
        std::istringstream stream(data);BRep_Builder builder;BRepTools::Read(state.sharp,stream,builder);
        const auto features=object.value("features").toArray();if(features.isEmpty()||features.size()>8) return {};
        for(const auto& value:features) {
            const auto o=value.toObject();Feature f;f.id=o.value("id").toString();auto kind=o.value("kind").toString();if(kind!="fillet"&&kind!="chamfer") return {};
            f.kind=kind=="fillet"?Kind::Fillet:Kind::Chamfer;f.size=o.value("size").toDouble();if(f.id.isEmpty()||!std::isfinite(f.size)||f.size<=0) return {};
            for(const auto& old:state.features) if(old.id==f.id) return {};
            auto planes=o.value("supports").toArray();if(planes.size()!=2) return {};
            for(int k=0;k<2;++k) {auto p=planes[k].toArray();if(p.size()!=4) return {};for(const auto& v:p) if(!v.isDouble()||!std::isfinite(v.toDouble())) return {};
                gp_Dir n(p[0].toDouble(),p[1].toDouble(),p[2].toDouble());gp_Pnt origin;origin.Translate(gp_Vec(n)*p[3].toDouble());f.supports[k]=gp_Pln(origin,n);}
            auto cylinders=o.value("cylinders").toArray();
            if(version==1&&!cylinders.isEmpty()) for(const auto& c:cylinders) if(!c.isNull()) return {};
            if(!cylinders.isEmpty()&&cylinders.size()!=2) return {};
            for(int k=0;k<cylinders.size();++k) if(!cylinders[k].isNull()) {
                auto c=cylinders[k].toArray();if(c.size()!=7) return {};
                for(const auto& v:c) if(!v.isDouble()||!std::isfinite(v.toDouble())) return {};
                if(c[6].toDouble()<=0||f.kind!=Kind::Fillet) return {};
                f.cylinders[k]=gp_Cylinder(gp_Ax3(gp_Pnt(c[0].toDouble(),c[1].toDouble(),c[2].toDouble()),gp_Dir(c[3].toDouble(),c[4].toDouble(),c[5].toDouble())),c[6].toDouble());
            }
            auto cones=o.value("cones").toArray();
            if(version<3&&o.contains("cones")) return {};
            if(!cones.isEmpty()&&cones.size()!=2) return {};
            for(int k=0;k<cones.size();++k) if(!cones[k].isNull()) {
                auto c=cones[k].toArray();if(c.size()!=8) return {};
                for(const auto& v:c) if(!v.isDouble()||!std::isfinite(v.toDouble())) return {};
                const double semi=c[7].toDouble();
                if(c[6].toDouble()<0||std::abs(semi)<1.e-6||std::abs(semi)>std::acos(-1.)/2-1.e-6) return {};
                f.cones[k]=gp_Cone(gp_Ax3(gp_Pnt(c[0].toDouble(),c[1].toDouble(),c[2].toDouble()),gp_Dir(c[3].toDouble(),c[4].toDouble(),c[5].toDouble())),semi,c[6].toDouble());
            }
            if(int(bool(f.cylinders[0]))+int(bool(f.cylinders[1]))+int(bool(f.cones[0]))+int(bool(f.cones[1]))>1) return {};
            state.features.push_back(f);
        }
    } catch(const Standard_Failure&) {return {};}
    return state;
}
}
