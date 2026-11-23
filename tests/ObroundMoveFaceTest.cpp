#include "ObroundExtrusionFixture.h"
#include "BRepCertification.h"
#include "BRepReconstruction.h"
#include "MoveFaceGeometry.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepTools.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Lin.hxx>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace C=BRepCertification;
namespace F=ObroundFixture;
namespace {
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
void near(double actual,double expected,const char* message) {
    check(std::isfinite(actual)&&std::abs(actual-expected)<=1.e-8*std::max(1.,std::abs(expected)),message);
}
std::string brep(const TopoDS_Shape& shape) {std::ostringstream out;BRepTools::Write(shape,out);return out.str();}
TopoDS_Face cap(const TopoDS_Shape& shape,double height) {
    TopoDS_Face result;double largest=0;
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());const BRepReconstruction::SupportDescriptor support(face);
        if(support.surface.GetType()==GeomAbs_Plane&&support.surface.Plane().Axis().Direction().IsParallel(F::axis(),1.e-8)
           &&support.surface.Plane().Distance(F::world(0,0,height))<1.e-7&&C::area(face)>largest) {
            result=face;largest=C::area(face);
        }
    }
    check(!result.IsNull(),"correct axial cap not found");return result;
}
bool inside(double x,double y,double z,double height) {
    const double dx=std::max(std::abs(x)-30.,0.);
    return z>0&&z<height&&dx*dx+y*y<400&&x*x+y*y>100;
}
void topology(const TopoDS_Face& face) {
    BRepReconstruction::TopologyIndex index;QString error;
    check(BRepReconstruction::TopologyIndex::build(face,index,error),"cap topology indexing failed");
    const auto* record=index.face(face);check(record&&record->wires.size()==2,"outer/inner wire topology lost");
    int outer=0,inner=0;
    for(const auto& wire:record->wires) {
        if(wire.outer) {
            ++outer;int lines=0;double arcSweep=0;
            for(const auto& edge:wire.edges) {
                const BRepAdaptor_Curve curve(edge);
                if(curve.GetType()==GeomAbs_Line) {++lines;near(curve.Value(curve.FirstParameter()).Distance(curve.Value(curve.LastParameter())),60,"straight-side length changed");}
                else {check(curve.GetType()==GeomAbs_Circle,"outer boundary approximated");near(curve.Circle().Radius(),20,"end radius changed");arcSweep+=curve.LastParameter()-curve.FirstParameter();}
            }
            check(lines==2,"straight sides missing");near(arcSweep,2*std::numbers::pi,"semicircular ends incomplete");
        } else {
            ++inner;double sweep=0;
            for(const auto& edge:wire.edges) {
                const BRepAdaptor_Curve curve(edge);check(curve.GetType()==GeomAbs_Circle,"hole boundary approximated");
                near(curve.Circle().Radius(),10,"hole boundary radius changed");
                check(curve.Circle().Location().Distance(F::world(0,0,gp_Vec(gp_Pnt(),curve.Circle().Location()).Dot(gp_Vec(F::axis()))))<1.e-6,"hole center moved off axis");
                sweep+=curve.LastParameter()-curve.FirstParameter();
            }
            near(sweep,2*std::numbers::pi,"hole boundary not complete");
        }
    }
    check(outer==1&&inner==1,"wire classification changed");near(C::area(face),F::area(),"cross-section material area wrong");
}
void verify(const TopoDS_Shape& shape,double height,const TopoDS_Shape& oracle) {
    double volume=0,offending=0;
    const bool valid=C::singleSolid(shape,volume),tolerance=C::withinTolerance(shape,1.e-6,&offending),manifold=C::manifold(shape);
    if(!valid||!tolerance||!manifold) {
        std::ostringstream details;details<<"height="<<height<<" singleSolid="<<valid<<" withinTolerance="<<tolerance
            <<" offending="<<offending<<" manifold="<<manifold;
        throw std::runtime_error("solid/manifold/tolerance certificate failed: "+details.str());
    }
    near(volume,height*F::area(),"analytic volume incorrect");check(C::equivalent(shape,oracle),"equal volume hides changed material");
    TopExp_Explorer solid(shape,TopAbs_SOLID);BRepClass3d_SolidClassifier orientation(TopoDS::Solid(solid.Current()));orientation.PerformInfinitePoint(1.e-7);
    check(orientation.State()==TopAbs_OUT,"shell orientation changed");
    topology(cap(shape,0));topology(cap(shape,height));
    auto bottom=F::profile();bottom.Reverse();check(C::sameTrimmedRegion(cap(shape,0),bottom),"original bottom cap changed");
    check(C::sameTrimmedRegion(cap(shape,height),F::profile(height)),"translated cap trims changed");
    BRepReconstruction::TopologyIndex index;QString error;check(BRepReconstruction::TopologyIndex::build(shape,index,error),"solid topology indexing failed");
    bool upper=false,lower=false,left=false,right=false,hole=false;
    for(const auto& record:index.faces()) {
        const auto& s=record.support.surface;
        if(s.GetType()==GeomAbs_Plane) {
            const auto local=BRepBuilderAPI_Transform(record.support.face,F::frame().Inverted(),true).Shape();
            const auto plane=BRepAdaptor_Surface(TopoDS::Face(local)).Plane();
            if(plane.Axis().Direction().IsParallel(gp_Dir(0,0,1),1.e-8)) continue;
            check(plane.Axis().Direction().IsParallel(gp_Dir(0,1,0),1.e-8),"straight wall support changed");
            if(plane.Distance(gp_Pnt(0,20,0))<1.e-6) upper=true;
            else if(plane.Distance(gp_Pnt(0,-20,0))<1.e-6) lower=true;
            else check(false,"unexpected planar side support");
        } else {
            check(s.GetType()==GeomAbs_Cylinder,"rounded wall replaced by noncylindrical geometry");
            const auto cylinder=s.Cylinder();check(cylinder.Axis().Direction().IsParallel(F::axis(),1.e-8),"cylindrical wall axis changed");
            const gp_Lin axis(cylinder.Axis());
            if(std::abs(cylinder.Radius()-10)<1.e-6) {check(axis.Distance(F::world(0,0,0))<1.e-6,"hole axis shifted");hole=true;}
            else {near(cylinder.Radius(),20,"outer wall radius changed");if(axis.Distance(F::world(-30,0,0))<1.e-6) left=true;
                else if(axis.Distance(F::world(30,0,0))<1.e-6) right=true;else check(false,"outer end axis shifted");}
        }
    }
    check(upper&&lower&&left&&right&&hole,"one or more analytic supports missing");
    // Independent analytic occupancy: distance to the [-30,30] center segment,
    // minus the central R10 disk. Probe all axial regions, including the hole.
    for(double z:{-1.,.3,height*.5,height-.3,height+1})
        for(const auto& xy:{std::pair{0.,0.},std::pair{9.,0.},std::pair{11.,0.},std::pair{0.,19.},std::pair{0.,21.},
                           std::pair{49.,0.},std::pair{-49.,0.},std::pair{51.,0.},std::pair{49.,19.},std::pair{30.,19.}})
            check(C::occupancy(shape,F::world(xy.first,xy.second,z),1.e-7)==(inside(xy.first,xy.second,z,height)?TopAbs_IN:TopAbs_OUT),"independent material occupancy mismatch");
}
}
int main() {
    try {
        check(!F::axis().IsParallel(gp_Dir(0,0,1),1.e-8),"Workplane axis is still global Z");
        near(F::axis().Angle(gp_Dir(0,0,1)),std::numbers::pi/6,"Workplane tilt is not 30 degrees");
        topology(F::profile());QString error;
        const auto prism=F::prism(),assembled=F::assembled(),boolean=F::booleanSolid();
        check(C::equivalent(prism,assembled)&&C::equivalent(prism,boolean),"construction routes are not equivalent");
        // Some OCCT versions produce coarse Boolean topology here. Refuse such
        // input unchanged; if a version improves it, require the same success
        // as the other eligible sources instead of baking in a version defect.
        std::vector<TopoDS_Shape> sources{prism,assembled};
        double offending=0,booleanVolume=0;
        check(C::singleSolid(boolean,booleanVolume)&&C::manifold(boolean),"independent Boolean source is not valid closed material");
        if(C::withinTolerance(boolean,1.e-6,&offending)) sources.push_back(boolean);
        else {
            const auto booleanBefore=brep(boolean);BRepReconstruction::Result coarse;
            check(!BRepReconstruction::movePlanarLocal(boolean,cap(boolean,20),15,coarse,error)
                &&coarse.solid.IsNull()&&error.contains("Source tolerance"),"coarse Boolean source bypassed tolerance policy");
            check(brep(boolean)==booleanBefore,"coarse Boolean refusal mutated source");
            std::cout<<"Equivalent Boolean source correctly refused: "<<error.toStdString()<<std::endl;
        }
        int route=0;
        for(const auto& source:sources) {
            std::cout<<"Source route "<<(route==0?"profile prism":route==1?"independent planes/cylinders shell":"Boolean")<<std::endl;++route;
            const auto original=brep(source);verify(source,20,boolean);const auto top=cap(source,20);
            BRepReconstruction::Neighborhood neighborhood;check(BRepReconstruction::neighborhood(source,top,neighborhood,error),"source neighborhood failed");
            for(const auto& side:neighborhood.neighbors) {
                const BRepReconstruction::SupportDescriptor support(side);
                check(support.surface.GetType()==GeomAbs_Plane||support.surface.GetType()==GeomAbs_Cylinder,"fixture side is not analytic");
            }
            for(double distance:{15.,-5.}) {
                BRepReconstruction::Result result;
                if(!BRepReconstruction::movePlanarLocal(source,top,distance,result,error))
                    throw std::runtime_error("movePlanarLocal stage: "+error.toStdString());
                std::cout<<"movePlanarLocal accepted displacement "<<distance<<"; checking result independently"<<std::endl;
                verify(result.solid,20+distance,F::booleanSolid(20+distance));
                const auto routed=MoveFaceGeometry::move(source,top,distance,error);
                if(routed.IsNull()) throw std::runtime_error("MoveFaceGeometry routing: "+error.toStdString());
                check(C::equivalent(result.solid,routed),"existing Move Face route differs from local reconstruction");
                const auto region=BRepPrimAPI_MakePrism(F::profile(20),gp_Vec(F::axis())*distance,true,true).Shape();
                check(C::preservedOutside(source,result.solid,region),"protected material changed");
                C::MaterialDifference change;check(C::difference(source,result.solid,change,error),"material difference failed");
                check(C::equivalent(distance>0?change.added:change.removed,region),"wrong axial material change");
                near(C::volume(distance>0?change.removed:change.added),0,"unexpected reverse material change");
                BRepReconstruction::TopologyIndex resultIndex;check(BRepReconstruction::TopologyIndex::build(result.solid,resultIndex,error),"result index failed");
                check(C::preservedFace(cap(source,0),resultIndex,1.e-8),"unaffected bottom region lost");
            }
            for(double distance:{-20.,-21.}) {
                BRepReconstruction::Result refused;
                check(!BRepReconstruction::movePlanarLocal(source,top,distance,refused,error)&&refused.solid.IsNull()&&!error.isEmpty(),"collapsed/inverted height accepted");
            }
            check(brep(source)==original,"source geometry changed on success or failure");
        }
        const auto skew=BRepPrimAPI_MakePrism(F::profile(),gp_Vec(5,0,20).Transformed(F::frame()),true,true).Shape();
        const auto before=brep(skew);BRepReconstruction::Result refused;
        check(!BRepReconstruction::movePlanarLocal(skew,cap(skew,20),15,refused,error)&&refused.solid.IsNull(),"unsupported non-axial extrusion accepted");
        check(error.contains("changes cross section"),"non-axial refusal occurred at an unexpected stage");
        check(brep(skew)==before,"unsupported source changed");
        std::cout<<"Obround rotated prism/assembled shell, axial growth/shortening, analytic material and coarse Boolean refusal passed\n";
        return 0;
    } catch(const Standard_Failure& failure) {std::cerr<<failure.GetMessageString()<<'\n';return 1;}
      catch(const std::exception& failure) {std::cerr<<failure.what()<<'\n';return 1;}
}
