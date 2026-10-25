#include "CadViewport.h"
#include <QComboBox>
#include <QListWidget>
#include <StdSelect_BRepOwner.hxx>
#include <TopExp_Explorer.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <algorithm>

namespace {
using Mode=SketchProjectionPanel::Mode;
bool accepts(Mode mode,TopAbs_ShapeEnum type) {
    return (type==TopAbs_VERTEX && (mode==Mode::Vertex || mode==Mode::Mixed))
        || (type==TopAbs_EDGE && (mode==Mode::Edge || mode==Mode::Mixed))
        || (type==TopAbs_FACE && (mode==Mode::Face || mode==Mode::Mixed));
}
bool samePlacement(const gp_Trsf& a,const gp_Trsf& b) {
    for(int r=1;r<=3;++r) for(int c=1;c<=4;++c) if(a.Value(r,c)!=b.Value(r,c)) return false;
    return true;
}
QString typeName(TopAbs_ShapeEnum type) {
    return type==TopAbs_VERTEX?QObject::tr("Vertex"):type==TopAbs_EDGE?QObject::tr("Edge"):QObject::tr("Face");
}
}
void CadViewport::startSketchProjection() {
    const auto* target=constructionObject(activeSketchWorkplaneId_);QString error;
    if(!target || target->kind!=Construction::Kind::Workplane || !target->visible || !target->selectable || !Construction::valid(*target,error)) {
        emit statusMessage(tr("Project 3D Geometry: activate a visible, unlocked Workplane with Edit Sketch first."));return;
    }
    const auto plane=*target;cancelCommandSession();const auto selection=selectedSketchIds_;
    initializeViewer();clearHistorySelection();clearSketchFeedback();
    commandSession_.emplace();auto& session=*commandSession_;session.kind=CommandSession::Kind::Sketch;
    session.sketch=std::make_shared<SketchChange>(SketchChange{sketchEntities_,sketchEntities_});
    session.projection=std::make_shared<SketchProjectionState>();session.projection->workplane=plane;
    session.projection->previousSketchSelection=selection;session.valid=false;
    auto* panel=sketchProjectionPanel_=new SketchProjectionPanel(plane.name,this);
    panel->modeChanged=[this]{changeSketchProjectionMode();};
    panel->clearSources=[this]{clearSketchProjectionSources();setFocus();};
    panel->removeSources=[this] {
        if(!commandSession_ || !commandSession_->projection) return;
        const auto& refs=commandSession_->projection->sources;
        for(const auto* item:sketchProjectionPanel_->sources->selectedItems()) {
            const int row=sketchProjectionPanel_->sources->row(item);
            if(row>=0 && size_t(row)<refs.size() && context_->IsSelected(refs[size_t(row)].owner))
                context_->AddOrRemoveSelected(refs[size_t(row)].owner,Standard_False);
        }
        captureSketchProjectionSelection();setFocus();
    };
    panel->confirmed=[this]{confirmCommandSession();};panel->cancelled=[this]{cancelCommandSession();};
    configureSketchProjectionPicking();refreshSketchProjection();showCommandPanel(panel);setFocus();
}
void CadViewport::configureSketchProjectionPicking() {
    if(!commandSession_ || !commandSession_->projection || context_.IsNull()) return;
    const auto& state=*commandSession_->projection;
    context_->ClearSelected(Standard_False);context_->ClearDetected(Standard_False);
    for(const auto& part:boxes_) if(!part.presentation.IsNull()) {
        context_->Deactivate(part.presentation);
        if(part.active && part.visible) for(const auto type:{TopAbs_VERTEX,TopAbs_EDGE,TopAbs_FACE})
            if(accepts(state.mode,type)) context_->Activate(part.presentation,AIS_Shape::SelectionMode(type),Standard_False);
    }
    for(const auto& [id,p]:constructionPresentations_) context_->Deactivate(p);
    for(const auto& [id,p]:sketchPresentations_) context_->Deactivate(p);
    for(const auto& [id,p]:sketchPreviews_) context_->Deactivate(p);
    for(const auto& ref:state.sources) if(accepts(state.mode,ref.shape.ShapeType()) && isPartVisible(ref.partIndex))
        context_->AddOrRemoveSelected(ref.owner,Standard_False);
}
bool CadViewport::projectionReference(const Handle(SelectMgr_EntityOwner)& owner,SketchProjectionState::Reference& result) const {
    if(!commandSession_ || !commandSession_->projection) return false;
    const auto brep=Handle(StdSelect_BRepOwner)::DownCast(owner);
    if(brep.IsNull() || brep->Shape().IsNull() || !accepts(commandSession_->projection->mode,brep->Shape().ShapeType())) return false;
    for(size_t index=0;index<boxes_.size();++index) {
        const auto& part=boxes_[index];if(!isPartVisible(index) || part.presentation!=owner->Selectable()) continue;
        // Use the owner's Part-local subshape, not SelectedShape(), which can
        // already include the AIS instance transform. Match current topology.
        for(TopExp_Explorer it(part.shape,brep->Shape().ShapeType());it.More();it.Next()) if(it.Current().IsSame(brep->Shape())) {
            result={index,part.persistentId,part.shape,brep->Shape(),part.orientation,owner};return true;
        }
    }
    return false;
}
bool CadViewport::validateSketchProjectionSources() {
    if(!commandSession_ || !commandSession_->projection) return false;
    const auto& d=*commandSession_->projection;const auto* owner=constructionObject(activeSketchWorkplaneId_);
    bool valid=owner && activeSketchWorkplaneId_==d.workplane.id && Construction::equivalent(*owner,d.workplane) && owner->visible && owner->selectable;
    for(const auto& ref:d.sources) {
        if(!isPartVisible(ref.partIndex)) {valid=false;break;}
        const auto& part=boxes_[ref.partIndex];
        if(part.persistentId!=ref.partId || !part.shape.IsEqual(ref.root) || !samePlacement(part.orientation,ref.placement)) {valid=false;break;}
        bool member=false;for(TopExp_Explorer it(part.shape,ref.shape.ShapeType());it.More();it.Next()) if(it.Current().IsSame(ref.shape)) {member=true;break;}
        if(!member) {valid=false;break;}
    }
    if(!valid) {
        cancelCommandSession();validateActiveSketchContext();if(activeSketchWorkplaneId_.isEmpty()) refreshSketchPicking(true);
        emit statusMessage(tr("Project 3D Geometry cancelled: its Workplane or 3D sources changed or are no longer available."));
    }
    return valid;
}
void CadViewport::changeSketchProjectionMode() {
    if(!validateSketchProjectionSources() || !sketchProjectionPanel_) return;
    auto& d=*commandSession_->projection;d.mode=static_cast<Mode>(sketchProjectionPanel_->mode->currentData().toInt());
    std::erase_if(d.sources,[&](const auto& ref){return !accepts(d.mode,ref.shape.ShapeType());});
    configureSketchProjectionPicking();captureSketchProjectionSelection();
}
bool CadViewport::captureSketchProjectionSelection() {
    if(!validateSketchProjectionSources()) return false;
    std::vector<SketchProjectionState::Reference> refs;
    for(context_->InitSelected();context_->MoreSelected();context_->NextSelected()) {
        SketchProjectionState::Reference ref{};
        if(!projectionReference(context_->SelectedOwner(),ref)) continue;
        if(std::none_of(refs.begin(),refs.end(),[&](const auto& prior){return prior.partId==ref.partId && prior.shape.IsSame(ref.shape);})) refs.push_back(ref);
    }
    // OCCT remains authoritative. Remove stale/unsupported/duplicate owners,
    // rather than keeping a separate invisible operand selection.
    context_->ClearSelected(Standard_False);context_->ClearDetected(Standard_False);
    for(const auto& ref:refs) context_->AddOrRemoveSelected(ref.owner,Standard_False);
    commandSession_->projection->sources=std::move(refs);
    std::vector<size_t> parts;
    for(const auto& ref:commandSession_->projection->sources) if(std::find(parts.begin(),parts.end(),ref.partIndex)==parts.end()) parts.push_back(ref.partIndex);
    emit partSelectionChanged(parts);refreshSketchProjection();return true;
}
bool CadViewport::sketchProjectionClick(const QPoint& pixel,Qt::KeyboardModifiers modifiers) {
    if(!commandSession_ || !commandSession_->projection) return false;
    if(!validateSketchProjectionSources()) return true;
    context_->MoveTo(pixel.x(),pixel.y(),view_,Standard_False);
    if(context_->HasDetected()) {
        SketchProjectionState::Reference ref{};
        if(!projectionReference(context_->DetectedOwner(),ref)) {
            emit statusMessage(tr("Choose a visible 3D vertex, edge or face in the current projection mode."));return true;
        }
    }
    context_->SelectDetected(modifiers&Qt::ControlModifier?AIS_SelectionScheme_XOR:AIS_SelectionScheme_Replace);
    captureSketchProjectionSelection();return true;
}
void CadViewport::clearSketchProjectionSources() {
    if(!commandSession_ || !commandSession_->projection) return;
    context_->ClearSelected(Standard_False);context_->ClearDetected(Standard_False);
    commandSession_->projection->sources.clear();emit partSelectionChanged({});refreshSketchProjection();
}
void CadViewport::refreshSketchProjection() {
    if(!validateSketchProjectionSources() || !sketchProjectionPanel_) return;
    auto& s=*commandSession_;auto& d=*s.projection;
    for(const auto& [id,p]:sketchPreviews_) context_->Remove(p,Standard_False);sketchPreviews_.clear();
    s.sketch->after=s.sketch->before;s.valid=false;
    std::vector<SketchProjection::Source> sources;QStringList labels;
    for(const auto& ref:d.sources) {
        const auto label=tr("%1. %2 / %3").arg(sources.size()+1).arg(boxes_[ref.partIndex].name,typeName(ref.shape.ShapeType()));
        labels<<label;sources.push_back({ref.shape,ref.placement,label});
    }
    auto result=SketchProjection::project(sources,d.workplane,s.sketch->before);
    for(auto& value:result.additions) {
        const auto prior=std::find_if(d.result.additions.begin(),d.result.additions.end(),[&](const auto& old){return old.geometry==value.geometry && old.workplaneId==value.workplaneId;});
        if(prior!=d.result.additions.end()) value.id=prior->id;
    }
    QStringList diagnostics;for(const auto& item:result.diagnostics) diagnostics<<tr("%1%2").arg(item.sourceLabel.isEmpty()?QString():item.sourceLabel+": ",item.message);
    if(result.success) {
        try {
            for(const auto& value:result.additions) {
                auto presentation=sketchPresentation(value,d.workplane,true);sketchPreviews_[value.id]=presentation;
                displaySketchPresentation(value,d.workplane,presentation,true);context_->SetZLayer(presentation,Graphic3d_ZLayerId_Topmost);
            }
            s.sketch->after.insert(s.sketch->after.end(),result.additions.begin(),result.additions.end());s.valid=!sources.empty();
        } catch(const Standard_Failure& failure) {
            for(const auto& [id,p]:sketchPreviews_) context_->Remove(p,Standard_False);sketchPreviews_.clear();
            s.sketch->after=s.sketch->before;result.success=false;result.additions.clear();diagnostics<<tr("Cannot display preview: %1").arg(QString::fromUtf8(failure.GetMessageString()));
        }
    }
    const auto message=sources.empty()?tr("Choose 3D sources in the viewport. Ctrl+click adds/removes.")
        :!result.success?tr("Projection refused. No partial result will be added; remove the invalid sources or Cancel.")
        :result.additions.empty()?tr("All geometry already exists. OK closes without a document or history change.")
        :tr("Blue preview only. OK adds all entities in one operation; Cancel leaves the document unchanged.");
    sketchProjectionPanel_->setResult(labels,int(result.additions.size()),message,diagnostics,s.valid);
    d.result=std::move(result);s.state=s.valid?CommandSession::State::Preview:CommandSession::State::Selection;
    emit statusMessage(message);updateConstructionLabels();view_->Redraw();
}
bool CadViewport::confirmSketchProjection() {
    if(!validateSketchProjectionSources()) return false;
    const auto previous=commandSession_->projection->result.additions;
    const bool wasSuccessful=commandSession_->projection->result.success;
    refreshSketchProjection();if(!commandSession_ || !commandSession_->projection) return false;
    if(commandSession_->projection->result.success!=wasSuccessful || commandSession_->projection->result.additions!=previous) {
        cancelCommandSession();emit statusMessage(tr("Project 3D Geometry cancelled: source geometry changed since the preview. Select it again."));return false;
    }
    if(!commandSession_->valid) {emit statusMessage(tr("Choose a valid projection batch before OK."));return false;}
    return true;
}
void CadViewport::closeSketchProjectionPanel() {
    if(!sketchProjectionPanel_) return;
    auto* panel=sketchProjectionPanel_;sketchProjectionPanel_=nullptr;
    panel->modeChanged={};panel->clearSources={};panel->removeSources={};panel->confirmed={};panel->cancelled={};panel->hide();panel->deleteLater();
    context_->ClearSelected(Standard_False);context_->ClearDetected(Standard_False);emit partSelectionChanged({});
}
