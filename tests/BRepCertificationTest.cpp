#include "BRepCertification.h"
#include "BRepMeasurementPrivate.h"
#include "MoveFaceGeometry.h"
#include <BOPAlgo_ArgumentAnalyzer.hxx>
#include <type_traits>
#include "BRepReconstruction.h"
#include "BooleanGeometry.h"
#include "NativeDocument.h"
#include "SplineExtrusionFixture.h"
#include <chrono>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace C = BRepCertification;
namespace {
void check(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
void near(double a,double b,const char* message) {check(std::abs(a-b)<1.e-7,message);}
std::string brep(const TopoDS_Shape& shape) {std::ostringstream s;BRepTools::Write(shape,s);return s.str();}
TopoDS_Shape box(double x,double y,double z,double dx,double dy,double dz) {
    return BRepPrimAPI_MakeBox(gp_Pnt(x,y,z),dx,dy,dz).Shape();
}
TopoDS_Face rectangle(double x0,double x1,double y0=0,double y1=10) {
    return BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),x0,x1,y0,y1).Face();
}
TopoDS_Shape compound(const std::vector<TopoDS_Shape>& shapes) {
    BRep_Builder b;TopoDS_Compound c;b.MakeCompound(c);for(const auto& s:shapes) b.Add(c,s);return c;
}
TopoDS_Face top(const TopoDS_Shape& shape,double z) {
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());BRepAdaptor_Surface s(face);
        if(s.GetType()==GeomAbs_Plane&&s.Plane().Axis().Direction().IsParallel(gp_Dir(0,0,1),1.e-8)
           &&std::abs(s.Plane().Location().Z()-z)<1.e-7) return face;
    }
    throw std::runtime_error("Missing synthetic cap");
}
// Frozen pre-F2.8C Boolean input policy, including its second validity query.
bool legacyBooleanValidSolid(const TopoDS_Shape& shape,double& volume) {
    BRepTiming::Scope timingScope("BooleanGeometry.validSolid");
    if(shape.IsNull()||!TopExp_Explorer(shape,TopAbs_SOLID).More()||!C::validBRep(shape))return false;
    if(!C::closedShells(shape))return false;
    BRepMeasurement::Result measured;QString error;
    if(!BRepMeasurement::volume(shape,measured,error))return false;
    volume=measured.properties.Mass();return std::isfinite(volume)&&volume>1.e-12;
}
// Frozen pre-P3D.5 implementation for behavioral and controlled timing comparison.
bool legacyCertifiedSolid(const TopoDS_Shape& shape,double& volume,size_t& count) {
    count=0;if(!legacyBooleanValidSolid(shape,volume))return false;
    for(TopExp_Explorer it(shape,TopAbs_SOLID);it.More();it.Next()) {
        double v=0;if(!legacyBooleanValidSolid(it.Current(),v)||v<=0)return false;
        BRepClass3d_SolidClassifier classifier(TopoDS::Solid(it.Current()));classifier.PerformInfinitePoint(Precision::Confusion());
        if(classifier.State()!=TopAbs_OUT)return false;++count;
    }
    return count>0;
}
void rootValidation() {
    const auto source=box(0,0,0,10,10,10);
    const auto hollow=BooleanGeometry::combine<BRepAlgoAPI_Cut>(source,box(3,3,3,4,4,4));
    BRep_Builder builder;TopoDS_Solid invalid;builder.MakeSolid(invalid);
    const auto checkShape=[&](const TopoDS_Shape& shape,bool expected,size_t expectedCount,unsigned validations) {
        const auto stamp=brep(shape);double a=0,b=0;size_t n=0,m=0;
        const bool old=legacyCertifiedSolid(shape,a,n);
        BRepTiming::reset();BRepTiming::enabled=true;
        const bool now=BooleanGeometry::certifiedSolid(shape,b,m);BRepTiming::enabled=false;
        check(old==expected&&now==old&&m==n,"root validation policy changed");
        if(expected){near(a,b,"root volume changed");check(m==expectedCount,"component count changed");}
        if(BRepTiming::stats["BooleanGeometry.validSolid"].calls!=validations)std::cerr<<"root case type="<<shape.ShapeType()<<" expected="<<expected<<" components="<<expectedCount<<" calls="<<BRepTiming::stats["BooleanGeometry.validSolid"].calls<<" wanted="<<validations<<"\n";
        check(BRepTiming::stats["BooleanGeometry.validSolid"].calls==validations,"unexpected validity invocation count");
        check(brep(shape)==stamp,"root validation mutated source");
    };
    checkShape(source,true,1,1);
    checkShape(invalid,false,0,1);
    checkShape(source.Reversed(),false,0,1);
    checkShape(compound({source}),true,1,2);
    checkShape(compound({source,box(20,0,0,10,10,10)}),true,2,3);
    checkShape(hollow,true,1,2); // Boolean Cut retains its one-solid compound wrapper.
    checkShape(TopExp_Explorer(hollow,TopAbs_SOLID).Current(),true,1,1);
    // Overlapping shells in one solid remain refused; the independent direct-
    // edit self-interference policy is not replaced by this Boolean certificate.
    TopoDS_Solid crossing;builder.MakeSolid(crossing);
    for(const auto& s:{source,box(5,5,5,10,10,10)})
        for(TopExp_Explorer sh(s,TopAbs_SHELL);sh.More();sh.Next())builder.Add(crossing,sh.Current());
    double v=0;check(!C::singleSolid(crossing,v),"interfering solid accepted by direct-edit certificate");
    checkShape(crossing,false,0,1);
    for(const auto& shape:{source,SplineFixture::solid()}) {
        const auto stamp=brep(shape);
        for(int trial=0;trial<3;++trial)for(bool legacy:{true,false}) {
            BRepTiming::reset();BRepTiming::enabled=true;size_t n=0;double volume=0;
            const auto begin=std::chrono::steady_clock::now();
            const bool ok=legacy?legacyCertifiedSolid(shape,volume,n):BooleanGeometry::certifiedSolid(shape,volume,n);
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
            BRepTiming::enabled=false;check(ok&&n==1,"timed certificate failed");
            near(volume,shape.IsEqual(source)?1000:SplineFixture::area*20,"independent certificate volume");
            std::cout<<"ROOT_TIMING "<<(shape.IsEqual(source)?"box":"spline")<<" "<<trial<<" "<<(legacy?"before":"after")<<" ms="<<ms<<" validations="<<BRepTiming::stats["BooleanGeometry.validSolid"].calls<<"\n";
        }
        check(brep(shape)==stamp,"timing changed source");
    }
    std::cout<<"Single-solid reuse, orientation, components, cavity and invalid-input checks passed\n";
}

