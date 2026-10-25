#include "SketchProjection.h"
#include "NativeDocument.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepTools.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <gp_Pln.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Iterator.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <gp_Hypr.hxx>
#include <QCoreApplication>
#include <QTemporaryDir>
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
bool near(double a,double b,double eps=1.e-9) {return std::abs(a-b)<=eps;}
bool near(Point a,Point b,double eps=1.e-9) {return std::hypot(a.x-b.x,a.y-b.y)<=eps;}
Construction::Object plane() {Construction::Object wp;wp.id="target";wp.kind=Construction::Kind::Workplane;wp.name="WP1";return wp;}
gp_Ax2 axes(double tilt,double phase=0) {
    gp_Ax2 a(gp_Pnt(3,-7,12),gp_Dir(0,-std::sin(tilt),std::cos(tilt)),gp_Dir(1,0,0));
    a.Rotate(a.Axis(),phase);return a;
}
template<class Conic> Source edge(const Conic& c,double first=0,double span=turn) {
    const auto shape=std::abs(span)==turn?BRepBuilderAPI_MakeEdge(c).Shape():
        span>0?BRepBuilderAPI_MakeEdge(c,first,first+span).Shape():BRepBuilderAPI_MakeEdge(c,first+span,first).Shape().Reversed();
    return {shape,{},"analytic source"};
}
const Entity& one(const Result& result) {
    if(!result.success) for(const auto& d:result.diagnostics) std::cerr<<int(d.code)<<": "<<d.message.toStdString()<<'\n';
    check(result.success && result.additions.size()==1,"expected one exact conic result");return result.additions.front();
}
void refused(const Result& r,Code code) {
    check(!r.success && r.additions.empty(),"failed batch left partial geometry");
    for(const auto& d:r.diagnostics) if(d.code==code && !d.message.isEmpty()) return;
    std::cerr<<"Expected refusal "<<int(code)<<"\n";
    for(const auto& d:r.diagnostics) std::cerr<<int(d.code)<<": "<<d.message.toStdString()<<'\n';
    throw std::runtime_error("missing refusal diagnostic");
}
Point local(gp_Pnt p,const Source& s,const Construction::Object& wp) {
    p.Transform(s.partPlacement);const gp_Vec v(wp.frame.Location(),p);
    return {v.Dot(gp_Vec(wp.frame.XDirection())),v.Dot(gp_Vec(wp.frame.YDirection()))};
}
void verify(const Source& s,const Construction::Object& wp,const Entity& result) {
    check(result.role==Role::Construction && result.workplaneId==wp.id && !QUuid(result.id).isNull(),"lost independent identity/role/owner");
    QString error;check(valid(result,error),"invalid conic output");
    BRepAdaptor_Curve curve(TopoDS::Edge(s.shape));const bool reverse=s.shape.Orientation()==TopAbs_REVERSED;
    if(const auto* arc=std::get_if<EllipticalArc>(&result.geometry)) {
        check(near(std::abs(arc->sweepParameter),curve.LastParameter()-curve.FirstParameter()),"short/long route changed");
        // Interior evaluations certify traversal, beyond just endpoint equality.
        for(double fraction:{0.,0.125,0.3,0.5,0.85,1.}) {
            const double t=reverse?curve.LastParameter()-fraction*(curve.LastParameter()-curve.FirstParameter()):
                curve.FirstParameter()+fraction*(curve.LastParameter()-curve.FirstParameter());
            check(near(ellipsePoint(arc->ellipse,arc->startParameter+fraction*arc->sweepParameter),local(curve.Value(t),s,wp)),"arc phase/endpoints/interior traversal changed");
        }
    } else {
        const auto& e=std::get<Ellipse>(result.geometry);
        for(double t:{0.,0.37,1.3,2.9,4.8,6.1}) {
            const auto p=local(curve.Value(t),s,wp);const auto parameter=ellipseParameter(e,p);
            check(parameter && near(ellipsePoint(e,*parameter),p),"complete conic does not reproduce source support");
        }
    }
    check(BRepAdaptor_Curve(TopoDS::Edge(Sketch::shape(result,wp.frame))).GetType()==GeomAbs_Ellipse,"projected conic became tessellation");
}
void circularProjection() {
    auto wp=plane();
    for(double degrees:{30.,45.,60.}) for(double phase:{0.,0.37,2.1}) {
        const gp_Circ c(axes(degrees*pi/180,phase),10);
        auto s=edge(c);auto r=project({s},wp);const auto& entity=one(r);verify(s,wp,entity);
        const auto e=std::get<Ellipse>(entity.geometry);
        check(near(e.majorRadius,10) && near(e.minorRadius,10*std::cos(degrees*pi/180)) && near(e.center,{3,-7}),"wrong oblique circle singular values");
        for(double start:{0.37,5.8,-0.4}) for(double sweep:{pi/2,3*pi/2,-pi/2,-3*pi/2,turn-1.e-8}) for(bool reversed:{false,true}) {
            s=edge(c,start,sweep);if(reversed) s.shape.Reverse();r=project({s},wp);verify(s,wp,one(r));
            const auto& arc=std::get<EllipticalArc>(r.additions[0].geometry);
            check((arc.sweepParameter>0)==((sweep>0)!=reversed),"CW/CCW source orientation ignored");
            auto flipped=wp;flipped.frame=gp_Ax3(gp_Pnt(),gp_Dir(0,0,-1),gp_Dir(1,0,0));
            const auto f=project({s},flipped);verify(s,flipped,one(f));
            check(near(std::get<EllipticalArc>(f.additions[0].geometry).sweepParameter,-arc.sweepParameter),"reversed target normal did not flip traversal");
        }
    }
    auto nearParallel=edge(gp_Circ(axes(1.e-10),10));
    check(std::holds_alternative<Ellipse>(one(project({nearParallel},wp)).geometry),"genuinely oblique circle rounded to Circle");
    auto seam=Source{BRepBuilderAPI_MakeEdge(gp_Circ(axes(pi/4,0.7),8),0.7,0.7+turn).Shape(),{},"full shifted seam"};
    check(std::holds_alternative<Ellipse>(one(project({seam},wp)).geometry),"full period became bounded arc");
}
void nativeAndTransforms() {
    auto wp=plane();gp_Trsf common;common.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),0.61);common.SetTranslationPart(gp_Vec(30,-20,100));
    wp.frame.Transform(common);
    for(double tilt:{0.,pi/6,pi/3}) for(double sweep:{turn,0.7,-4.8}) for(bool reflected:{false,true}) {
        auto s=edge(gp_Elips(axes(tilt,0.43),13,4),5.9,sweep);s.partPlacement=common;
        gp_Trsf scaling;scaling.SetScale(gp_Pnt(),2.5);
        if(reflected) {gp_Trsf mirror;mirror.SetMirror(gp_Ax2(gp_Pnt(),gp_Dir(1,0,0)));scaling=mirror*scaling;}
        s.partPlacement=common*scaling;
        auto r=project({s},wp);verify(s,wp,one(r));
        // OCCT accumulated subshape locations and separate outer Part placement.
        gp_Trsf child;child.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(0,0,1)),0.27);child.SetTranslationPart(gp_Vec(2,3,-1));
        gp_Trsf parent;parent.SetTranslation(gp_Vec(-10,5,2));BRep_Builder b;TopoDS_Compound compound;b.MakeCompound(compound);
        b.Add(compound,s.shape.Moved(TopLoc_Location(child)));compound.Move(TopLoc_Location(parent));
        s.shape=TopoDS_Iterator(compound).Value();r=project({s},wp);verify(s,wp,one(r));
    }
    for(bool reflected:{false,true}) for(double sweep:{turn,-4.8,0.7}) {
        auto placed=edge(gp_Circ(axes(pi/3,0.73),9),5.9,sweep);
        gp_Trsf scale;scale.SetScale(gp_Pnt(),-2);
        if(reflected) {gp_Trsf mirror;mirror.SetMirror(gp_Ax2(gp_Pnt(),gp_Dir(1,0,0)));scale=mirror*scale;}
        placed.partPlacement=common*scale;
        const auto result=project({placed},wp);verify(placed,wp,one(result));
    }
    // Native ellipse with a circular projected limit still uses exact Ellipse.
    auto s=edge(gp_Elips(gp_Ax2(gp_Pnt(),gp_Dir(std::sqrt(0.75),0,0.5),gp_Dir(0.5,0,-std::sqrt(0.75))),10,5));
    auto r=project({s},plane());const auto e=std::get<Ellipse>(one(r).geometry);verify(s,plane(),r.additions[0]);
    check(near(e.majorRadius,5) && near(e.minorRadius,5),"native ellipse circular limit failed");
    auto distant=plane();distant.frame.SetLocation({1.e20,-1.e20,1.e20});
    s=edge(gp_Circ(axes(pi/4,0.3),10));s.partPlacement.SetTranslation(gp_Vec(1.e20,-1.e20,1.e20));
    r=project({s},distant);check(std::get<Ellipse>(one(r).geometry).center==Point{3,-7},"large common translation erased local center");
}
Source fromGeometry(const Geometry& geometry) {
    Entity e;e.workplaneId=plane().id;e.role=Role::Construction;e.geometry=geometry;
    return {Sketch::shape(e,plane().frame),{},"conic comparison source"};
}
void count(const Result& r,size_t n) {
    if(!r.success || r.additions.size()!=n) {
        std::cerr<<"Expected "<<n<<" additions, got "<<r.additions.size()<<"\n";
        for(const auto& d:r.diagnostics) std::cerr<<int(d.code)<<": "<<d.message.toStdString()<<'\n';
    }
    check(r.success && r.additions.size()==n,"unexpected conic integration result");
}
void conicDomains() {
    const auto wp=plane();const Ellipse e{{2,-3},10,4,0.4};const auto full=fromGeometry(e);
    auto flipped=e;flipped.axisAngle+=pi;auto periodic=e;periodic.axisAngle+=4*turn;
    count(project({full,fromGeometry(flipped),fromGeometry(periodic)},wp),1);
    auto existing=project({full},wp).additions;existing[0].visible=false;existing[0].selectable=false;
    auto r=project({fromGeometry(flipped)},wp,existing);count(r,0);
    check(!r.changed() && r.diagnostics[0].code==Code::Duplicate && r.diagnostics[0].existingEntityId==existing[0].id,"duplicate-only projection is not a no-op");
    for(auto role:{Role::Construction,Role::Profile}) {
        auto others=existing;others[0].role=role;if(role==Role::Construction) others[0].workplaneId="other-wp";
        count(project({full},wp,others),1);
    }
    for(Point shift:{Point{5,0},Point{20,0},Point{30,0}}) {
        auto loose=e;loose.axisAngle=0;auto base=loose;loose.center.x+=shift.x;
        count(project({fromGeometry(base),fromGeometry(loose)},wp),2); // Crossing, tangent and disjoint.
    }
    auto rotated=e;rotated.axisAngle+=0.7;count(project({full,fromGeometry(rotated)},wp),2);
    auto nearby=e;nearby.center.x+=0.5e-7;count(project({full,fromGeometry(nearby)},wp),1);
    nearby.center.x+=2.e-7;count(project({full,fromGeometry(nearby)},wp),2);
    Ellipse round{{},10,10-1.e-9,0};auto roundRotated=round;roundRotated.axisAngle=1.3;
    count(project({fromGeometry(round),fromGeometry(roundRotated)},wp),1); // No principal-axis ambiguity near circular limit.
    for(double sweep:{0.7,pi,4.8,-0.7,-pi,-4.8}) {
        const EllipticalArc a{e,5.9,sweep};auto phase=a;phase.ellipse.axisAngle+=pi;phase.startParameter-=pi;
        auto periodicArc=a;periodicArc.startParameter+=turn;
        const auto source=fromGeometry(a);const auto reversed=fromGeometry(reversedEllipticalArc(a));
        count(project({source,reversed,fromGeometry(phase),fromGeometry(periodicArc)},wp),1);
        const auto saved=project({source},wp).additions;count(project({reversed},wp,saved),0);
        refused(project({full,source},wp),Code::PartialOverlap);refused(project({source,full},wp),Code::PartialOverlap);
        auto hidden=saved;hidden[0].visible=false;hidden[0].selectable=false;
        const auto overlapResult=project({full},wp,hidden);refused(overlapResult,Code::PartialOverlap);
        check(overlapResult.diagnostics[0].existingEntityId==hidden[0].id && overlapResult.diagnostics[0].sourceIndex==0,"conic overlap diagnostic lost provenance");
        // An existing overlapping collection must not turn a duplicate into an error.
        auto old=existing;old.push_back(saved[0]);count(project({source},wp,old),0);
    }
    for(double start:{0.,0.3,5.9}) {
        const auto a=fromGeometry(EllipticalArc{e,start,pi});
        const auto b=fromGeometry(EllipticalArc{e,start+pi,pi});
        count(project({a,b},wp),2); // Complementary arcs share endpoints only.
    }
    count(project({fromGeometry(EllipticalArc{e,5.8,turn-5.8}),fromGeometry(EllipticalArc{e,0,0.4})},wp),2);
    count(project({fromGeometry(EllipticalArc{e,0,1.e-6}),fromGeometry(EllipticalArc{e,1.e-6,turn-1.e-6})},wp),2);
    refused(project({fromGeometry(EllipticalArc{e,5.8,1.4}),fromGeometry(EllipticalArc{e,0.1,0.5})},wp),Code::PartialOverlap);
    refused(project({fromGeometry(EllipticalArc{e,0,pi}),fromGeometry(EllipticalArc{e,pi-1.e-9,0.7})},wp),Code::PartialOverlap);
    const Ellipse circular{{},10,10,2.1};
    const auto circle=fromGeometry(Circle{{},10});count(project({circle,fromGeometry(circular)},wp),1);
    count(project({fromGeometry(circular),circle},wp),1);
    const Arc arc{{},10,0.4,2.7};const EllipticalArc ellipticalArc{circular,0.4-circular.axisAngle,2.7};
    count(project({fromGeometry(arc),fromGeometry(reversedEllipticalArc(ellipticalArc))},wp),1);
    refused(project({circle,fromGeometry(ellipticalArc)},wp),Code::PartialOverlap);
    count(project({fromGeometry(arc),fromGeometry(EllipticalArc{circular,0.4+2.7-circular.axisAngle,turn-2.7})},wp),2);
}
void ellipticalFaces() {
    const auto wp=plane();const auto a=axes(pi/4,0.3);
    const auto outer=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(a,10)).Edge()).Wire();
    const auto inner=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(a,3)).Edge()).Wire();
    BRepBuilderAPI_MakeFace make(outer);make.Add(TopoDS::Wire(inner.Reversed()));
    check(BRepCheck_Analyzer(make.Face()).IsValid(),"annulus fixture invalid");
    for(const auto& face:{make.Face(),TopoDS::Face(make.Face().Reversed())}) {
        auto result=project({{face,{},"tilted annulus"}},wp);count(result,2);
        for(const auto& e:result.additions) check(std::holds_alternative<Ellipse>(e.geometry),"tilted annulus lost exact ellipse type");
        const auto repeat=project({{face.Reversed(),{},"repeat annulus"}},wp,result.additions);count(repeat,0);
    }
    // One closed boundary containing straight, circular and elliptical pieces.
    BRepBuilderAPI_MakeWire wire;
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Elips(gp_Ax2(),6,4),-pi/2,pi/2).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0,4,0),gp_Pnt(-10,4,0)).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(-10,0,0),gp_Dir(0,0,1)),4),pi/2,3*pi/2).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(-10,-4,0),gp_Pnt(0,-4,0)).Edge());
    for(bool reverse:{false,true}) {
        const auto boundary=reverse?TopoDS::Wire(wire.Wire().Reversed()):wire.Wire();
        const auto face=BRepBuilderAPI_MakeFace(boundary).Face();check(BRepCheck_Analyzer(face).IsValid(),"mixed boundary fixture invalid");
        Source source{face,{},"mixed boundary"};
        for(bool tilt:{false,true}) {
            if(tilt) source.partPlacement.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,0,0)),0.4);
            const auto result=project({source},wp);count(result,4);
            std::vector<std::array<Point,2>> ends;
            for(const auto& entity:result.additions) {
                if(const auto* g=std::get_if<Segment>(&entity.geometry)) ends.push_back({g->start,g->end});
                if(const auto* g=std::get_if<Arc>(&entity.geometry)) ends.push_back({Point{g->center.x+g->radius*std::cos(g->startAngle),g->center.y+g->radius*std::sin(g->startAngle)},
                    Point{g->center.x+g->radius*std::cos(g->startAngle+g->sweepAngle),g->center.y+g->radius*std::sin(g->startAngle+g->sweepAngle)}});
                if(const auto* g=std::get_if<EllipticalArc>(&entity.geometry)) ends.push_back(ellipticalArcEndpoints(*g));
            }
            check(ends.size()==4,"missing stored boundary endpoints");
            for(size_t i=0;i<ends.size();++i) check(near(ends[i][1],ends[(i+1)%ends.size()][0]),"stored elliptic wire opened after projection");
            // A selected face and an explicitly selected boundary edge deduplicate together.
            const auto edge=BRepTools_WireExplorer(boundary,face).Current();
            count(project({source,{edge,source.partPlacement,"shared boundary edge"}},wp),4);
        }
    }
    // A loose-tolerance B-Rep must not permit stored conic endpoint gaps.
    auto gapped=BRepBuilderAPI_MakeFace(wire.Wire()).Face();BRep_Builder builder;
    TopoDS_Edge elliptic;
    for(BRepTools_WireExplorer edges(wire.Wire(),gapped);edges.More();edges.Next())
        if(BRepAdaptor_Curve(edges.Current()).GetType()==GeomAbs_Ellipse) {elliptic=edges.Current();break;}
    for(auto vertex:{TopExp::FirstVertex(elliptic),TopExp::LastVertex(elliptic)}) builder.UpdateVertex(vertex,1.e-3);
    TopLoc_Location location;double first,last;auto curve=BRep_Tool::Curve(elliptic,location,first,last)->Copy();
    curve->Translate(gp_Vec(0,1.e-5,0));builder.UpdateEdge(elliptic,Handle(Geom_Curve)::DownCast(curve),location,1.e-3);
    check(BRepCheck_Analyzer(gapped).IsValid(),"loose elliptical gap fixture invalid");
    refused(project({{make.Face(),{},"good annulus"},{gapped,{},"gapped ellipse"}},wp),Code::InvalidFaceBoundary);
}
void safetyAndPersistence() {
    const auto wp=plane();auto s=edge(gp_Circ(axes(pi/4),10));
    for(const auto& bad:{edge(gp_Circ(axes(pi/2),10)),edge(gp_Circ(axes(pi/2-1.e-10),10))}) refused(project({bad},wp),Code::CollapsedProjection);
    refused(project({edge(gp_Circ(axes(pi/2-1.e-9),100))},wp,{},1.e-9),Code::NumericalFailure);
    refused(project({edge(gp_Circ(axes(pi/4),1.e25))},wp),Code::NumericalFailure);
    refused(project({edge(gp_Circ(axes(pi/4),1.e-8))},wp),Code::DegenerateEdge);
    refused(project({edge(gp_Circ(axes(pi/4),10),0,1.e-6)},wp,{},1.e-4),Code::DegenerateEdge);
    auto hugeCenter=edge(gp_Circ(gp_Ax2(gp_Pnt(1.e20,0,0),axes(pi/4).Direction()),10));
    refused(project({hugeCenter},wp),Code::NumericalFailure);
    const Source hyperbola{BRepBuilderAPI_MakeEdge(gp_Hypr(gp_Ax2(),10,5),-1,1).Shape(),{},"unsupported hyperbola"};
    const Source vertex{BRepBuilderAPI_MakeVertex(gp_Pnt(5,8,4)).Shape(),{},"vertex"};
    const Source line{BRepBuilderAPI_MakeEdge(gp_Pnt(20,20,3),gp_Pnt(40,20,3)).Shape(),{},"line"};
    refused(project({vertex,s,hyperbola},wp),Code::NonlinearEdge);
    const auto mixed=project({vertex,s,line},wp);check(mixed.success && mixed.additions.size()==3,"safe mixed batch failed");
    for(bool reverseOrder:{false,true}) {
        auto duplicate=s;duplicate.shape.Reverse();
        const auto pair=reverseOrder?std::vector{duplicate,s}:std::vector{s,duplicate};
        const auto duplicates=project(pair,wp);check(duplicates.success && duplicates.additions.size()==1 && duplicates.diagnostics.size()==1,"reversed conic not deduplicated");
        const auto circle=edge(gp_Circ(gp_Ax2(),10));
        const auto loose=project(reverseOrder?std::vector{s,circle}:std::vector{circle,s},wp);
        check(loose.success && loose.additions.size()==2,"different conics incorrectly refused");
    }
    auto existing=project({edge(gp_Circ(gp_Ax2(),10))},wp).additions;existing[0].visible=false;existing[0].selectable=false;
    check(project({s},wp,existing).success,"hidden disjoint circular reference blocked ellipse projection");
    existing[0].role=Role::Profile;check(project({s},wp,existing).success,"Profile conic incorrectly participates in Construction overlap");
    existing=project({s},wp).additions;check(project({vertex},wp,existing).success,"existing ellipse blocked point projection");
    check(project({},wp,existing).success,"empty request failed conservative conic guard");
    const auto ellipseFace=BRepBuilderAPI_MakeFace(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Elips(gp_Ax2(),10,5)).Edge()).Wire()).Shape();
    check(project({{ellipseFace,{},"ellipse face"}},wp).success,"native elliptical boundary refused");
    const auto tiltedFace=BRepBuilderAPI_MakeFace(BRepBuilderAPI_MakeWire(TopoDS::Edge(s.shape)).Wire()).Shape();
    check(project({{tiltedFace,{},"tilted face"}},wp).success,"oblique circular boundary refused");
    std::ostringstream before,after;BRepTools::Write(s.shape,before);
    auto arcSource=edge(gp_Circ(axes(pi/3,0.4),8),5.8,-4.2);auto arcResult=project({arcSource},wp);
    verify(arcSource,wp,one(arcResult));auto entities=mixed.additions;entities.push_back(arcResult.additions[0]);
    BRepTools::Write(s.shape,after);check(before.str()==after.str(),"source B-Rep mutated");
    const auto retained=entities;s.shape.Nullify();arcSource.shape.Nullify();check(entities==retained,"results depend on source lifetime");
    QTemporaryDir temp;NativeDocumentSnapshot doc,loaded;doc.constructions={wp};doc.sketchEntities=entities;QString error;
    check(NativeDocumentStorage::save(temp.filePath("conics.odc"),doc,error)
        && NativeDocumentStorage::load(temp.filePath("conics.odc"),loaded,error) && loaded.sketchEntities==entities,"schema v5 lost exact projected conic values");
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {circularProjection();nativeAndTransforms();conicDomains();ellipticalFaces();safetyAndPersistence();std::cout<<"Exact conic projection, signed traversal, placements and safety guards passed.\n";return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
