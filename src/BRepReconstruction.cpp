#include "BRepReconstruction.h"
#include "BooleanGeometry.h"
#include <BRepAdaptor_Surface.hxx>
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
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <BOPAlgo_ArgumentAnalyzer.hxx>
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
#include <cmath>

namespace {
TopTools_ListOfShape singleton(const TopoDS_Shape& shape) {
    TopTools_ListOfShape list;list.Append(shape);return list;
}
int count(const TopoDS_Shape& shape,TopAbs_ShapeEnum type) {
    TopTools_IndexedMapOfShape map; TopExp::MapShapes(shape,type,map); return map.Extent();
}
bool sameSupport(const TopoDS_Face& original, const TopoDS_Face& result,
                 const gp_Pln* expected = nullptr, bool unoriented = false) {
    const BRepAdaptor_Surface a(original), b(result);
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
}
bool BRepReconstruction::validSolid(const TopoDS_Shape& shape,double& volume) {
    if(shape.IsNull() || count(shape,TopAbs_SOLID)!=1 || count(shape,TopAbs_SHELL)!=1
       || !BRepCheck_Analyzer(shape).IsValid()) return false;
    TopExp_Explorer solid(shape,TopAbs_SOLID),shell(shape,TopAbs_SHELL);
    if(!BRep_Tool::IsClosed(shell.Current())) return false;
    for(const auto type : {TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX})
        if(count(shape,type)!=count(solid.Current(),type)) return false;
    GProp_GProps properties; BRepGProp::VolumeProperties(shape,properties);
    volume=properties.Mass();
    if(!std::isfinite(volume)||volume<=1.e-12) return false;
    BRepBuilderAPI_Copy isolated(shape,true,false);
    BOPAlgo_ArgumentAnalyzer check;
    check.SetShape1(isolated.Shape()); check.SelfInterMode()=true;
    check.StopOnFirstFaulty()=true; check.Perform();
    return !check.HasErrors()&&!check.HasFaulty();
}
bool BRepReconstruction::sameAnalyticSupport(const TopoDS_Face& a,const TopoDS_Face& b) {
    return sameSupport(a,b);
}
bool BRepReconstruction::neighborhood(const TopoDS_Shape& source,const TopoDS_Face& target,
                                      Neighborhood& result,QString& error) {
    result={};error.clear();
    const auto fail=[&](const QString& reason){result={};error=reason;return false;};
    try {
        TopTools_IndexedMapOfShape faces;TopExp::MapShapes(source,TopAbs_FACE,faces);
        if(target.IsNull()||!faces.Contains(target)||faces.Extent()>64||count(source,TopAbs_EDGE)>256||count(source,TopAbs_VERTEX)>256)
            return fail(QStringLiteral("Select a member face within the local reconstruction complexity budget."));
        BRepBuilderAPI_Copy copy(source,true,false);result.solid=copy.Shape();
        double volume=0;if(!validSolid(result.solid,volume)) return fail(QStringLiteral("Neighborhood needs one valid closed solid and shell."));
        for(const auto type:{TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX}) for(TopExp_Explorer it(result.solid,type);it.More();it.Next()) {
            const double t=type==TopAbs_FACE?BRep_Tool::Tolerance(TopoDS::Face(it.Current())):
                type==TopAbs_EDGE?BRep_Tool::Tolerance(TopoDS::Edge(it.Current())):BRep_Tool::Tolerance(TopoDS::Vertex(it.Current()));
            if(!std::isfinite(t)||t>1.e-6) return fail(QStringLiteral("Source tolerance %1 exceeds the local reconstruction limit (1e-6).").arg(t,0,'g',15));
        }
        for(int i=1;i<=faces.Extent();++i) {
            auto copied=TopoDS::Face(copy.ModifiedShape(faces(i)));copied.Orientation(faces(i).Orientation());
            result.history.push_back({TopoDS::Face(faces(i)),{copied}});
            if(faces(i).IsSame(target)) result.target=copied;
        }
        TopTools_IndexedDataMapOfShapeListOfShape adjacency;
        TopExp::MapShapesAndAncestors(result.solid,TopAbs_EDGE,TopAbs_FACE,adjacency);
        TopTools_IndexedMapOfShape patch,neighbors;patch.Add(result.target);
        for(int i=1;i<=patch.Extent();++i) for(TopExp_Explorer edge(patch(i),TopAbs_EDGE);edge.More();edge.Next())
            for(TopTools_ListIteratorOfListOfShape it(adjacency.FindFromKey(edge.Current()));it.More();it.Next())
                if(sameAnalyticSupport(result.target,TopoDS::Face(it.Value()))) patch.Add(it.Value());
        for(int i=1;i<=adjacency.Extent();++i) {
            TopTools_IndexedMapOfShape inside,outside;
            for(TopTools_ListIteratorOfListOfShape it(adjacency.FindFromIndex(i));it.More();it.Next())
                (patch.Contains(it.Value())?inside:outside).Add(it.Value());
            if(inside.IsEmpty()||outside.IsEmpty()) continue; // internal patch edges and periodic seams
            if(inside.Extent()!=1||outside.Extent()!=1) return fail(QStringLiteral("Selected patch has ambiguous boundary adjacency."));
            result.boundary.push_back(TopoDS::Edge(adjacency.FindKey(i)));neighbors.Add(outside(1));
        }
        for(int i=1;i<=patch.Extent();++i) result.patch.push_back(TopoDS::Face(patch(i)));
        for(int i=1;i<=neighbors.Extent();++i) result.neighbors.push_back(TopoDS::Face(neighbors(i)));
        return true;
    } catch(const Standard_Failure&) {return fail(QStringLiteral("OCCT could not safely analyze the selected neighborhood."));}
}

bool BRepReconstruction::finishLocal(const Neighborhood& source,const TopoDS_Shape& candidate,
        const TopoDS_Shape& editRegion,const std::vector<TopoDS_Face>& affected,
        const std::vector<TopoDS_Face>& moved,const gp_Trsf& displacement,Result& result,QString& error) {
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
        TopoDS_Shape normalized;if(!normalize(candidate,normalized,error,true)) return false;
        const auto oldOutside=BooleanGeometry::combine<BRepAlgoAPI_Cut>(source.solid,editRegion);
        const auto newOutside=BooleanGeometry::combine<BRepAlgoAPI_Cut>(normalized,editRegion);
        if(!equivalent(oldOutside,newOutside)) return fail(QStringLiteral("Local reconstruction changed material outside its certified edit region."));
        for(const auto& mapping:source.history) {
            if(mapping.modified.size()!=1||!members.Contains(mapping.modified.front())) return fail(QStringLiteral("Invalid neighborhood copy correspondence."));
            const auto copied=mapping.modified.front();
            const auto expected=movedMap.Contains(copied)?TopoDS::Face(BRepBuilderAPI_Transform(copied,displacement,true).Shape()):copied;
            FaceMapping final{mapping.original,{}};double covered=0;
            for(TopExp_Explorer it(normalized,TopAbs_FACE);it.More();it.Next()) {
                const auto face=TopoDS::Face(it.Current());
                if(!sameAnalyticSupport(expected,face)) continue;
                GProp_GProps overlap;BRepGProp::SurfaceProperties(BooleanGeometry::combine<BRepAlgoAPI_Common>(expected,face),overlap);
                if(overlap.Mass()>1.e-12) {final.modified.push_back(face);covered+=overlap.Mass();}
            }
            if(!affectedMap.Contains(copied)) {
                GProp_GProps area;BRepGProp::SurfaceProperties(copied,area);
                if(std::abs(covered-area.Mass())>1.e-8*std::max(1.,area.Mass()))
                    return fail(QStringLiteral("An unaffected trimmed face region changed during local reconstruction."));
            }
            if(copied.IsSame(source.target)&&final.modified.size()==1) result.target=final.modified.front();
            result.history.push_back(std::move(final));
        }
        result.solid=normalized;return true;
    } catch(const Standard_Failure&) {return fail(QStringLiteral("OCCT could not certify local reconstruction and face correspondence."));}
      catch(const std::exception&) {return fail(QStringLiteral("Boolean local-material certification failed."));}
}
bool BRepReconstruction::movePlanarLocal(const TopoDS_Shape& source,const TopoDS_Face& target,
                                        double distance,Result& result,QString& error) {
    result={};error.clear();
    const auto fail=[&](const QString& reason){result={};error=QStringLiteral("Local planar reconstruction refused: ")+reason;return false;};
    try {
        if(!std::isfinite(distance)||std::abs(distance)<1.e-7) return fail(QStringLiteral("requires a finite nonzero offset."));
        Neighborhood local;
        if(!neighborhood(source,target,local,error)) return false;
        if(BRepAdaptor_Surface(local.target).GetType()!=GeomAbs_Plane)
            return fail(QStringLiteral("requires a connected planar support patch."));
        auto normal=BRepAdaptor_Surface(local.target).Plane().Axis().Direction();
        if(local.target.Orientation()==TopAbs_REVERSED) normal.Reverse();
        // These adjacent supports retain exactly the same cross section during
        // the sweep. Sloped/curved intersections need another reconstruction.
        for(const auto& face:local.neighbors) {
            const BRepAdaptor_Surface surface(face);
            const bool invariant=surface.GetType()==GeomAbs_Plane
                ? std::abs(surface.Plane().Axis().Direction().Dot(normal))<1.e-8
                : surface.GetType()==GeomAbs_Cylinder&&surface.Cylinder().Axis().Direction().IsParallel(normal,1.e-8);
            if(!invariant) return fail(QStringLiteral("an adjacent support changes cross section along the requested direction."));
        }
        const gp_Vec delta=gp_Vec(normal)*distance;gp_Trsf shift;shift.SetTranslation(delta);
        TopoDS_Shape region;
        for(const auto& patch:local.patch) {
            const auto swept=BRepPrimAPI_MakePrism(patch,delta,true,true).Shape();
            region=region.IsNull()?swept:BooleanGeometry::combine<BRepAlgoAPI_Fuse>(region,swept);
        }
        double regionVolume=0;
        if(!validSolid(region,regionVolume)) return fail(QStringLiteral("the trimmed-face sweep is not a valid closed volume."));
        const auto overlap=BooleanGeometry::combine<BRepAlgoAPI_Common>(local.solid,region);
        GProp_GProps common;BRepGProp::VolumeProperties(overlap,common);
        if(distance>0 ? std::abs(common.Mass())>1.e-8*std::max(1.,regionVolume) : !equivalent(overlap,region))
            return fail(QStringLiteral("the sweep meets other material or crosses a material boundary; contact-changing edits need a separate certificate."));
        const auto candidate=distance>0?BooleanGeometry::combine<BRepAlgoAPI_Fuse>(local.solid,region)
                                       :BooleanGeometry::combine<BRepAlgoAPI_Cut>(local.solid,region);
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
                GProp_GProps overlapArea;BRepGProp::SurfaceProperties(BooleanGeometry::combine<BRepAlgoAPI_Common>(face,region),overlapArea);
                if(overlapArea.Mass()>1.e-12) affected.push_back(face);
            }
        }
        Result finished;
        if(!finishLocal(local,candidate,region,affected,local.patch,shift,finished,error)) return false;
        std::vector<TopoDS_Face> expectedPatch;
        for(const auto& patch:local.patch) expectedPatch.push_back(TopoDS::Face(BRepBuilderAPI_Transform(patch,shift,true).Shape()));
        const auto expected=expectedPatch.front();
        const auto sameBoundary=[&](const TopoDS_Face& a,const TopoDS_Face& b) {
            if(!sameAnalyticSupport(a,b)) return false;
            if(BRepAdaptor_Surface(a).GetType()!=GeomAbs_Cylinder) return true;
            const auto side=[](const TopoDS_Face& face) {
                const BRepAdaptor_Surface s(face);gp_Pnt p;gp_Vec du,dv;
                s.D1((s.FirstUParameter()+s.LastUParameter())/2,(s.FirstVParameter()+s.LastVParameter())/2,p,du,dv);
                auto n=du.Crossed(dv);if(face.Orientation()==TopAbs_REVERSED) n.Reverse();
                auto radial=gp_Vec(s.Cylinder().Location(),p);const gp_Vec axis(s.Cylinder().Axis().Direction());radial-=axis*radial.Dot(axis);
                return n.Dot(radial);
            };
            // Extending past a remote cap must not invent an oppositely
            // oriented cylindrical cavity on the same geometric support.
            return side(a)*side(b)>0;
        };
        double covered=0,targetArea=0;for(const auto& patch:expectedPatch) {GProp_GProps area;BRepGProp::SurfaceProperties(patch,area);targetArea+=area.Mass();}
        for(TopExp_Explorer it(finished.solid,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());bool allowed=sameBoundary(expected,face);
            if(allowed) {
                for(const auto& patch:expectedPatch) {GProp_GProps area;BRepGProp::SurfaceProperties(BooleanGeometry::combine<BRepAlgoAPI_Common>(patch,face),area);covered+=area.Mass();}
            }
            for(TopExp_Explorer old(local.solid,TopAbs_FACE);old.More()&&!allowed;old.Next())
                if(std::none_of(local.patch.begin(),local.patch.end(),[&](const auto& patch){return patch.IsSame(old.Current());})) allowed=sameBoundary(TopoDS::Face(old.Current()),face);
            if(!allowed) {
                const BRepAdaptor_Surface s(face);GProp_GProps p;BRepGProp::SurfaceProperties(face,p);
                return fail(QStringLiteral("the result introduced an unintended transition surface (type %1, center %2, %3, %4).")
                    .arg(int(s.GetType())).arg(p.CentreOfMass().X()).arg(p.CentreOfMass().Y()).arg(p.CentreOfMass().Z()));
            }
        }
        if(std::abs(covered-targetArea)>1.e-8*std::max(1.,targetArea))
            return fail(QStringLiteral("the complete translated target patch did not survive."));
        result=std::move(finished);error.clear();return true;
    } catch(const Standard_Failure&) {return fail(QStringLiteral("OCCT could not reconstruct this neighborhood safely."));}
      catch(const std::exception&) {return fail(QStringLiteral("local material certification failed."));}
}

