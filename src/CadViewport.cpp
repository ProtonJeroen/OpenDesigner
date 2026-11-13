#include "CadViewport.h"
#include "PartPresentation.h"
#include "CommandPanelPositioner.h"
#include "ConstructionPanel.h"
#include "BRepReconstruction.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPaintEngine>
#include <QResizeEvent>
#include <QInputDialog>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QCheckBox>
#include <QSignalBlocker>
#include <QShortcut>
#include <QAction>
#include <QVBoxLayout>
#include <QApplication>
#include <QKeyEvent>
#include <QPushButton>
#include <QLocale>
#include <algorithm>
#include <Aspect_NeutralWindow.hxx>
#if defined(Q_OS_WIN)
#include <WNT_Window.hxx>
#endif
#include <OpenGl_GraphicDriver.hxx>
#include <AIS_Shape.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <TopExp_Explorer.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <StdSelect_ViewerSelector3d.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include "CylindricalMoveGeometry.h"
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <TopExp_Explorer.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <gp_Circ.hxx>
#include <ElCLib.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax2.hxx>
#include <gp_Vec.hxx>
#include <gp_Ax1.hxx>
#include <TopLoc_Location.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <QDialog>
#include <QFormLayout>
#include <QComboBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QUuid>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pln.hxx>
#include <Quantity_Color.hxx>
#include <Graphic3d_Camera.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Sphere.hxx>
#include <cmath>
#include <algorithm>
#include <functional>
#include <SelectMgr_Filter.hxx>

namespace {
void stabilizeOrthographicCamera(const Handle(Graphic3d_Camera)& camera) {
    if(camera.IsNull() || !camera->IsOrthographic()) return;
    const auto center=camera->Center();const auto direction=camera->Direction();
    const double scale=camera->Scale();
    if(!std::isfinite(scale) || scale<=0 || !std::isfinite(center.X()) || !std::isfinite(center.Y()) || !std::isfinite(center.Z())) return;
    if(camera->Eye().SquareDistance(center)>1.e-16) return;
    // FitAll on a plane viewed normally can set OCCT's orthographic distance
    // to zero. OCCT retains Direction separately, but .odc stores Eye/Center.
    // Restore a representable separation without changing center, zoom or Up.
    const double distance=std::max(1.0,scale);
    camera->SetEyeAndCenter(center.Translated(gp_Vec(direction)*(-distance)),center);
}
QString dimensionNumber(QString text) {
    text = text.trimmed();
    if (text.startsWith(QChar(0x00d8))) text.remove(0,1);
    if (text.endsWith(QStringLiteral("mm"))) text.chop(2);
    return text.trimmed();
}

// Keep the diameter symbol and unit visible while allowing either the complete
// displayed dimension or a plain localized number to be entered.
class PullDimensionValidator final : public QDoubleValidator {
public:
    explicit PullDimensionValidator(QObject* parent)
        : QDoubleValidator(-100000.0,100000.0,2,parent) {}
    State validate(QString& input, int& pos) const override {
        QString number = dimensionNumber(input);
        int numberPos = std::min(pos,static_cast<int>(number.size()));
        return QDoubleValidator::validate(number,numberPos);
    }
};
}

