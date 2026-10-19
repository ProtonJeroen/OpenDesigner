#include "CadViewport.h"
#include "MoveFaceGeometry.h"
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLocale>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <TopoDS.hxx>
#include <Quantity_Color.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <cmath>
#include <algorithm>

void CadViewport::moveFace() {
    cancelCommandSession();
    setSelectionMode(SelectionMode::Face);
    if (context_.IsNull()) return;
    movePartMode_=directMoveMode_=rotateMode_=false;
    emit transformModesChanged(false,false,false);
    hideFaceArrow(); hideMoveAxes(); hideRotationRings();
    moveFacePicking_=true;
    if (context_->NbSelected()==1) {
        context_->InitSelected();
        const auto owner=Handle(StdSelect_BRepOwner)::DownCast(context_->SelectedOwner());
        if (!owner.IsNull() && !owner->Shape().IsNull() && owner->Shape().ShapeType()==TopAbs_FACE) {
            const auto selected=context_->SelectedInteractive();
            for (size_t i=0;i<boxes_.size();++i)
                if (boxes_[i].presentation==selected) { startMoveFace(i,TopoDS::Face(owner->Shape())); return; }
        }
    }
    emit statusMessage(tr("Move Face: select one planar face; move its normal arrow or enter an offset in mm."));
}
bool CadViewport::startMoveFace(size_t index, const TopoDS_Face& face) {
    if (!isPartVisible(index)) return false;
    const auto& part=boxes_[index];
    gp_Pnt center; gp_Dir normal; QString error;
    if (!MoveFaceGeometry::reference(part.shape,face,center,normal,error)) {
        emit statusMessage(error); resetModelingTools(); return false;
    }
    const gp_Pnt worldCenter=center.Transformed(part.orientation);
    const gp_Dir worldNormal=normal.Transformed(part.orientation);
    hideFaceArrow(); hideMoveAxes(); hideRotationRings();
    beginCommandSession(CommandSession::Kind::MoveFace,{index},worldNormal,worldCenter);
    commandSession_->face=face;
    commandSession_->faceBefore=std::make_shared<CadPart>(part);
    commandPanel_->setWindowTitle(tr("Move Face Preview"));
    dragDimension_->setToolTip(tr("Signed face offset in mm; positive is outward"));
    commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(true);
    displayPullArrow(moveFaceArrow_,worldCenter,gp_Vec(worldNormal));
    context_->SetZLayer(moveFaceArrow_,Graphic3d_ZLayerId_Topmost);
    context_->MoveTo(-1,-1,view_,Standard_False);
    view_->Redraw();
    commandPanel_->activateWindow();
    dragDimension_->setFocus(); dragDimension_->selectAll();
    emit statusMessage(tr("Move Face: drag the normal arrow or type signed mm. OK/Enter applies; Cancel/Esc discards."));
    return true;
}
bool CadViewport::previewMoveFace(double value) {
    auto& session=*commandSession_;
    session.value=value;
    const auto& before=*session.faceBefore;
    QString error;
    const double localDistance=value/std::abs(before.orientation.ScaleFactor());
    const auto candidate=MoveFaceGeometry::move(before.shape,session.face,localDistance,error);
    if (!session.preview.IsNull()) { context_->Remove(session.preview,Standard_False); session.preview.Nullify(); }
    session.valid=!candidate.IsNull();
    session.candidate=candidate;
    commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(session.valid);
    auto& change=session.changes.front();
    change.faceChange.reset();
    if (session.valid && std::abs(value)>=1.e-7) {
        CadPart after=before;
        after.shape=candidate;
        // Directly edited topology must not subsequently be rebuilt by Primitive Pull.
        after.geometryKind=CadPart::GeometryKind::GeneralBRep;
        after.cylinderPrimitive=after.spherePrimitive=after.conePrimitive=false;
        Bnd_Box bounds;
        BRepBndLib::AddOptimal(candidate,bounds,false,false);
        bounds.Get(after.min[0],after.min[1],after.min[2],after.max[0],after.max[1],after.max[2]);
        after.baseMin=after.min; after.baseMax=after.max;
        change.afterMin=after.min; change.afterMax=after.max;
        change.faceChange=std::make_shared<FaceChange>(FaceChange{before,after});
        session.preview=new AIS_Shape(candidate);
        session.preview->SetLocalTransformation(before.orientation);
        session.preview->SetColor(Quantity_Color(before.color.redF(),before.color.greenF(),before.color.blueF(),Quantity_TOC_RGB));
        session.preview->SetTransparency(0.25);
        context_->Erase(before.presentation,Standard_False);
        context_->Display(session.preview,Standard_False);
        context_->SetDisplayMode(session.preview,AIS_Shaded,Standard_False);
        context_->Deactivate(session.preview);
        displayPullArrow(moveFaceArrow_,session.pivot.Translated(gp_Vec(session.direction)*value),gp_Vec(session.direction));
    } else {
        context_->Display(before.presentation,Standard_False);
        displayPullArrow(moveFaceArrow_,session.pivot,gp_Vec(session.direction));
    }
    session.state=session.valid ? CommandSession::State::Preview : CommandSession::State::Parameters;
    if (!session.valid) emit statusMessage(error);
    view_->Redraw();
    return session.valid;
}
bool CadViewport::beginMoveFaceDrag(const QPoint& point) {
    if (!commandSession_ || commandSession_->kind!=CommandSession::Kind::MoveFace) return false;
    const auto& session=*commandSession_;
    const gp_Pnt origin=session.pivot.Translated(gp_Vec(session.direction)*(session.valid?session.value:0));
    const auto tip=origin.Translated(gp_Vec(session.direction)*31);
    Standard_Integer x0,y0,x1,y1;
    view_->Convert(origin.X(),origin.Y(),origin.Z(),x0,y0);
    view_->Convert(tip.X(),tip.Y(),tip.Z(),x1,y1);
    const double dx=x1-x0,dy=y1-y0,len2=dx*dx+dy*dy;
    if (len2<4) return false;
    const double t=std::clamp(((point.x()-x0)*dx+(point.y()-y0)*dy)/len2,0.0,1.0);
    if (std::hypot(point.x()-x0-t*dx,point.y()-y0-t*dy)>12) return false;
    draggingFace_=true; dragStart_=point;
    moveFaceDragStart_=session.valid?session.value:0;
    dragScreenX_=dx/std::sqrt(len2); dragScreenY_=dy/std::sqrt(len2);
    dragMmPerPixel_=31/std::sqrt(len2);
    return true;
}
void CadViewport::applyFaceState(size_t index, const CadPart& state) {
    auto& part=boxes_[index];
    context_->ClearSelected(Standard_False); // Owners from replaced topology must not be reused.
    part.shape=state.shape; part.geometryKind=state.geometryKind;
    part.min=state.min; part.max=state.max; part.baseMin=state.baseMin; part.baseMax=state.baseMax;
    part.orientation=state.orientation;
    part.cylinderPrimitive=state.cylinderPrimitive; part.spherePrimitive=state.spherePrimitive;
    part.conePrimitive=state.conePrimitive; part.cone=state.cone;
    part.sphereRadius=state.sphereRadius; part.cylinderRadius=state.cylinderRadius;
    part.cylinderHeight=state.cylinderHeight; part.cylinderBaseOffset=state.cylinderBaseOffset;
    part.presentation->SetShape(part.shape);
    part.presentation->SetLocalTransformation(part.orientation);
    context_->Redisplay(part.presentation,Standard_False);
    context_->Deactivate(part.presentation);
    if (part.visible) context_->Activate(part.presentation,AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
}