bool BRepReconstruction::normalize(const TopoDS_Shape& candidate,TopoDS_Shape& result,QString& error,bool allowValidSplits) {
    result.Nullify();
    try {
        double volume=0;
        if(!validSolid(candidate,volume)) {
            error=QStringLiteral("Reconstruction did not produce one valid closed solid.");return false;
        }
        try {
            BRepBuilderAPI_Copy isolated(candidate,true,false);
            auto cleaned=isolated.Shape();
            BRepTools::RemoveInternals(cleaned,false);
            ShapeUpgrade_UnifySameDomain unify(cleaned,true,true,false);
            if(allowValidSplits) {
                // Preserve cylindrical trims during optional cleanup. Merging
                // bands with different axial extents can throw or loop inside
                // OCCT's same-domain unifier. Planar seams may still merge.
                for(TopExp_Explorer f(cleaned,TopAbs_FACE);f.More();f.Next())
                    if(BRepAdaptor_Surface(TopoDS::Face(f.Current())).GetType()==GeomAbs_Cylinder)
                        for(TopExp_Explorer e(f.Current(),TopAbs_EDGE);e.More();e.Next()) unify.KeepShape(e.Current());
            }
            unify.SetSafeInputMode(true);unify.Build();
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
            BRepBuilderAPI_Copy splitCopy(candidate,true,false);
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
    result={};
    error.clear();
    try {
        BRepBuilderAPI_Copy copy(source,true,false);
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
        BRepGProp::VolumeProperties(source,before);
        BRepGProp::VolumeProperties(candidate,after);
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
    GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties);return properties.Mass();
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
    GProp_GProps properties;BRepGProp::VolumeProperties(cell,properties);
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
            GProp_GProps area;BRepGProp::SurfaceProperties(common.Shape(),area);
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
    if(a.IsNull()||b.IsNull()) return false;
    BRepBuilderAPI_Copy ac(a,true,false),bc(b,true,false);
    BRepAlgoAPI_Common common;
    common.SetArguments(singleton(ac.Shape()));
    common.SetTools(singleton(bc.Shape()));
    common.SetNonDestructive(true);common.Build();
    if(!common.IsDone()||common.HasErrors()) return false;
    const double av=mass(a),bv=mass(b),cv=mass(common.Shape());
    const double tolerance=1.e-8*std::max({1.0,std::abs(av),std::abs(bv)});
    return std::isfinite(cv)&&std::abs(av-bv)<=tolerance
        &&std::abs(av-cv)<=tolerance&&std::abs(bv-cv)<=tolerance;
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
            GProp_GProps properties;BRepGProp::SurfaceProperties(f.Current(),properties);
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
