#include "SketchProfileExtrusion.h"
#include "BooleanGeometry.h"
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <limits>
namespace SketchProfileExtrusion {
namespace {
Result fail(Code code,const QString& message,std::optional<size_t> region={}) {Result r;r.diagnostics.push_back({code,message,region});return r;}
}
Result extrude(const SketchProfileBuilder::Result& faces,const gp_Dir& normal,double distance) {
    if(!faces.success || faces.faces.empty() || !std::isfinite(distance) || !std::isfinite(faces.effectiveVertexTolerance) || faces.effectiveVertexTolerance<Precision::Confusion()
        || std::abs(distance)<=2*faces.effectiveVertexTolerance
        || std::abs(distance)>1.e9)return fail(Code::InvalidInput,"Choose certified Profile faces and a finite nonzero extrusion distance (greater than twice the certified vertex tolerance and at most 1e9 mm).");
    try {
        BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);TopoDS_Shape first;long double expected=0;
        for(size_t i=0;i<faces.faces.size();++i) {
            const auto& f=faces.faces[i];
            if(f.shape.IsNull() || !BRepCheck_Analyzer(f.shape).IsValid())return fail(Code::InvalidFace,"A source face is not a valid B-Rep.",i);
            BRepAdaptor_Surface surface(f.shape);
            if(surface.GetType()!=GeomAbs_Plane || !surface.Plane().Axis().Direction().IsParallel(normal,1.e-12))return fail(Code::InvalidFace,"Extrusion direction must be normal to every source Workplane face.",i);
            GProp_GProps area;BRepGProp::SurfaceProperties(f.shape,area,1.e-12);
            if(!std::isfinite(area.Mass()) || area.Mass()<=0)return fail(Code::InvalidFace,"A source face has no positive area.",i);
            BRepPrimAPI_MakePrism prism(f.shape,gp_Vec(normal)*distance,true,true);
            if(!prism.IsDone())return fail(Code::InvalidSolid,"OCCT could not construct the prism.",i);
            const auto shape=prism.Shape();double volume=0;size_t solids=0;
            if(shape.ShapeType()!=TopAbs_SOLID || !BooleanGeometry::certifiedSolid(shape,volume,solids) || solids!=1)return fail(Code::InvalidSolid,"Prism is not an oriented, closed, positive-volume solid.",i);
            if(i==0)first=shape;builder.Add(compound,shape);expected+=area.Mass()*std::abs(distance);
            // Refuse placements which cannot resolve the approved geometric tolerance.
            Bnd_Box bounds;BRepBndLib::AddOptimal(shape,bounds,false,false);double a,b,c,d,e,fz;bounds.Get(a,b,c,d,e,fz);
            for(double x:{a,b,c,d,e,fz})if(!std::isfinite(x) || std::nextafter(std::abs(x),std::numeric_limits<double>::infinity())-std::abs(x)>Precision::Confusion()/16)
                return fail(Code::NumericalFailure,"Extruded world coordinates cannot resolve the modeling tolerance.",i);
        }
        Result result;result.shape=faces.faces.size()==1?first:TopoDS_Shape(compound);
        if(!BooleanGeometry::certifiedSolid(result.shape,result.volume,result.solidCount) || result.solidCount!=faces.faces.size()
            || std::abs(result.volume-expected)>std::max(1.e-7L,std::abs(expected)*1.e-9L))return fail(Code::InvalidSolid,QString("Prism volume or solid count does not match its source regions (volume %1, expected %2; solids %3, expected %4).").arg(result.volume,0,'g',17).arg(double(expected),0,'g',17).arg(result.solidCount).arg(faces.faces.size()));
        result.success=true;return result;
    } catch(const Standard_Failure& e) {return fail(Code::NumericalFailure,QString::fromUtf8(e.GetMessageString()));}
      catch(const std::exception& e) {return fail(Code::NumericalFailure,QString::fromUtf8(e.what()));}
}
Result cut(const SketchProfileBuilder::Result& faces,const gp_Ax3& frame,
           const TopoDS_Shape& target,const gp_Trsf& placement,
           double depth,bool reversed,bool throughAll) {
    try {
        double originalVolume=0;size_t count=0;
        if(!BooleanGeometry::certifiedSolid(target,originalVolume,count) || placement.IsNegative()
           || std::abs(placement.ScaleFactor()-1)>1.e-12)
            return fail(Code::InvalidInput,"Cut requires a valid solid target with rigid positive placement.");
        const auto world=BRepBuilderAPI_Transform(target,placement,true).Shape();
        if(!BooleanGeometry::certifiedSolid(world,originalVolume,count))return fail(Code::InvalidSolid,"Transformed target is invalid.");
        const gp_Vec direction=gp_Vec(frame.Direction())*(reversed?-1:1);
        if(throughAll) {
            Bnd_Box bounds;BRepBndLib::AddOptimal(world,bounds,false,false);
            double lo[3],hi[3];bounds.Get(lo[0],lo[1],lo[2],hi[0],hi[1],hi[2]);
            double reach=0;
            for(int i=0;i<8;++i)reach=std::max(reach,gp_Vec(frame.Location(),gp_Pnt(
                i&1?hi[0]:lo[0],i&2?hi[1]:lo[1],i&4?hi[2]:lo[2])).Dot(direction));
            if(reach<=Precision::Confusion())return fail(Code::Unchanged,"Target lies outside the chosen cutting direction.");
            depth=reach+std::max(16*Precision::Confusion(),std::sqrt(bounds.SquareExtent())*1.e-7);
        }
        if(!std::isfinite(depth) || depth<=0)return fail(Code::InvalidInput,"Enter a positive cutting depth in millimeters.");
        auto tool=extrude(faces,frame.Direction(),reversed?-depth:depth);
        if(!tool.success)return tool;
        return removeMaterial(tool,target,placement);
    } catch(const Standard_Failure& e) {return fail(Code::NumericalFailure,QString::fromUtf8(e.GetMessageString()));}
      catch(const std::exception& e) {return fail(Code::NumericalFailure,QString::fromUtf8(e.what()));}
}
Result removeMaterial(const Result& tool,const TopoDS_Shape& target,const gp_Trsf& placement) {
    try {
        double toolVolume=0,originalVolume=0;size_t toolCount=0,count=0;
        if(!tool.success || !BooleanGeometry::certifiedSolid(tool.shape,toolVolume,toolCount)
           || !BooleanGeometry::certifiedSolid(target,originalVolume,count) || placement.IsNegative()
           || std::abs(placement.ScaleFactor()-1)>1.e-12)
            return fail(Code::InvalidInput,"Remove Material requires a valid swept tool and solid target with rigid positive placement.");
        const auto world=BRepBuilderAPI_Transform(target,placement,true).Shape();
        if(!BooleanGeometry::certifiedSolid(world,originalVolume,count))return fail(Code::InvalidSolid,"Transformed target is invalid.");
        auto shape=BooleanGeometry::combine<BRepAlgoAPI_Cut>(world,tool.shape);
        int solids=0;shape=BooleanGeometry::solidResult(shape,solids);
        if(solids==0)return fail(Code::InvalidSolid,"Cut would remove the entire target.");
        shape=BooleanGeometry::normalizeBooleanResult(shape);
        Result result;result.shape=shape;
        if(!BooleanGeometry::certifiedSolid(shape,result.volume,result.solidCount))return fail(Code::InvalidSolid,"Cut produced invalid solid geometry.");
        if(result.volume>=originalVolume-std::max(1.e-9,originalVolume*1.e-12))return fail(Code::Unchanged,"Swept profiles remove no measurable material from the target.");
        result.success=true;return result;
    } catch(const Standard_Failure& e) {return fail(Code::NumericalFailure,QString::fromUtf8(e.GetMessageString()));}
      catch(const std::exception& e) {return fail(Code::NumericalFailure,QString::fromUtf8(e.what()));}
}
Result addMaterial(const Result& tool,const TopoDS_Shape& target,const gp_Trsf& placement) {
    try {
        double toolVolume=0,targetVolume=0;size_t toolCount=0,targetCount=0;
        if(!tool.success || !BooleanGeometry::certifiedSolid(tool.shape,toolVolume,toolCount) || !BooleanGeometry::certifiedSolid(target,targetVolume,targetCount) || targetCount!=1)
            return fail(Code::InvalidInput,"Add Material requires a valid swept tool and one connected target solid.");
        if(placement.IsNegative() || std::abs(placement.ScaleFactor()-1)>1.e-12)return fail(Code::InvalidInput,"Target placement must be a rigid positive instance transform.");
        const auto world=BRepBuilderAPI_Transform(target,placement,true).Shape();
        if(!BooleanGeometry::certifiedSolid(world,targetVolume,targetCount))return fail(Code::InvalidSolid,"Transformed target is invalid.");
        auto fused=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(world,tool.shape);
        int count=0;fused=BooleanGeometry::solidResult(fused,count);
        if(count!=1)return fail(Code::Disconnected,"The swept tool must connect to the target as one solid; disconnected additions are refused.");
        fused=BooleanGeometry::normalizeBooleanResult(fused);
        Result result;result.shape=fused;
        if(!BooleanGeometry::certifiedSolid(fused,result.volume,result.solidCount) || result.solidCount!=1)return fail(Code::InvalidSolid,"Fused target is not one valid closed solid.");
        if(result.volume<=targetVolume+std::max(1.e-9,targetVolume*1.e-12))return fail(Code::Unchanged,"The swept tool adds no measurable volume to the target.");
        result.success=true;return result;
    } catch(const Standard_Failure& e) {return fail(Code::NumericalFailure,QString::fromUtf8(e.GetMessageString()));}
      catch(const std::exception& e) {return fail(Code::NumericalFailure,QString::fromUtf8(e.what()));}
}
}