// Frozen pre-F2.8B singleSolid behavior. Public measurement still independently
// validates, so this reference retains the original second predicate.
bool legacySingleSolid(const TopoDS_Shape& shape,double& volume) {
    const auto count=[](const TopoDS_Shape& s,TopAbs_ShapeEnum type) {
        TopTools_IndexedMapOfShape m;TopExp::MapShapes(s,type,m);return m.Extent();
    };
    if(shape.IsNull()||count(shape,TopAbs_SOLID)!=1||count(shape,TopAbs_SHELL)!=1||!C::validBRep(shape))return false;
    TopExp_Explorer solid(shape,TopAbs_SOLID);if(!C::closedShells(shape))return false;
    for(auto type:{TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX})if(count(shape,type)!=count(solid.Current(),type))return false;
    BRepMeasurement::Result measured;QString error;if(!BRepMeasurement::volume(shape,measured,error))return false;
    volume=measured.properties.Mass();if(!std::isfinite(volume)||volume<=1.e-12)return false;
    BRepBuilderAPI_Copy isolated(shape,true,false);BOPAlgo_ArgumentAnalyzer analyzer;
    analyzer.SetShape1(isolated.Shape());analyzer.SelfInterMode()=true;analyzer.StopOnFirstFaulty()=true;analyzer.Perform();
    return !analyzer.HasErrors()&&!analyzer.HasFaulty();
}
void validityReuse() {
    using Ticket=BRepMeasurement::detail::ValidatedInputVolume;
    static_assert(!std::is_constructible_v<Ticket,const TopoDS_Shape&>);
    static_assert(!std::is_copy_constructible_v<Ticket>&&!std::is_move_constructible_v<Ticket>);
    const auto source=box(0,0,0,40,30,20);const auto stamp=brep(source);
    auto single=[&](const TopoDS_Shape& shape,bool expected,unsigned fresh,unsigned reused) {
        const auto before=brep(shape);double oldVolume=0,volume=0;
        const bool old=legacySingleSolid(shape,oldVolume);
        BRepTiming::reset();BRepTiming::enabled=true;BRepTiming::detailed=true;
        const bool now=C::singleSolid(shape,volume);BRepTiming::enabled=BRepTiming::detailed=false;
        unsigned checks=0,tickets=0;
        for(const auto& e:BRepTiming::details) {
            if(e.kind=="Validate.BRep") {++checks;check(e.a>0&&BRepTiming::shapes[e.a-1].IsEqual(shape),"validated another shape/location/orientation");}
            if(e.kind=="Validate.BRep.Reused") {++tickets;check(e.a>0&&BRepTiming::shapes[e.a-1].IsEqual(shape),"handoff targeted another shape");}
        }
        check(now==expected&&old==now,"pre-F2.8B certificate verdict changed");
        check(checks==fresh&&tickets==reused,"incorrect validity/handoff count");
        if(now)near(volume,oldVolume,"pre-F2.8B volume changed");
        check(brep(shape)==before,"singleSolid modified geometry");return volume;
    };
    near(single(source,true,1,1),24000,"source volume");
    for(double delta:{15.,-5.}) {
        BRepTiming::reset();BRepTiming::enabled=BRepTiming::detailed=true;QString error;
        auto moved=MoveFaceGeometry::move(source,top(source,20),delta,error,MoveFaceGeometry::ReconstructionPolicy::AxialInvariantCap);
        BRepTiming::enabled=BRepTiming::detailed=false;
        unsigned fresh=0,reused=0,booleans=0,si=0;
        for(const auto& event:BRepTiming::details) {
            fresh+=event.kind=="Validate.BRep";reused+=event.kind=="Validate.BRep.Reused";
            booleans+=event.kind.rfind("Boolean.",0)==0;si+=event.kind=="Validate.SelfInterference";
        }
        check(!moved.IsNull()&&fresh==(delta>0?25:28)&&reused==6,"Box pipeline did not remove exactly six local duplicates (five singleSolid, one Boolean)");
        check(booleans==(delta>0?15:16)&&si==5,"Box material/SI pipeline changed");
        const auto expected=box(0,0,0,40,30,20+delta);
        check(C::equivalent(moved,expected),"Move result differs from independent exact Box oracle");
        near(single(moved,true,1,1),40*30*(20+delta),"Move volume changed");
        std::cout<<"VALIDITY_REUSE Box delta="<<delta<<" fresh="<<fresh<<" reused="<<reused<<" Boolean="<<booleans<<" SI="<<si<<'\n';
    }
    BRep_Builder builder;auto invalid=BRepBuilderAPI_Copy(source,true,false).Shape();
    auto shell=TopoDS::Shell(TopExp_Explorer(invalid,TopAbs_SHELL).Current());shell.Free(true);
    builder.Remove(shell,TopExp_Explorer(shell,TopAbs_FACE).Current());single(invalid,false,1,0);
    QString invalidError;const auto invalidStamp=brep(invalid);
    check(MoveFaceGeometry::move(invalid,top(invalid,20),15,invalidError,MoveFaceGeometry::ReconstructionPolicy::AxialInvariantCap).IsNull(),"invalid Move source accepted");
    check(brep(invalid)==invalidStamp,"failed Move modified invalid source");
    auto changed=BRepBuilderAPI_Copy(source,true,false).Shape();single(changed,true,1,1);
    const auto sameHandle=changed;auto v=TopoDS::Vertex(TopExp_Explorer(changed,TopAbs_VERTEX).Current());
    builder.UpdateVertex(v,BRep_Tool::Pnt(v).Translated(gp_Vec(1,0,0)),BRep_Tool::Tolerance(v));
    check(changed.IsEqual(sameHandle),"mutation fixture unexpectedly replaced root identity");single(changed,false,1,0);
    // A changed but valid copy still gets its own fresh analyzer.
    gp_Trsf transform;transform.SetTranslation(gp_Vec(7,11,3));auto located=source.Moved(TopLoc_Location(transform));
    single(located,true,1,1);single(source.Reversed(),false,1,1);
    TopoDS_Shape normalized;QString error;
    check(BRepReconstruction::normalize(source,normalized,error,true)&&!normalized.IsEqual(source),"normalization fixture did not produce separate topology");
    single(normalized,true,1,1); // No receipt transfers across normalization.
    BRepMeasurement::Result measured;
    for(bool area:{false,true}) {
        BRepTiming::reset();BRepTiming::enabled=BRepTiming::detailed=true;
        const bool ok=area?BRepMeasurement::area(normalized,measured,error):BRepMeasurement::volume(normalized,measured,error);
        BRepTiming::enabled=BRepTiming::detailed=false;unsigned fresh=0,reused=0;
        for(const auto& e:BRepTiming::details){fresh+=e.kind=="Validate.BRep";reused+=e.kind=="Validate.BRep.Reused";}
        check(ok&&fresh==1&&reused==0,"public measurement reused a previous invocation's validity");
    }
    check(!BRepMeasurement::volume(changed,measured,error)&&!error.isEmpty(),"public measurement accepted a mutated copied shape");
    check(brep(source)==stamp,"copy/normalization changed source");
    std::cout<<"Invocation-local validity, private access, changed copy, located/reversed and normalized-shape checks passed\n";
}

