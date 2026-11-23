#include "BRepTiming.h"
#include "BRepReconstruction.h"
#include "BRepCertification.h"
#include "BooleanGeometry.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Lin.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace {
namespace C = BRepCertification;
using BRepReconstruction::TopologyIndex;
struct Extrusion {
    TopoDS_Face target, opposite, wall, hole;
    gp_Elips ellipse;
    gp_Dir outward;
    double height=0, sectionArea=0, holeRadius=0;
};
bool closeVolume(double actual,double expected) {
    return std::isfinite(actual)&&std::isfinite(expected)
        &&std::abs(actual-expected)<=1.e-8*std::max(1.,std::abs(expected));
}
gp_Dir normal(const TopoDS_Face& face) {
    auto n=BRepAdaptor_Surface(face).Plane().Axis().Direction();
    if(face.Orientation()==TopAbs_REVERSED) n.Reverse();return n;
}
gp_Elips capEllipse(const TopologyIndex::Face& face) {
    if(face.support.surface.GetType()!=GeomAbs_Plane||face.wires.empty()||face.wires.size()>2)
        throw Standard_Failure("Requires one complete elliptical cap boundary.");
    const auto outer=std::find_if(face.wires.begin(),face.wires.end(),[](const auto& wire){return wire.outer;});
    if(outer==face.wires.end()||outer->edges.size()!=1) throw Standard_Failure("Requires one full outer ellipse.");
    const BRepAdaptor_Curve curve(outer->edges.front());
    if(curve.GetType()!=GeomAbs_Ellipse||std::abs(curve.LastParameter()-curve.FirstParameter()-2*std::numbers::pi)>1.e-8)
        throw Standard_Failure("The cap boundary is not one exact full ellipse.");
    return curve.Ellipse();
}
std::optional<gp_Circ> capHole(const TopologyIndex::Face& face,const gp_Elips& ellipse) {
    for(const auto& wire:face.wires) if(!wire.outer) {
        if(wire.edges.size()!=1) throw Standard_Failure("Requires one full circular inner wire.");
        const BRepAdaptor_Curve curve(wire.edges.front());
        if(curve.GetType()!=GeomAbs_Circle||std::abs(curve.LastParameter()-curve.FirstParameter()-2*std::numbers::pi)>1.e-8)
            throw Standard_Failure("Only one exact circular through-hole is supported.");
        const auto circle=curve.Circle();
        if(circle.Location().Distance(ellipse.Location())>1.e-6
           ||!circle.Axis().Direction().IsParallel(ellipse.Axis().Direction(),1.e-8)
           ||circle.Radius()<=1.e-7||circle.Radius()>=ellipse.MinorRadius()-1.e-7)
            throw Standard_Failure("Only a coaxial circular hole strictly inside the ellipse is supported.");
        return circle;
    }
    return {};
}
bool aligned(const gp_Elips& cap,const C::EllipticExtrusion& support) {
    const auto& e=support.section;
    return cap.Axis().Direction().IsParallel(support.axis,1.e-8)
        &&cap.XAxis().Direction().IsParallel(e.XAxis().Direction(),1.e-8)
        &&gp_Lin(gp_Ax1(e.Location(),support.axis)).Distance(cap.Location())<1.e-6
        &&std::abs(cap.MajorRadius()-e.MajorRadius())<1.e-6
        &&std::abs(cap.MinorRadius()-e.MinorRadius())<1.e-6;
}
bool carrier(const TopoDS_Face& a,const TopoDS_Face& b) {
    return C::sameCarrier(a,b)||C::sameEllipticExtrusion(a,b);
}
void regionsPreserved(const TopologyIndex& before,const TopologyIndex& after) {
    for(const auto& record:before.faces()) {
        double covered=0;
        for(const auto& other:after.faces()) if(carrier(record.support.face,other.support.face))
            covered+=C::area(BooleanGeometry::combine<BRepAlgoAPI_Common>(record.support.face,other.support.face));
        if(!C::areaMatches(C::area(record.support.face),covered,1.e-8))
            throw Standard_Failure("The exact source trimmed regions do not match the certified extrusion.");
    }
}
TopoDS_Shape prism(const TopoDS_Face& face,const gp_Vec& delta) {
    BRepTiming::Event prismEvent("Prism",face);
    const auto shape=BRepPrimAPI_MakePrism(face,delta,true,true).Shape();prismEvent.finish(shape);return shape;
}
void recognize(const TopoDS_Shape& shape,const TopoDS_Face& target,Extrusion& result,TopologyIndex& topology) {
    BRepTiming::Scope timingScope("BRepInvariantExtrusion.recognize");
    QString error;
    if(!TopologyIndex::build(shape,topology,error)) throw Standard_Failure(error.toUtf8().constData());
    double volume=0;
    if((topology.faces().size()!=3&&topology.faces().size()!=4)||!topology.face(target)||!C::singleSolid(shape,volume)
       ||!C::withinTolerance(shape,1.e-6)||!C::manifold(shape))
        throw Standard_Failure("Requires two elliptical caps, one outer wall and optionally one central cylindrical hole.");
    TopExp_Explorer solid(shape,TopAbs_SOLID);
    BRepClass3d_SolidClassifier orientation(TopoDS::Solid(solid.Current()));orientation.PerformInfinitePoint(1.e-7);
    if(orientation.State()!=TopAbs_OUT) throw Standard_Failure("The extrusion shell orientation is invalid.");
    result.target=target;result.ellipse=capEllipse(*topology.face(target));result.outward=normal(target);
    const auto hole=capHole(*topology.face(target),result.ellipse);
    if(hole) result.holeRadius=hole->Radius();
    C::EllipticExtrusion support;
    for(const auto& face:topology.faces()) {
        if(face.support.face.IsSame(target)) continue;
        if(face.support.surface.GetType()==GeomAbs_Plane) result.opposite=face.support.face;
        else if(C::ellipticExtrusion(face.support,support)) result.wall=face.support.face;
        else if(face.support.surface.GetType()==GeomAbs_Cylinder&&hole) {
            const auto cylinder=face.support.surface.Cylinder();
            if(!cylinder.Axis().Direction().IsParallel(result.outward,1.e-8)
               ||gp_Lin(cylinder.Axis()).Distance(hole->Location())>1.e-6
               ||std::abs(cylinder.Radius()-hole->Radius())>1.e-6)
                throw Standard_Failure("The inner wall does not match the exact cap hole.");
            result.hole=face.support.face;
        }
        else throw Standard_Failure("Only an exact analytic-ellipse linear-extrusion wall is supported.");
    }
    if(result.opposite.IsNull()||result.wall.IsNull()||bool(hole)!=!result.hole.IsNull()
       ||topology.faces().size()!=(hole?4:3)||!aligned(result.ellipse,support)
       ||!result.outward.IsParallel(support.axis,1.e-8))
        throw Standard_Failure("The planar cap is not perpendicular to a unique elliptical extrusion axis.");
    const auto opposite=capEllipse(*topology.face(result.opposite));
    const auto oppositeHole=capHole(*topology.face(result.opposite),opposite);
    if(bool(hole)!=bool(oppositeHole)||(hole&&std::abs(hole->Radius()-oppositeHole->Radius())>1.e-6))
        throw Standard_Failure("The circular hole must pass through both caps unchanged.");
    if(!aligned(opposite,support)||!normal(result.opposite).IsEqual(result.outward.Reversed(),1.e-8))
        throw Standard_Failure("The opposite cap does not preserve the same oriented elliptical section.");
    result.height=gp_Vec(opposite.Location(),result.ellipse.Location()).Dot(gp_Vec(result.outward));
    result.sectionArea=std::numbers::pi*(result.ellipse.MajorRadius()*result.ellipse.MinorRadius()-result.holeRadius*result.holeRadius);
    if(!std::isfinite(result.height)||result.height<=1.e-7
       ||!C::areaMatches(result.sectionArea,C::area(target),1.e-8)
       ||!C::areaMatches(result.sectionArea,C::area(result.opposite),1.e-8)
       ||!closeVolume(volume,result.sectionArea*result.height))
        throw Standard_Failure("The cap areas, height and analytic extrusion volume disagree.");
    // Verify the actual adjacency, not merely the presence of suitable supports.
    for(const auto& cap:{target,result.opposite}) {
        const auto neighbors=topology.neighbors(cap);
        if(neighbors.size()!=(hole?2:1)
           ||std::none_of(neighbors.begin(),neighbors.end(),[&](const auto& f){return f.IsSame(result.wall);})
           ||(hole&&std::none_of(neighbors.begin(),neighbors.end(),[&](const auto& f){return f.IsSame(result.hole);})))
            throw Standard_Failure("The cap has an unexpected neighboring region.");
    }
    const auto oracle=prism(result.opposite,gp_Vec(result.outward)*result.height);
    if(!C::equivalent(shape,oracle)) throw Standard_Failure("Current material is not the complete certified extrusion.");
    TopologyIndex oracleTopology;
    if(!TopologyIndex::build(oracle,oracleTopology,error)) throw Standard_Failure(error.toUtf8().constData());
    regionsPreserved(topology,oracleTopology);regionsPreserved(oracleTopology,topology);
}
}

