#include "CadViewport.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPaintEngine>
#include <QResizeEvent>
#include <QInputDialog>
#include <QLineEdit>
#include <QDoubleValidator>
#include <QShortcut>
#include <QLocale>
#include <algorithm>
#include <Aspect_NeutralWindow.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <AIS_Shape.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
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
#include <QDialog>
#include <QFormLayout>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pln.hxx>
#include <Quantity_Color.hxx>
#include <cmath>
#include <algorithm>

CadViewport::CadViewport(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    dragDimension_ = new QLineEdit(this);
    dragDimension_->setFixedWidth(112);
    dragDimension_->setAlignment(Qt::AlignCenter);
    dragDimension_->setValidator(new QDoubleValidator(-100000.0, 100000.0, 2, dragDimension_));
    dragDimension_->hide();
    connect(dragDimension_, &QLineEdit::returnPressed, this, [this]() {
        if (rotationEditing_) {
            bool ok = false;
            const double angle = QLocale().toDouble(dragDimension_->text(), &ok);
            if (!ok) return;
            applyRotationAngle(angle);
            if (!undoStack_.empty()) {
                auto& change = undoStack_.back();
                if (groupRotationEditing_ && !change.groupedChanges.empty()) {
                    for (auto& item : change.groupedChanges)
                        item.afterOrientation = boxes_[item.partIndex].orientation;
                } else change.afterOrientation = boxes_[rotationIndex_].orientation;
            }
            groupRotationEditing_ = false;
            rotationEditing_ = false;
            dragDimension_->hide();
            updateRotationRings();
            setFocus();
            return;
        }
        if (!dimensionEditing_) return;
        if (multiFaceEditing_) {
            bool ok = false;
            const double distance = QLocale().toDouble(dragDimension_->text(), &ok);
            if (!ok || !applyMultiFaceDrag(distance)) {
                emit statusMessage(tr("Invalid distance: one or more boxes would collapse."));
                return;
            }
            if (!undoStack_.empty()) {
                for (auto& change : undoStack_.back().groupedChanges) {
                    change.afterMin = boxes_[change.partIndex].min;
                    change.afterMax = boxes_[change.partIndex].max;
                }
            }
            // The old OCCT face owners are invalid after rebuilding the B-Reps.
            // Finish the operation cleanly: require a fresh selection for another pull.
            clearMultiFaceHighlight();
            hideFaceArrow();
            context_->ClearSelected(Standard_False);
            multiFaceParts_.clear();
            multiFaceEditing_ = false;
            dimensionEditing_ = false;
            dragDimension_->hide();
            view_->Redraw();
            setFocus();
            return;
        }
        if (groupMoveEditing_) {
            bool ok = false;
            const double distance = QLocale().toDouble(dragDimension_->text(), &ok);
            if (!ok) return;
            applyGroupMove(distance);
            if (!undoStack_.empty()) {
                auto& change = undoStack_.back();
                for (auto& item : change.groupedChanges) {
                    const auto& part = boxes_[item.partIndex];
                    item.afterMin = part.min;
                    item.afterMax = part.max;
                }
            }
            groupMoveEditing_ = false;
            dimensionEditing_ = false;
            dragDimension_->hide();
            setFocus();
            return;
        }
        bool ok = false;
        const double distance = QLocale().toDouble(dragDimension_->text(), &ok);
        if (!ok) { emit statusMessage(tr("Enter a valid distance in mm.")); return; }
        if (undoStack_.empty()) { dragDimension_->hide(); dimensionEditing_ = false; return; }
        auto& last = undoStack_.back();
        if (last.partIndex != dragIndex_) { dragDimension_->hide(); dimensionEditing_ = false; return; }
        auto min = last.beforeMin;
        auto max = last.beforeMax;
        if (dragMovesPart_) {
            const double delta = (dragMax_ ? 1.0 : -1.0) * distance;
            min[dragAxis_] += delta;
            max[dragAxis_] += delta;
        } else if (dragMax_) max[dragAxis_] += distance;
        else min[dragAxis_] -= distance;
        if (max[dragAxis_] - min[dragAxis_] <= 0.001) {
            emit statusMessage(tr("Distance would collapse or invert the box."));
            return;
        }
        applyBoxBounds(last.partIndex, min, max);
        last.afterMin = min;
        last.afterMax = max;
        dragDimension_->hide();
        dimensionEditing_ = false;
        setFocus();
        emit statusMessage(tr("Exact face distance: %1 mm").arg(distance));
    });
    auto* cancelDimension = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(cancelDimension, &QShortcut::activated, this, [this]() {
        dragDimension_->hide();
        dimensionEditing_ = false;
        rotationEditing_ = false;
        groupRotationEditing_ = false;
        groupMoveEditing_ = false;
        if (multiFaceEditing_) {
            clearMultiFaceHighlight();
            hideFaceArrow();
            if (!context_.IsNull()) context_->ClearSelected(Standard_False);
            multiFaceParts_.clear();
            if (!view_.IsNull()) view_->Redraw();
        }
        multiFaceEditing_ = false;
        setFocus();
    });
}
QPaintEngine* CadViewport::paintEngine() const { return nullptr; }
void CadViewport::initializeViewer() {
    if (!view_.IsNull()) return;
    display_ = new Aspect_DisplayConnection();
    Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(display_);
    viewer_ = new V3d_Viewer(driver);
    viewer_->SetDefaultLights();
    viewer_->SetLightOn();
    context_ = new AIS_InteractiveContext(viewer_);
    // Edge-only highlights: keep shaded faces at their original color.
    // Use OCCT's existing initialized drawers to avoid null shading aspects.
    const Quantity_Color cyan(0.0, 0.95, 1.0, Quantity_TOC_RGB);
    const Handle(Prs3d_Drawer) selectionStyle = context_->SelectionStyle();
    if (!selectionStyle.IsNull()) {
        selectionStyle->SetColor(cyan);
        selectionStyle->SetDisplayMode(AIS_WireFrame);
    }
    // OCCT uses a separate local-selection highlight drawer for faces.
    // Configuring only SelectionStyle() changes whole-object selection,
    // but leaves selected subshapes with the default highlight color.
    const Handle(Prs3d_Drawer) faceSelectionStyle =
        context_->HighlightStyle(Prs3d_TypeOfHighlight_LocalSelected);
    if (!faceSelectionStyle.IsNull()) {
        faceSelectionStyle->SetColor(cyan);
        // Outline only: leave the actual face material untouched.
        faceSelectionStyle->SetDisplayMode(AIS_WireFrame);
    }
    const Quantity_Color hoverEdges(0.85, 0.85, 0.85, Quantity_TOC_RGB);
    const Handle(Prs3d_Drawer) dynamicStyle =
        context_->HighlightStyle(Prs3d_TypeOfHighlight_Dynamic);
    if (!dynamicStyle.IsNull()) {
        dynamicStyle->SetColor(hoverEdges);
        dynamicStyle->SetDisplayMode(AIS_WireFrame);
    }
    const Handle(Prs3d_Drawer) localDynamicStyle =
        context_->HighlightStyle(Prs3d_TypeOfHighlight_LocalDynamic);
    if (!localDynamicStyle.IsNull()) {
        localDynamicStyle->SetColor(hoverEdges);
        localDynamicStyle->SetDisplayMode(AIS_WireFrame);
    }
    view_ = viewer_->CreateView();
    Handle(Aspect_NeutralWindow) window = new Aspect_NeutralWindow();
    window->SetNativeHandle(static_cast<Aspect_Drawable>(winId()));
    window->SetSize(width(), height());
    view_->SetWindow(window);
    if (!window->IsMapped()) window->Map();
    view_->SetBackgroundColor(Quantity_NOC_GRAY20);
    view_->MustBeResized();
    view_->SetProj(V3d_XposYnegZpos);
    view_->FitAll();
}
void CadViewport::paintEvent(QPaintEvent*) {
    initializeViewer();
    if (!view_.IsNull()) view_->Redraw();
}
void CadViewport::resizeEvent(QResizeEvent*) {
    if (!view_.IsNull()) {
        Handle(Aspect_NeutralWindow) window = Handle(Aspect_NeutralWindow)::DownCast(view_->Window());
        if (!window.IsNull()) window->SetSize(width(), height());
        view_->MustBeResized();
    }
}
void CadViewport::hideFaceArrow() {
    arrowVisible_ = false;
    if (!faceArrow_.IsNull() && !context_.IsNull()) {
        context_->Remove(faceArrow_, Standard_False);
        faceArrow_.Nullify();
    }
}
void CadViewport::updateFaceArrow() {
    if (!arrowVisible_ || arrowPartIndex_ >= boxes_.size() || context_.IsNull()) return;
    const auto& part = boxes_[arrowPartIndex_];
    gp_Pnt center(
        (part.min[0] + part.max[0]) * 0.5,
        (part.min[1] + part.max[1]) * 0.5,
        (part.min[2] + part.max[2]) * 0.5);
    center.SetCoord(arrowAxis_ + 1,
                    arrowMax_ ? part.max[arrowAxis_] : part.min[arrowAxis_]);
    gp_Vec direction(0, 0, 0);
    direction.SetCoord(arrowAxis_ + 1, arrowMax_ ? 1.0 : -1.0);
    const double shaftLength = 22.0;
    center.Transform(part.orientation);
    direction.Transform(part.orientation);
    const gp_Ax2 shaftAxis(center, gp_Dir(direction));
    const gp_Pnt tipBase = center.Translated(direction.Multiplied(shaftLength));
    const gp_Ax2 tipAxis(tipBase, gp_Dir(direction));
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    builder.Add(compound, BRepPrimAPI_MakeCylinder(shaftAxis, 1.6, shaftLength).Shape());
    builder.Add(compound, BRepPrimAPI_MakeCone(tipAxis, 4.5, 0.0, 9.0).Shape());
    if (faceArrow_.IsNull()) {
        faceArrow_ = new AIS_Shape(compound);
        faceArrow_->SetColor(Quantity_NOC_ORANGE);
        context_->Display(faceArrow_, Standard_False);
        context_->SetDisplayMode(faceArrow_, AIS_Shaded, Standard_False);
        context_->Deactivate(faceArrow_);
    } else {
        faceArrow_->SetShape(compound);
        context_->Redisplay(faceArrow_, Standard_False);
        context_->Deactivate(faceArrow_);
    }
}
void CadViewport::addBox() {
    initializeViewer();
    const double x = partCount_ * 125.0;
    BoxPart part;
    part.min = {x, 0.0, 0.0};
    part.max = {x + 100.0, 80.0, 60.0};
    TopoDS_Shape shape = BRepPrimAPI_MakeBox(gp_Pnt(x, 0, 0), 100.0, 80.0, 60.0).Shape();
    part.presentation = new AIS_Shape(shape);
    part.presentation->SetColor(Quantity_NOC_LIGHTGRAY);
    context_->Display(part.presentation, Standard_False);
    context_->SetDisplayMode(part.presentation, AIS_Shaded, Standard_False);
    // Disable global object selection (mode 0) before enabling subshape mode.
    context_->Deactivate(part.presentation);
    context_->Activate(part.presentation, AIS_Shape::SelectionMode(TopAbs_FACE),
        Standard_False);
    boxes_.push_back(part);
    ++partCount_;
    emit partCreated(QString("Part %1").arg(partCount_));
    fitAll();
}
void CadViewport::pullSelectedFace() {
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
        if (index == boxes_.size()) {
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
                emit statusMessage(tr("Pull would collapse or invert a selected box. No changes applied."));
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
}
void CadViewport::rotateSelectedPart() {
    if (context_.IsNull() || context_->NbSelected() != 1) {
        emit statusMessage(tr("Select a face on the part to rotate."));
        return;
    }
    context_->InitSelected();
    if (!context_->MoreSelected()) return;
    const Handle(AIS_InteractiveObject) selected = context_->SelectedInteractive();
    size_t index = 0;
    while (index < boxes_.size() && boxes_[index].presentation != selected) ++index;
    if (index == boxes_.size()) {
        emit statusMessage(tr("Select a face of an editable box."));
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
    auto& part = boxes_[index];
    const gp_Trsf before = part.orientation;
    const gp_Pnt center((part.min[0] + part.max[0]) * 0.5,
                        (part.min[1] + part.max[1]) * 0.5,
                        (part.min[2] + part.max[2]) * 0.5);
    const gp_Dir direction = axisBox.currentIndex() == 0 ? gp_Dir(1, 0, 0)
                           : axisBox.currentIndex() == 1 ? gp_Dir(0, 1, 0)
                                                          : gp_Dir(0, 0, 1);
    gp_Trsf rotation;
    const gp_Pnt worldCenter = center.Transformed(before);
    rotation.SetRotation(gp_Ax1(worldCenter, direction), angleBox.value() * 3.14159265358979323846 / 180.0);
    const gp_Trsf after = rotation * before;
    undoStack_.push_back({index, part.min, part.max, part.min, part.max, before, after});
    redoStack_.clear();
    hideFaceArrow();
    applyPartState(index, part.min, part.max, after);
    emit statusMessage(tr("Part rotated %1 degrees around world %2 axis.")
        .arg(angleBox.value()).arg(axisBox.currentText()));
}
void CadViewport::movePartAlongSelectedFace() {
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
    if (index == boxes_.size()) {
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
    applyBoxBounds(index, afterMin, afterMax);
    emit statusMessage(tr("Part translated %1 mm; dimensions unchanged.").arg(distance));
}
void CadViewport::applyPartState(size_t index, const std::array<double, 3>& min,
                                 const std::array<double, 3>& max, const gp_Trsf& orientation) {
    if (index >= boxes_.size()) return;
    boxes_[index].orientation = orientation;
    applyBoxBounds(index, min, max);
}
void CadViewport::applyBoxBounds(size_t index, const std::array<double, 3>& min,
                                 const std::array<double, 3>& max) {
    if (index >= boxes_.size() || context_.IsNull()) return;
    auto& part = boxes_[index];
    TopoDS_Shape shape = BRepPrimAPI_MakeBox(
        gp_Pnt(min[0], min[1], min[2]),
        max[0] - min[0], max[1] - min[1], max[2] - min[2]).Shape();
    if (!batchingPartUpdates_) context_->ClearSelected(Standard_False);
    part.presentation->SetShape(shape);
    part.presentation->SetLocalTransformation(part.orientation);
    context_->Redisplay(part.presentation, Standard_False);
    context_->Deactivate(part.presentation);
    context_->Activate(part.presentation,
        (directMoveMode_ || rotateMode_) ? 0 : AIS_Shape::SelectionMode(TopAbs_FACE),
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
void CadViewport::undoPull() {
    if (undoStack_.empty()) {
        emit statusMessage(tr("Nothing to undo."));
        return;
    }
    const PullChange change = undoStack_.back();
    undoStack_.pop_back();
    if (!change.groupedChanges.empty()) {
        batchingPartUpdates_ = true;
        for (const auto& item : change.groupedChanges)
            applyPartState(item.partIndex, item.beforeMin, item.beforeMax, item.beforeOrientation);
        batchingPartUpdates_ = false;
        refreshPartSelection();
    } else applyPartState(change.partIndex, change.beforeMin, change.beforeMax, change.beforeOrientation);
    redoStack_.push_back(change);
    emit statusMessage(tr("Pull Face undone."));
}
void CadViewport::redoPull() {
    if (redoStack_.empty()) {
        emit statusMessage(tr("Nothing to redo."));
        return;
    }
    const PullChange change = redoStack_.back();
    redoStack_.pop_back();
    if (!change.groupedChanges.empty()) {
        batchingPartUpdates_ = true;
        for (const auto& item : change.groupedChanges)
            applyPartState(item.partIndex, item.afterMin, item.afterMax, item.afterOrientation);
        batchingPartUpdates_ = false;
        refreshPartSelection();
    } else applyPartState(change.partIndex, change.afterMin, change.afterMax, change.afterOrientation);
    undoStack_.push_back(change);
    emit statusMessage(tr("Pull Face redone."));
}
void CadViewport::fitAll() {
    if (!view_.IsNull()) { view_->FitAll(0.01, Standard_False); view_->ZFitAll(); view_->Redraw(); }
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
    if (multiFaceDragging_) updateMultiFaceHighlight();
    draggingFace_ = true;
    emit statusMessage(dragMovesPart_
        ? tr("Drag to move the entire part along the face normal.")
        : tr("Drag along the face normal; release to commit."));
    return true;
}
bool CadViewport::beginArrowDrag(const QPoint& origin) {
    if (!arrowVisible_ || faceArrow_.IsNull() || view_.IsNull()
        || arrowPartIndex_ >= boxes_.size()) return false;
    const auto& part = boxes_[arrowPartIndex_];
    gp_Pnt center(
        (part.min[0] + part.max[0]) * 0.5,
        (part.min[1] + part.max[1]) * 0.5,
        (part.min[2] + part.max[2]) * 0.5);
    center.SetCoord(arrowAxis_ + 1,
                    arrowMax_ ? part.max[arrowAxis_] : part.min[arrowAxis_]);
    gp_Pnt tip = center;
    tip.SetCoord(arrowAxis_ + 1, center.Coord(arrowAxis_ + 1)
                    + (arrowMax_ ? 31.0 : -31.0));
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
    if (multiFaceDragging_) {
        // Preserve face identities in multiFaceParts_ before clearing OCCT owners.
        // The old selection highlight otherwise remains at its original position.
        context_->ClearSelected(Standard_False);
        updateMultiFaceHighlight();
        view_->Redraw();
    }
    draggingFace_ = true;
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
        if (index == boxes_.size()) { multiFaceParts_.clear(); return false; }
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
        multiFaceHighlight_->SetColor(Quantity_Color(0.0, 0.95, 1.0, Quantity_TOC_RGB));
        context_->Display(multiFaceHighlight_, Standard_False);
        context_->SetDisplayMode(multiFaceHighlight_, AIS_WireFrame, Standard_False);
        context_->Deactivate(multiFaceHighlight_);
    } else {
        multiFaceHighlight_->SetShape(edges);
        context_->Redisplay(multiFaceHighlight_, Standard_False);
        context_->Deactivate(multiFaceHighlight_);
    }
    context_->SetZLayer(multiFaceHighlight_, Graphic3d_ZLayerId_Topmost);
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

void CadViewport::showDragDimension(const QPoint& position) {
    if (!dragDimension_) return;
    dragDimension_->setText(QLocale().toString(dragDistance_, 'f', 2));
    dragDimension_->move(
        std::clamp(position.x() + 16, 0, std::max(0, width() - dragDimension_->width())),
        std::clamp(position.y() - 36, 0, std::max(0, height() - dragDimension_->height())));
    dragDimension_->show();
    dragDimension_->raise();
}

void CadViewport::hideMoveAxes() {
    if (context_.IsNull()) return;
    for (auto& axis : moveAxes_) {
        if (!axis.IsNull()) { context_->Remove(axis, Standard_False); axis.Nullify(); }
    }
}
gp_Pnt CadViewport::selectionCenter() const {
    if (selectedPartIndices_.empty()) return gp_Pnt(0, 0, 0);
    double x = 0, y = 0, z = 0;
    size_t count = 0;
    for (const size_t index : selectedPartIndices_) {
        if (index >= boxes_.size()) continue;
        const auto& part = boxes_[index];
        const gp_Pnt p = gp_Pnt((part.min[0]+part.max[0])*0.5,
            (part.min[1]+part.max[1])*0.5,
            (part.min[2]+part.max[2])*0.5).Transformed(part.orientation);
        x += p.X(); y += p.Y(); z += p.Z(); ++count;
    }
    return count ? gp_Pnt(x/count, y/count, z/count) : gp_Pnt(0, 0, 0);
}
void CadViewport::setPartVisible(size_t index, bool visible) {
    if (index >= boxes_.size()) return;
    auto& part = boxes_[index];
    if (part.visible == visible) return;
    part.visible = visible;
    if (!context_.IsNull()) {
        if (visible) {
            context_->Display(part.presentation, Standard_False);
            context_->Deactivate(part.presentation);
            context_->Activate(part.presentation,
                (directMoveMode_ || rotateMode_) ? 0 : AIS_Shape::SelectionMode(TopAbs_FACE),
                Standard_False);
        } else {
            context_->Erase(part.presentation, Standard_False);
        }
    }
    std::vector<size_t> remaining;
    for (const auto selected : selectedPartIndices_)
        if (selected < boxes_.size() && boxes_[selected].visible)
            remaining.push_back(selected);
    selectParts(remaining);
}
bool CadViewport::isPartVisible(size_t index) const {
    return index < boxes_.size() && boxes_[index].visible;
}
void CadViewport::fitSelection(const std::vector<size_t>& indices) {
    if (view_.IsNull()) return;
    Bnd_Box bounds;
    for (const auto index : indices) {
        if (index >= boxes_.size() || !boxes_[index].visible) continue;
        const auto& part = boxes_[index];
        BRepBndLib::Add(part.presentation->Shape().Moved(TopLoc_Location(part.orientation)), bounds);
    }
    if (bounds.IsVoid()) return;
    view_->FitAll(bounds, 0.1, Standard_False);
    view_->ZFitAll();
    view_->Redraw();
}
void CadViewport::selectParts(const std::vector<size_t>& indices) {
    selectedPartIndices_.clear();
    for (const size_t index : indices) {
        if (index < boxes_.size() && boxes_[index].visible &&
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
void CadViewport::applyGroupMove(double distance) {
    const gp_Vec worldDelta(groupMoveDirection_);
    batchingPartUpdates_ = true;
    for (const auto& before : groupMoveBefore_) {
        if (before.partIndex >= boxes_.size()) continue;
        const gp_Vec localDelta = worldDelta.Transformed(
            before.beforeOrientation.Inverted()).Multiplied(distance);
        auto min = before.beforeMin;
        auto max = before.beforeMax;
        for (int axis = 0; axis < 3; ++axis) {
            min[axis] += localDelta.Coord(axis + 1);
            max[axis] += localDelta.Coord(axis + 1);
        }
        applyPartState(before.partIndex, min, max, before.beforeOrientation);
    }
    batchingPartUpdates_ = false;
    refreshPartSelection();
}
void CadViewport::updateMoveAxes() {
    hideMoveAxes();
    if (!directMoveMode_ || moveIndex_ >= boxes_.size() || context_.IsNull()) return;
    const auto& part = boxes_[moveIndex_];
    const gp_Pnt center = selectedPartIndices_.size() > 1 ? selectionCenter() : gp_Pnt((part.min[0]+part.max[0])*0.5,
        (part.min[1]+part.max[1])*0.5, (part.min[2]+part.max[2])*0.5).Transformed(part.orientation);
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
    const int mode = AIS_Shape::SelectionMode(TopAbs_FACE);
    for (auto& part : boxes_) {
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
    directMoveMode_ = enabled;
    setTransformSelectionMode();
    if (enabled) hideFaceArrow();
    emit statusMessage(enabled
        ? tr("Move: select parts with Alt+click (Ctrl+Alt for groups), then drag an axis.")
        : tr("Move disabled. Selection preserved."));
}
bool CadViewport::beginMoveAxisDrag(const QPoint& point) {
    if (!directMoveMode_ || moveIndex_>=boxes_.size() || view_.IsNull()) return false;
    const auto& part=boxes_[moveIndex_];
    const gp_Pnt center = selectedPartIndices_.size() > 1 ? selectionCenter()
        : gp_Pnt((part.min[0]+part.max[0])*0.5,
            (part.min[1]+part.max[1])*0.5,(part.min[2]+part.max[2])*0.5).Transformed(part.orientation);
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
    groupMoveDragging_ = selectedPartIndices_.size() > 1;
    groupMoveBefore_.clear();
    if (groupMoveDragging_) {
        groupMoveDirection_ = gp_Dir(hit==0?1:0, hit==1?1:0, hit==2?1:0)
            .Transformed(part.orientation);
        for (const size_t index : selectedPartIndices_) {
            const auto& item = boxes_[index];
            groupMoveBefore_.push_back({index, item.min, item.max,
                item.min, item.max, item.orientation, item.orientation});
        }
    }
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
    if (!rotateMode_ || rotationIndex_ >= boxes_.size() || context_.IsNull()) return;
    const auto& part = boxes_[rotationIndex_];
    const gp_Pnt center = selectedPartIndices_.size() > 1 ? selectionCenter()
        : gp_Pnt((part.min[0]+part.max[0])*0.5,
            (part.min[1]+part.max[1])*0.5,
            (part.min[2]+part.max[2])*0.5).Transformed(part.orientation);
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
    if (!rotateMode_ || rotationIndex_ >= boxes_.size() || view_.IsNull()) return false;
    const auto& part = boxes_[rotationIndex_];
    const gp_Pnt center = selectedPartIndices_.size() > 1 ? selectionCenter()
        : gp_Pnt((part.min[0]+part.max[0])*0.5,
            (part.min[1]+part.max[1])*0.5, (part.min[2]+part.max[2])*0.5).Transformed(part.orientation);
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
    groupRotationBefore_.clear();
    if (groupRotationDragging_) {
        groupRotationCenter_ = center;
        groupRotationDirection_ = gp_Dir(axisHit==0?1:0,axisHit==1?1:0,axisHit==2?1:0).Transformed(part.orientation);
        for (const size_t index : selectedPartIndices_) {
            const auto& item = boxes_[index];
            groupRotationBefore_.push_back({index,item.min,item.max,item.min,item.max,item.orientation,item.orientation});
        }
    }
    rotationBefore_ = part.orientation;
    rotationAngle_ = 0.0;
    dragStart_ = point;
    rotationStartAngle_ = rotationCursorAngle(point, rotationAxis_, rotationBefore_);
    rotationPreviousAngle_ = rotationStartAngle_;
    rotationAccumulatedAngle_ = 0.0;
    draggingRotation_ = true;
    hideFaceArrow();
    return true;
}
double CadViewport::rotationCursorAngle(const QPoint& point, int axis, const gp_Trsf& orientation) const {
    const auto& part = boxes_[rotationIndex_];
    const gp_Pnt center = groupRotationDragging_ ? groupRotationCenter_
        : gp_Pnt((part.min[0]+part.max[0])*0.5,
            (part.min[1]+part.max[1])*0.5, (part.min[2]+part.max[2])*0.5).Transformed(orientation);
    const double radius = groupRotationDragging_ ? 60.0 : 0.65 * std::max({part.max[0]-part.min[0],
        part.max[1]-part.min[1], part.max[2]-part.min[2]});
    const gp_Dir dir = gp_Dir(axis == 0 ? 1 : 0, axis == 1 ? 1 : 0, axis == 2 ? 1 : 0).Transformed(orientation);
    const gp_Circ circle(gp_Ax2(center,dir),radius);
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
    if (groupRotationDragging_ || groupRotationEditing_) {
        gp_Trsf rotation;
        rotation.SetRotation(gp_Ax1(groupRotationCenter_,groupRotationDirection_),
            degrees * 3.14159265358979323846 / 180.0);
        batchingPartUpdates_ = true;
        for (const auto& before : groupRotationBefore_) {
            applyPartState(before.partIndex, before.beforeMin, before.beforeMax,
                rotation * before.beforeOrientation);
        }
        batchingPartUpdates_ = false;
        rotationAngle_ = degrees;
        refreshPartSelection();
        return;
    }
    if (rotationIndex_ >= boxes_.size()) return;
    auto& part = boxes_[rotationIndex_];
    const gp_Pnt center = gp_Pnt((part.min[0]+part.max[0])*0.5,
        (part.min[1]+part.max[1])*0.5, (part.min[2]+part.max[2])*0.5).Transformed(rotationBefore_);
    const gp_Dir dir = gp_Dir(rotationAxis_ == 0 ? 1 : 0, rotationAxis_ == 1 ? 1 : 0, rotationAxis_ == 2 ? 1 : 0).Transformed(rotationBefore_);
    gp_Trsf rotation;
    rotation.SetRotation(gp_Ax1(center,dir),degrees * 3.14159265358979323846 / 180.0);
    applyPartState(rotationIndex_,part.min,part.max,rotation * rotationBefore_);
    rotationAngle_ = degrees;
}
void CadViewport::setRotateMode(bool enabled) {
    rotateMode_ = enabled;
    setTransformSelectionMode();
    if (enabled) hideFaceArrow();
    emit statusMessage(enabled
        ? tr("Rotate: select parts with Alt+click (Ctrl+Alt for groups), then drag a ring.")
        : tr("Rotate disabled. Selection preserved."));
}
void CadViewport::setAngleSnap(double degrees) { rotationSnap_ = degrees; }

void CadViewport::mousePressEvent(QMouseEvent* e) {
    previous_ = e->pos();
    rotating_ = e->button() == Qt::MiddleButton && !(e->modifiers() & Qt::ShiftModifier);
    panning_ = e->button() == Qt::MiddleButton && (e->modifiers() & Qt::ShiftModifier);
    if (rotating_ && !view_.IsNull()) view_->StartRotation(previous_.x(), previous_.y());
    if (e->button() == Qt::LeftButton && !context_.IsNull()) {
        if (dimensionEditing_ || rotationEditing_) {
            clearMultiFaceHighlight();
            multiFaceEditing_ = false;
            dimensionEditing_ = false;
            rotationEditing_ = false;
            dragDimension_->hide();
        }
        const bool partPicking = (e->modifiers() & Qt::AltModifier);
        const bool additiveSelection = (e->modifiers() & Qt::ControlModifier);
        // Start manipulator drags BEFORE changing OCCT selection modes:
        // Deactivate/Activate may discard selected subshape owners.
        if (!partPicking && !additiveSelection && beginRotationDrag(e->pos())) return;
        if (!partPicking && !additiveSelection && beginMoveAxisDrag(e->pos())) return;
        if (!partPicking && !additiveSelection && !directMoveMode_ && !rotateMode_
            && beginArrowDrag(e->pos())) return;
        // Only reconfigure picking when actually selecting geometry.
        for (auto& part : boxes_) {
            if (!part.visible) continue;
            context_->Deactivate(part.presentation);
            context_->Activate(part.presentation,
                partPicking ? 0 : AIS_Shape::SelectionMode(TopAbs_FACE), Standard_False);
        }
        // Ctrl is reserved for additive face selection, not drag-to-pull.
        context_->MoveTo(e->position().x(), e->position().y(), view_, Standard_True);
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
            const int count = static_cast<int>(selectedPartIndices_.size());
            emit statusMessage(count > 1
                ? tr("%1 parts selected. Drag an axis to move the group.").arg(count)
                : count == 1 ? tr("Part selected. Drag a transform arrow.")
                             : tr("Selection cleared."));
            view_->Redraw();
            return;
        }
        // Use OCCT's additive selection scheme for Ctrl+click on faces.
        // Highlight selected faces by cyan edges only, preserving their material.
        if (!context_->SelectionStyle().IsNull())
            context_->SelectionStyle()->SetDisplayMode(AIS_WireFrame);
        context_->SelectDetected(additiveSelection
            ? AIS_SelectionScheme_XOR : AIS_SelectionScheme_Replace);
        hideFaceArrow();
        if (rotateMode_ && !additiveSelection) {
            hideRotationRings();
            if (context_->NbSelected() == 1) {
                context_->InitSelected();
                const auto selected = context_->SelectedInteractive();
                for (size_t i=0; i<boxes_.size(); ++i)
                    if (boxes_[i].presentation == selected) {
                        rotationIndex_ = i;
                        updateRotationRings();
                        break;
                    }
            }
        }
        // Keep one manipulation arrow as an anchor for a multi-face selection.
        // The grouped drag operation will use all selected face owners.
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
        if (context_->NbSelected() == 1) {
            context_->InitSelected();
            const TopoDS_Shape picked = context_->SelectedShape();
            emit statusMessage(tr("Selected shape: %1; B-Rep owner: %2")
                .arg(picked.IsNull() ? -1 : static_cast<int>(picked.ShapeType()))
                .arg(Handle(StdSelect_BRepOwner)::DownCast(context_->SelectedOwner()).IsNull() ? tr("no") : tr("yes")));
        }
        view_->Redraw();
    }
}
void CadViewport::setMovePartMode(bool enabled) {
    movePartMode_ = enabled;
    emit statusMessage(enabled
        ? tr("Move Part mode: select a face, then drag its orange arrow.")
        : tr("Pull Face mode: select a face, then drag its orange arrow."));
}
void CadViewport::setSnapStep(double stepMm) {
    snapStepMm_ = stepMm;
    emit statusMessage(stepMm > 0.0
        ? tr("Face snapping: %1 mm (hold Shift to bypass).").arg(stepMm)
        : tr("Face snapping disabled."));
}
void CadViewport::mouseMoveEvent(QMouseEvent* e) {
    if (view_.IsNull()) return;
    QPoint p = e->pos();
    if (draggingRotation_) {
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
            dragDimension_->move(std::clamp(p.x()+16,0,std::max(0,width()-dragDimension_->width())),
                                 std::clamp(p.y()-36,0,std::max(0,height()-dragDimension_->height())));
            dragDimension_->show();
            emit statusMessage(tr("Rotation: %1 degrees").arg(degrees,0,'f',1));
        }
    } else if (draggingFace_) {
        const double rawDistance = ((p.x() - dragStart_.x()) * dragScreenX_
                                  + (p.y() - dragStart_.y()) * dragScreenY_) * dragMmPerPixel_;
        const bool snapping = snapStepMm_ > 0.0 && !(e->modifiers() & Qt::ShiftModifier);
        const double requested = snapping
            ? std::round(rawDistance / snapStepMm_) * snapStepMm_
            : rawDistance;
        if (multiFaceDragging_) {
            if (std::abs(requested - dragDistance_) > 0.0001 && applyMultiFaceDrag(requested)) {
                dragDistance_ = requested;
                showDragDimension(p);
                emit statusMessage(tr("Multi-Face Pull: %1 mm").arg(requested, 0, 'f', 1));
            }
            previous_ = p;
            return;
        }
        if (groupMoveDragging_) {
            if (std::abs(requested - dragDistance_) > 0.0001) {
                dragDistance_ = requested;
                applyGroupMove(requested);
                showDragDimension(p);
                emit statusMessage(tr("Group Move: %1 mm").arg(requested, 0, 'f', 1));
            }
            previous_ = p;
            return;
        }
        auto min = dragMin_;
        auto max = dragMaxBounds_;
        if (dragMovesPart_) {
            const double delta = (dragMax_ ? 1.0 : -1.0) * requested;
            min[dragAxis_] += delta;
            max[dragAxis_] += delta;
        } else if (dragMax_) max[dragAxis_] += requested;
        else min[dragAxis_] -= requested;
        if (max[dragAxis_] - min[dragAxis_] > 0.001
            && std::abs(requested - dragDistance_) > 0.0001) {
            dragDistance_ = requested;
            applyBoxBounds(dragIndex_, min, max);
            emit statusMessage(tr("%1: %2 mm")
                .arg(dragMovesPart_ ? tr("Move Part") : tr("Pull Face"))
                .arg(dragDistance_, 0, 'f', 1));
            showDragDimension(p);
        }
    } else if (rotating_) view_->Rotation(p.x(), p.y());
    else if (panning_) view_->Pan(p.x() - previous_.x(), previous_.y() - p.y());
    else if (!context_.IsNull()) context_->MoveTo(p.x(), p.y(), view_, Standard_True);
    previous_ = p;
}
void CadViewport::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) { rotating_ = false; panning_ = false; }
    if (e->button() == Qt::LeftButton && draggingRotation_) {
        draggingRotation_ = false;
        if (std::abs(rotationAngle_) < 0.0001) { dragDimension_->hide(); return; }
        if (groupRotationDragging_) {
            groupRotationDragging_ = false;
            PullChange group = groupRotationBefore_.front();
            group.groupedChanges = groupRotationBefore_;
            for (auto& item : group.groupedChanges)
                item.afterOrientation = boxes_[item.partIndex].orientation;
            undoStack_.push_back(group);
            groupRotationEditing_ = true;
        } else {
            const auto& part = boxes_[rotationIndex_];
            undoStack_.push_back({rotationIndex_,part.min,part.max,part.min,part.max,
                                  rotationBefore_,part.orientation});
        }
        redoStack_.clear();
        rotationEditing_ = true;
        updateRotationRings();
        dragDimension_->setText(QLocale().toString(rotationAngle_,'f',2));
        dragDimension_->setFocus();
        dragDimension_->selectAll();
        emit statusMessage(tr("Enter exact angle, or Esc to keep rotation."));
        return;
    }
    if (e->button() != Qt::LeftButton || !draggingFace_) return;
    draggingFace_ = false;
    if (multiFaceDragging_) {
        multiFaceDragging_ = false;
        // Keep the live overlay visible while the exact distance is being edited.
        if (std::abs(dragDistance_) < 0.0001) {
            clearMultiFaceHighlight();
            dragDimension_->hide();
            return;
        }
        PullChange operation = multiFaceParts_.front().before;
        operation.groupedChanges.clear();
        for (const auto& item : multiFaceParts_) {
            auto change = item.before;
            change.afterMin = boxes_[change.partIndex].min;
            change.afterMax = boxes_[change.partIndex].max;
            operation.groupedChanges.push_back(change);
        }
        undoStack_.push_back(operation);
        redoStack_.clear();
        multiFaceEditing_ = true;
        dimensionEditing_ = true;
        showDragDimension(e->pos());
        dragDimension_->setFocus();
        dragDimension_->selectAll();
        emit statusMessage(tr("Multi-Face Pull: enter exact mm or Esc to keep."));
        return;
    }
    if (groupMoveDragging_) {
        groupMoveDragging_ = false;
        if (std::abs(dragDistance_) < 0.0001) { dragDimension_->hide(); return; }
        PullChange group = groupMoveBefore_.front();
        group.groupedChanges = groupMoveBefore_;
        for (auto& item : group.groupedChanges) {
            item.afterMin = boxes_[item.partIndex].min;
            item.afterMax = boxes_[item.partIndex].max;
        }
        undoStack_.push_back(group);
        redoStack_.clear();
        groupMoveEditing_ = true;
        dimensionEditing_ = true;
        showDragDimension(e->pos());
        dragDimension_->setFocus();
        dragDimension_->selectAll();
        emit statusMessage(tr("Group moved. Enter exact mm or Esc to keep."));
        return;
    }
    if (std::abs(dragDistance_) < 0.0001) { dragDimension_->hide(); return; }
    const auto& part = boxes_[dragIndex_];
    undoStack_.push_back({dragIndex_, dragMin_, dragMaxBounds_, part.min, part.max, part.orientation, part.orientation});
    redoStack_.clear();
    dimensionEditing_ = true;
    showDragDimension(e->pos());
    dragDimension_->setFocus();
    dragDimension_->selectAll();
    emit statusMessage(tr("%1: %2 mm. Type exact mm and press Enter, or Esc to keep.")
        .arg(dragMovesPart_ ? tr("Move Part") : tr("Pull Face"))
        .arg(dragDistance_, 0, 'f', 1));
}
void CadViewport::wheelEvent(QWheelEvent* e) {
    if (view_.IsNull()) return;
    const double scale = e->angleDelta().y() > 0 ? 0.8 : 1.25;
    view_->SetScale(view_->Scale() * scale);
    view_->Redraw();
}
