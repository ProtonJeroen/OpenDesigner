#include "CadViewport.h"
#include "DeleteFaceGeometry.h"
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <algorithm>

void CadViewport::deleteFace() {
    endSketchEditing();
    cancelCommandSession();
    const auto selection=selectionContext();
    if(selection.kind!=SelectionContext::Kind::Faces||selection.faces.empty()) {
        emit statusMessage(tr("Delete Face: select one or more faces first."));return;
    }
    // Build all candidates before starting any document or presentation update.
    std::vector<PullChange> changes;
    QString error;
    for(const auto index:selection.parts) {
        std::vector<TopoDS_Face> faces;
        for(const auto& selected:selection.faces) if(selected.partIndex==index) faces.push_back(selected.face);
        BRepReconstruction::Result result;
        const auto& before=boxes_[index];
        auto blendState=before.blends;TopoDS_Shape sharpResult;
        QString blendError;
        bool recognized=EdgeBlendFeatures::resolve(before.shape,blendState,blendError);
        bool transitions=recognized&&std::any_of(faces.begin(),faces.end(),[&](const auto& f){return EdgeBlendFeatures::selected(blendState,f)>=0;});
        EdgeBlendFeatures::State afterBlends;
        bool success=false;
        if(transitions) {
            success=EdgeBlendFeatures::remove(before.shape,blendState,faces,afterBlends,sharpResult,error);
            if(success) result.solid=sharpResult;
        } else success=DeleteFaceGeometry::remove(before.shape,faces,result,error);
        if(!success) {
            reportGeometryFailure(tr("Delete Face"),error,true);return;
        }
        auto after=before;after.shape=result.solid;after.blends=afterBlends;
        after.geometryKind=CadPart::GeometryKind::GeneralBRep;
        after.cylinderPrimitive=after.spherePrimitive=after.conePrimitive=false;
        Bnd_Box bounds;BRepBndLib::AddOptimal(after.shape,bounds,false,false);
        bounds.Get(after.min[0],after.min[1],after.min[2],after.max[0],after.max[1],after.max[2]);
        after.baseMin=after.min;after.baseMax=after.max;
        PullChange change{};change.partIndex=index;
        change.beforeMin=before.min;change.beforeMax=before.max;
        change.afterMin=after.min;change.afterMax=after.max;
        change.beforeOrientation=before.orientation;change.afterOrientation=after.orientation;
        change.faceChange=std::make_shared<FaceChange>(FaceChange{before,after});
        changes.push_back(change);
    }
    beginCommandSession(CommandSession::Kind::DeleteFace,selection.parts,gp_Dir(1,0,0));
    auto& session=*commandSession_;
    session.changes=std::move(changes);session.deletedFaces=selection.faces;
    session.changed=true;session.valid=false;
    commandPanel_->setWindowTitle(tr("Delete Face Preview"));
    dragDimension_->hide();
    faceAlignTargetLabel_->setText(tr("Delete %1 face(s) on %2 Part(s)").arg(selection.faces.size()).arg(selection.parts.size()));
    faceAlignTargetLabel_->show();
    commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(false);
    for(const auto& change:session.changes) {
        const auto preview=createFacePreview(change.faceChange->before,change.faceChange->after.shape,error);
        if(preview.IsNull()) {
            cancelCommandSession();reportGeometryFailure(tr("Delete Face"),error,true);return;
        }
        session.facePreviews.push_back(preview);
    }
    for(const auto& change:session.changes) context_->Erase(change.faceChange->before.presentation,Standard_False);
    session.valid=true;session.state=CommandSession::State::Preview;
    commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(true);
    commandPanel_->adjustSize();commandPanel_->activateWindow();
    commandButtons_->button(QDialogButtonBox::Ok)->setFocus();
    redrawFacePreview();
    emit statusMessage(tr("Delete Face: inspect the blue solid. OK/Enter applies; Cancel/Esc discards."));
}
