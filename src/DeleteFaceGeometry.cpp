#include "DeleteFaceGeometry.h"
#include "CylindricalMoveGeometry.h"
#include "PlanarFaceDeletion.h"
#include <BRepAlgoAPI_Defeaturing.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_FindPlane.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepBndLib.hxx>
#include <BRepLib.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Geom_Plane.hxx>
#include <GProp_GProps.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_MapOfShape.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <TopoDS.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace {
constexpr double tolerance=1.e-7;
double area(const TopoDS_Shape& shape) {
    GProp_GProps p;BRepGProp::SurfaceProperties(shape,p);return p.Mass();
}
double volume(const TopoDS_Shape& shape) {
    GProp_GProps p;BRepGProp::VolumeProperties(shape,p);return p.Mass();
}
template<class Operation>
TopoDS_Shape combine(const TopoDS_Shape& a,const TopoDS_Shape& b) {
    Operation op;TopTools_ListOfShape args,tools;args.Append(a);tools.Append(b);
    op.SetArguments(args);op.SetTools(tools);op.SetNonDestructive(true);op.Build();
    if(!op.IsDone()||op.HasErrors()) throw Standard_Failure("Feature material verification failed.");
    return op.Shape();
}
bool samePlane(const gp_Pln& a,const gp_Pln& b) {
    return a.Axis().Direction().IsParallel(b.Axis().Direction(),1.e-8)&&a.Distance(b.Location())<tolerance;
}
// Certify a unique intersection of retained halfspaces. The seed only limits
// computation; any seed support left in the oracle causes rejection below.
TopoDS_Shape convexOracle(const TopoDS_Shape& source,const TopTools_MapOfShape& removed) {
    std::vector<std::pair<gp_Pln,gp_Dir>> planes;
    for(TopExp_Explorer f(source,TopAbs_FACE);f.More();f.Next()) {
        const auto face=TopoDS::Face(f.Current());const BRepAdaptor_Surface s(face);
        if(s.GetType()!=GeomAbs_Plane) return {};
        auto outward=s.Plane().Axis().Direction();if(face.Orientation()==TopAbs_REVERSED) outward.Reverse();
        for(TopExp_Explorer v(source,TopAbs_VERTEX);v.More();v.Next())
            if(gp_Vec(s.Plane().Location(),BRep_Tool::Pnt(TopoDS::Vertex(v.Current()))).Dot(gp_Vec(outward))>tolerance) return {};
        if(!removed.Contains(face)) planes.emplace_back(s.Plane(),outward);
    }
    if(planes.size()<4||planes.size()>12) return {};
    Bnd_Box box;BRepBndLib::AddOptimal(source,box,false,false);
    double a,b,c,d,e,f;box.Get(a,b,c,d,e,f);
    const double span=std::max({d-a,e-b,f-c,1.0});
    TopoDS_Shape oracle=BRepPrimAPI_MakeBox(gp_Pnt(a-4*span,b-4*span,c-4*span),gp_Pnt(d+4*span,e+4*span,f+4*span)).Shape();
    for(const auto& [plane,outward]:planes) {
        const auto half=BRepPrimAPI_MakeHalfSpace(BRepBuilderAPI_MakeFace(plane).Face(),
            plane.Location().Translated(gp_Vec(outward)*-span)).Solid();
        oracle=combine<BRepAlgoAPI_Common>(oracle,half);
    }
    double mass=0;if(!BRepReconstruction::validSolid(oracle,mass)) return {};
    for(TopExp_Explorer face(oracle,TopAbs_FACE);face.More();face.Next()) {
        const BRepAdaptor_Surface s(TopoDS::Face(face.Current()));
        if(s.GetType()!=GeomAbs_Plane||std::none_of(planes.begin(),planes.end(),[&](const auto& p){return samePlane(s.Plane(),p.first);})) return {};
    }
    return oracle;
}
// A hole/pocket/boss is certified by its original selected faces and closed
// planar rims. Every rim plane must be a real adjacent surviving support.
// Caps are used ONLY for this independent material oracle, never to patch the
// model or to rescue a failed defeaturing result.
TopoDS_Shape featureVolume(const std::vector<TopoDS_Face>& faces,
        const TopTools_IndexedDataMapOfShapeListOfShape& adjacency,
        const TopTools_MapOfShape& removed) {
    TopTools_MapOfShape component;for(const auto& f:faces) component.Add(f);
    Handle(TopTools_HSequenceOfShape) edges=new TopTools_HSequenceOfShape;
    for(int i=1;i<=adjacency.Extent();++i) {
        TopTools_MapOfShape inside,outside;
        for(TopTools_ListIteratorOfListOfShape it(adjacency.FindFromIndex(i));it.More();it.Next()) {
            if(component.Contains(it.Value())) inside.Add(it.Value());
            else outside.Add(it.Value());
        }
        if(inside.IsEmpty()||outside.IsEmpty()) continue; // Includes periodic seams.
        TopTools_MapOfShape::Iterator neighbor(outside);
        if(inside.Extent()!=1||outside.Extent()!=1||removed.Contains(neighbor.Value())) return {};
        const BRepAdaptor_Surface s(TopoDS::Face(neighbor.Value()));
        if(s.GetType()!=GeomAbs_Plane) return {};
        edges->Append(adjacency.FindKey(i));
    }
    if(edges->IsEmpty()) return {};
    Handle(TopTools_HSequenceOfShape) wires=new TopTools_HSequenceOfShape;
    ShapeAnalysis_FreeBounds::ConnectEdgesToWires(edges,tolerance,true,wires);
    if(wires->IsEmpty()||wires->Length()>8) return {};
    BRepBuilderAPI_Sewing sewing(tolerance);
    for(const auto& face:faces) sewing.Add(face);
    std::vector<TopoDS_Face> caps;
    for(int i=1;i<=wires->Length();++i) {
        const auto wire=TopoDS::Wire(wires->Value(i));
        if(!BRep_Tool::IsClosed(wire)) return {};
        BRepBuilderAPI_FindPlane find(wire,tolerance);
        if(!find.Found()) return {};
        const auto plane=find.Plane()->Pln();
        // All neighbors of this loop must agree on the cap's actual support.
        for(TopExp_Explorer edge(wire,TopAbs_EDGE);edge.More();edge.Next()) {
            const auto& neighbors=adjacency.FindFromKey(edge.Current());
            for(TopTools_ListIteratorOfListOfShape it(neighbors);it.More();it.Next()) if(!component.Contains(it.Value())) {
                const BRepAdaptor_Surface s(TopoDS::Face(it.Value()));
                if(s.GetType()!=GeomAbs_Plane||!samePlane(plane,s.Plane())) return {};
            }
        }
        BRepBuilderAPI_MakeFace cap(plane,wire,true);if(!cap.IsDone()) return {};
        for(const auto& other:caps) if(area(combine<BRepAlgoAPI_Common>(other,cap.Face()))>tolerance) return {};
        caps.push_back(cap.Face());sewing.Add(cap.Face());
    }
    sewing.Perform();const auto shell=sewing.SewedShape();
    if(shell.IsNull()||shell.ShapeType()!=TopAbs_SHELL||!BRep_Tool::IsClosed(shell)) return {};
    auto solid=BRepBuilderAPI_MakeSolid(TopoDS::Shell(shell)).Solid();
    double mass=0;
    if(!BRepLib::OrientClosedSolid(solid)||!BRepReconstruction::validSolid(solid,mass)) return {};
    return solid;
}
}

