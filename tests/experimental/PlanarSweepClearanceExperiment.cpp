// F2.9G.2 isolated experiment: sweep-clearance proof against the certified
// Move Face route. Synthetic, source-independent fixtures built in code. Exits
// non-zero on any false provisional admission. No production behavior changes.
#include "PlanarSweepClearance.h"
#include "BRepProvisionalProviders.h"
#include "MoveFaceGeometry.h"
#include "BRepSweepConstruction.h"
#include "../ObroundExtrusionFixture.h"
#include "../EllipticalExtrusionFixture.h"
#include "../SplineExtrusionFixture.h"
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepLib.hxx>
#include <TopLoc_Location.hxx>
#include <Bnd_Box.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <iomanip>
#include <iostream>
#include <map>
#include <vector>

namespace {
using namespace SweepClearance;
TopoDS_Shape prism(const std::vector<gp_Pnt>& profile,double height) {
    BRepBuilderAPI_MakePolygon polygon;for(const auto& p:profile)polygon.Add(p);polygon.Close();
    return BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(polygon.Wire()).Face(),gp_Vec(0,0,height),true,true).Shape();
}
// Fixture builders: an OCCT construction failure yields a null shape, which the
// harness reports as InvalidInput (never as proof evidence).
TopoDS_Shape box(gp_Pnt a,gp_Pnt b){try{return BRepPrimAPI_MakeBox(a,b).Shape();}catch(const Standard_Failure&){return {};}}
TopoDS_Shape fuse(const TopoDS_Shape& a,const TopoDS_Shape& b){if(a.IsNull()||b.IsNull())return {};
    try{BRepAlgoAPI_Fuse f(a,b);f.Build();return f.IsDone()?f.Shape():TopoDS_Shape();}catch(const Standard_Failure&){return {};}}
TopoDS_Shape cut(const TopoDS_Shape& a,const TopoDS_Shape& b){if(a.IsNull()||b.IsNull())return {};
    try{BRepAlgoAPI_Cut c(a,b);c.Build();return c.IsDone()?c.Shape():TopoDS_Shape();}catch(const Standard_Failure&){return {};}}
TopoDS_Shape cylinder(gp_Pnt base,gp_Dir axis,double r,double h){return BRepPrimAPI_MakeCylinder(gp_Ax2(base,axis),r,h).Shape();}
// Planar Face whose outward normal is parallel to `dir` and that contains `p`.
TopoDS_Face pick(const TopoDS_Shape& s,const gp_Dir& dir,const gp_Pnt& p) {
    if(s.IsNull())return {};
    const auto vertex=BRepBuilderAPI_MakeVertex(p).Vertex();
    for(TopExp_Explorer f(s,TopAbs_FACE);f.More();f.Next()) {
        const auto face=TopoDS::Face(f.Current());const BRepAdaptor_Surface a(face);
        if(a.GetType()!=GeomAbs_Plane)continue;
        auto n=a.Plane().Axis().Direction();if(face.Orientation()==TopAbs_REVERSED)n.Reverse();
        if(n.Dot(dir)<1-1.e-9)continue;
        BRepExtrema_DistShapeShape distance(face,vertex);
        if(distance.IsDone()&&distance.Value()<1.e-7)return face;
    }
    return {};
}
std::string bbox(const TopoDS_Shape& s) {
    Bnd_Box b;BRepBndLib::AddOptimal(s,b,false,false);double v[6];b.Get(v[0],v[1],v[2],v[3],v[4],v[5]);
    std::ostringstream o;o<<std::setprecision(10);for(int i=0;i<6;++i)o<<v[i]<<(i<5?",":"");return o.str();
}
bool sameBox(const TopoDS_Shape& a,const TopoDS_Shape& b) {
    Bnd_Box x,y;BRepBndLib::AddOptimal(a,x,false,false);BRepBndLib::AddOptimal(b,y,false,false);
    double p[6],q[6];x.Get(p[0],p[1],p[2],p[3],p[4],p[5]);y.Get(q[0],q[1],q[2],q[3],q[4],q[5]);
    for(int i=0;i<6;++i)if(std::abs(p[i]-q[i])>1.e-6)return false;return true;
}
struct Fixture {std::string name;TopoDS_Shape shape;TopoDS_Face face;std::vector<double> values;std::string group;};
double ms(std::chrono::steady_clock::time_point t){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();}
}

