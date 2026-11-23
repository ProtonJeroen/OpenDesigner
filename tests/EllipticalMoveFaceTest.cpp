#include "EllipticalExtrusionFixture.h"
#include "BRepCertification.h"
#include "BRepReconstruction.h"
#include "MoveFaceGeometry.h"
#include "BooleanGeometry.h"
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_Lin.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <TopExp_Explorer.hxx>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace C=BRepCertification;
namespace {
void check(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
void near(double actual,double expected,const char* message) {
    check(std::abs(actual-expected)<=1.e-8*std::max(1.,std::abs(expected)),message);
}
std::string brep(const TopoDS_Shape& shape) {std::ostringstream out;BRepTools::Write(shape,out);return out.str();}
TopoDS_Face cap(const TopoDS_Shape& shape,double z) {
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());BRepAdaptor_Surface surface(face);
        if(surface.GetType()==GeomAbs_Plane&&surface.Plane().Distance(gp_Pnt(0,0,z))<1.e-7) return face;
    }
    throw std::runtime_error("Synthetic planar cap missing");
}
void verify(const TopoDS_Shape& shape,double height,bool hole=false) {
    double volume=0;check(C::singleSolid(shape,volume)&&C::withinTolerance(shape,1.e-6)&&C::manifold(shape),"result shape certificate failed");
    near(volume,(hole?1400:1500)*std::numbers::pi*height,"analytic elliptical volume mismatch");
    const auto oracle=hole?BooleanGeometry::combine<BRepAlgoAPI_Cut>(EllipticalFixture::prism(height),BRepPrimAPI_MakeCylinder(10,height).Shape()):EllipticalFixture::prism(height);
    check(C::equivalent(shape,oracle),"result differs from independent exact material oracle");
    auto bottom=EllipticalFixture::profile(0,hole);bottom.Reverse();
    check(C::sameTrimmedRegion(cap(shape,0),bottom),"opposite section changed");
    check(C::sameTrimmedRegion(cap(shape,height),EllipticalFixture::profile(height,hole)),"translated ellipse changed");
    int walls=0,holes=0;
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());const BRepReconstruction::SupportDescriptor support(face);
        if(support.surface.GetType()==GeomAbs_Plane) continue;
        if(support.surface.GetType()==GeomAbs_Cylinder&&hole) {
            const auto c=support.surface.Cylinder();near(c.Radius(),10,"hole radius changed");
            check(c.Axis().Direction().IsParallel(gp_Dir(0,0,1),1.e-8)&&gp_Lin(c.Axis()).Distance(gp_Pnt(0,0,0))<1.e-6,"hole axis changed");
            ++holes;continue;
        }
        C::EllipticExtrusion extrusion;check(C::ellipticExtrusion(support,extrusion),"side support approximated or replaced");
        near(extrusion.section.MajorRadius(),50,"major radius changed");near(extrusion.section.MinorRadius(),30,"minor radius changed");++walls;
    }
    check(walls==1&&holes==(hole?1:0),"elliptical/cylindrical side split or faceted");
    for(double z:{.2,height*.5,height-.2}) {
        check(C::occupancy(shape,gp_Pnt(0,0,z),1.e-7)==(hole?TopAbs_OUT:TopAbs_IN),"central hole blocked or center material missing");
        check(C::occupancy(shape,gp_Pnt(20,0,z),1.e-7)==TopAbs_IN,"annular material missing");
        check(C::occupancy(shape,gp_Pnt(49,0,z),1.e-7)==TopAbs_IN,"outer elliptical material missing");
        check(C::occupancy(shape,gp_Pnt(51,0,z),1.e-7)==TopAbs_OUT,"extra material outside major radius");
        check(C::occupancy(shape,gp_Pnt(0,31,z),1.e-7)==TopAbs_OUT,"extra material outside minor radius");
    }
    check(C::occupancy(shape,gp_Pnt(20,0,-1),1.e-7)==TopAbs_OUT
       &&C::occupancy(shape,gp_Pnt(20,0,height+1),1.e-7)==TopAbs_OUT,"extra axial material");
}
}
int main() {
    try {
        QString error;
        for(bool hole:{false,true}) for(const auto& source:{EllipticalFixture::prism(20,hole),
            hole?BooleanGeometry::combine<BRepAlgoAPI_Cut>(EllipticalFixture::assembled(),BRepPrimAPI_MakeCylinder(10,20).Shape()):EllipticalFixture::assembled()}) {
            const auto original=brep(source);verify(source,20,hole);const auto top=cap(source,20);
            for(double distance:{15.,-5.}) {
                const auto moved=MoveFaceGeometry::move(source,top,distance,error,MoveFaceGeometry::ReconstructionPolicy::AxialEllipticalCap);
                if(moved.IsNull()) throw std::runtime_error(error.toStdString());
                verify(moved,20+distance,hole);
                BRepReconstruction::Result mapped;
                check(BRepReconstruction::moveEllipticalCap(source,top,gp_Vec(0,0,distance),mapped,error),"shared reconstruction failed");
                check(mapped.history.size()==(hole?4:3)&&!mapped.target.IsNull(),"source correspondence lost");
                for(const auto& mapping:mapped.history) check(mapping.modified.size()==1,"retained face mapped as removed/ambiguous");
                std::stringstream saved;BRepTools::Write(moved,saved);TopoDS_Shape restored;BRep_Builder builder;BRepTools::Read(restored,saved,builder);verify(restored,20+distance,hole);
            }
            for(const auto& delta:{gp_Vec(1,0,15),gp_Vec(1,0,0),gp_Vec(0,0,-20),gp_Vec(0,0,-21)}) {
                BRepReconstruction::Result refused;check(!BRepReconstruction::moveEllipticalCap(source,top,delta,refused,error)
                    &&refused.solid.IsNull()&&!error.isEmpty(),"non-axial/collapsed move accepted");
            }
            check(MoveFaceGeometry::move(source,top,-20,error,MoveFaceGeometry::ReconstructionPolicy::AxialEllipticalCap).IsNull(),"collapsed Move Face accepted");
            check(MoveFaceGeometry::move(source,top,15,error).IsNull(),"legacy/internal caller gained elliptical capability");
            gp_Trsf placement;placement.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),.4);placement.SetTranslationPart(gp_Vec(17,-9,23));
            const TopLoc_Location location(placement);
            const auto located=source.Moved(location);const auto locatedCap=TopoDS::Face(top.Moved(location));
            const auto locatedMove=MoveFaceGeometry::move(located,locatedCap,15,error,MoveFaceGeometry::ReconstructionPolicy::AxialEllipticalCap);
            if(locatedMove.IsNull()) throw std::runtime_error(error.toStdString());
            verify(BRepBuilderAPI_Transform(locatedMove,placement.Inverted(),true).Shape(),35,hole);
            check(brep(source)==original,"source changed on success or failure");
        }
        const auto oblique=BRepPrimAPI_MakePrism(EllipticalFixture::profile(),gp_Vec(5,0,20)).Shape();
        const auto before=brep(oblique);
        check(MoveFaceGeometry::move(oblique,cap(oblique,20),15,error,MoveFaceGeometry::ReconstructionPolicy::AxialEllipticalCap).IsNull(),"non-axial extrusion reconstructed");
        check(brep(oblique)==before,"unsupported oblique extrusion mutated");
        std::cout<<"Exact elliptical cap Move Face geometry passed\n";return 0;
    } catch(const Standard_Failure& e) {std::cerr<<e.GetMessageString()<<'\n';return 1;}
      catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
