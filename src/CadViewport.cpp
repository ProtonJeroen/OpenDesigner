#include "CadViewport.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPaintEngine>
#include <QResizeEvent>
#include <Aspect_NeutralWindow.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <AIS_Shape.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <gp_Pnt.hxx>
#include <Quantity_Color.hxx>
#include <Graphic3d_NameOfMaterial.hxx>
#include <Standard_Failure.hxx>

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
    TopoDS_Shape shape = BRepPrimAPI_MakeBox(gp_Pnt(x, 0, 0), 100.0, 80.0, 60.0).Shape();
    Handle(AIS_Shape) object = new AIS_Shape(shape);
    context_->Display(object, Standard_False);
    context_->SetDisplayMode(object, AIS_Shaded, Standard_False);
    ++partCount_;
    emit partCreated(QString("Part %1").arg(partCount_));
    fitAll();
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