CadViewport::CadViewport(QWidget* parent) : QWidget(parent) {
    undoAction_=new QAction(tr("Undo"),this);
    undoAction_->setObjectName(QStringLiteral("context.undo"));
    undoAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z));
    redoAction_=new QAction(tr("Redo"),this);
    redoAction_->setObjectName(QStringLiteral("context.redo"));
    redoAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y));
    addAction(undoAction_); addAction(redoAction_);
    connect(undoAction_,&QAction::triggered,this,&CadViewport::undoPull);
    connect(redoAction_,&QAction::triggered,this,&CadViewport::redoPull);
    updateHistoryActions();
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    qApp->installEventFilter(this);
    dragDimension_ = new QLineEdit(this);
    dragDimension_->setFixedWidth(160);
    dragDimension_->setAlignment(Qt::AlignCenter);
    dragDimension_->setValidator(new PullDimensionValidator(dragDimension_));
    dragDimension_->hide();
    // A raster tool window keeps Qt controls off OCCT's paint-on-screen surface.
    commandPanel_ = new QWidget(this, Qt::Tool | Qt::FramelessWindowHint);
    commandPanel_->setAttribute(Qt::WA_ShowWithoutActivating);
    commandPanel_->installEventFilter(this);
    commandPanelPositioner_=new CommandPanelPositioner(this);
    auto* commandLayout = commandLayout_ = new QVBoxLayout(commandPanel_);
    faceAxisSelector_=new QComboBox(commandPanel_);
    faceAxisSelector_->setObjectName(QStringLiteral("rotateFaceAxisSelector"));
    faceAxisSelector_->addItems({tr("Choose a straight edge…"),tr("World X axis"),tr("World Y axis"),tr("World Z axis")});
    faceAxisSelector_->hide(); commandLayout->addWidget(faceAxisSelector_);
    initializeCylindricalMoveControls();
    initializeMoveDirectionControls();
    keepBlends_=new QCheckBox(tr("Keep Fillet / Chamfer"),commandPanel_);
    keepBlends_->setObjectName("keepFilletChamfer");keepBlends_->hide();commandLayout_->addWidget(keepBlends_);
    connect(keepBlends_,&QCheckBox::toggled,this,[this]{if(commandSession_) updateCommandParameter(false);});
    faceAlignSelector_=new QComboBox(commandPanel_);
    faceAlignSelector_->setObjectName(QStringLiteral("alignFaceModeSelector"));
    faceAlignSelector_->addItems({tr("Coplanar"),tr("Parallel")});
    faceAlignSelector_->hide(); commandLayout->addWidget(faceAlignSelector_);
    faceAlignTargetLabel_=new QLabel(commandPanel_);
    faceAlignTargetLabel_->setObjectName(QStringLiteral("alignFaceTarget"));
    faceAlignTargetLabel_->hide(); commandLayout->addWidget(faceAlignTargetLabel_);
    faceAlignReferenceLabel_=new QLabel(commandPanel_);
    faceAlignReferenceLabel_->setObjectName(QStringLiteral("alignFaceReference"));
    faceAlignReferenceLabel_->hide(); commandLayout->addWidget(faceAlignReferenceLabel_);
    commandButtons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, commandPanel_);
    commandLayout->addWidget(commandButtons_);
    commandButtons_->hide();
    connect(commandButtons_, &QDialogButtonBox::accepted, this, &CadViewport::confirmCommandSession);
    connect(commandButtons_, &QDialogButtonBox::rejected, this, &CadViewport::cancelCommandSession);
    connect(faceAxisSelector_,qOverload<int>(&QComboBox::currentIndexChanged),this,&CadViewport::chooseFaceRotationAxis);
    connect(faceAlignSelector_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index) {
        if(commandSession_ && commandSession_->kind==CommandSession::Kind::AlignFace && commandSession_->hasReference) {
            commandSession_->coplanar=index==0;
            previewFaceTransform(0.0);
        }
    });
    connect(dragDimension_, &QLineEdit::textEdited, this, [this]() {
        if (commandSession_) updateCommandParameter(false);
    });
    connect(dragDimension_, &QLineEdit::returnPressed, this, &CadViewport::confirmCommandSession);
    cancelDimension_ = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(cancelDimension_, &QShortcut::activated, this, &CadViewport::cancelCommandSession);
    commandPanelPositioner_->manage(commandPanel_,true);
    const auto invalidateProjection=[this] {
        if(commandSession_ && commandSession_->overdraw) {cancelCommandSession();emit statusMessage(tr("Overdraw cancelled because the model changed."));return;}
        if(commandSession_ && commandSession_->extend) {cancelCommandSession();emit statusMessage(tr("Extend cancelled because the model changed."));return;}
        if(commandSession_ && commandSession_->trim) {cancelCommandSession();emit statusMessage(tr("Trim cancelled because the model changed."));return;}
        if(commandSession_ && commandSession_->sweep) {cancelCommandSession();emit statusMessage(tr("Profile sweep cancelled because the model changed."));return;}
        if(commandSession_ && commandSession_->projection) {
            cancelCommandSession();emit statusMessage(tr("Project 3D Geometry cancelled because the model changed."));
        }
    };
    connect(this,&CadViewport::partsChanged,this,[this]{refreshActivePart();});
    // New, restored or imported Part presentations follow the view modes.
    const auto followViewModes=[this]{if(wireframe_ || !show3DGeometry_) applyViewModes();};
    connect(this,&CadViewport::partsChanged,this,followViewModes);
    connect(this,&CadViewport::documentReplaced,this,followViewModes);
    connect(this,&CadViewport::partCreated,this,followViewModes);
    connect(this,&CadViewport::partSelectionChanged,this,[this]{refreshActivePart();});
    connect(this,&CadViewport::partsChanged,this,invalidateProjection);
    connect(this,&CadViewport::constructionsChanged,this,invalidateProjection);
    connect(this,&CadViewport::sketchEntitiesChanged,this,invalidateProjection);
}
void CadViewport::beginCommandSession(CommandSession::Kind kind,
        const std::vector<size_t>& indices, const gp_Dir& direction, const gp_Pnt& pivot) {
    if(kind!=CommandSession::Kind::Sketch) endSketchEditing();
    if(commandSession_) cancelCommandSession();
    commandSession_.emplace();
    auto& session = *commandSession_;
    session.kind = kind;
    {const QSignalBlocker block(keepBlends_);keepBlends_->setChecked(true);}
    keepBlends_->setEnabled(true);
    keepBlends_->setToolTip(tr("Preserve dependent transitions on a supporting face; OFF restores only affected sharp intersections."));
    keepBlends_->setVisible(kind==CommandSession::Kind::MoveFace||kind==CommandSession::Kind::RotateFace||kind==CommandSession::Kind::Pull);
    auto* validator=const_cast<QDoubleValidator*>(qobject_cast<const QDoubleValidator*>(dragDimension_->validator()));
    validator->setDecimals(kind==CommandSession::Kind::Fillet || kind==CommandSession::Kind::Chamfer ? 17 : 2);
    session.selection = selectedPartIndices_;
    for (context_->InitSelected(); context_->MoreSelected(); context_->NextSelected())
        session.owners.push_back(context_->SelectedOwner());
    for (const auto index : indices) {
        const auto& part = boxes_[index];
        session.changes.push_back({index,part.min,part.max,part.min,part.max,
                                   part.orientation,part.orientation});
    }
    session.direction = direction;
    session.pivot = pivot;
    session.state = CommandSession::State::Parameters;
    const QString title = kind==CommandSession::Kind::Move ? tr("Move Part Preview")
        : kind==CommandSession::Kind::Rotate ? tr("Rotate Part Preview")
        : kind==CommandSession::Kind::MoveFace ? tr("Move Face Preview")
        : kind==CommandSession::Kind::RotateFace ? tr("Rotate Face Preview")
        : kind==CommandSession::Kind::AlignFace ? tr("Align Face Preview") : tr("Modeling Preview");
    commandPanel_->setWindowTitle(title);
    dragDimension_->setParent(commandPanel_);
    commandLayout_->insertWidget(0,dragDimension_);
    dragDimension_->setToolTip(kind==CommandSession::Kind::Rotate || kind==CommandSession::Kind::RotateFace
        ? tr("Angle in degrees") : tr("Distance in mm"));
    dragDimension_->setText(QLocale().toString(0.0,'f',2));
    commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(true);
    faceAxisSelector_->setVisible(kind==CommandSession::Kind::RotateFace);
    cylinderMoveLabel_->hide(); cylinderMoveDirection_->hide();
    cylinderMoveAngle_->hide(); cylinderBossBoundary_->hide();
    moveDirectionPicking_=false;moveDirectionSource_->hide();moveDirectionPick_->hide();moveDirectionReverse_->hide();moveDirectionLabel_->hide();
    moveDirectionRestart_->hide();dragDimension_->setReadOnly(false);
    faceAlignSelector_->setVisible(kind==CommandSession::Kind::AlignFace);
    faceAlignTargetLabel_->setVisible(kind==CommandSession::Kind::AlignFace);
    faceAlignReferenceLabel_->setVisible(kind==CommandSession::Kind::AlignFace);
    if(kind==CommandSession::Kind::AlignFace && !indices.empty()) {
        faceAlignTargetLabel_->setText(tr("Target: %1").arg(boxes_[indices.front()].name));
        faceAlignReferenceLabel_->setText(tr("Reference: select a planar face"));
    }
    showCommandControls(dragStart_);
}
void CadViewport::showCommandControls(const QPoint&) {
    dragDimension_->show(); commandButtons_->show();
    showCommandPanel(commandPanel_);
}
void CadViewport::showCommandPanel(QWidget* panel) {commandPanelPositioner_->show(panel);}
bool CadViewport::previewCommand(double value) {
    if(commandSession_ && commandSession_->kind==CommandSession::Kind::DeleteFace) return commandSession_->valid;
    if (!commandSession_ || !std::isfinite(value)) return false;
    if (commandSession_->kind==CommandSession::Kind::Fillet || commandSession_->kind==CommandSession::Kind::Chamfer) return previewEdgeBlend(value);
    if (commandSession_->kind==CommandSession::Kind::MoveFace) return previewMoveFace(value);
    if (commandSession_->kind==CommandSession::Kind::RotateFace || commandSession_->kind==CommandSession::Kind::AlignFace)
        return previewFaceTransform(value);
    if (commandSession_->kind==CommandSession::Kind::Pull) return previewPrimitivePull(value);
    auto& session = *commandSession_;
    session.value = value;
    if (session.kind == CommandSession::Kind::Rotate)
        session.delta.SetRotation(gp_Ax1(session.pivot,session.direction),value*std::acos(-1.0)/180.0);
    else session.delta.SetTranslation(gp_Vec(session.direction)*value);
    for (auto& change : session.changes) {
        change.afterOrientation = session.delta * change.beforeOrientation;
        // Preview touches only AIS: canonical geometry, metadata and selection stay intact.
        boxes_[change.partIndex].presentation->SetLocalTransformation(change.afterOrientation);
        context_->Redisplay(boxes_[change.partIndex].presentation,Standard_False);
    }
    session.state = CommandSession::State::Preview;
    if (directMoveMode_) updateMoveAxes();
    if (rotateMode_) updateRotationRings();
    if (arrowVisible_ && !faceArrow_.IsNull()) {
        faceArrow_->SetLocalTransformation(session.delta);
        context_->Redisplay(faceArrow_,Standard_False);
    }
    view_->Redraw();
    return true;
}
bool CadViewport::updateCommandParameter(bool reportError) {
    if(commandSession_) commandSession_->geometryError.clear();
    // Move Face by Points has no numeric input: the points define the value.
    if(moveFaceByPoints()) return previewCommand(0.0);
    bool ok = false;
    double value = QLocale().toDouble(dimensionNumber(dragDimension_->text()), &ok);
    if (!ok || !std::isfinite(value) || !dragDimension_->hasAcceptableInput()) {
        const bool faceCommand=commandSession_ && (commandSession_->kind==CommandSession::Kind::MoveFace
            || commandSession_->kind==CommandSession::Kind::RotateFace || commandSession_->kind==CommandSession::Kind::AlignFace || commandSession_->kind==CommandSession::Kind::Fillet || commandSession_->kind==CommandSession::Kind::Chamfer);
        if(faceCommand && commandSession_->faceBefore) {
            commandSession_->valid=false;
            commandSession_->state=CommandSession::State::Parameters;
            QString error;
            presentFacePreview(*commandSession_->faceBefore,false,error);
            if(commandSession_->kind==CommandSession::Kind::MoveFace && commandSession_->moveFeature!=MoveFaceFeature::Planar
               && commandSession_->lateralDirectionReady && !moveFaceArrow_.IsNull())
                displayPullArrow(moveFaceArrow_,commandSession_->pivot,gp_Vec(commandSession_->direction));
            redrawFacePreview();
        }
        if (reportError || faceCommand) emit statusMessage(tr("Enter a valid finite angle or distance before confirming."));
        return false;
    }
    if(commandSession_ && commandSession_->kind==CommandSession::Kind::Pull && primitivePullDragging_)
        value=(value-primitivePullStartDimension())/(primitivePullIsDiameter()?2.0:1.0);
    return previewCommand(value);
}
void CadViewport::confirmCommandSession() {
    if(commandSession_ && commandSession_->sweep) {finishProfileSweep(true);return;}
    if(commandSession_ && commandSession_->kind==CommandSession::Kind::Sketch) {finishSketchEdit(true);return;}
    if(commandSession_ && commandSession_->kind==CommandSession::Kind::Construction) {
        if(constructionPanel_) constructionPanel_->finishEditing();
        if(previewConstruction()) closeConstructionSession(true);
        return;
    }
    if(commandSession_ && commandSession_->kind==CommandSession::Kind::DeleteFace) {
        if(commandSession_->valid && commandSession_->changed) closeCommandSession(true);
        return;
    }
    if(commandSession_ && commandSession_->kind==CommandSession::Kind::Boolean) {
        if(previewBooleanSession()) closeCommandSession(true);
        return;
    }
    if (commandSession_) {
        if(updateCommandParameter()) closeCommandSession(true);
        else if(!commandSession_->geometryError.isEmpty())
            reportGeometryFailure(commandPanel_->windowTitle().remove(tr(" Preview")),commandSession_->geometryError,true);
    }
}
void CadViewport::cancelCommandSession() {
    cancelTargetPicking();
    if(commandSession_ && commandSession_->sweep) {finishProfileSweep(false);return;}
    if(commandSession_ && commandSession_->kind==CommandSession::Kind::Sketch) {finishSketchEdit(false);return;}
    if(commandSession_ && commandSession_->kind==CommandSession::Kind::Construction) {
        closeConstructionSession(false);return;
    }
    moveFacePicking_=false;
    if (commandSession_) closeCommandSession(false);
    else resetModelingTools();
}
void CadViewport::closeCommandSession(bool confirm) {
    commandSession_->state = confirm ? CommandSession::State::Confirmed : CommandSession::State::Cancelled;
    const auto session = std::move(*commandSession_);
    commandSession_.reset();
    if(session.kind==CommandSession::Kind::Boolean) {
        if(!session.preview.IsNull()) context_->Remove(session.preview,Standard_False);
        delete booleanControls_; booleanControls_=nullptr;
        commandPanel_->hide(); commandButtons_->hide(); dragDimension_->hide();
        commandLayout_->removeWidget(dragDimension_);
        dragDimension_->setParent(this);
        selectedPartIndices_=session.selection;
        context_->ClearSelected(Standard_False);
        for(const auto& owner:session.owners) context_->AddOrRemoveSelected(owner,Standard_False);
        emit partSelectionChanged(selectedBooleanParts());
        if(confirm && session.booleanChange) commitBooleanChange(session.booleanChange);
        resetModelingTools(); view_->Redraw(); setFocus(); return;
    }
    const bool blendCommand=session.kind==CommandSession::Kind::Fillet || session.kind==CommandSession::Kind::Chamfer;
    if(blendCommand) for(auto& part:boxes_) if(part.active && part.visible) {
        context_->Deactivate(part.presentation);
        context_->Activate(part.presentation,AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
    }
    const bool deleteCommand=session.kind==CommandSession::Kind::DeleteFace;
    const bool pullCommand=session.kind==CommandSession::Kind::Pull;
    const bool faceCommand=session.kind==CommandSession::Kind::MoveFace || session.kind==CommandSession::Kind::RotateFace
        || session.kind==CommandSession::Kind::AlignFace || session.kind==CommandSession::Kind::Fillet || session.kind==CommandSession::Kind::Chamfer || pullCommand || deleteCommand;
    if (!session.preview.IsNull()) context_->Remove(session.preview,Standard_False);
    for(const auto& preview:session.facePreviews) context_->Remove(preview,Standard_False);
    if (!moveFaceArrow_.IsNull()) { context_->Remove(moveFaceArrow_,Standard_False); moveFaceArrow_.Nullify(); }
    for (const auto& change : session.changes) {
        auto& part = boxes_[change.partIndex];
        if (confirm && faceCommand && change.faceChange
            && ((session.kind==CommandSession::Kind::AlignFace || deleteCommand || blendCommand) ? session.changed : std::abs(session.value)>1.e-7))
            applyFaceState(change.partIndex,change.faceChange->after);
        else if(pullCommand && change.faceChange) applyFaceState(change.partIndex,change.faceChange->before);
        else if (confirm) part.orientation = change.afterOrientation;
        if (faceCommand && part.visible) {
            context_->Display(part.presentation,Standard_False);
            context_->Deactivate(part.presentation);
            context_->Activate(part.presentation,AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
        }
        part.presentation->SetLocalTransformation(part.orientation);
        context_->Redisplay(part.presentation,Standard_False);
    }
    const bool changedFace=(session.kind==CommandSession::Kind::AlignFace || deleteCommand || blendCommand) ? session.changed : std::abs(session.value)>1.e-7;
    if (confirm && (faceCommand ? changedFace : std::abs(session.value)>1.e-10) && !session.changes.empty()) {
        PullChange operation = session.changes.front();
        if (session.changes.size()>1) operation.groupedChanges = session.changes;
        operation.debugOperation=debugCommand(session);
        operation.debugOperation["available"]=true;
        undoStack_.push_back(operation);
        redoStack_.clear();
        updateHistoryActions();
    }
    selectedPartIndices_ = session.selection;
    context_->ClearSelected(Standard_False);
    if (faceCommand && confirm && changedFace) {
        selectedPartIndices_.clear();
        for(const auto& change:session.changes) selectedPartIndices_.push_back(change.partIndex);
        refreshPartSelection();
        emit partSelectionChanged(selectedPartIndices_);
        emit partsChanged();
    } else {
        for (const auto& owner : session.owners) context_->AddOrRemoveSelected(owner,Standard_False);
        if (confirm && !session.changes.empty()
            && (faceCommand ? changedFace : std::abs(session.value) > 1.e-10))
            emit partsChanged();
    }
    draggingFace_ = draggingRotation_ = dimensionEditing_ = false;
    groupRotationDragging_ = false;
    dragDimension_->hide(); commandButtons_->hide(); commandPanel_->hide();
    faceAxisSelector_->hide(); faceAlignSelector_->hide();
    cylinderMoveLabel_->hide(); cylinderMoveDirection_->hide();
    cylinderMoveAngle_->hide(); cylinderBossBoundary_->hide();
    moveDirectionRestart_->hide();dragDimension_->setReadOnly(false);
    commandLayout_->removeWidget(dragDimension_);
    dragDimension_->setParent(this);
    resetModelingTools();
    if(faceCommand) redrawFacePreview();
    else view_->Redraw();
    setFocus();
    emit statusMessage(confirm ? tr("Transformation confirmed.") : tr("Transformation cancelled."));
}
bool CadViewport::eventFilter(QObject* watched, QEvent* event) {
    if(!commandSession_ && (pullMode_ || movePartMode_ || directMoveMode_ || rotateMode_ || moveFacePicking_)
        && (event->type()==QEvent::ShortcutOverride || event->type()==QEvent::KeyPress)) {
        const auto* widget=qobject_cast<QWidget*>(watched);
        const auto* key=static_cast<QKeyEvent*>(event);
        if(widget && (widget==this || isAncestorOf(widget))
            && (key->key()==Qt::Key_Escape || key->key()==Qt::Key_Return || key->key()==Qt::Key_Enter)) {
            event->accept();
            if(event->type()==QEvent::KeyPress) cancelCommandSession();
            return true;
        }
    }
    if (commandSession_ && (event->type()==QEvent::ShortcutOverride || event->type()==QEvent::KeyPress)) {
        const auto* widget=qobject_cast<QWidget*>(watched);
        const auto* key=static_cast<QKeyEvent*>(event);
        const bool sessionWidget=widget && (widget==this || isAncestorOf(widget)
            || widget==commandPanel_ || commandPanel_->isAncestorOf(widget)
            || (constructionPanel_ && (widget==constructionPanel_ || constructionPanel_->isAncestorOf(widget)))
            || (sketchDrawingPanel_ && (widget==sketchDrawingPanel_ || sketchDrawingPanel_->isAncestorOf(widget)))
            || (sketchOverdrawPanel_ && (widget==sketchOverdrawPanel_ || sketchOverdrawPanel_->isAncestorOf(widget)))
            || (sketchConstructionPanel_ && (widget==sketchConstructionPanel_ || sketchConstructionPanel_->isAncestorOf(widget)))
            || (sketchProjectionPanel_ && (widget==sketchProjectionPanel_ || sketchProjectionPanel_->isAncestorOf(widget)))
            || (commandSession_->kind==CommandSession::Kind::Boolean && widget && widget->window()==window()));
        if(sessionWidget && key->modifiers()==Qt::ControlModifier
            && (key->key()==Qt::Key_Z || key->key()==Qt::Key_Y)) {
            // The floating parameter editor is a separate tool window. Route
            // history keys to the same actions instead of its text undo buffer.
            auto* action=key->key()==Qt::Key_Z?undoAction_:redoAction_;
            if(action->isEnabled()) {
                event->accept();
                if(event->type()==QEvent::KeyPress) action->trigger();
                return true;
            }
        }
        if(sessionWidget && commandSession_->overdraw && key->key()==Qt::Key_Backspace) {
            event->accept();if(event->type()==QEvent::KeyPress) backOverdrawPiece();return true;
        }
        if (sessionWidget && (key->key()==Qt::Key_Escape || key->key()==Qt::Key_Return || key->key()==Qt::Key_Enter)) {
            event->accept();
            if (event->type()==QEvent::KeyPress) {
                if (key->key()==Qt::Key_Escape) {if(targetPicking()) cancelTargetPicking();else cancelCommandSession();}
                else if(commandSession_->drawing && commandSession_->drawing->tool==Sketch::DrawTool::Spline) confirmCommandSession();
                else if(commandSession_->drawing && !commandSession_->drawing->complete && sketchDrawingPanel_) {
                    const bool polar=sketchDrawingPanel_->parameterWidget(widget);
                    useSketchNumeric(polar);
                    if(commandSession_ && commandSession_->drawing && Sketch::orthogonalLineTool(commandSession_->drawing->tool)) confirmCommandSession();
                } else confirmCommandSession();
            }
            return true;
        }
    }
    if (watched == commandPanel_ && event->type() == QEvent::Close) {
        cancelCommandSession();
        return true;
    }
    return QWidget::eventFilter(watched,event);
}
QPaintEngine* CadViewport::paintEngine() const { return nullptr; }
namespace {
class HiddenSolidFilter final : public SelectMgr_Filter {
public:
    explicit HiddenSolidFilter(std::function<bool(const Handle(SelectMgr_SelectableObject)&)> hidden) : hidden_(std::move(hidden)) {}
    Standard_Boolean IsOk(const Handle(SelectMgr_EntityOwner)& owner) const override {
        return owner.IsNull() || !hidden_(owner->Selectable());
    }
private:
    std::function<bool(const Handle(SelectMgr_SelectableObject)&)> hidden_;
};
}
int CadViewport::partDisplayMode() const {return wireframe_?AIS_WireFrame:AIS_Shaded;}
void CadViewport::applyViewModes() {
    if(context_.IsNull() || view_.IsNull()) return;
    for(const auto& part:boxes_) if(!part.presentation.IsNull()) {
        context_->SetViewAffinity(part.presentation,view_,show3DGeometry_);
        // Erased (individually hidden) Parts keep the mode for their next Display.
        if(context_->IsDisplayed(part.presentation)) context_->SetDisplayMode(part.presentation,partDisplayMode(),Standard_False);
        else part.presentation->SetDisplayMode(partDisplayMode());
    }
    if(!show3DGeometry_) {
        // Hidden solids cannot stay selected or highlighted.
        context_->ClearDetected(Standard_False);
        if(!selectedPartIndices_.empty() || selectionContext().kind==SelectionContext::Kind::Faces || selectionContext().kind==SelectionContext::Kind::Edges) {
            context_->ClearSelected(Standard_False);selectedPartIndices_.clear();emit partSelectionChanged({});
        }
    }
    view_->Invalidate();view_->Redraw();
}
void CadViewport::setWireframe(bool wireframe) {
    if(wireframe_==wireframe) return;
    initializeViewer();wireframe_=wireframe;applyViewModes();emit viewModesChanged();
}
void CadViewport::setShow3DGeometry(bool show) {
    if(show3DGeometry_==show) return;
    initializeViewer();
    if(!show) cancelCommandSession();
    show3DGeometry_=show;applyViewModes();emit viewModesChanged();
    emit statusMessage(show?tr("3D geometry shown."):tr("3D geometry hidden (view only); Part visibility is unchanged."));
}
bool CadViewport::constructionShown(const Construction::Object& object) const {
    return object.visible && (showWorkplanes_ || object.kind!=Construction::Kind::Workplane);
}
void CadViewport::setShowWorkplanes(bool show) {
    if(showWorkplanes_==show) return;
    initializeViewer();
    // Never keep editing geometry that is about to become invisible.
    if(!show) {endSketchEditing();cancelCommandSession();}
    showWorkplanes_=show;
    if(!show) {
        // Hidden Workplanes and their Sketches cannot stay selected.
        QStringList kept;
        for(const auto& id:selectedConstructionIds_) if(const auto* o=constructionObject(id);o && o->kind!=Construction::Kind::Workplane) kept.push_back(id);
        if(kept!=selectedConstructionIds_) {selectedConstructionIds_=kept;emit constructionSelectionChanged(kept);}
        clearSketchSelection();
    }
    // Presentation only: Display/Erase the existing presentations. Objects
    // whose own visible flag is off are never displayed, so individually
    // hidden Workplanes and Sketch entities stay hidden after showing again.
    // Part selection is not touched.
    if(!context_.IsNull()) {
        context_->ClearDetected(Standard_False);
        for(const auto& o:constructions_) if(o.kind==Construction::Kind::Workplane && o.visible) {
            const auto p=constructionPresentations_.find(o.id);const auto label=constructionLabels_.find(o.id);
            if(p==constructionPresentations_.end() || label==constructionLabels_.end()) continue;
            if(show) {
                context_->Display(label->second,0,-1,Standard_False);context_->Deactivate(label->second);
                context_->Display(p->second,Standard_False);context_->Deactivate(p->second);
                if(o.selectable && activeSketchWorkplaneId_.isEmpty()) context_->Activate(p->second,AIS_Shape::SelectionMode(TopAbs_EDGE),Standard_False);
            } else {
                if(context_->IsSelected(p->second)) context_->AddOrRemoveSelected(p->second,Standard_False);
                context_->Erase(label->second,Standard_False);context_->Erase(p->second,Standard_False);
            }
        }
        for(const auto& e:sketchEntities_) if(const auto it=sketchPresentations_.find(e.id);it!=sketchPresentations_.end()) {
            if(!show) {context_->Erase(it->second,Standard_False);continue;}
            if(const auto* owner=constructionObject(e.workplaneId)) displaySketchPresentation(e,*owner,it->second);
        }
        if(show) {updateConstructionLabels();if(!activeSketchWorkplaneId_.isEmpty()) refreshSketchPicking();}
        view_->Invalidate();view_->Redraw();
    }
    emit viewModesChanged();
    emit statusMessage(show?tr("Workplanes shown."):tr("Workplanes and their Sketches hidden (view only); Workplane and Sketch visibility is unchanged."));
}
void CadViewport::initializeViewer() {
    if (!view_.IsNull()) return;
    display_ = new Aspect_DisplayConnection();
    Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(display_);
    viewer_ = new V3d_Viewer(driver);
    viewer_->SetDefaultLights();
    viewer_->SetLightOn();
    context_ = new AIS_InteractiveContext(viewer_);
    // View-level Hide 3D Geometry: one filter keeps hidden solids out of every
    // detection/selection path without touching per-Part visibility.
    context_->AddFilter(new HiddenSolidFilter([this](const Handle(SelectMgr_SelectableObject)& object) {
        if(!show3DGeometry_ && std::any_of(boxes_.begin(),boxes_.end(),[&](const CadPart& part){return part.presentation==object;})) return true;
        // View-level Hide Workplanes: frames and Sketch geometry are not
        // displayed; this filter is the second guard on every pick path.
        if(showWorkplanes_) return false;
        for(const auto& [id,p]:sketchPresentations_) if(p==object) return true;
        for(const auto& o:constructions_) if(o.kind==Construction::Kind::Workplane)
            if(const auto it=constructionPresentations_.find(o.id);it!=constructionPresentations_.end() && it->second==object) return true;
        return false;
    }));
    configureSelectionHighlights();
    view_ = viewer_->CreateView();
#if defined(Q_OS_WIN)
    Handle(WNT_Window) window = new WNT_Window(reinterpret_cast<Aspect_Handle>(winId()));
#else
    Handle(Aspect_NeutralWindow) window = new Aspect_NeutralWindow();
    window->SetNativeHandle(static_cast<Aspect_Drawable>(winId()));
    window->SetSize(width(), height());
#endif
    view_->SetWindow(window);
    if (!window->IsMapped()) window->Map();
    view_->SetBackgroundColor(Quantity_NOC_GRAY20);
    view_->MustBeResized();
    view_->SetProj(V3d_XposYnegZpos);
    view_->FitAll();
    stabilizeViewCamera();
}
void CadViewport::stabilizeViewCamera() {if(!view_.IsNull()) stabilizeOrthographicCamera(view_->Camera());}
void CadViewport::paintEvent(QPaintEvent*) {
    initializeViewer();
    refreshActivePart(false);
    if (!view_.IsNull()) {updateConstructionLabels();view_->Redraw();}
}
void CadViewport::configureSelectionHighlights() {
    if(context_.IsNull()) return;
    const Quantity_Color cyan(0.0,1.0,1.0,Quantity_TOC_sRGB);
    for(auto type:{Prs3d_TypeOfHighlight_Selected,Prs3d_TypeOfHighlight_LocalSelected,
                   Prs3d_TypeOfHighlight_Dynamic,Prs3d_TypeOfHighlight_LocalDynamic}) {
        auto style=context_->HighlightStyle(type);
        const bool hovered=type==Prs3d_TypeOfHighlight_Dynamic||type==Prs3d_TypeOfHighlight_LocalDynamic;
        const Quantity_Color color=hovered?cyan:Quantity_Color(135./255,206./255,250./255,Quantity_TOC_sRGB);
        style->SetColor(color);style->SetDisplayMode(AIS_WireFrame);
        style->SetZLayer(Graphic3d_ZLayerId_Top);style->SetOwnLineAspects();
        const double width=(type==Prs3d_TypeOfHighlight_Dynamic||type==Prs3d_TypeOfHighlight_LocalDynamic?4.:2.)*devicePixelRatioF();
        for(const auto& aspect:{style->WireAspect(),style->LineAspect(),style->FreeBoundaryAspect(),style->UnFreeBoundaryAspect(),style->SeenLineAspect()}) {
            aspect->SetColor(color);aspect->SetWidth(width);
        }
    }
    context_->SetToHilightSelected(true);
}
void CadViewport::leaveEvent(QEvent* event) {
    if(!context_.IsNull()) {context_->ClearDetected(Standard_False);refreshActivePart();}
    QWidget::leaveEvent(event);
}
void CadViewport::resizeEvent(QResizeEvent*) {
    configureSelectionHighlights();
    if (!view_.IsNull()) {
        Handle(Aspect_NeutralWindow) window = Handle(Aspect_NeutralWindow)::DownCast(view_->Window());
        if (!window.IsNull()) window->SetSize(width(), height());
        view_->MustBeResized();
        updateConstructionLabels();
    }
}
void CadViewport::hideFaceArrow() {
    arrowVisible_ = false;
    for (auto& arrow : coneArrows_) {
        if (!arrow.IsNull() && !context_.IsNull()) context_->Remove(arrow,Standard_False);
        arrow.Nullify();
    }
    if (!faceArrow_.IsNull() && !context_.IsNull()) {
        context_->Remove(faceArrow_, Standard_False);
        faceArrow_.Nullify();
    }
}
void CadViewport::updateFaceArrow() {
    if (!arrowVisible_ || arrowPartIndex_ >= boxes_.size() || context_.IsNull()) return;
    const auto& part = boxes_[arrowPartIndex_];
    if (part.conePrimitive && !movePartMode_) {
        for (size_t i = 0; i < coneArrows_.size(); ++i) {
            const auto target = static_cast<ConePullTarget>(i);
            displayPullArrow(coneArrows_[i],
                coneArrowOrigin(part,target).Transformed(part.orientation),
                gp_Vec(coneArrowDirection(target)).Transformed(part.orientation));
            context_->SetZLayer(coneArrows_[i],Graphic3d_ZLayerId_Topmost);
        }
        return;
    }
    gp_Pnt center = (part.cylinderPrimitive || part.spherePrimitive || part.conePrimitive || cylindricalFacePull_) && !movePartMode_ ? primitiveArrowOrigin_ :
        gp_Pnt((part.min[0]+part.max[0])*0.5,(part.min[1]+part.max[1])*0.5,
               (part.min[2]+part.max[2])*0.5);
    gp_Vec direction = (part.cylinderPrimitive || part.spherePrimitive || part.conePrimitive || cylindricalFacePull_) && !movePartMode_ ?
        gp_Vec(primitiveArrowDirection_) : gp_Vec(0,0,0);
    if ((!part.cylinderPrimitive && !part.spherePrimitive && !part.conePrimitive && !cylindricalFacePull_) || movePartMode_) {
        center.SetCoord(arrowAxis_+1,arrowMax_?part.max[arrowAxis_]:part.min[arrowAxis_]);
        direction.SetCoord(arrowAxis_+1,arrowMax_?1.0:-1.0);
    }
    center.Transform(part.orientation);
    direction.Transform(part.orientation);
    displayPullArrow(faceArrow_,center,direction);
    if (cylindricalFacePull_)
        context_->SetZLayer(faceArrow_, Graphic3d_ZLayerId_Topmost);
}
void CadViewport::displayPullArrow(Handle(AIS_Shape)& arrow, gp_Pnt center, gp_Vec direction) {
    constexpr double shaftLength = 22.0;
    const gp_Ax2 shaftAxis(center,gp_Dir(direction));
    const gp_Pnt tipBase = center.Translated(direction*shaftLength);
    const gp_Ax2 tipAxis(tipBase,gp_Dir(direction));
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    builder.Add(compound,BRepPrimAPI_MakeCylinder(shaftAxis,1.6,shaftLength).Shape());
    builder.Add(compound,BRepPrimAPI_MakeCone(tipAxis,4.5,0.0,9.0).Shape());
    if (arrow.IsNull()) {
        arrow = new AIS_Shape(compound);
        arrow->SetColor(Quantity_NOC_ORANGE);
        context_->Display(arrow,Standard_False);
        context_->SetDisplayMode(arrow,AIS_Shaded,Standard_False);
        context_->Deactivate(arrow);
    } else {
        arrow->SetShape(compound);
        context_->Redisplay(arrow,Standard_False);
        context_->Deactivate(arrow);
    }
}

void CadViewport::addBox() {
    endSketchEditing();
    cancelCommandSession();
    initializeViewer();
    const double x = partCount_ * 125.0;
    CadPart part;
    part.persistentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    part.min = {x, 0.0, 0.0};
    part.max = {x + 100.0, 80.0, 60.0};
    TopoDS_Shape shape = BRepPrimAPI_MakeBox(gp_Pnt(x, 0, 0), 100.0, 80.0, 60.0).Shape();
    part.shape = shape;
    part.presentation = new PartPresentation(part.shape);
    part.presentation->SetColor(Quantity_NOC_LIGHTGRAY);
    context_->Display(part.presentation, Standard_False);
    context_->SetDisplayMode(part.presentation, partDisplayMode(), Standard_False);
    // Disable global object selection (mode 0) before enabling subshape mode.
    context_->Deactivate(part.presentation);
    context_->Activate(part.presentation, AIS_Shape::SelectionMode(TopAbs_FACE),
        Standard_False);
    boxes_.push_back(part);
    ++partCount_;
    announcePartCreated(QString("Part %1").arg(partCount_));
    fitAll();
}
void CadViewport::addCylinder() {
    endSketchEditing();
    cancelCommandSession();
    initializeViewer();
    const double x = partCount_ * 125.0;
    constexpr double radius = 40.0;
    constexpr double height = 80.0;
    CadPart part;
    part.persistentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    part.geometryKind = CadPart::GeometryKind::GeneralBRep;
    part.cylinderPrimitive = true;
    part.cylinderRadius = radius;
    part.cylinderHeight = height;
    // Bounds are currently used to place the shared move/rotate manipulators.
    part.min = {x - radius, -radius, 0.0};
    part.max = {x + radius, radius, height};
    part.baseMin = part.min;
    part.baseMax = part.max;
    part.shape = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x, 0, 0),
        gp_Dir(0, 0, 1)), radius, height).Shape();
    part.presentation = new PartPresentation(part.shape);
    part.presentation->SetColor(Quantity_NOC_LIGHTGRAY);
    context_->Display(part.presentation, Standard_False);
    context_->SetDisplayMode(part.presentation, partDisplayMode(), Standard_False);
    context_->Deactivate(part.presentation);
    context_->Activate(part.presentation, AIS_Shape::SelectionMode(TopAbs_FACE),
        Standard_False);
    boxes_.push_back(part);
    ++partCount_;
    announcePartCreated(QString("Cylinder %1").arg(partCount_));
    fitAll();
}
void CadViewport::addSphere() {
    endSketchEditing();
    cancelCommandSession();
    initializeViewer();
    const double x = partCount_ * 125.0;
    constexpr double radius = 40.0;
    CadPart part;
    part.persistentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    part.geometryKind = CadPart::GeometryKind::GeneralBRep;
    part.spherePrimitive = true;
    part.sphereRadius = radius;
    part.min = {x-radius,-radius,-radius};
    part.max = {x+radius,radius,radius};
    part.baseMin = part.min;
    part.baseMax = part.max;
    part.shape = BRepPrimAPI_MakeSphere(gp_Pnt(x,0,0),radius).Shape();
    part.presentation = new PartPresentation(part.shape);
    part.presentation->SetColor(Quantity_NOC_LIGHTGRAY);
    context_->Display(part.presentation,Standard_False);
    context_->SetDisplayMode(part.presentation,partDisplayMode(),Standard_False);
    context_->Deactivate(part.presentation);
    context_->Activate(part.presentation,AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
    boxes_.push_back(part);
    ++partCount_;
    announcePartCreated(QString("Sphere %1").arg(partCount_));
    fitAll();
}
void CadViewport::addCone() {
    endSketchEditing();
    cancelCommandSession();
    initializeViewer();
    const double x = partCount_*125.0;
    CadPart part;
    part.persistentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    part.geometryKind = CadPart::GeometryKind::GeneralBRep;
    part.conePrimitive = true;
    part.min = {x-40.0,-40.0,0.0};
    part.max = {x+40.0,40.0,80.0};
    part.baseMin = part.min;
    part.baseMax = part.max;
    part.shape = BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(x,0,0),gp_Dir(0,0,1)),
        part.cone.bottomRadius,part.cone.topRadius,part.cone.height).Shape();
    part.presentation = new PartPresentation(part.shape);
    part.presentation->SetColor(Quantity_NOC_LIGHTGRAY);
    context_->Display(part.presentation,Standard_False);
    context_->SetDisplayMode(part.presentation,partDisplayMode(),Standard_False);
    context_->Deactivate(part.presentation);
    context_->Activate(part.presentation,AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
    boxes_.push_back(part);
    ++partCount_;
    announcePartCreated(QString("Cone %1").arg(partCount_));
    fitAll();
}
void CadViewport::duplicateSelectedParts() {
    endSketchEditing();
    cancelCommandSession();
    initializeViewer();
    // A face selection identifies its owning part as well. Allow Ctrl+D
    // directly after clicking a cylinder face, without requiring Alt+click.
    if (selectedPartIndices_.empty() && !context_.IsNull()) {
        for (context_->InitSelected(); context_->MoreSelected(); context_->NextSelected()) {
            const auto object = context_->SelectedInteractive();
            for (size_t i = 0; i < boxes_.size(); ++i) {
                if (boxes_[i].presentation == object &&
                    std::find(selectedPartIndices_.begin(), selectedPartIndices_.end(), i)
                        == selectedPartIndices_.end())
                    selectedPartIndices_.push_back(i);
            }
        }
    }
    if (selectedPartIndices_.empty()) {
        emit statusMessage(tr("Select one or more parts with Alt+click or in the Structure Browser."));
        return;
    }
    // Snapshot indices: newly appended copies must not become inputs to this operation.
    const auto sources = selectedPartIndices_;
    std::vector<size_t> copies;
    constexpr double offsetMm = 25.0;
    for (const size_t index : sources) {
        if (!isPartVisible(index)) continue;
        const CadPart original = boxes_[index];
        CadPart copy;
        copy.persistentId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        copy.geometryKind = original.geometryKind;
        copy.blends = original.blends;
        copy.cylinderPrimitive = original.cylinderPrimitive;
        copy.spherePrimitive = original.spherePrimitive;
        copy.conePrimitive = original.conePrimitive;
        copy.cone = original.cone;
        copy.color = original.color;
        copy.sphereRadius = original.sphereRadius;
        copy.cylinderRadius = original.cylinderRadius;
        copy.cylinderHeight = original.cylinderHeight;
        copy.cylinderBaseOffset = original.cylinderBaseOffset;
        copy.min = original.min;
        copy.max = original.max;
        copy.baseMin = original.baseMin;
        copy.baseMax = original.baseMax;
        copy.orientation = original.orientation;
        // Translate in world X without altering the part's local dimensions or rotation.
        gp_Trsf translation;
        translation.SetTranslation(gp_Vec(offsetMm, 0.0, 0.0));
        copy.orientation = translation * original.orientation;
        copy.visible = true;
        // Preserve the source B-Rep instead of reconstructing a box.
        // TopoDS_Shape is reference-counted and can be shared across instances.
        copy.shape = original.shape;
        copy.presentation = new PartPresentation(copy.shape);
        copy.presentation->SetColor(Quantity_Color(copy.color.redF(),copy.color.greenF(),copy.color.blueF(),Quantity_TOC_RGB));
        copy.presentation->SetLocalTransformation(copy.orientation);
        context_->Display(copy.presentation, Standard_False);
        context_->SetDisplayMode(copy.presentation, partDisplayMode(), Standard_False);
        context_->Deactivate(copy.presentation);
        context_->Activate(copy.presentation,
            selectionMode_==SelectionMode::Part ? 0 : AIS_Shape::SelectionMode(TopAbs_FACE),
            Standard_False);
        boxes_.push_back(copy);
        copies.push_back(boxes_.size() - 1);
        ++partCount_;
        announcePartCreated(QString("Part %1").arg(partCount_));
    }
    if (copies.empty()) return;
    selectParts(copies);
    emit statusMessage(tr("%1 part(s) duplicated. Use Move Part to position copies.")
        .arg(copies.size()));
}