void booleanValidityReuse() {
    const auto source=box(0,0,0,40,30,20);const auto stamp=brep(source);
    // Compare the complete Boolean certificate with the frozen older path,
    // while requiring independent current validation for every owned component.
    auto certificate=[&](const TopoDS_Shape& shape,bool expected,size_t components,unsigned fresh,unsigned reused) {
        const auto before=brep(shape);double oldVolume=0,volume=0;size_t oldCount=0,count=0;
        const bool old=legacyCertifiedSolid(shape,oldVolume,oldCount);
        BRepTiming::reset();BRepTiming::enabled=BRepTiming::detailed=true;
        const bool now=BooleanGeometry::certifiedSolid(shape,volume,count);
        BRepTiming::enabled=BRepTiming::detailed=false;unsigned checks=0,handoffs=0;
        TopTools_IndexedMapOfShape permitted;permitted.Add(shape);
        for(TopExp_Explorer solid(shape,TopAbs_SOLID);solid.More();solid.Next())permitted.Add(solid.Current());
        for(const auto& event:BRepTiming::details) {
            if(event.kind=="Validate.BRep"||event.kind=="Validate.BRep.Reused") {
                const auto& actual=BRepTiming::shapes.at(event.a-1);bool bound=false;
                for(int i=1;i<=permitted.Extent();++i)bound=bound||permitted(i).IsEqual(actual);
                check(bound,"Boolean handoff/fresh check targeted unrelated orientation/location/topology");
            }
            checks+=event.kind=="Validate.BRep";handoffs+=event.kind=="Validate.BRep.Reused";
        }
        check(old==expected&&now==old&&count==oldCount,"Boolean pre-F2.8C decision/component count changed");
        check(checks==fresh&&handoffs==reused,"Boolean root/component freshness changed");
        if(now){near(volume,oldVolume,"Boolean volume changed");check(count==components,"Boolean solid count changed");}
        check(brep(shape)==before,"Boolean certificate changed geometry");
    };
    certificate(source,true,1,1,1);
    certificate(compound({source}),true,1,2,2);
    certificate(compound({source,box(100,0,0,10,10,10)}),true,2,3,3);
    const auto hollow=BooleanGeometry::combine<BRepAlgoAPI_Cut>(source,box(4,4,4,4,4,4));
    certificate(hollow,true,1,2,2);double volume=0;
    check(!C::singleSolid(hollow,volume),"Boolean cavity policy leaked into direct-edit single-shell certificate");
    certificate(source.Reversed(),false,0,1,1);
    gp_Trsf location;location.SetTranslation(gp_Vec(7,11,3));certificate(source.Moved(TopLoc_Location(location)),true,1,1,1);
    auto invalid=BRepBuilderAPI_Copy(source,true,false).Shape();BRep_Builder builder;
    auto shell=TopoDS::Shell(TopExp_Explorer(invalid,TopAbs_SHELL).Current());shell.Free(true);
    builder.Remove(shell,TopExp_Explorer(shell,TopAbs_FACE).Current());certificate(invalid,false,0,1,0);
    auto changed=BRepBuilderAPI_Copy(source,true,false).Shape();certificate(changed,true,1,1,1);auto identity=changed;
    auto v=TopoDS::Vertex(TopExp_Explorer(changed,TopAbs_VERTEX).Current());
    builder.UpdateVertex(v,BRep_Tool::Pnt(v).Translated(gp_Vec(1,0,0)),BRep_Tool::Tolerance(v));
    check(changed.IsEqual(identity),"Boolean mutation fixture replaced root");certificate(changed,false,0,1,0);
    TopoDS_Shape normalized;QString error;
    check(BRepReconstruction::normalize(source,normalized,error,true)&&!normalized.IsEqual(source),"Boolean normalized input fixture");
    const auto calls=normalized.ShapeType()==TopAbs_SOLID?1:2;certificate(normalized,true,1,calls,calls);
    BRepMeasurement::Result measured;
    BRepTiming::reset();BRepTiming::enabled=BRepTiming::detailed=true;
    check(BRepMeasurement::volume(normalized,measured,error),"public measurement after Boolean certificate");
    BRepTiming::enabled=BRepTiming::detailed=false;unsigned fresh=0,reused=0;
    for(const auto& event:BRepTiming::details){fresh+=event.kind=="Validate.BRep";reused+=event.kind=="Validate.BRep.Reused";}
    check(fresh==1&&reused==0,"Boolean validity escaped into a later public measurement");
    check(!BRepMeasurement::volume(changed,measured,error),"mutated Boolean input inherited stale validity");
    check(brep(source)==stamp,"Boolean copies/normalization mutated source");
    std::cout<<"Boolean invocation-local reuse: single/compound/components/cavity/reversed/located/invalid/mutated/normalized inputs passed\n";
}
void materials() {
    const auto source=box(0,0,0,10,10,10);
    const auto same=box(0,0,0,10,10,10), shifted=box(2,0,0,10,10,10);
    check(C::equivalent(source,same),"identical material refused");
    near(C::volume(source),C::volume(shifted),"equal-volume fixture");
    check(!C::equivalent(source,shifted),"equal volume substituted for material equivalence");
    check(C::occupancy(source,gp_Pnt(1,5,5),1.e-7)==TopAbs_IN
       &&C::occupancy(shifted,gp_Pnt(1,5,5),1.e-7)==TopAbs_OUT,"material occupancy mismatch not observable");
    C::MaterialDifference difference;QString error;
    check(C::difference(source,same,difference,error),"identical difference failed");
    near(C::volume(difference.removed),0,"identical material removed");
    near(C::volume(difference.added),0,"identical material added");
    check(C::difference(source,shifted,difference,error),"symmetric difference failed");
    near(C::volume(difference.removed),200,"wrong removed material");
    near(C::volume(difference.added),200,"wrong added material");
    check(!C::difference({},source,difference,error)&&difference.removed.IsNull()&&difference.added.IsNull(),"failed difference retained output");

    const auto cavity=box(3,3,3,4,4,4);
    const auto hollow=BooleanGeometry::combine<BRepAlgoAPI_Cut>(source,cavity);
    check(C::validBRep(hollow)&&C::closedShells(hollow),"invalid internal cavity fixture");
    double volume=0;check(!C::singleSolid(hollow,volume),"direct-edit single-shell policy broadened");
    check(BooleanGeometry::validSolid(hollow,volume),"Boolean cavity policy lost");
    check(!C::equivalent(hollow,source),"missing internal cavity accepted");
    // Compensate cavity volume by removing unrelated outer material: equal
    // volume still cannot hide a missing cavity or its changed distribution.
    const auto compensated=BooleanGeometry::combine<BRepAlgoAPI_Cut>(source,box(0,0,0,.64,10,10));
    near(C::volume(hollow),C::volume(compensated),"compensated cavity fixture");
    check(!C::equivalent(hollow,compensated),"equal-volume cavity relocation accepted");
    check(C::occupancy(hollow,gp_Pnt(5,5,5),1.e-7)==TopAbs_OUT,"cavity occupancy wrong");
    check(!C::preservedOutside(hollow,source,box(0,0,10,10,10,2)),"protected internal cavity disappeared");

    check(C::singleSolid(source,volume)&&C::manifold(source),"valid box certificate changed");
    check(!C::manifold(rectangle(0,10)),"open surface accepted as manifold");
    BRepBuilderAPI_Copy coarse(source,true,false);BRep_Builder builder;
    TopExp_Explorer vertex(coarse.Shape(),TopAbs_VERTEX);builder.UpdateVertex(TopoDS::Vertex(vertex.Current()),2.e-6);
    double offending=0;check(!C::withinTolerance(coarse.Shape(),1.e-6,&offending)&&offending>1.e-6,"coarse topology tolerance accepted");
    check(C::withinTolerance(source,1.e-6),"coarse copy changed source tolerances");
    std::cout<<"Material, cavity and validity certificates passed\n";
}
void localAndIsolation() {
    NativePartState part;part.shape=box(0,0,0,10,10,10);part.id="synthetic-id";part.name="Certification fixture";
    part.visible=false;part.color=QColor(31,73,109);part.geometryKind=7;
    part.cylinderPrimitive=true;part.cylinderRadius=999;part.cylinderHeight=321;
    part.edgeBlendRecipe=QJsonObject{{"untrusted",true}};part.transform.SetTranslation(gp_Vec(7,11,13));
    const auto before=part;const auto serialized=brep(part.shape);
    const auto cap=top(part.shape,10);BRepReconstruction::Result moved;QString error;
    check(BRepReconstruction::movePlanarLocal(part.shape,cap,2,moved,error),"valid local reconstruction failed");
    const auto region=box(0,0,10,10,10,2);
    near(C::volume(moved.solid),1200,"local volume wrong");
    check(C::equivalent(moved.solid,box(0,0,0,10,10,12)),"local material wrong");
    check(C::preservedOutside(part.shape,moved.solid,region),"valid local change altered protected material");
    C::MaterialDifference delta;check(C::difference(part.shape,moved.solid,delta,error),"local difference failed");
    check(C::equivalent(delta.added,region),"intended local addition not exact");
    near(C::volume(delta.removed),0,"local addition removed material");
    const auto unexpected=BooleanGeometry::combine<BRepAlgoAPI_Cut>(moved.solid,box(1,1,1,2,2,2));
    check(!C::preservedOutside(part.shape,unexpected,region),"unexpected protected-region loss accepted");
    BRepReconstruction::TopologyIndex index;check(BRepReconstruction::TopologyIndex::build(moved.solid,index,error),"index local result");
    check(C::preservedFace(top(part.shape,0),index,1.e-8),"unrelated bottom region changed");
    BRepReconstruction::Result refused;
    check(!BRepReconstruction::movePlanarLocal(part.shape,cap,-20,refused,error)&&refused.solid.IsNull(),"invalid local change accepted");
    check(brep(part.shape)==serialized&&part.shape.IsSame(before.shape),"certificate mutated source topology");
    check(part.id==before.id&&part.name==before.name&&part.color==before.color&&part.visible==before.visible
       &&part.geometryKind==before.geometryKind&&part.cylinderPrimitive==before.cylinderPrimitive
       &&part.cylinderRadius==before.cylinderRadius&&part.cylinderHeight==before.cylinderHeight
       &&part.edgeBlendRecipe==before.edgeBlendRecipe,"certificate changed metadata");
    for(int row=1;row<=3;++row) for(int col=1;col<=4;++col)
        check(part.transform.Value(row,col)==before.transform.Value(row,col),"certificate changed placement");
    std::cout<<"Local preservation and source/metadata isolation passed\n";
}
void correspondence() {
    const auto whole=rectangle(0,10),same=rectangle(0,10),left=rectangle(0,4),right=rectangle(4,10);
    const auto shifted=rectangle(5,15),remote=rectangle(20,30);
    check(C::sameCarrier(whole,shifted)&&!C::sameTrimmedRegion(whole,shifted),"carrier match treated as trimmed identity");
    check(C::sameTrimmedRegion(whole,same),"identical trimmed region not matched");
    C::Correspondence map;QString error;
    check(C::correspond(whole,same,map,error)&&map.faces.size()==1&&map.faces[0].relation==C::FaceRelation::SameRegion,"trimmed region correspondence failed");
    check(C::correspond(whole,compound({left,right}),map,error)&&map.faces[0].relation==C::FaceRelation::Split
       &&map.faces[0].regions.overlaps.size()==2&&map.newFaces.empty(),"split correspondence failed");
    check(C::correspond(compound({left,right}),whole,map,error)&&map.faces.size()==2
       &&std::all_of(map.faces.begin(),map.faces.end(),[](const auto& f){return f.relation==C::FaceRelation::Merged;}),"merged correspondence failed");
    check(C::correspond(whole,shifted,map,error)&&map.faces[0].relation==C::FaceRelation::Modified,"modified trims misidentified");
    check(C::correspond(whole,remote,map,error)&&map.faces[0].relation==C::FaceRelation::Removed
       &&map.faces[0].regions.carriers.size()==1&&map.faces[0].regions.overlaps.empty()&&map.newFaces.size()==1,"removed/new region confused with same carrier");
    check(!C::correspond(whole,compound({same,rectangle(0,10)}),map,error)
       &&map.faces.empty()&&map.newFaces.empty()&&!error.isEmpty(),"duplicate result regions mapped ambiguously");
    check(!C::correspond(compound({whole,same}),left,map,error)&&map.faces.empty(),"duplicate source regions mapped ambiguously");
    check(!C::correspond(compound({left,right}),compound({rectangle(0,6),rectangle(6,10)}),map,error)
       &&map.faces.empty(),"many-to-many trim repartition guessed identities");
    check(!C::correspond(BRepPrimAPI_MakeSphere(3).Shape(),BRepPrimAPI_MakeSphere(3).Shape(),map,error)
       &&map.faces.empty(),"unsupported carrier correspondence accepted");
    std::cout<<"Trimmed, split, merged, removed/new and ambiguous correspondence passed\n";
}
void constructionParity() {
    const auto primitive=box(0,0,0,10,10,10);
    BRepBuilderAPI_MakePolygon polygon;polygon.Add(gp_Pnt(0,0,0));polygon.Add(gp_Pnt(10,0,0));
    polygon.Add(gp_Pnt(10,10,0));polygon.Add(gp_Pnt(0,10,0));polygon.Close();
    const auto swept=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(polygon.Wire()).Face(),gp_Vec(0,0,10)).Shape();
    const auto united=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(box(0,0,0,4,10,10),box(4,0,0,6,10,10));
    for(const auto& shape:{swept,united}) {
        const auto before=brep(shape);double volume=0;
        check(C::singleSolid(shape,volume)&&C::equivalent(primitive,shape),"construction route changed certificate");
        C::Correspondence map;QString error;check(C::correspond(primitive,shape,map,error),"construction route correspondence refused");
        check(map.newFaces.empty(),"construction route invented regions");
        check(brep(shape)==before,"parity analysis mutated source");
    }
    std::cout<<"Equivalent primitive/prism/Boolean B-Reps passed\n";
}
}
int main() {
    try {validityReuse();booleanValidityReuse();rootValidation();materials();localAndIsolation();correspondence();constructionParity();return 0;}
    catch(const Standard_Failure& failure) {std::cerr<<failure.GetMessageString()<<'\n';return 1;}
    catch(const std::exception& failure) {std::cerr<<failure.what()<<'\n';return 1;}
}
