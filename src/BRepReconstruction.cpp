#include "BRepTiming.h"
#include "BRepReconstruction.h"
#include "BRepTopology.h"
#include "BRepCertification.h"
#include "BRepMeasurement.h"
#include "BooleanGeometry.h"
#include "BRepSweepConstruction.h"
#include <BRepAdaptor_Surface.hxx>
#include <Adaptor3d_Curve.hxx>
#include <BOPAlgo_Splitter.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepTools.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <BRepTools_History.hxx>
#include <GProp_GProps.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Cone.hxx>
#include <gp_Lin.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <limits>
#include <Standard_Version.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <gp_Lin.hxx>
#include <cmath>

namespace {
TopTools_ListOfShape singleton(const TopoDS_Shape& shape) {
    TopTools_ListOfShape list;list.Append(shape);return list;
}
int count(const TopoDS_Shape& shape,TopAbs_ShapeEnum type) {
    TopTools_IndexedMapOfShape map; TopExp::MapShapes(shape,type,map); return map.Extent();
}
bool sameSupport(const TopoDS_Face& original,const TopoDS_Face& result,
                 const gp_Pln* expected=nullptr,bool unoriented=false) {
    return BRepCertification::sameCarrier(original,result,expected,unoriented);
}
// Scoped per-carrier correspondence retains Phase 5B.2's single-family proof.
BRepCertification::FaceCoverage splineCoverage(const TopoDS_Face& face,const BRepReconstruction::TopologyIndex& index) {
    BRepTiming::Scope timingScope("BRepReconstruction.splineCoverage");
    BRepCertification::FaceCoverage result;QString diagnostics;
    for(const auto& record:index.faces()) {
        BRepCertification::Correspondence mapping;QString error;
        if(!BRepCertification::correspondInvariantSpline(face,record.support.face,mapping,error)) {diagnostics+=error+"; ";continue;}
        const auto& coverage=mapping.faces.front().regions;
        result.carriers.insert(result.carriers.end(),coverage.carriers.begin(),coverage.carriers.end());
        result.overlaps.insert(result.overlaps.end(),coverage.overlaps.begin(),coverage.overlaps.end());
        result.coveredArea+=coverage.coveredArea;result.positiveArea+=coverage.positiveArea;
    }
    if(result.overlaps.empty()) throw Standard_Failure(diagnostics.toUtf8().constData());
    return result;
}
// A complete two-cap extrusion, not merely one recognizable adjacent wall.
double splineExtrusion(const BRepReconstruction::Neighborhood& local,const gp_Dir& axis,bool material=true) {
    BRepTiming::Scope timingScope("BRepReconstruction.splineExtrusion");
    using namespace BRepReconstruction;
    TopologyIndex index;QString error;
    if(!TopologyIndex::build(local.solid,index,error)) throw Standard_Failure(error.toUtf8().constData());
    const auto* cap=index.face(local.target);
    if(local.patch.size()!=1||!cap||cap->wires.size()!=1||!cap->wires.front().outer)
        throw Standard_Failure("Spline Move requires one complete cap without holes or split patches.");
    TopoDS_Face opposite;bool spline=false;
    for(const auto& record:index.faces()) {
        const auto face=record.support.face;const auto& surface=record.support.surface;
        if(face.IsSame(local.target)) continue;
        if(surface.GetType()==GeomAbs_Plane&&surface.Plane().Axis().Direction().IsParallel(axis,1.e-8)) {
            if(!opposite.IsNull()||record.wires.size()!=1)
                throw Standard_Failure("Spline Move requires exactly two complete planar caps without holes.");
            opposite=face;
        } else if(surface.GetType()==GeomAbs_SurfaceOfExtrusion) {
            InvariantSplineSupport proof;
            if(!record.support.invariantSpline(proof,error,&index)||!proof.axialContinuation||!proof.axis.IsParallel(axis,1.e-8))
                throw Standard_Failure(error.isEmpty()?"Inconsistent spline extrusion axis.":error.toUtf8().constData());
            spline=true;
        } else if(surface.GetType()!=GeomAbs_Plane||std::abs(surface.Plane().Axis().Direction().Dot(axis))>=1.e-8)
            throw Standard_Failure("Unsupported wall in complete spline boundary.");
    }
    if(!spline||opposite.IsNull()) throw Standard_Failure("A complete bounded spline extrusion was not established.");
    // Valid normalization may retain separate axial Face bands. The complete
    // section-sweep equivalence below certifies their union, rather than
    // requiring every individual band to touch both caps.
    if(!BRepCertification::manifold(local.solid)) throw Standard_Failure("Spline source must be manifold.");
    const auto top=BRepAdaptor_Surface(local.target).Plane(),bottom=BRepAdaptor_Surface(opposite).Plane();
    const double height=gp_Vec(bottom.Location(),top.Location()).Dot(gp_Vec(axis));
    if(height<=1.e-7) throw Standard_Failure("Invalid oriented spline extrusion height.");
    if(!material) return height; // Refusal-only preflight, never an acceptance certificate.
    BRepTiming::Event witnessEvent("Prism",local.target);
    const auto witness=BRepPrimAPI_MakePrism(local.target,-gp_Vec(axis)*height,true,true).Shape();
    witnessEvent.finish(witness);
    if(!BRepCertification::equivalent(local.solid,witness)
       ||!BRepCertification::areaMatches(BRepCertification::area(local.target)*height,local.certifiedVolume,1.e-8))
        throw Standard_Failure("Complete section/height material certificate failed.");
    return height;
}

}
bool BRepReconstruction::validSolid(const TopoDS_Shape& shape,double& volume) {
    BRepTiming::Scope timingScope("BRepReconstruction.BRepReconstruction::validSolid");
    return BRepCertification::singleSolid(shape,volume);
}
bool BRepReconstruction::sameAnalyticSupport(const TopoDS_Face& a,const TopoDS_Face& b) {
    return sameSupport(a,b);
}
namespace BRepReconstruction {
static bool analyzeNeighborhood(const TopoDS_Shape& source,const TopoDS_Face& target,
                                      Neighborhood& result,QString& error,bool certifySource) {
    BRepTiming::Scope timingScope("BRepReconstruction.BRepReconstruction::neighborhood");
    result={};error.clear();
    const auto fail=[&](const QString& reason){result={};error=reason;return false;};
    try {
        TopTools_IndexedMapOfShape faces;TopExp::MapShapes(source,TopAbs_FACE,faces);
        if(target.IsNull()||!faces.Contains(target)||faces.Extent()>64||count(source,TopAbs_EDGE)>256||count(source,TopAbs_VERTEX)>256)
            return fail(QStringLiteral("Select a member face within the local reconstruction complexity budget."));
        BRepTiming::Event copyEvent1("Copy",source);
        BRepBuilderAPI_Copy copy(source,true,false);
        copyEvent1.finish(copy.Shape());BRepTiming::copyOrigin(source,copy);result.solid=copy.Shape();
        double volume=0;
        if(certifySource ? !validSolid(result.solid,volume)
            : (!BRepCertification::validBRep(result.solid)||!BRepCertification::closedShells(result.solid)
               ||count(result.solid,TopAbs_SOLID)!=1||count(result.solid,TopAbs_SHELL)!=1))
            return fail(QStringLiteral("Neighborhood needs one valid closed solid and shell."));
        if(certifySource)result.certifiedVolume=volume;
        double offending=0;
        if(!BRepCertification::withinTolerance(result.solid,1.e-6,&offending))
            return fail(QStringLiteral("Source tolerance %1 exceeds the local reconstruction limit (1e-6).").arg(offending,0,'g',15));
        for(int i=1;i<=faces.Extent();++i) {
            auto copied=TopoDS::Face(copy.ModifiedShape(faces(i)));copied.Orientation(faces(i).Orientation());
            result.history.push_back({TopoDS::Face(faces(i)),{copied}});
            if(faces(i).IsSame(target)) result.target=copied;
        }
        TopologyIndex topology;
        if(!TopologyIndex::build(result.solid,topology,error)) {result={};return false;}
        TopTools_IndexedMapOfShape patch,neighbors;patch.Add(result.target);
        for(int i=1;i<=patch.Extent();++i)
            for(const auto& adjacent:topology.neighbors(TopoDS::Face(patch(i))))
                if(sameAnalyticSupport(result.target,adjacent)) patch.Add(adjacent);
        for(const auto& edge:topology.edges()) {
            TopTools_IndexedMapOfShape inside,outside;
            for(const auto& face:edge.faces)
                (patch.Contains(face)?inside:outside).Add(face);
            if(inside.IsEmpty()||outside.IsEmpty()) continue; // internal patch edges and periodic seams
            if(inside.Extent()!=1||outside.Extent()!=1) return fail(QStringLiteral("Selected patch has ambiguous boundary adjacency."));
            result.boundary.push_back(edge.edge);neighbors.Add(outside(1));
        }
        for(int i=1;i<=patch.Extent();++i) result.patch.push_back(TopoDS::Face(patch(i)));
        for(int i=1;i<=neighbors.Extent();++i) result.neighbors.push_back(TopoDS::Face(neighbors(i)));
        return true;
    } catch(const Standard_Failure&) {return fail(QStringLiteral("OCCT could not safely analyze the selected neighborhood."));}
}

} // namespace BRepReconstruction
bool BRepReconstruction::neighborhood(const TopoDS_Shape& source,const TopoDS_Face& target,
                                      Neighborhood& result,QString& error) {
    return analyzeNeighborhood(source,target,result,error,true);
}
bool BRepReconstruction::preflightPlanarSpline(const TopoDS_Shape& source,const TopoDS_Face& target,
                                              bool& spline,QString& error) {
    BRepTiming::Scope timing("BRepReconstruction.preflightPlanarSpline");
    spline=false;error.clear();
    try {
        for(TopExp_Explorer f(source,TopAbs_FACE);f.More();f.Next()) {
            const BRepAdaptor_Surface support(TopoDS::Face(f.Current()));
            const auto type=support.GetType();
            spline=spline||type==GeomAbs_BSplineSurface||(type==GeomAbs_SurfaceOfExtrusion
                &&support.BasisCurve()->GetType()==GeomAbs_BSplineCurve);
        }
        if(!spline) return true;
        Neighborhood local;if(!analyzeNeighborhood(source,target,local,error,false))return false;
        if(BRepAdaptor_Surface(local.target).GetType()!=GeomAbs_Plane) {
            error=QStringLiteral("Spline Move requires a planar cap.");return false;
        }
        auto normal=BRepAdaptor_Surface(local.target).Plane().Axis().Direction();
        if(local.target.Orientation()==TopAbs_REVERSED)normal.Reverse();
        splineExtrusion(local,normal,false);return true;
    } catch(const Standard_Failure& failure) {error=QString::fromUtf8(failure.GetMessageString());return false;}
}

