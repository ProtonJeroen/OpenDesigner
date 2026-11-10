#include "CadViewport.h"
#include "FilletGeometry.h"
#include "ChamferGeometry.h"
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLineEdit>
#include <QLocale>
#include <StdSelect_BRepOwner.hxx>
#include <TopoDS.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <cmath>

void CadViewport::fillet() {startEdgeBlend(false);}
void CadViewport::chamfer() {startEdgeBlend(true);}
void CadViewport::startEdgeBlend(bool chamfer) {
    endSketchEditing();cancelCommandSession();resetModelingTools();
    beginCommandSession(chamfer?CommandSession::Kind::Chamfer:CommandSession::Kind::Fillet,{},gp_Dir(1,0,0));
    commandSession_->valid=false;
    commandPanel_->setWindowTitle(chamfer?tr("Chamfer Preview"):tr("Fillet Preview"));
    dragDimension_->setToolTip(chamfer?tr("Equal distance in mm"):tr("Radius in mm"));
    dragDimension_->setText(QLocale().toString(1.0));
    commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(false);
    for(auto& part:boxes_) if(part.active && part.visible) {
        context_->Deactivate(part.presentation);
        context_->Activate(part.presentation,AIS_Shape::SelectionMode(TopAbs_EDGE),Standard_False);
    }
    emit statusMessage(tr("Select one solid edge, then enter a positive value in mm."));
}
bool CadViewport::pickEdgeBlend(const QPoint& point) {
    context_->MoveTo(point.x(),point.y(),view_,Standard_True);
    if(!context_->HasDetected()) return false;
    auto owner=Handle(StdSelect_BRepOwner)::DownCast(context_->DetectedOwner());
    if(owner.IsNull() || owner->Shape().IsNull() || owner->Shape().ShapeType()!=TopAbs_EDGE) return false;
    for(size_t i=0;i<boxes_.size();++i) if(isPartVisible(i) && boxes_[i].presentation==context_->DetectedInteractive()) {
        auto& session=*commandSession_;const auto& part=boxes_[i];
        session.edge=TopoDS::Edge(owner->Shape());
        session.faceBefore=std::make_shared<CadPart>(part);
        session.changes.push_back({i,part.min,part.max,part.min,part.max,part.orientation,part.orientation});
        return updateCommandParameter(false);
    }
    return false;
}
bool CadViewport::previewEdgeBlend(double value) {
    auto& session=*commandSession_;
    if(!session.faceBefore) return false;
    const auto& before=*session.faceBefore;
    QString error;
    const double localValue=value/std::abs(before.orientation.ScaleFactor());
    auto candidate=session.kind==CommandSession::Kind::Fillet
        ?FilletGeometry::apply(before.shape,session.edge,localValue,error)
        :ChamferGeometry::apply(before.shape,session.edge,localValue,error);
    session.value=value;session.candidate=candidate;
    session.valid=!candidate.IsNull();session.changed=session.valid;
    auto& change=session.changes.front();change.faceChange.reset();
    if(session.valid) {
        CadPart after=before;after.shape=candidate;
        after.geometryKind=CadPart::GeometryKind::GeneralBRep;
        after.cylinderPrimitive=after.spherePrimitive=after.conePrimitive=false;
        Bnd_Box bounds;BRepBndLib::AddOptimal(candidate,bounds,false,false);
        bounds.Get(after.min[0],after.min[1],after.min[2],after.max[0],after.max[1],after.max[2]);
        after.baseMin=after.min;after.baseMax=after.max;
        change.afterMin=after.min;change.afterMax=after.max;
        change.faceChange=std::make_shared<FaceChange>(FaceChange{before,after});
    }
    const bool valid=presentFacePreview(before,session.changed,error);
    session.geometryError=error;
    session.state=valid?CommandSession::State::Preview:CommandSession::State::Parameters;
    if(!valid) emit statusMessage(error);
    redrawFacePreview();return valid;
}