static bool removeNative(const TopoDS_Shape& source,const std::vector<TopoDS_Face>& faces,
        BRepReconstruction::Result& result,QString& error) {
    result={};error.clear();
    const auto fail=[&](const QString& reason){result={};error=reason;return false;};
    try {
        double mass=0;
        if(!BRepReconstruction::validSolid(source,mass)) return fail(QStringLiteral("Select faces on one valid closed solid with one shell."));
        TopTools_IndexedMapOfShape members;TopExp::MapShapes(source,TopAbs_FACE,members);
        if(members.Extent()>64) return fail(QStringLiteral("Delete Face supports at most 64 faces per solid."));
        TopTools_MapOfShape selected;
        for(const auto& face:faces) {
            if(face.IsNull()||!members.Contains(face)) return fail(QStringLiteral("A selected face does not belong to the current solid."));
            selected.Add(face);
        }
        if(selected.IsEmpty()||selected.Extent()==members.Extent()) return fail(QStringLiteral("Select at least one face and retain enough surfaces to close the solid."));
        for(int i=1;i<=members.Extent();++i) {
            const auto type=BRepAdaptor_Surface(TopoDS::Face(members(i))).GetType();
            if(type!=GeomAbs_Plane&&type!=GeomAbs_Cylinder&&type!=GeomAbs_Cone)
                return fail(QStringLiteral("Delete Face cannot certify a solid containing an unrecognized toroidal or other curved transition. Plane, cylinder and cone surfaces are supported; a toroidal Fillet requires a certified recipe."));
        }
        BRepBuilderAPI_Copy copy(source,true,false);
        TopTools_MapOfShape removed;std::vector<TopoDS_Face> copiedFaces;
        BRepAlgoAPI_Defeaturing builder;builder.SetShape(copy.Shape());builder.SetToFillHistory(true);
        for(const auto& face:faces) {
            const auto copied=TopoDS::Face(copy.ModifiedShape(face));
            if(removed.Add(copied)) {copiedFaces.push_back(copied);builder.AddFaceToRemove(copied);}
        }
        builder.Build();
        if(!builder.IsDone()||builder.HasErrors()||builder.HasWarnings()) {
            std::ostringstream details;builder.DumpErrors(details);builder.DumpWarnings(details);
            return fail(QStringLiteral("The adjacent surfaces cannot safely close every selected region. %1").arg(QString::fromStdString(details.str()).trimmed()));
        }
        for(const auto& face:copiedFaces) if(!builder.IsDeleted(face))
            return fail(QStringLiteral("Not all selected faces could be removed; partial deletion is refused."));
        TopoDS_Shape candidate;
        if(!BRepReconstruction::normalize(builder.Shape(),candidate,error)) return false;
        if(BRepReconstruction::equivalent(source,candidate)) return fail(QStringLiteral("The operation did not remove a geometric feature."));

        TopTools_IndexedDataMapOfShapeListOfShape adjacency;
        TopExp::MapShapesAndAncestors(copy.Shape(),TopAbs_EDGE,TopAbs_FACE,adjacency);
        TopoDS_Shape oracle=convexOracle(copy.Shape(),removed);
        if(oracle.IsNull()) {
            // Partition the selection into connected features, irrespective of
            // selection order. Certify each against the immutable original.
            std::vector<std::vector<TopoDS_Face>> components;
            TopTools_MapOfShape visited;
            for(const auto& face:copiedFaces) if(visited.Add(face)) {
                std::vector<TopoDS_Face> group{face};
                for(size_t i=0;i<group.size();++i) for(TopExp_Explorer e(group[i],TopAbs_EDGE);e.More();e.Next()) {
                    for(TopTools_ListIteratorOfListOfShape it(adjacency.FindFromKey(e.Current()));it.More();it.Next())
                        if(removed.Contains(it.Value())&&visited.Add(it.Value())) group.push_back(TopoDS::Face(it.Value()));
                }
                components.push_back(group);
            }
            oracle=copy.Shape();std::vector<TopoDS_Shape> regions;
            for(const auto& component:components) {
                const auto region=featureVolume(component,adjacency,removed);
                if(region.IsNull()) return fail(QStringLiteral("The selected region has no unambiguous closure on adjacent surfaces."));
                for(const auto& other:regions) if(volume(combine<BRepAlgoAPI_Common>(other,region))>tolerance)
                    return fail(QStringLiteral("Selected feature regions overlap; reconstruction is ambiguous."));
                regions.push_back(region);
                const double inside=volume(combine<BRepAlgoAPI_Common>(copy.Shape(),region)),size=volume(region);
                const double eps=1.e-8*std::max(1.0,size);
                if(std::abs(inside-size)<eps) oracle=combine<BRepAlgoAPI_Cut>(oracle,region);
                else if(std::abs(inside)<eps) oracle=combine<BRepAlgoAPI_Fuse>(oracle,region);
                else return fail(QStringLiteral("The selected region crosses existing material ambiguously."));
            }
        }
        if(!BRepReconstruction::equivalent(oracle,candidate))
            return fail(QStringLiteral("Reconstruction changes material outside the uniquely removable feature."));

        // Preserve every surviving patch, not just its carrier or face count.
        // Also require every result support to have a surviving source owner.
        for(int i=1;i<=members.Extent();++i) {
            const auto original=TopoDS::Face(members(i));BRepReconstruction::FaceMapping mapping{original,{}};
            if(!selected.Contains(original)) {
                for(TopExp_Explorer f(candidate,TopAbs_FACE);f.More();f.Next())
                    if(BRepReconstruction::sameAnalyticSupport(original,TopoDS::Face(f.Current()))) mapping.modified.push_back(TopoDS::Face(f.Current()));
                double overlap=0;for(const auto& f:mapping.modified) overlap+=area(combine<BRepAlgoAPI_Common>(original,f));
                if(mapping.modified.empty()||std::abs(overlap-area(original))>1.e-7*std::max(1.0,area(original)))
                    return fail(QStringLiteral("Reconstruction would remove or alter an unselected face patch."));
            }
            result.history.push_back(mapping);
        }
        for(TopExp_Explorer f(candidate,TopAbs_FACE);f.More();f.Next()) {
            bool mapped=false;
            for(const auto& m:result.history) for(const auto& g:m.modified) if(g.IsSame(f.Current())) mapped=true;
            if(!mapped) return fail(QStringLiteral("Reconstruction introduced an unintended closing surface."));
        }
        result.solid=candidate;return true;
    } catch(const Standard_Failure& failure) {
        return fail(QStringLiteral("Open CASCADE could not reconstruct the solid safely: %1").arg(QString::fromUtf8(failure.GetMessageString())));
    }
}

bool DeleteFaceGeometry::remove(const TopoDS_Shape& source,const std::vector<TopoDS_Face>& faces,
        BRepReconstruction::Result& result,QString& error) {
    result={};error.clear();
    QString cylindricalError;
    if(faces.size()==1&&!faces.front().IsNull()&&BRepAdaptor_Surface(faces.front()).GetType()==GeomAbs_Cylinder&&
       CylindricalMoveGeometry::removeBoundaryFeature(source,faces.front(),result,cylindricalError)) return true;
    if(removeNative(source,faces,result,error)) return true;
    QString planarError;
    if(PlanarFaceDeletion::rebuild(source,faces,result,planarError)) {error.clear();return true;}
    if(!planarError.isEmpty()) error+=QStringLiteral(" ")+planarError;
    if(!cylindricalError.isEmpty()) error=cylindricalError+QStringLiteral(" ")+error;
    return false;
}