bool BRepReconstruction::finishLocal(const Neighborhood& source,const TopoDS_Shape& candidate,
        const TopoDS_Shape& editRegion,const std::vector<TopoDS_Face>& affected,
        const std::vector<TopoDS_Face>& moved,const gp_Trsf& displacement,Result& result,QString& error,bool allowSpline,
        const gp_Pln* stackedCylinderSeamPlane) {
    BRepTiming::Scope timingScope("BRepReconstruction.BRepReconstruction::finishLocal");
    result={};error.clear();
    const auto fail=[&](const QString& reason){result={};error=reason;return false;};
    try {
        double volume=0;size_t solids=0;
        if(!BooleanGeometry::certifiedSolid(editRegion,volume,solids)) return fail(QStringLiteral("Local edit region must contain certified closed material volumes."));
        for(const auto type:{TopAbs_SHELL,TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX}) {
            TopTools_IndexedMapOfShape all,owned;TopExp::MapShapes(editRegion,type,all);
            for(TopExp_Explorer it(editRegion,TopAbs_SOLID);it.More();it.Next()) TopExp::MapShapes(it.Current(),type,owned);
            if(all.Extent()!=owned.Extent()) return fail(QStringLiteral("Local edit region contains loose topology outside its solids."));
        }
        TopTools_IndexedMapOfShape members,affectedMap,movedMap;
        TopExp::MapShapes(source.solid,TopAbs_FACE,members);
        for(const auto& f:affected) {if(!members.Contains(f)) return fail(QStringLiteral("Affected face is not in the isolated source."));affectedMap.Add(f);}
        for(const auto& f:moved) {if(!affectedMap.Contains(f)) return fail(QStringLiteral("Moved faces must be declared affected."));movedMap.Add(f);}
        TopoDS_Shape normalized;if(!normalize(candidate,normalized,error,true,stackedCylinderSeamPlane)) return false;
        if(!BRepCertification::preservedOutside(source.solid,normalized,editRegion))
            return fail(QStringLiteral("Local reconstruction changed material outside its certified edit region."));
        TopologyIndex resultTopology;
        if(!TopologyIndex::build(normalized,resultTopology,error)) return false;
        for(const auto& mapping:source.history) {
            if(mapping.modified.size()!=1||!members.Contains(mapping.modified.front())) return fail(QStringLiteral("Invalid neighborhood copy correspondence."));
            const auto copied=mapping.modified.front();
            const auto expected=movedMap.Contains(copied)?TopoDS::Face(BRepBuilderAPI_Transform(copied,displacement,true).Shape()):copied;
            const auto coverage=allowSpline&&BRepAdaptor_Surface(expected).GetType()==GeomAbs_SurfaceOfExtrusion
                ?splineCoverage(expected,resultTopology):BRepCertification::coverage(expected,resultTopology);
            FaceMapping final{mapping.original,coverage.overlaps};
            if(allowSpline&&BRepAdaptor_Surface(expected).GetType()==GeomAbs_SurfaceOfExtrusion&&coverage.overlaps.empty())
                return fail(QStringLiteral("An invariant spline wall has no certified trimmed-region correspondence."));
            if(!affectedMap.Contains(copied)&&!BRepCertification::areaMatches(BRepCertification::area(copied),coverage.positiveArea,1.e-8))
                return fail(QStringLiteral("An unaffected trimmed face region changed during local reconstruction."));
            if(copied.IsSame(source.target)&&final.modified.size()==1) result.target=final.modified.front();
            result.history.push_back(std::move(final));
        }
        result.solid=normalized;return true;
    } catch(const Standard_Failure& failure) {return fail(QStringLiteral("OCCT could not certify local reconstruction and face correspondence: %1").arg(QString::fromUtf8(failure.GetMessageString())));}
      catch(const std::exception&) {return fail(QStringLiteral("Boolean local-material certification failed."));}
}
namespace {
// F2.9G.5: on OCCT 7.6.3, releasing even these stacked seams can make
// ShapeUpgrade_UnifySameDomain::IntUnifyFaces loop (observed hang in
// cylindrical-boundary-follow-up-geometry). Merging is therefore enabled only
// on OCCT versions where it was validated (7.9.x); older kernels keep the
// original guarded normalization exactly (the split is retained).
constexpr bool stackedCylinderMergeSupported=OCC_VERSION_HEX>=0x070900;
std::vector<TopoDS_Edge> stackedCylinderSeams(const TopoDS_Shape& shape,const gp_Pln& plane); // F2.9G.5, defined before normalize
// Material side of a cylindrical Face: the sign of its outward normal (Face
// orientation applied) along the radial direction; > 0 when material lies
// inside the radius (boss/outer wall), < 0 for a hole. Independent of the
// carrier's parametrization handedness (gp_Ax3::Direct) and of the raw Face
// orientation flag, which differ between construction methods (F2.9G.5A).
double cylinderMaterialSide(const TopoDS_Face& face) {
    const BRepAdaptor_Surface s(face);gp_Pnt p;gp_Vec du,dv;
    s.D1((s.FirstUParameter()+s.LastUParameter())/2,(s.FirstVParameter()+s.LastVParameter())/2,p,du,dv);
    auto n=du.Crossed(dv);if(face.Orientation()==TopAbs_REVERSED) n.Reverse();
    auto radial=gp_Vec(s.Cylinder().Location(),p);const gp_Vec axis(s.Cylinder().Axis().Direction());radial-=axis*radial.Dot(axis);
    return n.Dot(radial);
}
QString planarRefusal(const QString& reason) {return QStringLiteral("Local planar reconstruction refused: ")+reason;}
}
// F2.9E.3: stages extracted verbatim from movePlanarLocal. Raw geometry only;
// Standard_Failure propagates to the caller exactly as the inline code did.
BRepReconstruction::SourceAdmission::SourceAdmission(const TopoDS_Shape& source,const TopoDS_Face& face)
    : source_(source),face_(face) {
    BRepTiming::Scope timingScope("BRepReconstruction.SourceAdmission");
    valid_=!face.IsNull()&&BRepCertification::singleSolid(source,volume_);
}
bool BRepReconstruction::planPlanarSweep(const TopoDS_Shape& source,const TopoDS_Face& target,
                                        bool allowSpline,PlanarSweepPlan& plan,QString& error,
                                        const SourceAdmission* admitted) {
    BRepTiming::Scope timingScope("BRepReconstruction.planPlanarSweep");
    plan={};error.clear();
    const auto fail=[&](const QString& reason){plan={};error=planarRefusal(reason);return false;};
    auto& local=plan.local;
    const bool reuse=admitted&&admitted->binds(source,target);
    if(!analyzeNeighborhood(source,target,local,error,!reuse)) {plan={};return false;}
    if(reuse) local.certifiedVolume=admitted->volume();
    if(BRepAdaptor_Surface(local.target).GetType()!=GeomAbs_Plane)
        return fail(QStringLiteral("requires a connected planar support patch."));
    auto normal=BRepAdaptor_Surface(local.target).Plane().Axis().Direction();
    if(local.target.Orientation()==TopAbs_REVERSED) normal.Reverse();
    plan.normal=normal;
    plan.splineEdit=allowSpline&&std::any_of(local.neighbors.begin(),local.neighbors.end(),[](const auto& face){
        const auto type=BRepAdaptor_Surface(face).GetType();return type==GeomAbs_SurfaceOfExtrusion||type==GeomAbs_BSplineSurface;
    });
    if(plan.splineEdit) plan.originalHeight=splineExtrusion(local,normal);
    // These adjacent supports retain exactly the same cross section during
    // the sweep. Sloped/curved intersections need another reconstruction.
    for(const auto& face:local.neighbors) {
        const BRepAdaptor_Surface surface(face);
        const bool invariant=surface.GetType()==GeomAbs_Plane
            ? std::abs(surface.Plane().Axis().Direction().Dot(normal))<1.e-8
            : surface.GetType()==GeomAbs_Cylinder&&surface.Cylinder().Axis().Direction().IsParallel(normal,1.e-8);
        if(!invariant&&!plan.splineEdit) return fail(QStringLiteral("an adjacent support changes cross section along the requested direction."));
    }
    return true;
}
void BRepReconstruction::sweepPlanarRegion(const PlanarSweepPlan& plan,double distance,PlanarSweep& sweep) {
    BRepTiming::Scope timingScope("BRepReconstruction.sweepPlanarRegion");
    sweep={};
    sweep.delta=gp_Vec(plan.normal)*distance;sweep.shift.SetTranslation(sweep.delta);
    for(const auto& patch:plan.local.patch) {
        const auto swept=detail::sweepPatch(patch,sweep.delta);
        sweep.region=sweep.region.IsNull()?swept:BooleanGeometry::combine<BRepAlgoAPI_Fuse>(sweep.region,swept);
    }
}
void BRepReconstruction::constructPlanarSweep(const PlanarSweepPlan& plan,double distance,PlanarSweep& sweep) {
    BRepTiming::Scope timingScope("BRepReconstruction.constructPlanarSweep");
    sweep.directional=detail::applySweep(plan.local.solid,sweep.region,distance);
    sweep.complete.Nullify();
    if(plan.splineEdit) {
        // OCCT Cut can replace exact isoparametric line pcurves by
        // unproven spline pcurves. Reuse the certified complete cap in the
        // same exact prism constructor; the caller independently compares it
        // with the Boolean material result. No curve is fitted or healed.
        const auto movedCap=TopoDS::Face(BRepBuilderAPI_Transform(plan.local.target,sweep.shift,true).Shape());
        BRepTiming::Event completeEvent("Prism",movedCap);
        sweep.complete=BRepPrimAPI_MakePrism(movedCap,-gp_Vec(plan.normal)*(plan.originalHeight+distance),true,true).Shape();
        completeEvent.finish(sweep.complete);
    }
}
bool BRepReconstruction::movePlanarLocal(const TopoDS_Shape& source,const TopoDS_Face& target,
                                        double distance,Result& result,QString& error,bool allowSpline) {
    BRepTiming::Scope timingScope("BRepReconstruction.BRepReconstruction::movePlanarLocal");
    result={};error.clear();
    const auto fail=[&](const QString& reason){result={};error=planarRefusal(reason);return false;};
    try {
        if(!std::isfinite(distance)||std::abs(distance)<1.e-7) return fail(QStringLiteral("requires a finite nonzero offset."));
        // Plan: neighborhood (isolated copy + full source certification) and
        // value-independent support admission.
        PlanarSweepPlan plan;
        if(!planPlanarSweep(source,target,allowSpline,plan,error)) return false;
        const auto& local=plan.local;const auto& normal=plan.normal;
        const bool splineEdit=plan.splineEdit;const double originalHeight=plan.originalHeight;
        // Value-dependent refusal formerly between the height and the
        // neighbor test; that test cannot refuse a spline edit, so the
        // outcome and message are unchanged.
        if(splineEdit&&originalHeight+distance<=1.e-7) return fail(QStringLiteral("collapsed or inverted extrusion height."));
        // Construct (1/2): trimmed-patch sweep region.
        PlanarSweep sweep;sweepPlanarRegion(plan,distance,sweep);
        const auto& region=sweep.region;const auto& shift=sweep.shift;
        // Certify: region validity and contact refusals stay before applySweep.
        double regionVolume=0;
        if(!validSolid(region,regionVolume)) return fail(QStringLiteral("the trimmed-face sweep is not a valid closed volume."));
        const auto overlap=BooleanGeometry::combine<BRepAlgoAPI_Common>(local.solid,region);
        GProp_GProps common;common=BRepMeasurement::volumeProperties(overlap);
        if(distance>0 ? std::abs(common.Mass())>1.e-8*std::max(1.,regionVolume) : !equivalent(overlap,region))
            return fail(QStringLiteral("the sweep meets other material or crosses a material boundary; contact-changing edits need a separate certificate."));
        // Construct (2/2): directional Boolean and, for spline edits, the
        // complete cap prism. Everything below is certification.
        constructPlanarSweep(plan,distance,sweep);
        if(splineEdit&&!equivalent(sweep.directional,sweep.complete))
            return fail(QStringLiteral("complete cap prism differs from the directional Boolean result."));
        const auto& candidate=sweep.candidate();
        const auto actualChange=distance>0?BooleanGeometry::combine<BRepAlgoAPI_Cut>(candidate,local.solid)
                                          :BooleanGeometry::combine<BRepAlgoAPI_Cut>(local.solid,candidate);
        if(!equivalent(actualChange,region)) return fail(QStringLiteral("the actual material change differs from the trimmed-face sweep."));
        auto affected=local.neighbors;affected.insert(affected.end(),local.patch.begin(),local.patch.end());
        // Earlier edits may leave seams on an invariant wall. A sweep crossing
        // such a seam changes the adjoining patch on the same analytic support.
        // Only actual swept surface regions join the affected neighborhood.
        for(TopExp_Explorer it(local.solid,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());
            if(std::none_of(affected.begin(),affected.end(),[&](const auto& f){return f.IsSame(face);})&&
               std::any_of(local.neighbors.begin(),local.neighbors.end(),[&](const auto& f){return sameAnalyticSupport(f,face);})) {
                GProp_GProps overlapArea;overlapArea=BRepMeasurement::areaProperties(BooleanGeometry::combine<BRepAlgoAPI_Common>(face,region));
                if(overlapArea.Mass()>1.e-12) affected.push_back(face);
            }
        }
        // F2.9G.5: certify with stacked cylindrical seams in the moved Face's
        // original plane released for merging; if anything after that refuses,
        // certify again with the original guarded normalization (unchanged).
        const gp_Pln originalPlane=BRepAdaptor_Surface(local.target).Plane();
        const auto certify=[&](const gp_Pln* seamPlane)->bool {
            Result finished;
            if(!finishLocal(local,candidate,region,affected,local.patch,shift,finished,error,splineEdit,seamPlane)) return false;
            std::vector<TopoDS_Face> expectedPatch;
            for(const auto& patch:local.patch) expectedPatch.push_back(TopoDS::Face(BRepBuilderAPI_Transform(patch,shift,true).Shape()));
            const auto expected=expectedPatch.front();
            const auto sameBoundary=[&](const TopoDS_Face& a,const TopoDS_Face& b) {
                if(splineEdit&&BRepAdaptor_Surface(a).GetType()==GeomAbs_SurfaceOfExtrusion) {
                    BRepCertification::Correspondence mapping;QString diagnostic;
                    return BRepCertification::correspondInvariantSpline(a,b,mapping,diagnostic)
                        &&!mapping.faces.front().regions.carriers.empty();
                }
                if(!sameAnalyticSupport(a,b)) return false;
                if(BRepAdaptor_Surface(a).GetType()!=GeomAbs_Cylinder) return true;
                // Extending past a remote cap must not invent an oppositely
                // oriented cylindrical cavity on the same geometric support.
                return cylinderMaterialSide(a)*cylinderMaterialSide(b)>0;
            };
            double covered=0,targetArea=0;for(const auto& patch:expectedPatch) {GProp_GProps area;area=BRepMeasurement::areaProperties(patch);targetArea+=area.Mass();}
            for(TopExp_Explorer it(finished.solid,TopAbs_FACE);it.More();it.Next()) {
                const auto face=TopoDS::Face(it.Current());bool allowed=sameBoundary(expected,face);
                if(allowed) {
                    for(const auto& patch:expectedPatch) {GProp_GProps area;area=BRepMeasurement::areaProperties(BooleanGeometry::combine<BRepAlgoAPI_Common>(patch,face));covered+=area.Mass();}
                }
                for(TopExp_Explorer old(local.solid,TopAbs_FACE);old.More()&&!allowed;old.Next())
                    if(std::none_of(local.patch.begin(),local.patch.end(),[&](const auto& patch){return patch.IsSame(old.Current());})) allowed=sameBoundary(TopoDS::Face(old.Current()),face);
                if(!allowed) {
                    const BRepAdaptor_Surface s(face);GProp_GProps p;p=BRepMeasurement::areaProperties(face);
                    return fail(QStringLiteral("the result introduced an unintended transition surface (type %1, center %2, %3, %4).")
                        .arg(int(s.GetType())).arg(p.CentreOfMass().X()).arg(p.CentreOfMass().Y()).arg(p.CentreOfMass().Z()));
                }
            }
            if(std::abs(covered-targetArea)>1.e-8*std::max(1.,targetArea))
                return fail(QStringLiteral("the complete translated target patch did not survive."));
            if(splineEdit) {
                if(!BRepCertification::withinTolerance(finished.solid,1.e-6)||!BRepCertification::manifold(finished.solid)
                   ||!BRepCertification::areaMatches(targetArea*(originalHeight+distance),BRepCertification::volume(finished.solid),1.e-8))
                    return fail(QStringLiteral("spline result topology or section/height certificate failed."));
                // Require exact eligible supports after normalization as well.
                Neighborhood finalLocal;
                if(finished.target.IsNull()||!neighborhood(finished.solid,finished.target,finalLocal,error)) return false;
                const double finalHeight=splineExtrusion(finalLocal,normal);
                if(std::abs(finalHeight-originalHeight-distance)>1.e-6) return fail(QStringLiteral("final extrusion height changed unexpectedly."));
                BRepMeasurement::Result surfaceMeasurement;
                if(!BRepMeasurement::area(finished.solid,surfaceMeasurement,error)) return false;
            }
            result=std::move(finished);error.clear();return true;
        };
        // Only when such seams exist does the first attempt differ from the
        // original normalization; otherwise certify once, exactly as before.
        if(!stackedCylinderMergeSupported||splineEdit||stackedCylinderSeams(candidate,originalPlane).empty()) return certify(nullptr);
        if(certify(&originalPlane)) return true;
        return certify(nullptr);
    } catch(const Standard_Failure& failure) {return fail(QStringLiteral("OCCT could not reconstruct this neighborhood safely: %1").arg(QString::fromUtf8(failure.GetMessageString())));}
      catch(const std::exception&) {return fail(QStringLiteral("local material certification failed."));}
}

