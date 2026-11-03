#include "CadViewport.h"
#include <QIcon>
#include "MoveFaceGeometry.h"
#include "BRepReconstruction.h"
#include "CylindricalMoveGeometry.h"
#include <QMenu>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QAction>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QContextMenuEvent>
#include <StdSelect_BRepOwner.hxx>
#include <TopoDS.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <TopExp_Explorer.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
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
        bool sketch=false;
        for(const auto& [id,presentation]:sketchPresentations_) if(presentation==object) {
            for(const auto& entity:sketchEntities_) if(entity.id==id && sketchPickable(entity)) result.sketches.push_back(id);
            sketch=true;break;
        }
        if(sketch) continue;
        bool construction=false;
        for(const auto& [id,presentation]:constructionPresentations_) if(presentation==object) {
            if(const auto* state=constructionObject(id);state && state->visible && state->selectable) result.constructions.push_back(id);
            construction=true;break;
        }
        if(construction) continue;
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
    if(!result.sketches.empty()) {
        if(faces || parts || !result.constructions.empty()) return {};
        result.kind=SelectionContext::Kind::Sketches;return result;
    }
    if(!result.constructions.empty()) {
        if(faces || parts) return {};
        result.kind=SelectionContext::Kind::Constructions;return result;
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
        auto part=boxes_[selected.partIndex];
        const bool primitive=recognizePullPrimitive(part);
        const BRepAdaptor_Surface surface(selected.face);
        if(selection.faces.size()==1 && primitive) return true;
        if(selection.faces.size()==1 && surface.GetType()==GeomAbs_Cylinder) {
            double volume=0.0;
            return BRepReconstruction::validSolid(part.shape,volume);
        }
        if(!primitive || part.geometryKind!=CadPart::GeometryKind::Box || surface.GetType()!=GeomAbs_Plane) return false;
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
    auto* menu=new QMenu(this);
    menu->setObjectName(QStringLiteral("viewportContextMenu"));
    if(commandSession_ && commandSession_->projection) {
        connect(menu->addAction(tr("Clear projection sources")),&QAction::triggered,this,&CadViewport::clearSketchProjectionSources);
        connect(menu->addAction(tr("Cancel Project 3D Geometry")),&QAction::triggered,this,&CadViewport::cancelCommandSession);
        menu->addSeparator();connect(menu->addAction(tr("Fit All")),&QAction::triggered,this,&CadViewport::fitAll);
        return menu;
    }
    if(selection.kind==SelectionContext::Kind::None) {
        updateHistoryActions();
        menu->addAction(undoAction_); menu->addAction(redoAction_);
        menu->addSeparator();
        auto* fit=menu->addAction(tr("Fit All"));fit->setObjectName("context.fit_all");
        connect(fit,&QAction::triggered,this,&CadViewport::fitAll);
    }
    else if(selection.kind==SelectionContext::Kind::Parts) populatePartMenu(*menu,selection);
    else if(selection.kind==SelectionContext::Kind::Constructions) populateConstructionMenu(*menu);
    else if(selection.kind==SelectionContext::Kind::Sketches) populateSketchMenu(*menu);
    else populateFaceMenu(*menu,selection,anchor);
    if(selection.kind==SelectionContext::Kind::Parts || selection.kind==SelectionContext::Kind::Faces) {
        menu->addSeparator();
        auto* fit=menu->addAction(tr("Fit Selected"));fit->setObjectName("context.fit_selected");
        connect(fit,&QAction::triggered,this,[this,selection] {
            if(selection.kind==SelectionContext::Kind::Parts) {fitSelection(selection.parts);return;}
            Bnd_Box bounds;
            for(const auto& selected:selection.faces) if(isPartVisible(selected.partIndex))
                BRepBndLib::Add(selected.face.Moved(TopLoc_Location(boxes_[selected.partIndex].orientation)),bounds,Standard_False);
            if(bounds.IsVoid() || view_.IsNull()) return;
            view_->FitAll(bounds,0.1,Standard_False);stabilizeViewCamera();view_->ZFitAll();updateConstructionLabels();view_->Redraw();
        });
    }
    if(!activeSketchWorkplaneId_.isEmpty()) {
        menu->addSeparator();auto* drawing=menu->addMenu(tr("2D Drawing"));
        auto* project=drawing->addAction(tr("Project 3D Geometry"));project->setObjectName("context.project_3d");
        connect(project,&QAction::triggered,this,&CadViewport::startSketchProjection);
        auto* axes=drawing->addMenu(tr("Construction Axes"));populateSketchAxesMenu(*axes);
        auto* overdraw=drawing->addAction(tr("Overdraw"));overdraw->setObjectName("context.overdraw");
        connect(overdraw,&QAction::triggered,this,&CadViewport::startSketchOverdraw);
        auto* offset=drawing->addAction(tr("Offset Construction Line"));offset->setObjectName("context.offset_line");
        connect(offset,&QAction::triggered,this,[this]{startSketchConstruction(Sketch::ConstructionTool::OffsetLine);});
        auto* tangent=drawing->addAction(tr("Common Tangent Construction Line"));tangent->setObjectName("context.common_tangent");
        connect(tangent,&QAction::triggered,this,[this]{startSketchConstruction(Sketch::ConstructionTool::CommonTangent);});
        const auto add=[this,drawing](const QString& text,Sketch::DrawTool tool,Sketch::Role role) {
            auto* action=drawing->addAction(text);
            connect(action,&QAction::triggered,this,[this,tool,role]{startSketchDrawing(tool,role);});return action;
        };
        add(tr("Profile Line"),Sketch::DrawTool::Segment,Sketch::Role::Profile);
        add(tr("Profile Circle"),Sketch::DrawTool::Circle,Sketch::Role::Profile);
        add(tr("Profile Arc"),Sketch::DrawTool::Arc,Sketch::Role::Profile);
        add(tr("Profile Ellipse"),Sketch::DrawTool::Ellipse,Sketch::Role::Profile);
        add(tr("Profile Elliptical Arc"),Sketch::DrawTool::EllipticalArc,Sketch::Role::Profile);
        add(tr("Construction Line"),Sketch::DrawTool::Segment,Sketch::Role::Construction);
        add(tr("Infinite Construction Line"),Sketch::DrawTool::InfiniteLine,Sketch::Role::Construction);
        auto* horizontal=add(tr("Horizontal Infinite Construction Line"),Sketch::DrawTool::HorizontalInfiniteLine,Sketch::Role::Construction);
        horizontal->setObjectName("context.infinite.horizontal");horizontal->setIcon(Sketch::orthogonalLineIcon(Sketch::DrawTool::HorizontalInfiniteLine));
        auto* vertical=add(tr("Vertical Infinite Construction Line"),Sketch::DrawTool::VerticalInfiniteLine,Sketch::Role::Construction);
        vertical->setObjectName("context.infinite.vertical");vertical->setIcon(Sketch::orthogonalLineIcon(Sketch::DrawTool::VerticalInfiniteLine));
        add(tr("Construction Circle"),Sketch::DrawTool::Circle,Sketch::Role::Construction);
        add(tr("Construction Arc"),Sketch::DrawTool::Arc,Sketch::Role::Construction);
        add(tr("Construction Ellipse"),Sketch::DrawTool::Ellipse,Sketch::Role::Construction);
        add(tr("Construction Elliptical Arc"),Sketch::DrawTool::EllipticalArc,Sketch::Role::Construction);
        connect(menu->addAction(tr("Look Normal")),&QAction::triggered,this,&CadViewport::lookNormalToSketch);
        connect(menu->addAction(tr("Finish 2D")),&QAction::triggered,this,&CadViewport::endSketchEditing);
    }
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
    if(!partsForDeletion().empty())
        add(tr("Delete Part"),"context.delete_part",[this]() {deleteSelectedParts();});
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
    if(!selection.faces.empty()) {
        auto* remove=menu.addAction(tr("Delete Face"));
        remove->setObjectName(QStringLiteral("context.delete_face"));
        connect(remove,&QAction::triggered,this,&CadViewport::deleteFace);
    }
    if(selection.faces.size()==2 && std::all_of(selection.faces.begin(),selection.faces.end(),[](const auto& selected) {
        return BRepAdaptor_Surface(selected.face).GetType()==GeomAbs_Plane;
    })) {
        auto* midplane=menu.addAction(tr("Midplane Workplane"));midplane->setObjectName("context.midplane");
        connect(midplane,&QAction::triggered,this,[this]{startConstruction(Construction::Method::PlaneMidplane);});
    }
    auto* move=menu.addAction(tr("Move Face"));move->setObjectName(QStringLiteral("context.move_face"));
    QString error=tr("Select exactly one supported planar face.");gp_Pnt center;gp_Dir direction;
    const bool supported=selection.faces.size()==1 && BRepAdaptor_Surface(selection.faces.front().face).GetType()==GeomAbs_Plane && MoveFaceGeometry::reference(
        boxes_[selection.faces.front().partIndex].shape,selection.faces.front().face,center,direction,error);
    MoveFaceFeature feature;
    const bool moveSupported=supported || (selection.faces.size()==1 && moveFaceReference(
        boxes_[selection.faces.front().partIndex].shape,selection.faces.front().face,feature,center,direction,error));
    const bool cylinder=selection.faces.size()==1&&BRepAdaptor_Surface(selection.faces.front().face).GetType()==GeomAbs_Cylinder;
    if(!moveSupported&&!cylinder) { delete move; move=nullptr; }
    else {
        move->setEnabled(moveSupported);move->setToolTip(error);move->setStatusTip(error);
        if(!moveSupported) menu.setToolTipsVisible(true);
    }
    if(move) connect(move,&QAction::triggered,this,&CadViewport::moveFace);
    if(supported) {
        auto* rotate=menu.addAction(tr("Rotate Face"));
        rotate->setObjectName(QStringLiteral("context.rotate_face"));
        connect(rotate,&QAction::triggered,this,&CadViewport::rotateFace);
        auto* align=menu.addAction(tr("Align Face"));
        align->setObjectName(QStringLiteral("context.align_face"));
        connect(align,&QAction::triggered,this,&CadViewport::alignFace);
    }
    if(selection.faces.size()==1) {
        const BRepAdaptor_Surface surface(selection.faces.front().face);
        if(surface.GetType()==GeomAbs_Plane || surface.GetType()==GeomAbs_Cylinder) {
            auto* construction=menu.addMenu(tr("Construction Geometry"));
            const auto add=[&](const QString& text,Construction::Method method) {
                auto* action=construction->addAction(text);
                connect(action,&QAction::triggered,this,[this,method]{startConstruction(method);});
            };
            if(surface.GetType()==GeomAbs_Plane) {
                add(tr("Workplane from Face"),Construction::Method::PlaneFace);
                add(tr("Offset Workplane"),Construction::Method::PlaneOffset);
            } else add(tr("Axis from Cylinder"),Construction::Method::AxisCylinder);
        }
    }
    auto* resize=menu.addAction(tr("Pull / Resize"));resize->setObjectName(QStringLiteral("context.resize"));
    if(!supportsResize(selection)) { delete resize; return; }
    connect(resize,&QAction::triggered,this,[this,anchor]() { previous_=anchor; startPrimitivePull(); });
}
void CadViewport::contextMenuEvent(QContextMenuEvent* event) {
    event->accept();
    if(commandSession_ && commandSession_->projection && !context_.IsNull()) {
        std::unique_ptr<QMenu> menu(createSelectionMenu(event->pos()));menu->exec(event->globalPos());return;
    }
    if(context_.IsNull() || commandSession_ || moveFacePicking_ || draggingFace_ || draggingRotation_ || draggingFaceRotation_ || rotating_ || panning_) return;
    const auto clicked=detectedPart(event->pos());
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
            selectedConstructionIds_=selection.constructions;emit constructionSelectionChanged(selectedConstructionIds_);
            selectedSketchIds_=selection.sketches;emit sketchSelectionChanged(selectedSketchIds_);
        }
    }
    // An existing selection owns the menu everywhere, including empty space.
    // Right-click never replaces or clears it to provide the zoom action.
    std::unique_ptr<QMenu> menu(createSelectionMenu(event->pos()));
    if(menu) {
        menu->addSeparator();
        if(clicked) {
            const auto id=partId(*clicked);
            auto* active=menu->addAction(id==activePartId_?tr("Deactivate Part"):tr("Set Active Part"));
            connect(active,&QAction::triggered,this,[this,id]{setActivePart(id==activePartId_?QString():id);});
        }
        auto* clear=menu->addAction(tr("Clear Active Part"));clear->setEnabled(!activePartId_.isEmpty());
        connect(clear,&QAction::triggered,this,[this]{setActivePart({});});
        menu->exec(event->globalPos());
    }
}

