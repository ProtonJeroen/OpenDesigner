#include "CadViewport.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPaintEngine>
#include <QResizeEvent>
#include <QInputDialog>
#include <Aspect_NeutralWindow.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <AIS_Shape.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pln.hxx>
#include <Quantity_Color.hxx>
#include <cmath>

CadViewport::CadViewport(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
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
    TopoDS_Shape updated = BRepPrimAPI_MakeBox(
        gp_Pnt(nextMin[0], nextMin[1], nextMin[2]),
        nextMax[0] - nextMin[0], nextMax[1] - nextMin[1], nextMax[2] - nextMin[2]).Shape();
    context_->ClearSelected(Standard_False);
    partIt->presentation->SetShape(updated);
    context_->Redisplay(partIt->presentation, Standard_False);
    context_->Deactivate(partIt->presentation);
    context_->Activate(partIt->presentation, AIS_Shape::SelectionMode(TopAbs_FACE), Standard_False);
    partIt->min = nextMin;
    partIt->max = nextMax;
    view_->Redraw();
    emit statusMessage(tr("Face pulled by %1 mm").arg(distance));
}
void CadViewport::fitAll() {
    if (!view_.IsNull()) { view_->FitAll(0.01, Standard_False); view_->ZFitAll(); view_->Redraw(); }
}
void CadViewport::mousePressEvent(QMouseEvent* e) {
    previous_ = e->pos();
    rotating_ = e->button() == Qt::MiddleButton && !(e->modifiers() & Qt::ShiftModifier);
    panning_ = e->button() == Qt::MiddleButton && (e->modifiers() & Qt::ShiftModifier);
    if (rotating_ && !view_.IsNull()) view_->StartRotation(previous_.x(), previous_.y());
    if (e->button() == Qt::LeftButton && !context_.IsNull()) {
        context_->MoveTo(e->position().x(), e->position().y(), view_, Standard_True);
        context_->SelectDetected();
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
void CadViewport::mouseMoveEvent(QMouseEvent* e) {
    if (view_.IsNull()) return;
    QPoint p = e->pos();
    if (rotating_) view_->Rotation(p.x(), p.y());
    else if (panning_) view_->Pan(p.x() - previous_.x(), previous_.y() - p.y());
    else if (!context_.IsNull()) context_->MoveTo(p.x(), p.y(), view_, Standard_True);
    previous_ = p;
}
void CadViewport::wheelEvent(QWheelEvent* e) {
    if (view_.IsNull()) return;
    const double scale = e->angleDelta().y() > 0 ? 0.8 : 1.25;
    view_->SetScale(view_->Scale() * scale);
    view_->Redraw();
}
