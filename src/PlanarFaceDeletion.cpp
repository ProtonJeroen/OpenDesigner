#include "PlanarFaceDeletion.h"
#include <BOPAlgo_Splitter.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_MapOfShape.hxx>
#include <TopoDS.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>
#include <optional>

namespace {
constexpr double tolerance=1.e-7;
struct Bound {gp_Pln plane;gp_Dir outward;};
struct Cell {TopoDS_Shape source;std::vector<Bound> bounds;};
bool samePlane(const gp_Pln& a,const gp_Pln& b) {
    return a.Axis().Direction().IsParallel(b.Axis().Direction(),1.e-8)&&a.Distance(b.Location())<tolerance;
}
double area(const TopoDS_Shape& shape) {GProp_GProps p;BRepGProp::SurfaceProperties(shape,p);return p.Mass();}
Bound support(const TopoDS_Face& f) {
    const auto plane=BRepAdaptor_Surface(f).Plane();auto n=plane.Axis().Direction();
    if(f.Orientation()==TopAbs_REVERSED) n.Reverse();return {plane,n};
}
template<class Operation>
TopoDS_Shape combine(const TopoDS_Shape& a,const TopoDS_Shape& b) {
    Operation op;TopTools_ListOfShape args,tools;args.Append(a);tools.Append(b);
    op.SetArguments(args);op.SetTools(tools);op.SetNonDestructive(true);op.Build();
    if(!op.IsDone()||op.HasErrors()) throw Standard_Failure("Planar step intersection failed.");
    return op.Shape();
}
double overlap(const TopoDS_Shape& cell,const TopoDS_Face& patch) {
    double sum=0;const auto plane=BRepAdaptor_Surface(patch).Plane();
    for(TopExp_Explorer f(cell,TopAbs_FACE);f.More();f.Next())
        if(samePlane(plane,BRepAdaptor_Surface(TopoDS::Face(f.Current())).Plane()))
            sum+=area(combine<BRepAlgoAPI_Common>(f.Current(),patch));
    return sum;
}
bool addBound(std::vector<Bound>& bounds,const Bound& next) {
    for(const auto& b:bounds) if(samePlane(b.plane,next.plane)) return b.outward.Dot(next.outward)>0.999999;
    bounds.push_back(next);return true;
}
}