int main() {
    std::cout<<std::setprecision(12);
    const gp_Dir Z(0,0,1);
    const auto B=box({0,0,0},{40,30,20});
    std::vector<Fixture> fx;
    auto add=[&](std::string name,const TopoDS_Shape& s,const gp_Dir& dir,const gp_Pnt& p,std::vector<double> d,std::string group) {
        fx.push_back({name,s,pick(s,dir,p),d,group});
    };
    const std::vector<double> common{15,0.5,1.e-6,-0.5,-5};
    // Representative geometry.
    add("box",B,Z,{20,15,20},{15,0.5,1.e-6,-0.5,-5,-19.99,-19.99999,-19.999997,-19.999999,-20,-25},"representative");
    add("translated box",box({-10,5,3},{7.5,12.25,9.125}),Z,{0,8,9.125},{15,-5,2.25,-6,-6.124997,-6.125},"representative");
    add("rectangle extrusion",prism({{0,0,0},{100,0,0},{100,50,0},{0,50,0}},20),Z,{50,25,20},common,"representative");
    {std::vector<gp_Pnt> hex;for(int i=0;i<6;++i)hex.push_back({30*std::cos(i*M_PI/3),30*std::sin(i*M_PI/3),0});
     add("convex hexagon extrusion",prism(hex,20),Z,{0,0,20},common,"representative");}
    add("non-convex L extrusion",prism({{0,0,0},{40,0,0},{40,10,0},{10,10,0},{10,30,0},{0,30,0}},20),Z,{5,5,20},common,"representative");
    add("non-convex U extrusion",prism({{0,0,0},{40,0,0},{40,30,0},{30,30,0},{30,10,0},{10,10,0},{10,30,0},{0,30,0}},20),Z,{20,5,20},common,"representative");
    add("obround with hole",ObroundFixture::prism(),ObroundFixture::axis(),ObroundFixture::world(0,15,20),{15,-5,-19.5},"representative");
    add("cylinder cap (curved wall)",BRepPrimAPI_MakeCylinder(10,20).Shape(),Z,{0,0,20},common,"representative");
    add("through-hole box",cut(B,cylinder({20,15,-1},Z,5,30)),Z,{5,5,20},common,"representative");
    add("blind hole from cap (depth 8)",cut(B,cylinder({20,15,12},Z,5,10)),Z,{5,5,20},{5,-5,-7.999999,-8,-10},"representative");
    add("blind hole from bottom (top at depth 8)",cut(B,cylinder({20,15,-1},Z,5,13)),Z,{20,15,20},{5,-5,-7.99999,-7.999997,-7.999999,-8,-10},"representative");
    add("boss on cap",fuse(B,cylinder({20,15,20},Z,5,5)),Z,{5,5,20},{5,-5,-15},"representative");
    {const auto L=fuse(box({0,0,0},{40,30,20}),box({0,0,20},{10,30,40}));
     add("stepped: tall top",L,Z,{5,15,40},{5,-5,-19.99999,-20,-25},"representative");
     add("stepped: lower step",L,Z,{25,15,20},{5,-5},"representative");}
    add("split cap (two coplanar Faces)",fuse(box({0,0,0},{20,30,20}),box({20,0,0},{40,30,20})),Z,{10,15,20},{5,-5},"representative");
    {BRepFilletAPI_MakeFillet fillet(B);TopoDS_Edge vertical;
     for(TopExp_Explorer e(B,TopAbs_EDGE);e.More();e.Next()){BRepAdaptor_Curve c(TopoDS::Edge(e.Current()));
        if(c.GetType()==GeomAbs_Line&&c.Line().Direction().IsParallel(Z,1.e-9)){vertical=TopoDS::Edge(e.Current());break;}}
     fillet.Add(4,vertical);fillet.Build();
     add("mixed: vertical-edge fillet",fillet.IsDone()?fillet.Shape():TopoDS_Shape(),Z,{20,15,20},common,"representative");}
    {BRepFilletAPI_MakeFillet fillet(B);const auto top=pick(B,Z,{20,15,20});TopoDS_Edge edge;
     for(TopExp_Explorer e(top,TopAbs_EDGE);e.More();e.Next()){edge=TopoDS::Edge(e.Current());break;}
     fillet.Add(3,edge);fillet.Build();
     add("mixed: top-edge fillet",fillet.IsDone()?fillet.Shape():TopoDS_Shape(),Z,{20,15,20},{5,-5},"representative");}
    {BRepFilletAPI_MakeChamfer chamfer(B);const auto top=pick(B,Z,{20,15,20});TopoDS_Edge edge;
     for(TopExp_Explorer e(top,TopAbs_EDGE);e.More();e.Next()){edge=TopoDS::Edge(e.Current());break;}
     TopTools_IndexedDataMapOfShapeListOfShape map;TopExp::MapShapesAndAncestors(B,TopAbs_EDGE,TopAbs_FACE,map);
     chamfer.Add(3,3,edge,top);chamfer.Build();
     add("mixed: top-edge chamfer",chamfer.IsDone()?chamfer.Shape():TopoDS_Shape(),Z,{20,15,20},{5,-5},"representative");}
    add("elliptical extrusion",EllipticalFixture::prism(),Z,{0,0,20},{5,-5},"representative");
    {const auto spline=SplineFixture::solid();fx.push_back({"spline-wall extrusion",spline,SplineFixture::cap(spline,20),{5,-5},"representative"});}
    {   // Analytic cap region over a NURBS-converted lower body: exercises the pole-hull bound on non-wall Faces.
        const auto upper=box({0,0,10},{40,30,20});const auto lower=BRepBuilderAPI_NurbsConvert(box({0,0,0},{40,30,10}),true).Shape();
        add("mixed: analytic top over B-spline body",fuse(upper,lower),Z,{20,15,20},{5,-5,-9.99999,-10},"representative");}
    {gp_Trsf r;r.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,0,0)),0.5);
     add("rotated box",BRepBuilderAPI_Transform(B,r,true).Shape(),Z.Transformed(r),gp_Pnt(20,15,20).Transformed(r),{5,-5},"representative");}
    add("cone frustum (non-invariant wall)",BRepPrimAPI_MakeCone(20,10,20).Shape(),Z,{0,0,20},{5,-5},"representative");
    // Adversarial.
    add("protrusion 1e-5 on cap",fuse(B,box({15,10,20},{25,20,20.00001})),Z,{5,5,20},{5,-5},"adversarial");
    add("protrusion 1e-6 on cap",fuse(B,box({15,10,20},{25,20,20.000001})),Z,{5,5,20},{5,-5},"adversarial");
    add("protrusion 5e-7 on cap",fuse(B,box({15,10,20},{25,20,20.0000005})),Z,{5,5,20},{5,-5},"adversarial");
    {   // Two separate prongs on a lower base: the other prong top is a separate,
        // non-adjacent Face at (or just below) the selected cap level.
        const auto prongs=[&](double drop){return fuse(box({0,0,0},{40,30,10}),fuse(box({0,0,10},{10,30,20}),box({30,0,10},{40,30,20-drop})));};
        add("separate prongs coplanar (touching level)",prongs(0),Z,{5,15,20},{5,-5},"adversarial");
        add("other prong 1e-6 lower (inside margin)",prongs(1.e-6),Z,{5,15,20},{5},"adversarial");
        add("other prong 3e-6 lower (outside margin)",prongs(3.e-6),Z,{5,15,20},{5},"adversarial");
        add("other prong 1e-5 lower",prongs(1.e-5),Z,{5,15,20},{5},"adversarial");}
    add("horizontal hole crossing slab",cut(B,cylinder({-1,15,15},gp_Dir(1,0,0),2,42)),Z,{20,5,20},{5,-2,-2.999997,-3,-5},"adversarial");
    add("horizontal hole tangent to cap",cut(B,cylinder({-1,15,15},gp_Dir(1,0,0),5,42)),Z,{20,5,20},{5,-2},"adversarial");
    add("oblique hole (curved wall not parallel)",cut(B,cylinder({20,15,-5},gp_Dir(0.3,0,1),4,40)),Z,{5,5,20},{5,-5},"adversarial");
    {BRep_Builder b;TopoDS_Compound two;b.MakeCompound(two);b.Add(two,B);b.Add(two,box({100,0,0},{110,10,10}));
     add("disconnected solids",two,Z,{20,15,20},{5,-5},"adversarial");}
    add("internal cavity",cut(B,box({10,10,5},{30,20,15})),Z,{20,15,20},{5,-2},"adversarial");
    add("shell only (invalid)",BRepPrimAPI_MakeBox(40,30,20).Shell(),Z,{20,15,20},{5},"adversarial");
    {Fixture f{"foreign Face",B,pick(box({0,0,0},{40,30,20}),Z,{20,15,20}),{5},"adversarial"};fx.push_back(f);}
    {Fixture f{"non-finite / zero displacement",B,pick(B,Z,{20,15,20}),{std::nan(""),INFINITY,0.,5.e-8},"adversarial"};fx.push_back(f);}
    {BRepBuilderAPI_NurbsConvert nurbs(B,true);const auto top=pick(B,Z,{20,15,20});
     fx.push_back({"NURBS-converted box (imported-like)",nurbs.Shape(),TopoDS::Face(nurbs.ModifiedShape(top)),{5,-5},"adversarial"});}

    const size_t originalFixtures=fx.size();
    // F2.9G.2A targeted fixtures (group "g2a").
    auto through=[&](const TopoDS_Shape& s,const std::vector<gp_Pnt>& profile){return cut(s,prism(profile,40).Moved(TopLoc_Location(gp_Trsf())));};
    {   auto slot=[](double x0,double x1,double y0,double y1){return std::vector<gp_Pnt>{{x0,y0,-5},{x1,y0,-5},{x1,y1,-5},{x0,y1,-5}};};
        add("two through-slots (cap with three wires)",through(through(B,slot(5,15,5,10)),slot(25,35,20,25)),Z,{20,15,20},{5,-5,-19.99999},"g2a");}
    add("side pocket (trimmed planar walls)",cut(B,box({35,10,5},{41,20,15})),Z,{20,15,20},{5,-2,-4.99999,-5},"g2a");
    add("split cylinder lateral (lugs below cap)",fuse(cylinder({0,0,0},Z,10,20),box({-2,-12,0},{2,12,10})),Z,{0,0,20},{5,-5,-9.99999,-10},"g2a");
    {   std::vector<gp_Pnt> star;for(int i=0;i<10;++i){const double r=i%2?4:9,a=i*M_PI/5;star.push_back({20+r*std::cos(a),15+r*std::sin(a),-5});}
        add("complex trimmed cap (star + round holes)",cut(cut(through(B,star),cylinder({5,5,-1},Z,2,30)),cylinder({35,25,-1},Z,2,30)),Z,{2,25,20},{5,-5},"g2a");}
    {   std::vector<gp_Pnt> star;for(int i=0;i<10;++i){const double r=i%2?8:20,a=i*M_PI/5;star.push_back({r*std::cos(a),r*std::sin(a),0});}
        add("non-convex star extrusion",prism(star,20),Z,{0,0,20},{5,-5},"g2a");}
    add("tangent external cylinder below cap",fuse(B,cylinder({45,15,0},Z,5,10)),Z,{20,15,20},{5,-5,-9.99999,-10},"g2a");
    add("tangent external cylinder at cap level",fuse(B,cylinder({45,15,0},Z,5,20)),Z,{20,15,20},{5,-5},"g2a");
    add("stacked boxes (split analytic walls)",fuse(box({0,0,0},{40,30,10}),box({0,0,10},{40,30,20})),Z,{20,15,20},{5,-5,-9.99999,-15},"g2a");
    {   BRepBuilderAPI_Sewing sewing(1.e-7);for(TopExp_Explorer f(B,TopAbs_FACE);f.More();f.Next())sewing.Add(BRepBuilderAPI_Copy(f.Current()).Shape());
        sewing.Perform();TopoDS_Shape solid;
        for(TopExp_Explorer sh(sewing.SewedShape(),TopAbs_SHELL);sh.More();sh.Next()){BRepBuilderAPI_MakeSolid make(TopoDS::Shell(sh.Current()));
            if(make.IsDone()){auto so=make.Solid();BRepLib::OrientClosedSolid(so);solid=so;}break;}
        add("imported-like: sewn analytic box",solid,Z,{20,15,20},{5,-5},"g2a");}
    {   gp_Trsf t;t.SetTranslation(gp_Vec(5,-3,7));const auto located=B.Moved(TopLoc_Location(t));
        add("imported-like: located analytic box",located,Z,{25,12,27},{5,-5},"g2a");}
    add("mixed: remote NURBS block",fuse(B,BRepBuilderAPI_NurbsConvert(box({40,0,0},{50,30,10}),true).Shape()),Z,{20,15,20},{5,-5},"g2a");
    add("NURBS-converted cylinder hole",cut(B,BRepBuilderAPI_NurbsConvert(cylinder({20,15,-1},Z,5,30),true).Shape()),Z,{5,5,20},{5,-5},"g2a");
    add("analytic cylinder cap over NURBS cylinder",fuse(BRepBuilderAPI_NurbsConvert(cylinder({0,0,0},Z,10,15),true).Shape(),cylinder({0,0,15},Z,10,5)),Z,{0,0,20},{5,-3},"g2a");
    {   const auto analytic=BRepPrimAPI_MakeCylinder(10,20).Shape();BRepBuilderAPI_NurbsConvert nurbs(analytic,true);
        fx.push_back({"NURBS-converted cylinder (whole)",nurbs.Shape(),TopoDS::Face(nurbs.ModifiedShape(pick(analytic,Z,{0,0,20}))),{5,-5},"g2a"});}
    add("near-tolerance tilted walls (5e-10)",BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(gp_Pln(),0,40,0,30).Face(),gp_Vec(1.e-8,0,20),true,true).Shape(),Z,{20,15,20},{5,-5},"g2a");
    add("near-tolerance tilted walls (5e-8)",BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(gp_Pln(),0,40,0,30).Face(),gp_Vec(1.e-6,0,20),true,true).Shape(),Z,{20,15,20},{5,-5},"g2a");
    add("protrusion 1e-7 on cap (Confusion)",fuse(B,box({15,10,20},{25,20,20.0000001})),Z,{5,5,20},{5,-5},"g2a");
    {   QString e1,e2;const auto first=MoveFaceGeometry::move(B,pick(B,Z,{20,15,20}),5,e1,MoveFaceGeometry::ReconstructionPolicy::AxialInvariantCap);
        const auto second=first.IsNull()?TopoDS_Shape():MoveFaceGeometry::move(first,pick(first,gp_Dir(1,0,0),{40,15,10}),-3,e2,MoveFaceGeometry::ReconstructionPolicy::AxialInvariantCap);
        add("previously modified analytic B-Rep",second,Z,{18,15,25},{5,-5,-24.99999},"g2a");}
    {   const auto hole=cut(B,cylinder({20,15,-1},Z,5,30));QString e;
        const auto modified=MoveFaceGeometry::move(hole,pick(hole,Z,{5,5,20}),4,e,MoveFaceGeometry::ReconstructionPolicy::AxialInvariantCap);
        add("previously modified through-hole",modified,Z,{5,5,24},{5,-5},"g2a");}
    const size_t g2aFixtures=fx.size();
    // F2.9G.2B targeted fixtures (group "g2b"): one per open obligation.
    {   const auto ellipse=BRepBuilderAPI_MakeFace(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Elips(gp_Ax2(gp_Pnt(0,0,0),Z),10,10)).Edge()).Wire()).Face();
        add("cap boundary typed Ellipse (equal radii)",BRepPrimAPI_MakePrism(ellipse,gp_Vec(0,0,20),true,true).Shape(),Z,{0,0,20},{5,-5},"g2b");}
    for(double delta:{5.e-6,2.e-7}) {
        std::ostringstream n;n<<"near-coincident wall carriers (step "<<delta<<")";
        add(n.str(),fuse(box({0,0,0},{40,30,10}),box({0,0,10},{40+delta,30,20})),Z,{20,15,20},{5,-5,-9.99999},"g2b");}
    add("thin slot: same-orientation faces 6e-6 apart",
        fuse(fuse(B,box({40,0,0},{40.000006,30,1})),box({40.000003,0,0},{40.000006,30,15})),Z,{20,15,20},{5,-3,-4.99999},"g2b");
    add("coaxial boss and bottom hole (same carrier, opposite sides)",
        fuse(cut(B,cylinder({20,15,-1},Z,5,13)),cylinder({20,15,20},Z,5,5)),Z,{5,5,20},{-5,-7.99999,-8},"g2b");
    add("coaxial cap hole over bottom boss (same carrier, opposite sides)",
        fuse(cut(B,cylinder({20,15,14},Z,5,7)),cylinder({20,15,-5},Z,5,5)),Z,{5,5,20},{5,-3,-5.99999,-6},"g2b");
    // F2.9G.2C near-coincident carrier fixtures (group "g2c").
    for(double sep:{1.e-7,1.e-6,5.e-6}) {
        std::ostringstream n;n<<"overlapping parallel +x Faces "<<sep<<" apart (thin slot)";
        add(n.str(),fuse(fuse(B,box({40,0,0},{40+sep,30,1})),box({40+sep/2,0,0},{40+sep,30,15})),Z,{20,15,20},{5,-3},"g2c");
        std::ostringstream m;m<<"non-overlapping parallel wall pieces "<<sep<<" apart (step)";
        add(m.str(),fuse(box({0,0,0},{40,30,10}),box({0,0,10},{40+sep,30,20})),Z,{20,15,20},{5,-5},"g2c");
    }
    add("coincident carrier, distinct trimmed wall pieces (side notch)",cut(B,box({38,-1,8},{41,31,12})),Z,{20,15,20},{5,-5,-7.99999},"g2c");
    for(double dr:{1.e-6,5.e-6}) {
        std::ostringstream n;n<<"coaxial holes, radius differs by "<<dr;
        add(n.str(),cut(cut(B,cylinder({20,15,10},Z,5,11)),cylinder({20,15,-1},Z,5+dr,11)),Z,{5,5,20},{5,-5},"g2c");
        std::ostringstream m;m<<"hole axes shifted by "<<dr;
        add(m.str(),cut(cut(B,cylinder({20,15,10},Z,5,11)),cylinder({20+dr,15,-1},Z,5,11)),Z,{5,5,20},{5,-5},"g2c");
    }
    add("near-coincident cylinder carriers: boss 5 over hole 5+2e-6",fuse(cut(B,cylinder({20,15,-1},Z,5.000002,13)),cylinder({20,15,20},Z,5,5)),Z,{5,5,20},{-5,5},"g2c");
    add("opposite orientation, same carrier: coaxial boss over through-hole r=5",fuse(cut(B,cylinder({20,15,-1},Z,5,30)),cylinder({20,15,20},Z,5,5)),Z,{5,5,20},{-5},"g2c");
    {   const auto L=fuse(box({0,0,0},{40,30,20}),box({0,0,20},{10,30,40}));QString e;
        const auto raised=MoveFaceGeometry::move(L,pick(L,Z,{25,15,20}),19.999995,e,MoveFaceGeometry::ReconstructionPolicy::AxialInvariantCap);
        add("previously modified: lower step raised to 5e-6 below the cap",raised,Z,{5,15,40},{5,-5},"g2c");
        const auto level=raised.IsNull()?TopoDS_Shape():MoveFaceGeometry::move(raised,pick(raised,Z,{25,15,39.999995}),5.e-6,e,MoveFaceGeometry::ReconstructionPolicy::AxialInvariantCap);
        add("previously modified: step brought exactly level with the cap",level,Z,{5,15,40},{5,-5},"g2c");}
    // Explicit policies (F2.9G.2C is the candidate configuration = Options defaults).
    Options g2,g2a,g2b,g2c;
    g2.representation=Representation::Any;g2.lineCircleBoundary=false;g2.sampleOrientation=true;g2.nearCoincidentRefusal=false;
    g2a=g2;g2a.representation=Representation::AnalyticPlaneCylinder;
    g2b=g2c;g2b.nearCoincidentRefusal=false;
    struct Tally{std::map<Verdict,unsigned> verdicts;unsigned falseAdmissions=0;};
    std::map<std::string,Tally> tally;std::map<std::string,unsigned> groupCases;
    unsigned conservative=0,cases=0,localAccepted=0,fallbackOnly=0,changesVsG2B=0,invalidSources=0,productionMismatch=0,productionFalse=0;
    std::map<std::string,unsigned> productionReadyCount;
    std::vector<double> proofMs,nearMs,planMs,constructMs,certifiedMs,representationMs;std::vector<unsigned> pairs,faceCounts;
    for(const auto& f:fx) for(double d:f.values) {
        ++cases;++groupCases[f.group];
        const auto t0=std::chrono::steady_clock::now();
        BRepReconstruction::PlanarSweepPlan plan;QString planError;bool planned=false;bool admitted=false;
        if(!f.shape.IsNull()&&!f.face.IsNull()) {
            const BRepReconstruction::SourceAdmission evidence(f.shape,f.face);
            admitted=evidence.valid();
            if(admitted)planned=BRepReconstruction::planPlanarSweep(f.shape,f.face,false,plan,planError,&evidence);
        }
        const double planTime=ms(t0);invalidSources+=!admitted;
        std::map<std::string,Result> verdict;
        for(const auto& [key,options]:std::vector<std::pair<std::string,Options>>{{"G2",g2},{"G2A",g2a},{"G2B",g2b},{"G2C",g2c}}) {
            Result r;
            if(f.shape.IsNull()||f.face.IsNull()||!admitted){r.verdict=Verdict::InvalidInput;r.reason=f.face.IsNull()?"selected Face not in source / missing":"source certificate refused";}
            else r=provePlanarSweepClearance(f.shape,f.face,plan,planned,d,options);
            verdict[key]=r;
        }
        const auto& proof=verdict["G2C"];changesVsG2B+=proof.verdict!=verdict["G2B"].verdict;
        // F2.9G.3 production: the clear-sweep provider alone (must equal G2C), and
        // the production selection path (exact Box first, then clear sweep).
        namespace PV=BRepReconstruction::Provisional;
        PV::PlanarMoveRequest request;request.source=f.shape;request.face=f.face;
        request.planarFaceMove=request.alongOutwardNormal=request.invariantCapPolicy=true;
        PV::Candidate clearOnly,production;const PV::PlanarMoveProvider* selected=nullptr;
        if(!f.shape.IsNull()&&!f.face.IsNull()) {
            clearOnly=PV::constructPlanarMove(request,&ProvisionalPlanarClearance::provider,d);
            PV::constructPlanarMove(request,d,&selected);
            production=PV::negotiatePlanarMove(request,d);
        }
        const bool clearReady=clearOnly.outcome==PV::Outcome::Ready,productionReady=production.outcome==PV::Outcome::Ready;
        if(clearReady!=(proof.verdict==Verdict::ProvenClear))++productionMismatch;
        const char* providerName=selected==&ProvisionalBoxPreview::provider?"box":selected==&ProvisionalPlanarClearance::provider?"clear":"none";
        // Independent certified oracle: first route and complete routing.
        BRepReconstruction::Result local;QString localError,moveError;bool localOk=false;TopoDS_Shape moved;
        const auto t1=std::chrono::steady_clock::now();
        if(!f.shape.IsNull()&&!f.face.IsNull()) {
            try{localOk=BRepReconstruction::movePlanarLocal(f.shape,f.face,d,local,localError,true);}catch(...){localOk=false;}
            try{moved=MoveFaceGeometry::move(f.shape,f.face,d,moveError,MoveFaceGeometry::ReconstructionPolicy::AxialInvariantCap);}catch(...){moved.Nullify();}
        }
        const double certTime=ms(t1);
        localAccepted+=localOk;if(!localOk&&!moved.IsNull())++fallbackOnly;
        bool anyProven=productionReady;for(const auto& [k,r]:verdict)anyProven|=r.verdict==Verdict::ProvenClear;
        std::string compare="-";bool agrees=true;
        if(anyProven) {
            TopoDS_Shape candidate;const auto t2=std::chrono::steady_clock::now();
            try{BRepReconstruction::PlanarSweep sweep;BRepReconstruction::sweepPlanarRegion(plan,d,sweep);
                BRepReconstruction::constructPlanarSweep(plan,d,sweep);candidate=sweep.candidate();}catch(...){candidate.Nullify();}
            const double constructTime=ms(t2);
            const bool candidateEq=localOk&&!candidate.IsNull()&&BRepCertification::equivalent(candidate,local.solid)&&sameBox(candidate,local.solid);
            const bool routeEq=localOk&&!moved.IsNull()&&BRepCertification::equivalent(moved,local.solid);
            agrees=localOk&&candidateEq&&routeEq;
            compare=std::string("local=")+(localOk?"accept":"REFUSE")+" candidate_equivalent="+(candidateEq?"1":"0")+" route_equivalent="+(routeEq?"1":"0");
            if(proof.verdict==Verdict::ProvenClear){proofMs.push_back(proof.ms);nearMs.push_back(proof.nearMs);representationMs.push_back(proof.representationMs);
                planMs.push_back(planTime);certifiedMs.push_back(certTime);constructMs.push_back(constructTime);pairs.push_back(proof.carrierPairs);faceCounts.push_back(proof.faces);}
        }
        for(const auto& [k,r]:verdict){++tally[k].verdicts[r.verdict];if(r.verdict==Verdict::ProvenClear&&!agrees)++tally[k].falseAdmissions;}
        if(proof.verdict==Verdict::NotProven&&localOk)++conservative;
        if(productionReady){++productionReadyCount[providerName];if(!agrees)++productionFalse;
            // The production Ready candidate itself (validated) must equal the certified result.
            if(agrees&&!(localOk&&BRepCertification::equivalent(production.shape,local.solid)&&sameBox(production.shape,local.solid)))++productionFalse;}
        const bool falseAdmission=proof.verdict==Verdict::ProvenClear&&!agrees;
        std::cout<<(falseAdmission?"FALSE_ADMISSION ":"CASE ")<<f.group<<" | "<<f.name<<" | d="<<d<<" | "<<name(proof.verdict)<<" ("<<proof.reason<<")"
                 <<" | G2="<<name(verdict["G2"].verdict)<<" G2A="<<name(verdict["G2A"].verdict)<<" G2B="<<name(verdict["G2B"].verdict)
                 <<" production="<<(productionReady?"Ready":production.outcome==PV::Outcome::Failed?"Failed":"CertifiedOnly")<<"/"<<providerName
                 <<" clear_only="<<(clearReady?"Ready":"no")<<" source_valid="<<admitted<<" | local="<<(localOk?"accept":"refuse")<<" move="<<(moved.IsNull()?"refuse":"accept")<<" | "<<compare
                 <<" | wallTop="<<proof.wallTop<<" otherTop="<<proof.otherTop<<" clearance="<<proof.clearance
                 <<" faces="<<proof.faces<<" pairs="<<proof.carrierPairs<<" matches="<<proof.carrierMatches<<" near_ms="<<proof.nearMs
                 <<" proof_ms="<<proof.ms<<" plan_ms="<<planTime;
        if(!localOk&&!localError.isEmpty())std::cout<<" | local_error="<<localError.left(120).toStdString();
        std::cout<<'\n';
    }
    // Diagnostic: what an OCCT bounding box would conclude for the same cap plane.
    for(const auto& f:fx) {
        if(f.group!="adversarial"&&f.name!="box")continue;
        if(f.shape.IsNull()||f.face.IsNull()||BRepAdaptor_Surface(f.face).GetType()!=GeomAbs_Plane||std::abs(BRepAdaptor_Surface(f.face).Plane().Axis().Direction().Z())<1-1.e-12)continue;
        const double L=BRepAdaptor_Surface(f.face).Plane().Location().Z();
        double exact=-INFINITY;bool supported=true;unsigned edges=0;
        Bnd_Box withTol,without;
        for(TopExp_Explorer e(f.shape,TopAbs_FACE);e.More();e.Next()) {
            const auto face=TopoDS::Face(e.Current());if(face.IsSame(f.face))continue;
            SweepClearance::Range r;if(SweepClearance::faceRange(face,Z,r,edges))exact=std::max(exact,r.hi);else supported=false;
            BRepBndLib::AddOptimal(face,withTol,false,true);BRepBndLib::AddOptimal(face,without,false,false);
        }
        double a[6],b[6];withTol.Get(a[0],a[1],a[2],a[3],a[4],a[5]);without.Get(b[0],b[1],b[2],b[3],b[4],b[5]);
        std::cout<<"BOUNDS "<<f.name<<" exact_max_minus_L="<<(supported?exact-L:NAN)<<" bnd_optimal_with_tolerance="<<a[5]-L
                 <<" bnd_optimal_without_tolerance="<<b[5]-L<<'\n';
    }
    auto median=[](std::vector<double> v)->double{if(v.empty())return NAN;std::sort(v.begin(),v.end());return v[v.size()/2];};
    auto maximum=[](const std::vector<double>& v)->double{return v.empty()?double(NAN):*std::max_element(v.begin(),v.end());};
    std::cout<<"SUMMARY cases="<<cases<<" ProvenClear="<<tally["G2C"].verdicts[Verdict::ProvenClear]<<" NotProven="<<tally["G2C"].verdicts[Verdict::NotProven]
             <<" InvalidInput="<<tally["G2C"].verdicts[Verdict::InvalidInput]<<" false_admissions="<<tally["G2C"].falseAdmissions
             <<" conservative_refusals="<<conservative<<" local_route_accepts="<<localAccepted<<" fallback_route_only="<<fallbackOnly
             <<" invalid_sources="<<invalidSources<<" verdict_changes_vs_G2B="<<changesVsG2B<<'\n';
    for(auto& [k,t]:tally) std::cout<<"POLICY "<<k<<" ProvenClear="<<t.verdicts[Verdict::ProvenClear]<<" NotProven="<<t.verdicts[Verdict::NotProven]
        <<" InvalidInput="<<t.verdicts[Verdict::InvalidInput]<<" false_admissions="<<t.falseAdmissions<<'\n';
    std::cout<<"PRODUCTION clear_provider_vs_G2C_mismatches="<<productionMismatch<<" selection_path_ready_box="<<productionReadyCount["box"]
             <<" ready_clear="<<productionReadyCount["clear"]<<" production_false_admissions="<<productionFalse<<'\n';
    std::cout<<"GROUPS";for(const auto& [g,n]:groupCases)std::cout<<' '<<g<<'='<<n;std::cout<<" fixtures="<<fx.size()<<'\n';
    std::vector<double> pd(pairs.begin(),pairs.end()),fd(faceCounts.begin(),faceCounts.end());
    std::cout<<"TIMING G2C proven: proof_ms median="<<median(proofMs)<<" max="<<maximum(proofMs)<<" | near_ms median="<<median(nearMs)<<" max="<<maximum(nearMs)
             <<" | carrier_pairs median="<<median(pd)<<" max="<<maximum(pd)<<" faces median="<<median(fd)<<" max="<<maximum(fd)
             <<" | representation_ms median="<<median(representationMs)<<" | plan_ms median="<<median(planMs)
             <<" | shared_construct_ms median="<<median(constructMs)<<" | certified(local+move)_ms median="<<median(certifiedMs)<<'\n';
    return tally["G2C"].falseAdmissions==0&&tally["G2B"].falseAdmissions==0&&tally["G2A"].falseAdmissions==0
        &&productionMismatch==0&&productionFalse==0?0:1;
}
