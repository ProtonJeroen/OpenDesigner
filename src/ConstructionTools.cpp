#include "CadViewport.h"
#include "ConstructionPanel.h"
#include <QAction>
#include <QMenu>
#include <QSignalBlocker>
#include <QFontDatabase>
#include <QFontInfo>
#include <QUuid>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp_Explorer.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_PointAspect.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Bnd_Box.hxx>
#include <gp_Pln.hxx>
#include <gp_Circ.hxx>
#include <ElCLib.hxx>
#include <cmath>
#include <algorithm>
#include <limits>
#include <numeric>
#include <Aspect_TypeOfDisplayText.hxx>

using namespace Construction;
namespace {
constexpr double pi=3.14159265358979323846;
bool matches(Kind kind,ReferenceKind role) {
    return (kind==Kind::Point && role==ReferenceKind::Point) || (kind==Kind::Axis && role==ReferenceKind::Axis)
        || (kind==Kind::Workplane && role==ReferenceKind::Plane);
}
}
const Object* CadViewport::constructionObject(const QString& id) const {
    for(const auto& object:constructions_) if(object.id==id) return &object;
    return nullptr;
}
Handle(AIS_Shape) CadViewport::constructionPresentation(const Object& object,bool preview,const std::vector<Sketch::Entity>* displayEntities) const {
    TopoDS_Shape shape;
    auto frame=object.frame;frame.SetLocation(displayCenter(object));
    const auto arrow=[&](const gp_Pnt& origin,const gp_Dir& direction,double length) {
        TopoDS_Compound compound;BRep_Builder builder;builder.MakeCompound(compound);
        builder.Add(compound,BRepBuilderAPI_MakeEdge(origin,origin.Translated(gp_Vec(direction)*length)).Shape());
        builder.Add(compound,BRepPrimAPI_MakeCone(gp_Ax2(origin.Translated(gp_Vec(direction)*(length*0.9)),direction),length*0.035,0,length*0.1).Shape());
        return compound;
    };
    if(object.kind==Kind::Workplane) {
        const auto outer=workplaneDisplayRectangle(object,displayEntities);
        const Sketch::DisplayRectangle opening{outer.minX+Sketch::WorkplaneBorderWidth,outer.minY+Sketch::WorkplaneBorderWidth,
                                               outer.maxX-Sketch::WorkplaneBorderWidth,outer.maxY-Sketch::WorkplaneBorderWidth};
        shape=Sketch::workplaneBorder(opening,object.frame);
    } else if(object.kind==Kind::Axis) {
        TopoDS_Compound compound;BRep_Builder builder;builder.MakeCompound(compound);
        builder.Add(compound,arrow(frame.Location().Translated(gp_Vec(frame.Direction())*(-object.extent)),frame.Direction(),2*object.extent));shape=compound;
    }
    else shape=BRepBuilderAPI_MakeVertex(frame.Location()).Shape();
    Handle(AIS_Shape) result=new AIS_Shape(shape);
    // AIS_Shape renders infinite presentations as wireframe even in AIS_Shaded.
    // The Workplane ring is a finite face, independent of its analytical plane.
    result->SetInfiniteState(object.kind!=Kind::Workplane);
    result->SetColor(preview?Quantity_Color(0.1,0.65,1.0,Quantity_TOC_RGB):object.kind==Kind::Workplane
        ?workplaneColor(object.id):object.kind==Kind::Axis
        ?Quantity_Color(0.3,0.9,0.4,Quantity_TOC_RGB):Quantity_Color(1.0,0.85,0.15,Quantity_TOC_RGB));
    result->SetWidth(2.0);
    if(object.kind==Kind::Axis) result->SetDisplayMode(AIS_Shaded);
    if(object.kind==Kind::Workplane) {
        result->SetColor(workplaneColor(object.id));result->SetDisplayMode(AIS_Shaded);result->SetTransparency(0.5);
        // This reference surface uses its UI color, without a specular highlight
        // washing green/light gray out when viewed normal to the plane.
        result->Attributes()->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
    }
    if(object.kind==Kind::Point) result->Attributes()->SetPointAspect(new Prs3d_PointAspect(Aspect_TOM_O_PLUS,preview?Quantity_Color(0.1,0.65,1.0,Quantity_TOC_RGB):Quantity_Color(Quantity_NOC_YELLOW),5.0));
    return result;
}
Quantity_Color CadViewport::workplaneColor(const QString& id) const {
    return id==activeSketchWorkplaneId_?Quantity_Color(0.25,0.8,0.35,Quantity_TOC_RGB):Quantity_Color(0.75,0.75,0.75,Quantity_TOC_RGB);
}
Handle(AIS_TextLabel) CadViewport::constructionLabel(const Object& object,bool preview) const {
    Handle(AIS_TextLabel) label=new AIS_TextLabel;
    // OCCT's generic serif alias can fall back to a symbol-only font on KDE.
    // Resolve the platform UI family through Qt before giving it to OCCT.
    label->SetFont(QFontInfo(QFontDatabase::systemFont(QFontDatabase::GeneralFont)).family().toUtf8().constData());
    label->SetText(TCollection_ExtendedString(object.name.toStdU16String().c_str()));
    label->SetPosition(displayCenter(object));label->SetHeight(object.kind==Kind::Workplane?12:14);label->SetZoomable(false);
    label->SetColor(preview?Quantity_Color(0.1,0.65,1.0,Quantity_TOC_RGB):object.kind==Kind::Workplane
        ?Quantity_Color(0.3,0.9,0.9,Quantity_TOC_RGB):object.kind==Kind::Axis
        ?Quantity_Color(0.3,0.9,0.4,Quantity_TOC_RGB):Quantity_Color(Quantity_NOC_YELLOW));
    if(object.kind==Kind::Workplane) label->SetColor(workplaneColor(object.id));
    label->SetDisplayType(Aspect_TODT_SUBTITLE);label->SetColorSubTitle(Quantity_Color(Quantity_NOC_GRAY20));
    label->SetInfiniteState(true);label->SetZLayer(Graphic3d_ZLayerId_Topmost);
    label->SetHJustification(object.kind==Kind::Point?Graphic3d_HTA_LEFT:Graphic3d_HTA_RIGHT);
    label->SetVJustification(object.kind==Kind::Point?Graphic3d_VTA_BOTTOM:Graphic3d_VTA_TOP);
    return label;
}
void CadViewport::updateConstructionLabels() {
    if(view_.IsNull() || context_.IsNull()) return;
    refreshWorkplaneDisplay();
    const gp_Vec right=gp_Vec(view_->Camera()->Direction()).Crossed(gp_Vec(view_->Camera()->Up())).Normalized();
    const gp_Vec up(view_->Camera()->Up());const double padding=view_->Convert(7);
    const auto update=[&](const Object& object,const Handle(AIS_TextLabel)& label) {
        bool restyle=false;
        if(object.kind==Kind::Workplane) {
            Quantity_Color previous;label->Color(previous);const auto color=workplaneColor(object.id);
            if(previous.Distance(color)>1.e-9) {label->SetColor(color);restyle=true;}
        }
        const auto bounds=constructionDisplayCorners(object);auto anchor=bounds.front();
        if(object.kind==Kind::Workplane) {
            // Lowest-left projected corner, recalculated as the camera rotates.
            int best=std::numeric_limits<int>::min();
            for(const auto& corner:bounds) {int x,y;view_->Convert(corner.X(),corner.Y(),corner.Z(),x,y);
                if(y-x>best) {best=y-x;anchor=corner;}}
        }
        anchor.Translate((right+up)*(object.kind==Kind::Point?padding:-padding));
        if(restyle || !label->Position().IsEqual(anchor,1.e-10)) {label->SetPosition(anchor);context_->Redisplay(label,Standard_False);}
    };
    for(const auto& object:constructions_) if(auto it=constructionLabels_.find(object.id);it!=constructionLabels_.end()) update(object,it->second);
    if(!constructionPreviewLabel_.IsNull() && commandSession_ && commandSession_->construction)
        update(commandSession_->construction->candidate,constructionPreviewLabel_);
}
void CadViewport::rebuildConstructionPresentations() {
    if(context_.IsNull()) return;
    // Remove owners before replacing their presentation objects.
    context_->ClearSelected(Standard_False);context_->ClearDetected(Standard_False);
    for(const auto& [id,presentation]:constructionPresentations_) context_->Remove(presentation,Standard_False);
    constructionPresentations_.clear();
    for(const auto& [id,label]:constructionLabels_) context_->Remove(label,Standard_False);
    constructionLabels_.clear();
    for(const auto& object:constructions_) {
        auto presentation=constructionPresentation(object);constructionPresentations_[object.id]=presentation;
        auto label=constructionLabel(object);constructionLabels_[object.id]=label;
        if(object.visible) {
            context_->Display(label,0,-1,Standard_False);context_->Deactivate(label);
            context_->Display(presentation,Standard_False);context_->Deactivate(presentation);
            if(object.selectable) context_->Activate(presentation,object.kind==Kind::Workplane?AIS_Shape::SelectionMode(TopAbs_EDGE):0,Standard_False);
        }
    }
    updateConstructionLabels();
    if(!view_.IsNull()) {view_->Invalidate();view_->Redraw();}
}
void CadViewport::restoreConstructions(const std::vector<Object>& objects) {
    constructions_=objects;validateActiveSketchContext();advanceNameCounters(objects,constructionNameCounters_);rebuildConstructionPresentations();rebuildSketchPresentations();emit constructionsChanged();emit sketchEntitiesChanged();
}
bool CadViewport::commitConstructions(const std::vector<Object>& objects) {
    bool same=objects.size()==constructions_.size();
    if(same) for(size_t i=0;i<objects.size();++i) if(!equivalent(objects[i],constructions_[i])) {same=false;break;}
    if(same) return false;
    auto retained=sketchEntities_;
    std::erase_if(retained,[&](const auto& e){return std::none_of(objects.begin(),objects.end(),[&](const auto& o){return o.id==e.workplaneId && o.kind==Kind::Workplane;});});
    PullChange change{};change.constructionChange=std::make_shared<ConstructionChange>(ConstructionChange{constructions_,objects,sketchEntities_,retained});
    sketchEntities_=std::move(retained);
    restoreConstructions(objects);undoStack_.push_back(change);redoStack_.clear();updateHistoryActions();return true;
}
void CadViewport::selectConstructions(const QStringList& ids) {
    if(commandSession_ && commandSession_->kind==CommandSession::Kind::Revolve) {
        if(commandSession_->sweep->axisPicking && ids.size()==1)chooseRevolutionAxisId(ids.front());
        else emit statusMessage(tr("Use Pick rotation axis to choose a construction axis."));
        return;
    }
    if(!activeSketchWorkplaneId_.isEmpty() && ids.size()==1) {
        const auto* owner=constructionObject(ids.front());
        if(owner && owner->kind==Kind::Workplane && owner->id!=activeSketchWorkplaneId_) activateSketchWorkplane(owner->id);
    }
    if(commandSession_ && commandSession_->kind==CommandSession::Kind::Construction) {
        auto& s=*commandSession_->construction;
        if(s.pickRole>=0) {
            if(ids.empty() || !acceptConstructionChoice(size_t(s.pickRole),ids.front()))
                emit statusMessage(tr("This object cannot supply the requested reference."));
            return;
        }
    }
    cancelCommandSession();clearSketchSelection();initializeViewer();selectedPartIndices_.clear();selectedConstructionIds_.clear();
    context_->ClearSelected(Standard_False);
    for(const auto& id:ids) if(const auto* object=constructionObject(id)) {
        if(selectedConstructionIds_.contains(id)) continue;
        selectedConstructionIds_.push_back(id);
        if(object->visible && object->selectable) context_->AddOrRemoveSelected(constructionPresentations_.at(id),Standard_False);
    }
    emit partSelectionChanged({});emit constructionSelectionChanged(selectedConstructionIds_);refreshSketchPicking();view_->Redraw();
}
bool CadViewport::updateConstruction(const Object& object) {
    QString error;if(!constructionObject(object.id) || !valid(object,error)) {emit statusMessage(error);return false;}
    cancelCommandSession();auto objects=constructions_;
    for(auto& entry:objects) if(entry.id==object.id) entry=object;
    const auto selection=selectedConstructionIds_;const bool changed=commitConstructions(objects);selectConstructions(selection);return changed;
}
void CadViewport::deleteConstructions(const QStringList& ids) {
    cancelCommandSession();auto objects=constructions_;
    std::erase_if(objects,[&](const Object& object){return ids.contains(object.id);});
    selectedConstructionIds_.clear();commitConstructions(objects);emit constructionSelectionChanged({});
}
void CadViewport::fitConstructions(const QStringList& ids) {
    initializeViewer();Bnd_Box bounds;
    for(const auto& id:ids) if(const auto* object=constructionObject(id)) {
        for(const auto& p:constructionDisplayCorners(*object)) {
            const double e=object->kind==Kind::Point?5.0:0;
            bounds.Add(gp_Pnt(p.X()-e,p.Y()-e,p.Z()-e));bounds.Add(gp_Pnt(p.X()+e,p.Y()+e,p.Z()+e));
        }
    }
    if(!bounds.IsVoid()) {view_->FitAll(bounds,0.05,Standard_False);stabilizeViewCamera();view_->ZFitAll();updateConstructionLabels();view_->Redraw();}
}
void CadViewport::populateConstructionMenu(QMenu& menu) {
    const auto ids=selectedConstructionIds_;
    if(ids.size()==1) if(const auto* object=constructionObject(ids.front())) {
        const auto add=[&](const QString& label,Method method) {auto* a=menu.addAction(label);connect(a,&QAction::triggered,this,[this,method,id=object->id]{startConstruction(method,id);});};
        if(object->kind==Kind::Workplane) {
            auto* edit=menu.addAction(tr("Edit Sketch"));edit->setObjectName("context.edit_sketch");
            connect(edit,&QAction::triggered,this,[this,id=object->id]{activateSketchWorkplane(id);});
            auto* normal=menu.addAction(tr("Look at Workplane"));normal->setObjectName("context.look_at_workplane");
            connect(normal,&QAction::triggered,this,[this,id=object->id]{lookAtWorkplane(id);});
            add(tr("Move Workplane"),Method::PlaneMove);add(tr("Rotate Workplane"),Method::PlaneRotate);add(tr("Offset Workplane"),Method::PlaneOffset);add(tr("Normal Axis"),Method::AxisNormalPlane);}
        else if(object->kind==Kind::Axis) {add(tr("Move Axis"),Method::AxisMove);add(tr("Rotate Axis"),Method::AxisRotate);add(tr("Point on Axis"),Method::PointOnAxis);add(tr("Workplane Normal through Point"),Method::PlaneNormalAxisPoint);add(tr("Workplane through Axis and Point"),Method::PlaneThroughAxisPoint);add(tr("Workplane at Angle"),Method::PlaneAxisAngle);}
        else {add(tr("Offset Point"),Method::PointOffset);add(tr("Edit Coordinates"),Method::PointXYZ);add(tr("Axis and Direction"),Method::AxisPointDirection);add(tr("Project Point"),Method::PointProject);}
        auto* lock=menu.addAction(object->selectable?tr("Lock selection"):tr("Unlock selection"));
        connect(lock,&QAction::triggered,this,[this,state=*object]() mutable {state.selectable=!state.selectable;updateConstruction(state);});
    }
    if(!ids.empty() && ids.size()<=2 && std::all_of(ids.begin(),ids.end(),[this](const QString& id) {
        const auto* object=constructionObject(id);return object && object->kind==Kind::Workplane && object->visible && object->selectable;
    })) {
        auto* midplane=menu.addAction(tr("Midplane Workplane"));midplane->setObjectName("context.midplane");
        connect(midplane,&QAction::triggered,this,[this]{startConstruction(Method::PlaneMidplane);});
    }
    if(ids.isEmpty()) return;
    auto* hide=menu.addAction(tr("Hide"));auto* show=menu.addAction(tr("Show"));
    const auto visibility=[this,ids](bool visible) {cancelCommandSession();auto objects=constructions_;for(auto& object:objects) if(ids.contains(object.id)) object.visible=visible;commitConstructions(objects);selectConstructions(ids);};
    connect(hide,&QAction::triggered,this,[visibility] {visibility(false);});connect(show,&QAction::triggered,this,[visibility]{visibility(true);});
    auto* fit=menu.addAction(tr("Fit Selected"));fit->setObjectName("context.fit_selected");
    connect(fit,&QAction::triggered,this,[this,ids]{fitConstructions(ids);});
    connect(menu.addAction(tr("Delete Construction")),&QAction::triggered,this,[this,ids]{deleteConstructions(ids);});
}
void CadViewport::startConstruction(Method method,const QString& targetId) {
    endSketchEditing();
    cancelCommandSession();initializeViewer();
    auto session=std::make_shared<ConstructionSessionState>();session->request.method=method;session->selection=selectedConstructionIds_;
    const auto& d=definition(method);
    const auto* target=constructionObject(d.edit && targetId.isEmpty() && !selectedConstructionIds_.isEmpty()?selectedConstructionIds_.front():targetId);
    if(!targetId.isEmpty() && (d.edit || method==Method::PointXYZ) && !target) {
        emit statusMessage(tr("The construction object no longer exists."));return;
    }
    if((d.edit || (method==Method::PointXYZ && !targetId.isEmpty())) && target
        && (target->kind!=d.kind || !target->visible || !target->selectable)) {emit statusMessage(tr("Select an object of the correct construction type."));return;}
    session->candidate.kind=d.kind;session->candidate.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    const bool editing=target && (d.edit || method==Method::PointXYZ);
    if(!editing && constructionNameCounters_[size_t(d.kind)]==std::numeric_limits<int>::max()) {
        emit statusMessage(tr("The construction name sequence is exhausted."));return;
    }
    if(!editing) session->candidate.name=namePrefix(d.kind)+QString::number(constructionNameCounters_[size_t(d.kind)]+1);
    if(target && (d.edit || method==Method::PointXYZ)) {session->targetId=target->id;session->candidate=*target;}
    commandSession_.emplace();commandSession_->kind=CommandSession::Kind::Construction;commandSession_->construction=session;
    commandSession_->selection=selectedPartIndices_;
    for(context_->InitSelected();context_->MoreSelected();context_->NextSelected()) commandSession_->owners.push_back(context_->SelectedOwner());
    constructionPanel_=new ConstructionPanel(d,this);constructionPanel_->name->setText(session->candidate.name);
    session->choices.resize(d.roles.size());
    for(size_t role=0;role<d.roles.size();++role) {
        auto* combo=constructionPanel_->roles[role];auto& choices=session->choices[role];
        for(const auto& object:constructions_) if(object.visible && object.selectable && matches(object.kind,d.roles[role].kind)) {
            choices.push_back({object.id,object.name,object.id,reference(object)});combo->addItem(object.name,object.id);
        }
        if(!d.edit) for(const auto& part:boxes_) if(part.active && part.visible) {
            const auto expected=d.roles[role].kind;
            const auto type=expected==ReferenceKind::Point?TopAbs_VERTEX:(expected==ReferenceKind::Plane || expected==ReferenceKind::Cylinder)?TopAbs_FACE:TopAbs_EDGE;
            if(expected==ReferenceKind::Axis) continue;
            int ordinal=0;
            for(TopExp_Explorer it(part.shape,type);it.More();it.Next()) {
                ++ordinal;Reference ref;QString error;
                if(!resolve(it.Current(),part.orientation,expected,ref,error)) continue;
                const QString key=part.persistentId+":"+QString::number(int(type))+":"+QString::number(ordinal);
                const QString label=part.name+" / "+(type==TopAbs_FACE?"Face ":type==TopAbs_EDGE?"Edge ":"Vertex ")+QString::number(ordinal);
                choices.push_back({key,label,{},ref});combo->addItem(label,key);
            }
        }
        const QString preselect=target?target->id:(role<size_t(selectedConstructionIds_.size())?selectedConstructionIds_[int(role)]:QString());
        const int index=combo->findData(preselect);if(index>0) combo->setCurrentIndex(index);
    }
    // Assign each selected subshape to at most one operand role.
    std::vector<Handle(SelectMgr_EntityOwner)> usedOwners;
    for(size_t role=0;role<d.roles.size();++role) if(constructionPanel_->roles[role]->currentIndex()==0 && !d.edit) {
        for(context_->InitSelected();context_->MoreSelected();context_->NextSelected()) {
            const auto owner=Handle(StdSelect_BRepOwner)::DownCast(context_->SelectedOwner());
            if(owner.IsNull() || std::find(usedOwners.begin(),usedOwners.end(),context_->SelectedOwner())!=usedOwners.end()) continue;
            bool assigned=false;
            for(const auto& part:boxes_) if(part.presentation==context_->SelectedInteractive()) {
                Reference ref;QString error;
                if(resolve(owner->Shape(),part.orientation,d.roles[role].kind,ref,error)) {
                    const QString key="preselection:"+QString::number(role);
                    session->choices[role].push_back({key,tr("Selected geometry"),{},ref});
                    constructionPanel_->roles[role]->addItem(tr("Selected geometry"),key);
                    constructionPanel_->roles[role]->setCurrentIndex(constructionPanel_->roles[role]->count()-1);
                    usedOwners.push_back(context_->SelectedOwner());assigned=true;break;
                }
            }
            if(assigned) break;
        }
    }
    if(method==Method::AxisPointDirection || method==Method::PlaneRotate || method==Method::AxisRotate) constructionPanel_->setVector(gp_Vec(0,0,1));
    if(method==Method::PointXYZ && target) constructionPanel_->setVector(gp_Vec(target->frame.Location().XYZ()));
    constructionPanel_->changed=[this]{previewConstruction();};
    constructionPanel_->confirmed=[this]{confirmCommandSession();};constructionPanel_->cancelled=[this]{cancelCommandSession();};
    constructionPanel_->pickRequested=[this](size_t role){chooseConstructionRole(role);};
    showCommandPanel(constructionPanel_);previewConstruction();
}
bool CadViewport::acceptConstructionChoice(size_t role,const QString& key) {
    if(!constructionPanel_ || role>=constructionPanel_->roles.size()) return false;
    const int index=constructionPanel_->roles[role]->findData(key);if(index<1) return false;
    commandSession_->construction->pickRole=-1;restoreConstructionPicker();constructionPanel_->roles[role]->setCurrentIndex(index);previewConstruction();return true;
}
void CadViewport::chooseConstructionRole(size_t role) {
    if(!commandSession_ || !commandSession_->construction || role>=commandSession_->construction->choices.size()) return;
    auto& state=*commandSession_->construction;state.pickRole=int(role);
    // Replacing a reference must not silently confirm its old value with Enter.
    constructionPanel_->roles[role]->setCurrentIndex(0);
    const auto expected=definition(state.request.method).roles[role].kind;
    const auto type=expected==ReferenceKind::Point?TopAbs_VERTEX:(expected==ReferenceKind::Plane || expected==ReferenceKind::Cylinder)?TopAbs_FACE:TopAbs_EDGE;
    for(const auto& part:boxes_) if(part.active && part.visible) {
        context_->Deactivate(part.presentation);
        if(!definition(state.request.method).edit && expected!=ReferenceKind::Axis)
            context_->Activate(part.presentation,AIS_Shape::SelectionMode(type),Standard_False);
    }
    constructionPanel_->setValid(false,tr("Pick %1 in the viewport or Structure Browser.").arg(definition(state.request.method).roles[role].name));
    emit statusMessage(tr("Select the highlighted operand role; Esc cancels the command."));
}
void CadViewport::restoreConstructionPicker() {
    for(const auto& part:boxes_) if(part.active && part.visible) {
        context_->Deactivate(part.presentation);context_->Activate(part.presentation,AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
    }
}
bool CadViewport::captureConstructionReference(const QPoint& point) {
    auto& state=*commandSession_->construction;
    if(state.pickRole<0) return false;
    const size_t role=size_t(state.pickRole);context_->MoveTo(point.x(),point.y(),view_,Standard_True);
    if(!context_->HasDetected()) return true;
    const auto detected=context_->DetectedInteractive();
    for(const auto& [id,presentation]:constructionPresentations_) if(detected==presentation) {
        if(!acceptConstructionChoice(role,id)) constructionPanel_->setValid(false,tr("This construction cannot supply that operand."));
        return true;
    }
    const auto owner=Handle(StdSelect_BRepOwner)::DownCast(context_->DetectedOwner());
    if(owner.IsNull()) return true;
    for(const auto& part:boxes_) if(part.presentation==detected) {
        Reference ref;QString error;
        const auto expected=definition(state.request.method).roles[role].kind;
        if(!resolve(owner->Shape(),part.orientation,expected,ref,error)) {constructionPanel_->setValid(false,error);return true;}
        const QString key="picked:"+QString::number(role)+":"+QString::number(state.choices[role].size());
        state.choices[role].push_back({key,tr("Picked %1 geometry").arg(part.name),{},ref});
        constructionPanel_->roles[role]->addItem(tr("Picked %1 geometry").arg(part.name),key);acceptConstructionChoice(role,key);break;
    }
    return true;
}
bool CadViewport::previewConstruction() {
    if(!commandSession_ || !commandSession_->construction || !constructionPanel_) return false;
    auto& state=*commandSession_->construction;const auto& d=definition(state.request.method);
    if(state.pickRole>=0 && constructionPanel_->roles[size_t(state.pickRole)]->currentIndex()>0) {
        state.pickRole=-1;restoreConstructionPicker();
    }
    state.request=constructionPanel_->request();state.request.references.clear();QString error;
    for(size_t role=0;role<d.roles.size();++role) {
        const auto key=constructionPanel_->roles[role]->currentData().toString();
        const auto& choices=state.choices[role];const auto choice=std::find_if(choices.begin(),choices.end(),[&](const auto& entry){return entry.key==key;});
        if(choice==choices.end()) {error=tr("Select %1.").arg(d.roles[role].name);break;}
        state.request.references.push_back(choice->reference);
        if(d.edit && role==0) {
            const auto* original=constructionObject(choice->objectId);
            if(!original) {error=tr("Choose an existing construction object to edit.");break;}
            if(state.targetId!=original->id) constructionPanel_->name->setText(original->name);
            state.targetId=original->id;state.candidate=*original;
        }
    }
    state.candidate.name=constructionPanel_->name->text().trimmed();
    const bool validCandidate=error.isEmpty() && build(state.request,state.candidate,error) && valid(state.candidate,error);
    commandSession_->valid=validCandidate;commandSession_->state=validCandidate?CommandSession::State::Preview:CommandSession::State::Parameters;
    if(!commandSession_->preview.IsNull()) {context_->Remove(commandSession_->preview,Standard_False);commandSession_->preview.Nullify();}
    if(!constructionPreviewLabel_.IsNull()) {context_->Remove(constructionPreviewLabel_,Standard_False);constructionPreviewLabel_.Nullify();}
    for(const auto& [id,presentation]:constructionPresentations_) {
        const auto* object=constructionObject(id);
        if(object && object->visible) {
            context_->Display(constructionLabels_.at(id),0,-1,Standard_False);context_->Deactivate(constructionLabels_.at(id));
            context_->Display(presentation,Standard_False);context_->Deactivate(presentation);
            if(object->selectable) context_->Activate(presentation,object->kind==Kind::Workplane?AIS_Shape::SelectionMode(TopAbs_EDGE):0,Standard_False);
        }
    }
    for(const auto& [id,p]:sketchPreviews_) context_->Remove(p,Standard_False);
    sketchPreviews_.clear();
    for(const auto& entity:sketchEntities_) if(const auto* owner=constructionObject(entity.workplaneId)) {
        auto p=sketchPresentations_.at(entity.id);
        if(validCandidate && entity.workplaneId==state.targetId && state.candidate.kind==Kind::Workplane) {
            context_->Erase(p,Standard_False);
            auto preview=sketchPresentation(entity,state.candidate,true);sketchPreviews_[entity.id]=preview;
            displaySketchPresentation(entity,state.candidate,preview,true);
        } else displaySketchPresentation(entity,*owner,p);
    }
    clearConstructionHandles();
    if(validCandidate) {
        commandSession_->preview=constructionPresentation(state.candidate,true);
        constructionPreviewLabel_=constructionLabel(state.candidate,true);
        context_->Display(constructionPreviewLabel_,0,-1,Standard_False);context_->Deactivate(constructionPreviewLabel_);
        context_->Display(commandSession_->preview,-1,-1,Standard_False);context_->Deactivate(commandSession_->preview);
        if(!state.targetId.isEmpty()) {
            context_->Erase(constructionPresentations_.at(state.targetId),Standard_False);
            context_->Erase(constructionLabels_.at(state.targetId),Standard_False);
        }
        displayConstructionHandles();
    }
    constructionPanel_->setValid(validCandidate,validCandidate?tr("Preview — OK/Enter confirms; Cancel/Esc discards."):error);
    updateConstructionLabels();view_->Invalidate();view_->Redraw();return validCandidate;
}
void CadViewport::closeConstructionSession(bool confirm) {
    auto session=std::move(*commandSession_);commandSession_.reset();
    for(const auto& [id,p]:sketchPreviews_) context_->Remove(p,Standard_False);
    sketchPreviews_.clear();
    if(!session.preview.IsNull()) context_->Remove(session.preview,Standard_False);
    if(!constructionPreviewLabel_.IsNull()) {context_->Remove(constructionPreviewLabel_,Standard_False);constructionPreviewLabel_.Nullify();}
    clearConstructionHandles();constructionDragAxis_=-1;
    auto* panel=constructionPanel_;constructionPanel_=nullptr;
    panel->changed={};panel->cancelled={};panel->confirmed={};panel->hide();panel->deleteLater();
    restoreConstructionPicker();
    if(confirm && session.valid) {
        auto objects=constructions_;bool replaced=false;
        for(auto& object:objects) if(object.id==session.construction->targetId) {object=session.construction->candidate;replaced=true;break;}
        if(!replaced) {objects.push_back(session.construction->candidate);++constructionNameCounters_[size_t(session.construction->candidate.kind)];}
        if(!commitConstructions(objects)) rebuildSketchPresentations();
        selectConstructions({session.construction->candidate.id});
    } else {
        rebuildConstructionPresentations();rebuildSketchPresentations();selectedConstructionIds_=session.construction->selection;
        context_->ClearSelected(Standard_False);
        for(const auto& id:selectedConstructionIds_) if(const auto* object=constructionObject(id);object && object->visible && object->selectable)
            context_->AddOrRemoveSelected(constructionPresentations_.at(id),Standard_False);
        if(selectedConstructionIds_.isEmpty()) {
            if(!selectedSketchIds_.empty()) {
                // Sketch presentations were rebuilt; never restore their stale owners.
                for(const auto& entity:sketchEntities_) if(selectedSketchIds_.contains(entity.id) && sketchPickable(entity))
                    context_->AddOrRemoveSelected(sketchPresentations_.at(entity.id),Standard_False);
            } else for(const auto& owner:session.owners) context_->AddOrRemoveSelected(owner,Standard_False);
        }
        selectedPartIndices_=session.selection;emit partSelectionChanged(selectionContext().parts);emit constructionSelectionChanged(selectedConstructionIds_);
        emit sketchSelectionChanged(selectedSketchIds_);
    }
    resetModelingTools();view_->Invalidate();view_->Redraw();setFocus();
}
void CadViewport::clearConstructionHandles() {
    for(const auto& handle:constructionHandles_) if(!context_.IsNull()) context_->Remove(handle,Standard_False);
    constructionHandles_.clear();
}
void CadViewport::displayConstructionHandles() {
    const auto& state=*commandSession_->construction;const auto method=state.request.method;
    if(method!=Method::PlaneMove && method!=Method::AxisMove && method!=Method::PlaneRotate && method!=Method::AxisRotate) return;
    constructionRotating_=method==Method::PlaneRotate || method==Method::AxisRotate;
    constructionHandleLength_=std::max(1.e-4,view_->Convert(75));
    const auto center=state.request.references[0].frame.Location();
    for(int axis=0;axis<3;++axis) {
        const gp_Dir direction(axis==0?1:0,axis==1?1:0,axis==2?1:0);
        TopoDS_Shape shape;
        if(constructionRotating_) shape=BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(center,direction),constructionHandleLength_)).Shape();
        else {
            const auto origin=state.candidate.frame.Location();
            TopoDS_Compound arrow;BRep_Builder builder;builder.MakeCompound(arrow);
            const double length=constructionHandleLength_;
            builder.Add(arrow,BRepPrimAPI_MakeCylinder(gp_Ax2(origin,direction),length*0.025,length*0.75).Shape());
            builder.Add(arrow,BRepPrimAPI_MakeCone(gp_Ax2(origin.Translated(gp_Vec(direction)*length*0.75),direction),length*0.08,0,length*0.25).Shape());shape=arrow;
        }
        Handle(AIS_Shape) handle=new AIS_Shape(shape);handle->SetColor(Quantity_NOC_ORANGE);handle->SetWidth(3.0);
        handle->SetZLayer(Graphic3d_ZLayerId_Topmost);handle->SetInfiniteState(true);
        context_->Display(handle,constructionRotating_?AIS_WireFrame:AIS_Shaded,-1,Standard_False);context_->Deactivate(handle);constructionHandles_.push_back(handle);
    }
}
double CadViewport::constructionCursorAngle(const QPoint& point) const {
    const auto center=commandSession_->construction->request.references[0].frame.Location();
    const int axis=constructionDragAxis_;const gp_Dir normal(axis==0?1:0,axis==1?1:0,axis==2?1:0);
    const gp_Circ circle(gp_Ax2(center,normal),constructionHandleLength_);
    double best=1.e100,result=0;
    for(int i=0;i<720;++i) {const double angle=2*pi*i/720;const auto p=ElCLib::Value(angle,circle);int x,y;view_->Convert(p.X(),p.Y(),p.Z(),x,y);
        const double distance=std::hypot(point.x()-x,point.y()-y);if(distance<best) {best=distance;result=angle;}}
    return result;
}
bool CadViewport::constructionMousePress(const QPoint& point,Qt::KeyboardModifiers modifiers) {
    if(commandSession_ && commandSession_->kind==CommandSession::Kind::Construction) {
        if(captureConstructionReference(point)) return true;
        if(!constructionHandles_.empty() && !(modifiers&Qt::ControlModifier)) {
            const auto& state=*commandSession_->construction;
            const auto center=constructionRotating_?state.request.references[0].frame.Location():state.candidate.frame.Location();
            double best=12;int hit=-1;
            for(int axis=0;axis<3;++axis) {
                const gp_Dir normal(axis==0?1:0,axis==1?1:0,axis==2?1:0);
                const gp_Circ circle(gp_Ax2(center,normal),constructionHandleLength_);
                const int samples=constructionRotating_?180:20;
                for(int i=1;i<=samples;++i) {
                    const auto p=constructionRotating_?ElCLib::Value(2*pi*i/samples,circle):center.Translated(gp_Vec(normal)*(constructionHandleLength_*i/samples));
                    int x,y;view_->Convert(p.X(),p.Y(),p.Z(),x,y);const double distance=std::hypot(point.x()-x,point.y()-y);
                    if(distance<best) {best=distance;hit=axis;}
                }
            }
            if(hit>=0) {
                constructionDragAxis_=hit;constructionDragStart_=point;constructionDragBefore_=state.request.vector;constructionDragValue_=state.request.value;
                if(constructionRotating_) {
                    constructionPanel_->setVector(gp_Vec(hit==0?1:0,hit==1?1:0,hit==2?1:0));
                    constructionLastAngle_=constructionCursorAngle(point);constructionAccumulatedAngle_=0;
                }
                return true;
            }
        }
        cancelCommandSession();
        return constructionMousePress(point,modifiers);
    }
    if(modifiers&Qt::AltModifier || context_.IsNull()) return false;
    context_->MoveTo(point.x(),point.y(),view_,Standard_True);
    if(!context_->HasDetected()) return false;
    for(const auto& [id,presentation]:constructionPresentations_) if(presentation==context_->DetectedInteractive()) {
        QStringList selection=(modifiers&Qt::ControlModifier)?selectedConstructionIds_:QStringList();
        if(selection.contains(id)) selection.removeAll(id);else selection.push_back(id);
        selectConstructions(selection);return true;
    }
    return false;
}
bool CadViewport::constructionMouseMove(const QPoint& point,Qt::KeyboardModifiers modifiers) {
    if(constructionDragAxis_<0 || !commandSession_ || !commandSession_->construction) return false;
    if(constructionRotating_) {
        const double angle=constructionCursorAngle(point);double delta=angle-constructionLastAngle_;
        while(delta>pi) delta-=2*pi;while(delta<-pi) delta+=2*pi;
        constructionAccumulatedAngle_+=delta;constructionLastAngle_=angle;
        double degrees=constructionDragValue_+constructionAccumulatedAngle_*180/pi;
        if(rotationSnap_>0 && !(modifiers&Qt::ShiftModifier)) degrees=std::round(degrees/rotationSnap_)*rotationSnap_;
        constructionPanel_->setValue(degrees);
    } else {
        const auto center=commandSession_->construction->request.references[0].frame.Location();
        const int axis=constructionDragAxis_;const gp_Vec direction(axis==0?1:0,axis==1?1:0,axis==2?1:0);
        int x,y,xx,yy;view_->Convert(center.X(),center.Y(),center.Z(),x,y);const auto end=center.Translated(direction*constructionHandleLength_);view_->Convert(end.X(),end.Y(),end.Z(),xx,yy);
        const double dx=xx-x,dy=yy-y,denom=dx*dx+dy*dy;
        if(denom<4.0) return true;
        double distance=((point.x()-constructionDragStart_.x())*dx+(point.y()-constructionDragStart_.y())*dy)/denom*constructionHandleLength_;
        if(snapStepMm_>0 && !(modifiers&Qt::ShiftModifier)) distance=std::round(distance/snapStepMm_)*snapStepMm_;
        constructionPanel_->setVector(constructionDragBefore_+direction*distance);
    }
    return true;
}
void CadViewport::constructionMouseRelease() {constructionDragAxis_=-1;}
