// Isolated bounded extrusion experiment, NOT a general surface-editing engine.
#include "SplineExtrusionFixture.h"
#include "BRepCertification.h"
#include "BRepMeasurement.h"
#include "BRepReconstruction.h"
#include <GeomAPI_IntSS.hxx>
#include <Geom_Plane.hxx>
#include <Geom_RectangularTrimmedSurface.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepLib.hxx>
#include <BRepTools.hxx>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <map>
namespace C=BRepCertification;
namespace R=BRepReconstruction;
using Clock=std::chrono::steady_clock;
std::map<std::string,double> times;
struct Timer {std::string key; Clock::time_point start=Clock::now(); ~Timer(){times[key]+=std::chrono::duration<double,std::milli>(Clock::now()-start).count();}};
void require(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
std::string snapshot(const TopoDS_Shape& s){std::ostringstream o;BRepTools::Write(s,o);return o.str();}
void near(double a,double b){require(std::abs(a-b)<1.e-8*std::max(1.,std::abs(b)),"independent polynomial volume/area");}
TopoDS_Shape experimental(const TopoDS_Shape& source,double delta){
    Timer total{"surface-construction"};
    require(delta>-20,"collapsed height");
    BRepBuilderAPI_Copy copy(source,true,false);const auto isolated=copy.Shape();
    R::TopologyIndex index;QString error;require(R::TopologyIndex::build(isolated,index,error),"source index");
    std::vector<TopoDS_Face> faces;
    gp_Trsf move;move.SetTranslation(gp_Vec(0,0,delta));
    auto top=SplineFixture::cap(isolated,20);
    const auto bottom=SplineFixture::cap(isolated,0);
    require(!top.IsNull()&&!bottom.IsNull(),"fixture caps");
    // Transform the actual trimmed cap; no section extraction or prism here.
    faces.push_back(TopoDS::Face(BRepBuilderAPI_Transform(top,move,true).Shape()));
    faces.push_back(bottom);
    for(const auto& record:index.faces()) {
        const auto& face=record.support.face;
        if(BRepAdaptor_Surface(face).GetType()==GeomAbs_Plane)continue;
        R::InvariantSplineSupport proof;
        require(R::SupportDescriptor(face).invariantSpline(proof,error,&index),error.toStdString().c_str());
        require(proof.axis.IsParallel(gp_Dir(0,0,1),1.e-12),"fixture axis");
        Handle(Geom_Surface) support;
        {Timer timer{"continuation"};
            // Exact unbounded carrier already supplies continuation in V.
            // No GeomLib extrapolation or modified spline coefficients.
            support=BRep_Tool::Surface(face);
            require(!Handle(Geom_SurfaceOfLinearExtrusion)::DownCast(support).IsNull(),"exact extrusion carrier");
        }
        const double firstZ=support->Value(proof.firstU,proof.firstV).Z();
        const double lastZ=support->Value(proof.firstU,proof.lastV).Z();
        double v0=proof.firstV,v1=proof.lastV;
        if(firstZ>lastZ)v0+=delta/proof.axis.Z();else v1+=delta/proof.axis.Z();
        require(v1>v0,"nonpositive parameter domain");
        {Timer timer{"intersections"};
            Handle(Geom_Surface) bounded=new Geom_RectangularTrimmedSurface(support,proof.firstU,proof.lastU,v0,v1);
            for(double v:{v0,v1}) {
                Handle(Geom_Surface) plane=new Geom_Plane(gp_Pln(support->Value(proof.firstU,v),proof.axis));
                GeomAPI_IntSS intersection(bounded,plane,1.e-7);
                require(intersection.IsDone()&&intersection.NbLines()==1,"unique support/plane intersection");
                // This structural case has V=constant exactly: basis poles lie
                // in the perpendicular section plane and axis is normal to it.
                // Do not install the numerically computed curve as an exact refit.
                const auto curve=intersection.Line(1);
                require(!curve.IsNull(),"intersection curve");
            }
        }
        {Timer timer{"edges-trimming"};
            // The exact constant-V intersection is represented as an iso trim.
            // MakeFace builds coherent 3D edges AND surface pcurves together.
            BRepBuilderAPI_MakeFace maker(support,proof.firstU,proof.lastU,v0,v1,1.e-7);
            require(maker.IsDone(),"bounded support retrim");
            auto rebuilt=maker.Face();rebuilt.Orientation(face.Orientation());faces.push_back(rebuilt);
        }
    }
    require(faces.size()==4,"complete two-wall fixture only");
    BRepBuilderAPI_Sewing sew(1.e-7);
    {Timer timer{"sewing-solid"};for(const auto& f:faces)sew.Add(f);sew.Perform();}
    const auto shell=sew.SewedShape();require(shell.ShapeType()==TopAbs_SHELL,"one sewn shell");
    auto result=BRepBuilderAPI_MakeSolid(TopoDS::Shell(shell)).Solid();
    require(BRepLib::OrientClosedSolid(result),"solid orientation");return result;
}
void certify(const TopoDS_Shape& source,const TopoDS_Shape& result,double delta){
    Timer timer{"common-certification"};QString error;double volume=0;
    require(C::singleSolid(source,volume)&&C::manifold(source)&&C::withinTolerance(source,1.e-6),"source certification");
    require(C::singleSolid(result,volume)&&C::manifold(result)&&C::withinTolerance(result,1.e-6),"result certification");
    near(volume,420*(20+delta));
    BRepMeasurement::Result area;require(BRepMeasurement::area(result,area,error),"surface measurement");
    near(C::area(SplineFixture::cap(result,20+delta)),420);
    require(C::equivalent(result,SplineFixture::solid(20+delta)),"independent full material oracle");
    const auto region=BRepPrimAPI_MakePrism(SplineFixture::cap(source,20),gp_Vec(0,0,delta),true,true).Shape();
    require(C::preservedOutside(source,result,region),"protected material");
    C::MaterialDifference diff;require(C::difference(source,result,diff,error),"directional differences");
    require(C::equivalent(delta>0?diff.added:diff.removed,region),"intended material delta");
    near(C::volume(delta>0?diff.removed:diff.added),0);
    gp_Trsf shift;shift.SetTranslation(gp_Vec(0,0,delta));
    const auto movedTop=TopoDS::Face(BRepBuilderAPI_Transform(SplineFixture::cap(source,20),shift,true).Shape());
    require(C::sameTrimmedRegion(movedTop,SplineFixture::cap(result,20+delta)),"translated top correspondence");
    for(double z:{1.,(20+delta)/2,19+delta}) {
        require(C::occupancy(result,gp_Pnt(30,20,z),1.e-7)==TopAbs_IN,"missing material");
        require(C::occupancy(result,gp_Pnt(30,30,z),1.e-7)==TopAbs_OUT,"extra material");
    }
    require(C::sameTrimmedRegion(SplineFixture::cap(source,0),SplineFixture::cap(result,0)),"bottom preserved");
    // Pairwise existing correspondence: whole-shape matcher deliberately refuses
    // multiple unrelated spline carrier families. No invented stable identities.
    R::TopologyIndex old,current;require(R::TopologyIndex::build(source,old,error)&&R::TopologyIndex::build(result,current,error),"correspondence indexes");int matched=0;
    for(const auto& record:old.faces()) {
        const auto& face=record.support.face;
        if(BRepAdaptor_Surface(face).GetType()==GeomAbs_Plane)continue;
        int matches=0;
        for(const auto& entry:current.faces()) {
            const auto& other=entry.support.face;
            if(BRepAdaptor_Surface(other).GetType()==GeomAbs_Plane)continue;
            C::Correspondence correspondence;
            if(C::correspondInvariantSpline(face,other,correspondence,error)&&!correspondence.faces[0].regions.overlaps.empty())++matches;
        }
        require(matches==1,"unique spline trimmed-region correspondence");++matched;
    }
    require(matched==2,"all spline walls matched");
}
int main(){try{
    for(double delta:{15.,-5.})for(int trial=0;trial<3;++trial){
        const auto source=SplineFixture::solid();const auto before=snapshot(source);
        for(int route:{0,1,2}) {
            times.clear();const auto start=Clock::now();TopoDS_Shape result;QString error;
            if(route==1)result=experimental(source,delta);
            else if(route==2) {
                Timer timer{"cap-prism-construction"};gp_Trsf shift;shift.SetTranslation(gp_Vec(0,0,delta));
                const auto moved=BRepBuilderAPI_Transform(SplineFixture::cap(source,20),shift,true).Shape();
                result=BRepPrimAPI_MakePrism(moved,gp_Vec(0,0,-20-delta),true,true).Shape();
            }
            else {Timer timer{"production-certified-route"};R::Result r;require(R::movePlanarLocal(source,SplineFixture::cap(source,20),delta,r,error,true),error.toStdString().c_str());result=r.solid;}
            certify(source,result,delta);
            require(snapshot(source)==before,"source mutated");
            std::cout<<(route==0?"production":route==1?"surface":"cap-prism")<<" delta="<<delta<<" trial="<<trial<<" total="<<std::chrono::duration<double,std::milli>(Clock::now()-start).count();
            for(const auto& [key,value]:times)std::cout<<" "<<key<<"="<<value;
            std::cout<<'\n';
        }
    }
    const auto source=SplineFixture::solid();const auto before=snapshot(source);
    for(const auto& unsupported:{source,BRepBuilderAPI_NurbsConvert(source,true).Shape()}) {
        const auto saved=snapshot(unsupported);bool refused=false;
        try{experimental(unsupported,-20);}catch(const std::exception&){refused=true;}
        require(refused&&snapshot(unsupported)==saved,"collapse refusal immutability");
    }
    const auto generic=BRepBuilderAPI_NurbsConvert(source,true).Shape();const auto saved=snapshot(generic);bool refused=false;
    try{experimental(generic,15);}catch(const std::exception&){refused=true;}
    require(refused&&snapshot(generic)==saved&&snapshot(source)==before,"unsupported representation refusal");
}catch(const Standard_Failure& e){std::cerr<<e.GetMessageString()<<'\n';return 1;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
