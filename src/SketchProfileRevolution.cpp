#include "SketchProfileRevolution.h"
#include "BooleanGeometry.h"
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBndLib.hxx>
#include <BOPAlgo_ArgumentAnalyzer.hxx>
#include <Bnd_Box.hxx>
#include <Standard_Failure.hxx>
#include <limits>
#include <numbers>

namespace SketchProfileRevolution {
namespace {
Result fail(Code code,const QString& message,std::optional<size_t> region={}) {
    Result result;result.diagnostics.push_back({code,message,region});return result;
}
bool resolvable(double coordinate) {
    const auto a=std::abs(coordinate);
    return std::isfinite(a) && std::nextafter(a,std::numeric_limits<double>::infinity())-a<=Precision::Confusion()/16;
}
}
Result revolve(const SketchProfileBuilder::Result& faces,const gp_Ax1& axis,double degrees) {
    if(!faces.success || faces.faces.empty() || !std::isfinite(degrees)
       || std::abs(degrees)<1 || std::abs(degrees)>360
       || !std::isfinite(faces.effectiveVertexTolerance) || faces.effectiveVertexTolerance<Precision::Confusion())
        return fail(Code::InvalidInput,"Choose certified closed Profile faces and a signed angle from 1 to 360 degrees.");
    try {
        for(double c:{axis.Location().X(),axis.Location().Y(),axis.Location().Z()})
            if(!resolvable(c))return fail(Code::NumericalFailure,"Axis coordinates cannot resolve the modeling tolerance.");
        const double angle=std::abs(degrees)*std::numbers::pi/180;
        const gp_Ax1 sweepAxis(axis.Location(),degrees<0?axis.Direction().Reversed():axis.Direction());
        BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);
        TopoDS_Shape first;long double expected=0;int side=0;
        std::optional<gp_Pln> sourcePlane;
        for(size_t i=0;i<faces.faces.size();++i) {
            const auto& face=faces.faces[i].shape;
            if(face.IsNull() || !BRepCheck_Analyzer(face).IsValid())
                return fail(Code::InvalidFace,"A source Profile face is not a valid B-Rep.",i);
            BRepAdaptor_Surface surface(face);
            if(surface.GetType()!=GeomAbs_Plane)return fail(Code::InvalidFace,"Revolve requires planar Profile faces.",i);
            const auto plane=surface.Plane();const auto normal=plane.Axis().Direction();
            if(std::abs(normal.Dot(axis.Direction()))>1.e-12 || plane.Distance(axis.Location())>Precision::Confusion()
               || (sourcePlane && (!normal.IsParallel(sourcePlane->Axis().Direction(),1.e-12)
                                  || sourcePlane->Distance(plane.Location())>Precision::Confusion())))
                return fail(Code::UnsupportedAxis,"The rotation axis must lie in the common Profile plane.",i);
            if(!sourcePlane)sourcePlane=plane;
            // Exact analytic bounds in meridian coordinates, not sampled vertices:
            // a curved edge can cross the axis while both endpoints stay on one side.
            const gp_Dir radial(axis.Direction().Crossed(sourcePlane->Axis().Direction()));
            gp_Trsf toMeridian;toMeridian.SetTransformation(gp_Ax3(axis.Location(),sourcePlane->Axis().Direction(),radial));
            const auto local=BRepBuilderAPI_Transform(face,toMeridian,true).Shape();
            Bnd_Box bounds;BRepBndLib::AddOptimal(local,bounds,false,false);
            double lo[3],hi[3];bounds.Get(lo[0],lo[1],lo[2],hi[0],hi[1],hi[2]);
            const double tolerance=faces.effectiveVertexTolerance;
            if(lo[0]<-tolerance && hi[0]>tolerance)
                return fail(Code::SelfIntersection,"A Profile crosses the rotation axis; self-overlapping revolutions are refused.",i);
            const int regionSide=hi[0]>tolerance?1:(lo[0]<-tolerance?-1:0);
            if(!regionSide || (side && side!=regionSide))
                return fail(Code::UnsupportedAxis,"All Profile regions must be on the same side of the rotation axis.",i);
            side=regionSide;
            // A tangency inside an edge (e.g. a circle tangent to the axis)
            // creates a pinched solid. OCCT can report a horn torus as valid.
            // Endpoints on the axis and whole axis edges remain supported:
            // these produce ordinary cone/sphere poles and cylinder axes.
            for(TopExp_Explorer edges(local,TopAbs_EDGE);edges.More();edges.Next()) {
                Bnd_Box edgeBounds;BRepBndLib::AddOptimal(edges.Current(),edgeBounds,false,false);
                double eLo[3],eHi[3];edgeBounds.Get(eLo[0],eLo[1],eLo[2],eHi[0],eHi[1],eHi[2]);
                const double near=side>0?eLo[0]:-eHi[0],far=side>0?eHi[0]:-eLo[0];
                if(near>tolerance || far<=tolerance)continue;
                BRepAdaptor_Curve curve(TopoDS::Edge(edges.Current()));
                if(curve.IsClosed() || (std::abs(curve.Value(curve.FirstParameter()).X())>tolerance
                                       && std::abs(curve.Value(curve.LastParameter()).X())>tolerance))
                    return fail(Code::SelfIntersection,"A Profile is tangent to the axis inside a curve; pinched revolutions are refused.",i);
            }
            GProp_GProps area;BRepGProp::SurfaceProperties(face,area);
            const double radius=std::abs(gp_Vec(axis.Location(),area.CentreOfMass()).Dot(gp_Vec(radial)));
            if(!std::isfinite(area.Mass()) || area.Mass()<=0 || !std::isfinite(radius) || radius<=tolerance)
                return fail(Code::InvalidFace,"Profile area or radial moment is degenerate.",i);
            expected+=static_cast<long double>(area.Mass())*radius*angle;
            BRepPrimAPI_MakeRevol sweep(face,sweepAxis,angle,true);
            if(!sweep.IsDone())return fail(Code::InvalidSolid,"OCCT could not construct the rotational sweep.",i);
            const auto shape=sweep.Shape();double volume=0;size_t count=0;
            if(shape.ShapeType()!=TopAbs_SOLID || !BooleanGeometry::certifiedSolid(shape,volume,count) || count!=1)
                return fail(Code::InvalidSolid,"Revolution is not one closed, outward-oriented, positive-volume solid.",i);
            Bnd_Box world;BRepBndLib::AddOptimal(shape,world,false,false);
            world.Get(lo[0],lo[1],lo[2],hi[0],hi[1],hi[2]);
            for(double c:{lo[0],lo[1],lo[2],hi[0],hi[1],hi[2]})
                if(!resolvable(c))return fail(Code::NumericalFailure,"Revolved world coordinates cannot resolve the modeling tolerance.",i);
            if(i==0)first=shape;builder.Add(compound,shape);
        }
        Result result;result.shape=faces.faces.size()==1?first:TopoDS_Shape(compound);
        // Check the aggregate too: individually valid sweeps must not intersect.
        BOPAlgo_ArgumentAnalyzer interference;interference.SetShape1(result.shape);
        interference.ArgumentTypeMode()=false;interference.SelfInterMode()=true;
        interference.StopOnFirstFaulty()=true;interference.Perform();
        if(interference.HasErrors() || interference.HasFaulty())
            return fail(Code::SelfIntersection,"Revolution has self-intersecting or interfering solid boundaries.");
        if(!BooleanGeometry::certifiedSolid(result.shape,result.volume,result.solidCount)
           || result.solidCount!=faces.faces.size() || !std::isfinite(expected)
           || std::abs(result.volume-expected)>std::max(1.e-7L,std::abs(expected)*1.e-9L))
            return fail(Code::InvalidSolid,"Revolved volume or solid count does not match the Profile radial moment and angle.");
        result.success=true;return result;
    } catch(const Standard_Failure& e) {return fail(Code::NumericalFailure,QString::fromUtf8(e.GetMessageString()));}
      catch(const std::exception& e) {return fail(Code::NumericalFailure,QString::fromUtf8(e.what()));}
}
}