void CadViewport::resetModelingTools() {
    const bool restoreFacePicker=faceAxisPicking_ || selectionMode_!=SelectionMode::Face;
    std::vector<Handle(SelectMgr_EntityOwner)> retainedOwners;
    if(restoreFacePicker && !context_.IsNull())
        for(context_->InitSelected();context_->MoreSelected();context_->NextSelected()) retainedOwners.push_back(context_->SelectedOwner());
    movePartMode_=directMoveMode_=rotateMode_=pullMode_=moveFacePicking_=false;
    draggingFace_=draggingRotation_=primitivePullDragging_=multiFaceDragging_=groupRotationDragging_=false;
    dimensionEditing_=primitivePullEditing_=multiFaceEditing_=false;
    cylindricalFacePull_=false;
    cylindricalPullFace_.Nullify();
    cylindricalPullRadius_=0.0;
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
    cylinderMoveLabel_->hide(); cylinderMoveDirection_->hide();
    cylinderMoveAngle_->hide(); cylinderBossBoundary_->hide();
    faceAlignTargetLabel_->hide(); faceAlignReferenceLabel_->hide();
    emit transformModesChanged(false,false,false);
    emit selectionModeChanged(selectionMode_);
    refreshSketchPicking();
    if(!view_.IsNull()) view_->Redraw();
}
void CadViewport::startPrimitivePull() {
    endSketchEditing();
    cancelCommandSession();
    const auto selection=selectionContext();
    if(!supportsResize(selection)) {
        emit statusMessage(tr("Pull / Resize: select supported primitive faces first. No command started."));
        return;
    }
    if(selection.faces.size()==1&&BRepAdaptor_Surface(selection.faces.front().face).GetType()==GeomAbs_Cylinder) {
        const auto& selected=selection.faces.front();auto probe=boxes_[selected.partIndex];
        if(!recognizePullPrimitive(probe)) {
            const auto capability=CylindricalMoveGeometry::resizeRadius(probe.shape,selected.face,BRepAdaptor_Surface(selected.face).Cylinder().Radius());
            if(!capability) {emit statusMessage(QString::fromStdString(capability.message));return;}
        }
    }
    pullMode_=true;
    for (const auto index:selection.parts) {
        auto& part=boxes_[index];
        if (!recognizePullPrimitive(part)) {
            part.cylinderPrimitive=part.spherePrimitive=part.conePrimitive=false;
            part.geometryKind=CadPart::GeometryKind::GeneralBRep;
        }
    }
    updateSelectedFaceArrow(previous_);
    // All arrows are displayed with deferred viewer updates. Activation must
    // render them even when the cursor does not move after the menu closes.
    view_->Invalidate();
    view_->Redraw();
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
    session.geometryError.clear();
    bool valid=false;
    try {
    if(primitivePullDragging_) valid=applyPrimitivePull(value);
    else if(multiFaceDragging_) valid=applyMultiFaceDrag(value);
    else {
        auto min=dragMin_,max=dragMaxBounds_;
        if(dragMax_) max[dragAxis_]+=value; else min[dragAxis_]-=value;
        if(max[dragAxis_]-min[dragAxis_]>0.001) { applyBoxBounds(dragIndex_,min,max); valid=true; }
    }
    if(valid) for(const auto& change:session.changes) {
        double volume=0;
        if(!BRepReconstruction::validSolid(boxes_[change.partIndex].shape,volume)) {
            valid=false;session.geometryError=tr("Pull could not produce a valid closed solid.");break;
        }
    }
    } catch(const Standard_Failure&) {
        session.geometryError=tr("Open CASCADE could not reconstruct the Pull solid safely.");
        valid=false;
        for(const auto& change:session.changes) applyFaceState(change.partIndex,change.faceChange->after);
    } catch(const std::exception&) {
        session.geometryError=tr("Pull could not reconstruct the solid safely.");
        valid=false;
        for(const auto& change:session.changes) applyFaceState(change.partIndex,change.faceChange->after);
    }
    session.valid=valid;
    commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(valid);
    if(!valid) {
        for(const auto& change:session.changes) applyFaceState(change.partIndex,change.faceChange->after);
        if(session.geometryError.isEmpty()) session.geometryError=tr("The requested Pull dimension cannot produce a closed solid with positive dimensions.");
        reportGeometryFailure(tr("Pull / Resize"),session.geometryError,false);
        return false;
    }
    session.value=value; session.state=CommandSession::State::Preview;
    for(auto& change:session.changes) change.faceChange->after=boxes_[change.partIndex];
    return true;
}