void CadViewport::pullSelectedFace() {
    cancelCommandSession();
    if (context_.IsNull() || context_->NbSelected() == 0) {
        emit statusMessage(tr("Select one or more planar box faces first."));
        return;
    }

    // Collect every selected boundary before modifying any geometry.
    // A part can contribute multiple faces, but must be rebuilt only once.
    struct Pending {
        size_t index;
        std::array<double, 3> min, max;
        std::array<bool, 6> sides{};
    };
    std::vector<Pending> pending;
    int faceCount = 0;
    for (context_->InitSelected(); context_->MoreSelected(); context_->NextSelected()) {
        const Handle(StdSelect_BRepOwner) owner =
            Handle(StdSelect_BRepOwner)::DownCast(context_->SelectedOwner());
        if (owner.IsNull() || owner->Shape().IsNull()
            || owner->Shape().ShapeType() != TopAbs_FACE) {
            emit statusMessage(tr("Pull requires face selection (not whole parts)."));
            return;
        }
        const Handle(AIS_InteractiveObject) object = context_->SelectedInteractive();
        size_t index = 0;
        while (index < boxes_.size() && boxes_[index].presentation != object) ++index;
        if (index == boxes_.size() || boxes_[index].geometryKind != CadPart::GeometryKind::Box) {
            emit statusMessage(tr("Selected face does not belong to an editable box. Use its Pull arrows."));
            return;
        }
        const BRepAdaptor_Surface surface(TopoDS::Face(owner->Shape()));
        if (surface.GetType() != GeomAbs_Plane) {
            emit statusMessage(tr("Only planar box faces are supported."));
            return;
        }
        const gp_Dir normal = surface.Plane().Axis().Direction();
        const double components[3] = {normal.X(), normal.Y(), normal.Z()};
        int axis = 0;
        for (int k = 1; k < 3; ++k)
            if (std::abs(components[k]) > std::abs(components[axis])) axis = k;
        if (std::abs(components[axis]) < 0.999) {
            emit statusMessage(tr("Only box boundary faces are supported."));
            return;
        }
        const auto& part = boxes_[index];
        const double position = surface.Plane().Location().Coord(axis + 1);
        const bool onMax = std::abs(position - part.max[axis]) < 0.001;
        const bool onMin = std::abs(position - part.min[axis]) < 0.001;
        if (!onMax && !onMin) {
            emit statusMessage(tr("Could not match a selected face to a box boundary."));
            return;
        }
        auto it = std::find_if(pending.begin(), pending.end(),
            [index](const Pending& p) { return p.index == index; });
        if (it == pending.end()) {
            pending.push_back({index, part.min, part.max, {}});
            it = std::prev(pending.end());
        }
        const int side = axis * 2 + (onMax ? 1 : 0);
        if (!it->sides[side]) {
            it->sides[side] = true;
            ++faceCount;
        }
    }
    if (pending.empty()) return;

    bool ok = false;
    const double distance = QInputDialog::getDouble(this, tr("Pull Selected Faces"),
        tr("Outward distance for all selected faces (mm; negative = inward):"),
        20.0, -100000.0, 100000.0, 2, &ok);
    if (!ok || std::abs(distance) < 0.000001) return;

    std::vector<PullChange> changes;
    for (auto& item : pending) {
        const auto& part = boxes_[item.index];
        for (int axis = 0; axis < 3; ++axis) {
            if (item.sides[axis * 2]) item.min[axis] -= distance;
            if (item.sides[axis * 2 + 1]) item.max[axis] += distance;
            if (item.max[axis] - item.min[axis] <= 0.001) {
                reportGeometryFailure(tr("Pull / Resize"),tr("Pull would collapse or invert a selected box."),true);
                return;
            }
        }
        changes.push_back({item.index, part.min, part.max, item.min, item.max,
                           part.orientation, part.orientation});
    }

    // Store a single grouped undo operation even when only one part has
    // multiple selected faces.
    PullChange operation = changes.front();
    if (changes.size() > 1) operation.groupedChanges = changes;
    undoStack_.push_back(operation);
    redoStack_.clear();
    updateHistoryActions();
    hideFaceArrow();
    batchingPartUpdates_ = true;
    for (const auto& change : changes)
        applyPartState(change.partIndex, change.afterMin, change.afterMax,
                       change.afterOrientation);
    batchingPartUpdates_ = false;
    context_->ClearSelected(Standard_False); // Old face owners refer to replaced geometry.
    view_->Redraw();
    emit statusMessage(tr("%1 selected face(s) pulled by %2 mm across %3 part(s).")
        .arg(faceCount).arg(distance).arg(changes.size()));
    emit partsChanged();
}
void CadViewport::rotateSelectedPart() {
    endSketchEditing();
    cancelCommandSession();
    const auto indices = selectedBooleanParts();
    if (context_.IsNull() || indices.empty()) {
        emit statusMessage(tr("Select a face or part to rotate."));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Rotate Part"));
    QFormLayout layout(&dialog);
    QComboBox axisBox(&dialog);
    axisBox.addItems({tr("X"), tr("Y"), tr("Z")});
    QDoubleSpinBox angleBox(&dialog);
    angleBox.setRange(-3600.0, 3600.0);
    angleBox.setDecimals(2);
    angleBox.setSuffix(tr(" deg"));
    angleBox.setValue(90.0);
    layout.addRow(tr("World axis"), &axisBox);
    layout.addRow(tr("Angle"), &angleBox);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || std::abs(angleBox.value()) < 0.00001) return;
    const gp_Pnt worldCenter = rotationCenter(indices);
    const gp_Dir direction = axisBox.currentIndex() == 0 ? gp_Dir(1, 0, 0)
                           : axisBox.currentIndex() == 1 ? gp_Dir(0, 1, 0)
                                                          : gp_Dir(0, 0, 1);
    gp_Trsf rotation;
    rotation.SetRotation(gp_Ax1(worldCenter, direction), angleBox.value() * 3.14159265358979323846 / 180.0);
    std::vector<PullChange> changes;
    for (const auto index : indices) {
        const auto& part = boxes_[index];
        changes.push_back({index, part.min, part.max, part.min, part.max,
                           part.orientation, rotation * part.orientation});
    }
    PullChange operation = changes.front();
    if (changes.size() > 1) operation.groupedChanges = changes;
    undoStack_.push_back(operation);
    redoStack_.clear();
    updateHistoryActions();
    hideFaceArrow();
    context_->ClearSelected(Standard_False); // Face owners may refer to rebuilt geometry.
    batchingPartUpdates_ = true;
    for (const auto& change : changes)
        applyPartState(change.partIndex, change.afterMin, change.afterMax, change.afterOrientation);
    batchingPartUpdates_ = false;
    if (!selectedPartIndices_.empty()) refreshPartSelection();
    else {
        if (rotateMode_) updateRotationRings();
        if (directMoveMode_) updateMoveAxes();
        view_->Redraw();
    }
    emit statusMessage(tr("Part rotated %1 degrees around world %2 axis.")
        .arg(angleBox.value()).arg(axisBox.currentText()));
    emit partsChanged();
}
void CadViewport::movePartAlongSelectedFace() {
    endSketchEditing();
    cancelCommandSession();
    if (context_.IsNull() || context_->NbSelected() != 1) {
        emit statusMessage(tr("Select a box face to choose the move direction."));
        return;
    }
    context_->InitSelected();
    if (!context_->MoreSelected()) return;
    Handle(StdSelect_BRepOwner) owner =
        Handle(StdSelect_BRepOwner)::DownCast(context_->SelectedOwner());
    if (owner.IsNull() || owner->Shape().IsNull()
        || owner->Shape().ShapeType() != TopAbs_FACE) {
        emit statusMessage(tr("Select a planar box face."));
        return;
    }
    const Handle(AIS_InteractiveObject) selected = context_->SelectedInteractive();
    size_t index = 0;
    while (index < boxes_.size() && boxes_[index].presentation != selected) ++index;
    if (index == boxes_.size() || boxes_[index].geometryKind != CadPart::GeometryKind::Box) {
        emit statusMessage(tr("Selected face does not belong to an editable box."));
        return;
    }
    const BRepAdaptor_Surface surface(TopoDS::Face(owner->Shape()));
    if (surface.GetType() != GeomAbs_Plane) {
        emit statusMessage(tr("Only planar box faces are supported."));
        return;
    }
    const gp_Dir normal = surface.Plane().Axis().Direction();
    const double components[3] = {normal.X(), normal.Y(), normal.Z()};
    int axis = 0;
    for (int i = 1; i < 3; ++i)
        if (std::abs(components[i]) > std::abs(components[axis])) axis = i;
    if (std::abs(components[axis]) < 0.999) {
        emit statusMessage(tr("Only axis-aligned faces are supported."));
        return;
    }
    const double pos = surface.Plane().Location().Coord(axis + 1);
    const bool onMax = std::abs(pos - boxes_[index].max[axis]) < 0.001;
    const bool onMin = std::abs(pos - boxes_[index].min[axis]) < 0.001;
    if (!onMax && !onMin) return;
    bool ok = false;
    const double distance = QInputDialog::getDouble(
        this, tr("Move Part"), tr("Distance along selected face outward normal (mm):"),
        20.0, -100000.0, 100000.0, 2, &ok);
    if (!ok || distance == 0.0) return;
    const auto beforeMin = boxes_[index].min;
    const auto beforeMax = boxes_[index].max;
    auto afterMin = beforeMin;
    auto afterMax = beforeMax;
    const double delta = (onMax ? 1.0 : -1.0) * distance;
    afterMin[axis] += delta;
    afterMax[axis] += delta;
    undoStack_.push_back({index, beforeMin, beforeMax, afterMin, afterMax, boxes_[index].orientation, boxes_[index].orientation});
    redoStack_.clear();
    updateHistoryActions();
    applyBoxBounds(index, afterMin, afterMax);
    emit statusMessage(tr("Part translated %1 mm; dimensions unchanged.").arg(distance));
    emit partsChanged();
}
gp_Pnt CadViewport::partLocalCenter(const CadPart& part) const {
    const auto& min = part.geometryKind == CadPart::GeometryKind::GeneralBRep
        ? part.baseMin : part.min;
    const auto& max = part.geometryKind == CadPart::GeometryKind::GeneralBRep
        ? part.baseMax : part.max;
    return gp_Pnt((min[0] + max[0]) * 0.5,
                  (min[1] + max[1]) * 0.5,
                  (min[2] + max[2]) * 0.5);
}
gp_Pnt CadViewport::partLocalGizmoCenter(const CadPart& part) const {
    // The canonical B-Rep is local geometry. Callers apply the instance transform
    // exactly once, for display, picking and the actual rotation pivot alike.
    // Keep Pull's original local reference center separate from this live center.
    if (!part.shape.IsNull()) {
        GProp_GProps properties;
        BRepGProp::VolumeProperties(part.shape,properties);
        if (std::isfinite(properties.Mass()) && std::abs(properties.Mass()) > 1.0e-12) {
            const gp_Pnt center = properties.CentreOfMass();
            if (std::isfinite(center.X()) && std::isfinite(center.Y()) && std::isfinite(center.Z()))
                return center;
        }
    }
    // Retain the previous placement for shapes without usable volume properties.
    return partLocalCenter(part);
}
gp_Pnt CadViewport::partWorldRotationCenter(const CadPart& part, const gp_Trsf& orientation) const {
    return partLocalGizmoCenter(part).Transformed(orientation);
}
void CadViewport::applyPartState(size_t index, const std::array<double, 3>& min,
                                 const std::array<double, 3>& max, const gp_Trsf& orientation) {
    if (!isPartActive(index)) return;
    auto& part = boxes_[index];
    if (part.geometryKind == CadPart::GeometryKind::GeneralBRep) {
        // The caller supplies the complete target instance transform.
        // Never derive it from the previous frame's bounds: doing so
        // compounds translation during grouped moves and undo/redo.
        part.orientation = orientation;
        part.min = min;
        part.max = max;
        if (!context_.IsNull()) {
            if (!batchingPartUpdates_) context_->ClearSelected(Standard_False);
            part.presentation->SetLocalTransformation(part.orientation);
            context_->Redisplay(part.presentation, Standard_False);
            if (!batchingPartUpdates_) {
                if (rotateMode_) updateRotationRings();
                if (directMoveMode_) updateMoveAxes();
                view_->Redraw();
            }
        }
        return;
    }
    part.orientation = orientation;
    applyBoxBounds(index, min, max);
}
void CadViewport::applyBoxBounds(size_t index, const std::array<double, 3>& min,
                                 const std::array<double, 3>& max) {
    if (index >= boxes_.size() || context_.IsNull()) return;
    auto& part = boxes_[index];
    // Box-only direct modeling must not overwrite an arbitrary imported B-Rep.
    if (part.geometryKind != CadPart::GeometryKind::Box) return;
    TopoDS_Shape shape = BRepPrimAPI_MakeBox(
        gp_Pnt(min[0], min[1], min[2]),
        max[0] - min[0], max[1] - min[1], max[2] - min[2]).Shape();
    if (!batchingPartUpdates_) context_->ClearSelected(Standard_False);
    part.shape = shape;
    part.presentation->SetShape(part.shape);
    part.presentation->SetLocalTransformation(part.orientation);
    context_->Redisplay(part.presentation, Standard_False);
    context_->Deactivate(part.presentation);
    context_->Activate(part.presentation,
        selectionMode_==SelectionMode::Part ? 0 : AIS_Shape::SelectionMode(TopAbs_FACE),
        Standard_False);
    part.min = min;
    part.max = max;
    if (!batchingPartUpdates_) {
        updateFaceArrow();
        if (rotateMode_) updateRotationRings();
        if (directMoveMode_) updateMoveAxes();
        view_->Redraw();
    }
}
void CadViewport::updateHistoryActions() {
    undoAction_->setEnabled(!undoStack_.empty());
    redoAction_->setEnabled(!redoStack_.empty());
}
void CadViewport::clearHistorySelection() {
    // Release subshape owners before rebuilding topology; resetModelingTools
    // must not retain old owners when restoring the default face picker.
    selectedPartIndices_.clear();
    clearSketchSelection();
    selectedConstructionIds_.clear();
    emit constructionSelectionChanged({});
    if(!context_.IsNull()) {
        context_->UnhilightSelected(Standard_False);
        context_->ClearSelected(Standard_False);
        context_->ClearDetected(Standard_False);
        context_->MainSelector()->ClearPicked();
    }
    resetModelingTools();
    primitivePullBefore_={};
    emit partSelectionChanged({}); // Clear browser-only/hidden selections too.
    if(!view_.IsNull()) {
        context_->UpdateCurrentViewer();view_->Invalidate();view_->Redraw();
    }
}
void CadViewport::undoPull() {
    cancelCommandSession();
    if (undoStack_.empty()) {
        emit statusMessage(tr("Nothing to undo."));
        return;
    }
    finishModelingInteraction();
    clearHistorySelection();
    const PullChange change = undoStack_.back();
    undoStack_.pop_back();
    if (change.constructionChange) {
        sketchEntities_=change.constructionChange->sketchesBefore;
        restoreConstructions(change.constructionChange->before);
    } else if(change.sketchChange) restoreSketchEntities(change.sketchChange->before);
    else if (change.booleanChange) restoreBooleanChange(*change.booleanChange,false);
    else if (!change.groupedChanges.empty()) {
        batchingPartUpdates_ = true;
        for (const auto& item : change.groupedChanges)
            restorePullChange(item, false);
        batchingPartUpdates_ = false;
        refreshPartSelection();
    } else restorePullChange(change, false);
    redoStack_.push_back(change);
    updateHistoryActions();
    clearHistorySelection();
    emit statusMessage(tr("Modeling operation undone."));
}
void CadViewport::redoPull() {
    cancelCommandSession();
    if (redoStack_.empty()) {
        emit statusMessage(tr("Nothing to redo."));
        return;
    }
    finishModelingInteraction();
    clearHistorySelection();
    const PullChange change = redoStack_.back();
    redoStack_.pop_back();
    if (change.constructionChange) {
        sketchEntities_=change.constructionChange->sketchesAfter;
        restoreConstructions(change.constructionChange->after);
    } else if(change.sketchChange) restoreSketchEntities(change.sketchChange->after);
    else if (change.booleanChange) restoreBooleanChange(*change.booleanChange,true);
    else if (!change.groupedChanges.empty()) {
        batchingPartUpdates_ = true;
        for (const auto& item : change.groupedChanges)
            restorePullChange(item, true);
        batchingPartUpdates_ = false;
        refreshPartSelection();
    } else restorePullChange(change, true);
    undoStack_.push_back(change);
    updateHistoryActions();
    clearHistorySelection();
    emit statusMessage(tr("Modeling operation redone."));
}
void CadViewport::fitAll() {
    const bool visiblePart=std::any_of(boxes_.begin(),boxes_.end(),[](const auto& part){return part.active && part.visible;});
    if(!visiblePart && !constructions_.empty()) {
        Bnd_Box bounds;
        for(const auto& object:constructions_) if(constructionShown(object)) for(const auto& point:constructionDisplayCorners(object)) {
            const double e=object.kind==Construction::Kind::Point?5.0:0;
            bounds.Add(point.Translated(gp_Vec(-e,-e,-e)));bounds.Add(point.Translated(gp_Vec(e,e,e)));
        }
        for(const auto& entity:sketchEntities_) if(entity.visible) {
            const auto* owner=constructionObject(entity.workplaneId);
            if(owner && constructionShown(*owner) && sketchPresentations_.contains(entity.id)) BRepBndLib::Add(sketchPresentations_.at(entity.id)->Shape(),bounds);
        }
        if(!bounds.IsVoid()) {view_->FitAll(bounds,0.05,Standard_False);stabilizeViewCamera();view_->ZFitAll();updateConstructionLabels();view_->Redraw();}
        return;
    }
    if (!view_.IsNull()) { view_->FitAll(0.01, Standard_False);stabilizeViewCamera();view_->ZFitAll();updateConstructionLabels();view_->Redraw(); }
}

