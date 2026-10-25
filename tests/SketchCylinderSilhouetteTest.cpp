#include "SketchProjection.h"
#include "NativeDocument.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <BRepTools.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Iterator.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Pln.hxx>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace {
using namespace SketchProjection;
using namespace Sketch;
constexpr double pi=std::numbers::pi,turn=2*pi;
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
Construction::Object plane() {Construction::Object p;p.id="silhouette-wp";p.name="WP1";p.kind=Construction::Kind::Workplane;return p;}
bool near(Point a,Point b) {return std::hypot(a.x-b.x,a.y-b.y)<1.e-8;}
bool same(const Segment& a,const Segment& b) {return (near(a.start,b.start) && near(a.end,b.end)) || (near(a.start,b.end) && near(a.end,b.start));}
gp_Cylinder cylinder(double angle=pi/4,double phase=0.37) {
    gp_Ax3 axes(gp_Pnt(2,3,8),gp_Dir(0,-std::sin(angle),std::cos(angle)),gp_Dir(1,0,0));
    axes.Rotate(axes.Axis(),phase);return {axes,10};
}
Source source(const gp_Cylinder& c,double u=0,double lo=-4,double hi=12) {
    return {BRepBuilderAPI_MakeFace(c,u,u+turn,lo,hi).Shape(),{},"cylinder mantle"};
}
void count(const Result& r,size_t n) {
    if(!r.success || r.additions.size()!=n) {
        std::cerr<<"Expected "<<n<<", got "<<r.additions.size()<<'\n';
        for(const auto& d:r.diagnostics) std::cerr<<int(d.code)<<": "<<d.message.toStdString()<<'\n';
    }
    check(r.success && r.additions.size()==n,"unexpected cylinder projection result");
}
void refused(const Result& r,Code code,size_t index=0) {
    check(!r.success && r.additions.empty(),"invalid cylinder batch left partial geometry");
    for(const auto& d:r.diagnostics) if(d.code==code && d.sourceIndex==index && !d.message.isEmpty()) return;
    for(const auto& d:r.diagnostics) std::cerr<<int(d.code)<<": "<<d.message.toStdString()<<'\n';
    throw std::runtime_error("missing cylinder source diagnostic");
}
Point local(const gp_Pnt& p,const Construction::Object& wp) {
    const gp_Vec d(wp.frame.Location(),p);return {d.Dot(gp_Vec(wp.frame.XDirection())),d.Dot(gp_Vec(wp.frame.YDirection()))};
}
std::array<Segment,2> expected(const gp_Cylinder& c,const gp_Trsf& placement,const Construction::Object& wp,double lo=-4,double hi=12) {
    // Independent world-space construction: radial = normalized(axis x view).
    auto axis=gp_Vec(c.Axis().Direction());axis.Transform(placement);
    auto radial=axis.Crossed(gp_Vec(wp.frame.Direction()));radial.Normalize();
    radial*=c.Radius()*std::abs(placement.ScaleFactor());const auto origin=c.Location().Transformed(placement);
    const auto p=[&](double v,int side){return local(origin.Translated(axis*v+radial*side),wp);};
    return {Segment{p(lo,1),p(hi,1)},Segment{p(lo,-1),p(hi,-1)}};
}
void verify(const Result& r,const gp_Cylinder& c,const gp_Trsf& placement,const Construction::Object& wp,double lo=-4,double hi=12) {
    check(r.success,"cylinder projection failed");const auto generators=expected(c,placement,wp,lo,hi);
    std::vector<Segment> lines;
    for(const auto& e:r.additions) {
        check(e.role==Role::Construction && e.workplaneId==wp.id && !QUuid(e.id).isNull(),"silhouette identity/role/owner changed");
        if(const auto* s=std::get_if<Segment>(&e.geometry)) {
            for(const auto& old:lines) check(!same(*s,old),"seam/silhouette duplicate line survived");lines.push_back(*s);
            check(BRepAdaptor_Curve(TopoDS::Edge(Sketch::shape(e,wp.frame))).GetType()==GeomAbs_Line,"silhouette is not an analytic line");
        }
    }
    for(const auto& g:generators) {
        size_t matches=0;for(const auto& s:lines) if(same(g,s)) ++matches;
        check(matches==1,"missing, duplicated or incorrectly clipped tangent generator");
    }
    check(std::count_if(r.additions.begin(),r.additions.end(),[](const auto& e){return isElliptical(e.geometry);})==2,"circular rims lost their exact elliptical projections");
}
void geometryAndDuplicates() {
    const auto wp=plane();
    for(double tilt:{pi/6,pi/4,pi/3}) for(double seam:{0.,0.7,2.1}) {
        const auto c=cylinder(tilt);auto s=source(c,seam);auto r=project({s},wp);count(r,5);verify(r,c,{},wp);
        auto reverse=s;reverse.shape.Reverse();const auto backwards=project({reverse},wp);count(backwards,5);verify(backwards,c,{},wp);
        for(size_t i=0;i<r.additions.size();++i) check(r.additions[i].geometry==backwards.additions[i].geometry,"reversed face changed silhouette geometry");
        count(project({s,reverse},wp),5);
        auto saved=r.additions;for(auto& e:saved) {e.visible=false;e.selectable=false;}
        const auto repeat=project({reverse},wp,saved);count(repeat,0);check(!repeat.changed(),"duplicate cylinder was not a no-op");
        const auto tangent=expected(c,{},wp)[0];Entity old;old.workplaneId=wp.id;old.role=Role::Construction;old.geometry=tangent;
        old.visible=false;old.selectable=false;count(project({s},wp,{old}),4);
    }
    // Seam u=0 is precisely one tangent generator for this basis.
    const auto c=cylinder(pi/4,0);const auto r=project({source(c)},wp);count(r,4);verify(r,c,{},wp);
    check(std::count_if(r.diagnostics.begin(),r.diagnostics.end(),[](const auto& d){return d.code==Code::Duplicate;})>=2,"coincident seam and silhouette not deduplicated");
}
void placementsAndStorage() {
    auto wp=plane();gp_Trsf common;common.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),0.64);common.SetTranslationPart(gp_Vec(50,-20,35));wp.frame.Transform(common);
    // A cylinder with an independently rotated axis, nested OCCT locations,
    // uniform scale and reflection, projected onto a moved/rotated Workplane.
    const gp_Cylinder c(gp_Ax3(gp_Pnt(2,3,8),gp_Dir(1,2,3),gp_Dir(0,1,0)),7);
    for(bool reflected:{false,true}) {
        gp_Trsf scale;scale.SetScale(gp_Pnt(),reflected?-2:2);auto s=source(c,0.4,-3,17);s.partPlacement=common*scale;
        auto r=project({s},wp);count(r,5);verify(r,c,s.partPlacement,wp,-3,17);
        gp_Trsf child;child.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(0,0,1)),0.2);child.SetTranslationPart(gp_Vec(3,2,1));
        gp_Trsf parent;parent.SetTranslation(gp_Vec(-4,5,6));BRep_Builder b;TopoDS_Compound root;b.MakeCompound(root);
        b.Add(root,s.shape.Moved(TopLoc_Location(child)));root.Move(TopLoc_Location(parent));s.shape=TopoDS_Iterator(root).Value();
        r=project({s},wp);count(r,5);verify(r,c,s.partPlacement*parent*child,wp,-3,17);
        QTemporaryDir temp;NativeDocumentSnapshot doc,loaded;doc.constructions={wp};doc.sketchEntities=r.additions;QString error;
        check(NativeDocumentStorage::save(temp.filePath("silhouettes.odc"),doc,error)
            && NativeDocumentStorage::load(temp.filePath("silhouettes.odc"),loaded,error) && loaded.sketchEntities==doc.sketchEntities,"silhouettes lost exact parameters in Save/Open");
    }
    auto distant=plane();distant.frame.SetLocation(gp_Pnt(1.e20,-1.e20,1.e20));auto s=source(cylinder());s.partPlacement.SetTranslation(gp_Vec(1.e20,-1.e20,1.e20));
    const auto far=project({s},distant),base=project({source(cylinder())},plane());count(far,5);
    for(size_t i=0;i<far.additions.size();++i) check(far.additions[i].geometry==base.additions[i].geometry,"large common translation erased finite silhouette values");
}
TopoDS_Face mantle(const TopoDS_Shape& shape) {
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const BRepAdaptor_Surface s(TopoDS::Face(it.Current()));if(s.GetType()==GeomAbs_Cylinder && std::abs(s.Cylinder().Radius()-10)<1.e-9) return TopoDS::Face(it.Current());
    }
    throw std::runtime_error("trimmed mantle fixture missing");
}
void limits() {
    const auto wp=plane();
    const auto partial=BRepBuilderAPI_MakeFace(cylinder(),0,pi,-4,12).Shape();
    refused(project({{partial,{},"partial mantle"}},wp),Code::UnsupportedSilhouetteTrim);
    const auto body=BRepPrimAPI_MakeCylinder(10,20).Shape();
    const auto bore=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-20,-20,10),gp_Dir(1,1,0)),2,60).Shape();
    const auto withHoles=BRepAlgoAPI_Cut(body,bore).Shape();
    refused(project({{mantle(withHoles),{},"mantle holes"}},wp),Code::UnsupportedSilhouetteTrim);
    const auto cutPlane=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,15),gp_Dir(0.3,0,1))).Face();
    const auto half=BRepPrimAPI_MakeHalfSpace(cutPlane,gp_Pnt(0,0,0)).Solid();
    const auto oblique=BRepAlgoAPI_Common(body,half).Shape();
    refused(project({{mantle(oblique),{},"oblique cap"}},wp),Code::UnsupportedSilhouetteTrim);
    refused(project({source(cylinder(0,0))},wp),Code::CollapsedProjection); // No unique pair viewed down the axis.
    refused(project({source(cylinder(pi/2,0))},wp),Code::CollapsedProjection); // Existing rim rank-loss policy.
    refused(project({source(cylinder(1.e-10,0),0,-4,10000)},wp),Code::NumericalFailure);
    refused(project({source(cylinder(),0,0,1.e-6)},wp,{},1.e-4),Code::CollapsedProjection);
    // Loose B-Rep tolerances can hide a closed but displaced boundary. Its
    // exact p-curve/cylinder coefficients must still meet our tighter request.
    auto loose=source(cylinder());BRep_Builder builder;const gp_Vec shift(1.e-5,0,0);
    TopTools_IndexedMapOfShape vertices,edges;TopExp::MapShapes(loose.shape,TopAbs_VERTEX,vertices);TopExp::MapShapes(loose.shape,TopAbs_EDGE,edges);
    for(int i=1;i<=vertices.Extent();++i) {
        const auto v=TopoDS::Vertex(vertices(i));builder.UpdateVertex(v,BRep_Tool::Pnt(v).Translated(shift),1.e-3);
    }
    for(int i=1;i<=edges.Extent();++i) {
        const auto e=TopoDS::Edge(edges(i));TopLoc_Location loc;double f,l;auto curve=BRep_Tool::Curve(e,loc,f,l)->Copy();curve->Translate(shift);
        builder.UpdateEdge(e,Handle(Geom_Curve)::DownCast(curve),loc,1.e-3);
    }
    builder.UpdateFace(TopoDS::Face(loose.shape),1.e-3);
    check(BRepCheck_Analyzer(loose.shape).IsValid(),"loose displaced-boundary fixture is not valid B-Rep");
    refused(project({loose},wp),Code::NumericalFailure);
    const Source vertex{BRepBuilderAPI_MakeVertex(gp_Pnt(1,2,3)).Shape(),{},"valid vertex"};
    const auto r=project({vertex,source(cylinder()),{partial,{},"bad trim"}},wp);refused(r,Code::UnsupportedSilhouetteTrim,2);
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {geometryAndDuplicates();placementsAndStorage();limits();std::cout<<"Exact finite cylinder silhouettes, placements, seams, duplicates and safe trim limits passed.\n";return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
