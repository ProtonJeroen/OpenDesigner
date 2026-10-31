#pragma once

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <SelectMgr_Selection.hxx>
#include <StdSelect_BRepOwner.hxx>

// The part silhouette is drawn by the existing shaded presentation. Suppress
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

protected:
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
};
