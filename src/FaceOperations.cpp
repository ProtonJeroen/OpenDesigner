#include "CadViewport.h"
#include "FaceTransformGeometry.h"
#include "MoveFaceGeometry.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QLocale>
#include <AIS_Shape.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepGProp.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>
#include <gp_Vec.hxx>
#include <ElCLib.hxx>
#include <Quantity_Color.hxx>
#include <cmath>
#include <algorithm>

namespace {
constexpr double facePi=3.14159265358979323846;
}
bool CadViewport::startRotateFace(size_t index,const TopoDS_Face& face) {
    if(!isPartVisible(index)) return false;
    gp_Pnt center; gp_Dir normal; QString error;
    if(!MoveFaceGeometry::reference(boxes_[index].shape,face,center,normal,error)) {
        emit statusMessage(tr("Rotate Face refused: select one supported planar face."));
        return false;
    }
    cancelCommandSession(); resetModelingTools();
    setSelectionMode(SelectionMode::Face);
    beginCommandSession(CommandSession::Kind::RotateFace,{index},normal.Transformed(boxes_[index].orientation),
                        center.Transformed(boxes_[index].orientation));
    auto& session=*commandSession_;
    session.face=face;
    session.faceBefore=std::make_shared<CadPart>(boxes_[index]);
    session.state=CommandSession::State::Selection;
    session.valid=false;
    commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(false);
    dragDimension_->setText(QLocale().toString(0.0,'f',2));
    faceAxisPicking_=true;
    faceAxisSelector_->setCurrentIndex(0);
    chooseFaceRotationAxis(0);
    updateFaceRotationManipulator();
    showCommandControls(previous_);
    emit statusMessage(tr("Rotate Face: choose a straight edge or world reference axis, then enter degrees or drag the orange ring."));
    return true;
}

bool CadViewport::startAlignFace(size_t index,const TopoDS_Face& face) {
    if(!isPartVisible(index)) return false;
    gp_Pnt center; gp_Dir normal; QString error;
    if(!MoveFaceGeometry::reference(boxes_[index].shape,face,center,normal,error)) {
        emit statusMessage(tr("Align Face refused: select one supported planar target face."));
        return false;
    }
    cancelCommandSession(); resetModelingTools();
    setSelectionMode(SelectionMode::Face);
    beginCommandSession(CommandSession::Kind::AlignFace,{index},normal.Transformed(boxes_[index].orientation),
                        center.Transformed(boxes_[index].orientation));
    auto& session=*commandSession_;
    session.face=face;
    session.faceBefore=std::make_shared<CadPart>(boxes_[index]);
    session.state=CommandSession::State::Selection;
    session.valid=false;
    session.coplanar=true;
    faceAlignSelector_->setCurrentIndex(0);
    commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(false);
    alignFacePickingReference_=true;
    emit statusMessage(tr("Align Face: select a planar reference face. Target: %1; reference remains unchanged.").arg(boxes_[index].name));
    return true;
}

void CadViewport::rotateFace() {
    cancelCommandSession();
    const auto selection=selectionContext();
    if(selection.kind!=SelectionContext::Kind::Faces || selection.faces.size()!=1) {
        emit statusMessage(tr("Rotate Face: select one planar face first, then choose Rotate Face.")); return;
    }
    startRotateFace(selection.faces.front().partIndex,selection.faces.front().face);
}

void CadViewport::alignFace() {
    cancelCommandSession();
    const auto selection=selectionContext();
    if(selection.kind!=SelectionContext::Kind::Faces || selection.faces.size()!=1) {
        emit statusMessage(tr("Align Face: select one planar target face first, then choose Align Face.")); return;
    }
    startAlignFace(selection.faces.front().partIndex,selection.faces.front().face);
}

