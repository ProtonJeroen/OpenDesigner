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
#include <StdSelect_BRepOwner.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <gp_Circ.hxx>
#include <ElCLib.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax2.hxx>
#include <gp_Vec.hxx>
#include <gp_Ax1.hxx>
#include <TopLoc_Location.hxx>
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
            if (!undoStack_.empty()) undoStack_.back().afterOrientation = boxes_[rotationIndex_].orientation;
            rotationEditing_ = false;
            dragDimension_->hide();
            updateRotationRings();
            setFocus();
            return;
        }
        if (!dimensionEditing_) return;
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
    if (part.orientation.Form() != gp_Identity) return;
    gp_Pnt center(
        (part.min[0] + part.max[0]) * 0.5,
        (part.min[1] + part.max[1]) * 0.5,
        (part.min[2] + part.max[2]) * 0.5);
    center.SetCoord(arrowAxis_ + 1,
                    arrowMax_ ? part.max[arrowAxis_] : part.min[arrowAxis_]);
    gp_Vec direction(0, 0, 0);
    direction.SetCoord(arrowAxis_ + 1, arrowMax_ ? 1.0 : -1.0);
    const double shaftLength = 22.0;
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
    context_->Activate(part.presentation, AIS_Shape::SelectionMode(TopAbs_FACE), Standard_False);
    boxes_.push_back(part);
    ++partCount_;
    emit partCreated(QString("Part %1").arg(partCount_));
    fitAll();
}
void CadViewport::pullSelectedFace() {
    if (context_.IsNull() || context_->NbSelected() != 1) {
        emit statusMessage(tr("Select exactly one box face first."));
        return;
    }
    // OCCT SelectedOwner(), SelectedShape() and SelectedInteractive() refer to
    // the CURRENT selection iterator. Initialize it before reading any value.
    context_->InitSelected();
    if (!context_->MoreSelected()) {
        emit statusMessage(tr("Selection iterator is empty."));
        return;
    }
    // The selection owner holds the picked B-Rep subshape.
    Handle(StdSelect_BRepOwner) faceOwner =
        Handle(StdSelect_BRepOwner)::DownCast(context_->SelectedOwner());
    if (faceOwner.IsNull()) {
        emit statusMessage(tr("No B-Rep face owner selected (check face selection mode)."));
        return;
    }
    const TopoDS_Shape faceShape = faceOwner->Shape();
    if (faceShape.IsNull() || faceShape.ShapeType() != TopAbs_FACE) {
        emit statusMessage(tr("Selected shape type: %1 (expected FACE = %2).")
            .arg(static_cast<int>(faceShape.IsNull() ? TopAbs_SHAPE : faceShape.ShapeType()))
            .arg(static_cast<int>(TopAbs_FACE)));
        return;
    }
    Handle(AIS_InteractiveObject) selectedObject = context_->SelectedInteractive();
    auto partIt = boxes_.end();
    for (auto it = boxes_.begin(); it != boxes_.end(); ++it) {
        if (it->presentation == selectedObject) { partIt = it; break; }
    }
    if (partIt == boxes_.end()) {
        emit statusMessage(tr("This face is not part of an editable box."));
        return;
    }
    if (partIt->orientation.Form() != gp_Identity) {
        emit statusMessage(tr("Pull Face on rotated parts is not supported yet; undo rotation first."));
        return;
    }
    const BRepAdaptor_Surface surface(TopoDS::Face(faceShape));
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
        emit statusMessage(tr("Only axis-aligned box faces are supported."));
        return;
    }
    const double position = surface.Plane().Location().Coord(axis + 1);
    const bool onMax = std::abs(position - partIt->max[axis]) < 0.001;
    const bool onMin = std::abs(position - partIt->min[axis]) < 0.001;
    if (!onMax && !onMin) {
        emit statusMessage(tr("Could not match selected face to box boundary."));
        return;
    }
    bool ok = false;
    const double distance = QInputDialog::getDouble(this, tr("Pull Face"),
        tr("Outward distance (mm; negative = inward):"), 20.0, -100000.0, 100000.0, 2, &ok);
    if (!ok || distance == 0.0) return;
    auto nextMin = partIt->min;
    auto nextMax = partIt->max;
    if (onMax) nextMax[axis] += distance;
    else nextMin[axis] -= distance;
    if (nextMax[axis] - nextMin[axis] <= 0.001) {
        emit statusMessage(tr("Pull distance would collapse or invert the box."));
        return;
    }
    const size_t index = static_cast<size_t>(partIt - boxes_.begin());
    undoStack_.push_back({index, partIt->min, partIt->max, nextMin, nextMax, partIt->orientation, partIt->orientation});
    redoStack_.clear();
    applyBoxBounds(index, nextMin, nextMax);
    emit statusMessage(tr("Face pulled by %1 mm").arg(distance));
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
    if (boxes_[index].orientation.Form() != gp_Identity) {
        emit statusMessage(tr("Face-based editing on rotated parts is not supported yet."));
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
    context_->ClearSelected(Standard_False);
    part.presentation->SetShape(shape);
    part.presentation->SetLocalTransformation(part.orientation);
    context_->Redisplay(part.presentation, Standard_False);
    context_->Deactivate(part.presentation);
    context_->Activate(part.presentation, AIS_Shape::SelectionMode(TopAbs_FACE), Standard_False);
    part.min = min;
    part.max = max;
    updateFaceArrow();
    if (rotateMode_ && !draggingRotation_) updateRotationRings();
    view_->Redraw();
}
void CadViewport::undoPull() {
    if (undoStack_.empty()) {
        emit statusMessage(tr("Nothing to undo."));
        return;
    }
    const PullChange change = undoStack_.back();
    undoStack_.pop_back();
    applyPartState(change.partIndex, change.beforeMin, change.beforeMax, change.beforeOrientation);
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
    applyPartState(change.partIndex, change.afterMin, change.afterMax, change.afterOrientation);
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
    draggingFace_ = true;
    emit statusMessage(dragMovesPart_
        ? tr("Drag arrow to move the entire part.")
        : tr("Drag arrow along face normal; release to commit."));
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
    const gp_Pnt center = gp_Pnt((part.min[0]+part.max[0])*0.5,
        (part.min[1]+part.max[1])*0.5, (part.min[2]+part.max[2])*0.5).Transformed(part.orientation);
    const double radius = 0.65 * std::max({part.max[0]-part.min[0],
        part.max[1]-part.min[1], part.max[2]-part.min[2]});
    for (int axis = 0; axis < 3; ++axis) {
        const gp_Dir dir = gp_Dir(axis == 0 ? 1 : 0, axis == 1 ? 1 : 0, axis == 2 ? 1 : 0).Transformed(part.orientation);
        rotationRings_[axis] = new AIS_Shape(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(center,dir),radius)).Shape());
        rotationRings_[axis]->SetColor(axis == 0 ? Quantity_NOC_RED
            : axis == 1 ? Quantity_NOC_GREEN : Quantity_NOC_BLUE1);
        context_->Display(rotationRings_[axis], Standard_False);
        context_->Deactivate(rotationRings_[axis]);
    }
    view_->Redraw();
}
bool CadViewport::beginRotationDrag(const QPoint& point) {
    if (!rotateMode_ || rotationIndex_ >= boxes_.size() || view_.IsNull()) return false;
    const auto& part = boxes_[rotationIndex_];
    const gp_Pnt center = gp_Pnt((part.min[0]+part.max[0])*0.5,
        (part.min[1]+part.max[1])*0.5, (part.min[2]+part.max[2])*0.5).Transformed(part.orientation);
    const double radius = 0.65 * std::max({part.max[0]-part.min[0],
        part.max[1]-part.min[1], part.max[2]-part.min[2]});
    double best = 12.0;
    int axisHit = -1;
    for (int axis = 0; axis < 3; ++axis) {
        const gp_Dir dir = gp_Dir(axis == 0 ? 1 : 0, axis == 1 ? 1 : 0, axis == 2 ? 1 : 0).Transformed(part.orientation);
        const gp_Circ circle(gp_Ax2(center,dir),radius);
        for (int i = 0; i < 96; ++i) {
            const gp_Pnt world = ElCLib::Value(i * 2.0 * 3.14159265358979323846 / 96.0, circle);
            Standard_Integer x,y;
            view_->Convert(world.X(),world.Y(),world.Z(),x,y);
            const double distance = std::hypot(point.x()-x,point.y()-y);
            if (distance < best) { best = distance; axisHit = axis; }
        }
    }
    if (axisHit < 0) return false;
    rotationAxis_ = axisHit;
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
    const gp_Pnt center = gp_Pnt((part.min[0]+part.max[0])*0.5,
        (part.min[1]+part.max[1])*0.5, (part.min[2]+part.max[2])*0.5).Transformed(orientation);
    const double radius = 0.65 * std::max({part.max[0]-part.min[0],
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
    if (!enabled) hideRotationRings();
    else if (!context_.IsNull() && context_->NbSelected() == 1) {
        context_->InitSelected();
        const auto selected = context_->SelectedInteractive();
        for (size_t i=0; i<boxes_.size(); ++i)
            if (boxes_[i].presentation == selected) {
                rotationIndex_ = i;
                updateRotationRings();
                break;
            }
    }
    emit statusMessage(enabled ? tr("Rotate mode: select a face and drag a colored ring.")
        : tr("Rotate mode disabled."));
}
void CadViewport::setAngleSnap(double degrees) { rotationSnap_ = degrees; }

void CadViewport::mousePressEvent(QMouseEvent* e) {
    previous_ = e->pos();
    rotating_ = e->button() == Qt::MiddleButton && !(e->modifiers() & Qt::ShiftModifier);
    panning_ = e->button() == Qt::MiddleButton && (e->modifiers() & Qt::ShiftModifier);
    if (rotating_ && !view_.IsNull()) view_->StartRotation(previous_.x(), previous_.y());
    if (e->button() == Qt::LeftButton && !context_.IsNull()) {
        if (dimensionEditing_ || rotationEditing_) {
            dimensionEditing_ = false;
            rotationEditing_ = false;
            dragDimension_->hide();
        }
        if (beginRotationDrag(e->pos())) return;
        if (beginArrowDrag(e->pos())) return;
        if ((e->modifiers() & Qt::ControlModifier) && beginFaceDrag(e->pos())) return;
        context_->MoveTo(e->position().x(), e->position().y(), view_, Standard_True);
        context_->SelectDetected();
        hideFaceArrow();
        if (rotateMode_) {
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
        if (context_->NbSelected() == 1) {
            context_->InitSelected();
            Handle(StdSelect_BRepOwner) arrowOwner =
                Handle(StdSelect_BRepOwner)::DownCast(context_->SelectedOwner());
            if (!arrowOwner.IsNull() && !arrowOwner->Shape().IsNull()
                && arrowOwner->Shape().ShapeType() == TopAbs_FACE) {
                Handle(AIS_InteractiveObject) selected = context_->SelectedInteractive();
                for (size_t i = 0; i < boxes_.size(); ++i) {
                    if (boxes_[i].presentation != selected) continue;
                    if (boxes_[i].orientation.Form() != gp_Identity) break;
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
        const auto& part = boxes_[rotationIndex_];
        undoStack_.push_back({rotationIndex_,part.min,part.max,part.min,part.max,
                              rotationBefore_,part.orientation});
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
