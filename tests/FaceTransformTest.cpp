#include "FaceTransformGeometry.h"
#include "RotateMoveFixture.h"
#include "MoveFaceGeometry.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepFeat_SplitShape.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include "BRepReconstruction.h"
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <sstream>

namespace {
void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
void near(double actual,double expected,const char* message) {
    require(std::abs(actual-expected)<1.e-5*std::max(1.0,std::abs(expected)),message);
}
int uniqueCount(const TopoDS_Shape& shape,TopAbs_ShapeEnum type) {
    TopTools_IndexedMapOfShape map;TopExp::MapShapes(shape,type,map);return map.Extent();
}
int count(const TopoDS_Shape& shape,TopAbs_ShapeEnum type) {
    int n=0; for(TopExp_Explorer it(shape,type);it.More();it.Next()) ++n; return n;
}
TopoDS_Face topFace(const TopoDS_Shape& shape) {
    TopoDS_Face result;
    double z=-1.e100;
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());
        if(BRepAdaptor_Surface(face).GetType()!=GeomAbs_Plane) continue;
        const double candidate=BRepAdaptor_Surface(face).Plane().Location().Z();
        if(candidate>z) {z=candidate;result=face;}
    }
    return result;
}
gp_Ax1 straightAxis(const TopoDS_Face& face) {
    for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()) {
        const BRepAdaptor_Curve curve(TopoDS::Edge(it.Current()));
        if(curve.GetType()==GeomAbs_Line) return curve.Line().Position();
    }
    throw std::runtime_error("planar face has no straight edge");
}
gp_Ax1 perpendicularStraightAxis(const TopoDS_Face& face,const gp_Ax1& first) {
    for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()) {
        const BRepAdaptor_Curve curve(TopoDS::Edge(it.Current()));
        if(curve.GetType()==GeomAbs_Line
           && std::abs(curve.Line().Direction().Dot(first.Direction()))<0.1) return curve.Line().Position();
    }
    throw std::runtime_error("planar face has no second straight direction");
}
TopoDS_Face transitionFace(const TopoDS_Shape& shape) {
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());
        if(BRepAdaptor_Surface(face).GetType()!=GeomAbs_Plane) continue;
        const auto plane=BRepAdaptor_Surface(face).Plane();
        if(std::abs(std::abs(plane.Axis().Direction().X())-1.0)>1.e-7) continue;
        GProp_GProps properties; BRepGProp::SurfaceProperties(face,properties);
        const auto center=properties.CentreOfMass();
        if(center.X()>59.9 && center.X()<60.1 && center.Z()>69.9 && center.Z()<70.1) return face;
    }
    return {};
}
gp_Ax1 yAxis(const TopoDS_Face& face) {
    for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()) {
        const BRepAdaptor_Curve curve(TopoDS::Edge(it.Current()));
        if(curve.GetType()==GeomAbs_Line && std::abs(curve.Line().Direction().Y())>0.99)
            return curve.Line().Position();
    }
    throw std::runtime_error("transition face has no straight Y edge");
}
double volume(const TopoDS_Shape& shape) { GProp_GProps p;BRepGProp::VolumeProperties(shape,p);return p.Mass(); }
std::string shapeStamp(const TopoDS_Shape& shape) { std::ostringstream out;BRepTools::Write(shape,out);return out.str(); }
// Independent material oracle for the two-height Unite, including probes on
// both sides of the requested riser. This catches a valid but wrong solid.
void staircase(const TopoDS_Shape& shape,double baseX,double slope,double pivotZ) {
    require(BRepCheck_Analyzer(shape).IsValid(),"staircase B-Rep invalid");
    BRepClass3d_SolidClassifier classifier(shape);
    auto probe=[&](double x,double y,double z) {
        const bool expected=x<160&&y>0&&y<80&&z>0&&z<90
            &&(z<50?x>0:x>baseX+slope*(z-pivotZ));
        classifier.Perform(gp_Pnt(x,y,z),1.e-7);
        if(classifier.State()!=(expected?TopAbs_IN:TopAbs_OUT)) {
            std::ostringstream message;message<<"Material mismatch at "<<x<<","<<y<<","<<z
                <<"; base="<<baseX<<" slope="<<slope<<" pivot="<<pivotZ<<" expected="<<expected<<" state="<<classifier.State();
            throw std::runtime_error(message.str());
        }
    };
    const double minX=std::min({0.0,baseX+slope*(50-pivotZ),baseX+slope*(90-pivotZ)});
    for(double x=minX-3;x<165;x+=7.3)
        for(double y=-3;y<85;y+=21.1)
            for(double z=-3;z<95;z+=6.7) probe(x,y,z);
    for(double z=51;z<90;z+=3.1) {
        const double boundary=baseX+slope*(z-pivotZ);
        probe(boundary-0.05,40,z); probe(boundary+0.05,40,z);
    }
}
double staircaseVolume(double base,double slope,double pivot) {
    const double intercept=base+slope*(50-pivot);
    std::vector<double> cuts{0,40};
    if(std::abs(slope)>1.e-12) for(double x:{0.0,160.0}) {
        const double t=(x-intercept)/slope;
        if(t>0&&t<40) cuts.push_back(t);
    }
    std::sort(cuts.begin(),cuts.end());
    double area=0;
    for(size_t i=1;i<cuts.size();++i) {
        const auto width=[&](double t){return std::max(160-intercept-slope*t,0.0);};
        area+=(cuts[i]-cuts[i-1])*(width(cuts[i])+width(cuts[i-1]))/2;
    }
    return 160*80*50+80*area;
}
void noCoplanarSeams(const TopoDS_Shape& shape) {
    TopTools_IndexedDataMapOfShapeListOfShape adjacency;
    TopExp::MapShapesAndAncestors(shape,TopAbs_EDGE,TopAbs_FACE,adjacency);
    for(int i=1;i<=adjacency.Extent();++i) {
        const auto& faces=adjacency.FindFromIndex(i);
        if(faces.Extent()!=2) continue;
        const BRepAdaptor_Surface a(TopoDS::Face(faces.First())),b(TopoDS::Face(faces.Last()));
        if(a.GetType()!=GeomAbs_Plane||b.GetType()!=GeomAbs_Plane) continue;
        require(!a.Plane().Axis().Direction().IsParallel(b.Plane().Axis().Direction(),1.e-8)
            || a.Plane().Distance(b.Plane().Location())>1.e-7,"redundant coplanar dividing edge remains");
    }
}
// Investigation fixtures for the reported high-section protrusion. These do
// not yet reproduce the exact model shown in the manual KDE recording.
void highSectionMaterialAndMesh() {
    const auto low=BRepPrimAPI_MakeBox(100,80,50).Shape();
    for(int variant=0;variant<3;++variant) {
        gp_Trsf placement;
        if(variant) placement.SetRotation(gp_Ax1(gp_Pnt(60,30,0),
            variant==1?gp_Dir(0,0,1):gp_Dir(0.1,0.3,1)),0.37);
        const auto high=BRepBuilderAPI_Transform(
            BRepPrimAPI_MakeBox(gp_Pnt(60,30,0),100,80,90).Shape(),placement,true).Shape();
        BRepAlgoAPI_Fuse fuse(low,high);
        const auto source=fuse.Shape();
        // A real displayed source already carries meshes before reconstruction.
        BRepMesh_IncrementalMesh originalMesh(source,0.1,false,0.35,false);
        const auto stamp=shapeStamp(source);
        for(double distance:{-60.1234567,-30.1234567,-5.778314,5.1234567,30.778314,80.55555}) {
            QString error;
            const auto moved=MoveFaceGeometry::move(source,topFace(source),distance,error);
            if(moved.IsNull()) throw std::runtime_error(error.toStdString());
            double actualVolume=0;
            require(BRepReconstruction::validSolid(moved,actualVolume),"high-section move is not a valid closed solid");
            const auto highExpected=BRepBuilderAPI_Transform(
                BRepPrimAPI_MakeBox(gp_Pnt(60,30,0),100,80,90+distance).Shape(),placement,true).Shape();
            BRepAlgoAPI_Fuse oracle(low,highExpected);
            near(actualVolume,volume(oracle.Shape()),"high-section volume differs from independent Boolean oracle");
            require(BRepReconstruction::equivalent(moved,oracle.Shape()),"high-section move adds or removes unintended material");
            noCoplanarSeams(moved);
            BRepClass3d_SolidClassifier classifier(moved);
            const auto probe=[&](const gp_Pnt& world) {
                const auto local=world.Transformed(placement.Inverted());
                const bool inLow=world.X()>0&&world.X()<100&&world.Y()>0&&world.Y()<80&&world.Z()>0&&world.Z()<50;
                const bool inHigh=local.X()>60&&local.X()<160&&local.Y()>30&&local.Y()<110&&local.Z()>0&&local.Z()<90+distance;
                classifier.Perform(world,1.e-7);
                require(classifier.State()==((inLow||inHigh)?TopAbs_IN:TopAbs_OUT),"high-section material probe detected a protrusion or missing material");
            };
            for(double x=-19.37;x<200;x+=11.3)
                for(double y=-19.13;y<160;y+=11.7)
                    for(double z=-19.71;z<190;z+=11.1) probe(gp_Pnt(x,y,z));
            // Closely spaced samples on both sides of the high box's top and
            // lateral limits distinguish a thin spike from a harmless seam.
            for(double x=60.31;x<160;x+=7.1) for(double y=30.27;y<110;y+=7.3)
                for(double delta:{-0.001,0.001})
                    probe(gp_Pnt(x,y,90+distance+delta).Transformed(placement));
            BRepMesh_IncrementalMesh mesh(moved,0.1,false,0.35,false);
            require(mesh.IsDone(),"high-section result could not be meshed");
            for(TopExp_Explorer f(moved,TopAbs_FACE);f.More();f.Next()) {
                const auto face=TopoDS::Face(f.Current());TopLoc_Location location;
                const auto triangulation=BRep_Tool::Triangulation(face,location);
                require(!triangulation.IsNull(),"high-section face has no display triangulation");
                for(int t=1;t<=triangulation->NbTriangles();++t) {
                    int a,b,c;triangulation->Triangle(t).Get(a,b,c);
                    auto point=gp_Pnt((triangulation->Node(a).XYZ()+triangulation->Node(b).XYZ()+triangulation->Node(c).XYZ())/3.);
                    point.Transform(location.Transformation());
                    BRepClass_FaceClassifier trimmedFace(face,point,1.e-6);
                    require(trimmedFace.State()!=TopAbs_OUT,"display triangle extends outside the trimmed high-section face");
                }
            }
            require(shapeStamp(source)==stamp,"high-section reconstruction modified its displayed source");
        }
    }
}
void exactRotateMoveRegression() {
    using namespace RotateMoveFixture;
    const auto source=load("rotated30.brep"),bad=load("recorded-bad49.brep"),unite=load("unite.brep");
    const auto before=shapeStamp(source),badBefore=shapeStamp(bad),uniteBefore=shapeStamp(unite);
    const auto target=face(source,5);QString error;
    // Replay the already-correct preceding rotation from the exact original.
    const auto rotated=FaceTransformGeometry::rotate(unite,face(unite,9),
        gp_Ax1(gp_Pnt(125,80,60),gp_Dir(1,0,0)),30,error);
    if(rotated.IsNull()) throw std::runtime_error(error.toStdString());
    require(BRepReconstruction::equivalent(rotated,source),"Rotate +30 no longer matches the supplied source B-Rep");
    double badVolume=0;require(BRepReconstruction::validSolid(bad,badVolume),"recorded negative control should be a valid closed solid");
    BRepClass3d_SolidClassifier spike(bad,gp_Pnt(175,-20,50),1.e-7);
    require(spike.State()==TopAbs_IN,"recorded result does not reproduce real triangular solid material");
    near(badVolume,1156871.7334342287,"recorded negative-control volume changed");
    // A fresh display mesh cannot fix this negative control: the solid itself
    // contains the wing. Geometric comparison deliberately ignores all meshes.
    auto fresh=BRepBuilderAPI_Copy(bad,true,false).Shape();BRepTools::Clean(fresh,true);
    BRepMesh_IncrementalMesh mesh(fresh,0.1,false,0.35,false);
    require(mesh.IsDone()&&BRepReconstruction::equivalent(fresh,bad),"negative-control remeshing changed material");
    spike.Load(fresh);spike.Perform(gp_Pnt(175,-20,50),1.e-7);
    require(spike.State()==TopAbs_IN,"remeshing unexpectedly removed actual solid material");
    const auto moved=MoveFaceGeometry::move(source,target,49,error);
    if(moved.IsNull()) throw std::runtime_error(error.toStdString());
    verify(moved);noCoplanarSeams(moved);
    require(!BRepReconstruction::equivalent(moved,bad),"fixed result still matches the faulty result-debug.zip material");
    require(uniqueCount(moved,TopAbs_FACE)==9&&uniqueCount(moved,TopAbs_EDGE)==21&&uniqueCount(moved,TopAbs_VERTEX)==14,
        "exact fixture did not normalize to its necessary boundary faces/edges/vertices");
    auto requested=BRepAdaptor_Surface(target).Plane();requested.Translate(gp_Vec(0,-0.5,std::sqrt(3.)/2)*49);
    BRepReconstruction::Result direct;
    if(!BRepReconstruction::rebuildPlanar(source,target,requested,direct,error)) throw std::runtime_error(error.toStdString());
    require(BRepReconstruction::equivalent(direct.solid,moved)&&!direct.target.IsNull(),"direct reconstruction and Move adapter disagree");
    const auto finalPlane=BRepAdaptor_Surface(direct.target).Plane();
    require(finalPlane.Axis().Direction().IsParallel(requested.Axis().Direction(),1.e-8)&&finalPlane.Distance(requested.Location())<1.e-6,
        "Move did not reach the requested oblique plane at +49 mm");
    bool targetMapped=false;
    for(const auto& history:direct.history) if(history.original.IsSame(target))
        targetMapped=history.modified.size()==1&&history.modified.front().IsSame(direct.target);
    require(targetMapped,"target mapping refers to stale or ambiguous topology");
    auto alignedPlane=requested;alignedPlane.Translate(gp_Vec(0,-0.5,std::sqrt(3.)/2)*3);
    const auto aligned=FaceTransformGeometry::align(direct.solid,direct.target,alignedPlane,true,error);
    if(aligned.IsNull()) throw std::runtime_error(error.toStdString());verify(aligned,52);
    // Translation/rotation of the exact fixture rules out an axis- or model-
    // coordinate-specific clamp in the shared kernel.
    gp_Trsf placement;placement.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(0,0,1)),0.41);placement.SetTranslationPart(gp_Vec(17,-29,11));
    BRepBuilderAPI_Transform transformed(source,placement,true);
    const auto movedPlaced=MoveFaceGeometry::move(transformed.Shape(),TopoDS::Face(transformed.ModifiedShape(target)),49,error);
    if(movedPlaced.IsNull()) throw std::runtime_error(error.toStdString());
    require(BRepReconstruction::equivalent(movedPlaced,BRepBuilderAPI_Transform(expected(49),placement,true).Shape()),"neighbor trimming depends on global axes or coordinates");
    // A concave target cannot be represented by one common neighbor-trim
    // halfspace set. Refuse that fallback explicitly instead of clipping it.
    const auto concave=BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(100,80,60).Shape(),
        BRepPrimAPI_MakeBox(gp_Pnt(50,40,-1),60,50,62).Shape()).Shape();
    const auto concaveStamp=shapeStamp(concave);const auto concaveTop=topFace(concave);
    auto concavePlane=BRepAdaptor_Surface(concaveTop).Plane();concavePlane.Translate(gp_Vec(0,0,5));
    BRepReconstruction::Result refused;
    require(!BRepReconstruction::rebuildPlanar(concave,concaveTop,concavePlane,refused,error)
        &&refused.solid.IsNull()&&error.contains("concave"),"ambiguous concave neighbor trims were accepted");
    require(shapeStamp(concave)==concaveStamp,"refused concave trim changed input");
    require(shapeStamp(source)==before&&shapeStamp(bad)==badBefore&&shapeStamp(unite)==uniteBefore,"exact-fixture reconstruction modified supplied B-Rep data");
}
void geometry() {
    const auto box=BRepPrimAPI_MakeBox(100,80,60).Shape();
    const auto face=topFace(box);
    const auto axis=straightAxis(face);
    QString error;
    const double originalVolume=volume(box);
    for(const double degrees : {12.0,-12.0,25.0,-25.0}) {
        const auto result=FaceTransformGeometry::rotate(box,face,axis,degrees,error);
        require(!result.IsNull(),error.toStdString().c_str());
        require(BRepCheck_Analyzer(result).IsValid(),"rotated Box is invalid");
        require(count(result,TopAbs_FACE)==count(box,TopAbs_FACE)
                &&count(result,TopAbs_EDGE)==count(box,TopAbs_EDGE)
                &&count(result,TopAbs_VERTEX)==count(box,TopAbs_VERTEX),
                "Rotate Face added or removed Box topology");
        gp_Pln expected=BRepAdaptor_Surface(face).Plane();
        gp_Trsf transform; transform.SetRotation(axis,degrees*std::acos(-1.0)/180.0);
        expected.Transform(transform);
        bool targetPlaneFound=false;
        for(TopExp_Explorer f(result,TopAbs_FACE);f.More();f.Next()) {
            const auto candidate=TopoDS::Face(f.Current());
            if(BRepAdaptor_Surface(candidate).GetType()!=GeomAbs_Plane) continue;
            const auto plane=BRepAdaptor_Surface(candidate).Plane();
            if(plane.Axis().Direction().IsParallel(expected.Axis().Direction(),1.e-5)
               &&plane.Distance(expected.Location())<1.e-4) targetPlaneFound=true;
        }
        require(targetPlaneFound,"Rotate Face did not place the selected support at the requested angle");
        require(std::abs(volume(result)-originalVolume)>1.0,"face rotation did not change geometry");
    }
    const auto otherAxis=perpendicularStraightAxis(face,axis);
    const auto otherAxisResult=FaceTransformGeometry::rotate(box,face,otherAxis,17,error);
    require(!otherAxisResult.IsNull(),error.toStdString().c_str());
    const auto priorEdit=FaceTransformGeometry::rotate(box,face,axis,8,error);
    require(!priorEdit.IsNull(),error.toStdString().c_str());
    TopoDS_Face tiltedFace;
    for(TopExp_Explorer f(priorEdit,TopAbs_FACE);f.More();f.Next()) {
        const auto candidate=TopoDS::Face(f.Current());
        if(BRepAdaptor_Surface(candidate).GetType()==GeomAbs_Plane
           && std::abs(BRepAdaptor_Surface(candidate).Plane().Axis().Direction().Z())>0.5
           && std::abs(BRepAdaptor_Surface(candidate).Plane().Axis().Direction().Z())<0.999) {tiltedFace=candidate;break;}
    }
    require(!tiltedFace.IsNull(),"tilted GeneralBRep face was not retained");
    const auto generalResult=FaceTransformGeometry::rotate(priorEdit,tiltedFace,straightAxis(tiltedFace),-5,error);
    require(!generalResult.IsNull(),error.toStdString().c_str());
    require(FaceTransformGeometry::rotate(box,face,gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1)),10,error).IsNull(),
            "axis outside the selected face was accepted");

    const auto farReference=gp_Pln(gp_Pnt(0,0,75),gp_Dir(0,0,1));
    const auto coplanar=FaceTransformGeometry::align(box,face,farReference,true,error);
    require(!coplanar.IsNull(),error.toStdString().c_str());
    require(BRepCheck_Analyzer(coplanar).IsValid(),"coplanar result is invalid");
    require(std::abs(BRepAdaptor_Surface(topFace(coplanar)).Plane().Location().Z()-75.0)<1.e-5,"target face did not become coplanar");
    require(count(coplanar,TopAbs_FACE)==count(box,TopAbs_FACE),"Align Face added Box faces");
    const auto parallel=FaceTransformGeometry::align(box,face,farReference,false,error);
    require(!parallel.IsNull(),error.toStdString().c_str());
    require(std::abs(volume(parallel)-originalVolume)<1.e-5,"Parallel changed the target offset");
    const auto samePartReference=FaceTransformGeometry::align(box,face,
        gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),false,error);
    require(!samePartReference.IsNull(),error.toStdString().c_str());
    require(std::abs(volume(samePartReference)-originalVolume)<1.e-5,"same-Part Parallel changed geometry");

    const auto otherPlane=gp_Pln(gp_Pnt(100,0,0),gp_Dir(0.5,0,0.866025403784));
    const auto tilted=FaceTransformGeometry::align(box,face,otherPlane,false,error);
    require(!tilted.IsNull(),error.toStdString().c_str());
    require(BRepCheck_Analyzer(tilted).IsValid(),"parallel-aligned target is invalid");

    // Boolean Unite staircase: two overlapping boxes with different heights.
    // The target is the narrow vertical riser left at x=60 (z=50..90).
    const auto low=BRepPrimAPI_MakeBox(100,80,50).Shape();
    const auto high=BRepPrimAPI_MakeBox(gp_Pnt(60,0,0),100,80,90).Shape();
    BRepAlgoAPI_Fuse fuse(low,high); fuse.Build();
    require(fuse.IsDone(),"Boolean Unite setup failed");
    const auto boolean=fuse.Shape();
    const auto riser=transitionFace(boolean);
    require(!riser.IsNull(),"Boolean Unite transition face was not found");
    BRepReconstruction::Prepared prepared;
    require(BRepReconstruction::prepare(boolean,riser,prepared,error),error.toStdString().c_str());
    require(count(prepared.solid,TopAbs_FACE)<count(boolean,TopAbs_FACE),
            "fixture did not exercise controlled coplanar face merging");
    for(const auto& mapping:prepared.history)
        require(!mapping.modified.empty(),"normalization lost original face history");
    staircase(prepared.solid,60,0,0);
    require(FaceTransformGeometry::rotate(boolean,riser,yAxis(riser),0,error).IsSame(boolean),
            "zero rotation changed Boolean topology");
    require(MoveFaceGeometry::move(boolean,riser,0,error).IsSame(boolean),
            "zero move changed Boolean topology");
    auto expectedRiser=BRepAdaptor_Surface(prepared.target).Plane();
    expectedRiser.Translate(gp_Vec(-5,0,0));
    require(!BRepReconstruction::supports(prepared.solid,prepared.solid,prepared.target,expectedRiser,error),
            "support validation accepted an unchanged solid for a requested move");
    const auto wrongSolid=BRepPrimAPI_MakeBox(160,80,90).Shape();
    require(!BRepReconstruction::supports(prepared.solid,wrongSolid,prepared.target,expectedRiser,error),
            "support validation accepted a valid but unintended volume");
    const double uniteVolume=volume(boolean);
    const auto uniteBefore=shapeStamp(boolean);
    gp_Pnt riserCenter; gp_Dir riserNormal;
    require(MoveFaceGeometry::reference(boolean,riser,riserCenter,riserNormal,error),error.toStdString().c_str());
    for(const double distance : {5.0,-5.0,25.0,-25.0}) {
        const auto moved=MoveFaceGeometry::move(boolean,riser,distance,error);
        require(!moved.IsNull(),error.toStdString().c_str());
        staircase(moved,60-distance,0,0);
        require(count(moved,TopAbs_FACE)<count(boolean,TopAbs_FACE),"Move did not retain controlled topology merging");
        near(volume(moved),uniteVolume+80*40*distance,"Unite Move did not extend/trim both terraces correctly");
        require(std::abs(volume(boolean)-uniteVolume)<1.e-8,"Boolean Move Face modified its source");
        require(shapeStamp(boolean)==uniteBefore,"Boolean Move Face changed source B-Rep data");
    }
    for(const double angle : {8.0,-8.0}) {
        const auto rotated=FaceTransformGeometry::rotate(boolean,riser,yAxis(riser),angle,error);
        require(!rotated.IsNull(),error.toStdString().c_str());
        require(BRepCheck_Analyzer(rotated).IsValid(),"Unite Rotate invalid");
        const auto pivot=yAxis(riser);
        const double slope=std::tan(angle*std::acos(-1.0)/180.0)*pivot.Direction().Y();
        near(volume(rotated),uniteVolume-80*40*(70-pivot.Location().Z())*slope,
             "Unite Rotate wedge volume disagrees with the requested plane");
        staircase(rotated,60,slope,pivot.Location().Z());
        require(std::abs(volume(boolean)-uniteVolume)<1.e-8,"Boolean Rotate Face modified its source");
        require(shapeStamp(boolean)==uniteBefore,"Boolean Rotate Face changed source B-Rep data");
    }
    // Positive 75 degrees was refused by v0.3.5 when the z=90 cap vanished.
    // Negative 75 degrees must preserve the existing overhang behavior.
    for(double angle : {60.0,-60.0,68.1,68.3,75.0,-75.0}) {
        const gp_Ax1 pivot(gp_Pnt(60,0,50),gp_Dir(0,1,0));
        const double slope=std::tan(angle*std::acos(-1.0)/180.0);
        const auto rotated=FaceTransformGeometry::rotate(boolean,riser,pivot,angle,error);
        require(!rotated.IsNull(),error.toStdString().c_str());
        staircase(rotated,60,slope,50);
        near(volume(rotated),staircaseVolume(60,slope,50),"clipped Rotate material volume is wrong");
        noCoplanarSeams(rotated);
        if(std::abs(angle)==75) {
            gp_Trsf rotation;rotation.SetRotation(pivot,angle*std::acos(-1.0)/180.0);
            auto expected=BRepAdaptor_Surface(riser).Plane();expected.Transform(rotation);
            BRepReconstruction::Result mapped;
            require(BRepReconstruction::rebuildPlanar(boolean,riser,expected,mapped,error),error.toStdString().c_str());
            require(!mapped.target.IsNull(),"topology reconstruction lost target mapping");
            require(BRepReconstruction::equivalent(rotated,mapped.solid),"cell and adapter reconstructions disagree");
            int deleted=0;
            for(const auto& mapping:mapped.history) {
                deleted+=mapping.modified.empty();
                for(const auto& face:mapping.modified) {
                    bool member=false;
                    for(TopExp_Explorer f(mapped.solid,TopAbs_FACE);f.More();f.Next()) member=member||f.Current().IsSame(face);
                    require(member,"mapping references stale topology");
                }
            }
            require(angle>0?deleted>0:deleted==0,"unexpected support disappearance");
            require(count(mapped.solid,TopAbs_FACE)==(angle>0?7:8),"necessary face patches were lost or spurious faces added");
            if(angle>0) require(uniqueCount(mapped.solid,TopAbs_EDGE)<uniqueCount(prepared.solid,TopAbs_EDGE)
                &&uniqueCount(mapped.solid,TopAbs_VERTEX)<uniqueCount(prepared.solid,TopAbs_VERTEX),
                "vanished cap edges/vertices were retained");
            staircase(mapped.solid,60,slope,50);noCoplanarSeams(mapped.solid);
            if(angle>0) {
                auto reference=BRepAdaptor_Surface(mapped.target).Plane();
                auto outward=reference.Axis().Direction();if(mapped.target.Orientation()==TopAbs_REVERSED) outward.Reverse();
                reference.Translate(gp_Vec(outward)*2);
                const auto aligned=FaceTransformGeometry::align(mapped.solid,mapped.target,reference,true,error);
                require(!aligned.IsNull(),error.toStdString().c_str());
                const double base=60-2/std::cos(angle*std::acos(-1.0)/180.0);
                staircase(aligned,base,slope,50);
                near(volume(aligned),staircaseVolume(base,slope,50),"Align after topology change has wrong material");
                noCoplanarSeams(aligned);
            }
        }
    }
    for(double distance : {-60.0,60.0,100.0}) {
        const auto moved=MoveFaceGeometry::move(boolean,riser,distance,error);
        require(!moved.IsNull(),error.toStdString().c_str());
        staircase(moved,60-distance,0,50);
        near(volume(moved),staircaseVolume(60-distance,0,50),"Move crossing transition has wrong volume");
        noCoplanarSeams(moved);
        require(count(moved,TopAbs_FACE)==(distance==60?6:8),"Move retained a redundant face or removed a necessary terrace");
    }
    // Deliberately subdivide a coplanar side, simulating a leftover trim seam.
    // Final normalization must remove it without changing any material.
    BRepBuilderAPI_Copy isolated(boolean,true,false);
    TopoDS_Face side;
    for(TopExp_Explorer f(isolated.Shape(),TopAbs_FACE);f.More();f.Next()) {
        const BRepAdaptor_Surface surface(TopoDS::Face(f.Current()));
        GProp_GProps props;BRepGProp::SurfaceProperties(f.Current(),props);
        if(surface.GetType()==GeomAbs_Plane && std::abs(surface.Plane().Axis().Direction().Y())>0.99
           && std::abs(props.CentreOfMass().Y())<1.e-7 && props.CentreOfMass().X()<30.1) {side=TopoDS::Face(f.Current());break;}
    }
    require(!side.IsNull(),"seam fixture side missing");
    BRepFeat_SplitShape splitSide(isolated.Shape());
    // This raw Unite has a low side patch x=0..60, y=0, z=0..50.
    splitSide.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0,0,25),gp_Pnt(60,0,25)).Edge(),side);
    splitSide.Build();require(splitSide.IsDone(),"seam fixture splitting failed");
    require(count(splitSide.Shape(),TopAbs_FACE)>count(boolean,TopAbs_FACE),"seam fixture did not split a face");
    BRepReconstruction::Result normalized;
    require(BRepReconstruction::finish(boolean,splitSide.Shape(),riser,BRepAdaptor_Surface(riser).Plane(),normalized,error),error.toStdString().c_str());
    require(BRepReconstruction::equivalent(boolean,normalized.solid),"normalization changed material");
    noCoplanarSeams(normalized.solid);require(count(normalized.solid,TopAbs_FACE)==8,"normalization left split side faces");
    BRepBuilderAPI_Copy targetCopy(boolean,true,false);
    BRepFeat_SplitShape splitTarget(targetCopy.Shape());
    splitTarget.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(60,40,50),gp_Pnt(60,40,90)).Edge(),
        TopoDS::Face(targetCopy.ModifiedShape(riser)));
    splitTarget.Build();require(splitTarget.IsDone(),"target split fixture failed");
    require(BRepReconstruction::finish(boolean,splitTarget.Shape(),riser,BRepAdaptor_Surface(riser).Plane(),normalized,error),error.toStdString().c_str());
    require(!normalized.target.IsNull()&&count(normalized.solid,TopAbs_FACE)==8,"final target mapping did not follow merged target patches");
    noCoplanarSeams(normalized.solid);
    require(shapeStamp(boolean)==uniteBefore,"topology operations modified original B-Rep");
    // A side notch leaves TWO necessary disconnected patches on the same x=0
    // support. Unification must retain both; removing the notch merges them.
    const auto stock=BRepPrimAPI_MakeBox(160,80,90).Shape();
    const auto notchTool=BRepPrimAPI_MakeBox(gp_Pnt(-10,-10,20),30,100,20).Shape();
    BRepAlgoAPI_Cut notchCut(stock,notchTool);notchCut.Build();
    require(notchCut.IsDone(),"notch fixture failed");
    const auto notch=notchCut.Shape();
    TopoDS_Face notchBack;
    for(TopExp_Explorer f(notch,TopAbs_FACE);f.More();f.Next()) {
        GProp_GProps props;BRepGProp::SurfaceProperties(f.Current(),props);
        if(props.CentreOfMass().Distance(gp_Pnt(20,40,30))<1.e-6) notchBack=TopoDS::Face(f.Current());
    }
    require(!notchBack.IsNull(),"notch target not found");
    const auto deepened=MoveFaceGeometry::move(notch,notchBack,-5,error);
    require(!deepened.IsNull(),error.toStdString().c_str());
    near(volume(deepened),volume(stock)-25*80*20,"notch Move material incorrect");
    require(count(deepened,TopAbs_FACE)==10,"necessary disconnected coplanar faces were merged");
    noCoplanarSeams(deepened);
    int leftPatches=0;
    for(TopExp_Explorer f(deepened,TopAbs_FACE);f.More();f.Next()) {
        const auto plane=BRepAdaptor_Surface(TopoDS::Face(f.Current())).Plane();
        if(std::abs(plane.Axis().Direction().X())>0.99&&plane.Distance(gp_Pnt(0,0,0))<1.e-7) ++leftPatches;
    }
    require(leftPatches==2,"normalization merged necessary disconnected x=0 patches");
    BRepClass3d_SolidClassifier notchMaterial(deepened);
    for(double x:{-5.0,5.0,22.0,27.0,100.0,165.0}) for(double y:{-5.0,40.0,85.0})
        for(double z:{-5.0,10.0,30.0,50.0,95.0}) {
            const bool expected=x>0&&x<160&&y>0&&y<80&&z>0&&z<90&&(z<20||z>40||x>25);
            notchMaterial.Perform(gp_Pnt(x,y,z),1.e-7);
            require(notchMaterial.State()==(expected?TopAbs_IN:TopAbs_OUT),"notch material samples disagree");
        }
    const auto closed=MoveFaceGeometry::move(notch,notchBack,20,error);
    require(!closed.IsNull(),error.toStdString().c_str());
    require(BRepReconstruction::equivalent(closed,stock),"notch collapse did not restore the stock material");
    require(count(closed,TopAbs_FACE)==6,"notch collapse left unnecessary topology");
    auto notchExpected=BRepAdaptor_Surface(notchBack).Plane();notchExpected.Translate(gp_Vec(-20,0,0));
    BRepReconstruction::Result notchMapping;
    require(BRepReconstruction::rebuildPlanar(notch,notchBack,notchExpected,notchMapping,error),error.toStdString().c_str());
    require(!notchMapping.target.IsNull() && !notchMapping.target.IsSame(notchBack),
            "target mapping was not replaced after notch collapse");
    bool mappedTarget=false;
    for(const auto& mapping:notchMapping.history) if(mapping.original.IsSame(notchBack))
        mappedTarget=mapping.modified.size()==1 && mapping.modified.front().IsSame(notchMapping.target);
    require(mappedTarget,"selected notch face did not map to the merged exterior face");
    TopoDS_Face ambiguousTarget;
    for(TopExp_Explorer f(notch,TopAbs_FACE);f.More();f.Next()) {
        GProp_GProps props;BRepGProp::SurfaceProperties(f.Current(),props);
        if(std::abs(props.CentreOfMass().X())<1.e-7&&props.CentreOfMass().Z()<20) ambiguousTarget=TopoDS::Face(f.Current());
    }
    require(!ambiguousTarget.IsNull(),"ambiguous target fixture missing");
    auto ambiguousPlane=BRepAdaptor_Surface(ambiguousTarget).Plane();ambiguousPlane.Translate(gp_Vec(-2,0,0));
    BRepReconstruction::Result refusedMapping;
    require(!BRepReconstruction::rebuildPlanar(notch,ambiguousTarget,ambiguousPlane,refusedMapping,error),
            "cell reconstruction silently moved multiple coplanar target patches");
    require(refusedMapping.solid.IsNull(),"refusal retained a stale result solid");

    require(BRepReconstruction::equivalent(notchMapping.solid,stock),"mapped notch collapse changed material");

    const auto alignedRiser=FaceTransformGeometry::align(boolean,riser,
        gp_Pln(gp_Pnt(55,0,0),gp_Dir(1,0,0)),true,error);
    require(!alignedRiser.IsNull(),error.toStdString().c_str());
    near(volume(alignedRiser),uniteVolume+80*40*5,"Unite Align did not trim to x=55");
    staircase(alignedRiser,55,0,0);
    for(const double x : {55.0,65.0}) {
        const auto aligned=FaceTransformGeometry::align(boolean,riser,
            gp_Pln(gp_Pnt(x,0,0),gp_Dir(1,0,0)),true,error);
        require(!aligned.IsNull(),error.toStdString().c_str());
        staircase(aligned,x,0,0);
        near(volume(aligned),uniteVolume+(60-x)*3200,"signed riser Align volume incorrect");
    }
    const auto parallelRiser=FaceTransformGeometry::align(boolean,riser,
        gp_Pln(gp_Pnt(55,0,0),gp_Dir(1,0,0)),false,error);
    require(!parallelRiser.IsNull(),error.toStdString().c_str());
    staircase(parallelRiser,60,0,0);
    for(double slope : {0.1,-0.1}) for(bool coplanarMode : {false,true}) {
        const auto reference=gp_Pln(gp_Pnt(55,0,70),gp_Dir(1,0,-slope));
        const auto aligned=FaceTransformGeometry::align(boolean,riser,reference,coplanarMode,error);
        require(!aligned.IsNull(),error.toStdString().c_str());
        staircase(aligned,coplanarMode?55:60,slope,70);
        near(volume(aligned),uniteVolume+(coplanarMode?16000:0),"tilted Align riser volume incorrect");
    }
    // Collapse the low terrace or cross the far wall: no partial source mutation.
    for(const double distance : {-120.0})
        require(MoveFaceGeometry::move(boolean,riser,distance,error).IsNull(),
                "riser collapse/crossing was accepted");
    require(std::abs(volume(boolean)-uniteVolume)<1.e-8,"Boolean Align Face modified its source");
    require(shapeStamp(boolean)==uniteBefore,"Boolean Align Face changed source B-Rep data");

    // The shared face tooling also accepts a simple planar face on a Boolean cut.
    const auto cutter=BRepPrimAPI_MakeBox(gp_Pnt(30,20,20),30,40,40).Shape();
    BRepAlgoAPI_Cut cut(low,cutter); cut.Build();
    require(cut.IsDone(),"Boolean Subtract setup failed");
    const auto cutTop=topFace(cut.Shape());
    const auto movedCut=MoveFaceGeometry::move(cut.Shape(),cutTop,3,error);
    require(!movedCut.IsNull(),error.toStdString().c_str());
    require(BRepCheck_Analyzer(movedCut).IsValid(),"moved Boolean Subtract face is invalid");
    require(count(movedCut,TopAbs_FACE)==count(cut.Shape(),TopAbs_FACE),
            "Boolean Subtract Move Face added faces");
    near(volume(movedCut)-volume(cut.Shape()),3.0*(8000-30*40),"Boolean Subtract adjacent faces were not trimmed at the moved plane");
    const auto rotatedCut=FaceTransformGeometry::rotate(cut.Shape(),cutTop,straightAxis(cutTop),5,error);
    if(!rotatedCut.IsNull()) {
        require(BRepCheck_Analyzer(rotatedCut).IsValid(),"rotated Boolean Subtract face is invalid");
        require(count(rotatedCut,TopAbs_FACE)==count(cut.Shape(),TopAbs_FACE),
                "Boolean Subtract Rotate Face added faces");
    }

    const auto b=BRepPrimAPI_MakeBox(gp_Pnt(60,15,0),60,50,60).Shape();
    BRepAlgoAPI_Fuse fuseSimple(box,b); fuseSimple.Build();
    require(fuseSimple.IsDone(),"Boolean setup failed");
    const auto simpleBoolean=fuseSimple.Shape();
    TopoDS_Shape booleanResult;
    for(TopExp_Explorer f(simpleBoolean,TopAbs_FACE);f.More();f.Next()) {
        const auto planar=TopoDS::Face(f.Current());
        if(BRepAdaptor_Surface(planar).GetType()!=GeomAbs_Plane) continue;
        const auto probe=FaceTransformGeometry::rotate(simpleBoolean,planar,straightAxis(planar),8,error);
        if(!probe.IsNull()) { booleanResult=probe; break; }
    }
    require(!booleanResult.IsNull(),"no planar face on the Boolean result supported a simple rotation");
    require(BRepCheck_Analyzer(booleanResult).IsValid(),"Boolean face rotation is invalid");

    const auto untouched=FaceTransformGeometry::rotate(box,face,axis,180,error);
    require(untouched.IsNull(),"unsupported rotation angle was accepted");
    require(std::abs(volume(box)-originalVolume)<1.e-8,"a refused operation mutated its source");
}
}
int main() {
    try { exactRotateMoveRegression();highSectionMaterialAndMesh();geometry();std::cout<<"Face transform geometry passed.\n";return 0; }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