bool CadViewport::exportDocumentSnapshot(NativeDocumentSnapshot& snapshot) const {
    try {
        snapshot = {};
        snapshot.activePartId = activePartId_;
        snapshot.schemaVersion = NativeDocumentStorage::CurrentSchemaVersion;
        snapshot.constructions = constructions_;
        snapshot.sketchEntities = sketchEntities_;
        snapshot.constructionNameCounters=constructionNameCounters_;
        for (size_t i = 0; i < boxes_.size(); ++i) {
            const CadPart* saved=&boxes_[i];
            if (commandSession_ && commandSession_->kind==CommandSession::Kind::Pull)
                for (const auto& change:commandSession_->changes)
                    if (change.partIndex==i && change.faceChange) saved=&change.faceChange->before;
            const auto& part=*saved;
            if (!part.active || part.shape.IsNull()) continue;
            NativePartState state;
            state.id = part.persistentId.isEmpty()
                ? QStringLiteral("slot-%1").arg(i) : part.persistentId;
            state.name = part.name;
            state.shape = part.shape;
            if(part.blends.certified.IsSame(part.shape)) state.edgeBlendRecipe = EdgeBlendFeatures::encode(part.blends);
            state.transform = part.orientation;
            state.color = part.color;
            state.visible = part.visible;
            state.geometryKind = static_cast<int>(part.geometryKind);
            state.cylinderPrimitive = part.cylinderPrimitive;
            state.spherePrimitive = part.spherePrimitive;
            state.conePrimitive = part.conePrimitive;
            state.sphereRadius = part.sphereRadius;
            state.cylinderRadius = part.cylinderRadius;
            state.cylinderHeight = part.cylinderHeight;
            state.cylinderBaseOffset = part.cylinderBaseOffset;
            state.coneBottomRadius = part.cone.bottomRadius;
            state.coneTopRadius = part.cone.topRadius;
            state.coneHeight = part.cone.height;
            state.coneBaseOffset = part.cone.baseOffset;
            snapshot.parts.push_back(std::move(state));
        }
        if (!view_.IsNull()) {
            // Capture a canonical copy as a boundary safeguard for OCCT fits
            // outside the normal UI paths. Save does not mutate the live view.
            const Handle(Graphic3d_Camera) camera=new Graphic3d_Camera(view_->Camera());
            stabilizeOrthographicCamera(camera);
            snapshot.camera.eye = camera->Eye();
            snapshot.camera.center = camera->Center();
            snapshot.camera.up = camera->Up();
            snapshot.camera.scale = camera->Scale();
            snapshot.camera.projectionType = static_cast<int>(camera->ProjectionType());
        }
        return true;
    } catch (const Standard_Failure&) {
        return false;
    }
}

void CadViewport::resetDocument() {
    cancelTargetPicking();
    setActivePart({});
    endSketchEditing();
    cancelCommandSession();
    finishModelingInteraction();
    if (!context_.IsNull()) {
        context_->ClearSelected(Standard_False);
        for (auto& part : boxes_)
            if (!part.presentation.IsNull()) context_->Remove(part.presentation, Standard_False);
    }
    for(const auto& [id,presentation]:constructionPresentations_)
        if(!context_.IsNull()) context_->Remove(presentation,Standard_False);
    for(const auto& [id,label]:constructionLabels_) if(!context_.IsNull()) context_->Remove(label,Standard_False);
    for(const auto& [id,presentation]:sketchPresentations_) if(!context_.IsNull()) context_->Remove(presentation,Standard_False);
    sketchEntities_.clear();sketchPresentations_.clear();workplaneDisplayRectangles_.clear();clearSketchSelection();emit sketchEntitiesChanged();
    constructionLabels_.clear();constructionNameCounters_={};
    constructions_.clear();constructionPresentations_.clear();selectedConstructionIds_.clear();
    emit constructionsChanged();emit constructionSelectionChanged({});
    boxes_.clear();
    selectedPartIndices_.clear();
    partCount_ = 0;
    undoStack_.clear();
    redoStack_.clear();
    clearHistorySelection();
    updateHistoryActions();
    if (!view_.IsNull()) {view_->FitAll(0.01,Standard_False);stabilizeViewCamera();view_->ZFitAll();view_->Redraw();}
    emit partSelectionChanged({});
    emit documentReplaced();
    emit partsChanged();
}

bool CadViewport::recognizePullPrimitive(CadPart& part) const {
    // Editing metadata is a derived cache, never evidence of geometric
    // capability. Certify a complete primitive against the current B-Rep.
    try {
        gp_Cone cone;
        gp_Cylinder cylinder;
        gp_Sphere sphere;
        int coneFaces=0, cylinderFaces=0, sphereFaces=0, capFaces=0;
        std::vector<gp_Pln> planes;
        double bottom=0.0, top=0.0;
        for (TopExp_Explorer f(part.shape,TopAbs_FACE);f.More();f.Next()) {
            const BRepAdaptor_Surface surface(TopoDS::Face(f.Current()));
            if (surface.GetType()==GeomAbs_Cone) { cone=surface.Cone(); ++coneFaces; }
            else if (surface.GetType()==GeomAbs_Cylinder) { cylinder=surface.Cylinder(); ++cylinderFaces; }
            else if (surface.GetType()==GeomAbs_Sphere) { sphere=surface.Sphere(); ++sphereFaces; }
            else if (surface.GetType()==GeomAbs_Plane) {
                const auto plane=surface.Plane();
                planes.push_back(plane);
                const double z=plane.Location().Z();
                if (capFaces==0) bottom=top=z;
                else { bottom=std::min(bottom,z); top=std::max(top,z); }
                ++capFaces;
            } else return false;
        }
        CadPart recognized=part;
        recognized.cylinderPrimitive=recognized.spherePrimitive=recognized.conePrimitive=false;
        recognized.geometryKind=CadPart::GeometryKind::GeneralBRep;
        TopoDS_Shape candidate;
        const bool horizontalCaps=std::all_of(planes.begin(),planes.end(),[](const auto& plane) {
            return plane.Axis().Direction().IsParallel(gp_Dir(0,0,1),1.e-8);
        });
        if (coneFaces==1 && cylinderFaces==0 && sphereFaces==0
            && (capFaces==1 || capFaces==2) && horizontalCaps
            && cone.Axis().Direction().IsEqual(gp_Dir(0,0,1),1.e-8)) {
            if (capFaces==1) { bottom=std::min(bottom,cone.Apex().Z()); top=std::max(top,cone.Apex().Z()); }
            const double slope=std::tan(cone.SemiAngle());
            const double bottomRadius=cone.RefRadius()+(bottom-cone.Location().Z())*slope;
            const double topRadius=std::max(0.0,cone.RefRadius()+(top-cone.Location().Z())*slope);
            if (bottomRadius<=0.01 || top-bottom<=0.01) return false;
            const gp_Pnt base(cone.Location().X(),cone.Location().Y(),bottom);
            candidate=BRepPrimAPI_MakeCone(gp_Ax2(base,gp_Dir(0,0,1)),bottomRadius,topRadius,top-bottom).Shape();
            recognized.conePrimitive=true;
            recognized.cone={bottomRadius,topRadius,top-bottom,0.0};
        } else if (cylinderFaces==1 && coneFaces==0 && sphereFaces==0
            && capFaces==2 && horizontalCaps && top-bottom>0.01
            && cylinder.Axis().Direction().IsEqual(gp_Dir(0,0,1),1.e-8)) {
            candidate=BRepPrimAPI_MakeCylinder(gp_Ax2(
                gp_Pnt(cylinder.Location().X(),cylinder.Location().Y(),bottom),gp_Dir(0,0,1)),
                cylinder.Radius(),top-bottom).Shape();
            recognized.cylinderPrimitive=true;
            recognized.cylinderRadius=cylinder.Radius();
            recognized.cylinderHeight=top-bottom; recognized.cylinderBaseOffset=0.0;
            // The equal-radius limit also supports both cone diameter handles.
            if (part.conePrimitive) {
                recognized.cylinderPrimitive=false; recognized.conePrimitive=true;
                recognized.cone={cylinder.Radius(),cylinder.Radius(),top-bottom,0.0};
            }
        } else if (sphereFaces==1 && coneFaces==0 && cylinderFaces==0 && capFaces==0) {
            candidate=BRepPrimAPI_MakeSphere(sphere.Location(),sphere.Radius()).Shape();
            recognized.spherePrimitive=true; recognized.sphereRadius=sphere.Radius();
        } else if (capFaces==6 && coneFaces==0 && cylinderFaces==0 && sphereFaces==0) {
            Bnd_Box bounds; BRepBndLib::AddOptimal(part.shape,bounds,false,false);
            double x0,y0,z0,x1,y1,z1; bounds.Get(x0,y0,z0,x1,y1,z1);
            candidate=BRepPrimAPI_MakeBox(gp_Pnt(x0,y0,z0),gp_Pnt(x1,y1,z1)).Shape();
            recognized.geometryKind=CadPart::GeometryKind::Box;
        } else return false;
        double volume=0.0;
        if (!BRepReconstruction::validSolid(part.shape,volume)
            || !BRepReconstruction::equivalent(part.shape,candidate)) return false;
        Bnd_Box bounds; BRepBndLib::AddOptimal(candidate,bounds,false,false);
        bounds.Get(recognized.min[0],recognized.min[1],recognized.min[2],
            recognized.max[0],recognized.max[1],recognized.max[2]);
        recognized.baseMin=recognized.min; recognized.baseMax=recognized.max;
        // Keep current editing anchors if already consistent with the shape.
        // This preserves the opposite cap during repeated height Pulls.
        if (recognized.conePrimitive && part.conePrimitive
            && std::abs(part.cone.bottomRadius-recognized.cone.bottomRadius)<1.e-7
            && std::abs(part.cone.topRadius-recognized.cone.topRadius)<1.e-7
            && std::abs(part.cone.height-recognized.cone.height)<1.e-7
            && std::abs(part.baseMin[2]+part.cone.baseOffset-bottom)<1.e-7) {
            recognized.cone=part.cone; recognized.baseMin=part.baseMin; recognized.baseMax=part.baseMax;
        } else if (recognized.cylinderPrimitive && part.cylinderPrimitive
            && std::abs(part.cylinderRadius-recognized.cylinderRadius)<1.e-7
            && std::abs(part.cylinderHeight-recognized.cylinderHeight)<1.e-7
            && std::abs(part.baseMin[2]+part.cylinderBaseOffset-bottom)<1.e-7) {
            recognized.cylinderBaseOffset=part.cylinderBaseOffset;
            recognized.baseMin=part.baseMin; recognized.baseMax=part.baseMax;
        }
        part=std::move(recognized);
        return true;
    } catch (const Standard_Failure&) {
        return false;
    }
}

bool CadViewport::importDocumentSnapshot(const NativeDocumentSnapshot& snapshot, QString& error) {
    if(!snapshot.activePartId.isEmpty() && std::none_of(snapshot.parts.begin(),snapshot.parts.end(),[&](const auto& p){return p.id==snapshot.activePartId;})) {error=tr("Active Part does not exist.");return false;}
    if ((snapshot.schemaVersion<1 || snapshot.schemaVersion>NativeDocumentStorage::CurrentSchemaVersion)) {
        error = tr("Unsupported OpenDesigner document schema.");
        return false;
    }
    if(snapshot.schemaVersion==1 && !snapshot.constructions.empty()) {error=tr("Schema v1 cannot contain constructions.");return false;}
    QStringList documentIds;
    for(const auto& object:snapshot.constructions) {
        if(!Construction::valid(object,error)) return false;
        if(documentIds.contains(object.id)) {error=tr("Duplicate document object ID.");return false;}
        documentIds.push_back(object.id);
    }
    for (const auto& state : snapshot.parts) {
        if(documentIds.contains(state.id)) {error=tr("Duplicate document object ID.");return false;}
        documentIds.push_back(state.id);
        if (state.shape.IsNull() || state.id.isEmpty()) {
            error = tr("The document contains an invalid Part.");
            return false;
        }
    }
    if(snapshot.schemaVersion<3 && !snapshot.sketchEntities.empty()) {error=tr("Schema v1/v2 cannot contain 2D geometry.");return false;}
    if(!snapshot.sketchEntities.empty() && snapshot.schemaVersion<NativeDocumentStorage::sketchSchemaVersion(snapshot.sketchEntities)) {
        error=tr("The requested schema cannot contain these 2D types (points require v4; ellipses require v5).");return false;
    }
    if(!Sketch::validate(snapshot.sketchEntities,snapshot.constructions,documentIds,error)) return false;
    if (!std::isfinite(snapshot.camera.scale) || snapshot.camera.scale <= 0.0) {
        error = tr("The document contains an invalid camera state.");
        return false;
    }
    const auto& cameraState=snapshot.camera;
    for (int axis=1;axis<=3;++axis) {
        if (!std::isfinite(cameraState.eye.Coord(axis)) || !std::isfinite(cameraState.center.Coord(axis))
            || !std::isfinite(cameraState.up.Coord(axis))) {
            error=tr("The document contains invalid camera coordinates."); return false;
        }
    }
    const gp_Vec cameraDirection(cameraState.eye,cameraState.center);
    if (cameraState.projectionType<0 || cameraState.projectionType>4
        || cameraDirection.SquareMagnitude()<1.e-16
        || cameraDirection.Crossed(gp_Vec(cameraState.up)).SquareMagnitude()<1.e-16) {
        error=tr("The document contains an invalid camera orientation."); return false;
    }
    std::vector<CadPart> imported;
    std::map<QString,Handle(AIS_Shape)> importedConstructions, importedSketches;
    std::map<QString,Handle(AIS_TextLabel)> importedLabels;
    for(int counter:snapshot.constructionNameCounters) if(counter<0) {error=tr("Invalid construction name counters.");return false;}
    try {
        imported.reserve(snapshot.parts.size());
        for (const auto& state : snapshot.parts) {
            CadPart part;
            part.persistentId = state.id;
            part.name = state.name;
            part.shape = state.shape;
            part.blends=EdgeBlendFeatures::decode(state.edgeBlendRecipe);
            if(!part.blends.sharp.IsNull()) {QString diagnostic;EdgeBlendFeatures::resolve(part.shape,part.blends,diagnostic);}
            part.orientation = state.transform;
            part.color = state.color;
            part.visible = state.visible;
            part.geometryKind = state.geometryKind == 0 ? CadPart::GeometryKind::Box : CadPart::GeometryKind::GeneralBRep;
            part.cylinderPrimitive = state.cylinderPrimitive;
            part.spherePrimitive = state.spherePrimitive;
            part.conePrimitive = state.conePrimitive;
            part.sphereRadius = state.sphereRadius;
            part.cylinderRadius = state.cylinderRadius;
            part.cylinderHeight = state.cylinderHeight;
            part.cylinderBaseOffset = state.cylinderBaseOffset;
            part.cone.bottomRadius = state.coneBottomRadius;
            part.cone.topRadius = state.coneTopRadius;
            part.cone.height = state.coneHeight;
            part.cone.baseOffset = state.coneBaseOffset;
            Bnd_Box bounds;
            BRepBndLib::Add(part.shape, bounds);
            if (bounds.IsVoid()) throw std::runtime_error("Part has no bounds.");
            double xmin, ymin, zmin, xmax, ymax, zmax;
            bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
            part.min = {xmin, ymin, zmin}; part.max = {xmax, ymax, zmax};
            part.baseMin = part.min; part.baseMax = part.max;
            part.presentation = new PartPresentation(part.shape);
            part.presentation->SetColor(Quantity_Color(part.color.redF(), part.color.greenF(),
                part.color.blueF(), Quantity_TOC_RGB));
            part.presentation->SetLocalTransformation(part.orientation);
            imported.push_back(std::move(part));
        }
        initializeViewer();
        const Handle(Graphic3d_Camera) camera=new Graphic3d_Camera(view_->Camera());
        camera->SetEyeAndCenter(snapshot.camera.eye,snapshot.camera.center);
        camera->SetUp(snapshot.camera.up);
        camera->SetScale(snapshot.camera.scale);
        camera->SetProjectionType(static_cast<Graphic3d_Camera::Projection>(snapshot.camera.projectionType));
        // Prepare every presentation before replacing the document. Kernel or
        // graphics failures still leave the old geometry and history intact.
        for (auto& loaded : imported) {
            context_->Display(loaded.presentation, Standard_False);
            context_->SetDisplayMode(loaded.presentation, partDisplayMode(), Standard_False);
            context_->Deactivate(loaded.presentation);
            if (!loaded.visible) context_->Erase(loaded.presentation,Standard_False);
            if (loaded.visible)
                context_->Activate(loaded.presentation,AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
        }
        for(const auto& object:snapshot.constructions) {
            auto presentation=constructionPresentation(object,false,&snapshot.sketchEntities);importedConstructions[object.id]=presentation;
            auto label=constructionLabel(object);importedLabels[object.id]=label;
            if(constructionShown(object)) { // view-level Hide Workplanes survives New/Open
                context_->Display(label,0,-1,Standard_False);context_->Deactivate(label);
                context_->Display(presentation,Standard_False);context_->Deactivate(presentation);
                if(object.selectable) context_->Activate(presentation,object.kind==Construction::Kind::Workplane?AIS_Shape::SelectionMode(TopAbs_EDGE):0,Standard_False);
            }
        }
        for(const auto& entity:snapshot.sketchEntities) {
            const auto owner=std::find_if(snapshot.constructions.begin(),snapshot.constructions.end(),[&](const auto& o){return o.id==entity.workplaneId;});
            auto presentation=sketchPresentation(entity,*owner,false,&snapshot.sketchEntities);importedSketches[entity.id]=presentation;
            displaySketchPresentation(entity,*owner,presentation);
        }
        resetDocument();
        sketchEntities_=snapshot.sketchEntities;sketchPresentations_=std::move(importedSketches);
        constructions_=snapshot.constructions;constructionPresentations_=std::move(importedConstructions);
        constructionLabels_=std::move(importedLabels);constructionNameCounters_=snapshot.constructionNameCounters;
        Construction::advanceNameCounters(constructions_,constructionNameCounters_);
        boxes_=std::move(imported);
        setActivePart(snapshot.activePartId);
        partCount_=static_cast<int>(boxes_.size());
        // Staged presentations were prepared while the previous document's
        // editing context was still active. Open restores ordinary 3D/2D picks.
        refreshSketchPicking(true);
        view_->SetCamera(camera);
        view_->ZFitAll();
        updateConstructionLabels();
        view_->Redraw();
        emit documentReplaced();
        emit sketchEntitiesChanged();
        emit constructionsChanged();
        emit partsChanged();
        error.clear();
        return true;
    } catch (const Standard_Failure& e) {
        error = QString::fromUtf8(e.GetMessageString());
    } catch (const std::exception& e) {
        error = QString::fromUtf8(e.what());
    }
    if (!context_.IsNull()) {
        for(const auto& [id,presentation]:importedSketches) context_->Remove(presentation,Standard_False);
        for(const auto& [id,label]:importedLabels) context_->Remove(label,Standard_False);
        for(const auto& [id,presentation]:importedConstructions) context_->Remove(presentation,Standard_False);
        for (const auto& part:imported)
            if (!part.presentation.IsNull()) context_->Remove(part.presentation,Standard_False);
        if (!view_.IsNull()) view_->Redraw();
    }
    return false;
}
bool CadViewport::beginFaceDrag(const QPoint& origin) {
    if (context_.IsNull()) return false;
    context_->MoveTo(origin.x(), origin.y(), view_, Standard_False);
    context_->SelectDetected(AIS_SelectionScheme_Replace);
    if (context_->NbSelected() != 1) return false;
    context_->InitSelected();
    if (!context_->MoreSelected()) return false;
    Handle(StdSelect_BRepOwner) owner =
        Handle(StdSelect_BRepOwner)::DownCast(context_->SelectedOwner());
    if (owner.IsNull() || owner->Shape().IsNull()
        || owner->Shape().ShapeType() != TopAbs_FACE) return false;
    Handle(AIS_InteractiveObject) selected = context_->SelectedInteractive();
    size_t index = 0;
    while (index < boxes_.size() && boxes_[index].presentation != selected) ++index;
    if (index == boxes_.size()) return false;
    const BRepAdaptor_Surface surface(TopoDS::Face(owner->Shape()));
    if (surface.GetType() != GeomAbs_Plane) return false;
    const gp_Dir normal = surface.Plane().Axis().Direction();
    const double n[3] = {normal.X(), normal.Y(), normal.Z()};
    int axis = 0;
    for (int i = 1; i < 3; ++i)
        if (std::abs(n[i]) > std::abs(n[axis])) axis = i;
    if (std::abs(n[axis]) < 0.999) return false;
    const double position = surface.Plane().Location().Coord(axis + 1);
    const bool maxSide = std::abs(position - boxes_[index].max[axis]) < 0.001;
    if (!maxSide && std::abs(position - boxes_[index].min[axis]) >= 0.001) return false;
    dragIndex_ = index;
    dragAxis_ = axis;
    dragMax_ = maxSide;
    dragMin_ = boxes_[index].min;
    dragMaxBounds_ = boxes_[index].max;
    dragStart_ = origin;
    dragDistance_ = 0.0;
    // Project a world-space outward axis onto screen pixels. Qt Y points down.
    const double outward = maxSide ? 1.0 : -1.0;
    gp_Pnt a(0.0, 0.0, 0.0);
    gp_Pnt b(0.0, 0.0, 0.0);
    b.SetCoord(axis + 1, outward * 100.0);
    a.Transform(boxes_[index].orientation);
    b.Transform(boxes_[index].orientation);
    Standard_Integer ax, ay, bx, by;
    view_->Convert(a.X(), a.Y(), a.Z(), ax, ay);
    view_->Convert(b.X(), b.Y(), b.Z(), bx, by);
    const double dx = static_cast<double>(bx - ax);
    const double dy = static_cast<double>(by - ay);
    const double pixels = std::hypot(dx, dy);
    if (pixels < 2.0) {
        emit statusMessage(tr("Face normal is nearly aligned with camera; rotate view first."));
        return false;
    }
    dragScreenX_ = dx / pixels;
    dragScreenY_ = dy / pixels;
    dragMmPerPixel_ = 100.0 / pixels;
    dragMovesPart_ = movePartMode_;
    multiFaceDragging_ = !dragMovesPart_ && prepareMultiFaceDrag();
    if(!dragMovesPart_) beginPullSession();
    if (multiFaceDragging_) updateMultiFaceHighlight();
    draggingFace_ = true;
    if (dragMovesPart_)
        beginCommandSession(CommandSession::Kind::Move,{dragIndex_},
            gp_Dir(dragAxis_==0?1:0,dragAxis_==1?1:0,dragAxis_==2?1:0)
                .Transformed(boxes_[dragIndex_].orientation));
    if (commandSession_ && !dragMax_) commandSession_->direction.Reverse();
    emit statusMessage(dragMovesPart_
        ? tr("Drag to move the entire part along the face normal.")
        : tr("Drag along the face normal; release to commit."));
    return true;
}
bool CadViewport::beginArrowDrag(const QPoint& origin) {
    if (!arrowVisible_ || view_.IsNull() || arrowPartIndex_ >= boxes_.size()) return false;
    const auto& part = boxes_[arrowPartIndex_];
    if (part.conePrimitive && !movePartMode_) {
        if (!pickConeArrow(origin)) return false;
    } else if (faceArrow_.IsNull()) return false;
    gp_Pnt center = (part.cylinderPrimitive || part.spherePrimitive || part.conePrimitive || cylindricalFacePull_) && !movePartMode_ ? primitiveArrowOrigin_ :
        gp_Pnt((part.min[0]+part.max[0])*0.5,(part.min[1]+part.max[1])*0.5,
               (part.min[2]+part.max[2])*0.5);
    gp_Vec outward = (part.cylinderPrimitive || part.spherePrimitive || part.conePrimitive || cylindricalFacePull_) && !movePartMode_ ?
        gp_Vec(primitiveArrowDirection_) : gp_Vec(0,0,0);
    if ((!part.cylinderPrimitive && !part.spherePrimitive && !part.conePrimitive && !cylindricalFacePull_) || movePartMode_) {
        center.SetCoord(arrowAxis_+1,arrowMax_?part.max[arrowAxis_]:part.min[arrowAxis_]);
        outward.SetCoord(arrowAxis_+1,arrowMax_?1.0:-1.0);
    }
    gp_Pnt tip = center.Translated(outward.Multiplied(31.0));
    center.Transform(part.orientation);
    tip.Transform(part.orientation);
    Standard_Integer x0, y0, x1, y1;
    view_->Convert(center.X(), center.Y(), center.Z(), x0, y0);
    view_->Convert(tip.X(), tip.Y(), tip.Z(), x1, y1);
    const double dx = static_cast<double>(x1 - x0);
    const double dy = static_cast<double>(y1 - y0);
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared < 4.0) return false;
    const double t = std::clamp(
        ((origin.x() - x0) * dx + (origin.y() - y0) * dy) / lengthSquared,
        0.0, 1.0);
    const double distance = std::hypot(origin.x() - (x0 + t * dx),
                                       origin.y() - (y0 + t * dy));
    if (distance > 12.0) return false;
    if ((part.cylinderPrimitive || part.spherePrimitive || part.conePrimitive || cylindricalFacePull_) && !movePartMode_) {
        primitivePullDragging_ = true;
        primitivePullBefore_ = {arrowPartIndex_,part.min,part.max,part.min,part.max,
            part.orientation,part.orientation};
        primitivePullBefore_.beforeShape = part.shape;
        primitivePullBefore_.beforeCone = part.cone;
        primitivePullBefore_.beforeRadius = cylindricalFacePull_ ? cylindricalPullRadius_
            : part.spherePrimitive ? part.sphereRadius : part.cylinderRadius;
        primitivePullBefore_.beforeHeight = bossHeightPull_ ? bossPullHeight_ : part.cylinderHeight;
        primitivePullBefore_.beforeBaseOffset = part.cylinderBaseOffset;
        dragIndex_ = arrowPartIndex_;
        dragStart_ = origin;
        dragDistance_ = 0.0;
        const double pixels = std::sqrt(lengthSquared);
        dragScreenX_ = dx/pixels;
        dragScreenY_ = dy/pixels;
        dragMmPerPixel_ = 31.0/pixels;
        draggingFace_ = true;
        beginPullSession();
        context_->ClearSelected(Standard_False);
        context_->MoveTo(-1,-1,view_,Standard_False);
        dragDimension_->setToolTip(primitivePullIsDiameter()
            ? tr("Final diameter in mm") : tr("Final height in mm"));
        dragDimension_->setText(primitivePullDimensionText());
        // The dimension editor is reparented into the floating command panel
        // by beginCommandSession().  Positioning it with viewport coordinates
        // would move it outside that parent and leave only a blank panel
        // visible.  Let the panel layout place the editor instead.
        showCommandControls(origin);
        return true;
    }
    dragIndex_ = arrowPartIndex_;
    dragAxis_ = arrowAxis_;
    dragMax_ = arrowMax_;
    dragMin_ = part.min;
    dragMaxBounds_ = part.max;
    dragStart_ = origin;
    dragDistance_ = 0.0;
    const double pixels = std::sqrt(lengthSquared);
    dragScreenX_ = dx / pixels;
    dragScreenY_ = dy / pixels;
    dragMmPerPixel_ = 31.0 / pixels;
    dragMovesPart_ = movePartMode_;
    multiFaceDragging_ = !dragMovesPart_ && prepareMultiFaceDrag();
    if(!dragMovesPart_) beginPullSession();
    if (multiFaceDragging_) {
        // Preserve face identities in multiFaceParts_ before clearing OCCT owners.
        // The old selection highlight otherwise remains at its original position.
        context_->ClearSelected(Standard_False);
        updateMultiFaceHighlight();
        view_->Redraw();
    }
    draggingFace_ = true;
    if (dragMovesPart_)
        beginCommandSession(CommandSession::Kind::Move,{dragIndex_},
            gp_Dir(dragAxis_==0?1:0,dragAxis_==1?1:0,dragAxis_==2?1:0)
                .Transformed(boxes_[dragIndex_].orientation));
    if (commandSession_ && !dragMax_) commandSession_->direction.Reverse();
    emit statusMessage(dragMovesPart_
        ? tr("Drag arrow to move the entire part.")
        : multiFaceDragging_
            ? tr("Drag arrow to pull all selected faces together.")
            : tr("Drag arrow along face normal; release to commit."));
    return true;
}
bool CadViewport::prepareMultiFaceDrag() {
    multiFaceParts_.clear();
    if (context_.IsNull() || context_->NbSelected() < 2) return false;
    for (context_->InitSelected(); context_->MoreSelected(); context_->NextSelected()) {
        const auto owner = Handle(StdSelect_BRepOwner)::DownCast(context_->SelectedOwner());
        if (owner.IsNull() || owner->Shape().IsNull() || owner->Shape().ShapeType() != TopAbs_FACE) {
            multiFaceParts_.clear();
            return false;
        }
        const auto object = context_->SelectedInteractive();
        size_t index = 0;
        while (index < boxes_.size() && boxes_[index].presentation != object) ++index;
        if (index == boxes_.size() || boxes_[index].geometryKind != CadPart::GeometryKind::Box) {
            multiFaceParts_.clear(); return false;
        }
        const auto surface = BRepAdaptor_Surface(TopoDS::Face(owner->Shape()));
        if (surface.GetType() != GeomAbs_Plane) { multiFaceParts_.clear(); return false; }
        const gp_Dir normal = surface.Plane().Axis().Direction();
        const double n[3] = {normal.X(), normal.Y(), normal.Z()};
        int axis = 0;
        for (int k = 1; k < 3; ++k) if (std::abs(n[k]) > std::abs(n[axis])) axis = k;
        if (std::abs(n[axis]) < 0.999) { multiFaceParts_.clear(); return false; }
        const auto& part = boxes_[index];
        const double position = surface.Plane().Location().Coord(axis + 1);
        const bool onMax = std::abs(position - part.max[axis]) < 0.001;
        const bool onMin = std::abs(position - part.min[axis]) < 0.001;
        if (!onMax && !onMin) { multiFaceParts_.clear(); return false; }
        auto it = std::find_if(multiFaceParts_.begin(), multiFaceParts_.end(),
            [index](const FaceDragPart& item) { return item.before.partIndex == index; });
        if (it == multiFaceParts_.end()) {
            PullChange before{index, part.min, part.max, part.min, part.max,
                              part.orientation, part.orientation};
            multiFaceParts_.push_back({before, {}});
            it = std::prev(multiFaceParts_.end());
        }
        it->sides[axis * 2 + (onMax ? 1 : 0)] = true;
    }
    return !multiFaceParts_.empty();
}

