#include "CadViewport.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLocale>
#include <QUuid>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Standard_Failure.hxx>

void CadViewport::startProfileExtrusion() {
    const auto* owner=constructionObject(activeSketchWorkplaneId_);
    if(!owner || owner->kind!=Construction::Kind::Workplane || !owner->visible || !owner->selectable) {
        emit statusMessage(tr("Extrude Profile: activate a visible, unlocked Workplane with Edit Sketch first."));return;
    }
    const auto plane=*owner;cancelCommandSession();initializeViewer();resetModelingTools();
    commandSession_.emplace();auto& session=*commandSession_;session.kind=CommandSession::Kind::Extrude;session.valid=false;
    session.selection=selectedPartIndices_;session.extrusion=std::make_shared<ProfileExtrusionState>();auto& state=*session.extrusion;
    state.workplane=plane;state.sketches=sketchEntities_;state.originals=boxes_;state.sketchSelection=selectedSketchIds_;
    const auto validated=SketchProfileValidation::validate(state.sketches,plane.id);
    if(validated.status!=SketchProfileValidation::Status::Valid) state.sourceError=validated.diagnostics.empty()?tr("Invalid Profile."):validated.diagnostics.front().message;
    else {
        state.faces=SketchProfileBuilder::build(validated,plane.frame);
        if(!state.faces.success)state.sourceError=state.faces.diagnostics.empty()?tr("Profile face construction failed."):state.faces.diagnostics.front().message;
    }
    extrusionControls_=new QWidget(commandPanel_);auto* layout=new QVBoxLayout(extrusionControls_);auto* form=new QFormLayout;layout->addLayout(form);
    auto* source=new QLabel(plane.name,extrusionControls_);source->setTextFormat(Qt::PlainText);form->addRow(tr("Workplane"),source);
    auto* operation=new QComboBox(extrusionControls_);operation->setObjectName("extrusionOperation");operation->addItems({tr("New Solid"),tr("Add Material")});form->addRow(tr("Operation"),operation);
    auto* target=new QComboBox(extrusionControls_);target->setObjectName("extrusionTarget");target->addItem(tr("Choose one target Part"),-1);
    for(size_t i=0;i<boxes_.size();++i)if(isPartVisible(i))target->addItem(partName(i),qulonglong(i));
    form->addRow(tr("Target Part"),target);target->setEnabled(false);
    auto* depth=new QLineEdit(QLocale().toString(20.0,'f',2),extrusionControls_);depth->setObjectName("extrusionDistance");form->addRow(tr("Distance (mm)"),depth);
    auto* status=new QLabel(extrusionControls_);status->setObjectName("extrusionStatus");status->setTextFormat(Qt::PlainText);status->setWordWrap(true);status->setMaximumWidth(370);layout->addWidget(status);
    commandLayout_->insertWidget(0,extrusionControls_);dragDimension_->hide();commandPanel_->setWindowTitle(tr("Extrude Profile"));commandButtons_->show();
    connect(operation,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,target](int i){target->setEnabled(i==1);refreshProfileExtrusion();});
    connect(target,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{refreshProfileExtrusion();});
    connect(depth,&QLineEdit::textChanged,this,[this]{refreshProfileExtrusion();});
    refreshSketchPicking();refreshProfileExtrusion();showCommandPanel(commandPanel_);depth->setFocus();depth->selectAll();
}
void CadViewport::chooseExtrusionTarget(const std::vector<size_t>& indices) {
    if(!commandSession_ || !commandSession_->extrusion || !extrusionControls_)return;
    auto* mode=extrusionControls_->findChild<QComboBox*>("extrusionOperation");
    if(mode->currentIndex()!=1)return;
    auto* target=extrusionControls_->findChild<QComboBox*>("extrusionTarget");
    if(indices.size()!=1) {target->setCurrentIndex(0);return;}
    target->setCurrentIndex(target->findData(QVariant::fromValue<qulonglong>(indices.front())));
}
void CadViewport::refreshProfileExtrusion() {
    if(!commandSession_ || !commandSession_->extrusion || !extrusionControls_)return;
    auto& session=*commandSession_;auto& state=*session.extrusion;
    if(!session.preview.IsNull()){context_->Remove(session.preview,Standard_False);session.preview.Nullify();}
    session.candidate.Nullify();session.valid=false;state.after.reset();commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(false);
    auto* status=extrusionControls_->findChild<QLabel*>("extrusionStatus");
    const auto fail=[&](const QString& error){status->setText(error);view_->Redraw();};
    if(!state.sourceError.isEmpty()) {fail(state.sourceError);return;}
    const auto* current=constructionObject(state.workplane.id);
    if(!current || !Construction::equivalent(*current,state.workplane) || sketchEntities_!=state.sketches){fail(tr("Source changed; cancel and restart Extrude Profile."));return;}
    bool ok=false;const auto text=extrusionControls_->findChild<QLineEdit*>("extrusionDistance")->text();double depth=QLocale().toDouble(text,&ok);
    if(!ok){fail(tr("Enter a valid signed distance in millimeters."));return;}
    state.adding=extrusionControls_->findChild<QComboBox*>("extrusionOperation")->currentIndex()==1;
    auto result=SketchProfileExtrusion::extrude(state.faces,state.workplane.frame.Direction(),depth);
    if(!result.success){fail(result.diagnostics.front().message);return;}
    state.target.reset();
    try {
        if(state.adding) {
            const auto data=extrusionControls_->findChild<QComboBox*>("extrusionTarget")->currentData();
            const qlonglong index=data.isValid()?data.toLongLong():-1;
            if(index<0 || size_t(index)>=state.originals.size() || !isPartActive(size_t(index))) {fail(tr("Select exactly one existing target Part."));return;}
            state.target=size_t(index);const auto& original=state.originals[*state.target];
            if(!boxes_[*state.target].shape.IsSame(original.shape)){fail(tr("Target changed; cancel and restart."));return;}
            result=SketchProfileExtrusion::addMaterial(result,original.shape,original.orientation);
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
        session.valid=true;session.state=CommandSession::State::Preview;commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(true);
        status->setText(tr("Blue preview: %1 solid(s), %2 mm³. OK confirms one operation.").arg(result.solidCount).arg(result.volume,0,'g',10));view_->ZFitAll();view_->Redraw();
    } catch(const Standard_Failure& e) {session.candidate.Nullify();if(!session.preview.IsNull()){context_->Remove(session.preview,Standard_False);session.preview.Nullify();}fail(QString::fromUtf8(e.GetMessageString()));}
}
void CadViewport::finishProfileExtrusion(bool confirm) {
    if(!commandSession_ || !commandSession_->extrusion)return;
    if(confirm){refreshProfileExtrusion();if(!commandSession_->valid)return;}
    auto session=std::move(*commandSession_);commandSession_.reset();const auto state=session.extrusion;
    if(!session.preview.IsNull())context_->Remove(session.preview,Standard_False);
    delete extrusionControls_;extrusionControls_=nullptr;commandPanel_->hide();commandButtons_->hide();resetModelingTools();
    if(confirm) {
        clearHistorySelection();
        if(state->adding) {
            PullChange change{};change.partIndex=*state->target;
            change.faceChange=std::make_shared<FaceChange>(FaceChange{state->originals[*state->target],*state->after});
            applyFaceState(*state->target,*state->after);undoStack_.push_back(change);redoStack_.clear();updateHistoryActions();
            emit partsChanged();selectParts({*state->target});
        } else {
            auto change=std::make_shared<BooleanChange>();change->resultState.geometryKind=CadPart::GeometryKind::GeneralBRep;
            auto& part=change->resultState;part.shape=session.candidate;part.name=tr("Extrude %1").arg(partCount_+1);part.persistentId=QUuid::createUuid().toString(QUuid::WithoutBraces);
            part.presentation=new AIS_Shape(part.shape);Bnd_Box bounds;BRepBndLib::AddOptimal(part.shape,bounds,false,false);
            bounds.Get(part.min[0],part.min[1],part.min[2],part.max[0],part.max[1],part.max[2]);part.baseMin=part.min;part.baseMax=part.max;
            change->beforeSelection=session.selection;commitBooleanChange(change);
        }
        emit statusMessage(tr("Extrude Profile confirmed. Source Sketch and Workplane were preserved."));
    } else {
        selectedPartIndices_=session.selection;selectedSketchIds_=state->sketchSelection;refreshPartSelection();
        for(const auto& entity:sketchEntities_) if(selectedSketchIds_.contains(entity.id) && sketchPickable(entity) && sketchPresentations_.contains(entity.id)
            && !context_->IsSelected(sketchPresentations_.at(entity.id))) context_->AddOrRemoveSelected(sketchPresentations_.at(entity.id),Standard_False);
        emit partSelectionChanged(selectedPartIndices_);emit sketchSelectionChanged(selectedSketchIds_);
    }
    refreshSketchPicking(true);view_->Redraw();setFocus();
}