namespace {
// F2.9G.5 stacked cylindrical band seams in `plane`: an edge shared by exactly two
// distinct cylindrical Faces of one solid with geometrically identical carriers
// (sameCarrier both ways; axis/radius identical to 64 ulp), the same material side,
// whose shared edge is a circle perpendicular to and centred on the axis (one
// axial height) lying in `plane` (1e-6), and which spans each Face's full
// angular range (the Faces abut along their whole width). Returns those seams
// plus the periodic seam edges of full (2 pi) bands. Topology/geometry only.
std::vector<TopoDS_Edge> stackedCylinderSeams(const TopoDS_Shape& shape,const gp_Pln& plane) {
    std::vector<TopoDS_Edge> result;
    TopTools_IndexedDataMapOfShapeListOfShape owners;TopExp::MapShapesAndAncestors(shape,TopAbs_EDGE,TopAbs_FACE,owners);
    const double eps=64*std::numeric_limits<double>::epsilon();
    const auto span=[](const TopoDS_Face& f){double u0,u1,v0,v1;BRepTools::UVBounds(f,u0,u1,v0,v1);return u1-u0;};
    for(int i=1;i<=owners.Extent();++i) {
        const auto& faces=owners(i);if(faces.Extent()!=2) continue;
        const auto a=TopoDS::Face(faces.First()),b=TopoDS::Face(faces.Last());
        if(a.IsSame(b)) continue;
        const BRepAdaptor_Surface sa(a),sb(b);
        if(sa.GetType()!=GeomAbs_Cylinder||sb.GetType()!=GeomAbs_Cylinder) continue;
        // Same material side, not the same raw orientation flag: a separately
        // Cut hole and its swept extension can carry opposite-handed carriers
        // with opposite flags while bounding material identically (F2.9G.5A).
        if(cylinderMaterialSide(a)*cylinderMaterialSide(b)<=0) continue;
        if(!BRepCertification::sameCarrier(a,b)||!BRepCertification::sameCarrier(b,a)) continue;
        const auto ca=sa.Cylinder(),cb=sb.Cylinder();const gp_Lin axis(ca.Axis());
        const double scale=std::max({1.,gp_Vec(ca.Location().XYZ()).Magnitude(),gp_Vec(cb.Location().XYZ()).Magnitude(),ca.Radius()});
        if(gp_Vec(ca.Axis().Direction()).Crossed(gp_Vec(cb.Axis().Direction())).Magnitude()>eps
           ||axis.Distance(cb.Location())>eps*scale||std::abs(ca.Radius()-cb.Radius())>eps*scale) continue;
        const auto edge=TopoDS::Edge(owners.FindKey(i));const BRepAdaptor_Curve curve(edge);
        if(curve.GetType()!=GeomAbs_Circle) continue;
        const auto circle=curve.Circle();
        if(!circle.Axis().Direction().IsParallel(ca.Axis().Direction(),1.e-7)||axis.Distance(circle.Location())>1.e-6
           ||std::abs(circle.Radius()-ca.Radius())>1.e-6) continue;                         // one axial height
        if(!plane.Axis().Direction().IsParallel(ca.Axis().Direction(),1.e-7)||plane.Distance(circle.Location())>1.e-6) continue;
        const double arc=curve.LastParameter()-curve.FirstParameter();
        if(std::abs(span(a)-arc)>1.e-9||std::abs(span(b)-arc)>1.e-9) continue;              // abut along the whole width
        result.push_back(edge);
        if(std::abs(arc-2*M_PI)<=1.e-9)
            for(const auto& f:{a,b}) for(TopExp_Explorer e(f,TopAbs_EDGE);e.More();e.Next())
                if(BRep_Tool::IsClosed(TopoDS::Edge(e.Current()),f)) result.push_back(TopoDS::Edge(e.Current()));
    }
    return result;
}
}
bool BRepReconstruction::normalize(const TopoDS_Shape& candidate,TopoDS_Shape& result,QString& error,bool allowValidSplits,
                                   const gp_Pln* stackedCylinderSeamPlane) {
    BRepTiming::Scope timingScope("BRepReconstruction.BRepReconstruction::normalize");
    result.Nullify();
    try {
        double volume=0;
        if(!validSolid(candidate,volume)) {
            error=QStringLiteral("Reconstruction did not produce one valid closed solid.");return false;
        }
        try {
            BRepTiming::Event copyEvent2("Copy",candidate);
        BRepBuilderAPI_Copy isolated(candidate,true,false);
        copyEvent2.finish(isolated.Shape());BRepTiming::copyOrigin(candidate,isolated);
            auto cleaned=isolated.Shape();
            {BRepTiming::Event removal("Normalize.RemoveInternals",cleaned);
             BRepTools::RemoveInternals(cleaned,false);removal.finish(cleaned);BRepTiming::forgetCopy(cleaned);}
            ShapeUpgrade_UnifySameDomain unify(cleaned,true,true,false);
            if(allowValidSplits) {
                // Preserve cylindrical trims during optional cleanup. Merging
                // bands with different axial extents can throw or loop inside
                // OCCT's same-domain unifier. Planar seams may still merge.
                // F2.9G.5: opt-in exception for stacked bands seamed in the moved
                // Face's original plane; all other cylindrical edges stay pinned.
                const auto released=stackedCylinderMergeSupported&&stackedCylinderSeamPlane
                    ?stackedCylinderSeams(cleaned,*stackedCylinderSeamPlane):std::vector<TopoDS_Edge>{};
                for(TopExp_Explorer f(cleaned,TopAbs_FACE);f.More();f.Next())
                    if(BRepAdaptor_Surface(TopoDS::Face(f.Current())).GetType()==GeomAbs_Cylinder)
                        for(TopExp_Explorer e(f.Current(),TopAbs_EDGE);e.More();e.Next())
                            if(std::none_of(released.begin(),released.end(),[&](const TopoDS_Edge& x){return x.IsSame(e.Current());}))
                                unify.KeepShape(e.Current());
            }
            unify.SetSafeInputMode(true);
            {BRepTiming::Event unification("Normalize.Unify",cleaned);unify.Build();unification.finish(unify.Shape());}
            if(validSolid(unify.Shape(),volume)&&equivalent(candidate,unify.Shape())) {
                result=unify.Shape();error.clear();return true;
            }
            error=QStringLiteral("Final unification changed material or invalidated the solid.");
        } catch(const Standard_Failure&) {
            error=QStringLiteral("OCCT could not safely unify the reconstructed solid.");
        }
        // Optional same-domain merging can return bad geometry OR throw (in
        // particular on OCCT 7.6). Both cases may retain the original split
        // candidate, with exactly the same solid/material certification.
        if(allowValidSplits) {
            BRepTiming::Event copyEvent3("Copy",candidate);
        BRepBuilderAPI_Copy splitCopy(candidate,true,false);
        copyEvent3.finish(splitCopy.Shape());BRepTiming::copyOrigin(candidate,splitCopy);
            if(validSolid(splitCopy.Shape(),volume)&&equivalent(candidate,splitCopy.Shape())) {
                result=splitCopy.Shape();error.clear();return true;
            }
        }
        return false;
    } catch(const Standard_Failure&) {
        error=QStringLiteral("OCCT could not safely unify the reconstructed solid.");return false;
    }
}
bool BRepReconstruction::prepare(const TopoDS_Shape& source,const TopoDS_Face& target,
                                 Prepared& result,QString& error) {
    BRepTiming::Scope timingScope("BRepReconstruction.BRepReconstruction::prepare");
    result={};
    error.clear();
    try {
        BRepTiming::Event copyEvent4("Copy",source);
        BRepBuilderAPI_Copy copy(source,true,false);
        copyEvent4.finish(copy.Shape());BRepTiming::copyOrigin(source,copy);
        const auto copiedTarget=TopoDS::Face(copy.ModifiedShape(target));
        ShapeUpgrade_UnifySameDomain unify(copy.Shape(),true,true,false);
        unify.SetSafeInputMode(true);
        // A selected patch must not silently expand into an adjacent coplanar
        // patch. Keep its boundary; other redundant Boolean seams may merge.
        for(TopExp_Explorer e(copiedTarget,TopAbs_EDGE);e.More();e.Next()) unify.KeepShape(e.Current());
        unify.Build();
        const auto candidate=unify.Shape();
        double normalizedVolume=0;
        if(!validSolid(candidate,normalizedVolume)) {
            error=QStringLiteral("B-Rep normalization failed.");return false;
        }
        GProp_GProps before,after;
        before=BRepMeasurement::volumeProperties(source);
        after=BRepMeasurement::volumeProperties(candidate);
        if(before.CentreOfMass().Distance(after.CentreOfMass())>1.e-6 || !equivalent(source,candidate)) {
            error=QStringLiteral("B-Rep normalization changed the volume.");return false;
        }
        for(TopExp_Explorer f(source,TopAbs_FACE);f.More();f.Next()) {
            FaceMapping mapping{TopoDS::Face(f.Current()),{}};
            const auto copied=copy.ModifiedShape(f.Current());
            const auto& modified=unify.History()->Modified(copied);
            for(TopExp_Explorer c(candidate,TopAbs_FACE);c.More();c.Next()) {
                bool matches=c.Current().IsSame(copied);
                for(TopTools_ListIteratorOfListOfShape it(modified);it.More();it.Next())
                    matches=matches||c.Current().IsSame(it.Value());
                if(matches) mapping.modified.push_back(TopoDS::Face(c.Current()));
            }
            if(mapping.modified.empty()) {
                error=QStringLiteral("An original face was lost during B-Rep normalization.");return false;
            }
            if(f.Current().IsSame(target)) {
                if(mapping.modified.size()!=1) {
                    error=QStringLiteral("The selected face has an ambiguous reconstruction history.");return false;
                }
                result.target=mapping.modified.front();
            }
            result.history.push_back(mapping);
        }
        if(result.target.IsNull()) {error=QStringLiteral("Selected face was lost during normalization.");return false;}
        result.solid=candidate;
        return true;
    } catch(const Standard_Failure&) {
        error=QStringLiteral("Open CASCADE could not normalize this B-Rep.");return false;
    }
}
bool BRepReconstruction::supports(const TopoDS_Shape& source,const TopoDS_Shape& result,
        const TopoDS_Face& target,const gp_Pln& expected,QString& error) {
    // Bidirectional support coverage permits edge splits and coplanar merging,
    // but neither new transition surfaces nor disappearing analytic supports.
    for(int pass=0;pass<2;++pass) {
        const auto& from=pass==0?source:result;
        const auto& to=pass==0?result:source;
        for(TopExp_Explorer f(from,TopAbs_FACE);f.More();f.Next()) {
            bool found=false;
            for(TopExp_Explorer g(to,TopAbs_FACE);g.More();g.Next()) {
                const auto a=TopoDS::Face(pass==0?f.Current():g.Current());
                const auto b=TopoDS::Face(pass==0?g.Current():f.Current());
                if(sameSupport(a,b,a.IsSame(target)?&expected:nullptr)) {found=true;break;}
            }
            if(!found) {
                error=QStringLiteral("Reconstruction lost an original support or introduced an unintended transition surface.");
                return false;
            }
        }
    }
    return true;
}