void CadViewport::clearMultiFaceHighlight() {
    if (!multiFaceHighlight_.IsNull() && !context_.IsNull()) {
        context_->Remove(multiFaceHighlight_, Standard_False);
        multiFaceHighlight_.Nullify();
    }
}

void CadViewport::updateMultiFaceHighlight() {
    if (context_.IsNull()) return;
    BRep_Builder builder;
    TopoDS_Compound edges;
    builder.MakeCompound(edges);
    int count = 0;
    for (const auto& item : multiFaceParts_) {
        if (item.before.partIndex >= boxes_.size()) continue;
        const auto& part = boxes_[item.before.partIndex];
        for (TopExp_Explorer faces(part.presentation->Shape(), TopAbs_FACE); faces.More(); faces.Next()) {
            const TopoDS_Face face = TopoDS::Face(faces.Current());
            const BRepAdaptor_Surface surface(face);
            if (surface.GetType() != GeomAbs_Plane) continue;
            const gp_Dir normal = surface.Plane().Axis().Direction();
            const double n[3] = {normal.X(), normal.Y(), normal.Z()};
            int axis = 0;
            for (int k = 1; k < 3; ++k)
                if (std::abs(n[k]) > std::abs(n[axis])) axis = k;
            if (std::abs(n[axis]) < 0.999) continue;
            const double pos = surface.Plane().Location().Coord(axis + 1);
            const bool onMax = std::abs(pos - part.max[axis]) < 0.001;
            const bool onMin = std::abs(pos - part.min[axis]) < 0.001;
            if (!((onMax && item.sides[axis * 2 + 1]) ||
                  (onMin && item.sides[axis * 2]))) continue;
            for (TopExp_Explorer edge(face, TopAbs_EDGE); edge.More(); edge.Next()) {
                builder.Add(edges, BRepBuilderAPI_Transform(
                    edge.Current(), part.orientation, true).Shape());
                ++count;
            }
        }
    }
    if (count == 0) { clearMultiFaceHighlight(); return; }
    if (multiFaceHighlight_.IsNull()) {
        multiFaceHighlight_ = new AIS_Shape(edges);
        multiFaceHighlight_->SetColor(Quantity_Color(135./255,206./255,250./255,Quantity_TOC_sRGB));
        multiFaceHighlight_->SetWidth(2.*devicePixelRatioF());
        context_->Display(multiFaceHighlight_, Standard_False);
        context_->SetDisplayMode(multiFaceHighlight_, AIS_WireFrame, Standard_False);
        context_->Deactivate(multiFaceHighlight_);
    } else {
        multiFaceHighlight_->SetShape(edges);
        context_->Redisplay(multiFaceHighlight_, Standard_False);
        context_->Deactivate(multiFaceHighlight_);
    }
    context_->SetZLayer(multiFaceHighlight_, Graphic3d_ZLayerId_Top);
}

bool CadViewport::applyMultiFaceDrag(double distance) {
    // Validate the complete group before applying any geometry.
    std::vector<PullChange> changes;
    for (const auto& item : multiFaceParts_) {
        auto change = item.before;
        for (int axis = 0; axis < 3; ++axis) {
            if (item.sides[axis * 2]) change.afterMin[axis] -= distance;
            if (item.sides[axis * 2 + 1]) change.afterMax[axis] += distance;
            if (change.afterMax[axis] - change.afterMin[axis] <= 0.001) return false;
        }
        changes.push_back(change);
    }
    batchingPartUpdates_ = true;
    for (const auto& change : changes)
        applyPartState(change.partIndex, change.afterMin, change.afterMax, change.afterOrientation);
    batchingPartUpdates_ = false;
    // Rebuilt B-Reps invalidate OCCT's old selected face owners. Show a
    // temporary cyan edge overlay reconstructed from the current geometry.
    updateMultiFaceHighlight();
    updateFaceArrow();
    view_->Redraw();
    return true;
}

gp_Pnt CadViewport::coneArrowOrigin(const CadPart& part, ConePullTarget target) const {
    gp_Pnt origin((part.baseMin[0]+part.baseMax[0])*0.5,
        (part.baseMin[1]+part.baseMax[1])*0.5,part.baseMin[2]+part.cone.baseOffset);
    if (target == ConePullTarget::TopDiameter || target == ConePullTarget::TopHeight)
        origin.SetZ(origin.Z()+part.cone.height);
    if (target == ConePullTarget::BottomDiameter || target == ConePullTarget::TopDiameter)
        origin.Translate(gp_Vec(coneRadialDirection_)*(target == ConePullTarget::BottomDiameter
            ? part.cone.bottomRadius : part.cone.topRadius));
    return origin;
}
gp_Dir CadViewport::coneArrowDirection(ConePullTarget target) const {
    if (target == ConePullTarget::BottomDiameter || target == ConePullTarget::TopDiameter)
        return coneRadialDirection_;
    return gp_Dir(0,0,target == ConePullTarget::TopHeight ? 1 : -1);
}
bool CadViewport::pickConeArrow(const QPoint& point) {
    const auto& part = boxes_[arrowPartIndex_];
    double closest = 12.0;
    bool found = false;
    for (size_t i = 0; i < coneArrows_.size(); ++i) {
        if (coneArrows_[i].IsNull()) continue;
        const auto target = static_cast<ConePullTarget>(i);
        const gp_Pnt local = coneArrowOrigin(part,target);
        const gp_Pnt a = local.Transformed(part.orientation);
        const gp_Pnt b = local.Translated(gp_Vec(coneArrowDirection(target))*31.0)
            .Transformed(part.orientation);
        Standard_Integer ax,ay,bx,by;
        view_->Convert(a.X(),a.Y(),a.Z(),ax,ay);
        view_->Convert(b.X(),b.Y(),b.Z(),bx,by);
        const double dx = bx-ax, dy = by-ay;
        const double lengthSquared = dx*dx+dy*dy;
        if (lengthSquared < 4.0) continue;
        const double t = std::clamp(((point.x()-ax)*dx+(point.y()-ay)*dy)/lengthSquared,0.0,1.0);
        const double distance = std::hypot(point.x()-ax-t*dx,point.y()-ay-t*dy);
        if (distance < closest) {
            closest = distance;
            conePullTarget_ = target;
            found = true;
        }
    }
    if (found) {
        primitiveArrowOrigin_ = coneArrowOrigin(part,conePullTarget_);
        primitiveArrowDirection_ = coneArrowDirection(conePullTarget_);
    }
    return found;
}
double CadViewport::primitivePullStartDimension() const {
    if (boxes_[dragIndex_].conePrimitive) {
        const auto& cone = primitivePullBefore_.beforeCone;
        if (conePullTarget_ == ConePullTarget::BottomDiameter) return cone.bottomRadius*2.0;
        if (conePullTarget_ == ConePullTarget::TopDiameter) return cone.topRadius*2.0;
        return cone.height;
    }
    return primitivePullIsDiameter() ? primitivePullBefore_.beforeRadius*2.0
                                     : primitivePullBefore_.beforeHeight;
}
bool CadViewport::primitivePullIsDiameter() const {
    if (cylindricalFacePull_) return !bossHeightPull_;
    if (boxes_[dragIndex_].conePrimitive)
        return conePullTarget_ == ConePullTarget::BottomDiameter
            || conePullTarget_ == ConePullTarget::TopDiameter;
    return boxes_[dragIndex_].spherePrimitive || cylinderRadialArrow_;
}

QString CadViewport::primitivePullDimensionText() const {
    const auto& part = boxes_[dragIndex_];
    const bool diameter = primitivePullIsDiameter();
    const double dimension = cylindricalFacePull_ ? (bossHeightPull_ ? bossPullHeight_ : 2.0*cylindricalPullRadius_)
        : part.conePrimitive
        ? (diameter ? 2.0*(conePullTarget_ == ConePullTarget::BottomDiameter
            ? part.cone.bottomRadius : part.cone.topRadius) : part.cone.height)
        : (diameter ? 2.0*(part.spherePrimitive ? part.sphereRadius : part.cylinderRadius)
                    : part.cylinderHeight);
    const QString number = QLocale().toString(dimension,'f',2);
    return diameter ? QStringLiteral("Ø %1 mm").arg(number)
                    : QStringLiteral("%1 mm").arg(number);
}

