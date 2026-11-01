#include "CadViewport.h"
#include "SketchProfileRevolution.h"
#include "PartPresentation.h"
#include "ProfileSweepSelection.h"
#include <QComboBox>
#include <QCheckBox>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLocale>
#include <QSignalBlocker>
#include <QUuid>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <algorithm>

void CadViewport::startProfileExtrusion() {startProfileSweep(false);}
void CadViewport::startProfileRevolution() {startProfileSweep(true);}
void CadViewport::startProfileSweep(bool revolving) {
    const auto* owner=constructionObject(activeSketchWorkplaneId_);
    if(!owner || owner->kind!=Construction::Kind::Workplane || !owner->visible || !owner->selectable) {
        emit statusMessage(tr("%1: activate a visible, unlocked Workplane with Edit Sketch first.").arg(revolving?tr("Revolve"):tr("Extrude")));return;
    }
    const auto plane=*owner;
    if(commandSession_)cancelCommandSession();else cancelTargetPicking();
    initializeViewer();
    std::vector<Handle(SelectMgr_EntityOwner)> retainedOwners;
    for(context_->InitSelected();context_->MoreSelected();context_->NextSelected())retainedOwners.push_back(context_->SelectedOwner());
    resetModelingTools();
    commandSession_.emplace();auto& session=*commandSession_;session.kind=revolving?CommandSession::Kind::Revolve:CommandSession::Kind::Extrude;session.valid=false;
    session.selection=selectedPartIndices_;session.sweep=std::make_shared<ProfileSweepState>();auto& state=*session.sweep;
    state.workplane=plane;state.sketches=sketchEntities_;state.originals=boxes_;state.sketchSelection=selectedSketchIds_;state.constructionSelection=selectedConstructionIds_;
    session.owners=std::move(retainedOwners);
    const auto validated=SketchProfileValidation::validate(state.sketches,plane.id);
    if(validated.status!=SketchProfileValidation::Status::Valid) state.sourceError=validated.diagnostics.empty()?tr("Invalid Profile."):validated.diagnostics.front().message;
    else {
        state.faces=SketchProfileBuilder::build(validated,plane.frame);
        if(!state.faces.success)state.sourceError=state.faces.diagnostics.empty()?tr("Profile face construction failed."):state.faces.diagnostics.front().message;
    }
    sweepControls_=new QWidget(commandPanel_);auto* layout=new QVBoxLayout(sweepControls_);auto* form=new QFormLayout;layout->addLayout(form);
    auto* source=new QLabel(plane.name,sweepControls_);source->setTextFormat(Qt::PlainText);form->addRow(tr("Workplane"),source);
    auto* operation=new QComboBox(sweepControls_);operation->setObjectName("extrusionOperation");
    operation->addItem(tr("New Solid"),int(ModelingOperation::NewSolid));
    operation->addItem(tr("Add Material"),int(ModelingOperation::AddMaterial));
    operation->addItem(tr("Remove Material"),int(ModelingOperation::RemoveMaterial));form->addRow(tr("Operation"),operation);
    auto* target=new QComboBox(sweepControls_);target->setObjectName("extrusionTarget");target->addItem(tr("Select target part"),-1);
    for(size_t i=0;i<boxes_.size();++i)if(isPartVisible(i)){target->addItem(partName(i),qulonglong(i));target->setItemData(target->count()-1,partId(i),Qt::UserRole+1);}
    form->addRow(tr("Target Part"),target);target->setEnabled(false);
    connect(operation,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,operation]{
        setExtrusionOperation(static_cast<ModelingOperation>(operation->currentData().toInt()));
    });
    connect(target,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,target]{if(target->currentIndex()>0)cancelTargetPicking();refreshProfileSweep();});
    if(revolving)createRevolutionControls(form);
    else {
        auto* depth=new QLineEdit(QLocale().toString(20.0,'f',2),sweepControls_);depth->setObjectName("extrusionDistance");form->addRow(tr("Distance (mm)"),depth);
        connect(depth,&QLineEdit::textChanged,this,[this]{refreshProfileSweep();});
    }
    auto* regionsLabel=new QLabel(tr("Profiles (select one or more regions)"),sweepControls_);regionsLabel->setObjectName("cutProfilesLabel");layout->addWidget(regionsLabel);
    auto* regions=new QListWidget(sweepControls_);regions->setObjectName("cutProfiles");
    regions->setSelectionMode(QAbstractItemView::MultiSelection);regions->setMaximumHeight(100);
    for(size_t i=0;i<state.faces.faces.size();++i){auto* item=new QListWidgetItem(tr("Profile region %1").arg(i+1),regions);item->setSelected(true);}
    layout->addWidget(regions);
    if(!revolving) {
        auto* mode=new QComboBox(sweepControls_);mode->setObjectName("cutMode");mode->addItems({tr("Blind"),tr("Through All")});layout->addWidget(mode);
        auto* reverse=new QCheckBox(tr("Reverse direction"),sweepControls_);reverse->setObjectName("cutReverse");layout->addWidget(reverse);
        connect(mode,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int i){
            sweepControls_->findChild<QLineEdit*>("extrusionDistance")->setEnabled(commandSession_->sweep->operation!=ModelingOperation::RemoveMaterial || i==0);refreshProfileSweep();
        });
        connect(reverse,&QCheckBox::toggled,this,[this]{refreshProfileSweep();});
    }
    auto* pick=new QPushButton(tr("Pick target part"),sweepControls_);pick->setObjectName("pickTargetPart");pick->setVisible(false);layout->addWidget(pick);
    connect(pick,&QPushButton::clicked,this,&CadViewport::pickExtrusionTarget);
    auto* status=new QLabel(sweepControls_);status->setObjectName("extrusionStatus");status->setTextFormat(Qt::PlainText);status->setWordWrap(true);status->setMaximumWidth(370);layout->addWidget(status);
    commandLayout_->insertWidget(0,sweepControls_);dragDimension_->hide();commandPanel_->setWindowTitle(revolving?tr("Revolve"):tr("Extrude"));commandButtons_->show();
    connect(regions,&QListWidget::itemSelectionChanged,this,[this]{refreshProfileSweep();});
    if(revolving)beginRevolutionAxisPick();else {refreshSketchPicking();setExtrusionOperation(ModelingOperation::NewSolid);}
    showCommandPanel(commandPanel_);
    auto* parameter=sweepControls_->findChild<QLineEdit*>(revolving?"revolutionAngle":"extrusionDistance");parameter->setFocus();parameter->selectAll();
}
QString CadViewport::extrusionTargetError(size_t index) const {
    const auto error=solidTargetError(index);if(!error.isEmpty())return error;
    if(commandSession_->sweep->operation==ModelingOperation::AddMaterial) {
        size_t count=0;for(TopExp_Explorer it(boxes_[index].shape,TopAbs_SOLID);it.More();it.Next())++count;
        if(count!=1)return tr("Add Material requires one connected target solid.");
    }
    return {};
}
void CadViewport::pickExtrusionTarget() {
    if(commandSession_->kind==CommandSession::Kind::Revolve)commandSession_->sweep->axisPicking=false;
    auto* target=sweepControls_->findChild<QComboBox*>("extrusionTarget");
    target->setCurrentIndex(0);
    refreshProfileSweep();
    if(commandSession_->kind==CommandSession::Kind::Revolve)refreshSketchPicking();
    beginTargetPicking([this](size_t i){return extrusionTargetError(i);},[this](const QString& id){
        if(const auto index=partIndexForId(id))chooseExtrusionTarget({*index});
    });
}
void CadViewport::setExtrusionOperation(ModelingOperation operation) {
    auto& state=*commandSession_->sweep;
    cancelTargetPicking();
    // Restore a hidden Remove preview target before changing mode or target.
    if(state.operation==ModelingOperation::RemoveMaterial && state.target && isPartVisible(*state.target))context_->Display(boxes_[*state.target].presentation,Standard_False);
    state.target.reset();state.operation=operation;
    const bool removing=operation==ModelingOperation::RemoveMaterial;
    const bool targeted=operation!=ModelingOperation::NewSolid;
    sweepControls_->findChild<QPushButton*>("pickTargetPart")->setVisible(targeted);
    if(commandSession_->kind==CommandSession::Kind::Revolve) {
        state.axisPicking=!state.axis.has_value();refreshSketchPicking();
    } else {
        for(const auto* name:{"cutProfilesLabel","cutProfiles","cutMode","cutReverse"})
            sweepControls_->findChild<QWidget*>(name)->setVisible(removing);
        auto* depth=sweepControls_->findChild<QLineEdit*>("extrusionDistance");
        depth->setEnabled(!removing || sweepControls_->findChild<QComboBox*>("cutMode")->currentIndex()==0);
        auto* form=qobject_cast<QFormLayout*>(sweepControls_->layout()->itemAt(0)->layout());
        qobject_cast<QLabel*>(form->labelForField(depth))->setText(removing?tr("Depth (mm)"):tr("Distance (mm)"));
    }
    auto* target=sweepControls_->findChild<QComboBox*>("extrusionTarget");
    {const QSignalBlocker blocker(target);target->setCurrentIndex(0);target->setEnabled(targeted);}
    QString error;
    if(targeted) {
        const auto active=initialTargetPart([this](size_t i){return extrusionTargetError(i);},error);
        if(active)chooseExtrusionTarget({*active});else pickExtrusionTarget();
    } else refreshProfileSweep();
    if(!error.isEmpty()) {
        const auto message=tr("Active Part cannot be used: %1 Select target part.").arg(error);
        sweepControls_->findChild<QLabel*>("extrusionStatus")->setText(message);emit statusMessage(message);
    }
}
void CadViewport::chooseExtrusionTarget(const std::vector<size_t>& indices) {
    if(!commandSession_ || !commandSession_->sweep || !sweepControls_)return;
    if(commandSession_->sweep->operation==ModelingOperation::NewSolid)return;
    auto* target=sweepControls_->findChild<QComboBox*>("extrusionTarget");
    if(indices.size()==1 && extrusionTargetError(indices.front()).isEmpty())cancelTargetPicking();
    if(indices.size()!=1) {target->setCurrentIndex(0);return;}
    target->setCurrentIndex(target->findData(QVariant::fromValue<qulonglong>(indices.front())));
    if(commandSession_->kind==CommandSession::Kind::Revolve && !commandSession_->sweep->axis) {
        commandSession_->sweep->axisPicking=true;refreshSketchPicking();
    }
}
void CadViewport::refreshProfileSweep() {
    if(!commandSession_ || !commandSession_->sweep || !sweepControls_)return;
    auto& session=*commandSession_;auto& state=*session.sweep;
    const bool revolving=session.kind==CommandSession::Kind::Revolve;
    if(state.operation==ModelingOperation::RemoveMaterial && state.target && isPartVisible(*state.target))context_->Display(boxes_[*state.target].presentation,Standard_False);
    if(!session.preview.IsNull()){context_->Remove(session.preview,Standard_False);session.preview.Nullify();}
    state.target.reset();
    session.candidate.Nullify();session.valid=false;state.after.reset();commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(false);
    auto* status=sweepControls_->findChild<QLabel*>("extrusionStatus");
    const auto fail=[&](const QString& error){status->setText(error);view_->Redraw();};
    if(!state.sourceError.isEmpty()) {fail(state.sourceError);return;}
    const auto* current=constructionObject(state.workplane.id);
    if(!current || !Construction::equivalent(*current,state.workplane) || sketchEntities_!=state.sketches){fail(tr("Source changed; cancel and restart the profile sweep."));return;}
    auto profiles=state.faces;
    if(revolving || state.operation==ModelingOperation::RemoveMaterial) {
        profiles.faces.clear();auto* regions=sweepControls_->findChild<QListWidget*>("cutProfiles");
        for(int i=0;i<regions->count();++i)if(regions->item(i)->isSelected())profiles.faces.push_back(state.faces.faces[size_t(i)]);
    }
    double depth=0;SketchProfileSweep::Result result;
    if(revolving) {
        if(!state.axis) {fail(tr("Select a sketch line, construction axis or straight Part edge for the rotation axis."));return;}
        if(!validateRevolutionAxis()) {fail(tr("Axis source changed or is no longer available; pick it again."));return;}
        bool ok=false;double angle=QLocale().toDouble(sweepControls_->findChild<QLineEdit*>("revolutionAngle")->text(),&ok);
        if(!ok) {fail(tr("Enter a signed angle from 1 to 360 degrees."));return;}
        if(sweepControls_->findChild<QCheckBox*>("revolutionReverse")->isChecked())angle=-angle;
        result=SketchProfileRevolution::revolve(profiles,*state.axis,angle);
    } else {
        bool ok=false;depth=QLocale().toDouble(sweepControls_->findChild<QLineEdit*>("extrusionDistance")->text(),&ok);
        if(!ok && !(state.operation==ModelingOperation::RemoveMaterial && sweepControls_->findChild<QComboBox*>("cutMode")->currentIndex()==1)){fail(tr("Enter a valid signed distance in millimeters."));return;}
        if(state.operation!=ModelingOperation::RemoveMaterial)result=SketchProfileExtrusion::extrude(state.faces,state.workplane.frame.Direction(),depth);
    }
    if((revolving || state.operation!=ModelingOperation::RemoveMaterial) && !result.success){fail(result.diagnostics.front().message);return;}
    try {
        if(state.operation!=ModelingOperation::NewSolid) {
            const auto data=sweepControls_->findChild<QComboBox*>("extrusionTarget")->currentData();
            const qlonglong index=data.isValid()?data.toLongLong():-1;
            if(index<0 || size_t(index)>=state.originals.size() || !isPartVisible(size_t(index))) {fail(tr("Select target part — click visible geometry, or use the target list / Structure Browser."));return;}
            const auto id=sweepControls_->findChild<QComboBox*>("extrusionTarget")->currentData(Qt::UserRole+1).toString();
            if(partIndexForId(id)!=std::optional<size_t>(size_t(index)) || !extrusionTargetError(size_t(index)).isEmpty()){fail(tr("Target Part is no longer eligible."));return;}
            state.target=size_t(index);const auto& original=state.originals[*state.target];
            if(!boxes_[*state.target].shape.IsSame(original.shape)){fail(tr("Target changed; cancel and restart."));return;}
            for(int row=1;row<=3;++row)for(int column=1;column<=4;++column)
                if(boxes_[*state.target].orientation.Value(row,column)!=original.orientation.Value(row,column)){fail(tr("Target placement changed; cancel and restart."));return;}
            if(state.operation==ModelingOperation::RemoveMaterial && revolving)result=SketchProfileExtrusion::removeMaterial(result,original.shape,original.orientation);
            else if(state.operation==ModelingOperation::RemoveMaterial) {
                result=SketchProfileExtrusion::cut(profiles,state.workplane.frame,original.shape,original.orientation,depth,
                    sweepControls_->findChild<QCheckBox*>("cutReverse")->isChecked(),
                    sweepControls_->findChild<QComboBox*>("cutMode")->currentIndex()==1);
            } else result=SketchProfileExtrusion::addMaterial(result,original.shape,original.orientation);
            if(!result.success){fail(result.diagnostics.front().message);return;}
            state.after=std::make_shared<CadPart>(original);
            state.after->shape=BRepBuilderAPI_Transform(result.shape,original.orientation.Inverted(),true).Shape();
            state.after->geometryKind=CadPart::GeometryKind::GeneralBRep;
            state.after->cylinderPrimitive=state.after->spherePrimitive=state.after->conePrimitive=false;
            Bnd_Box bounds;BRepBndLib::AddOptimal(state.after->shape,bounds,false,false);
            bounds.Get(state.after->min[0],state.after->min[1],state.after->min[2],state.after->max[0],state.after->max[1],state.after->max[2]);
            state.after->baseMin=state.after->min;state.after->baseMax=state.after->max;
        }
        session.candidate=result.shape;session.preview=new AIS_Shape(result.shape);session.preview->SetColor(Quantity_Color(0.1,0.65,1.0,Quantity_TOC_RGB));session.preview->SetTransparency(.35);
        context_->Display(session.preview,Standard_False);context_->SetDisplayMode(session.preview,AIS_Shaded,Standard_False);
        context_->SetZLayer(session.preview,Graphic3d_ZLayerId_Topmost);context_->Deactivate(session.preview);
        if(state.operation==ModelingOperation::RemoveMaterial && state.target)context_->Erase(boxes_[*state.target].presentation,Standard_False);
        session.valid=true;session.state=CommandSession::State::Preview;commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(true);
        status->setText(tr("Blue preview: %1 solid(s), %2 mm³. OK confirms one operation.").arg(result.solidCount).arg(result.volume,0,'g',10));view_->ZFitAll();view_->Redraw();
    } catch(const Standard_Failure& e) {session.candidate.Nullify();if(!session.preview.IsNull()){context_->Remove(session.preview,Standard_False);session.preview.Nullify();}fail(QString::fromUtf8(e.GetMessageString()));}
}
void CadViewport::finishProfileSweep(bool confirm) {
    if(!commandSession_ || !commandSession_->sweep)return;
    if(confirm){refreshProfileSweep();if(!commandSession_->valid)return;}
    cancelTargetPicking();
    auto session=std::move(*commandSession_);commandSession_.reset();const auto state=session.sweep;
    if(state->operation==ModelingOperation::RemoveMaterial && state->target && isPartVisible(*state->target))context_->Display(boxes_[*state->target].presentation,Standard_False);
    if(!session.preview.IsNull())context_->Remove(session.preview,Standard_False);
    delete sweepControls_;sweepControls_=nullptr;commandPanel_->hide();commandButtons_->hide();resetModelingTools();
    if(confirm) {
        clearHistorySelection();
        if(state->operation!=ModelingOperation::NewSolid) {
            PullChange change{};change.partIndex=*state->target;
            change.faceChange=std::make_shared<FaceChange>(FaceChange{state->originals[*state->target],*state->after});
            applyFaceState(*state->target,*state->after);undoStack_.push_back(change);redoStack_.clear();updateHistoryActions();
            emit partsChanged();selectParts({*state->target});
        } else {
            auto change=std::make_shared<BooleanChange>();change->resultState.geometryKind=CadPart::GeometryKind::GeneralBRep;
            auto& part=change->resultState;part.shape=session.candidate;part.name=(session.kind==CommandSession::Kind::Revolve?tr("Revolve %1"):tr("Extrude %1")).arg(partCount_+1);part.persistentId=QUuid::createUuid().toString(QUuid::WithoutBraces);
            part.presentation=new PartPresentation(part.shape);Bnd_Box bounds;BRepBndLib::AddOptimal(part.shape,bounds,false,false);
            bounds.Get(part.min[0],part.min[1],part.min[2],part.max[0],part.max[1],part.max[2]);part.baseMin=part.min;part.baseMax=part.max;
            change->beforeSelection=session.selection;commitBooleanChange(change);
        }
        if(session.kind!=CommandSession::Kind::Revolve)emit statusMessage((state->operation==ModelingOperation::RemoveMaterial)?tr("Extrude Remove Material confirmed. Target identity and source profiles preserved."):tr("Extrude confirmed. Source Sketch and Workplane were preserved."));
    }
    if(!confirm && session.kind!=CommandSession::Kind::Revolve) {
        selectedPartIndices_=session.selection;selectedSketchIds_=state->sketchSelection;refreshPartSelection();
        for(const auto& entity:sketchEntities_) if(selectedSketchIds_.contains(entity.id) && sketchPickable(entity) && sketchPresentations_.contains(entity.id)
            && !context_->IsSelected(sketchPresentations_.at(entity.id))) context_->AddOrRemoveSelected(sketchPresentations_.at(entity.id),Standard_False);
        emit partSelectionChanged(selectedPartIndices_);emit sketchSelectionChanged(selectedSketchIds_);
    }
    if(session.kind==CommandSession::Kind::Revolve) {
        // New-Part commit uses the normal Boolean history path, which selects
        // its result. Restore Revolve's original editing/selection context.
        const auto* plane=constructionObject(state->workplane.id);
        if(plane && plane->visible && plane->selectable)activeSketchWorkplaneId_=plane->id;
        selectedPartIndices_=session.selection;selectedSketchIds_=state->sketchSelection;
        selectedConstructionIds_=state->constructionSelection;
        if(confirm && state->target) {
            const auto presentation=boxes_[*state->target].presentation;
            // Target topology changed: discard stale local face/edge owners.
            // A prior whole-Part selection follows its existing presentation.
            std::erase_if(session.owners,[&](const auto& owner){return !owner.IsNull() && owner->Selectable()==presentation;});
            if(std::find(session.selection.begin(),session.selection.end(),*state->target)!=session.selection.end())session.owners.push_back(presentation->GlobalSelOwner());
        }
        refreshSketchPicking(true);
        ProfileSweepSelection::restore(context_,session.owners,state->sketchSelection,sketchPresentations_);
        emit partSelectionChanged(selectedPartIndices_);emit sketchSelectionChanged(selectedSketchIds_);
        context_->ClearDetected(Standard_False);
        emit activeSketchWorkplaneChanged(activeSketchWorkplaneId_);emit constructionSelectionChanged(selectedConstructionIds_);
        updateConstructionLabels();
        if(confirm)emit statusMessage(tr("Revolve confirmed. Source profiles and axis were preserved."));
    }
    if(session.kind!=CommandSession::Kind::Revolve)refreshSketchPicking(true);
    view_->Redraw();setFocus();
}