void CadViewport::chooseFaceRotationAxis(int choice) {
    if(!commandSession_ || commandSession_->kind!=CommandSession::Kind::RotateFace) return;
    if(choice==0) {
        if(commandSession_->valid) previewFaceTransform(0.0);
        faceAxisPicking_=true;
        commandSession_->valid=false;
        commandSession_->state=CommandSession::State::Selection;
        commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(false);
        if(!context_.IsNull()) for(size_t i=0;i<boxes_.size();++i) if(boxes_[i].active && boxes_[i].visible) {
            context_->Deactivate(boxes_[i].presentation);
            context_->Activate(boxes_[i].presentation,AIS_Shape::SelectionMode(TopAbs_EDGE),Standard_False);
        }
        emit statusMessage(tr("Select a straight edge belonging to the target face to define the rotation axis."));
        return;
    }
    faceAxisPicking_=false;
    const auto& part=*commandSession_->faceBefore;
    gp_Pnt center; gp_Dir normal; QString error;
    if(!MoveFaceGeometry::reference(part.shape,commandSession_->face,center,normal,error)) return;
    const gp_Dir world(choice==1?1:0,choice==2?1:0,choice==3?1:0);
    gp_Vec projected(world);
    projected.Transform(part.orientation.Inverted());
    const double component=gp_Vec(normal).Dot(projected);
    projected-=gp_Vec(normal)*component;
    if(projected.Magnitude()<1.e-7) {
        emit statusMessage(tr("That reference axis is normal to this face. Choose another axis."));
        return;
    }
    commandSession_->faceAxis=gp_Ax1(center,gp_Dir(projected));
    commandSession_->valid=true;
    commandSession_->state=CommandSession::State::Parameters;
    commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(true);
    for(size_t i=0;i<boxes_.size();++i) if(boxes_[i].active && boxes_[i].visible) {
        context_->Deactivate(boxes_[i].presentation);
        context_->Activate(boxes_[i].presentation,AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
    }
    updateFaceRotationManipulator();
    previewFaceTransform(commandSession_->value);
}

bool CadViewport::pickFaceRotationAxis(const QPoint& point) {
    if(!faceAxisPicking_ || !commandSession_ || context_.IsNull()) return false;
    const size_t targetIndex=commandSession_->changes.front().partIndex;
    for(size_t i=0;i<boxes_.size();++i) if(boxes_[i].active && boxes_[i].visible) {
        context_->Deactivate(boxes_[i].presentation);
        context_->Activate(boxes_[i].presentation,AIS_Shape::SelectionMode(TopAbs_EDGE),Standard_False);
    }
    context_->MoveTo(point.x(),point.y(),view_,Standard_True);
    if(!context_->HasDetected()) return true;
    const auto owner=Handle(StdSelect_BRepOwner)::DownCast(context_->DetectedOwner());
    const auto object=context_->DetectedInteractive();
    if(owner.IsNull() || owner->Shape().IsNull() || owner->Shape().ShapeType()!=TopAbs_EDGE
       || object!=boxes_[targetIndex].presentation) {
        emit statusMessage(tr("Choose a straight edge on the target face.")); return true;
    }
    const auto edge=TopoDS::Edge(owner->Shape());
    bool belongs=false;
    for(TopExp_Explorer it(commandSession_->face,TopAbs_EDGE);it.More();it.Next()) if(it.Current().IsSame(edge)) belongs=true;
    if(!belongs) { emit statusMessage(tr("The rotation axis edge must border the selected face.")); return true; }
    const BRepAdaptor_Curve curve(edge);
    if(curve.GetType()!=GeomAbs_Line) { emit statusMessage(tr("Rotate Face currently requires a straight edge axis.")); return true; }
    commandSession_->faceAxis=curve.Line().Position();
    faceAxisPicking_=false;
    faceAxisSelector_->blockSignals(true); faceAxisSelector_->setCurrentIndex(0); faceAxisSelector_->blockSignals(false);
    commandSession_->valid=true;
    commandSession_->state=CommandSession::State::Parameters;
    commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(true);
    for(size_t i=0;i<boxes_.size();++i) if(boxes_[i].active && boxes_[i].visible) {
        context_->Deactivate(boxes_[i].presentation);
        context_->Activate(boxes_[i].presentation,AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
    }
    updateFaceRotationManipulator();
    previewFaceTransform(commandSession_->value);
    return true;
}

bool CadViewport::pickAlignReference(const QPoint& point) {
    if(!alignFacePickingReference_ || !commandSession_ || context_.IsNull()) return false;
    for(size_t i=0;i<boxes_.size();++i) if(boxes_[i].active && boxes_[i].visible) {
        context_->Deactivate(boxes_[i].presentation);
        context_->Activate(boxes_[i].presentation,AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
    }
    context_->MoveTo(point.x(),point.y(),view_,Standard_True);
    if(!context_->HasDetected()) return true;
    const auto owner=Handle(StdSelect_BRepOwner)::DownCast(context_->DetectedOwner());
    const auto object=context_->DetectedInteractive();
    if(owner.IsNull() || owner->Shape().IsNull() || owner->Shape().ShapeType()!=TopAbs_FACE) {
        emit statusMessage(tr("Align Face reference must be a planar face.")); return true;
    }
    size_t referenceIndex=boxes_.size();
    for(size_t i=0;i<boxes_.size();++i) if(boxes_[i].presentation==object) referenceIndex=i;
    if(referenceIndex==boxes_.size() || !isPartVisible(referenceIndex)) return true;
    const auto referenceFace=TopoDS::Face(owner->Shape());
    if(BRepAdaptor_Surface(referenceFace).GetType()!=GeomAbs_Plane) {
        emit statusMessage(tr("Align Face supports planar reference faces only.")); return true;
    }
    auto& session=*commandSession_;
    const auto& targetPart=*session.faceBefore;
    gp_Pln worldPlane=BRepAdaptor_Surface(referenceFace).Plane();
    worldPlane.Transform(boxes_[referenceIndex].orientation);
    worldPlane.Transform(targetPart.orientation.Inverted());
    session.referencePlane=worldPlane;
    session.referencePart=referenceIndex;
    session.referenceFace=referenceFace;
    session.hasReference=true;
    faceAlignReferenceLabel_->setText(tr("Reference: %1 — planar Face").arg(boxes_[referenceIndex].name));
    session.coplanar=faceAlignSelector_->currentIndex()==0;
    alignFacePickingReference_=false;
    commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(true);
    const bool valid=previewFaceTransform(0.0);
    alignFacePickingReference_=!valid;
    if(!valid) commandSession_->state=CommandSession::State::Selection;
    emit statusMessage(valid
        ? tr("Align Face preview ready. Target: %1; Reference: %2.").arg(boxes_[session.changes.front().partIndex].name,boxes_[referenceIndex].name)
        : tr("Align Face could not make a valid preview; choose another reference or Cancel."));
    return true;
}

bool CadViewport::previewFaceTransform(double value) {
    if(!commandSession_ || (commandSession_->kind!=CommandSession::Kind::RotateFace
                            && commandSession_->kind!=CommandSession::Kind::AlignFace)) return false;
    auto& session=*commandSession_;
    session.value=value;
    session.geometryError.clear();
    if(session.kind==CommandSession::Kind::RotateFace && faceAxisPicking_) {
        emit statusMessage(tr("Choose a valid rotation axis before previewing.")); return false;
    }
    if(session.kind==CommandSession::Kind::AlignFace && !session.hasReference) {
        emit statusMessage(tr("Select a planar reference face before previewing.")); return false;
    }
    const auto& before=*session.faceBefore;
    QString error;
    TopoDS_Shape candidate;
    if(session.kind==CommandSession::Kind::RotateFace)
        candidate=FaceTransformGeometry::rotate(before.shape,session.face,session.faceAxis,value,error);
    else candidate=FaceTransformGeometry::align(before.shape,session.face,session.referencePlane,session.coplanar,error);
    bool valid=!candidate.IsNull();
    if(session.kind==CommandSession::Kind::RotateFace) session.changed=valid && std::abs(value)>1.e-8;
    else {
        BRepAdaptor_Surface targetSurface(session.face);
        const auto targetPlane=targetSurface.Plane();
        const auto targetNormal=targetPlane.Axis().Direction();
        const auto referenceNormal=session.referencePlane.Axis().Direction();
        const double angle=std::acos(std::clamp(std::abs(targetNormal.Dot(referenceNormal)),0.0,1.0));
        const double gap=session.referencePlane.Distance(targetPlane.Location());
        session.changed=valid && (angle>1.e-7 || (session.coplanar && gap>1.e-6));
    }
    auto& change=session.changes.front();
    change.faceChange.reset();
    session.candidate=candidate;
    session.valid=valid;
    commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(valid);
    if(valid && session.changed) {
        CadPart after=before;
        after.shape=candidate;
        after.geometryKind=CadPart::GeometryKind::GeneralBRep;
        after.cylinderPrimitive=after.spherePrimitive=after.conePrimitive=false;
        Bnd_Box bounds;
        BRepBndLib::AddOptimal(candidate,bounds,false,false);
        bounds.Get(after.min[0],after.min[1],after.min[2],after.max[0],after.max[1],after.max[2]);
        after.baseMin=after.min; after.baseMax=after.max;
        change.afterMin=after.min; change.afterMax=after.max;
        change.faceChange=std::make_shared<FaceChange>(FaceChange{before,after});
    } else if(valid) session.candidate=before.shape;
    valid=presentFacePreview(before,session.changed,error);
    session.state=valid?CommandSession::State::Preview:CommandSession::State::Parameters;
    if(!valid) reportGeometryFailure(session.kind==CommandSession::Kind::RotateFace?tr("Rotate Face"):tr("Align Face"),error,false);
    redrawFacePreview();
    return valid;
}

void CadViewport::updateFaceRotationManipulator() {
    if(!commandSession_ || commandSession_->kind!=CommandSession::Kind::RotateFace || context_.IsNull()) return;
    if(!faceRotationManipulator_.IsNull()) { context_->Remove(faceRotationManipulator_,Standard_False); faceRotationManipulator_.Nullify(); }
    if(!commandSession_->valid) return;
    const auto& part=*commandSession_->faceBefore;
    const gp_Pnt center=commandSession_->faceAxis.Location().Transformed(part.orientation);
    faceRotationPivotWorld_=center;
    faceRotationAxisWorld_=commandSession_->faceAxis.Direction().Transformed(part.orientation);
    faceRotationRadius_=std::max(18.0,0.38*std::min({part.max[0]-part.min[0],part.max[1]-part.min[1],part.max[2]-part.min[2]}));
    gp_Dir x(1,0,0);
    if(std::abs(gp_Vec(x).Dot(gp_Vec(faceRotationAxisWorld_)))>0.9) x=gp_Dir(0,1,0);
    const gp_Circ circle(gp_Ax2(center,faceRotationAxisWorld_,x),faceRotationRadius_);
    BRepBuilderAPI_MakeEdge edge(circle);
    faceRotationManipulator_=new AIS_Shape(edge.Edge());
    faceRotationManipulator_->SetColor(Quantity_NOC_ORANGE);
    faceRotationManipulator_->SetWidth(3.0);
    context_->Display(faceRotationManipulator_,Standard_False);
    context_->Deactivate(faceRotationManipulator_);
    context_->SetZLayer(faceRotationManipulator_,Graphic3d_ZLayerId_Topmost);
    view_->Redraw();
}

double CadViewport::faceRotationCursorAngle(const QPoint& point) const {
    gp_Dir x(1,0,0);
    if(std::abs(gp_Vec(x).Dot(gp_Vec(faceRotationAxisWorld_)))>0.9) x=gp_Dir(0,1,0);
    const gp_Circ circle(gp_Ax2(faceRotationPivotWorld_,faceRotationAxisWorld_,x),faceRotationRadius_);
    double best=1.e100,angle=0;
    for(int i=0;i<360;++i) {
        const double a=i*2.0*facePi/360.0;
        const auto p=ElCLib::Value(a,circle);
        Standard_Integer sx,sy; view_->Convert(p.X(),p.Y(),p.Z(),sx,sy);
        const double d=std::hypot(point.x()-sx,point.y()-sy);
        if(d<best) { best=d; angle=a; }
    }
    return angle;
}

bool CadViewport::beginFaceRotationDrag(const QPoint& point) {
    if(!commandSession_ || commandSession_->kind!=CommandSession::Kind::RotateFace
       || !commandSession_->valid || faceRotationManipulator_.IsNull() || view_.IsNull()) return false;
    gp_Dir x(1,0,0);
    if(std::abs(gp_Vec(x).Dot(gp_Vec(faceRotationAxisWorld_)))>0.9) x=gp_Dir(0,1,0);
    const gp_Circ circle(gp_Ax2(faceRotationPivotWorld_,faceRotationAxisWorld_,x),faceRotationRadius_);
    double best=15.0;
    for(int i=0;i<=128;++i) {
        const auto p=ElCLib::Value(i*2.0*facePi/128.0,circle);
        Standard_Integer sx,sy; view_->Convert(p.X(),p.Y(),p.Z(),sx,sy);
        best=std::min(best,std::hypot(point.x()-sx,point.y()-sy));
    }
    if(best>15.0) return false;
    faceRotationPreviousAngle_=faceRotationCursorAngle(point);
    faceRotationAccumulatedAngle_=commandSession_->value*facePi/180.0;
    draggingFaceRotation_=true;
    return true;
}
