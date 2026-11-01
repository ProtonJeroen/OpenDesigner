#pragma once
#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <QStringList>
#include <map>
#include <vector>

namespace ProfileSweepSelection {
// Restore native local owners, but use each Sketch presentation's current
// global owner. Selected Sketches must retain active mode 0 during axis picking.
inline void restore(const Handle(AIS_InteractiveContext)& context,
                    const std::vector<Handle(SelectMgr_EntityOwner)>& owners,
                    const QStringList& selectedSketches,
                    const std::map<QString,Handle(AIS_Shape)>& sketches) {
    context->ClearSelected(Standard_False);
    for(const auto& owner:owners)if(!owner.IsNull()) {
        bool sketch=false;
        for(const auto& id:selectedSketches) {
            const auto it=sketches.find(id);
            if(it!=sketches.end() && it->second==owner->Selectable()){sketch=true;break;}
        }
        const auto selectable=Handle(AIS_InteractiveObject)::DownCast(owner->Selectable());
        if(!sketch && !selectable.IsNull() && context->IsDisplayed(selectable))context->AddOrRemoveSelected(owner,Standard_False);
    }
    for(const auto& id:selectedSketches) {
        const auto it=sketches.find(id);
        if(it!=sketches.end() && context->IsDisplayed(it->second))context->AddOrRemoveSelected(it->second,Standard_False);
    }
}
}