void CadViewport::hideMoveAxes() {
    if (context_.IsNull()) return;
    for (auto& axis : moveAxes_) {
        if (!axis.IsNull()) { context_->Remove(axis, Standard_False); axis.Nullify(); }
    }
}
gp_Pnt CadViewport::selectionCenter() const {
    return rotationCenter(selectedPartIndices_);
}
gp_Pnt CadViewport::rotationCenter(const std::vector<size_t>& indices) const {
    double x = 0, y = 0, z = 0;
    size_t count = 0;
    for (const size_t index : indices) {
        if (!isPartActive(index)) continue;
        const auto& part = boxes_[index];
        const gp_Pnt p = partWorldRotationCenter(part, part.orientation);
        x += p.X(); y += p.Y(); z += p.Z(); ++count;
    }
    return count ? gp_Pnt(x/count, y/count, z/count) : gp_Pnt(0, 0, 0);
}
void CadViewport::setPartVisible(size_t index, bool visible) {
    const bool projecting=commandSession_ && commandSession_->projection;
    cancelCommandSession();
    if (!isPartActive(index)) return;
    auto& part = boxes_[index];
    if (!part.active || part.visible == visible) return;
    part.visible = visible;
    if (!context_.IsNull()) {
        if (visible) {
            context_->Display(part.presentation, Standard_False);
            context_->Deactivate(part.presentation);
            context_->Activate(part.presentation,
                selectionMode_==SelectionMode::Part ? 0 : AIS_Shape::SelectionMode(TopAbs_FACE),
                Standard_False);
        } else {
            context_->Erase(part.presentation, Standard_False);
        }
    }
    std::vector<size_t> remaining;
    for (const auto selected : selectedPartIndices_)
        if (isPartVisible(selected))
            remaining.push_back(selected);
    if(projecting) refreshSketchPicking();else selectParts(remaining);
    emit partsChanged();
}
bool CadViewport::isPartVisible(size_t index) const {
    return isPartActive(index) && boxes_[index].visible;
}
void CadViewport::fitSelection(const std::vector<size_t>& indices) {
    if (view_.IsNull()) return;
    Bnd_Box bounds;
    try {
        for (const auto index : indices) {
            if (!isPartVisible(index)) continue;
            const auto& part = boxes_[index];
            // Topological locations cannot represent scale on all supported OCCT
            // versions. Apply the placement to geometry when scale requires it.
            BRepBndLib::Add(BRepBuilderAPI_Transform(part.presentation->Shape(),part.orientation).Shape(), bounds);
        }
    } catch(const Standard_Failure&) {
        emit statusMessage(tr("Open CASCADE could not compute the selected Part bounds."));return;
    }
    if (bounds.IsVoid()) return;
    view_->FitAll(bounds, 0.1, Standard_False);
    stabilizeViewCamera();
    view_->ZFitAll();
    updateConstructionLabels();
    view_->Redraw();
}
void CadViewport::selectParts(const std::vector<size_t>& indices) {
    if(commandSession_ && commandSession_->sweep) {chooseExtrusionTarget(indices);return;}
    if(commandSession_ && commandSession_->projection) {
        emit statusMessage(tr("Project 3D Geometry requires vertices, edges or faces picked in the viewport, not whole Parts."));return;
    }
    endSketchEditing();
    if(commandSession_ && commandSession_->kind==CommandSession::Kind::Construction) {
        emit statusMessage(tr("Pick the required subshape in the viewport or choose a construction reference."));return;
    }
    clearSketchSelection();
    selectedConstructionIds_.clear();emit constructionSelectionChanged({});
    if (commandSession_ && commandSession_->kind==CommandSession::Kind::Boolean) { selectBooleanParts(indices,false); return; }
    cancelCommandSession();
    selectedPartIndices_.clear();
    for (const size_t index : indices) {
        if (isPartVisible(index) &&
            std::find(selectedPartIndices_.begin(), selectedPartIndices_.end(), index)
                == selectedPartIndices_.end())
            selectedPartIndices_.push_back(index);
    }
    hideFaceArrow();
    hideMoveAxes();
    hideRotationRings();
    refreshPartSelection();
    emit partSelectionChanged(selectedPartIndices_);
}
void CadViewport::refreshPartSelection() {
    if (context_.IsNull()) return;
    context_->ClearSelected(Standard_False);
    for (const size_t index : selectedPartIndices_) {
        if (index < boxes_.size())
            context_->AddOrRemoveSelected(boxes_[index].presentation, Standard_False);
    }
    if (!selectedPartIndices_.empty()) {
        moveIndex_ = selectedPartIndices_.front();
        rotationIndex_ = moveIndex_;
        if (directMoveMode_) updateMoveAxes();
        if (rotateMode_) updateRotationRings();
    }
    view_->Redraw();
}
bool CadViewport::applyPrimitivePull(double distance) {
    if (dragIndex_ >= boxes_.size() || context_.IsNull()) return false;
    if (cylindricalFacePull_) return applyCylindricalFacePull(distance);
    auto& part = boxes_[dragIndex_];
    if (!std::isfinite(distance)) return false;
    if (part.conePrimitive) {
        auto cone = primitivePullBefore_.beforeCone;
        if (conePullTarget_ == ConePullTarget::BottomDiameter) cone.bottomRadius += distance;
        else if (conePullTarget_ == ConePullTarget::TopDiameter) cone.topRadius += distance;
        else {
            cone.height += distance;
            if (conePullTarget_ == ConePullTarget::BottomHeight) cone.baseOffset -= distance;
        }
        if (cone.bottomRadius <= 0.01 || cone.topRadius < 0.0 || cone.height <= 0.01) {
            emit statusMessage(tr("Cone: bottom diameter must exceed 0.02 mm, top diameter "
                "must be nonnegative and height must exceed 0.01 mm."));
            return false;
        }
        const gp_Pnt base((part.baseMin[0]+part.baseMax[0])*0.5,
            (part.baseMin[1]+part.baseMax[1])*0.5,part.baseMin[2]+cone.baseOffset);
        // Equal diameters are a valid limiting case; OCCT's cone builder
        // requires unequal radii, so build a cylinder while keeping cone parameters.
        if (std::abs(cone.bottomRadius-cone.topRadius) < 1.0e-7)
            part.shape = BRepPrimAPI_MakeCylinder(gp_Ax2(base,gp_Dir(0,0,1)),
                cone.bottomRadius,cone.height).Shape();
        else
            part.shape = BRepPrimAPI_MakeCone(gp_Ax2(base,gp_Dir(0,0,1)),
                cone.bottomRadius,cone.topRadius,cone.height).Shape();
        const auto& before = primitivePullBefore_.beforeCone;
        const double radiusChange = std::max(cone.bottomRadius,cone.topRadius)
            - std::max(before.bottomRadius,before.topRadius);
        part.min = primitivePullBefore_.beforeMin;
        part.max = primitivePullBefore_.beforeMax;
        for (int axis = 0; axis < 2; ++axis) {
            part.min[axis] -= radiusChange;
            part.max[axis] += radiusChange;
        }
        if (conePullTarget_ == ConePullTarget::BottomHeight) part.min[2] -= distance;
        else if (conePullTarget_ == ConePullTarget::TopHeight) part.max[2] += distance;
        part.cone = cone;
        primitiveArrowOrigin_ = coneArrowOrigin(part,conePullTarget_);
    } else if (part.spherePrimitive) {
        const double radius = primitivePullBefore_.beforeRadius + distance;
        if (radius <= 0.01) {
            emit statusMessage(tr("Sphere radius must be greater than 0.01 mm."));
            return false;
        }
        // Rebuild in local coordinates. The instance transform and center stay fixed,
        // including on moved, rotated and duplicated spheres.
        const gp_Pnt center = partLocalCenter(part);
        part.shape = BRepPrimAPI_MakeSphere(center, radius).Shape();
        part.sphereRadius = radius;
        for (int axis = 0; axis < 3; ++axis) {
            part.min[axis] = primitivePullBefore_.beforeMin[axis] - distance;
            part.max[axis] = primitivePullBefore_.beforeMax[axis] + distance;
        }
        primitiveArrowOrigin_ = center.Translated(gp_Vec(primitiveArrowDirection_)*radius);
    } else {
        if (!part.cylinderPrimitive) return false;
        const double radius = primitivePullBefore_.beforeRadius +
            (cylinderRadialArrow_ ? distance : 0.0);
        const double height = primitivePullBefore_.beforeHeight +
            (cylinderRadialArrow_ ? 0.0 : distance);
        if (radius <= 0.01 || height <= 0.01) return false;
        // A negative-Z cap pull moves the bottom down; the top stays fixed.
        const bool bottomCap = !cylinderRadialArrow_ && primitiveArrowDirection_.Z() < 0;
        const double baseOffset = primitivePullBefore_.beforeBaseOffset -
            (bottomCap ? distance : 0.0);
        const gp_Pnt base((part.baseMin[0]+part.baseMax[0])*0.5,
            (part.baseMin[1]+part.baseMax[1])*0.5,part.baseMin[2]+baseOffset);
        const TopoDS_Shape shape =
            BRepPrimAPI_MakeCylinder(gp_Ax2(base,gp_Dir(0,0,1)),radius,height).Shape();
        part.shape = shape;
        part.cylinderRadius = radius;
        part.cylinderHeight = height;
        part.cylinderBaseOffset = baseOffset;
        part.min = primitivePullBefore_.beforeMin;
        part.max = primitivePullBefore_.beforeMax;
        if (cylinderRadialArrow_) {
            const double change = radius-primitivePullBefore_.beforeRadius;
            part.min[0] -= change;
            part.min[1] -= change;
            part.max[0] += change;
            part.max[1] += change;
        } else if (bottomCap) {
            part.min[2] -= distance;
        } else {
            part.max[2] += distance;
        }
        if (cylinderRadialArrow_) {
            primitiveArrowOrigin_ = gp_Pnt(
                base.X()+primitiveArrowDirection_.X()*radius,
                base.Y()+primitiveArrowDirection_.Y()*radius,
                base.Z()+height*0.5);
        } else {
            primitiveArrowOrigin_.SetZ(primitiveArrowDirection_.Z()>0 ?
                base.Z()+height : base.Z());
        }
    }
    context_->ClearSelected(Standard_False);
    part.presentation->SetShape(part.shape);
    part.presentation->SetLocalTransformation(part.orientation);
    context_->Redisplay(part.presentation,Standard_False);
    context_->Deactivate(part.presentation);
    context_->Activate(part.presentation,AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
    updateFaceArrow();
    view_->Redraw();
    return true;
}
bool CadViewport::applyCylindricalFacePull(double distance) {
    if (dragIndex_ >= boxes_.size() || cylindricalPullFace_.IsNull()
        || primitivePullBefore_.beforeShape.IsNull() || !std::isfinite(distance)) return false;
    const double dimension = (bossHeightPull_?primitivePullBefore_.beforeHeight:primitivePullBefore_.beforeRadius) + distance;
    if (dimension <= 0.01) {
        if (commandSession_) commandSession_->geometryError = (bossHeightPull_?tr("Boss height must be greater than 0.01 mm."):tr("Cylinder radius must be greater than 0.01 mm."));
        return false;
    }
    try {
        // Reconstruct a current cylindrical wall or boss cap from the immutable
        // pre-drag B-Rep; neither route falls back to global offset.
        const auto resized=bossHeightPull_
            ? CylindricalMoveGeometry::resizeBossHeight(primitivePullBefore_.beforeShape,cylindricalPullFace_,dimension)
            : CylindricalMoveGeometry::resizeRadius(primitivePullBefore_.beforeShape,cylindricalPullFace_,dimension);
        if(!resized) {
            if(commandSession_) commandSession_->geometryError=QString::fromStdString(resized.message);
            return false;
        }
        const auto candidate=resized.shape;
        auto& part=boxes_[dragIndex_];
        part.shape=candidate;
        part.geometryKind=CadPart::GeometryKind::GeneralBRep;
        part.cylinderPrimitive=part.spherePrimitive=part.conePrimitive=false;
        Bnd_Box bounds;
        BRepBndLib::AddOptimal(part.shape,bounds,false,false);
        bounds.Get(part.min[0],part.min[1],part.min[2],part.max[0],part.max[1],part.max[2]);
        part.baseMin=part.min;part.baseMax=part.max;
        primitiveArrowOrigin_.Translate(gp_Vec(primitiveArrowDirection_)*(dimension-(bossHeightPull_?bossPullHeight_:cylindricalPullRadius_)));
        if(bossHeightPull_) bossPullHeight_=dimension;else cylindricalPullRadius_=dimension;
        context_->ClearSelected(Standard_False);
        part.presentation->SetShape(part.shape);
        part.presentation->SetLocalTransformation(part.orientation);
        context_->Redisplay(part.presentation,Standard_False);
        context_->Deactivate(part.presentation);
        context_->Activate(part.presentation,AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
        updateFaceArrow();
        view_->Redraw();
        return true;
    } catch (const Standard_Failure&) {
        if (commandSession_) commandSession_->geometryError = tr("Open CASCADE could not safely resize the cylindrical face.");
        return false;
    }
}
void CadViewport::restorePullChange(const PullChange& change, bool after) {
    if (change.faceChange) {
        applyFaceState(change.partIndex,after?change.faceChange->after:change.faceChange->before);
        if(!batchingPartUpdates_) { selectParts({change.partIndex}); emit partsChanged(); }
        return;
    }
    applyPartState(change.partIndex,after?change.afterMin:change.beforeMin,
        after?change.afterMax:change.beforeMax,
        after?change.afterOrientation:change.beforeOrientation);
    if (change.beforeShape.IsNull() || change.partIndex>=boxes_.size()) return;
    auto& part=boxes_[change.partIndex];
    part.shape=after?change.afterShape:change.beforeShape;
    if (part.conePrimitive) part.cone=after?change.afterCone:change.beforeCone;
    if (part.spherePrimitive)
        part.sphereRadius=after?change.afterRadius:change.beforeRadius;
    else
        part.cylinderRadius=after?change.afterRadius:change.beforeRadius;
    part.cylinderHeight=after?change.afterHeight:change.beforeHeight;
    part.cylinderBaseOffset=after?change.afterBaseOffset:change.beforeBaseOffset;
    part.presentation->SetShape(part.shape);
    context_->Redisplay(part.presentation,Standard_False);
    context_->Deactivate(part.presentation);
    context_->Activate(part.presentation,AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
    hideFaceArrow();
    if (!batchingPartUpdates_) {
        if (rotateMode_) updateRotationRings();
        if (directMoveMode_) updateMoveAxes();
    }
    view_->Redraw();
}
void CadViewport::updateMoveAxes() {
    hideMoveAxes();
    if (!directMoveMode_ || !isPartVisible(moveIndex_) || context_.IsNull()) return;
    const auto& part = boxes_[moveIndex_];
    const gp_Pnt center = selectedPartIndices_.size() > 1 ? selectionCenter() : partWorldRotationCenter(part, part.orientation);
    for (int axis=0; axis<3; ++axis) {
        const gp_Dir direction = gp_Dir(axis==0?1:0,axis==1?1:0,axis==2?1:0).Transformed(part.orientation);
        const gp_Ax2 arrowAxis(center,direction);
        const gp_Pnt tipBase = center.Translated(gp_Vec(direction).Multiplied(42.0));
        BRep_Builder builder;
        TopoDS_Compound shape;
        builder.MakeCompound(shape);
        builder.Add(shape,BRepPrimAPI_MakeCylinder(arrowAxis,1.7,42.0).Shape());
        builder.Add(shape,BRepPrimAPI_MakeCone(gp_Ax2(tipBase,direction),5.0,0.0,10.0).Shape());
        moveAxes_[axis] = new AIS_Shape(shape);
        if (commandSession_) moveAxes_[axis]->SetLocalTransformation(commandSession_->delta);
        moveAxes_[axis]->SetColor(axis==0 ? Quantity_NOC_RED : axis==1 ? Quantity_NOC_GREEN : Quantity_NOC_BLUE1);
        context_->Display(moveAxes_[axis],Standard_False);
        // Topmost layer draws the manipulator over solid geometry (depth cleared).
        context_->SetZLayer(moveAxes_[axis],Graphic3d_ZLayerId_Topmost);
        context_->SetDisplayMode(moveAxes_[axis],AIS_Shaded,Standard_False);
        context_->Deactivate(moveAxes_[axis]);
    }
    view_->Redraw();
}
void CadViewport::setTransformSelectionMode() {
    if (context_.IsNull()) return;
    const int mode = selectionMode_==SelectionMode::Part ? 0 : AIS_Shape::SelectionMode(TopAbs_FACE);
    for (auto& part : boxes_) {
        if (!part.active || !part.visible) continue;
        context_->Deactivate(part.presentation);
        context_->Activate(part.presentation, mode, Standard_False);
    }
    hideMoveAxes();
    hideRotationRings();
    // Preserve face selections when changing tools. Rebuild the OCCT
    // selection only when there is an explicit whole-part selection.
    if (!selectedPartIndices_.empty()) refreshPartSelection();
    else view_->Redraw();
}
void CadViewport::setDirectMoveMode(bool enabled) {
    if(enabled) endSketchEditing();
    if(enabled || directMoveMode_) cancelCommandSession();
    if(enabled && selectedPartIndices_.empty()) {
        emit statusMessage(tr("Move Part: select Parts with Alt+click or the Structure Browser, then choose this command."));
        return;
    }
    directMoveMode_ = enabled;
    setTransformSelectionMode();
    if (enabled) hideFaceArrow();
    emit transformModesChanged(movePartMode_,directMoveMode_,rotateMode_);
    emit statusMessage(enabled
        ? tr("Move Part: drag an axis; OK/Enter confirms, Cancel/Esc closes.")
        : tr("Move disabled. Selection preserved."));
}
bool CadViewport::beginMoveAxisDrag(const QPoint& point) {
    if (!directMoveMode_ || !isPartVisible(moveIndex_) || view_.IsNull() || moveAxes_[0].IsNull()) return false;
    const auto& part=boxes_[moveIndex_];
    const gp_Pnt center = selectedPartIndices_.size() > 1 ? selectionCenter()
        : partWorldRotationCenter(part, part.orientation);
    double best=12.0;
    int hit=-1;
    double bestDx=0.0,bestDy=0.0,bestPixels=0.0;
    for (int axis=0;axis<3;++axis) {
        const gp_Dir dir=gp_Dir(axis==0?1:0,axis==1?1:0,axis==2?1:0).Transformed(part.orientation);
        const gp_Pnt tip=center.Translated(gp_Vec(dir).Multiplied(52.0));
        Standard_Integer x0,y0,x1,y1;
        view_->Convert(center.X(),center.Y(),center.Z(),x0,y0);
        view_->Convert(tip.X(),tip.Y(),tip.Z(),x1,y1);
        const double dx=x1-x0,dy=y1-y0,len2=dx*dx+dy*dy;
        if (len2<16.0) continue;
        const double t=std::clamp(((point.x()-x0)*dx+(point.y()-y0)*dy)/len2,0.12,1.0);
        const double distance=std::hypot(point.x()-(x0+t*dx),point.y()-(y0+t*dy));
        if (distance<best) { best=distance;hit=axis;bestDx=dx;bestDy=dy;bestPixels=std::sqrt(len2); }
    }
    if (hit<0) return false;
    dragIndex_=moveIndex_;
    dragAxis_=hit;
    dragMax_=true;
    dragMovesPart_=true;
    dragMin_=part.min;
    dragMaxBounds_=part.max;
    dragStart_=point;
    dragDistance_=0.0;
    dragScreenX_=bestDx/bestPixels;
    dragScreenY_=bestDy/bestPixels;
    dragMmPerPixel_=52.0/bestPixels;
    draggingFace_=true;
    // The cursor was hovering over the old cylinder face before grabbing
    // the move gizmo. Drop OCCT's dynamic highlight now so its white edge
    // presentation cannot remain at the pre-drag position.
    context_->MoveTo(-1, -1, view_, Standard_False);
    beginCommandSession(CommandSession::Kind::Move,
        selectedPartIndices_.size()>1 ? selectedPartIndices_ : std::vector<size_t>{moveIndex_},
        gp_Dir(hit==0?1:0,hit==1?1:0,hit==2?1:0).Transformed(part.orientation));
    return true;
}
void CadViewport::hideRotationRings() {
    if (context_.IsNull()) return;
    for (auto& ring : rotationRings_) {
        if (!ring.IsNull()) { context_->Remove(ring, Standard_False); ring.Nullify(); }
    }
}
void CadViewport::updateRotationRings() {
    hideRotationRings();
    if (!rotateMode_ || !isPartVisible(rotationIndex_) || context_.IsNull()) return;
    const auto& part = boxes_[rotationIndex_];
    const gp_Pnt center = selectedPartIndices_.size() > 1 ? selectionCenter()
        : partWorldRotationCenter(part, part.orientation);
    const double radius = selectedPartIndices_.size() > 1 ? 60.0 : 0.65 * std::max({part.max[0]-part.min[0],
        part.max[1]-part.min[1], part.max[2]-part.min[2]});
    for (int axis = 0; axis < 3; ++axis) {
        const gp_Dir dir = gp_Dir(axis == 0 ? 1 : 0, axis == 1 ? 1 : 0, axis == 2 ? 1 : 0).Transformed(part.orientation);
        // A 120-degree curved arrow replaces the full 360-degree ring.
        // The tip is tangent to the arc, indicating positive rotation.
        // Keep the circle's zero-angle direction attached to the part's local frame.
        // gp_Ax2(center, dir) alone chooses an arbitrary world-aligned X direction,
        // so a curved arrow would appear stationary when rotating around its own axis.
        const gp_Dir localReference = axis == 0 ? gp_Dir(0,1,0) : gp_Dir(1,0,0);
        const gp_Dir reference = localReference.Transformed(part.orientation);
        const gp_Circ circle(gp_Ax2(center,dir,reference),radius);
        constexpr double pi = 3.14159265358979323846;
        const double start = pi / 12.0;
        const double finish = start + 2.0*pi/3.0;
        // End the shaft at the cone base; the cone tip completes the curved arrow.
        const double shaftFinish = finish - std::min(10.0 / radius, (finish-start)*0.35);
        const gp_Pnt coneBase = ElCLib::Value(shaftFinish,circle);
        const gp_Vec tangent(ElCLib::Value(shaftFinish-0.01,circle),coneBase);
        BRep_Builder builder;
        TopoDS_Compound arrow;
        builder.MakeCompound(arrow);
        // Sweep a circular profile along the arc: same solid, shaded style as move arrows.
        const double shaftRadius = 1.7;
        const gp_Pnt arcStart = ElCLib::Value(start, circle);
        const gp_Vec startTangent(arcStart, ElCLib::Value(start+0.01, circle));
        const TopoDS_Wire arcPath = BRepBuilderAPI_MakeWire(
            BRepBuilderAPI_MakeEdge(circle, start, shaftFinish)).Wire();
        const TopoDS_Wire profile = BRepBuilderAPI_MakeWire(
            BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(arcStart, gp_Dir(startTangent)), shaftRadius))).Wire();
        builder.Add(arrow, BRepOffsetAPI_MakePipe(arcPath, BRepBuilderAPI_MakeFace(profile).Face()).Shape());
        builder.Add(arrow,BRepPrimAPI_MakeCone(gp_Ax2(coneBase,gp_Dir(tangent)),4.5,0.0,10.0).Shape());
        rotationRings_[axis] = new AIS_Shape(arrow);
        if (commandSession_) rotationRings_[axis]->SetLocalTransformation(commandSession_->delta);
        rotationRings_[axis]->SetColor(axis == 0 ? Quantity_NOC_RED
            : axis == 1 ? Quantity_NOC_GREEN : Quantity_NOC_BLUE1);
        context_->Display(rotationRings_[axis], Standard_False);
        context_->SetDisplayMode(rotationRings_[axis], AIS_Shaded, Standard_False);
        context_->SetZLayer(rotationRings_[axis], Graphic3d_ZLayerId_Topmost);
        context_->Deactivate(rotationRings_[axis]);
    }
    view_->Redraw();
}
bool CadViewport::beginRotationDrag(const QPoint& point) {
    if (!rotateMode_ || !isPartVisible(rotationIndex_) || view_.IsNull() || rotationRings_[0].IsNull()) return false;
    const auto& part = boxes_[rotationIndex_];
    const gp_Pnt center = selectedPartIndices_.size() > 1 ? selectionCenter()
        : partWorldRotationCenter(part, part.orientation);
    const double radius = selectedPartIndices_.size() > 1 ? 60.0 : 0.65 * std::max({part.max[0]-part.min[0],
        part.max[1]-part.min[1], part.max[2]-part.min[2]});
    double best = 16.0;
    int axisHit = -1;
    for (int axis = 0; axis < 3; ++axis) {
        const gp_Dir dir = gp_Dir(axis == 0 ? 1 : 0, axis == 1 ? 1 : 0, axis == 2 ? 1 : 0).Transformed(part.orientation);
        // Keep the circle's zero-angle direction attached to the part's local frame.
        // gp_Ax2(center, dir) alone chooses an arbitrary world-aligned X direction,
        // so a curved arrow would appear stationary when rotating around its own axis.
        const gp_Dir localReference = axis == 0 ? gp_Dir(0,1,0) : gp_Dir(1,0,0);
        const gp_Dir reference = localReference.Transformed(part.orientation);
        const gp_Circ circle(gp_Ax2(center,dir,reference),radius);
        for (int i = 0; i <= 96; ++i) {
            const double angle = 3.14159265358979323846 / 12.0 + i * (2.0 * 3.14159265358979323846 / 3.0) / 96.0;
            const gp_Pnt world = ElCLib::Value(angle, circle);
            Standard_Integer x,y;
            view_->Convert(world.X(),world.Y(),world.Z(),x,y);
            const double distance = std::hypot(point.x()-x,point.y()-y);
            if (distance < best) { best = distance; axisHit = axis; }
        }
    }
    if (axisHit < 0) return false;
    rotationAxis_ = axisHit;
    groupRotationDragging_ = selectedPartIndices_.size() > 1;
    groupRotationCenter_ = center;
    rotationBefore_ = part.orientation;
    rotationAngle_ = 0.0;
    dragStart_ = point;
    rotationStartAngle_ = rotationCursorAngle(point, rotationAxis_, rotationBefore_);
    rotationPreviousAngle_ = rotationStartAngle_;
    rotationAccumulatedAngle_ = 0.0;
    draggingRotation_ = true;
    hideFaceArrow();
    // Match direct-move startup: remove the previous OCCT hover outline
    // before changing the part's local transformation.
    if (!context_.IsNull()) {
        context_->MoveTo(-1, -1, view_, Standard_False);
        view_->Redraw();
    }
    beginCommandSession(CommandSession::Kind::Rotate,
        selectedPartIndices_.size()>1 ? selectedPartIndices_ : std::vector<size_t>{rotationIndex_},
        gp_Dir(axisHit==0?1:0,axisHit==1?1:0,axisHit==2?1:0).Transformed(part.orientation),center);
    return true;
}
double CadViewport::rotationCursorAngle(const QPoint& point, int axis, const gp_Trsf& orientation) const {
    const auto& part = boxes_[rotationIndex_];
    const gp_Pnt center = groupRotationDragging_ ? groupRotationCenter_
        : partWorldRotationCenter(part, orientation);
    const double radius = groupRotationDragging_ ? 60.0 : 0.65 * std::max({part.max[0]-part.min[0],
        part.max[1]-part.min[1], part.max[2]-part.min[2]});
    const gp_Dir dir = gp_Dir(axis == 0 ? 1 : 0, axis == 1 ? 1 : 0, axis == 2 ? 1 : 0).Transformed(orientation);
    // Use exactly the same local reference frame as the displayed rotation
    // gizmo and beginRotationDrag(). Otherwise the angle can jump or appear
    // unchanged while dragging a single part.
    const gp_Dir localReference = axis == 0 ? gp_Dir(0,1,0) : gp_Dir(1,0,0);
    const gp_Dir reference = localReference.Transformed(orientation);
    const gp_Circ circle(gp_Ax2(center,dir,reference),radius);
    double best = 1e100;
    double result = 0.0;
    constexpr int samples = 360;
    for (int i=0; i<samples; ++i) {
        const double angle = i * 2.0 * 3.14159265358979323846 / samples;
        const gp_Pnt world = ElCLib::Value(angle,circle);
        Standard_Integer x,y;
        view_->Convert(world.X(),world.Y(),world.Z(),x,y);
        const double d = std::hypot(point.x()-x,point.y()-y);
        if (d < best) { best=d; result=angle; }
    }
    return result;
}
void CadViewport::applyRotationAngle(double degrees) {
    if (!commandSession_) return;
    rotationAngle_ = degrees;
    previewCommand(degrees);
}
void CadViewport::setRotateMode(bool enabled) {
    if(enabled) endSketchEditing();
    if(enabled || rotateMode_) cancelCommandSession();
    if(enabled && selectedPartIndices_.empty()) {
        emit statusMessage(tr("Rotate Part: select Parts with Alt+click or the Structure Browser, then choose this command."));
        return;
    }
    rotateMode_ = enabled;
    setTransformSelectionMode();
    if (enabled) hideFaceArrow();
    emit transformModesChanged(movePartMode_,directMoveMode_,rotateMode_);
    emit statusMessage(enabled
        ? tr("Rotate Part: drag a ring; OK/Enter confirms, Cancel/Esc closes.")
        : tr("Rotate disabled. Selection preserved."));
}
void CadViewport::setAngleSnap(double degrees) { rotationSnap_ = degrees; }

void CadViewport::mousePressEvent(QMouseEvent* e) {
    if (e->button()==Qt::RightButton) return;
    previous_ = e->pos();
    rotating_ = e->button() == Qt::MiddleButton && !(e->modifiers() & Qt::ShiftModifier);
    panning_ = e->button() == Qt::MiddleButton && (e->modifiers() & Qt::ShiftModifier);
    if (rotating_ && !view_.IsNull()) view_->StartRotation(previous_.x(), previous_.y());
    if (e->button() == Qt::LeftButton && !context_.IsNull()) {
        if(targetPicking()) {
            const auto index=detectedPart(e->pos());
            if(!index) {emit statusMessage(tr("Select target part: click visible part geometry."));return;}
            const auto error=targetEligibility_?targetEligibility_(*index):QString();
            if(!error.isEmpty()){emit statusMessage(error);return;}
            const auto id=partId(*index);auto accepted=targetAccepted_;cancelTargetPicking();accepted(id);return;
        }
        if(pickMoveDirection(e->pos())) return;
        if(commandSession_ && commandSession_->sweep) {
            if(commandSession_->kind==CommandSession::Kind::Revolve && commandSession_->sweep->axisPicking) {
                context_->MoveTo(e->pos().x(),e->pos().y(),view_,Standard_True);
                chooseRevolutionAxis(context_->HasDetected()?context_->DetectedOwner():Handle(SelectMgr_EntityOwner)());
            }
            return;
        }
        if(sketchProjectionClick(e->pos(),e->modifiers())) return;
        if(sketchTrimClick(e->pos())) return;
        if(sketchExtendClick(e->pos())) return;
        if(sketchConstructionClick(e->pos())) return;
        if(overdrawClick(e->pos())) return;
        if(sketchDrawingClick(e->pos(),e->modifiers())) return;
        if(!activeSketchWorkplaneId_.isEmpty()) {
            if(e->modifiers()&Qt::AltModifier) endSketchEditing();
            else {
                if(!sketchMousePress(e->pos(),e->modifiers())) selectSketchEntities({});
                return;
            }
        }
        if(sketchMousePress(e->pos(),e->modifiers())) return;
        clearSketchSelection();
        if(constructionMousePress(e->pos(),e->modifiers())) return;
        if(!selectedConstructionIds_.empty()) {selectedConstructionIds_.clear();emit constructionSelectionChanged({});}
        if (commandSession_ && commandSession_->kind==CommandSession::Kind::Boolean) {
            context_->MoveTo(e->pos().x(),e->pos().y(),view_,Standard_True);
            if(context_->HasDetected()) {
                const auto object=context_->DetectedInteractive();
                for(size_t i=0;i<boxes_.size();++i) if(boxes_[i].presentation==object) { selectBooleanParts({i},true); break; }
            }
            return;
        }
        if(commandSession_ && (commandSession_->kind==CommandSession::Kind::Fillet || commandSession_->kind==CommandSession::Kind::Chamfer) && !commandSession_->faceBefore) { pickEdgeBlend(e->pos()); return; }
        if(faceAxisPicking_) { pickFaceRotationAxis(e->pos()); return; }
        if(alignFacePickingReference_) { pickAlignReference(e->pos()); return; }
        if(commandSession_ && commandSession_->kind==CommandSession::Kind::RotateFace
           && beginFaceRotationDrag(e->pos())) return;
        if(commandSession_ && (commandSession_->kind==CommandSession::Kind::MoveFace
            ||commandSession_->kind==CommandSession::Kind::Fillet||commandSession_->kind==CommandSession::Kind::Chamfer)
            &&beginMoveFaceDrag(e->pos())) return;
        if (moveFacePicking_) {
            for (const auto& part : boxes_) if (part.active && part.visible) {
                context_->Deactivate(part.presentation);
                context_->Activate(part.presentation,AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
            }
            context_->MoveTo(e->pos().x(),e->pos().y(),view_,Standard_True);
            context_->SelectDetected(AIS_SelectionScheme_Replace);
            selectedPartIndices_.clear();
            emit partSelectionChanged(selectionContext().parts);
            if (context_->NbSelected()==1) {
                context_->InitSelected();
                const auto owner=Handle(StdSelect_BRepOwner)::DownCast(context_->SelectedOwner());
                const auto object=context_->SelectedInteractive();
                if (!owner.IsNull() && !owner->Shape().IsNull() && owner->Shape().ShapeType()==TopAbs_FACE)
                    for (size_t i=0;i<boxes_.size();++i)
                        if (boxes_[i].presentation==object) { startMoveFace(i,TopoDS::Face(owner->Shape())); break; }
            }
            return;
        }
        if (dimensionEditing_) {
            clearMultiFaceHighlight();
            multiFaceEditing_ = false;
            primitivePullEditing_ = false;
            dimensionEditing_ = false;
            dragDimension_->hide();
        }
        bool partPicking = (e->modifiers() & Qt::AltModifier);
        const bool additiveSelection = (e->modifiers() & Qt::ControlModifier);
        // Start manipulator drags BEFORE changing OCCT selection modes:
        // Deactivate/Activate may discard selected subshape owners.
        if (!(e->modifiers() & Qt::AltModifier) && !additiveSelection && beginRotationDrag(e->pos())) return;
        if (!(e->modifiers() & Qt::AltModifier) && !additiveSelection && beginMoveAxisDrag(e->pos())) return;
        if (!partPicking && !additiveSelection && !directMoveMode_ && !rotateMode_
            && beginArrowDrag(e->pos())) return;
        cancelCommandSession();
        activateAutomaticSelection(partPicking);
        // Alt-Part picking must pass through visible construction geometry.
        if(partPicking) {
            for(const auto& [id,presentation]:constructionPresentations_) context_->Deactivate(presentation);
            for(const auto& [id,presentation]:sketchPresentations_) context_->Deactivate(presentation);
        }
        // Ctrl is reserved for additive face selection, not drag-to-pull.
        context_->MoveTo(e->position().x(), e->position().y(), view_, Standard_True);
        // A second independent press on the same sole subelement promotes it.
        // No timer or stored TopoDS identity survives geometry replacement.
        if(!partPicking&&!additiveSelection&&context_->HasDetected()&&context_->NbSelected()==1) {
            context_->InitSelected();
            const auto owner=Handle(StdSelect_BRepOwner)::DownCast(context_->SelectedOwner());
            const auto hit=Handle(StdSelect_BRepOwner)::DownCast(context_->DetectedOwner());
            partPicking=!owner.IsNull()&&!hit.IsNull()&&owner->Selectable()==hit->Selectable()
                &&!owner->Shape().IsNull()&&(owner->Shape().ShapeType()==TopAbs_FACE||owner->Shape().ShapeType()==TopAbs_EDGE)
                &&owner->Shape().IsSame(hit->Shape());
        }
        if (partPicking) {
            if (!context_->SelectionStyle().IsNull())
                context_->SelectionStyle()->SetDisplayMode(AIS_WireFrame);
            // Keep a separate set of part indices. OCCT selection owners are
            // replaced during manipulator display updates and cannot be used
            // as the sole source of truth for additive part selection.
            const bool multiSelect = additiveSelection;
            // DetectedInteractive() must only be called when a valid owner exists.
            // Clicking empty viewport space otherwise dereferences a null owner
            // in OCCT and segfaults before we can clear the selection.
            size_t hit = boxes_.size();
            if (context_->HasDetected()) {
                const auto detected = context_->DetectedInteractive();
                if (!detected.IsNull()) {
                    for (size_t i = 0; i < boxes_.size(); ++i) {
                        if (boxes_[i].presentation == detected) {
                            hit = i;
                            break;
                        }
                    }
                }
            }
            if (!multiSelect) selectedPartIndices_.clear();
            if (hit < boxes_.size()) {
                const auto it = std::find(selectedPartIndices_.begin(),
                                          selectedPartIndices_.end(), hit);
                if (it != selectedPartIndices_.end()) {
                    if (multiSelect) selectedPartIndices_.erase(it);
                } else {
                    selectedPartIndices_.push_back(hit);
                }
            }
            hideFaceArrow();
            hideMoveAxes();
            hideRotationRings();
            context_->ClearSelected(Standard_False);
            for (const size_t i : selectedPartIndices_)
                context_->AddOrRemoveSelected(boxes_[i].presentation, Standard_False);
            if (!selectedPartIndices_.empty()) {
                moveIndex_ = selectedPartIndices_.front();
                rotationIndex_ = moveIndex_;
                if (directMoveMode_) updateMoveAxes();
                if (rotateMode_) updateRotationRings();
            }
            emit partSelectionChanged(selectedPartIndices_);
            for(const auto& object:constructions_) if(constructionShown(object) && object.selectable)
                context_->Activate(constructionPresentations_.at(object.id),object.kind==Construction::Kind::Workplane?AIS_Shape::SelectionMode(TopAbs_EDGE):0,Standard_False);
            for(const auto& entity:sketchEntities_) if(sketchPickable(entity)) context_->Activate(sketchPresentations_.at(entity.id),0,Standard_False);
            const int count = static_cast<int>(selectedPartIndices_.size());
            emit statusMessage(count > 1
                ? tr("%1 parts selected. Drag an axis to move the group.").arg(count)
                : count == 1 ? tr("Part selected. Drag a transform arrow.")
                             : tr("Selection cleared."));
            view_->Redraw();
            return;
        }
        // Use OCCT's additive selection scheme for Ctrl+click on faces.
        // Face contours are drawn in the shaded Part without changing material.
        if (!context_->SelectionStyle().IsNull())
            context_->SelectionStyle()->SetDisplayMode(AIS_WireFrame);
        // Additive selection stays typed: Ctrl on a face starts a face set
        // if the prior selection consisted of whole parts (and vice versa).
        const auto hitOwner=context_->HasDetected()?Handle(StdSelect_BRepOwner)::DownCast(context_->DetectedOwner()):Handle(StdSelect_BRepOwner)();
        const auto hitKind=!hitOwner.IsNull()&&!hitOwner->Shape().IsNull()&&hitOwner->Shape().ShapeType()==TopAbs_EDGE
            ?SelectionContext::Kind::Edges:SelectionContext::Kind::Faces;
        if(selectionContext().kind!=hitKind) context_->ClearSelected(Standard_False);
        context_->SelectDetected(additiveSelection
            ? AIS_SelectionScheme_XOR : AIS_SelectionScheme_Replace);
        hideFaceArrow();
        if (context_->NbSelected() == 0) {
            // A background click must not leave former part IDs as Boolean
            // operands after their visible selection has been cleared.
            selectedPartIndices_.clear();
            hideMoveAxes(); hideRotationRings();
            emit partSelectionChanged(selectedPartIndices_);
        }
        // Face selection has its own owners; browser rows reflect their owning parts,
        // without turning the face selection into a whole-part gizmo group.
        selectedPartIndices_.clear();
        emit partSelectionChanged(selectionContext().parts);
        directMoveMode_=rotateMode_=false;
        hideMoveAxes(); hideRotationRings();
        emit transformModesChanged(movePartMode_,false,false);
        if (context_->NbSelected() == 1
            && !(arrowVisible_ && (boxes_[arrowPartIndex_].spherePrimitive
                || boxes_[arrowPartIndex_].conePrimitive))) {
            context_->InitSelected();
            const TopoDS_Shape picked = context_->SelectedShape();
            emit statusMessage(tr("Selected shape: %1; B-Rep owner: %2")
                .arg(picked.IsNull() ? -1 : static_cast<int>(picked.ShapeType()))
                .arg(Handle(StdSelect_BRepOwner)::DownCast(context_->SelectedOwner()).IsNull() ? tr("no") : tr("yes")));
        }
        view_->Redraw();
    }
}
void CadViewport::updateSelectedFaceArrow(const QPoint& point) {
    // Keep one manipulation arrow as an anchor for a multi-face selection.
    // The grouped drag operation will use all selected face owners.
    cylindricalFacePull_=false;bossHeightPull_=false;bossPullHeight_=0;
    cylindricalPullFace_.Nullify();
    cylindricalPullRadius_=0.0;
    if (context_->NbSelected() >= 1) {
        context_->InitSelected();
        Handle(StdSelect_BRepOwner) arrowOwner =
            Handle(StdSelect_BRepOwner)::DownCast(context_->SelectedOwner());
        if (!arrowOwner.IsNull() && !arrowOwner->Shape().IsNull()
            && arrowOwner->Shape().ShapeType() == TopAbs_FACE) {
            Handle(AIS_InteractiveObject) selected = context_->SelectedInteractive();
            for (size_t i = 0; i < boxes_.size(); ++i) {
                if (boxes_[i].presentation != selected) continue;
                const BRepAdaptor_Surface surf(TopoDS::Face(arrowOwner->Shape()));
                if(!movePartMode_&&surf.GetType()==GeomAbs_Plane&&!boxes_[i].cylinderPrimitive&&!boxes_[i].spherePrimitive&&!boxes_[i].conePrimitive) {
                    const auto reference=CylindricalMoveGeometry::bossHeightReference(boxes_[i].shape,TopoDS::Face(arrowOwner->Shape()));
                    if(reference) {
                        cylindricalFacePull_=true;bossHeightPull_=true;cylindricalPullFace_=TopoDS::Face(arrowOwner->Shape());
                        cylindricalPullRadius_=reference.radius;bossPullHeight_=reference.height;
                        GProp_GProps p;BRepGProp::SurfaceProperties(cylindricalPullFace_,p);primitiveArrowOrigin_=p.CentreOfMass();primitiveArrowDirection_=reference.axis.Direction();
                        arrowPartIndex_=i;arrowVisible_=true;updateFaceArrow();
                        emit statusMessage(tr("Boss cap: drag the arrow or enter the final height in mm."));break;
                    }
                }
                if (!movePartMode_ && surf.GetType()==GeomAbs_Cylinder
                    && !boxes_[i].cylinderPrimitive && !boxes_[i].spherePrimitive
                    && !boxes_[i].conePrimitive) {
                    double volume=0.0;
                    if (!BRepReconstruction::validSolid(boxes_[i].shape,volume)) break;
                    const auto cylinder=surf.Cylinder();
                    const gp_Vec axis(cylinder.Axis().Direction());
                    // Choose a camera-facing radial direction. A fixed local
                    // X axis can point into the screen and make a zero-length
                    // projected arrow. The topmost layer keeps an inner wall's
                    // outward (diameter-increasing) handle visible over material.
                    Standard_Real x,y,z,rx,ry,rz,ux,uy,uz;
                    view_->Convert(point.x(),point.y(),x,y,z);
                    view_->Convert(point.x()+40,point.y(),rx,ry,rz);
                    view_->Convert(point.x(),point.y()+40,ux,uy,uz);
                    const auto inverse=boxes_[i].orientation.Inverted();
                    gp_Vec radial=gp_Vec(gp_Pnt(x,y,z),gp_Pnt(rx,ry,rz)).Transformed(inverse);
                    gp_Vec alternate=gp_Vec(gp_Pnt(x,y,z),gp_Pnt(ux,uy,uz)).Transformed(inverse);
                    radial-=axis.Multiplied(radial.Dot(axis));
                    alternate-=axis.Multiplied(alternate.Dot(axis));
                    if (alternate.SquareMagnitude()>radial.SquareMagnitude()) radial=alternate;
                    if (radial.Magnitude()<1.0e-6) break;
                    radial.Normalize();
                    GProp_GProps properties;
                    BRepGProp::SurfaceProperties(TopoDS::Face(arrowOwner->Shape()),properties);
                    const gp_Vec axisToCenter(cylinder.Axis().Location(),properties.CentreOfMass());
                    const gp_Pnt axialCenter=cylinder.Axis().Location().Translated(
                        axis.Multiplied(axisToCenter.Dot(axis)));
                    cylindricalFacePull_=true;
                    cylindricalPullFace_=TopoDS::Face(arrowOwner->Shape());
                    cylindricalPullRadius_=cylinder.Radius();
                    primitiveArrowDirection_=gp_Dir(radial);
                    primitiveArrowOrigin_=axialCenter.Translated(radial.Multiplied(cylindricalPullRadius_));
                    arrowPartIndex_=i;
                    arrowVisible_=true;
                    updateFaceArrow();
                    emit statusMessage(tr("Cylindrical face: drag the arrow or enter the final diameter in mm."));
                    break;
                }
                if (boxes_[i].conePrimitive && !movePartMode_) {
                    const auto& part = boxes_[i];
                    Standard_Real x,y,z,rx,ry,rz;
                    view_->Convert(point.x(),point.y(),x,y,z);
                    view_->Convert(point.x()+40,point.y(),rx,ry,rz);
                    const gp_Vec right = gp_Vec(gp_Pnt(x,y,z),gp_Pnt(rx,ry,rz))
                        .Transformed(part.orientation.Inverted());
                    gp_Vec radial(right.X(),right.Y(),0);
                    if (radial.Magnitude() < 1.0e-6) radial = gp_Vec(1,0,0);
                    coneRadialDirection_ = gp_Dir(radial);
                    conePullTarget_ = ConePullTarget::TopHeight;
                    primitiveArrowOrigin_ = coneArrowOrigin(part,conePullTarget_);
                    primitiveArrowDirection_ = coneArrowDirection(conePullTarget_);
                    arrowPartIndex_ = i;
                    arrowVisible_ = true;
                    updateFaceArrow();
                    emit statusMessage(tr("Cone: radial arrows set bottom/top diameter; "
                        "axial arrows set height with the opposite end fixed."));
                    break;
                }
                if (boxes_[i].spherePrimitive && !movePartMode_) {
                    const auto& part = boxes_[i];
                    const gp_Pnt center = partLocalCenter(part);
                    Standard_Real eyeX, eyeY, eyeZ, rayX, rayY, rayZ;
                    view_->ConvertWithProj(point.x(), point.y(),
                        eyeX, eyeY, eyeZ, rayX, rayY, rayZ);
                    const gp_Trsf inverse = part.orientation.Inverted();
                    const gp_Pnt rayOrigin = gp_Pnt(eyeX,eyeY,eyeZ).Transformed(inverse);
                    const gp_Vec rayDirection = gp_Vec(rayX,rayY,rayZ).Transformed(inverse);
                    const gp_Vec toCenter(rayOrigin,center);
                    const double t = toCenter.Dot(rayDirection)/rayDirection.SquareMagnitude();
                    gp_Vec radial(center,rayOrigin.Translated(rayDirection*t));
                    if (radial.Magnitude() < 1.0e-6) {
                        // Use screen right for a visible, draggable arrow even
                        // when the user clicks the center of the sphere.
                        Standard_Real rightX, rightY, rightZ;
                        view_->Convert(point.x()+40,point.y(),rightX,rightY,rightZ);
                        radial = gp_Vec(gp_Pnt(eyeX,eyeY,eyeZ),
                            gp_Pnt(rightX,rightY,rightZ)).Transformed(inverse);
                    }
                    if (radial.Magnitude() < 1.0e-6) break;
                    primitiveArrowDirection_ = gp_Dir(radial);
                    primitiveArrowOrigin_ = center.Translated(
                        gp_Vec(primitiveArrowDirection_)*part.sphereRadius);
                    arrowPartIndex_ = i;
                    arrowVisible_ = true;
                    updateFaceArrow();
                    emit statusMessage(tr("Drag the orange arrow; enter the final diameter in mm. "
                        "Current diameter: Ø %1 mm.")
                        .arg(QLocale().toString(2.0*part.sphereRadius,'f',2)));
                    break;
                }
                if (boxes_[i].cylinderPrimitive && !movePartMode_) {
                    const auto& part = boxes_[i];
                    const gp_Pnt base((part.baseMin[0]+part.baseMax[0])*0.5,
                        (part.baseMin[1]+part.baseMax[1])*0.5,part.baseMin[2]+part.cylinderBaseOffset);
                    cylinderRadialArrow_ = surf.GetType() == GeomAbs_Cylinder;
                    if (!cylinderRadialArrow_ && surf.GetType() != GeomAbs_Plane) break;
                    if (cylinderRadialArrow_) {
                        // Choose a stable outward radial arrow based on cursor position.
                        // Project the cursor ray into the cylinder's local XY plane.
                        // AIS_InteractiveContext::DetectedPoint is not available
                        // in the OCCT version shipped by Fedora.
                        Standard_Real eyeX, eyeY, eyeZ, rayX, rayY, rayZ;
                        view_->ConvertWithProj(point.x(), point.y(),
                            eyeX, eyeY, eyeZ, rayX, rayY, rayZ);
                        const gp_Pnt rayOrigin = gp_Pnt(eyeX,eyeY,eyeZ)
                            .Transformed(part.orientation.Inverted());
                        const gp_Vec rayDirection = gp_Vec(rayX,rayY,rayZ)
                            .Transformed(part.orientation.Inverted());
                        const double middleZ = base.Z()+part.cylinderHeight*0.5;
                        double t = 0.0;
                        if (std::abs(rayDirection.Z()) > 1.0e-8)
                            t = (middleZ-rayOrigin.Z())/rayDirection.Z();
                        const gp_Pnt hit = rayOrigin.Translated(rayDirection.Multiplied(t));
                        const gp_Vec radial(base,gp_Pnt(hit.X(),hit.Y(),base.Z()));
                        if (radial.Magnitude() < 1.0e-6) break;
                        primitiveArrowDirection_ = gp_Dir(radial);
                        primitiveArrowOrigin_ = gp_Pnt(base.X()+primitiveArrowDirection_.X()*part.cylinderRadius,
                            base.Y()+primitiveArrowDirection_.Y()*part.cylinderRadius,
                            base.Z()+part.cylinderHeight*0.5);
                    } else {
                        const double z = surf.Plane().Location().Z();
                        const bool top = std::abs(z-(base.Z()+part.cylinderHeight)) <
                            std::abs(z-base.Z());
                        primitiveArrowDirection_ = gp_Dir(0,0,top?1:-1);
                        primitiveArrowOrigin_ = gp_Pnt(base.X(),base.Y(),
                            top?base.Z()+part.cylinderHeight:base.Z());
                    }
                    arrowPartIndex_ = i;
                    arrowVisible_ = true;
                    updateFaceArrow();
                    break;
                }
                cylinderRadialArrow_ = false;
                if (boxes_[i].geometryKind != CadPart::GeometryKind::Box && !movePartMode_) break;
                if (surf.GetType() != GeomAbs_Plane) break;
                const gp_Dir normal = surf.Plane().Axis().Direction();
                const double components[3] = {normal.X(), normal.Y(), normal.Z()};
                int axis = 0;
                for (int k = 1; k < 3; ++k)
                    if (std::abs(components[k]) > std::abs(components[axis])) axis = k;
                if (std::abs(components[axis]) < 0.999) break;
                const double pos = surf.Plane().Location().Coord(axis + 1);
                const bool onMax = std::abs(pos - boxes_[i].max[axis]) < 0.001;
                const bool onMin = std::abs(pos - boxes_[i].min[axis]) < 0.001;
                if (!onMax && !onMin) break;
                arrowPartIndex_ = i;
                arrowAxis_ = axis;
                arrowMax_ = onMax;
                arrowVisible_ = true;
                updateFaceArrow();
                break;
            }
        }
    }
}
void CadViewport::setMovePartMode(bool enabled) {
    if(enabled) endSketchEditing();
    if(enabled || movePartMode_) cancelCommandSession();
    if (arrowVisible_ && boxes_[arrowPartIndex_].conePrimitive) hideFaceArrow();
    movePartMode_ = enabled;
    if(enabled) {
        updateSelectedFaceArrow(previous_);
        if(!arrowVisible_) {
            resetModelingTools();
            emit statusMessage(tr("Move Part Along Face: select a supported face before choosing this command."));
            return;
        }
    }
    else hideFaceArrow();
    emit transformModesChanged(movePartMode_,directMoveMode_,rotateMode_);
    emit statusMessage(enabled
        ? tr("Move Part mode: select a face, then drag its orange arrow.")
        : tr("Move Part Along Face closed. Select geometry and choose a command."));
}
void CadViewport::setSnapStep(double stepMm) {
    snapStepMm_ = stepMm;
    emit statusMessage(stepMm > 0.0
        ? tr("Face snapping: %1 mm (hold Shift to bypass).").arg(stepMm)
        : tr("Face snapping disabled."));
}
void CadViewport::mouseMoveEvent(QMouseEvent* e) {
    if(!rotating_ && !panning_ && (sketchTrimMove(e->pos()) || sketchExtendMove(e->pos()) || sketchConstructionMove(e->pos()) || overdrawMove(e->pos()) || sketchDrawingMove(e->pos(),e->modifiers()))) return;
    if (view_.IsNull()) return;
    QPoint p = e->pos();
    if(constructionMouseMove(p,e->modifiers())) return;
    if(draggingFaceRotation_) {
        const double angle=faceRotationCursorAngle(p);
        double delta=angle-faceRotationPreviousAngle_;
        while(delta>std::acos(-1.0)) delta-=2.0*std::acos(-1.0);
        while(delta<-std::acos(-1.0)) delta+=2.0*std::acos(-1.0);
        faceRotationPreviousAngle_=angle;
        faceRotationAccumulatedAngle_+=delta;
        double degrees=faceRotationAccumulatedAngle_*180.0/std::acos(-1.0);
        if(rotationSnap_>0.0 && !(e->modifiers()&Qt::ShiftModifier)) degrees=std::round(degrees/rotationSnap_)*rotationSnap_;
        previewCommand(degrees);
        dragDimension_->setText(QLocale().toString(degrees,'f',2));
        showCommandControls(p);
    } else if (draggingRotation_) {
        const double angle = rotationCursorAngle(p, rotationAxis_, rotationBefore_);
        double delta = angle - rotationPreviousAngle_;
        constexpr double pi = 3.14159265358979323846;
        while (delta > pi) delta -= 2.0*pi;
        while (delta < -pi) delta += 2.0*pi;
        rotationPreviousAngle_ = angle;
        rotationAccumulatedAngle_ += delta;
        const double raw = rotationAccumulatedAngle_ * 180.0 / pi;
        const double degrees = rotationSnap_ > 0.0 && !(e->modifiers() & Qt::ShiftModifier)
            ? std::round(raw/rotationSnap_) * rotationSnap_ : raw;
        if (std::abs(degrees-rotationAngle_) > 0.0001) {
            applyRotationAngle(degrees);
            dragDimension_->setText(QLocale().toString(degrees,'f',2));
            showCommandControls(p);
            emit statusMessage(tr("Rotation: %1 degrees").arg(degrees,0,'f',1));
        }
    } else if (draggingFace_) {
        const double rawDistance = ((p.x() - dragStart_.x()) * dragScreenX_
                                  + (p.y() - dragStart_.y()) * dragScreenY_) * dragMmPerPixel_
            + (commandSession_ && (commandSession_->kind==CommandSession::Kind::MoveFace
                ||commandSession_->kind==CommandSession::Kind::Fillet||commandSession_->kind==CommandSession::Kind::Chamfer) ? moveFaceDragStart_ : 0.0);
        const bool snapping = snapStepMm_ > 0.0 && !(e->modifiers() & Qt::ShiftModifier);
        const double requested = snapping
            ? std::round(rawDistance / snapStepMm_) * snapStepMm_
            : rawDistance;
        if (commandSession_) {
            const bool blend=commandSession_->kind==CommandSession::Kind::Fillet||commandSession_->kind==CommandSession::Kind::Chamfer;
            if(blend&&std::abs(requested-commandSession_->value)<dragMmPerPixel_*0.05) {previous_=p;return;}
            dragDistance_ = requested;
            previewCommand(requested);
            dragDimension_->setText(commandSession_->kind==CommandSession::Kind::Pull && primitivePullDragging_ ? primitivePullDimensionText() : blend?QLocale().toString(requested,'g',17):QLocale().toString(requested,'f',2));
            showCommandControls(p);
            previous_ = p;
            return;
        }
        cancelCommandSession();
    } else if (rotating_) view_->Rotation(p.x(), p.y());
    else if (panning_) view_->Pan(p.x() - previous_.x(), previous_.y() - p.y());
    else if (!context_.IsNull()) {
        const auto before=context_->DetectedOwner();
        if(!commandSession_&&!moveFacePicking_&&!faceAxisPicking_&&!alignFacePickingReference_&&!targetPicking()&&activeSketchWorkplaneId_.isEmpty())
            activateAutomaticSelection(e->modifiers()&Qt::AltModifier);
        context_->MoveTo(p.x(),p.y(),view_,Standard_True);
        if(before!=context_->DetectedOwner()) refreshActivePart();
    }
    if(rotating_ || panning_) {refreshActivePart();updateConstructionLabels();view_->Redraw();}
    previous_ = p;
}
void CadViewport::mouseReleaseEvent(QMouseEvent* e) {
    if(e->button()==Qt::LeftButton) constructionMouseRelease();
    if (e->button() == Qt::MiddleButton) { rotating_ = false; panning_ = false; }
    if(e->button()==Qt::LeftButton && commandSession_ && (commandSession_->sweep || commandSession_->kind==CommandSession::Kind::Boolean
        || commandSession_->kind==CommandSession::Kind::Construction || commandSession_->kind==CommandSession::Kind::Sketch)) return;
    if(e->button()==Qt::LeftButton && commandSession_
       && (commandSession_->kind==CommandSession::Kind::Fillet || commandSession_->kind==CommandSession::Kind::Chamfer)) {
        draggingFace_=false;
        showCommandControls(e->pos());
        if(commandSession_->faceBefore) {
            commandPanel_->activateWindow();dragDimension_->setFocus();dragDimension_->selectAll();
        }
        return;
    }
    if (e->button()==Qt::LeftButton && commandSession_) {
        draggingFace_ = draggingRotation_ = draggingFaceRotation_ = false;
        groupRotationDragging_ = false;
        showCommandControls(e->pos());
        dragDimension_->setText(commandSession_->kind==CommandSession::Kind::Pull && primitivePullDragging_ ? primitivePullDimensionText() : QLocale().toString(commandSession_->value,'f',2));
        commandPanel_->activateWindow();
        dragDimension_->setFocus(); dragDimension_->selectAll();
        emit statusMessage(tr("Preview: enter an exact value; OK/Enter confirms, Cancel/Esc discards."));
        return;
    }

}
void CadViewport::wheelEvent(QWheelEvent* e) {
    if (view_.IsNull()) return;
    const double scale = e->angleDelta().y() > 0 ? 0.8 : 1.25;
    view_->SetScale(view_->Scale() * scale);
    updateConstructionLabels();
    view_->Redraw();
}

QString CadViewport::partId(size_t index) const {
    return isPartActive(index)?boxes_[index].persistentId:QString();
}
std::optional<size_t> CadViewport::partIndexForId(const QString& id) const {
    if(!id.isEmpty()) for(size_t i=0;i<boxes_.size();++i)
        if(boxes_[i].active && boxes_[i].persistentId==id)return i;
    return {};
}
bool CadViewport::setActivePart(const QString& id) {
    if(!id.isEmpty() && !partIndexForId(id))return false;
    if(activePartId_==id)return true;
    activePartId_=id;refreshActivePart();emit activePartChanged();return true;
}
namespace {
// OpenGL inverted-hull outlines use the existing closed triangulation and
// camera matrices. Never substitute topological boundaries on older OCCT.
template<class Aspect>
std::optional<bool> activeSilhouette(Aspect& aspect,bool enabled,const Quantity_Color& color,double width) {
    if constexpr(requires { aspect.SetDrawSilhouette(enabled); }) {
        const bool changed=aspect.ToDrawSilhouette()!=enabled || aspect.ToDrawEdges()
            || (enabled && (aspect.EdgeColor()!=color || std::abs(aspect.EdgeWidth()-width)>0.001));
        if(!changed)return false;
        aspect.SetDrawSilhouette(enabled);
        aspect.SetDrawEdges(false);
        if(enabled) {
            aspect.SetEdgeColor(color);
            aspect.SetEdgeWidth(width);
        }
        return true;
    }
    return {};
}
}
void CadViewport::refreshActivePart(bool redraw) {
    if(!activePartId_.isEmpty() && !partIndexForId(activePartId_)) {
        activePartId_.clear();emit activePartChanged();
    }
    if(context_.IsNull())return;
    const Quantity_Color green(167.0/255,212.0/255,0.0,Quantity_TOC_sRGB);
    const Quantity_Color cyan(0.0,1.0,1.0,Quantity_TOC_sRGB);
    for(auto& part:boxes_) if(!part.presentation.IsNull()) {
        const bool active=part.active && part.persistentId==activePartId_;
        // Part silhouettes use the native renderer; local Face contours are
        // generated below. Edge owners retain OCCT highlighting.
        const bool selected=part.active && context_->IsSelected(part.presentation->GlobalSelOwner());
        const auto detected=Handle(StdSelect_BRepOwner)::DownCast(context_->DetectedOwner());
        const bool hovered=!detected.IsNull()&&detected->Selectable()==part.presentation
            &&!detected->Shape().IsNull()&&detected->Shape().ShapeType()!=TopAbs_FACE&&detected->Shape().ShapeType()!=TopAbs_EDGE;
        const bool outlined=active || selected || hovered;
        const auto color=hovered?cyan:selected?Quantity_Color(135./255,206./255,250./255,Quantity_TOC_sRGB):green;
        const double outlineWidth=(hovered?4.:selected?2.:2.8)*devicePixelRatioF();
        auto drawer=part.presentation->Attributes();

        drawer->SetupOwnShadingAspect();
        auto changed=activeSilhouette(*drawer->ShadingAspect()->Aspect(),outlined,color,outlineWidth);
        // Generic face boundaries include seams/tangent joins. Feature edges
        // are classified geometrically by PartPresentation instead.
        const bool boundaryChanged=drawer->FaceBoundaryDraw();
        if(boundaryChanged)drawer->SetFaceBoundaryDraw(false);
        auto* presentation=dynamic_cast<PartPresentation*>(part.presentation.get());
        bool faceChanged=false;
        if(presentation && !view_.IsNull()) {
            std::vector<PartPresentation::FaceContour> faces;
            for(context_->InitSelected();context_->MoreSelected();context_->NextSelected()) {
                const auto owner=Handle(StdSelect_BRepOwner)::DownCast(context_->SelectedOwner());
                if(!owner.IsNull() && owner->Selectable()==part.presentation && owner->Shape().ShapeType()==TopAbs_FACE
                    && owner!=detected)
                    faces.push_back({TopoDS::Face(owner->Shape()),Quantity_Color(135./255,206./255,250./255,Quantity_TOC_sRGB),2.*devicePixelRatioF()});
            }
            if(!detected.IsNull() && detected->Selectable()==part.presentation && detected->Shape().ShapeType()==TopAbs_FACE)
                faces.push_back({TopoDS::Face(detected->Shape()),cyan,4.*devicePixelRatioF()});
            const auto camera=view_->Camera();
            const auto inverse=part.presentation->Transformation().Inverted();
            const gp_Ax2 axes(camera->Center().Transformed(inverse),camera->Direction().Reversed().Transformed(inverse),camera->SideRight().Transformed(inverse));
            const HLRAlgo_Projector projector=camera->IsOrthographic()?HLRAlgo_Projector(axes):HLRAlgo_Projector(axes,camera->Distance());
            faceChanged=presentation->setFaceContours(faces,projector);
            std::vector<TopoDS_Face> fillets;
            if(outlined && part.blends.certified.IsSame(part.shape))
                for(const auto& feature:part.blends.features)if(feature.kind==EdgeBlendFeatures::Kind::Fillet)
                    for(const auto& face:feature.faces) {
                        if(std::none_of(fillets.begin(),fillets.end(),[&](const auto& f){return f.IsSame(face);}))fillets.push_back(face);
                    }
            presentation->setFilletFaces(fillets,false);

        }
        const bool featuresChanged=presentation && presentation->setFeatureHighlight(outlined,color,outlineWidth);
        if(outlined && !changed.has_value())
            emit statusMessage(tr("Native part silhouettes are unavailable in this OCCT build; only sharp feature edges are highlighted."));
        // Update only shaded rendering. Redisplay also rebuilds selection
        // presentations and can revive stale highlights during history cleanup.
        if(part.active && (boundaryChanged || featuresChanged || faceChanged)) {
            part.presentation->SetToUpdate(AIS_Shaded);
            context_->MainPrsMgr()->Update(part.presentation,AIS_Shaded);
        } else if(part.active && changed.value_or(false)) {
            part.presentation->SynchronizeAspects();
        }
    }
    if(redraw && !view_.IsNull())view_->Redraw();
}
std::optional<size_t> CadViewport::detectedPart(const QPoint& position) {
    if(context_.IsNull())return {};
    context_->MoveTo(position.x(),position.y(),view_,Standard_True);
    if(context_->HasDetected()) for(size_t i=0;i<boxes_.size();++i)
        if(isPartVisible(i) && boxes_[i].presentation==context_->DetectedInteractive())return i;
    return {};
}
QString CadViewport::solidTargetError(size_t index) const {
    if(!isPartVisible(index))return tr("Target Part is hidden or no longer exists.");
    if(!TopExp_Explorer(boxes_[index].shape,TopAbs_SOLID).More())return tr("Target Part must contain a solid.");
    return {};
}
void CadViewport::beginTargetPicking(TargetEligibility eligible,std::function<void(const QString&)> accepted) {
    initializeViewer();cancelTargetPicking();targetEligibility_=std::move(eligible);targetAccepted_=std::move(accepted);
    for(auto& part:boxes_)if(part.active && part.visible){
        context_->Deactivate(part.presentation);context_->Activate(part.presentation,AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
    }
    emit statusMessage(tr("Select target part"));
}
void CadViewport::cancelTargetPicking() {
    if(!targetPicking())return;
    targetAccepted_={};targetEligibility_={};refreshSketchPicking(true);
    context_->ClearDetected(Standard_False);
    if(!view_.IsNull())view_->Redraw();
    emit statusMessage(tr("Target picking ended. Use Pick target part to resume."));
}

std::optional<size_t> CadViewport::initialTargetPart(const TargetEligibility& eligible,QString& error) const {
    error.clear();const auto index=partIndexForId(activePartId_);
    if(!index)return {};
    if(!isPartVisible(*index))error=tr("Active Part is hidden.");
    else if(eligible)error=eligible(*index);
    return error.isEmpty()?index:std::nullopt;
}