namespace {
double mass(const TopoDS_Shape& shape) {
    GProp_GProps properties;properties=BRepMeasurement::volumeProperties(shape);return properties.Mass();
}
bool samePlane(const gp_Pln& a,const gp_Pln& b) {
    return a.Axis().Direction().IsParallel(b.Axis().Direction(),1.e-8)
        && a.Distance(b.Location())<1.e-7;
}
double signedDistance(const gp_Pln& plane,const gp_Pnt& point) {
    return gp_Vec(plane.Location(),point).Dot(gp_Vec(plane.Axis().Direction()));
}
// A convex cell is certified by an interior centroid and all its vertices
// lying on the interior side of every bounding support. Only actual facets
// constrain it: distant, nonadjacent planes must not clip a legitimate overhang.
bool cellConstraints(const TopoDS_Shape& cell,const std::vector<gp_Pln>& planes,std::vector<int>& sides) {
    GProp_GProps properties;properties=BRepMeasurement::volumeProperties(cell);
    if(count(cell,TopAbs_SOLID)!=1 || properties.Mass()<=1.e-9) return false;
    const auto interior=properties.CentreOfMass();
    BRepClass3d_SolidClassifier classifier(cell,interior,1.e-7);
    if(classifier.State()!=TopAbs_IN) return false;
    sides.assign(planes.size(),0);
    for(TopExp_Explorer f(cell,TopAbs_FACE);f.More();f.Next()) {
        const BRepAdaptor_Surface support(TopoDS::Face(f.Current()));
        if(support.GetType()!=GeomAbs_Plane) return false;
        size_t index=0;while(index<planes.size()&&!samePlane(planes[index],support.Plane())) ++index;
        if(index==planes.size()) return false;
        const double distance=signedDistance(planes[index],interior);
        if(std::abs(distance)<1.e-6) return false;
        sides[index]=distance>0?1:-1;
        for(TopExp_Explorer vertex(cell,TopAbs_VERTEX);vertex.More();vertex.Next())
            if(sides[index]*signedDistance(planes[index],BRep_Tool::Pnt(TopoDS::Vertex(vertex.Current())))<-1.e-5) return false;
    }
    return true;
}
// A cell facet can cease to be geometrically necessary after arrangement
// splitting: e.g. the old tilted top plus a horizontal split already imply
// y >= 0. Moving that top can invalidate this implication and grow a wedge
// through the real front face. Retain the selected patch's physical neighbor
// trims even when they were redundant in an individual source cell.
// These are local boundary supports, NOT every plane in the solid (which
// would incorrectly clip legitimate overhangs past unrelated outer bounds).
bool targetTrims(const TopoDS_Shape& solid,const TopoDS_Face& target,
                 const std::vector<gp_Pln>& planes,std::vector<int>& trims) {
    if(count(target,TopAbs_WIRE)!=1) return false;
    trims.assign(planes.size(),0);
    TopTools_IndexedDataMapOfShapeListOfShape adjacency;
    TopExp::MapShapesAndAncestors(solid,TopAbs_EDGE,TopAbs_FACE,adjacency);
    for(TopExp_Explorer edge(target,TopAbs_EDGE);edge.More();edge.Next()) {
        if(!adjacency.Contains(edge.Current())) return false;
        const auto& neighbors=adjacency.FindFromKey(edge.Current());
        if(neighbors.Extent()!=2) return false;
        for(TopTools_ListIteratorOfListOfShape n(neighbors);n.More();n.Next()) {
            if(n.Value().IsSame(target)) continue;
            const auto support=BRepAdaptor_Surface(TopoDS::Face(n.Value())).Plane();
            size_t i=0;while(i<planes.size()&&!samePlane(planes[i],support)) ++i;
            if(i==planes.size()) return false;
            int side=0;
            for(TopExp_Explorer vertex(target,TopAbs_VERTEX);vertex.More();vertex.Next()) {
                const double d=signedDistance(planes[i],BRep_Tool::Pnt(TopoDS::Vertex(vertex.Current())));
                if(std::abs(d)<1.e-7) continue;
                const int next=d>0?1:-1;
                if(side&&side!=next) return false; // Concave trim needs a local decomposition.
                side=next;
            }
            if(!side||(trims[i]&&trims[i]!=side)) return false;
            trims[i]=side;
        }
    }
    return true;
}
bool sharedTarget(const TopoDS_Shape& a,const TopoDS_Shape& b,const gp_Pln& target) {
    for(TopExp_Explorer f(a,TopAbs_FACE);f.More();f.Next()) {
        if(!samePlane(BRepAdaptor_Surface(TopoDS::Face(f.Current())).Plane(),target)) continue;
        for(TopExp_Explorer g(b,TopAbs_FACE);g.More();g.Next()) {
            if(!samePlane(BRepAdaptor_Surface(TopoDS::Face(g.Current())).Plane(),target)) continue;
            if(f.Current().IsSame(g.Current())) return true;
            BRepAlgoAPI_Common common;common.SetArguments(singleton(f.Current()));common.SetTools(singleton(g.Current()));
            common.SetNonDestructive(true);common.Build();
            if(!common.IsDone()||common.HasErrors()) throw Standard_Failure("Cannot establish target-cell adjacency.");
            GProp_GProps area;area=BRepMeasurement::areaProperties(common.Shape());
            if(area.Mass()>1.e-8) return true;
        }
    }
    return false;
}
bool finishImpl(const TopoDS_Shape& source,const TopoDS_Shape& candidate,
        const TopoDS_Face& target,const gp_Pln& expected,BRepReconstruction::Result& result,
        QString& error,bool certifiedCells) {
    result={};
    try {
        TopoDS_Shape normalized;
        if(!BRepReconstruction::normalize(candidate,normalized,error)) return false;
        TopTools_IndexedDataMapOfShapeListOfShape adjacency;
        TopExp::MapShapesAndAncestors(normalized,TopAbs_EDGE,TopAbs_FACE,adjacency);
        for(int i=1;i<=adjacency.Extent();++i) {
            const auto& faces=adjacency.FindFromIndex(i);
            if(faces.Extent()!=2 || faces.First().IsSame(faces.Last())) continue;
            const BRepAdaptor_Surface a(TopoDS::Face(faces.First())),b(TopoDS::Face(faces.Last()));
            if(a.GetType()==GeomAbs_Plane&&b.GetType()==GeomAbs_Plane&&samePlane(a.Plane(),b.Plane())) {
                error=QStringLiteral("Final normalization left a redundant coplanar dividing edge.");return false;
            }
        }
        if(!certifiedCells && !BRepReconstruction::supports(source,normalized,target,expected,error)) return false;
        for(TopExp_Explorer f(source,TopAbs_FACE);f.More();f.Next()) {
            BRepReconstruction::FaceMapping mapping{TopoDS::Face(f.Current()),{}};
            for(TopExp_Explorer g(normalized,TopAbs_FACE);g.More();g.Next())
                if(sameSupport(mapping.original,TopoDS::Face(g.Current()),mapping.original.IsSame(target)?&expected:nullptr,certifiedCells&&!mapping.original.IsSame(target)))
                    mapping.modified.push_back(TopoDS::Face(g.Current()));
            if(mapping.original.IsSame(target)) {
                if(mapping.modified.empty()) {
                    error=QStringLiteral("The requested target plane disappeared; this edit is ambiguous.");return false;
                }
                if(mapping.modified.size()==1) result.target=mapping.modified.front();
            }
            result.history.push_back(mapping);
        }
        // Even the certified-cell route may only create patches on original
        // supports or the requested target, never seed-box or transition faces.
        for(TopExp_Explorer f(normalized,TopAbs_FACE);f.More();f.Next()) {
            bool mapped=false;
            for(const auto& mapping:result.history) for(const auto& face:mapping.modified)
                if(face.IsSame(f.Current())) mapped=true;
            if(!mapped) {
                error=QStringLiteral("Reconstruction introduced a bounding-box or transition support.");return false;
            }
        }
        result.solid=normalized;error.clear();return true;
    } catch(const Standard_Failure&) {
        error=QStringLiteral("OCCT could not safely unify the reconstructed solid.");return false;
    }
}
}
bool BRepReconstruction::equivalent(const TopoDS_Shape& a,const TopoDS_Shape& b) {
    return BRepCertification::equivalent(a,b);
}
bool BRepReconstruction::finish(const TopoDS_Shape& source,const TopoDS_Shape& candidate,
        const TopoDS_Face& target,const gp_Pln& expected,Result& result,QString& error) {
    return finishImpl(source,candidate,target,expected,result,error,false);
}
bool BRepReconstruction::rebuildPlanar(const TopoDS_Shape& source,const TopoDS_Face& target,
        const gp_Pln& expected,Result& result,QString& error) {
    result={};
    const auto fail=[&](const QString& reason) {error=QStringLiteral("Planar topology reconstruction refused: ")+reason;return false;};
    try {
        Prepared prepared;
        if(!prepare(source,target,prepared,error)) return false;
        const auto originalTarget=BRepAdaptor_Surface(target).Plane();
        if(originalTarget.Axis().Direction().Dot(expected.Axis().Direction())<=1.e-4)
            return fail(QStringLiteral("target rotation must remain below 90 degrees."));
        int targetPatches=0;
        std::vector<gp_Pln> planes;
        for(TopExp_Explorer f(prepared.solid,TopAbs_FACE);f.More();f.Next()) {
            const BRepAdaptor_Surface support(TopoDS::Face(f.Current()));
            if(support.GetType()!=GeomAbs_Plane) return fail(QStringLiteral("cell reconstruction requires planar supports."));
            if(samePlane(support.Plane(),originalTarget)) ++targetPatches;
            if(std::none_of(planes.begin(),planes.end(),[&](const auto& plane){return samePlane(plane,support.Plane());}))
                planes.push_back(support.Plane());
        }
        if(targetPatches!=1 || planes.size()>12) return fail(QStringLiteral("requires one target patch and at most 12 distinct planes."));
        size_t targetIndex=0;
        while(targetIndex<planes.size()&&!samePlane(planes[targetIndex],originalTarget)) ++targetIndex;
        if(targetIndex==planes.size()) return fail(QStringLiteral("target support was lost."));
        std::vector<int> trims;
        if(!targetTrims(prepared.solid,prepared.target,planes,trims))
            return fail(QStringLiteral("target boundary requires an unsupported concave or ambiguous trim decomposition."));
        // Preserve the target surface's parameter normal (not merely its locus).
        gp_Pln replacement=expected;
        if(planes[targetIndex].Axis().Direction().Dot(originalTarget.Axis().Direction())<0)
            replacement=gp_Pln(expected.Location(),expected.Axis().Direction().Reversed());
        Bnd_Box bounds;BRepBndLib::AddOptimal(prepared.solid,bounds,false,false);
        double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
        const double span=std::max({x1-x0,y1-y0,z1-z0,1.0});
        const double margin=4*span+std::abs(signedDistance(originalTarget,expected.Location()));
        const auto seed=BRepPrimAPI_MakeBox(gp_Pnt(x0-margin,y0-margin,z0-margin),gp_Pnt(x1+margin,y1+margin,z1+margin)).Shape();
        BOPAlgo_Splitter split;
        split.SetNonDestructive(true);split.AddArgument(prepared.solid);
        for(const auto& plane:planes) split.AddTool(BRepBuilderAPI_MakeFace(plane).Face());
        split.Perform();
        if(split.HasErrors()) return fail(QStringLiteral("OCCT could not split the support arrangement."));
        if(count(split.Shape(),TopAbs_SOLID)>32) return fail(QStringLiteral("more than 32 material cells."));
        std::vector<TopoDS_Shape> cells;
        double sourceVolume=0,candidateVolume=0;
        for(TopExp_Explorer cell(split.Shape(),TopAbs_SOLID);cell.More();cell.Next()) {
            sourceVolume+=mass(cell.Current());cells.push_back(cell.Current());
        }
        // A plane through existing material is an artificial partition, not a
        // selected boundary. Eliminate these partitions BEFORE substitution.
        bool progress=true;
        while(progress) {
            progress=false;
            for(size_t i=0;i<cells.size()&&!progress;++i) for(size_t j=i+1;j<cells.size();++j) {
                if(!sharedTarget(cells[i],cells[j],originalTarget)) continue;
                BRepAlgoAPI_Fuse fuse;fuse.SetArguments(singleton(cells[i]));fuse.SetTools(singleton(cells[j]));
                fuse.SetNonDestructive(true);fuse.Build();
                if(!fuse.IsDone()||fuse.HasErrors()) return fail(QStringLiteral("source cell merging failed."));
                const double expectedVolume=mass(cells[i])+mass(cells[j]);
                if(std::abs(mass(fuse.Shape())-expectedVolume)>1.e-8*std::max(1.0,expectedVolume))
                    return fail(QStringLiteral("source cell union changed material volume."));
                ShapeUpgrade_UnifySameDomain unify(fuse.Shape(),true,true,false);unify.SetSafeInputMode(true);unify.Build();
                std::vector<int> constraints;
                if(!cellConstraints(unify.Shape(),planes,constraints)) continue;
                if(!equivalent(fuse.Shape(),unify.Shape())) return fail(QStringLiteral("source cell merging changed material."));
                cells[i]=unify.Shape();cells.erase(cells.begin()+j);progress=true;break;
            }
        }
        for(size_t i=0;i<cells.size();++i) for(size_t j=i+1;j<cells.size();++j)
            if(sharedTarget(cells[i],cells[j],originalTarget)) return fail(QStringLiteral("nonconvex internal target partition is ambiguous."));
        TopoDS_Shape united;
        std::vector<std::vector<int>> materialRules;
        for(const auto& cell:cells) {
            std::vector<int> sides;
            if(!cellConstraints(cell,planes,sides)) return fail(QStringLiteral("source cell is not certified convex."));
            if(sides[targetIndex]) {
                for(size_t i=0;i<planes.size();++i) if(trims[i]) {
                    // The extra constraints must be redundant BEFORE editing:
                    // no part of a certified convex source cell may be removed.
                    for(TopExp_Explorer vertex(cell,TopAbs_VERTEX);vertex.More();vertex.Next())
                        if(trims[i]*signedDistance(planes[i],BRep_Tool::Pnt(TopoDS::Vertex(vertex.Current())))<-1.e-5)
                            return fail(QStringLiteral("target material cell crosses a physical neighbor trim; decomposition is ambiguous."));
                    if(sides[i]&&sides[i]!=trims[i])
                        return fail(QStringLiteral("target material cell has conflicting neighbor trims."));
                    sides[i]=trims[i];
                }
            }
            materialRules.push_back(sides);
            TopoDS_Shape rebuilt=seed;
            for(size_t i=0;i<planes.size();++i) {
                if(sides[i]==0) continue;
                const auto& plane=i==targetIndex?replacement:planes[i];
                const auto point=plane.Location().Translated(gp_Vec(plane.Axis().Direction())*sides[i]*span);
                const auto halfspace=BRepPrimAPI_MakeHalfSpace(BRepBuilderAPI_MakeFace(plane).Face(),point).Solid();
                BRepAlgoAPI_Common clip;
                clip.SetArguments(singleton(rebuilt));clip.SetTools(singleton(halfspace));
                clip.SetNonDestructive(true);clip.Build();
                if(!clip.IsDone()||clip.HasErrors()) return fail(QStringLiteral("halfspace intersection failed."));
                rebuilt=clip.Shape();
                if(count(rebuilt,TopAbs_SOLID)==0) break;
            }
            if(count(rebuilt,TopAbs_SOLID)==0) continue; // A certified empty cell disappears.
            const double cellVolume=mass(rebuilt);
            if(cellVolume<=1.e-9) return fail(QStringLiteral("degenerate reconstructed cell."));
            candidateVolume+=cellVolume;
            if(united.IsNull()) united=rebuilt;
            else {
                BRepAlgoAPI_Fuse fuse;fuse.SetArguments(singleton(united));fuse.SetTools(singleton(rebuilt));
                fuse.SetNonDestructive(true);fuse.Build();
                if(!fuse.IsDone()||fuse.HasErrors()) return fail(QStringLiteral("cell union failed."));
                united=fuse.Shape();
            }
        }
        const double before=mass(prepared.solid);
        if(std::abs(sourceVolume-before)>1.e-8*std::max(1.0,before)) return fail(QStringLiteral("split cells do not cover original material."));
        if(united.IsNull()||mass(united)>candidateVolume+1.e-8*std::max(1.0,candidateVolume))
            return fail(QStringLiteral("cell union lost or overlapped intended material."));
        if(!finishImpl(source,united,target,expected,result,error,true)) return false;
        for(TopExp_Explorer edge(result.solid,TopAbs_EDGE);edge.More();edge.Next())
            if(BRep_Tool::Degenerated(TopoDS::Edge(edge.Current()))) {
                result={};return fail(QStringLiteral("degenerate planar result edge."));
            }
        // Check material independently of the returned B-Rep's topology: the
        // intended set is the union of the certified cells' signed halfspaces.
        BRepClass3d_SolidClassifier material(result.solid);
        const auto matches=[&](const gp_Pnt& point) {
            bool expectedInside=false,boundary=false;
            for(const auto& rule:materialRules) {
                bool inside=true;
                for(size_t i=0;i<planes.size();++i) if(rule[i]) {
                    const double distance=rule[i]*signedDistance(i==targetIndex?replacement:planes[i],point);
                    boundary=boundary||std::abs(distance)<1.e-6;
                    inside=inside&&distance>0;
                }
                expectedInside=expectedInside||inside;
            }
            if(boundary) return true;
            material.Perform(point,1.e-7);
            return material.State()==(expectedInside?TopAbs_IN:TopAbs_OUT);
        };
        Bnd_Box finalBounds;BRepBndLib::AddOptimal(result.solid,finalBounds,false,false);
        finalBounds.Get(x0,y0,z0,x1,y1,z1);
        for(int i=0;i<9;++i) for(int j=0;j<9;++j) for(int k=0;k<9;++k) {
            const gp_Pnt point(x0+(i-0.37)*(x1-x0)/7.3,y0+(j-0.37)*(y1-y0)/7.3,z0+(k-0.37)*(z1-z0)/7.3);
            if(!matches(point)) {result={};return fail(QStringLiteral("material distribution disagrees with the requested support substitution."));}
        }
        for(TopExp_Explorer f(result.solid,TopAbs_FACE);f.More();f.Next()) {
            GProp_GProps properties;properties=BRepMeasurement::areaProperties(f.Current());
            if(properties.Mass()<1.e-10) {result={};return fail(QStringLiteral("degenerate result face."));}
            const auto face=TopoDS::Face(f.Current());
            auto normal=BRepAdaptor_Surface(face).Plane().Axis().Direction();
            if(face.Orientation()==TopAbs_REVERSED) normal.Reverse();
            for(double side:{-1.0,1.0})
                if(!matches(properties.CentreOfMass().Translated(gp_Vec(normal)*side*span*1.e-5))) {
                    result={};return fail(QStringLiteral("boundary material disagrees with the intended cells."));
                }
        }
        return true;
    } catch(const Standard_Failure&) {
        return fail(QStringLiteral("OCCT failed; original geometry retained."));
    }
}
