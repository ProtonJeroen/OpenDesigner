#include "CadViewport.h"
#include "MoveFaceGeometry.h"
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLocale>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <Prs3d_Drawer.hxx>
#include <Standard_Failure.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <TopoDS.hxx>
#include <Quantity_Color.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <cmath>
#include <algorithm>

void CadViewport::moveFace() {
    endSketchEditing();
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
void CadViewport::redrawFacePreview() {
    // Replacing a retained scene presentation must invalidate the main render
    // buffer, not just the immediate (manipulator/highlight) layer.
    context_->UpdateCurrentViewer();
    view_->Invalidate();
    view_->Redraw();
}
Handle(AIS_Shape) CadViewport::createFacePreview(const CadPart& before,const TopoDS_Shape& candidate,QString& error) {
    Handle(AIS_Shape) preview;
    try {
        // Reconstructed B-Reps are copied without display meshes. Complete
        // the shaded representation before erasing the original object.
        BRepMesh_IncrementalMesh mesh(candidate,0.1,false,0.35,false);
        bool meshed=mesh.IsDone();
        for(TopExp_Explorer f(candidate,TopAbs_FACE);f.More()&&meshed;f.Next()) {
            TopLoc_Location location;
            const auto triangulation=BRep_Tool::Triangulation(TopoDS::Face(f.Current()),location);
            meshed=!triangulation.IsNull() && triangulation->NbTriangles()>0;
        }
        if(!meshed) throw Standard_Failure("The reconstructed solid could not be tessellated for preview.");
        preview=new AIS_Shape(candidate);
        preview->SetLocalTransformation(before.orientation);
        preview->Attributes()->SetAutoTriangulation(true);
        preview->SetColor(Quantity_Color(0.15,0.65,1.0,Quantity_TOC_RGB));
        preview->SetTransparency(0.25);
        preview->SetDisplayMode(AIS_Shaded);
        // Select mode -1: previews never participate in operand picking.
        context_->Display(preview,AIS_Shaded,-1,Standard_False);
        context_->Redisplay(preview,Standard_False);
        return preview;
    } catch(const Standard_Failure& failure) {
        error=tr("Cannot display the reconstructed preview: %1").arg(QString::fromUtf8(failure.GetMessageString()));
        if(!preview.IsNull()) context_->Remove(preview,Standard_False);
        return {};
    }
}
bool CadViewport::presentFacePreview(const CadPart& before,bool changed,QString& error) {
    auto& session=*commandSession_;
    if(!session.preview.IsNull()) {
        context_->Remove(session.preview,Standard_False);
        session.preview.Nullify();
    }
    if(session.valid && changed) {
        session.preview=createFacePreview(before,session.candidate,error);
        session.valid=!session.preview.IsNull();
        if(session.valid) context_->Erase(before.presentation,Standard_False);
    }
    if(!session.valid || !changed) {
        context_->Display(before.presentation,Standard_False);
        context_->Redisplay(before.presentation,Standard_False);
    }
    if(!session.valid) {
        session.candidate.Nullify();
        session.changes.front().faceChange.reset();
    }
    commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(session.valid);
    return session.valid;
}
bool CadViewport::previewMoveFace(double value) {
    auto& session=*commandSession_;
    session.value=value;
    session.geometryError.clear();
    const auto& before=*session.faceBefore;
    QString error;
    const double localDistance=value/std::abs(before.orientation.ScaleFactor());
    const auto candidate=MoveFaceGeometry::move(before.shape,session.face,localDistance,error);
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
        displayPullArrow(moveFaceArrow_,session.pivot.Translated(gp_Vec(session.direction)*value),gp_Vec(session.direction));
    } else {
        displayPullArrow(moveFaceArrow_,session.pivot,gp_Vec(session.direction));
    }
    presentFacePreview(before,std::abs(value)>=1.e-7,error);
    session.state=session.valid ? CommandSession::State::Preview : CommandSession::State::Parameters;
    if (!session.valid) reportGeometryFailure(tr("Move Face"),error,false);
    redrawFacePreview();
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
    part.persistentId=state.persistentId; part.name=state.name; part.color=state.color;
    part.visible=state.visible;
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
