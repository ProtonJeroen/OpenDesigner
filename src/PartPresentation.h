#pragma once

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <BRepLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <StdPrs_WFShape.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <SelectMgr_Selection.hxx>
#include <StdSelect_BRepOwner.hxx>

// Silhouettes and sharp feature edges use the existing part presentation. Suppress
// only the standard whole-part selection wireframe, not hover or local owners.
class PartSelectionOwner final : public StdSelect_BRepOwner {
public:
    PartSelectionOwner(const Handle(StdSelect_BRepOwner)& original)
        : StdSelect_BRepOwner(original->Shape(),original->Selectable(),original->Priority(),false) {}

    void HilightWithColor(const Handle(PrsMgr_PresentationManager)& manager,
                          const Handle(Prs3d_Drawer)& style,
                          Standard_Integer mode=0) override {
        const auto object=Handle(AIS_InteractiveObject)::DownCast(Selectable());
        const auto context=object.IsNull()?Handle(AIS_InteractiveContext)():object->GetContext();
        if(!context.IsNull() && style==context->SelectionStyle())return;
        StdSelect_BRepOwner::HilightWithColor(manager,style,mode);
    }
};

class PartPresentation final : public AIS_Shape {
public:
    explicit PartPresentation(const TopoDS_Shape& shape) : AIS_Shape(shape) {}

    bool setFeatureHighlight(bool enabled,const Quantity_Color& color) {
        const bool changed=featureHighlight_!=enabled || (enabled && featureColor_!=color);
        featureHighlight_=enabled;featureColor_=color;return changed;
    }

    // Shared BRep references only: no copied geometry or selection primitives.
    // Geometric continuity also handles imported tangent joins without stored
    // regularity flags. 1e-4 radians rejects numerical noise at tangent joins.
    static TopoDS_Compound featureEdges(const TopoDS_Shape& shape) {
        TopoDS_Compound features;BRep_Builder builder;builder.MakeCompound(features);
        TopTools_IndexedDataMapOfShapeListOfShape adjacent;
        TopExp::MapShapesAndAncestors(shape,TopAbs_EDGE,TopAbs_FACE,adjacent);
        for(int i=1;i<=adjacent.Extent();++i) {
            const auto edge=TopoDS::Edge(adjacent.FindKey(i));
            if(BRep_Tool::Degenerated(edge))continue;
            TopTools_IndexedMapOfShape faces;bool seam=false;
            for(const auto& face:adjacent.FindFromIndex(i)) {
                faces.Add(face);
                if(BRep_Tool::IsClosed(edge,TopoDS::Face(face)))seam=true;
            }
            if(seam || faces.IsEmpty() || faces.Extent()>2)continue;
            if(faces.Extent()==2) {
                try {
                    if(BRepLib::ContinuityOfFaces(edge,TopoDS::Face(faces(1)),TopoDS::Face(faces(2)),1e-4)!=GeomAbs_C0)continue;
                } catch(const Standard_Failure&) {continue;} // Unclassifiable joins are not highlighted.
            }
            builder.Add(features,edge);
        }
        return features;
    }

protected:
    void Compute(const Handle(PrsMgr_PresentationManager)& manager,
                 const Handle(Prs3d_Presentation)& presentation,Standard_Integer mode) override {
        AIS_Shape::Compute(manager,presentation,mode);
        if(mode!=AIS_Shaded || !featureHighlight_)return;
        const auto features=featureEdges(Shape());
        Handle(Prs3d_Drawer) drawer=new Prs3d_Drawer;drawer->SetLink(Attributes());
        const Handle(Prs3d_LineAspect) lines=new Prs3d_LineAspect(featureColor_,Aspect_TOL_SOLID,2.8);
        drawer->SetWireAspect(lines);drawer->SetLineAspect(lines);
        drawer->SetFreeBoundaryAspect(lines);drawer->SetUnFreeBoundaryAspect(lines);
        // Scene-depth-tested lines in the same presentation as the shaded
        // faces. No Topmost layer, triangulation edges, or selectable overlay.
        // OCCT 7.6's wireframe builder writes Polygon3D caches into edges.
        // Detach only display-edge topology/caches; share the exact curves.
        // Document and Undo snapshots must not change when highlighting draws.
        const auto displayEdges=BRepBuilderAPI_Copy(features,false,false).Shape();
        StdPrs_WFShape::Add(presentation,displayEdges,drawer);
    }

    void ComputeSelection(const Handle(SelectMgr_Selection)& selection,Standard_Integer mode) override {
        AIS_Shape::ComputeSelection(selection,mode);
        if(mode!=0)return;
        Handle(PartSelectionOwner) partOwner;
        for(const auto& entity:selection->Entities()) {
            const auto& sensitive=entity->BaseSensitive();
            const auto original=Handle(StdSelect_BRepOwner)::DownCast(sensitive->OwnerId());
            if(original.IsNull() || original->ComesFromDecomposition())continue;
            if(partOwner.IsNull())partOwner=new PartSelectionOwner(original);
            // Keep OCCT's sensitive geometry, priority, placement and BVH.
            // Only its whole-object highlight callback changes.
            sensitive->Set(partOwner);
        }
    }
private:
    bool featureHighlight_=false;
    Quantity_Color featureColor_=Quantity_NOC_WHITE;
};
