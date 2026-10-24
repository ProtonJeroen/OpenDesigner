#include "CadViewport.h"
#include "SketchConstruction.h"
#include <QAction>
#include <QMenu>
#include <QWidget>
#include <QDialogButtonBox>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_PointAspect.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <SelectMgr_Selection.hxx>
#include <SelectMgr_SensitiveEntity.hxx>
#include <Select3D_SensitiveEntity.hxx>
#include <algorithm>

Handle(AIS_Shape) CadViewport::sketchPresentation(const Sketch::Entity& entity,const Construction::Object& owner,bool preview) const {
    Handle(AIS_Shape) presentation=new AIS_Shape(Sketch::shape(entity,owner.frame,owner.extent));
    const bool construction=entity.role==Sketch::Role::Construction;
    const Quantity_Color color=preview?Quantity_Color(0.1,0.65,1,Quantity_TOC_RGB)
        :construction?Quantity_Color(1,0.4,0.8,Quantity_TOC_RGB):Quantity_Color(Quantity_NOC_WHITE);
    presentation->SetColor(color);presentation->SetWidth(construction?1.5:2.5);
    presentation->Attributes()->SetWireAspect(new Prs3d_LineAspect(color,construction?Aspect_TOL_DASH:Aspect_TOL_SOLID,construction?1.5:2.5));
    if(std::holds_alternative<Sketch::ConstructionPoint>(entity.geometry))
        presentation->Attributes()->SetPointAspect(new Prs3d_PointAspect(Aspect_TOM_O_PLUS,color,7.0));
    if(std::holds_alternative<Sketch::InfiniteLine>(entity.geometry)) presentation->SetInfiniteState(true);
    return presentation;
}
void CadViewport::displaySketchPresentation(const Sketch::Entity& entity,const Construction::Object& owner,const Handle(AIS_Shape)& presentation,bool preview) {
    if(!entity.visible || !owner.visible) return;
    context_->Display(presentation,Standard_False);
    context_->SetDisplayMode(presentation,AIS_WireFrame,Standard_False);
    context_->Deactivate(presentation);
    if(!preview && entity.selectable && owner.selectable && (activeSketchWorkplaneId_.isEmpty() || entity.workplaneId==activeSketchWorkplaneId_)) {
        context_->Activate(presentation,0,Standard_False);
        // A curve on a solid face must win coplanar picking. Alt temporarily
        // deactivates these owners so whole-Part selection still passes through.
        const auto selection=presentation->Selection(0);
        if(!selection.IsNull()) for(const auto& sensitive:selection->Entities())
            sensitive->BaseSensitive()->OwnerId()->SetPriority(8);
    }
}
bool CadViewport::sketchPickable(const Sketch::Entity& entity) const {
    const auto* owner=constructionObject(entity.workplaneId);
    return owner && owner->visible && owner->selectable && entity.visible && entity.selectable
        && (activeSketchWorkplaneId_.isEmpty() || entity.workplaneId==activeSketchWorkplaneId_);
}
void CadViewport::clearSketchSelection() {
    if(selectedSketchIds_.empty()) return;
    if(!context_.IsNull()) for(const auto& id:selectedSketchIds_) {
        const auto it=sketchPresentations_.find(id);
        if(it!=sketchPresentations_.end() && context_->IsSelected(it->second)) context_->AddOrRemoveSelected(it->second,Standard_False);
    }
    selectedSketchIds_.clear();emit sketchSelectionChanged({});
}
void CadViewport::rebuildSketchPresentations() {
    if(context_.IsNull()) return;
    for(const auto& [id,p]:sketchPresentations_) context_->Remove(p,Standard_False);
    sketchPresentations_.clear();
    QStringList retained;
    for(const auto& entity:sketchEntities_) if(const auto* owner=constructionObject(entity.workplaneId)) {
        auto p=sketchPresentation(entity,*owner);sketchPresentations_[entity.id]=p;
        displaySketchPresentation(entity,*owner,p);
        if(selectedSketchIds_.contains(entity.id)) {
            retained.push_back(entity.id);
            if(sketchPickable(entity)) context_->AddOrRemoveSelected(p,Standard_False);
        }
    }
    selectedSketchIds_=retained;emit sketchSelectionChanged(retained);
    refreshSketchPicking();
    if(!view_.IsNull()) {view_->ZFitAll();view_->Redraw();}
}
void CadViewport::restoreSketchEntities(const std::vector<Sketch::Entity>& entities) {
    sketchEntities_=entities;rebuildSketchPresentations();emit sketchEntitiesChanged();
}
void CadViewport::startSketchAxes(bool localX,bool localY) {
    const auto* owner=constructionObject(activeSketchWorkplaneId_);
    if(!owner || !owner->visible || !owner->selectable) {emit statusMessage(tr("Activate a visible, unlocked Workplane first."));return;}
    cancelCommandSession();
    const auto missing=Sketch::missingConstructionAxes(sketchEntities_,owner->id,localX,localY);
    if(missing.empty()) {emit statusMessage(tr("The requested Construction axes already exist; no duplicate was added."));return;}
    auto candidate=sketchEntities_;candidate.insert(candidate.end(),missing.begin(),missing.end());QString error;
    if(!previewSketchEntities(candidate,error)) {emit statusMessage(error);return;}
    commandPanel_->setWindowTitle(tr("Construction Axes"));
    emit statusMessage(tr("Blue Construction axes are temporary. OK confirms one operation; Cancel discards them."));setFocus();
}
void CadViewport::populateSketchAxesMenu(QMenu& menu) {
    const auto add=[this,&menu](const QString& name,const char* id,bool x,bool y) {
        auto* action=menu.addAction(name);action->setObjectName(id);
        connect(action,&QAction::triggered,this,[this,x,y]{startSketchAxes(x,y);});
    };
    add(tr("Local X axis (Y=0)"),"sketch.axes.x",true,false);
    add(tr("Local Y axis (X=0)"),"sketch.axes.y",false,true);
    add(tr("Both"),"sketch.axes.both",true,true);
}
bool CadViewport::previewSketchEntities(const std::vector<Sketch::Entity>& entities,QString& error,bool showControls) {
    QStringList ids;
    for(const auto& o:constructions_) ids.push_back(o.id);
    for(const auto& p:boxes_) if(p.active) ids.push_back(p.persistentId);
    if(!Sketch::validate(entities,constructions_,ids,error)) return false;
    std::map<QString,Handle(AIS_Shape)> previews;
    try {
        initializeViewer();
        for(const auto& e:entities) previews[e.id]=sketchPresentation(e,*constructionObject(e.workplaneId),true);
    } catch(const Standard_Failure& failure) {error=QString::fromUtf8(failure.GetMessageString());return false;}
    cancelCommandSession();
    commandSession_.emplace();commandSession_->kind=CommandSession::Kind::Sketch;
    commandSession_->sketch=std::make_shared<SketchChange>(SketchChange{sketchEntities_,entities});
    commandSession_->selection=selectedPartIndices_;
    for(context_->InitSelected();context_->MoreSelected();context_->NextSelected()) commandSession_->owners.push_back(context_->SelectedOwner());
    sketchPreviews_=std::move(previews);
    for(const auto& [id,p]:sketchPresentations_) context_->Erase(p,Standard_False);
    for(const auto& e:entities) displaySketchPresentation(e,*constructionObject(e.workplaneId),sketchPreviews_.at(e.id),true);
    commandSession_->state=CommandSession::State::Preview;
    if(showControls) {
        commandPanel_->setWindowTitle(tr("2D Geometry Preview"));commandButtons_->show();
        showCommandPanel(commandPanel_);
    }
    view_->Redraw();error.clear();return true;
}
void CadViewport::finishSketchEdit(bool confirm) {
    if(!commandSession_ || commandSession_->kind!=CommandSession::Kind::Sketch) return;
    if(confirm && commandSession_->projection && !confirmSketchProjection()) return;
    if(confirm && sketchConstructionPanel_) sketchConstructionPanel_->finishEditing();
    if(confirm && commandSession_->lineConstruction && !commandSession_->valid) {
        emit statusMessage(tr("Choose valid construction references, parameters and an explicit tangent solution before OK."));return;
    }
    if(confirm && commandSession_->drawing && (!commandSession_->valid || !commandSession_->drawing->complete)) {
        emit statusMessage(tr("Finish a valid 2D element before confirming."));return;
    }
    if(confirm && commandSession_->overdraw && !commandSession_->valid) {
        emit statusMessage(tr("Choose at least one valid Overdraw piece before OK."));return;
    }
    const auto change=commandSession_->sketch;
    const auto projection=commandSession_->projection;
    closeSketchProjectionPanel();
    closeSketchDrawingPanel();closeOverdrawPanel();closeSketchConstructionPanel();clearSketchFeedback();
    commandSession_.reset();
    for(const auto& [id,p]:sketchPreviews_) context_->Remove(p,Standard_False);
    sketchPreviews_.clear();commandPanel_->hide();commandButtons_->hide();
    if(confirm && change->before!=change->after) {
        clearHistorySelection();
        PullChange operation{};operation.sketchChange=change;
        restoreSketchEntities(change->after);undoStack_.push_back(operation);redoStack_.clear();updateHistoryActions();
    } else {
        if(projection) {
            selectedSketchIds_.clear();
            for(const auto& e:sketchEntities_) if(projection->previousSketchSelection.contains(e.id) && sketchPickable(e)) selectedSketchIds_<<e.id;
        }
        rebuildSketchPresentations();
    }
    refreshSketchPicking(activeSketchWorkplaneId_.isEmpty());view_->Redraw();
}
void CadViewport::selectSketchEntities(const QStringList& ids) {
    if(commandSession_ && commandSession_->projection) {
        emit statusMessage(tr("Project 3D Geometry accepts 3D subshapes in the viewport; 2D objects are not projection sources."));return;
    }
    if(commandSession_ && commandSession_->lineConstruction && commandSession_->lineConstruction->pickRole>=0) {
        if(ids.size()!=1 || !acceptSketchConstructionReference(ids.front())) emit statusMessage(tr("Choose one eligible 2D reference on the active Workplane."));
        return;
    }
    cancelCommandSession();initializeViewer();
    selectedPartIndices_.clear();selectedConstructionIds_.clear();selectedSketchIds_.clear();
    context_->ClearSelected(Standard_False);
    for(const auto& id:ids) for(const auto& e:sketchEntities_) if(e.id==id && !selectedSketchIds_.contains(id)) {
        selectedSketchIds_.push_back(id);
        if(sketchPickable(e)) context_->AddOrRemoveSelected(sketchPresentations_.at(id),Standard_False);
    }
    emit partSelectionChanged({});emit constructionSelectionChanged({});emit sketchSelectionChanged(selectedSketchIds_);refreshSketchPicking();view_->Redraw();
}
bool CadViewport::updateSketchEntity(const Sketch::Entity& entity,QString& error) {
    auto entities=sketchEntities_;
    auto it=std::find_if(entities.begin(),entities.end(),[&](const auto& e){return e.id==entity.id;});
    if(it==entities.end()) {error=tr("The 2D object no longer exists.");return false;}
    *it=entity;
    const auto selection=selectedSketchIds_;
    if(!previewSketchEntities(entities,error,false)) return false;
    finishSketchEdit(true);selectSketchEntities(selection);return true;
}
bool CadViewport::deleteSketchEntities(const QStringList& ids,QString& error) {
    auto entities=sketchEntities_;
    std::erase_if(entities,[&](const auto& e){return ids.contains(e.id);});
    if(entities==sketchEntities_) {error.clear();return true;}
    if(!previewSketchEntities(entities,error,false)) return false;
    finishSketchEdit(true);return true;
}
bool CadViewport::sketchMousePress(const QPoint& point,Qt::KeyboardModifiers modifiers) {
    if(commandSession_ || modifiers&Qt::AltModifier || context_.IsNull()) return false;
    context_->MoveTo(point.x(),point.y(),view_,Standard_False);
    if(!context_->HasDetected()) return false;
    for(const auto& [id,p]:sketchPresentations_) if(p==context_->DetectedInteractive()) {
        auto ids=modifiers&Qt::ControlModifier?selectedSketchIds_:QStringList{};
        if(ids.contains(id)) ids.removeAll(id);else ids.push_back(id);
        selectSketchEntities(ids);return true;
    }
    return false;
}
void CadViewport::populateSketchMenu(QMenu& menu) {
    const auto ids=selectedSketchIds_;
    if(ids.empty()) return;
    for(const auto& pair:{std::pair{tr("Show"),true},std::pair{tr("Hide"),false}}) {
        auto* action=menu.addAction(pair.first);
        connect(action,&QAction::triggered,this,[this,ids,visible=pair.second] {
            auto entities=sketchEntities_;for(auto& e:entities) if(ids.contains(e.id)) e.visible=visible;
            QString error;if(previewSketchEntities(entities,error,false)) {finishSketchEdit(true);selectSketchEntities(ids);} else emit statusMessage(error);
        });
    }
    auto* lock=menu.addAction(tr("Toggle selection lock"));
    connect(lock,&QAction::triggered,this,[this,ids] {
        auto entities=sketchEntities_;for(auto& e:entities) if(ids.contains(e.id)) e.selectable=!e.selectable;
        QString error;if(previewSketchEntities(entities,error,false)) {finishSketchEdit(true);selectSketchEntities(ids);} else emit statusMessage(error);
    });
    auto* fit=menu.addAction(tr("Fit Selected"));fit->setObjectName("context.fit_selected");
    connect(fit,&QAction::triggered,this,[this,ids] {
        Bnd_Box bounds;
        for(const auto& id:ids) if(sketchPresentations_.contains(id)) BRepBndLib::Add(sketchPresentations_.at(id)->Shape(),bounds);
        if(!bounds.IsVoid()) {view_->FitAll(bounds,0.1,Standard_False);stabilizeViewCamera();view_->ZFitAll();updateConstructionLabels();view_->Redraw();}
    });
    auto* remove=menu.addAction(tr("Delete 2D Geometry"));remove->setObjectName("context.delete_sketch");
    connect(remove,&QAction::triggered,this,[this,ids] {QString error;if(!deleteSketchEntities(ids,error)) emit statusMessage(error);});
}
