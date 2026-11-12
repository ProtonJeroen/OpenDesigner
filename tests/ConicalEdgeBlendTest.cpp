// v0.5.2 conical Fillet/Chamfer certification on a synthetic replica of the
// private "Cone 3" Part (fillet.odc). Dimensions measured from that document:
// cone R40 (z=0) -> R20 (z=80) on axis (250,0,z), top Chamfer 8 mm, bottom
// Fillet 11 mm, two planar caps. The repository test never reads fillet.odc.
#include "EdgeBlendFeatures.h"
#include "FilletGeometry.h"
#include "ChamferGeometry.h"
#include "BRepReconstruction.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <QJsonArray>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
using namespace EdgeBlendFeatures;
const gp_Pnt origin(250,0,0);
void check(bool value,const std::string& message) {if(!value) throw std::runtime_error(message);}
double volume(const TopoDS_Shape& s) {GProp_GProps p;BRepGProp::VolumeProperties(s,p);return p.Mass();}
std::string stamp(const TopoDS_Shape& s) {std::ostringstream o;BRepTools::Write(s,o);return o.str();}
void strict(const TopoDS_Shape& shape) {
    double v=0;check(BRepReconstruction::validSolid(shape,v),"invalid solid");
    for(auto type:{TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX}) for(TopExp_Explorer it(shape,type);it.More();it.Next()) {
        const double t=type==TopAbs_FACE?BRep_Tool::Tolerance(TopoDS::Face(it.Current())):
            type==TopAbs_EDGE?BRep_Tool::Tolerance(TopoDS::Edge(it.Current())):BRep_Tool::Tolerance(TopoDS::Vertex(it.Current()));
        check(std::isfinite(t)&&t<=1.e-6,"tolerance above 1e-6");
    }
}
// Circular edge at height z (along +Z) in shape.
TopoDS_Edge rim(const TopoDS_Shape& s,double z,const gp_Trsf& placement={}) {
    for(TopExp_Explorer it(s,TopAbs_EDGE);it.More();it.Next()) {
        BRepAdaptor_Curve c(TopoDS::Edge(it.Current()));
        if(c.GetType()!=GeomAbs_Circle) continue;
        auto local=c.Circle().Location().Transformed(placement.Inverted());
        if(std::abs(local.Z()-z)<1.e-6) return TopoDS::Edge(it.Current());
    }
    return {};
}
TopoDS_Face capAt(const TopoDS_Shape& s,double z,const gp_Trsf& placement={}) {
    for(TopExp_Explorer it(s,TopAbs_FACE);it.More();it.Next()) {
        BRepAdaptor_Surface a(TopoDS::Face(it.Current()));
        if(a.GetType()==GeomAbs_Plane&&std::abs(a.Plane().Location().Transformed(placement.Inverted()).Z()-z)<1.e-6
           &&std::abs(a.Plane().Distance(gp_Pnt(0,0,z).Transformed(placement)))<1.e-6) return TopoDS::Face(it.Current());
    }
    return {};
}
TopoDS_Shape frustum(double z0,double r0,double z1,double r1) {
    return BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(origin.X(),origin.Y(),z0),gp_Dir(0,0,1)),r0,r1,z1-z0).Shape();
}
// Independent oracle: OCCT kernel blends on an explicitly constructed frustum.
TopoDS_Shape kernel(const TopoDS_Shape& sharp,double zTop,double chamfer,double zBottom,double fillet) {
    TopoDS_Shape shape=sharp;
    if(chamfer>0) {BRepFilletAPI_MakeChamfer c(shape);c.Add(chamfer,rim(shape,zTop));c.Build();check(c.IsDone(),"oracle chamfer");shape=c.Shape();}
    if(fillet>0) {BRepFilletAPI_MakeFillet f(shape);f.Add(fillet,rim(shape,zBottom));f.Build();check(f.IsDone(),"oracle fillet");shape=f.Shape();}
    return shape;
}
// Synthetic Cone 3 replica.
TopoDS_Shape cone3() {return kernel(frustum(0,40,80,20),80,8,0,11);}
const Feature* find(const State& s,Kind kind) {
    for(const auto& f:s.features) if(f.kind==kind) return &f;
    return nullptr;
}
// Recognize from geometry only (no recipe), as for an imported or recipe-free Part.
State recognize(const TopoDS_Shape& shape) {
    State state;QString error;check(resolve(shape,state,error),"recognition failed: "+error.toStdString());return state;
}
}

