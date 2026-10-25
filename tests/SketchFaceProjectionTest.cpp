#include "SketchProjection.h"
#include "SketchSnapping.h"
#include "SketchOverdraw.h"
#include "NativeDocument.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Geom_Line.hxx>
#include <Geom_Ellipse.hxx>
#include <GeomConvert.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_Circle.hxx>
#include <Geom_SurfaceOfRevolution.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Iterator.hxx>
#include <gp_Circ.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Elips.hxx>
#include <gp_Pln.hxx>
#include <gp_Sphere.hxx>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
using namespace SketchProjection;
using namespace Sketch;
constexpr double pi=std::numbers::pi,turn=2*pi;
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
Construction::Object plane() {Construction::Object p;p.id="target-wp";p.name="WP1";p.kind=Construction::Kind::Workplane;return p;}
bool near(Point a,Point b,double tol=1.e-9) {return std::hypot(a.x-b.x,a.y-b.y)<=tol;}
Source source(const TopoDS_Shape& shape,QString name="face") {return {shape,{},name};}
void ok(const Result& r,size_t count) {
    if(!r.success || r.additions.size()!=count) {
        std::cerr<<"Expected "<<count<<" additions; got "<<r.additions.size()<<", success="<<r.success<<'\n';
        for(const auto& d:r.diagnostics) std::cerr<<int(d.code)<<": "<<d.message.toStdString()<<'\n';
    }
    check(r.success && r.additions.size()==count,"unexpected face projection result");
}
void refused(const Result& r,Code code,size_t index=0) {
    check(!r.success && !r.changed() && r.additions.empty(),"failed face batch returned partial geometry");
    const bool found=std::any_of(r.diagnostics.begin(),r.diagnostics.end(),[&](const auto& d){return d.code==code && d.sourceIndex==index && d.isError() && !d.message.isEmpty();});
    if(!found) {std::cerr<<"Expected error "<<int(code)<<'\n';for(const auto& d:r.diagnostics) std::cerr<<int(d.code)<<": "<<d.message.toStdString()<<'\n';}
    check(found,"missing source-specific face failure diagnostic");
}
TopoDS_Wire rectangle(double x=0,double y=0,double width=40,double height=30,double z=0) {
    return BRepBuilderAPI_MakePolygon(gp_Pnt(x,y,z),gp_Pnt(x+width,y,z),gp_Pnt(x+width,y+height,z),gp_Pnt(x,y+height,z),true).Wire();
}
TopoDS_Wire circleWire(gp_Pnt center=gp_Pnt(),double radius=5) {
    return BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(center,gp_Dir(0,0,1)),radius)).Edge()).Wire();
}
TopoDS_Face rectangleFace(int holes=0,double z=0) {
    BRepBuilderAPI_MakeFace make(gp_Pln(gp_Pnt(0,0,z),gp_Dir(0,0,1)),rectangle(0,0,40,30,z));
    for(int i=0;i<holes;++i) make.Add(TopoDS::Wire(circleWire(gp_Pnt(10+15*i,15,z),4).Reversed()));
    const auto face=make.Face();check(BRepCheck_Analyzer(face).IsValid(),"invalid rectangle/hole test fixture");return face;
}
TopoDS_Edge firstEdge(const TopoDS_Shape& s) {return TopoDS::Edge(TopExp_Explorer(s,TopAbs_EDGE).Current());}
size_t count(const Result& r,size_t variant) {return std::count_if(r.additions.begin(),r.additions.end(),[&](const auto& e){return e.geometry.index()==variant;});}
std::string brep(const TopoDS_Shape& s) {std::ostringstream out;BRepTools::Write(s,out);return out.str();}
gp_Trsf transform() {gp_Trsf t;t.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),0.63);t.SetTranslationPart(gp_Vec(25,-16,71));return t;}
Point projected(gp_Pnt p,const gp_Trsf& part,const Construction::Object& wp) {
    const auto world=p.Transformed(part);const gp_Vec d(wp.frame.Location(),world);
    return {d.Dot(gp_Vec(wp.frame.XDirection())),d.Dot(gp_Vec(wp.frame.YDirection()))};
}
std::vector<Point> ends(const Geometry& g) {
    if(const auto* s=std::get_if<Segment>(&g)) return {s->start,s->end};
    if(const auto* a=std::get_if<Arc>(&g)) return {{a->center.x+a->radius*std::cos(a->startAngle),a->center.y+a->radius*std::sin(a->startAngle)},
        {a->center.x+a->radius*std::cos(a->startAngle+a->sweepAngle),a->center.y+a->radius*std::sin(a->startAngle+a->sweepAngle)}};
    return {};
}
void closedEndpoints(const Result& r) {
    std::vector<Point> points;for(const auto& e:r.additions) {const auto pair=ends(e.geometry);points.insert(points.end(),pair.begin(),pair.end());}
    for(const auto p:points) check(std::count_if(points.begin(),points.end(),[&](Point q){return near(p,q);})==2,"a projected contour was opened or joined to another contour");
}
void contours() {
    const auto wp=plane();
    for(int holes:{0,1,2}) for(double z:{0.,54.,-81.}) {
        auto face=rectangleFace(holes,z);const auto before=brep(face);const auto r=project({source(face)},wp);ok(r,4+holes);
        check(count(r,0)==4 && count(r,2)==size_t(holes) && count(r,4)==0,"inner wires lost or endpoint points added");closedEndpoints(r);
        for(int i=0;i<holes;++i) check(std::any_of(r.additions.begin(),r.additions.end(),[&](const auto& e){const auto* c=std::get_if<Circle>(&e.geometry);return c && c->center==Point{10.+15*i,15} && c->radius==4;}),"hole parameters changed");
        check(brep(face)==before,"face projection modified source topology/geometric data");
        auto reversed=source(face.Reversed());auto noOp=project({reversed},wp,r.additions);ok(noOp,0);check(!noOp.changed(),"reversed face generated a duplicate contour");
    }
    const auto disk=BRepBuilderAPI_MakeFace(circleWire(gp_Pnt(3,-4,7),8)).Face();auto r=project({source(disk)},wp);ok(r,1);
    check(std::get<Circle>(r.additions[0].geometry)==Circle{{3,-4},8},"disk became bounding box/silhouette/extra points");
    // A D contour is a diameter plus a semicircle, not a polygonal approximation.
    const gp_Circ support(gp_Ax2(gp_Pnt(),gp_Dir(0,0,1)),10);
    const auto a=BRepBuilderAPI_MakeEdge(support,0,pi).Edge();
    const auto b=BRepBuilderAPI_MakeEdge(gp_Pnt(-10,0,0),gp_Pnt(10,0,0)).Edge();
    const auto mixedWire=BRepBuilderAPI_MakeWire(a,b).Wire();
    const auto mixedFace=BRepBuilderAPI_MakeFace(mixedWire).Face();r=project({source(mixedFace)},wp);ok(r,2);closedEndpoints(r);
    check(count(r,0)==1 && count(r,3)==1 && std::abs(std::get<Arc>(r.additions[0].geometry).sweepAngle)==pi,"mixed analytic boundary lost its arc");
    // A wire built in the opposite direction still has every boundary piece.
    const auto reverseWire=TopoDS::Wire(mixedWire.Reversed());const auto reverseFace=BRepBuilderAPI_MakeFace(reverseWire).Face();
    ok(project({source(reverseFace)},wp,r.additions),0);
    // Seam-crossing small arc and two straight segments, with a genuine vertex
    // at the center: preserve the short 20-degree route across 0/360.
    const double start=350*pi/180,finish=370*pi/180;
    const auto curved=BRepBuilderAPI_MakeEdge(support,start,finish).Edge();
    const auto av=BRep_Tool::Pnt(TopExp::FirstVertex(curved)),bv=BRep_Tool::Pnt(TopExp::LastVertex(curved));
    BRepBuilderAPI_MakeWire sector(curved,BRepBuilderAPI_MakeEdge(bv,gp_Pnt()).Edge(),BRepBuilderAPI_MakeEdge(gp_Pnt(),av).Edge());
    r=project({source(BRepBuilderAPI_MakeFace(sector.Wire()).Face())},wp);ok(r,3);closedEndpoints(r);
    const auto piece=std::find_if(r.additions.begin(),r.additions.end(),[](const auto& e){return std::holds_alternative<Arc>(e.geometry);});
    check(std::abs(std::abs(std::get<Arc>(piece->geometry).sweepAngle)-20*pi/180)<1.e-12,"face arc switched to complementary route");
}
void placements() {
    const auto original=rectangleFace(2,19);const auto rigid=transform();auto wp=plane();wp.frame.Transform(rigid);wp.frame.SetLocation(wp.frame.Location().Translated(gp_Vec(wp.frame.Direction())*41));
    wp.displayOffset=gp_Vec(100,200,0);wp.extent=0.01;auto s=source(original);s.partPlacement=rigid;
    auto r=project({s},wp);ok(r,6);closedEndpoints(r);check(near(std::get<Segment>(r.additions[0].geometry).start,{0,0}),"normal offset/display bounds changed contour XY");
    gp_Trsf child;child.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(0,0,1)),0.31);child.SetTranslationPart(gp_Vec(8,12,-3));
    gp_Trsf parent;parent.SetTranslation(gp_Vec(-5,4,35));BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);
    builder.Add(compound,original.Moved(TopLoc_Location(child)));compound.Move(TopLoc_Location(parent));
    s.shape=TopoDS_Iterator(compound).Value();r=project({s},wp);ok(r,6);closedEndpoints(r);
    const auto expected=projected(gp_Pnt(0,0,19).Transformed(child).Transformed(parent),rigid,wp);
    check(near(std::get<Segment>(r.additions[0].geometry).start,expected),"face/nested/Part location was doubled or lost");
    auto baked=s;baked.shape=baked.shape.Moved(TopLoc_Location(rigid));baked.partPlacement=gp_Trsf();const auto world=project({baked},wp);ok(world,6);
    for(size_t i=0;i<r.additions.size();++i) {
        const auto a=ends(r.additions[i].geometry),b=ends(world.additions[i].geometry);
        for(size_t j=0;j<a.size();++j) check(near(a[j],b[j]),"baked world-space face differs from separate placement");
    }
    auto inverse=wp;inverse.frame=gp_Ax3(wp.frame.Location(),-wp.frame.Direction(),wp.frame.XDirection());ok(project({s},inverse),6);
    // Arbitrary oblique projection is still exact for straight boundary edges.
    auto sloping=source(rectangleFace());sloping.partPlacement=rigid;r=project({sloping},plane());ok(r,4);closedEndpoints(r);
    const auto start=projected(gp_Pnt(),rigid,plane());check(near(std::get<Segment>(r.additions[0].geometry).start,start),"oblique linear face projection incorrect");
    auto huge=plane();huge.frame.SetLocation(gp_Pnt(1.e20,-1.e20,1.e20));auto far=source(rectangleFace(1));far.partPlacement.SetTranslation(gp_Vec(1.e20,-1.e20,1.e20));
    r=project({far},huge);ok(r,5);check(std::get<Segment>(r.additions[0].geometry).start==Point{0,0},"common huge translation erased small face coordinates");
}
void existingConstructionOverlap() {
    auto wp=plane();wp.frame.SetLocation(gp_Pnt(0,0,70));
    Entity reference;reference.workplaneId=wp.id;reference.role=Role::Construction;
    reference.geometry=Segment{{-11,-59},{89,-59}};
    for(const auto [x1,x2]:{std::pair{-17.224,79.369},std::pair{-1.694,110.427}}) {
        const auto face=source(BRepBuilderAPI_MakeFace(rectangle(x1,-59,x2-x1,20,30)).Face(),"reported face");
        for(bool visible:{false,true}) for(bool selectable:{false,true}) {
            reference.visible=visible;reference.selectable=selectable;
            const std::vector before{reference};const auto result=project({face},wp,before);ok(result,4);closedEndpoints(result);
            check(before==std::vector{reference},"face projection mutated the old Construction line");
            const auto boundary=std::find_if(result.additions.begin(),result.additions.end(),[&](const auto& entity) {
                const auto* line=std::get_if<Segment>(&entity.geometry);
                return line && ((near(line->start,{x1,-59}) && near(line->end,{x2,-59}))
                    || (near(line->end,{x1,-59}) && near(line->start,{x2,-59})));
            });
            check(boundary!=result.additions.end(),"overlapping source boundary was trimmed or omitted");
            auto saved=before;saved.insert(saved.end(),result.additions.begin(),result.additions.end());
            const auto repeat=project({face},wp,saved);ok(repeat,0);check(!repeat.changed(),"complete duplicate face is not a no-op");
            const auto edge=source(firstEdge(face.shape));
            refused(project({edge},wp,before),Code::PartialOverlap);
            for(const auto& batch:{std::vector{edge,face},std::vector{face,edge}}) ok(project(batch,wp,before),4);
            const auto invalid=project({face,{}},wp,before);refused(invalid,Code::NullSource,1);
        }
    }
    // A full duplicate edge is reused whole; a partial reference never trims it.
    const auto face=source(BRepBuilderAPI_MakeFace(rectangle(-17.224,-59,96.593,20,30)).Face());
    const auto all=project({face},wp).additions;
    ok(project({face},wp,{all.front(),reference}),3);
    auto edgeOn=source(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(),gp_Dir(1,0,0)),0,20,0,30).Face());
    refused(project({face,edgeOn},wp,{reference}),Code::CollapsedProjection,1);
    // The policy also preserves an entire elliptic boundary arc against an
    // existing full conic, without weakening standalone edge overlap refusal.
    const gp_Elips support(gp_Ax2(gp_Pnt(0,0,30),gp_Dir(0,0,1)),10,4);
    const auto arc=BRepBuilderAPI_MakeEdge(support,0,pi).Edge();
    const auto chord=BRepBuilderAPI_MakeEdge(gp_Pnt(-10,0,30),gp_Pnt(10,0,30)).Edge();
    const auto half=source(BRepBuilderAPI_MakeFace(BRepBuilderAPI_MakeWire(arc,chord).Wire()).Face());
    Entity ellipse;ellipse.workplaneId=wp.id;ellipse.role=Role::Construction;ellipse.geometry=Ellipse{{},10,4,0};
    const auto result=project({half},wp,{ellipse});ok(result,2);
    check(count(result,6)==1 && count(result,0)==1,"existing conic changed full face-boundary output");
    refused(project({source(arc)},wp,{ellipse}),Code::PartialOverlap);
}
void duplicatesAndBatches() {
    const auto wp=plane();auto face=source(rectangleFace(1));auto edge=source(firstEdge(face.shape),"individual edge");
    auto reversed=edge;reversed.shape.Reverse();auto r=project({face,face,reversed,edge},wp);ok(r,5);
    check(r.diagnostics.size()==7 && std::all_of(r.diagnostics.begin(),r.diagnostics.end(),[](const auto& d){return d.code==Code::Duplicate && !d.isError();}),"repeated faces/edges did not deduplicate by projected geometry");
    auto existing=r.additions;existing[0].visible=false;existing[1].selectable=false;const auto before=existing;
    r=project({face},wp,existing);ok(r,0);check(!r.changed() && existing==before,"face no-op changed existing values");
    auto adjacent=source(BRepBuilderAPI_MakeFace(rectangle(40,0,20,30)).Face());
    // Distinct topology with a geometrically shared edge; reversed shared span.
    r=project({source(rectangleFace()),adjacent},wp);ok(r,7);
    // Also use one genuinely shared TopoDS edge in both faces.
    const auto shared=firstEdge(rectangleFace());const auto p=BRep_Tool::Pnt(TopExp::FirstVertex(shared)),q=BRep_Tool::Pnt(TopExp::LastVertex(shared));
    const auto down=gp_Vec(0,-20,0);
    BRepBuilderAPI_MakeWire other(TopoDS::Edge(shared.Reversed()),BRepBuilderAPI_MakeEdge(p,p.Translated(down)).Edge(),
        BRepBuilderAPI_MakeEdge(p.Translated(down),q.Translated(down)).Edge(),BRepBuilderAPI_MakeEdge(q.Translated(down),q).Edge());
    // Build the first face from a wire retaining that exact shared edge.
    BRepBuilderAPI_MakeWire upper(shared,BRepBuilderAPI_MakeEdge(q,q.Translated(-down)).Edge(),
        BRepBuilderAPI_MakeEdge(q.Translated(-down),p.Translated(-down)).Edge(),BRepBuilderAPI_MakeEdge(p.Translated(-down),p).Edge());
    const auto f1=BRepBuilderAPI_MakeFace(upper.Wire()).Face(),f2=BRepBuilderAPI_MakeFace(other.Wire()).Face();
    check(firstEdge(f1).IsSame(shared),"shared edge test fixture lost topology");ok(project({source(f1),source(f2)},wp),7);
    auto partA=source(rectangleFace(),"Part A");partA.partPlacement.SetTranslation(gp_Vec(-100,0,5));
    auto partB=source(rectangleFace(1),"Part B");partB.partPlacement.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(0,0,1)),pi/2);
    partB.partPlacement.SetTranslationPart(gp_Vec(100,0,11));r=project({partA,partB},wp);ok(r,9);closedEndpoints(r);
    // Partial collinear overlap is refused, without trimming either contour.
    auto overlapping=source(BRepBuilderAPI_MakeFace(rectangle(10,0,40,30)).Face());
    refused(project({source(rectangleFace()),overlapping},wp),Code::PartialOverlap,1);
    Source vertex=source(BRepBuilderAPI_MakeVertex(gp_Pnt(100,100,80)).Shape(),"vertex");
    Source circle=source(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(100,0,3),gp_Dir(0,0,1)),4)).Shape(),"circle");
    Source arc=source(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(100,20,3),gp_Dir(0,0,1)),4),0,pi/2).Shape(),"arc");
    Source line=source(BRepBuilderAPI_MakeEdge(gp_Pnt(0,100,9),gp_Pnt(40,100,19)).Shape(),"line");
    r=project({vertex,line,circle,arc,face},wp);ok(r,9);
    check(count(r,4)==1 && count(r,3)==1 && count(r,2)==2 && count(r,0)==5,"mixed source types not kept independently");
    std::set<QString> ids;for(const auto& e:r.additions) check(e.workplaneId==wp.id && e.role==Role::Construction && !QUuid(e.id).isNull() && ids.insert(e.id).second,"boundary identity/ownership/role invalid");
}
void seamsAndPoles() {
    const auto wp=plane();const gp_Cone cone(gp_Ax3(),pi/6,5);
    auto frustum=BRepBuilderAPI_MakeFace(cone,0,turn,0,10).Face();
    check(BRepCheck_Analyzer(frustum).IsValid(),"invalid seam face fixture");
    size_t seams=0;for(TopExp_Explorer e(frustum,TopAbs_EDGE);e.More();e.Next()) if(BRep_Tool::IsClosed(TopoDS::Edge(e.Current()),frustum)) ++seams;
    check(seams==2,"frustum fixture does not expose repeated seam occurrences");
    auto r=project({source(frustum)},wp);ok(r,3);check(count(r,2)==2 && count(r,0)==1,"seam occurrences created duplicate or missing geometry");
    check(std::count_if(r.diagnostics.begin(),r.diagnostics.end(),[](const auto& d){return d.code==Code::Duplicate;})==1,"seam duplicate not diagnosed");
    const auto apex=BRepBuilderAPI_MakeFace(cone,0,turn,-5/std::sin(pi/6),0).Face();
    r=project({source(apex)},wp);ok(r,2);check(count(r,4)==0,"cone apex created an unintended point");
    check(std::any_of(r.diagnostics.begin(),r.diagnostics.end(),[](const auto& d){return d.code==Code::DegenerateSkipped && !d.isError();}),"certified cone pole omission not reported");
    auto xz=plane();xz.frame=gp_Ax3(gp_Pnt(),gp_Dir(0,-1,0),gp_Dir(1,0,0));
    const auto halfSphere=BRepBuilderAPI_MakeFace(gp_Sphere(gp_Ax3(),10),0,pi,-pi/2,pi/2).Face();
    r=project({source(halfSphere)},xz);ok(r,2);closedEndpoints(r);check(count(r,3)==2 && count(r,4)==0,"spherical poles lost arcs or added points");
    check(std::count_if(r.diagnostics.begin(),r.diagnostics.end(),[](const auto& d){return d.code==Code::DegenerateSkipped;})==2,"spherical degenerate edges not handled individually");
    const auto placement=transform();gp_Trsf child;child.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(0,0,1)),0.31);child.SetTranslationPart(gp_Vec(12,-8,35));
    for(const auto& entry:{std::pair{apex,wp},std::pair{halfSphere,xz}}) {
        auto located=source(entry.first.Moved(TopLoc_Location(child)));located.partPlacement=placement;
        auto target=entry.second;target.frame.Transform(child);target.frame.Transform(placement);
        r=project({located},target);ok(r,2);
        check(std::any_of(r.diagnostics.begin(),r.diagnostics.end(),[](const auto& d){return d.code==Code::DegenerateSkipped;}),"located surface/Part lost pole certification");
    }
    auto scaled=source(apex);scaled.partPlacement.SetScale(gp_Pnt(),2);r=project({scaled},wp);ok(r,2);
    const auto base=std::find_if(r.additions.begin(),r.additions.end(),[](const auto& e){return std::holds_alternative<Circle>(e.geometry);});
    check(std::abs(std::get<Circle>(base->geometry).radius-10)<1.e-10,"scaled cone pole/boundary radius incorrect");
    // An ordinary or seam line collapsing under projection is still refused;
    // the pole exception cannot be used to hide a nonzero 3D curve.
    const auto cylinder=BRepBuilderAPI_MakeFace(gp_Cylinder(gp_Ax3(),5),0,turn,0,10).Face();
    refused(project({source(cylinder)},wp),Code::CollapsedProjection);
    auto flagged=rectangleFace();BRep_Builder builder;builder.Degenerated(firstEdge(flagged),true);
    const auto invalid=project({source(flagged)},wp);check(!invalid.success && invalid.additions.empty(),"false degenerate flag silently dropped a real boundary");
    // The same sphere-like locus supplied as a generic surface of revolution
    // is intentionally outside our analytic pole certificate. Coincident
    // vertices alone must not let an unverified degenerate edge disappear.
    Handle(Geom_Circle) meridian=new Geom_Circle(gp_Circ(gp_Ax2(gp_Pnt(),gp_Dir(0,-1,0),gp_Dir(1,0,0)),10));
    Handle(Geom_SurfaceOfRevolution) revolution=new Geom_SurfaceOfRevolution(meridian,gp_Ax1(gp_Pnt(),gp_Dir(0,0,1)));
    const auto generic=BRepBuilderAPI_MakeFace(revolution,0,pi,-pi/2,pi/2,1.e-7).Face();
    check(BRepCheck_Analyzer(generic).IsValid(),"unverifiable-pole fixture is not valid B-Rep");
    refused(project({source(generic)},xz),Code::DegenerateEdge);
}
void failuresAndTolerance() {
    auto wp=plane();
    const auto ellipse=BRepBuilderAPI_MakeFace(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Elips(gp_Ax2(),10,5)).Edge()).Wire()).Face();
    auto r=project({source(ellipse)},wp);ok(r,1);
    const auto unsupported=BRepBuilderAPI_MakeFace(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(
        GeomConvert::CurveToBSplineCurve(new Geom_Ellipse(gp_Elips(gp_Ax2(),10,5)))).Edge()).Wire()).Face();
    r=project({source(unsupported)},wp);refused(r,Code::NonlinearEdge);check(r.diagnostics.front().message.contains("wire 1, edge 1"),"edge diagnostic lacks face boundary context");
    const auto ellipseHole=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Elips(gp_Ax2(gp_Pnt(20,15,0),gp_Dir(0,0,1)),5,3)).Edge()).Wire();
    BRepBuilderAPI_MakeFace withBadHole(gp_Pln(),rectangle());withBadHole.Add(TopoDS::Wire(ellipseHole.Reversed()));
    check(BRepCheck_Analyzer(withBadHole.Face()).IsValid(),"unsupported inner contour fixture is invalid");
    r=project({source(withBadHole.Face())},wp);ok(r,5);
    const auto unsupportedHole=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(GeomConvert::CurveToBSplineCurve(
        new Geom_Ellipse(gp_Elips(gp_Ax2(gp_Pnt(20,15,0),gp_Dir(0,0,1)),5,3)))).Edge()).Wire();
    BRepBuilderAPI_MakeFace badInner(gp_Pln(),rectangle());badInner.Add(TopoDS::Wire(unsupportedHole.Reversed()));
    check(BRepCheck_Analyzer(badInner.Face()).IsValid(),"unsupported inner contour fixture is invalid");
    r=project({source(badInner.Face())},wp);refused(r,Code::NonlinearEdge);
    check(r.diagnostics.front().message.contains("wire 2"),"an unsupported inner wire was ignored or misdiagnosed");
    const auto disk=BRepBuilderAPI_MakeFace(circleWire()).Face();auto tilted=source(disk);tilted.partPlacement.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,0,0)),0.2);
    r=project({tilted},wp);ok(r,1);check(std::holds_alternative<Ellipse>(r.additions.front().geometry),"tilted disk did not produce an exact ellipse");
    auto edgeOn=plane();edgeOn.frame=gp_Ax3(gp_Pnt(),gp_Dir(1,0,0),gp_Dir(0,1,0));
    refused(project({source(rectangleFace())},edgeOn),Code::CollapsedProjection);
    edgeOn.frame=gp_Ax3(gp_Pnt(),gp_Dir(1,1,0),gp_Dir(0,0,1));refused(project({source(rectangleFace())},edgeOn),Code::CollapsedProjection);
    for(double size:{1.e-5,1.e6}) {
        const auto face=BRepBuilderAPI_MakeFace(gp_Pln(),0,size,0,size).Face();r=project({source(face)},wp,{},1.e-9);ok(r,4);closedEndpoints(r);
    }
    const auto tiny=BRepBuilderAPI_MakeFace(gp_Pln(),0,1.e-6,0,10).Face();
    refused(project({source(tiny)},wp,{},1.e-5),Code::DegenerateEdge);
    // Topological validity with loose OCCT tolerances must not authorize a
    // projected gap larger than the caller's stricter geometric tolerance.
    auto gapped=rectangleFace();BRep_Builder builder;
    for(TopExp_Explorer e(gapped,TopAbs_VERTEX);e.More();e.Next()) builder.UpdateVertex(TopoDS::Vertex(e.Current()),1.e-3);
    const auto edge=firstEdge(gapped);double first,last;TopLoc_Location location;auto c=BRep_Tool::Curve(edge,location,first,last)->Copy();
    c->Translate(gp_Vec(0,1.e-5,0));builder.UpdateEdge(edge,Handle(Geom_Curve)::DownCast(c),location,1.e-3);
    check(BRepCheck_Analyzer(gapped).IsValid(),"loose-tolerance gap fixture not topologically valid");
    refused(project({source(gapped)},wp),Code::InvalidFaceBoundary);
    Source unbounded=source(BRepBuilderAPI_MakeFace(gp_Pln()).Face());refused(project({unbounded},wp),Code::InvalidFaceBoundary);
    Source vertex=source(BRepBuilderAPI_MakeVertex(gp_Pnt(4,5,6)).Shape(),"valid vertex");
    for(size_t invalidIndex:{0u,2u}) {
        std::vector batch{source(rectangleFace(2)),vertex,source(rectangleFace())};batch[invalidIndex]=source(unsupported,"unsupported source");
        r=project(batch,wp);refused(r,Code::NonlinearEdge,invalidIndex);
    }
    auto nearly=source(rectangleFace());nearly.partPlacement.SetTranslation(gp_Vec(0.5e-7,0,50));ok(project({source(rectangleFace()),nearly},wp),4);
    nearly.partPlacement.SetTranslation(gp_Vec(2.e-7,0,50));refused(project({source(rectangleFace()),nearly},wp),Code::PartialOverlap,1);
}
void storageAndIndependence() {
    auto wp=plane();auto face=source(rectangleFace(2));const auto before=brep(face.shape);auto r=project({face},wp);ok(r,6);
    const auto saved=r.additions;check(brep(face.shape)==before,"source face modified during projection");face.shape.Nullify();face.partPlacement=transform();wp.frame.Transform(transform());
    check(r.additions==saved,"2D boundaries depend on source shape/Part/Workplane changes");
    QTemporaryDir directory;QString error;NativeDocumentSnapshot document,loaded;document.constructions={wp};document.sketchEntities=saved;
    const auto path=directory.filePath("face-v3.odc");check(NativeDocumentStorage::save(path,document,error) && NativeDocumentStorage::load(path,loaded,error) && loaded.sketchEntities==saved,"exact face geometry failed native v3 roundtrip");
    document.sketchEntities.push_back(project({source(BRepBuilderAPI_MakeVertex(gp_Pnt(4,9,40)).Shape())},plane()).additions.front());
    check(NativeDocumentStorage::save(directory.filePath("face-v4.odc"),document,error) && NativeDocumentStorage::load(directory.filePath("face-v4.odc"),loaded,error)
        && loaded.sketchEntities==document.sketchEntities,"face+point values failed native v4 roundtrip");
    check(!snapTargets(saved,wp.id,{0,0}).empty(),"boundary geometry is unavailable for snapping");
    OverdrawGraph graph;check(overdrawCandidates(saved,wp.id,graph,error),"face boundary values incompatible with Overdraw");
    check(NativeDocumentStorage::sketchSchemaVersion(saved)==3 && NativeDocumentStorage::sketchSchemaVersion(document.sketchEntities)==4,"face projection changed required native schemas");
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {contours();placements();existingConstructionOverlap();duplicatesAndBatches();seamsAndPoles();failuresAndTolerance();storageAndIndependence();
        std::cout<<"Exact face boundaries, holes, placements, seams/poles, atomic batches and native storage passed.\n";return 0;
    } catch(const Standard_Failure& e) {std::cerr<<"OCCT fixture failure: "<<e.GetMessageString()<<'\n';return 1;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