bool PlanarFaceDeletion::rebuild(const TopoDS_Shape& source,const std::vector<TopoDS_Face>& faces,
        BRepReconstruction::Result& result,QString& error) {
    result={};error.clear();
    const auto fail=[&](const QString& why){result={};error=why;return false;};
    try {
        double mass=0;if(!BRepReconstruction::validSolid(source,mass)) return false;
        TopTools_IndexedMapOfShape members;TopExp::MapShapes(source,TopAbs_FACE,members);
        if(members.Extent()>64||faces.empty()) return false;
        TopTools_MapOfShape removed;std::vector<TopoDS_Face> selectedFaces;
        for(const auto& f:faces) {
            if(f.IsNull()||!members.Contains(f)) return false;
            // Selection/history handles may carry a different orientation.
            // Use the face as oriented in the current solid for halfspaces.
            if(removed.Add(f)) selectedFaces.push_back(TopoDS::Face(members(members.FindIndex(f))));
        }
        if(removed.Extent()==members.Extent()) return false;
        for(int i=1;i<=members.Extent();++i)
            if(BRepAdaptor_Surface(TopoDS::Face(members(i))).GetType()!=GeomAbs_Plane) return false;

        // Prefer a transition carrying an opposite-facing retained patch.
        // Only the current Part can supply reconstruction supports.
        std::optional<gp_Dir> axis;
        for(const auto& f:selectedFaces) {
            const auto p=support(f);
            for(int i=1;i<=members.Extent();++i) if(!removed.Contains(members(i))) {
                const auto q=support(TopoDS::Face(members(i)));
                if(samePlane(p.plane,q.plane)&&p.outward.Dot(q.outward)<-0.999999) {
                    if(axis&&!axis->IsParallel(p.outward,1.e-8)) return fail(QStringLiteral("Selected steps have incompatible reconstruction axes."));
                    axis=p.outward;
                }
            }
        }
        // A side facet alone may also be removed from an already stepped
        // extrusion. Infer the same unique axis from its retained transitions.
        if(!axis) for(int i=1;i<=members.Extent();++i) for(int j=i+1;j<=members.Extent();++j) {
            const auto p=support(TopoDS::Face(members(i))),q=support(TopoDS::Face(members(j)));
            if(samePlane(p.plane,q.plane)&&p.outward.Dot(q.outward)<-0.999999) {
                if(axis&&!axis->IsParallel(p.outward,1.e-8)) return fail(QStringLiteral("The source has incompatible step axes."));
                axis=p.outward;
            }
        }
        // A previous deletion can remove the last opposite-facing transition
        // patch. Infer the unique section axis from the current geometry,
        // without depending on deleted topology or a command's history.
        if(!axis) {
            std::vector<gp_Dir> candidates;
            for(int i=1;i<=members.Extent();++i) {
                const auto direction=support(TopoDS::Face(members(i))).outward;
                if(std::any_of(candidates.begin(),candidates.end(),[&](const auto& a){return a.IsParallel(direction,1.e-8);})) continue;
                bool prismatic=true;std::vector<gp_Pln> axialPlanes;
                for(int j=1;j<=members.Extent();++j) {
                    const auto b=support(TopoDS::Face(members(j)));const double alignment=std::abs(b.outward.Dot(direction));
                    if(alignment>1.e-8&&alignment<1.0-1.e-8) {prismatic=false;break;}
                    if(alignment>1.0-1.e-8&&std::none_of(axialPlanes.begin(),axialPlanes.end(),[&](const auto& p){return samePlane(p,b.plane);})) axialPlanes.push_back(b.plane);
                }
                if(prismatic&&axialPlanes.size()>=3&&axialPlanes.size()<=8) candidates.push_back(direction);
            }
            // A rotated section can vary along its axis: its top/front supports
            // are oblique to the station planes. Prefer a prismatic axis when
            // available; otherwise require a unique family of 3..8 stations.
            // Convexity, complete source coverage and actual opposite-section
            // adjacency below still certify every reconstructed section.
            if(candidates.empty()) for(int i=1;i<=members.Extent();++i) {
                const auto direction=support(TopoDS::Face(members(i))).outward;
                if(std::any_of(candidates.begin(),candidates.end(),[&](const auto& a){return a.IsParallel(direction,1.e-8);})) continue;
                std::vector<gp_Pln> axialPlanes;
                for(int j=1;j<=members.Extent();++j) {
                    const auto b=support(TopoDS::Face(members(j)));
                    if(b.outward.IsParallel(direction,1.e-8)&&std::none_of(axialPlanes.begin(),axialPlanes.end(),[&](const auto& p){return samePlane(p,b.plane);})) axialPlanes.push_back(b.plane);
                }
                if(axialPlanes.size()>=3&&axialPlanes.size()<=8) candidates.push_back(direction);
            }
            if(candidates.size()>1) return fail(QStringLiteral("The remaining geometry has more than one possible section reconstruction axis."));
            if(candidates.size()==1) axis=candidates.front();
        }
        if(!axis) return fail(QStringLiteral("The source has no unique planar section axis."));
        std::vector<gp_Pln> stations,planes;
        for(int i=1;i<=members.Extent();++i) {
            const auto b=support(TopoDS::Face(members(i)));const double alignment=std::abs(b.outward.Dot(*axis));
            if(std::none_of(planes.begin(),planes.end(),[&](const auto& p){return samePlane(p,b.plane);})) planes.push_back(b.plane);
            if(alignment>1.0-1.e-8&&std::none_of(stations.begin(),stations.end(),[&](const auto& p){return samePlane(p,b.plane);})) stations.push_back(b.plane);
        }
        if(planes.size()>12||stations.size()<3||stations.size()>8) return fail(QStringLiteral("Planar step arrangement exceeds the supported bounds."));
        BRepBuilderAPI_Copy copy(source,true,false);
        BOPAlgo_Splitter split;split.SetNonDestructive(true);split.AddArgument(copy.Shape());
        for(const auto& p:stations) split.AddTool(BRepBuilderAPI_MakeFace(p).Face());
        split.Perform();if(split.HasErrors()) return fail(QStringLiteral("Cannot isolate the planar material sections."));
        std::vector<Cell> cells;double covered=0;
        for(TopExp_Explorer s(split.Shape(),TopAbs_SOLID);s.More();s.Next()) {
            Cell cell{s.Current(),{}};double size=0;
            if(!BRepReconstruction::validSolid(cell.source,size)) return fail(QStringLiteral("A source section is not a valid solid."));
            covered+=size;
            for(TopExp_Explorer f(cell.source,TopAbs_FACE);f.More();f.Next()) {
                const auto b=support(TopoDS::Face(f.Current()));
                for(TopExp_Explorer v(cell.source,TopAbs_VERTEX);v.More();v.Next())
                    if(gp_Vec(b.plane.Location(),BRep_Tool::Pnt(TopoDS::Vertex(v.Current()))).Dot(gp_Vec(b.outward))>tolerance)
                        return fail(QStringLiteral("A source section is nonconvex."));
                if(!addBound(cell.bounds,b)) return fail(QStringLiteral("A source section has conflicting support orientations."));
            }
            cells.push_back(cell);
        }
        if(cells.size()!=stations.size()-1||std::abs(covered-mass)>1.e-8*std::max(1.0,mass))
            return fail(QStringLiteral("The source has disconnected or ambiguous material sections."));
        // Equal volume is insufficient: certify complete original coverage too.
        if(!BRepReconstruction::equivalent(source,split.Shape())) return fail(QStringLiteral("Source section coverage could not be certified."));
        TopTools_IndexedDataMapOfShapeListOfShape adjacency;
        TopExp::MapShapesAndAncestors(source,TopAbs_EDGE,TopAbs_FACE,adjacency);
        for(const auto& shape:selectedFaces) {
            const auto patch=TopoDS::Face(shape);const auto b=support(patch);
            std::vector<size_t> owners;
            for(size_t i=0;i<cells.size();++i) if(overlap(cells[i].source,patch)>tolerance) owners.push_back(i);
            if(owners.empty()) return fail(QStringLiteral("The selected patch has no owning material section."));
            if(std::abs(b.outward.Dot(*axis))<1.0-1.e-8) {
                // Only remove whole section facets. A partially selected patch
                // must not silently remove another patch on the same plane.
                for(const auto index:owners) {
                    auto& bounds=cells[index].bounds;
                    double facetArea=0,selectedArea=0;
                    for(TopExp_Explorer f(cells[index].source,TopAbs_FACE);f.More();f.Next())
                        if(samePlane(b.plane,BRepAdaptor_Surface(TopoDS::Face(f.Current())).Plane())) facetArea+=area(f.Current());
                    for(const auto& selected:selectedFaces)
                        if(samePlane(b.plane,BRepAdaptor_Surface(TopoDS::Face(selected)).Plane())) selectedArea+=overlap(cells[index].source,TopoDS::Face(selected));
                    if(std::abs(facetArea-selectedArea)>1.e-7*std::max(1.0,facetArea)) return fail(QStringLiteral("A selected boundary covers only part of a section facet."));
                    bounds.erase(std::remove_if(bounds.begin(),bounds.end(),[&](const auto& p){return samePlane(p.plane,b.plane);}),bounds.end());
                }
                continue;
            }
            if(owners.size()!=1) return fail(QStringLiteral("A selected transition belongs to multiple material sections."));
            auto& owner=cells[owners.front()];bool trimmed=false;
            for(TopExp_Explorer edge(patch,TopAbs_EDGE);edge.More();edge.Next()) {
                for(TopTools_ListIteratorOfListOfShape n(adjacency.FindFromKey(edge.Current()));n.More();n.Next()) {
                    if(n.Value().IsSame(patch)||removed.Contains(n.Value())) continue;
                    const auto next=support(TopoDS::Face(n.Value()));
                    if(std::abs(next.outward.Dot(*axis))>1.0-1.e-8) return fail(QStringLiteral("A transition neighbor cannot serve as a transverse trim."));
                    if(std::any_of(owner.bounds.begin(),owner.bounds.end(),[&](const auto& p){return samePlane(p.plane,next.plane);})) continue;
                    // A new trim must be a physical neighbor support from the
                    // opposite section, and must cut some of the selected patch.
                    // An L-shaped patch can wrap around several such supports;
                    // their combined trims must remove it completely (checked
                    // against the final boundary below).
                    bool opposite=false;
                    for(size_t i=0;i<cells.size();++i) if(i!=owners.front()) {
                        const bool across=std::any_of(cells[i].bounds.begin(),cells[i].bounds.end(),[&](const auto& p){return samePlane(p.plane,b.plane)&&p.outward.Dot(b.outward)<-0.999999;});
                        opposite=opposite||(across&&std::any_of(cells[i].bounds.begin(),cells[i].bounds.end(),[&](const auto& p){return samePlane(p.plane,next.plane)&&p.outward.Dot(next.outward)>0.999999;}));
                    }
                    if(!opposite) return fail(QStringLiteral("A transition trim has no opposite-section support."));
                    bool outside=false;
                    for(TopExp_Explorer v(patch,TopAbs_VERTEX);v.More();v.Next()) {
                        const double d=gp_Vec(next.plane.Location(),BRep_Tool::Pnt(TopoDS::Vertex(v.Current()))).Dot(gp_Vec(next.outward));
                        outside=outside||d>tolerance;
                    }
                    if(!outside||!addBound(owner.bounds,next)) return fail(QStringLiteral("A neighboring support does not trim the selected transition."));
                    trimmed=true;
                }
            }
            if(!trimmed) return fail(QStringLiteral("The transition has no unique neighboring section trim."));
        }
        Bnd_Box bounds;BRepBndLib::AddOptimal(source,bounds,false,false);
        double a,b,c,d,e,f;bounds.Get(a,b,c,d,e,f);const double span=std::max({d-a,e-b,f-c,1.0});
        const auto seed=BRepPrimAPI_MakeBox(gp_Pnt(a-4*span,b-4*span,c-4*span),gp_Pnt(d+4*span,e+4*span,f+4*span)).Shape();
        TopoDS_Shape united;
        for(const auto& cell:cells) {
            TopoDS_Shape rebuilt=seed;
            for(const auto& bound:cell.bounds) {
                const auto half=BRepPrimAPI_MakeHalfSpace(BRepBuilderAPI_MakeFace(bound.plane).Face(),bound.plane.Location().Translated(gp_Vec(bound.outward)*-span)).Solid();
                rebuilt=combine<BRepAlgoAPI_Common>(rebuilt,half);
            }
            double size=0;if(!BRepReconstruction::validSolid(rebuilt,size)) return fail(QStringLiteral("The requested section cannot bound a closed solid."));
            for(TopExp_Explorer face(rebuilt,TopAbs_FACE);face.More();face.Next())
                if(std::none_of(cell.bounds.begin(),cell.bounds.end(),[&](const auto& p){return samePlane(p.plane,BRepAdaptor_Surface(TopoDS::Face(face.Current())).Plane());}))
                    return fail(QStringLiteral("The requested section is unbounded."));
            united=united.IsNull()?rebuilt:combine<BRepAlgoAPI_Fuse>(united,rebuilt);
        }
        TopoDS_Shape candidate;if(!BRepReconstruction::normalize(united,candidate,error)) return false;
        if(BRepReconstruction::equivalent(source,candidate)) return fail(QStringLiteral("The operation leaves the original material unchanged."));
        // Deleted patches must actually disappear, including where their
        // carrier still hosts a necessary opposite-facing exterior patch.
        for(const auto& selected:selectedFaces) if(overlap(candidate,selected)>tolerance)
            return fail(QStringLiteral("A selected transition patch remains in the reconstructed boundary."));
        for(int i=1;i<=members.Extent();++i) {
            const auto original=TopoDS::Face(members(i));BRepReconstruction::FaceMapping mapping{original,{}};
            if(!removed.Contains(original)) {
                double retained=0;
                for(TopExp_Explorer face(candidate,TopAbs_FACE);face.More();face.Next())
                    if(BRepReconstruction::sameAnalyticSupport(original,TopoDS::Face(face.Current()))) {
                        mapping.modified.push_back(TopoDS::Face(face.Current()));
                        retained+=area(combine<BRepAlgoAPI_Common>(original,face.Current()));
                    }
                if(retained<=tolerance) return fail(QStringLiteral("Reconstruction would lose an unselected face patch."));
            }
            result.history.push_back(mapping);
        }
        for(TopExp_Explorer face(candidate,TopAbs_FACE);face.More();face.Next()) {
            bool mapped=false;for(const auto& m:result.history) for(const auto& patch:m.modified) mapped=mapped||patch.IsSame(face.Current());
            if(!mapped) return fail(QStringLiteral("Reconstruction would introduce a new support."));
        }
        result.solid=candidate;return true;
    } catch(const Standard_Failure& failure) {
        return fail(QStringLiteral("Planar step reconstruction refused: %1").arg(QString::fromUtf8(failure.GetMessageString())));
    }
}