int main() {
try {
    QString error;const double pi=std::acos(-1.);
    const double half=std::atan(20./80.); // Body half-angle, 14.036 degrees.

    // 1. Ordinary cones are non-feature geometry.
    const auto plain=frustum(0,40,80,20);
    {State s;check(!resolve(plain,s,error)&&!error.isEmpty()&&s.features.empty(),"ordinary frustum recognized as transitions");}
    {State s;auto pointed=BRepPrimAPI_MakeCone(gp_Ax2(origin,gp_Dir(0,0,1)),40,0,80).Shape();
     check(!resolve(pointed,s,error)&&s.features.empty(),"pointed cone recognized");}

    // 2. Conical circular-edge Fillet creation.
    State filleted;TopoDS_Shape filletShape;
    check(create(plain,{},rim(plain,0),Kind::Fillet,11,filleted,filletShape,error),"conical Fillet: "+error.toStdString());
    strict(filletShape);
    check(filleted.features.size()==1&&(filleted.features[0].cones[0]||filleted.features[0].cones[1]),"Fillet recipe lacks cone support");
    check(BRepAdaptor_Surface(filleted.features[0].faces.front()).GetType()==GeomAbs_Torus,"conical Fillet face is not a torus");
    check(BRepReconstruction::equivalent(filletShape,kernel(plain,80,0,0,11)),"conical Fillet differs from kernel oracle");

    // 3. Conical circular-edge Chamfer creation, distance convention verified analytically.
    State chamfered;TopoDS_Shape chamferShape;
    check(create(plain,{},rim(plain,80),Kind::Chamfer,8,chamfered,chamferShape,error),"conical Chamfer: "+error.toStdString());
    strict(chamferShape);
    const auto band=chamfered.features.at(0).faces.front();
    check(BRepAdaptor_Surface(band).GetType()==GeomAbs_Cone,"conical Chamfer face is not a cone");
    {   // Sharp circle R20 at z=80: cap boundary at R12, body boundary 8 mm down the generator.
        bool cap=false,body=false;
        for(TopExp_Explorer it(band,TopAbs_EDGE);it.More();it.Next()) {
            BRepAdaptor_Curve c(TopoDS::Edge(it.Current()));if(c.GetType()!=GeomAbs_Circle) continue;
            const auto circle=c.Circle();const double z=circle.Location().Z(),r=circle.Radius();
            if(std::abs(z-80)<1.e-9) {cap=true;check(std::abs(r-12)<1.e-9,"Chamfer cap distance is not 8 mm");}
            else {body=true;check(std::abs(std::hypot(r-20,80-z)-8)<1.e-9&&std::abs(r-(40-z*std::tan(half)))<1.e-9,"Chamfer body distance is not 8 mm along the generator");}
        }
        check(cap&&body,"Chamfer boundaries missing");
    }
    check(BRepReconstruction::equivalent(chamferShape,kernel(plain,80,8,0,0)),"conical Chamfer differs from kernel oracle");

    // Building both through the recipe path equals the replica.
    State both;TopoDS_Shape bothShape;
    check(create(chamferShape,chamfered,rim(chamferShape,0),Kind::Fillet,11,both,bothShape,error),"second transition: "+error.toStdString());
    const auto replica=cone3();strict(replica);
    check(BRepReconstruction::equivalent(bothShape,replica),"recipe-built Cone 3 differs from replica");

    // 4./5. Recipe-free recovery of the 8 mm Chamfer and 11 mm Fillet.
    const auto replicaStamp=stamp(replica);
    State recovered=recognize(replica);
    check(recovered.features.size()==2,"Cone 3 needs exactly two transitions");
    const auto* c=find(recovered,Kind::Chamfer);const auto* f=find(recovered,Kind::Fillet);
    check(c&&std::abs(c->size-8)<1.e-6,"8 mm Chamfer not recovered");
    check(f&&std::abs(f->size-11)<1.e-6,"11 mm Fillet not recovered");
    check(selected(recovered,c->faces.front())>=0&&selected(recovered,f->faces.front())>=0,"recovered faces not selectable");
    for(TopExp_Explorer it(replica,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());const auto type=BRepAdaptor_Surface(face).GetType();
        const bool transition=face.IsSame(c->faces.front())||face.IsSame(f->faces.front());
        check(transition==(selected(recovered,face)>=0),"body cone or cap classified as transition");
        if(type==GeomAbs_Plane) check(!transition,"cap classified as transition");
    }
    {   // Recovered sharp source is the frustum itself.
        check(BRepReconstruction::equivalent(recovered.sharp,plain),"recovered sharp source is not the frustum");
    }
    const auto fid=f->id,cid=c->id;

    // 6. Independent dimension edits.
    State edited;TopoDS_Shape result;
    check(edit(replica,recovered,fid,6,edited,result,error),"Fillet edit: "+error.toStdString());strict(result);
    check(BRepReconstruction::equivalent(result,kernel(plain,80,8,0,6)),"Fillet edit material wrong");
    check(std::abs(find(edited,Kind::Chamfer)->size-8)<1.e-12&&find(edited,Kind::Chamfer)->id==cid&&find(edited,Kind::Fillet)->id==fid,"Fillet edit changed Chamfer/identity");
    {State again=recognize(result);check(std::abs(find(again,Kind::Fillet)->size-6)<1.e-6&&std::abs(find(again,Kind::Chamfer)->size-8)<1.e-6,"edited Fillet not re-recognized");}
    check(edit(replica,recovered,cid,5,edited,result,error),"Chamfer edit: "+error.toStdString());strict(result);
    check(BRepReconstruction::equivalent(result,kernel(plain,80,5,0,11)),"Chamfer edit material wrong");
    check(std::abs(find(edited,Kind::Fillet)->size-11)<1.e-12&&find(edited,Kind::Chamfer)->id==cid,"Chamfer edit changed Fillet/identity");
    for(double invalid:{0.,-1.,60.,std::numeric_limits<double>::quiet_NaN()}) {
        State none;TopoDS_Shape nothing;
        check(!edit(replica,recovered,fid,invalid,none,nothing,error)&&nothing.IsNull()&&!error.isEmpty(),"invalid Fillet radius accepted");
    }

    // 7./8. Delete transitions with sharp reconstruction.
    check(remove(replica,recovered,{f->faces.front()},edited,result,error),"Delete Fillet: "+error.toStdString());strict(result);
    check(BRepReconstruction::equivalent(result,kernel(plain,80,8,0,0))&&edited.features.size()==1&&edited.features[0].id==cid,"Delete Fillet wrong");
    check(remove(replica,recovered,{c->faces.front()},edited,result,error),"Delete Chamfer: "+error.toStdString());strict(result);
    check(BRepReconstruction::equivalent(result,kernel(plain,80,0,0,11))&&edited.features.size()==1&&edited.features[0].id==fid,"Delete Chamfer wrong");
    check(remove(replica,recovered,{c->faces.front(),f->faces.front()},edited,result,error)&&BRepReconstruction::equivalent(result,plain)
          &&edited.features.empty(),"Delete both did not restore the frustum");
    {   // Body cone and caps are not transitions.
        State none;TopoDS_Shape nothing;
        for(TopExp_Explorer it(replica,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());if(selected(recovered,face)>=0) continue;
            check(!remove(replica,recovered,{face},none,nothing,error)&&nothing.IsNull(),"non-transition face deleted as transition");
        }
    }

    // 9./10./11. Cap Pull/Move with Keep ON and OFF, both caps, both directions.
    const double t=std::tan(half);
    for(bool top:{true,false}) for(double d:{5.,-5.}) for(bool keep:{true,false}) {
        const auto face=capAt(replica,top?80:0);check(!face.IsNull(),"cap missing");
        State after;TopoDS_Shape moved;
        check(move(replica,recovered,face,d,keep,after,moved,error),"cap move: "+error.toStdString());strict(moved);
        // Outward is +Z on top, -Z at the bottom.
        const double z0=top?0:-d,z1=top?80+d:80;
        const auto sharp=frustum(z0,40-z0*t,z1,40-z1*t);
        const bool chamferKept=keep||!top,filletKept=keep||top;
        const auto expected=kernel(sharp,z1,chamferKept?8:0,z0,filletKept?11:0);
        check(BRepReconstruction::equivalent(moved,expected),"cap move differs from independent construction");
        check(after.features.size()==size_t(chamferKept)+size_t(filletKept),"Keep policy removed wrong transitions");
        if(chamferKept) check(find(after,Kind::Chamfer)->id==cid&&std::abs(find(after,Kind::Chamfer)->size-8)<1.e-12,"Chamfer distance/identity changed");
        if(filletKept) check(find(after,Kind::Fillet)->id==fid&&std::abs(find(after,Kind::Fillet)->size-11)<1.e-12,"Fillet radius/identity changed");
        State replay=decode(encode(after));check(resolve(moved,replay,error),"moved recipe failed replay: "+error.toStdString());
        if(!after.features.empty()) {State again=recognize(moved);check(again.features.size()==after.features.size(),"moved geometry not re-recognized");}
    }
    // Rotate Face on a cap breaks coaxiality: explicit safe refusal.
    for(bool keep:{true,false}) {
        State after;TopoDS_Shape rotated;
        check(!rotate(replica,recovered,capAt(replica,80),gp_Ax1(gp_Pnt(250,0,80),gp_Dir(1,0,0)),5,keep,after,rotated,error)
              &&rotated.IsNull()&&error.contains("coaxiality"),"cap rotation not refused");
    }

    // 12. Recipe persistence: version 3, identity and revalidation.
    const auto stored=encode(recovered);
    check(stored.value("version").toInt()==3,"conical recipe must be version 3");
    {State loaded=decode(stored);loaded.certified.Nullify();
     check(resolve(replica,loaded,error)&&find(loaded,Kind::Fillet)->id==fid&&find(loaded,Kind::Chamfer)->id==cid,"persisted recipe not certified");}
    {auto old=stored;old.insert("version",2);check(decode(old).features.empty(),"version 2 recipe with cones accepted");}
    {State tampered=decode(stored);tampered.certified.Nullify();
     for(auto& feature:tampered.features) for(auto& cone:feature.cones) if(cone) cone->SetSemiAngle(cone->SemiAngle()*1.01);
     check(resolve(replica,tampered,error),"tampered recipe must fall back to geometric recognition");
     check(find(tampered,Kind::Fillet)->id!=fid,"tampered recipe retained authoritative identity");}
    {   // A persisted recipe for other geometry never certifies this Part.
        State loaded=decode(stored);loaded.certified.Nullify();State none;TopoDS_Shape nothing;
        auto other=kernel(frustum(0,40,80,20),80,6,0,11);
        check(resolve(other,loaded,error)&&std::abs(find(loaded,Kind::Chamfer)->size-6)<1.e-6,"stale recipe trusted over current geometry");
    }

    // 13. Undo/Redo safety at geometry level: every operation left the source unchanged.
    check(stamp(replica)==replicaStamp,"source geometry mutated");
    check(recovered.certified.IsSame(replica)&&find(recovered,Kind::Fillet)->size==11,"source recipe mutated");

    // 14. Transformed Parts (tilted axis) and duplicated geometry.
    gp_Trsf placement;placement.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,1,0)),0.7);
    gp_Trsf shift;shift.SetTranslation(gp_Vec(-30,15,7));placement=shift*placement;
    const auto moved=BRepBuilderAPI_Transform(replica,placement,true).Shape();
    {   State tilted=recognize(moved);
        check(std::abs(find(tilted,Kind::Fillet)->size-11)<1.e-6&&std::abs(find(tilted,Kind::Chamfer)->size-8)<1.e-6,"tilted Cone 3 dimensions");
        State after;TopoDS_Shape r;
        check(edit(moved,tilted,find(tilted,Kind::Chamfer)->id,4,after,r,error),"tilted edit: "+error.toStdString());strict(r);
        check(BRepReconstruction::equivalent(r,BRepBuilderAPI_Transform(kernel(plain,80,4,0,11),placement,true).Shape()),"tilted edit material");
        check(move(moved,tilted,capAt(moved,80,placement),3,true,after,r,error),"tilted cap move: "+error.toStdString());strict(r);
    }
    {   // A duplicate is an independent copy: its own recognition, its own recipe.
        const auto duplicate=BRepBuilderAPI_Transform(replica,gp_Trsf(),true).Shape();
        State copy=recognize(duplicate);
        check(!copy.certified.IsSame(recovered.certified),"duplicate shares certified topology");
        State after;TopoDS_Shape r;
        check(edit(duplicate,copy,find(copy,Kind::Fillet)->id,7,after,r,error)&&stamp(replica)==replicaStamp,"duplicate edit touched original");
        State loaded=decode(encode(recovered));check(resolve(duplicate,loaded,error),"copied recipe not revalidated on duplicate");
    }

    // 15. Stepped / ambiguous / invalid geometry refused, source unchanged.
    {   // Unequal-distance band (stepped cone): ordinary geometry.
        BRepFilletAPI_MakeChamfer unequal(plain);unequal.Add(8,5,rim(plain,80),capAt(plain,80));unequal.Build();check(unequal.IsDone(),"unequal fixture");
        const auto stepped=unequal.Shape();const auto before=stamp(stepped);State s;
        check(!resolve(stepped,s,error)&&error.contains("unequal")&&s.features.empty(),"stepped cone certified: "+error.toStdString());
        TopoDS_Face bandFace;for(TopExp_Explorer it(stepped,TopAbs_FACE);it.More();it.Next())
            if(BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType()==GeomAbs_Cone) bandFace=TopoDS::Face(it.Current());
        State none;TopoDS_Shape nothing;check(!remove(stepped,s,{bandFace},none,nothing,error)&&nothing.IsNull(),"stepped band deleted");
        check(stamp(stepped)==before,"refusal mutated stepped source");
    }
    {   // Symmetric double cone: both cones read as equal-distance Chamfers of the other. Ambiguous.
        const double h=20,r=20,m=r+h/(2*std::sqrt(3.));
        auto lower=frustum(0,r,h/2,m),upper=frustum(h/2,m,h,r);
        BRepAlgoAPI_Fuse fuse(lower,upper);fuse.SimplifyResult();check(fuse.IsDone(),"double cone fixture");
        const auto ambiguous=fuse.Shape();const auto before=stamp(ambiguous);State s;
        check(!resolve(ambiguous,s,error)&&error.contains("Ambiguous")&&s.features.empty(),"ambiguous double cone certified: "+error.toStdString());
        check(stamp(ambiguous)==before,"ambiguity refusal mutated source");
        // A slightly different middle radius is an ordinary stepped cone.
        auto l2=frustum(0,r,h/2,m+1),u2=frustum(h/2,m+1,h,r);BRepAlgoAPI_Fuse f2(l2,u2);f2.SimplifyResult();
        check(!resolve(f2.Shape(),s,error)&&s.features.empty(),"stepped double cone certified");
    }
    {   // Cap/Fillet ambiguity is never resolved by treating the body cone as Chamfer.
        State s=recognize(replica);int bodies=0;
        for(TopExp_Explorer it(replica,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());BRepAdaptor_Surface a(face);
            if(a.GetType()==GeomAbs_Cone&&std::abs(std::abs(a.Cone().SemiAngle())-half)<1.e-9) {++bodies;check(selected(s,face)<0,"body cone classified as Chamfer");}
        }
        check(bodies==1,"replica body cone missing");
    }
    {   // Interacting junction: creating a transition on the chamfered edge is refused.
        State none;TopoDS_Shape nothing;auto e=rim(replica,80);
        check(!create(replica,recovered,e,Kind::Fillet,1,none,nothing,error)&&nothing.IsNull(),"junction transition accepted");
    }
    check(stamp(replica)==replicaStamp,"source geometry mutated by refusals");
    std::cout<<"Conical Fillet/Chamfer certification, edits, deletion, Keep ON/OFF, persistence and refusals passed.\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