bool BRepReconstruction::moveEllipticalCap(const TopoDS_Shape& source,const TopoDS_Face& target,
                                         const gp_Vec& displacement,Result& result,QString& error) {
    BRepTiming::Scope timingScope("BRepInvariantExtrusion.BRepReconstruction::moveEllipticalCap");
    result={};error.clear();
    const auto fail=[&](const QString& reason){result={};error=QStringLiteral("Elliptical cap Move refused: ")+reason;return false;};
    try {
        if(!std::isfinite(displacement.X())||!std::isfinite(displacement.Y())||!std::isfinite(displacement.Z())
           ||displacement.Magnitude()<1.e-7) return fail(QStringLiteral("requires a finite nonzero axial displacement."));
        Neighborhood local;
        if(!neighborhood(source,target,local,error)) return false;
        Extrusion original;TopologyIndex sourceTopology;
        recognize(local.solid,local.target,original,sourceTopology);
        if(!gp_Dir(displacement).IsParallel(original.outward,1.e-8))
            return fail(QStringLiteral("non-axial cap displacement is unsupported."));
        const double distance=displacement.Dot(gp_Vec(original.outward));
        const double height=original.height+distance;
        if(!std::isfinite(height)||height<=1.e-7) return fail(QStringLiteral("the requested height collapses or inverts the solid."));
        const gp_Vec axial=gp_Vec(original.outward)*distance;
        const auto region=prism(local.target,axial);
        const auto candidate=prism(original.opposite,gp_Vec(original.outward)*height);
        double volume=0;
        if(!C::singleSolid(region,volume)||!closeVolume(volume,original.sectionArea*std::abs(distance)))
            return fail(QStringLiteral("the exact change volume could not be certified."));
        C::MaterialDifference change;
        if(!C::difference(local.solid,candidate,change,error)) return false;
        if(!C::equivalent(distance>0?change.added:change.removed,region)
           ||!closeVolume(C::volume(distance>0?change.removed:change.added),0))
            return fail(QStringLiteral("material change is not exactly the permitted axial region."));
        gp_Trsf shift;shift.SetTranslation(axial);
        auto affected=local.neighbors;affected.push_back(local.target);
        Result finished;
        if(!finishLocal(local,candidate,region,affected,{local.target},shift,finished,error)) return false;
        if(finished.target.IsNull()) return fail(QStringLiteral("the translated cap is ambiguous."));
        Extrusion final;TopologyIndex resultTopology;
        recognize(finished.solid,finished.target,final,resultTopology);
        if(!closeVolume(C::volume(finished.solid),original.sectionArea*height)
           ||!C::sameEllipticExtrusion(original.wall,final.wall)
           ||(!original.hole.IsNull()&&(final.hole.IsNull()||!C::sameCarrier(original.hole,final.hole)))
           ||!C::sameTrimmedRegion(original.opposite,final.opposite))
            return fail(QStringLiteral("the opposite cap or exact elliptical support changed."));
        const auto translated=TopoDS::Face(BRepBuilderAPI_Transform(local.target,shift,true).Shape());
        if(!C::sameTrimmedRegion(translated,finished.target))
            return fail(QStringLiteral("the complete translated cross-section did not survive."));
        // finishLocal's legacy analytic matcher deliberately excludes extrusion
        // carriers. Add only this independently certified wall correspondence.
        for(auto& mapping:finished.history)
            if(C::sameEllipticExtrusion(mapping.original,original.wall)) mapping.modified={final.wall};
        result=std::move(finished);return true;
    } catch(const Standard_Failure& failure) {return fail(QString::fromUtf8(failure.GetMessageString()));}
      catch(const std::exception&) {return fail(QStringLiteral("OCCT could not certify the axial extrusion reconstruction."));}
}
