#include "EdgeBlendFeatures.h"
#include "FilletGeometry.h"
#include "ChamferGeometry.h"
#include "BRepReconstruction.h"
#include "MoveFaceGeometry.h"
#include "FaceTransformGeometry.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
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
        if(type==TopAbs_FACE&&BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType()!=GeomAbs_Plane) return false;
    }
    return true;
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
TopoDS_Edge supportEdge(const TopoDS_Shape& shape,const Feature& f) {
    auto a=supportFace(shape,f.supports[0]),b=supportFace(shape,f.supports[1]);
    if(a.IsNull()||b.IsNull()) return {};
    TopoDS_Edge match;
    for(TopExp_Explorer x(a,TopAbs_EDGE);x.More();x.Next()) for(TopExp_Explorer y(b,TopAbs_EDGE);y.More();y.Next())
        if(x.Current().IsSame(y.Current())) {
            if(!match.IsNull()&&!match.IsSame(x.Current())) return {};
            match=TopoDS::Edge(x.Current());
        }
    if(!match.IsNull()&&BRepAdaptor_Curve(match).GetType()!=GeomAbs_Line) return {};
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
bool bind(State& state,const TopoDS_Shape& current,QString& error) {
    // Prototypes from replay are carrier geometry, never saved topology IDs.
    for(auto& feature:state.features) {
        auto prototypes=feature.faces;feature.faces.clear();
        for(TopExp_Explorer it(current,TopAbs_FACE);it.More();it.Next()) {
            auto f=TopoDS::Face(it.Current());
            if(std::any_of(prototypes.begin(),prototypes.end(),[&](const auto& p){return BRepAdaptor_Surface(p).GetType()==GeomAbs_Plane&&BRepAdaptor_Surface(f).GetType()==GeomAbs_Plane
                    ?planeEqual(BRepAdaptor_Surface(p).Plane(),BRepAdaptor_Surface(f).Plane()):BRepReconstruction::sameAnalyticSupport(p,f);})) feature.faces.push_back(f);
        }
        if(feature.faces.size()!=1) {error=QStringLiteral("Transition is missing, split or ambiguous; its recipe was invalidated.");return false;}
    }
    for(const auto& feature:state.features) for(const auto& face:feature.faces) for(const auto& n:neighbors(current,face))
        for(const auto& other:state.features) if(feature.id!=other.id&&std::any_of(other.faces.begin(),other.faces.end(),[&](const auto& f){return f.IsSame(n);})) {
            error=QStringLiteral("Interacting transition junctions are not supported. Use separated edges.");return false;
        }
    state.certified=current;return true;
}
bool rebuild(State& state,TopoDS_Shape& result,QString& error) {
    result.Nullify();
    if(!strictSharp(state.sharp)) {error=QStringLiteral("The sharp supporting solid must be planar, valid, and within the existing 1e-6 tolerance limit.");return false;}
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
                if(BRepReconstruction::sameAnalyticSupport(candidate,TopoDS::Face(prev.Current()))) {old=true;break;}
            BRepAdaptor_Surface surface(candidate);
            if(!old&&((feature.kind==Kind::Fillet&&surface.GetType()==GeomAbs_Cylinder)
                ||(feature.kind==Kind::Chamfer&&surface.GetType()==GeomAbs_Plane))) feature.faces.push_back(candidate);
        }
        if(feature.faces.size()!=1) {error=QStringLiteral("Only a single straight-edge cylindrical Fillet or planar Chamfer can be reconstructed.");return false;}
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
    for(auto& values:bounds) {if(values.size()!=2) return false;std::sort(values.begin(),values.end());}
    candidate.sharp=BRepPrimAPI_MakeBox(gp_Pnt(bounds[0][0],bounds[1][0],bounds[2][0]),gp_Pnt(bounds[0][1],bounds[1][1],bounds[2][1])).Shape();
    TopoDS_Shape replay;
    if(!rebuild(candidate,replay,error)||!BRepReconstruction::equivalent(source,replay)||!bind(candidate,source,error)) return false;
    state=std::move(candidate);return true;
}
bool affected(const Feature& feature,const TopoDS_Shape& source,const TopoDS_Face& target) {
    for(const auto& f:feature.faces) for(const auto& n:neighbors(source,f)) if(n.IsSame(target)) return true;
    return false;
}
template<class Operation> bool changeSupport(const TopoDS_Shape& current,const State& prior,const TopoDS_Face& target,
        bool keep,State& after,TopoDS_Shape& result,QString& error,Operation operation,const gp_Pln& expected) {
    result.Nullify();State state=prior;
    if(!resolve(current,state,error)||!member(current,target)||BRepAdaptor_Surface(target).GetType()!=GeomAbs_Plane) return false;
    auto original=BRepAdaptor_Surface(target).Plane();auto sharpFace=supportFace(state.sharp,original);
    if(sharpFace.IsNull()) {error=QStringLiteral("Choose a sharp supporting face, or use Edit Fillet / Chamfer to change a transition dimension.");return false;}
    if(!keep) std::erase_if(state.features,[&](const auto& f){return affected(f,current,target);});
    auto base=operation(state.sharp,sharpFace);
    if(base.IsNull()) return false;
    if(!supportFace(base,expected).IsNull()) {
        for(auto& f:state.features) for(auto& plane:f.supports) if(planeEqual(plane,original)) plane=expected;
    } else {error=QStringLiteral("The requested face operation lost its supporting plane.");return false;}
    state.sharp=base;
    if(!rebuild(state,result,error)) return false;
    after=std::move(state);return true;
}
}

bool resolve(const TopoDS_Shape& current,State& state,QString& error) {
    error.clear();
    try {
        if(!current.IsNull()&&!state.sharp.IsNull()&&state.certified.IsSame(current)) return true;
        if(!state.sharp.IsNull()) {
            auto candidate=state;TopoDS_Shape replay;
            if(rebuild(candidate,replay,error)&&BRepReconstruction::equivalent(current,replay)&&bind(candidate,current,error)) {state=std::move(candidate);return true;}
        }
        state={};
        if(recover(current,state,error)) return true;
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
        for(const auto& face:adjacency.FindFromKey(edge)) {BRepAdaptor_Surface s(TopoDS::Face(face));if(s.GetType()!=GeomAbs_Plane) {error=QStringLiteral("Editable transitions require two planar supports.");return false;}f.supports[k++]=s.Plane();}
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
    return changeSupport(current,prior,face,keep,after,result,error,[&](const auto& s,const auto& f){return MoveFaceGeometry::move(s,f,distance,error);},expected);
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
        features.append(QJsonObject{{"id",f.id},{"kind",f.kind==Kind::Fillet?"fillet":"chamfer"},{"size",f.size},{"supports",planes}});
    }
    return {{"version",1},{"sharp",QString::fromStdString(stream.str())},{"features",features}};
}
State decode(const QJsonObject& object) {
    State state;
    try {
        if(object.value("version").toInt()!=1) return {};
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
            state.features.push_back(f);
        }
    } catch(const Standard_Failure&) {return {};}
    return state;
}
}
