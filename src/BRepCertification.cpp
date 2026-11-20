#include "BRepTiming.h"
#include "BRepCertification.h"
#include "BRepMeasurement.h"
#include "BRepMeasurementPrivate.h"
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BOPAlgo_ArgumentAnalyzer.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Cone.hxx>
#include <gp_Lin.hxx>
#include <Adaptor3d_Curve.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>
#include <limits>

namespace BRepCertification {
namespace {
TopTools_ListOfShape singleton(const TopoDS_Shape& shape) {
    TopTools_ListOfShape list;list.Append(shape);return list;
}
int count(const TopoDS_Shape& shape,TopAbs_ShapeEnum type) {
    TopTools_IndexedMapOfShape map;TopExp::MapShapes(shape,type,map);return map.Extent();
}
template<class Algorithm>
TopoDS_Shape combine(const TopoDS_Shape& a,const TopoDS_Shape& b) {
    BRepTiming::Scope timingScope(typeid(Algorithm).name());
    Algorithm operation;operation.SetArguments(singleton(a));operation.SetTools(singleton(b));
    operation.SetNonDestructive(true);
    BRepTiming::Event booleanEvent(std::string("Boolean.")+typeid(Algorithm).name(),a,b);
    operation.Build();booleanEvent.finish(operation.IsDone()&&!operation.HasErrors()?operation.Shape():TopoDS_Shape{});
    if(!operation.IsDone()||operation.HasErrors()) throw Standard_Failure("Material/region Boolean failed.");
    return operation.Shape();
}
}
bool validBRep(const TopoDS_Shape& shape) {
    BRepTiming::Scope timingScope("BRepCertification.validBRep");
    BRepTiming::Event validityEvent("Validate.BRep",shape);
    const bool valid=!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid();
    validityEvent.finish({},valid?1:0);return valid;
}
bool closedShells(const TopoDS_Shape& shape) {
    BRepTiming::Scope timingScope("BRepCertification.closedShells");
    if(shape.IsNull()) return false;
    for(TopExp_Explorer it(shape,TopAbs_SHELL);it.More();it.Next())
        if(!BRep_Tool::IsClosed(it.Current())) return false;
    return true;
}
bool withinTolerance(const TopoDS_Shape& shape,double limit,double* offending) {
    BRepTiming::Scope timingScope("BRepCertification.withinTolerance");
    if(offending) *offending=0;
    for(const auto type:{TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX})
        for(TopExp_Explorer it(shape,type);it.More();it.Next()) {
            const double t=type==TopAbs_FACE?BRep_Tool::Tolerance(TopoDS::Face(it.Current())):
                type==TopAbs_EDGE?BRep_Tool::Tolerance(TopoDS::Edge(it.Current())):BRep_Tool::Tolerance(TopoDS::Vertex(it.Current()));
            if(!std::isfinite(t)||t>limit) {if(offending) *offending=t;return false;}
        }
    return true;
}
double volume(const TopoDS_Shape& shape) {
    return BRepMeasurement::volumeProperties(shape).Mass();
}
double area(const TopoDS_Shape& shape) {
    return BRepMeasurement::areaProperties(shape).Mass();
}
TopAbs_State occupancy(const TopoDS_Shape& shape,const gp_Pnt& point,double tolerance) {
    BRepClass3d_SolidClassifier classifier(shape,point,tolerance);return classifier.State();
}
bool sameCarrier(const TopoDS_Face& original, const TopoDS_Face& result,
                 const gp_Pln* expected, bool unoriented) {
    const BRepReconstruction::SupportDescriptor originalSupport(original), resultSupport(result);
    const auto& a=originalSupport.surface;
    const auto& b=resultSupport.surface;
    if(a.GetType()!=b.GetType()) return false;
    if(a.GetType()==GeomAbs_Plane) {
        const auto p=expected?*expected:a.Plane(), q=b.Plane();
        auto pn=p.Axis().Direction(), qn=q.Axis().Direction();
        if(original.Orientation()==TopAbs_REVERSED) pn.Reverse();
        if(result.Orientation()==TopAbs_REVERSED) qn.Reverse();
        return (unoriented?pn.IsParallel(qn,1.e-7):pn.IsEqual(qn,1.e-7)) && p.Distance(q.Location())<1.e-5;
    }
    if(a.GetType()==GeomAbs_Cylinder) {
        const auto p=a.Cylinder(),q=b.Cylinder();
        // Surface support is an unoriented axis line. Sweeping a circular trim
        // can reverse its parameter axis without changing the cylinder.
        return p.Axis().Direction().IsParallel(q.Axis().Direction(),1.e-7)
            && gp_Lin(p.Axis()).Distance(q.Location())<1.e-5
            && std::abs(p.Radius()-q.Radius())<1.e-5;
    }
    if(a.GetType()==GeomAbs_Cone) {
        const auto p=a.Cone(),q=b.Cone();
        return p.Axis().Direction().IsParallel(q.Axis().Direction(),1.e-7)
            && p.Apex().Distance(q.Apex())<1.e-5
            && std::abs(std::abs(p.SemiAngle())-std::abs(q.SemiAngle()))<1.e-7;
    }
    return false;
}
bool ellipticExtrusion(const BRepReconstruction::SupportDescriptor& support,EllipticExtrusion& result) {
    const auto& surface=support.surface;
    if(surface.GetType()!=GeomAbs_SurfaceOfExtrusion) return false;
    const auto curve=surface.BasisCurve();
    if(curve.IsNull()||curve->GetType()!=GeomAbs_Ellipse) return false;
    const auto ellipse=curve->Ellipse();const auto axis=surface.Direction();
    if(!axis.IsParallel(ellipse.Axis().Direction(),1.e-8)
       ||!std::isfinite(ellipse.MajorRadius())||!std::isfinite(ellipse.MinorRadius())
       ||ellipse.MinorRadius()<=1.e-7||ellipse.MajorRadius()-ellipse.MinorRadius()<=1.e-7) return false;
    result={ellipse,axis};return true;
}
bool sameEllipticExtrusion(const TopoDS_Face& a,const TopoDS_Face& b) {
    EllipticExtrusion x,y;
    if(!ellipticExtrusion(BRepReconstruction::SupportDescriptor(a),x)
       ||!ellipticExtrusion(BRepReconstruction::SupportDescriptor(b),y)) return false;
    return x.axis.IsParallel(y.axis,1.e-8)
        &&gp_Lin(gp_Ax1(x.section.Location(),x.axis)).Distance(y.section.Location())<1.e-6
        &&x.section.XAxis().Direction().IsParallel(y.section.XAxis().Direction(),1.e-8)
        &&std::abs(x.section.MajorRadius()-y.section.MajorRadius())<1.e-6
        &&std::abs(x.section.MinorRadius()-y.section.MinorRadius())<1.e-6;
}
bool singleSolid(const TopoDS_Shape& shape,double& volume) {
    BRepTiming::Scope timingScope("BRepCertification.singleSolid");
    if(shape.IsNull() || count(shape,TopAbs_SOLID)!=1 || count(shape,TopAbs_SHELL)!=1
       || !validBRep(shape)) return false;
    TopExp_Explorer solid(shape,TopAbs_SOLID);
    if(!closedShells(shape)) return false;
    for(const auto type : {TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX})
        if(count(shape,type)!=count(solid.Current(),type)) return false;
    BRepMeasurement::Result measured;QString error;
    // validBRep above uses the same analyzer options as measurement. Between
    // that check and this lexical handoff only read-only closure/ownership
    // traversals occur. The handoff cannot escape or target another shape.
    const BRepMeasurement::detail::ValidatedInputVolume checked(shape);
    if(!checked.compute(measured,error)) return false;
    volume=measured.properties.Mass();
    if(!std::isfinite(volume)||volume<=1.e-12) return false;
    BRepTiming::Scope selfInterScope("OCCT.selfInterference.copy-and-perform");
    BRepTiming::Event copyEvent("Copy",shape);
    BRepBuilderAPI_Copy isolated(shape,true,false);
    copyEvent.finish(isolated.Shape());BRepTiming::copyOrigin(shape,isolated);
    BOPAlgo_ArgumentAnalyzer check;
    check.SetShape1(isolated.Shape()); check.SelfInterMode()=true;
    check.StopOnFirstFaulty()=true;
    {BRepTiming::Scope performScope("OCCT.selfInterference.Perform");
     BRepTiming::Event selfEvent("Validate.SelfInterference",shape,isolated.Shape());check.Perform();selfEvent.finish();}
    return !check.HasErrors()&&!check.HasFaulty();
}
bool manifold(const TopoDS_Shape& shape) {
    BRepTiming::Scope timingScope("BRepCertification.manifold");
    TopTools_IndexedMapOfShape edges,faces,vertices;
    TopExp::MapShapes(shape,TopAbs_EDGE,edges); TopExp::MapShapes(shape,TopAbs_FACE,faces);
    TopExp::MapShapes(shape,TopAbs_VERTEX,vertices);
    std::vector<std::vector<int>> uses(edges.Extent()+1);
    for(int f=1;f<=faces.Extent();++f)
        for(TopExp_Explorer e(faces(f),TopAbs_EDGE);e.More();e.Next())
            uses[edges.FindIndex(e.Current())].push_back(f);
    for(int e=1;e<=edges.Extent();++e)
        if(BRep_Tool::Degenerated(TopoDS::Edge(edges(e))) || uses[e].size()!=2) return false;
    for(int v=1;v<=vertices.Extent();++v) {
        std::vector<int> incident;
        std::vector<std::pair<int,int>> links;
        for(int e=1;e<=edges.Extent();++e) {
            bool touches=false;
            for(TopExp_Explorer it(edges(e),TopAbs_VERTEX);it.More();it.Next())
                touches=touches||it.Current().IsSame(vertices(v));
            if(touches) { links.emplace_back(uses[e][0],uses[e][1]);
                incident.push_back(uses[e][0]); incident.push_back(uses[e][1]); }
        }
        if(incident.empty()) return false;
        std::vector<bool> reached(faces.Extent()+1,false); reached[incident.front()]=true;
        bool changed=true;
        while(changed) { changed=false; for(const auto& [a,b]:links)
            if(reached[a]!=reached[b]) { reached[a]=reached[b]=true; changed=true; } }
        for(int f:incident) if(!reached[f]) return false;
    }
    return true;
}
bool equivalent(const TopoDS_Shape& a,const TopoDS_Shape& b) {
    BRepTiming::Scope timingScope("BRepCertification.equivalent");
    if(a.IsNull()||b.IsNull()) return false;
    BRepTiming::Event copyA("Copy",a);BRepBuilderAPI_Copy ac(a,true,false);
    copyA.finish(ac.Shape());BRepTiming::copyOrigin(a,ac);
    BRepTiming::Event copyB("Copy",b);BRepBuilderAPI_Copy bc(b,true,false);
    copyB.finish(bc.Shape());BRepTiming::copyOrigin(b,bc);
    BRepAlgoAPI_Common common;
    common.SetArguments(singleton(ac.Shape()));
    common.SetTools(singleton(bc.Shape()));
    common.SetNonDestructive(true);
    {BRepTiming::Scope commonScope("OCCT.materialCommon.Build");
     BRepTiming::Event booleanEvent("Boolean.MaterialCommon",ac.Shape(),bc.Shape());common.Build();booleanEvent.finish(common.IsDone()&&!common.HasErrors()?common.Shape():TopoDS_Shape{});}
    if(!common.IsDone()||common.HasErrors()) return false;
    BRepMeasurement::Result am,bm,cm;QString error;
    if(!BRepMeasurement::volume(a,am,error)||!BRepMeasurement::volume(b,bm,error)
       ||!BRepMeasurement::volume(common.Shape(),cm,error)) return false;
    const double av=am.properties.Mass(),bv=bm.properties.Mass(),cv=cm.properties.Mass();
    const double tolerance=1.e-8*std::max({1.0,std::abs(av),std::abs(bv)});
    return std::isfinite(cv)&&std::abs(av-bv)<=tolerance
        &&std::abs(av-cv)<=tolerance&&std::abs(bv-cv)<=tolerance;
}

bool difference(const TopoDS_Shape& before,const TopoDS_Shape& after,
                MaterialDifference& result,QString& error) {
    BRepTiming::Scope timingScope("BRepCertification.difference");
    result={};error.clear();
    if(before.IsNull()||after.IsNull()) {error=QStringLiteral("Material difference requires non-null sets.");return false;}
    try {
        BRepTiming::Event copyA("Copy",before);BRepBuilderAPI_Copy a(before,true,false);
        copyA.finish(a.Shape());BRepTiming::copyOrigin(before,a);
        BRepTiming::Event copyB("Copy",after);BRepBuilderAPI_Copy b(after,true,false);
        copyB.finish(b.Shape());BRepTiming::copyOrigin(after,b);
        MaterialDifference candidate;
        candidate.removed=combine<BRepAlgoAPI_Cut>(a.Shape(),b.Shape());
        candidate.added=combine<BRepAlgoAPI_Cut>(b.Shape(),a.Shape());
        result=std::move(candidate);return true;
    } catch(const Standard_Failure&) {error=QStringLiteral("OCCT could not certify the material difference.");return false;}
      catch(const std::exception&) {error=QStringLiteral("Material difference Boolean failed.");return false;}
}
bool preservedOutside(const TopoDS_Shape& before,const TopoDS_Shape& after,const TopoDS_Shape& region) {
    BRepTiming::Scope timingScope("BRepCertification.preservedOutside");
    const auto oldOutside=combine<BRepAlgoAPI_Cut>(before,region);
    const auto newOutside=combine<BRepAlgoAPI_Cut>(after,region);
    return equivalent(oldOutside,newOutside);
}
namespace {
bool sameSplineBasis(const BRepReconstruction::InvariantSplineSupport& a,
                     const BRepReconstruction::InvariantSplineSupport& b) {
    if(!a.axis.IsParallel(b.axis,1.e-8)||a.basis->Degree()!=b.basis->Degree()
       ||a.basis->NbPoles()!=b.basis->NbPoles()||a.basis->NbKnots()!=b.basis->NbKnots()) return false;
    for(bool reverse:{false,true}) {
        auto candidate=Handle(Geom_BSplineCurve)::DownCast(b.basis->Copy());if(reverse) candidate->Reverse();
        bool match=true;
        const double af=a.basis->Knot(1),as=a.basis->Knot(a.basis->NbKnots())-af;
        const double bf=candidate->Knot(1),bs=candidate->Knot(candidate->NbKnots())-bf;
        for(int i=1;i<=a.basis->NbKnots();++i)
            if(a.basis->Multiplicity(i)!=candidate->Multiplicity(i)
               ||std::abs((a.basis->Knot(i)-af)/as-(candidate->Knot(i)-bf)/bs)>64*std::numeric_limits<double>::epsilon()) match=false;
        for(int i=1;i<=a.basis->NbPoles();++i) {
            auto delta=gp_Vec(a.basis->Pole(i),candidate->Pole(i));
            delta-=gp_Vec(a.axis)*delta.Dot(gp_Vec(a.axis));
            if(delta.Magnitude()>1.e-7) match=false;
        }
        if(match) return true;
    }
    return false;
}
struct CarrierMatcher {
    bool allowSpline=false;
    std::vector<BRepReconstruction::InvariantSplineSupport> proofs;
    void add(const BRepReconstruction::TopologyIndex& topology) {
        if(!allowSpline) return;
        for(const auto& record:topology.faces()) {
            if(sameCarrier(record.support.face,record.support.face)) continue;
            BRepReconstruction::InvariantSplineSupport proof;QString error;
            if(!record.support.invariantSpline(proof,error,&topology)) throw Standard_Failure(error.toUtf8().constData());
            if(!proofs.empty()&&!sameSplineBasis(proofs.front(),proof))
                throw Standard_Failure("Spline carrier equivalence is unsupported: one directly comparable basis family is required; knot insertion, degree elevation and unrelated families need a later proof.");
            proofs.push_back(std::move(proof));
        }
    }
    bool spline(const TopoDS_Face& face) const {
        return std::any_of(proofs.begin(),proofs.end(),[&](const auto& proof){return proof.face.IsEqual(face);});
    }
    bool matches(const TopoDS_Face& a,const TopoDS_Face& b,bool unoriented=false) const {
        return sameCarrier(a,b,nullptr,unoriented)||(allowSpline&&spline(a)&&spline(b));
    }
};
// Owned by exactly one synchronous correspondence invocation. The caller's
// current oriented Faces and their trims remain immutable for that invocation;
// carrier matches or successful material equivalence never define a key.
struct OverlapTable {
    struct Entry {TopoDS_Face first,second;double area;};
    std::vector<Entry> entries;
};
double regionOverlap(const TopoDS_Face& a,const TopoDS_Face& b,const CarrierMatcher& matcher,
                     OverlapTable* overlaps=nullptr) {
    BRepTiming::Scope timingScope("BRepCertification.regionOverlap");
    if(overlaps) for(const auto& entry:overlaps->entries)
        if(entry.first.IsEqual(a)&&entry.second.IsEqual(b)) {
            BRepTiming::Scope reused("BRepCertification.overlapReused");return entry.area;
        }
    double measured=0;
    if(matcher.allowSpline) {
        // Non-destructive Common can still change OCCT ownership/cache flags
        // on supplied Faces. New opt-in correspondence isolates both operands;
        // mappings continue to refer to the caller's actual topology.
        BRepTiming::Event copyA("Copy",a);BRepBuilderAPI_Copy ac(a,true,false);
        copyA.finish(ac.Shape());BRepTiming::copyOrigin(a,ac);
        BRepTiming::Event copyB("Copy",b);BRepBuilderAPI_Copy bc(b,true,false);
        copyB.finish(bc.Shape());BRepTiming::copyOrigin(b,bc);
        measured=area(combine<BRepAlgoAPI_Common>(ac.Shape(),bc.Shape()));
    } else measured=area(combine<BRepAlgoAPI_Common>(a,b));
    // Publish only after both Boolean and certified measurement succeeded.
    // A thrown failure is never installed as a default/empty intersection.
    if(overlaps) overlaps->entries.push_back({a,b,measured});
    return measured;
}
FaceCoverage coverageImpl(const TopoDS_Face& face,const BRepReconstruction::TopologyIndex& result,const CarrierMatcher& matcher,
                          OverlapTable* overlaps=nullptr) {
    BRepTiming::Scope timingScope("BRepCertification.coverageImpl");
    FaceCoverage coverage;
    for(const auto& record:result.faces()) {
        const auto& candidate=record.support.face;
        if(!matcher.matches(face,candidate)) continue;
        coverage.carriers.push_back(candidate);
        const double overlap=regionOverlap(face,candidate,matcher,overlaps);
        coverage.coveredArea+=overlap;
        if(overlap>1.e-12) {coverage.overlaps.push_back(candidate);coverage.positiveArea+=overlap;}
    }
    return coverage;
}
bool sameRegion(const TopoDS_Face& a,const TopoDS_Face& b,const CarrierMatcher& matcher,
                OverlapTable* overlaps=nullptr) {
    BRepTiming::Scope timingScope("BRepCertification.sameRegion");
    if(!matcher.matches(a,b)) return false;
    const double overlap=regionOverlap(a,b,matcher,overlaps);
    return areaMatches(area(a),overlap,1.e-8)&&areaMatches(area(b),overlap,1.e-8);
}
}
FaceCoverage coverage(const TopoDS_Face& face,const BRepReconstruction::TopologyIndex& result) {
    return coverageImpl(face,result,CarrierMatcher{});
}
bool areaMatches(double expected,double actual,double tolerance) {
    return std::isfinite(expected)&&std::isfinite(actual)
        &&std::abs(actual-expected)<=tolerance*std::max(1.,expected);
}
bool preservedFace(const TopoDS_Face& face,const BRepReconstruction::TopologyIndex& result,double tolerance) {
    return areaMatches(area(face),coverage(face,result).coveredArea,tolerance);
}
bool sameTrimmedRegion(const TopoDS_Face& a,const TopoDS_Face& b) {
    return sameRegion(a,b,CarrierMatcher{});
}
namespace {
void requireUnambiguous(const BRepReconstruction::TopologyIndex& topology,const CarrierMatcher& matcher,
                        OverlapTable& overlaps) {
    const auto& faces=topology.faces();
    for(size_t i=0;i<faces.size();++i) {
        const auto& a=faces[i].support.face;
        if(!matcher.matches(a,a)) throw Standard_Failure("Unsupported correspondence carrier.");
        if(!std::isfinite(area(a))||area(a)<=1.e-12) throw Standard_Failure("Degenerate correspondence region.");
        for(size_t j=i+1;j<faces.size();++j) {
            const auto& b=faces[j].support.face;
            if(matcher.matches(a,b,true)&&regionOverlap(a,b,matcher,&overlaps)>1.e-12)
                throw Standard_Failure("Overlapping face regions make correspondence ambiguous.");
        }
    }
}
}
static bool correspondImpl(const TopoDS_Shape& source,const TopoDS_Shape& result,Correspondence& mapping,QString& error,bool allowSpline) {
    BRepTiming::Scope timingScope("BRepCertification.correspondImpl");
    mapping={};error.clear();
    try {
        BRepReconstruction::TopologyIndex from,to;
        if(!BRepReconstruction::TopologyIndex::build(source,from,error)
           ||!BRepReconstruction::TopologyIndex::build(result,to,error)) return false;
        if(from.faces().empty()||to.faces().empty()) {
            error=QStringLiteral("Face correspondence requires nonempty face sets.");return false;
        }
        CarrierMatcher matcher;matcher.allowSpline=allowSpline;matcher.add(from);matcher.add(to);
        OverlapTable overlaps;
        requireUnambiguous(from,matcher,overlaps);requireUnambiguous(to,matcher,overlaps);
        Correspondence candidate;
        std::vector<int> owners(to.faces().size(),0);
        std::vector<double> targetCoverage(to.faces().size(),0);
        for(const auto& record:from.faces()) {
            FaceCorrespondence relation;relation.source=record.support.face;
            relation.regions=coverageImpl(relation.source,to,matcher,&overlaps);
            for(size_t j=0;j<to.faces().size();++j) {
                const auto& target=to.faces()[j].support.face;
                if(std::any_of(relation.regions.overlaps.begin(),relation.regions.overlaps.end(),[&](const auto& f){return f.IsSame(target);})) {
                    ++owners[j];
                    targetCoverage[j]+=regionOverlap(relation.source,target,matcher,&overlaps);
                }
            }
            candidate.faces.push_back(std::move(relation));
        }
        for(size_t j=0;j<owners.size();++j) if(!owners[j]) candidate.newFaces.push_back(to.faces()[j].support.face);
        for(auto& relation:candidate.faces) {
            const auto& matches=relation.regions.overlaps;
            if(matches.empty()) continue;
            relation.relation=FaceRelation::Modified;
            if(matches.size()==1&&sameRegion(relation.source,matches.front(),matcher,&overlaps)) relation.relation=FaceRelation::SameRegion;
            const bool complete=areaMatches(area(relation.source),relation.regions.coveredArea,1.e-8);
            for(size_t j=0;j<owners.size();++j) {
                const auto& target=to.faces()[j].support.face;
                if(std::none_of(matches.begin(),matches.end(),[&](const auto& f){return f.IsSame(target);})) continue;
                if(matches.size()>1) {
                    if(!complete||owners[j]!=1||!areaMatches(area(target),targetCoverage[j],1.e-8))
                        throw Standard_Failure("Ambiguous split or many-to-many face repartition.");
                    relation.relation=FaceRelation::Split;
                } else if(owners[j]>1) {
                    if(!complete||!areaMatches(area(target),targetCoverage[j],1.e-8))
                        throw Standard_Failure("Ambiguous partial face merge.");
                    relation.relation=FaceRelation::Merged;
                }
            }
        }
        mapping=std::move(candidate);return true;
    } catch(const Standard_Failure& failure) {error=QString::fromUtf8(failure.GetMessageString());return false;}
      catch(const std::exception&) {error=QStringLiteral("OCCT face correspondence could not be certified.");return false;}
}

bool correspond(const TopoDS_Shape& source,const TopoDS_Shape& result,Correspondence& mapping,QString& error) {
    return correspondImpl(source,result,mapping,error,false);
}
bool correspondInvariantSpline(const TopoDS_Shape& source,const TopoDS_Shape& result,Correspondence& mapping,QString& error) {
    return correspondImpl(source,result,mapping,error,true);
}

}
