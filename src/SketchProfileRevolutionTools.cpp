#include "CadViewport.h"
#include "ProfileSweepSelection.h"
#include <QComboBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QLocale>
#include <StdSelect_BRepOwner.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>

void CadViewport::createRevolutionControls(QFormLayout* form) {
    form->addRow(tr("Operation"),new QLabel(tr("New Solid"),sweepControls_));
    auto* axis=new QComboBox(sweepControls_);axis->setObjectName("revolutionAxis");
    axis->addItem(tr("Select rotation axis"),QString());
    int lineNumber=0;
    for(const auto& entity:sketchEntities_)if(sketchPickable(entity)
        && (std::holds_alternative<Sketch::Segment>(entity.geometry) || std::holds_alternative<Sketch::InfiniteLine>(entity.geometry)))
        axis->addItem(tr("Sketch: %1").arg(entity.name.isEmpty()?tr("Line %1").arg(++lineNumber):entity.name),entity.id);
    for(const auto& object:constructions_)if(object.kind==Construction::Kind::Axis && object.visible && object.selectable)
        axis->addItem(tr("Axis: %1").arg(object.name),object.id);
    form->addRow(tr("Rotation axis"),axis);
    auto* pick=new QPushButton(tr("Pick rotation axis"),sweepControls_);pick->setObjectName("pickRevolutionAxis");form->addRow(pick);
    auto* angle=new QLineEdit(QLocale().toString(360.0,'f',2),sweepControls_);angle->setObjectName("revolutionAngle");form->addRow(tr("Angle (degrees)"),angle);
    auto* reverse=new QCheckBox(tr("Reverse direction"),sweepControls_);reverse->setObjectName("revolutionReverse");form->addRow(reverse);
    connect(axis,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,axis]{chooseRevolutionAxisId(axis->currentData().toString());});
    connect(pick,&QPushButton::clicked,this,[this]{beginRevolutionAxisPick();setFocus();});
    connect(angle,&QLineEdit::textChanged,this,[this]{refreshProfileSweep();});
    connect(reverse,&QCheckBox::toggled,this,[this]{refreshProfileSweep();});
}
void CadViewport::configureRevolutionPicking() {
    const bool picking=commandSession_->sweep->axisPicking;
    for(const auto& part:boxes_)if(part.active && part.visible) {
        context_->Deactivate(part.presentation);
        if(picking)context_->Activate(part.presentation,AIS_Shape::SelectionMode(TopAbs_EDGE),Standard_False);
    }
    for(const auto& [id,p]:sketchPresentations_) {
        context_->Deactivate(p);
        if(picking || commandSession_->sweep->sketchSelection.contains(id))for(const auto& entity:sketchEntities_)if(entity.id==id && sketchPickable(entity))context_->Activate(p,0,Standard_False);
    }
    for(const auto& object:constructions_)if(constructionPresentations_.contains(object.id)) {
        const auto p=constructionPresentations_.at(object.id);context_->Deactivate(p);
        if(picking && object.kind==Construction::Kind::Axis && object.visible && object.selectable)context_->Activate(p,0,Standard_False);
    }
    ProfileSweepSelection::restore(context_,commandSession_->owners,commandSession_->sweep->sketchSelection,sketchPresentations_);
}
void CadViewport::beginRevolutionAxisPick() {
    auto& state=*commandSession_->sweep;
    state.axis.reset();state.axisPartId.clear();state.axisConstruction.reset();state.axisPicking=true;
    auto* combo=sweepControls_->findChild<QComboBox*>("revolutionAxis");
    {const QSignalBlocker blocker(combo);combo->setCurrentIndex(0);}
    context_->ClearDetected(Standard_False);configureRevolutionPicking();refreshProfileSweep();
    emit statusMessage(tr("Select a sketch line, construction axis or straight Part edge for the rotation axis."));
}
void CadViewport::chooseRevolutionAxisId(const QString& id) {
    auto& state=*commandSession_->sweep;
    state.axis.reset();state.axisPartId.clear();state.axisConstruction.reset();
    QString error;Construction::Reference reference;
    try {
        for(const auto& entity:sketchEntities_)if(entity.id==id && sketchPickable(entity)) {
            if(Construction::resolve(Sketch::shape(entity,constructionObject(entity.workplaneId)->frame),gp_Trsf(),Construction::ReferenceKind::Line,reference,error))
                state.axis=gp_Ax1(reference.frame.Location(),reference.frame.Direction());
            break;
        }
        if(const auto* object=constructionObject(id);object && object->kind==Construction::Kind::Axis && object->visible && object->selectable) {
            state.axis=gp_Ax1(object->frame.Location(),object->frame.Direction());state.axisConstruction=*object;
        }
    } catch(const Standard_Failure& failure) {error=QString::fromUtf8(failure.GetMessageString());}
    auto* combo=sweepControls_->findChild<QComboBox*>("revolutionAxis");
    {const QSignalBlocker blocker(combo);const int index=combo->findData(id);combo->setCurrentIndex(index<0?0:index);}
    state.axisPicking=!state.axis.has_value();configureRevolutionPicking();refreshProfileSweep();
    if(!error.isEmpty())sweepControls_->findChild<QLabel*>("extrusionStatus")->setText(error);
}
bool CadViewport::chooseRevolutionAxis(const Handle(SelectMgr_EntityOwner)& owner) {
    if(!commandSession_ || commandSession_->kind!=CommandSession::Kind::Revolve || owner.IsNull())return false;
    for(const auto& entity:sketchEntities_)if(sketchPresentations_.contains(entity.id) && sketchPresentations_.at(entity.id)==owner->Selectable() && sketchPickable(entity)) {
        chooseRevolutionAxisId(entity.id);
        auto* combo=sweepControls_->findChild<QComboBox*>("revolutionAxis");
        const QSignalBlocker blocker(combo);const int index=combo->findData(entity.id);combo->setCurrentIndex(index<0?0:index);return commandSession_->sweep->axis.has_value();
    }
    for(const auto& object:constructions_)if(constructionPresentations_.contains(object.id) && constructionPresentations_.at(object.id)==owner->Selectable()
        && object.kind==Construction::Kind::Axis && object.visible && object.selectable) {
        chooseRevolutionAxisId(object.id);
        auto* combo=sweepControls_->findChild<QComboBox*>("revolutionAxis");const QSignalBlocker blocker(combo);combo->setCurrentIndex(combo->findData(object.id));return true;
    }
    const auto brep=Handle(StdSelect_BRepOwner)::DownCast(owner);Construction::Reference ref;QString error;
    if(!brep.IsNull() && !brep->Shape().IsNull())for(size_t i=0;i<boxes_.size();++i) {
        const auto& part=boxes_[i];if(!isPartVisible(i) || part.presentation!=owner->Selectable())continue;
        // Owner geometry is canonical Part-local topology. Resolve with exactly
        // one instance placement, as in the existing construction picker.
        bool member=false;
        for(TopExp_Explorer it(part.shape,TopAbs_EDGE);it.More();it.Next())if(it.Current().IsSame(brep->Shape())){member=true;break;}
        if(member && Construction::resolve(brep->Shape(),part.orientation,Construction::ReferenceKind::Line,ref,error)) {
            auto& state=*commandSession_->sweep;state.axis=gp_Ax1(ref.frame.Location(),ref.frame.Direction());
            state.axisPartId=part.persistentId;state.axisRoot=part.shape;state.axisPlacement=part.orientation;
            state.axisConstruction.reset();state.axisPicking=false;
            auto* combo=sweepControls_->findChild<QComboBox*>("revolutionAxis");const QSignalBlocker blocker(combo);
            combo->addItem(tr("%1 / picked edge").arg(part.name),QString());combo->setCurrentIndex(combo->count()-1);
            configureRevolutionPicking();refreshProfileSweep();return true;
        }
        break;
    }
    // An invalid new reference cannot leave the previous valid preview behind.
    beginRevolutionAxisPick();
    const auto message=error.isEmpty()?tr("Select a straight sketch line, construction axis or Part edge."):error;
    sweepControls_->findChild<QLabel*>("extrusionStatus")->setText(message);emit statusMessage(message);return false;
}
bool CadViewport::validateRevolutionAxis() const {
    const auto& state=*commandSession_->sweep;
    if(state.axisConstruction) {
        const auto* current=constructionObject(state.axisConstruction->id);
        if(!current || !current->visible || !current->selectable || !Construction::equivalent(*current,*state.axisConstruction))return false;
    }
    if(!state.axisPartId.isEmpty()) {
        const auto index=partIndexForId(state.axisPartId);
        if(!index || !isPartVisible(*index) || !boxes_[*index].shape.IsSame(state.axisRoot))return false;
        for(int row=1;row<=3;++row)for(int column=1;column<=4;++column)
            if(boxes_[*index].orientation.Value(row,column)!=state.axisPlacement.Value(row,column))return false;
    }
    return true;
}
