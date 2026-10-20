#include "CadViewport.h"
#include "MoveFaceGeometry.h"
#include <QMenu>
#include <QLineEdit>
#include <QAction>
#include <QColorDialog>
#include <QComboBox>
#include <QLabel>
#include <QContextMenuEvent>
#include <StdSelect_BRepOwner.hxx>
#include <TopoDS.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <TopExp_Explorer.hxx>
#include <Standard_Failure.hxx>
#include <exception>
#include <algorithm>
#include <cmath>

CadViewport::SelectionContext CadViewport::selectionContext() const {
    SelectionContext result;
    if (context_.IsNull()) return result;
    bool faces=false,parts=false;
    for (context_->InitSelected();context_->MoreSelected();context_->NextSelected()) {
        const auto object=context_->SelectedInteractive();
        size_t index=0;
        while(index<boxes_.size() && boxes_[index].presentation!=object) ++index;
        if (!isPartVisible(index)) return {};
        if (std::find(result.parts.begin(),result.parts.end(),index)==result.parts.end()) result.parts.push_back(index);
        const auto owner=Handle(StdSelect_BRepOwner)::DownCast(context_->SelectedOwner());
        if (!owner.IsNull() && !owner->Shape().IsNull() && owner->Shape().ShapeType()==TopAbs_FACE) {
            const auto face=TopoDS::Face(owner->Shape());
            bool member=false;
            for(TopExp_Explorer f(boxes_[index].shape,TopAbs_FACE);f.More();f.Next())
                if(f.Current().IsSame(face)) member=true;
            if(!member) return {}; // Never offer an edit for stale topology.
            result.faces.push_back({index,face}); faces=true;
        } else parts=true;
    }
    if(!faces && !parts) {
        // Some existing Undo paths refresh geometry before restoring OCCT owners.
        // Whole-part IDs remain authoritative; face picks clear this list explicitly.
        for(const auto index:selectedPartIndices_) if(isPartVisible(index)) result.parts.push_back(index);
        parts=!result.parts.empty();
    }
    if(faces && parts) return {};
    result.kind=faces ? SelectionContext::Kind::Faces : parts ? SelectionContext::Kind::Parts : SelectionContext::Kind::None;
    return result;
}
void CadViewport::setSelectionMode(SelectionMode mode) {
    if(selectionMode_==mode && !(mode==SelectionMode::Face && (movePartMode_ || directMoveMode_ || rotateMode_))) return;
    cancelCommandSession();
    std::vector<Handle(SelectMgr_EntityOwner)> owners;
    if(!context_.IsNull())
        for(context_->InitSelected();context_->MoreSelected();context_->NextSelected()) owners.push_back(context_->SelectedOwner());
    selectionMode_=mode;
    selectedPartIndices_.clear();
    if(mode==SelectionMode::Face) {
        movePartMode_=directMoveMode_=rotateMode_=false;
        emit transformModesChanged(false,false,false);
    }
    hideFaceArrow(); hideMoveAxes(); hideRotationRings();
    setTransformSelectionMode();
    if(!context_.IsNull()) {
        context_->ClearSelected(Standard_False);
        for(const auto& owner:owners) {
            const auto brep=Handle(StdSelect_BRepOwner)::DownCast(owner);
            const bool face=!brep.IsNull() && !brep->Shape().IsNull() && brep->Shape().ShapeType()==TopAbs_FACE;
            if(face==(mode==SelectionMode::Face)) context_->AddOrRemoveSelected(owner,Standard_False);
        }
        if(mode==SelectionMode::Part) selectedPartIndices_=selectionContext().parts;
        emit partSelectionChanged(selectionContext().parts);
        view_->Redraw();
    }
    emit selectionModeChanged(mode);
    emit statusMessage(mode==SelectionMode::Part
        ? tr("Selection: Parts. Ctrl adds/removes parts; Alt and Ctrl+Alt remain available.")
        : tr("Selection: Faces. Ctrl adds/removes faces; Alt selects parts."));
}
bool CadViewport::supportsResize(const SelectionContext& selection) const {
    if(selection.kind!=SelectionContext::Kind::Faces || selection.faces.empty()) return false;
    for(const auto& selected:selection.faces) {
        const auto& part=boxes_[selected.partIndex];
        const BRepAdaptor_Surface surface(selected.face);
        if(selection.faces.size()==1 && (part.cylinderPrimitive || part.spherePrimitive || part.conePrimitive)) return true;
        if(part.geometryKind!=CadPart::GeometryKind::Box || surface.GetType()!=GeomAbs_Plane) return false;
        const auto direction=surface.Plane().Axis().Direction();
        bool side=false;
        for(int axis=0;axis<3;++axis) if(std::abs(direction.Coord(axis+1))>0.999) {
            const double coordinate=surface.Plane().Location().Coord(axis+1);
            side=std::abs(coordinate-part.min[axis])<0.001 || std::abs(coordinate-part.max[axis])<0.001;
        }
        if(!side) return false;
    }
    return true;
}
QMenu* CadViewport::createSelectionMenu(const QPoint& anchor) {
    const auto selection=selectionContext();
    if(selection.kind==SelectionContext::Kind::None) return nullptr;
    auto* menu=new QMenu(this);
    menu->setObjectName(QStringLiteral("viewportContextMenu"));
    if(selection.kind==SelectionContext::Kind::Parts) populatePartMenu(*menu,selection);
    else populateFaceMenu(*menu,selection,anchor);
    if(menu->isEmpty()) { delete menu; return nullptr; }
    return menu;
}
void CadViewport::populatePartMenu(QMenu& menu,const SelectionContext& selection) {
    const auto add=[&](const QString& text,const char* id,auto callback) {
        auto* action=menu.addAction(text); action->setObjectName(QString::fromLatin1(id));
        connect(action,&QAction::triggered,this,callback); return action;
    };
    add(tr("Move Part"),"context.move_part",[this]() {selectionMode_=SelectionMode::Part; emit selectionModeChanged(selectionMode_); setMovePartMode(false);setRotateMode(false);setDirectMoveMode(true);});
    add(tr("Rotate Part"),"context.rotate_part",[this]() {selectionMode_=SelectionMode::Part; emit selectionModeChanged(selectionMode_); setMovePartMode(false);setDirectMoveMode(false);setRotateMode(true);});
    add(tr("Duplicate"),"context.duplicate",[this]() {duplicateSelectedParts();});
    auto* boolean=menu.addMenu(tr("Boolean")); boolean->setObjectName(QStringLiteral("context.boolean"));
    bool solids=!selection.parts.empty();
    for(const auto index:selection.parts) if(!TopExp_Explorer(boxes_[index].shape,TopAbs_SOLID).More()) solids=false;
    const auto op=[&](const QString& text,const char* id,auto method) {
        auto* action=boolean->addAction(text);action->setObjectName(QString::fromLatin1(id));action->setEnabled(solids);
        connect(action,&QAction::triggered,this,method);
    };
    op(tr("Unite"),"context.unite",&CadViewport::uniteSelectedParts);
    op(tr("Subtract"),"context.subtract",&CadViewport::subtractSelectedParts);
    op(tr("Intersect"),"context.intersect",&CadViewport::intersectSelectedParts);
    menu.addSeparator();
    add(tr("Hide"),"context.hide",[this,indices=selection.parts]() {
        for(const auto index:indices) setPartVisible(index,false);
        emit partsChanged();
    });
    auto* show=add(tr("Show"),"context.show",[this,indices=selection.parts]() {
        for(const auto index:indices) setPartVisible(index,true);
        emit partsChanged();
    });
    if(!std::any_of(selection.parts.begin(),selection.parts.end(),[this](size_t i){return !isPartVisible(i);})) delete show;
    add(tr("Color…"),"context.color",[this,indices=selection.parts]() {
        const auto color=QColorDialog::getColor(partColor(indices.front()),this,tr("Part Color"));
        if(color.isValid()) for(const auto index:indices) setPartColor(index,color);
    });
}
void CadViewport::populateFaceMenu(QMenu& menu,const SelectionContext& selection,const QPoint& anchor) {
    auto* move=menu.addAction(tr("Move Face"));move->setObjectName(QStringLiteral("context.move_face"));
    QString error=tr("Select exactly one supported planar face.");gp_Pnt center;gp_Dir direction;
    const bool supported=selection.faces.size()==1 && MoveFaceGeometry::reference(
        boxes_[selection.faces.front().partIndex].shape,selection.faces.front().face,center,direction,error);
    if(!supported) { delete move; move=nullptr; }
    else move->setToolTip(error);
    if(move) connect(move,&QAction::triggered,this,&CadViewport::moveFace);
    if(supported) {
        auto* rotate=menu.addAction(tr("Rotate Face"));
        rotate->setObjectName(QStringLiteral("context.rotate_face"));
        connect(rotate,&QAction::triggered,this,&CadViewport::rotateFace);
        auto* align=menu.addAction(tr("Align Face"));
        align->setObjectName(QStringLiteral("context.align_face"));
        connect(align,&QAction::triggered,this,&CadViewport::alignFace);
    }
    auto* resize=menu.addAction(tr("Pull / Resize"));resize->setObjectName(QStringLiteral("context.resize"));
    if(!supportsResize(selection)) { delete resize; return; }
    connect(resize,&QAction::triggered,this,[this,anchor]() { previous_=anchor; startPrimitivePull(); });
}
void CadViewport::contextMenuEvent(QContextMenuEvent* event) {
    event->accept();
    if(context_.IsNull() || commandSession_ || moveFacePicking_ || draggingFace_ || draggingRotation_ || rotating_ || panning_) return;
    auto selection=selectionContext();
    if(selection.kind==SelectionContext::Kind::None && event->reason()==QContextMenuEvent::Mouse) {
        for(const auto& part:boxes_) if(part.active && part.visible) {
            context_->Deactivate(part.presentation);
            context_->Activate(part.presentation,AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
        }
        context_->MoveTo(event->pos().x(),event->pos().y(),view_,Standard_False);
        if(context_->HasDetected()) {
            selectedPartIndices_.clear(); context_->SelectDetected(AIS_SelectionScheme_Replace);
            selection=selectionContext(); emit partSelectionChanged(selection.parts);
        }
    }
    if(selection.kind==SelectionContext::Kind::None) return;
    if(event->reason()==QContextMenuEvent::Mouse) {
        context_->MoveTo(event->pos().x(),event->pos().y(),view_,Standard_True);
        if(!context_->HasDetected()) return;
        const auto object=context_->DetectedInteractive();
        bool selected=false;
        if(selection.kind==SelectionContext::Kind::Parts) {
            for(const auto i:selection.parts) if(boxes_[i].presentation==object) selected=true;
        } else {
            const auto owner=Handle(StdSelect_BRepOwner)::DownCast(context_->DetectedOwner());
            if(!owner.IsNull()) for(const auto& face:selection.faces)
                if(boxes_[face.partIndex].presentation==object &&
                   (face.face.IsSame(owner->Shape()) || (!owner->Shape().IsNull() && owner->Shape().ShapeType()!=TopAbs_FACE))) selected=true;
        }
        if(!selected) return; // Right-click never replaces or clears a selection.
    }
    std::unique_ptr<QMenu> menu(createSelectionMenu(event->pos()));
    if(menu) menu->exec(event->globalPos());
}

void CadViewport::resetModelingTools() {
    const bool restoreFacePicker=faceAxisPicking_ || selectionMode_!=SelectionMode::Face;
    std::vector<Handle(SelectMgr_EntityOwner)> retainedOwners;
    if(restoreFacePicker && !context_.IsNull())
        for(context_->InitSelected();context_->MoreSelected();context_->NextSelected()) retainedOwners.push_back(context_->SelectedOwner());
    movePartMode_=directMoveMode_=rotateMode_=pullMode_=moveFacePicking_=false;
    draggingFace_=draggingRotation_=primitivePullDragging_=multiFaceDragging_=groupRotationDragging_=false;
    dimensionEditing_=primitivePullEditing_=multiFaceEditing_=false;
    hideFaceArrow(); hideMoveAxes(); hideRotationRings(); clearMultiFaceHighlight();
    if(!moveFaceArrow_.IsNull() && !context_.IsNull()) { context_->Remove(moveFaceArrow_,Standard_False); moveFaceArrow_.Nullify(); }
    if(!faceRotationManipulator_.IsNull() && !context_.IsNull()) { context_->Remove(faceRotationManipulator_,Standard_False); faceRotationManipulator_.Nullify(); }
    faceAxisPicking_=alignFacePickingReference_=draggingFaceRotation_=false;
    multiFaceParts_.clear();
    selectionMode_=SelectionMode::Face;
    if(restoreFacePicker && !context_.IsNull()) {
        for(auto& part:boxes_) if(part.active && part.visible) {
            context_->Deactivate(part.presentation);
            context_->Activate(part.presentation,AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
        }
        context_->ClearSelected(Standard_False);
        for(const auto& owner:retainedOwners) context_->AddOrRemoveSelected(owner,Standard_False);
    }
    dragDimension_->hide(); commandPanel_->hide();
    faceAxisSelector_->hide(); faceAlignSelector_->hide();
    faceAlignTargetLabel_->hide(); faceAlignReferenceLabel_->hide();
    emit transformModesChanged(false,false,false);
    emit selectionModeChanged(selectionMode_);
    if(!view_.IsNull()) view_->Redraw();
}
void CadViewport::startPrimitivePull() {
    cancelCommandSession();
    const auto selection=selectionContext();
    if(!supportsResize(selection)) {
        emit statusMessage(tr("Pull / Resize: select supported primitive faces first. No command started."));
        return;
    }
    pullMode_=true;
    updateSelectedFaceArrow(previous_);
    emit statusMessage(tr("Pull / Resize: drag an orange arrow; OK/Enter confirms, Cancel/Esc restores the original."));
}
void CadViewport::beginPullSession() {
    std::vector<size_t> indices;
    if(multiFaceDragging_) for(const auto& item:multiFaceParts_) indices.push_back(item.before.partIndex);
    else indices.push_back(dragIndex_);
    beginCommandSession(CommandSession::Kind::Pull,indices,gp_Dir(1,0,0));
    for(auto& change:commandSession_->changes)
        change.faceChange=std::make_shared<FaceChange>(FaceChange{boxes_[change.partIndex],boxes_[change.partIndex]});
    commandPanel_->setWindowTitle(tr("Pull / Resize Preview"));
}
bool CadViewport::previewPrimitivePull(double value) {
    auto& session=*commandSession_;
    bool valid=false;
    try {
    if(primitivePullDragging_) valid=applyPrimitivePull(value);
    else if(multiFaceDragging_) valid=applyMultiFaceDrag(value);
    else {
        auto min=dragMin_,max=dragMaxBounds_;
        if(dragMax_) max[dragAxis_]+=value; else min[dragAxis_]-=value;
        if(max[dragAxis_]-min[dragAxis_]>0.001) { applyBoxBounds(dragIndex_,min,max); valid=true; }
    }
    } catch(const Standard_Failure&) {
        for(const auto& change:session.changes) applyFaceState(change.partIndex,change.faceChange->after);
    } catch(const std::exception&) {
        for(const auto& change:session.changes) applyFaceState(change.partIndex,change.faceChange->after);
    }
    session.valid=valid;
    if(!valid) {
        emit statusMessage(tr("Invalid Pull dimension. Enter a valid value or Cancel/Esc to restore the original."));
        return false;
    }
    session.value=value; session.state=CommandSession::State::Preview;
    for(auto& change:session.changes) change.faceChange->after=boxes_[change.partIndex];
    return true;
}
