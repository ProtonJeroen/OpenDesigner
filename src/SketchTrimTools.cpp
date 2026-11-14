#include "CadViewport.h"
#include "SketchCurveOperations.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <SelectMgr_EntityOwner.hxx>
#include <algorithm>

void CadViewport::startSketchTrim() {
    const auto* owner=constructionObject(activeSketchWorkplaneId_);
    if(!owner || !constructionShown(*owner) || !owner->selectable) {
        emit statusMessage(tr("Activate a visible, unlocked Workplane before Trim."));return;
    }
    cancelCommandSession();resetModelingTools();clearHistorySelection();refreshSketchPicking();
    commandSession_.emplace();auto& session=*commandSession_;session.kind=CommandSession::Kind::Sketch;
    session.sketch=std::make_shared<SketchChange>(SketchChange{sketchEntities_,sketchEntities_});session.trim=true;session.valid=false;
    if(!sketchTrimPrompt_) {
        sketchTrimPrompt_=new QLabel(commandPanel_);sketchTrimPrompt_->setObjectName("sketchTrimPrompt");
        sketchTrimPrompt_->setWordWrap(true);sketchTrimPrompt_->setMinimumWidth(300);commandLayout_->insertWidget(0,sketchTrimPrompt_);
    }
    const auto prompt=tr("Click the unwanted curve portion. Crossing curves on this Workplane bound the removal. One Trim per OK.");
    sketchTrimPrompt_->setText(prompt);sketchTrimPrompt_->show();commandPanel_->setWindowTitle(tr("Trim"));
    commandButtons_->show();commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(false);
    showCommandPanel(commandPanel_);setFocus();emit statusMessage(prompt);
}
bool CadViewport::sketchTrimMove(const QPoint& pixel) {
    if(!commandSession_ || !commandSession_->trim) return false;
    if(!commandSession_->valid) context_->MoveTo(pixel.x(),pixel.y(),view_,Standard_True);
    return true;
}
bool CadViewport::sketchTrimClick(const QPoint& pixel) {
    if(!commandSession_ || !commandSession_->trim) return false;
    const auto feedback=[&](const QString& message) {
        sketchTrimPrompt_->setText(message);showCommandPanel(commandPanel_);emit statusMessage(message);
    };
    if(commandSession_->valid) {feedback(tr("Review the blue remaining geometry. OK/Enter confirms this Trim; Cancel/Escape discards it."));return true;}
    const auto* owner=constructionObject(activeSketchWorkplaneId_);
    if(!owner || !constructionShown(*owner) || !owner->selectable) {cancelCommandSession();return true;}
    const auto cursor=sketchPlanePoint(pixel);
    if(!cursor) {feedback(tr("The Workplane is edge-on. Use Look at Workplane before Trim."));return true;}
    const auto hits=sketchCurveHits(pixel);
    if(hits.size()!=1) {
        feedback(hits.empty()?tr("Click a visible, unlocked curve on the active Workplane.")
            :tr("Several curves are under the pointer. Click farther along the unwanted portion, away from intersections."));return true;
    }
    const auto source=std::find_if(sketchEntities_.begin(),sketchEntities_.end(),[&](const auto& e){return e.id==hits.front();});
    std::vector<Sketch::Geometry> boundaries;
    for(const auto& e:sketchEntities_) if(e.id!=source->id && e.workplaneId==owner->id && e.visible && e.selectable
        && !std::holds_alternative<Sketch::ConstructionPoint>(e.geometry)) boundaries.push_back(e.geometry);
    const auto result=SketchCurveOperations::trim(source->geometry,boundaries,*cursor);
    if(result.status!=SketchCurveOperations::Status::Success) {feedback(tr("Trim unavailable: %1").arg(result.message));return true;}
    auto after=sketchEntities_;const auto index=size_t(source-sketchEntities_.begin());
    after.erase(after.begin()+index);
    std::vector<Sketch::Entity> replacements;
    for(const auto& geometry:result.remaining) {
        auto entity=*source;entity.geometry=geometry;
        if(!replacements.empty()) entity.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        replacements.push_back(std::move(entity));
    }
    after.insert(after.begin()+index,replacements.begin(),replacements.end());
    QString error;
    if(!previewSketchEntities(after,error,false)) {feedback(tr("Trim preview failed: %1").arg(error));return true;}
    commandSession_->trim=true;commandSession_->valid=true;
    sketchTrimPrompt_->show();commandButtons_->show();commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(true);
    commandPanel_->setWindowTitle(tr("Trim Preview"));
    QString prompt=tr("Blue shows the remaining geometry. OK/Enter confirms one Trim; Cancel/Escape restores the original.");
    if(std::any_of(replacements.begin(),replacements.end(),[](const auto& e){return std::holds_alternative<Sketch::BSpline>(e.geometry);}))
        prompt+=tr(" Spline fragments retain exact coefficients. Profile certification supports nonrational degree ≤3 fragments only; through-point editing does not apply to fragments.");
    feedback(prompt);setFocus();return true;
}

// Native AIS detection and the established Sketch eligibility rules, shared by
// Trim and Extend. No alternate selection model or geometry approximation.
QStringList CadViewport::sketchCurveHits(const QPoint& pixel) {
    context_->MoveTo(pixel.x(),pixel.y(),view_,Standard_False);
    QStringList hits;
    for(context_->InitDetected();context_->MoreDetected();context_->NextDetected()) {
        const auto detected=context_->DetectedCurrentOwner();if(detected.IsNull()) continue;
        for(const auto& entity:sketchEntities_) if(entity.workplaneId==activeSketchWorkplaneId_ && sketchPickable(entity)
            && !std::holds_alternative<Sketch::ConstructionPoint>(entity.geometry)
            && sketchPresentations_.contains(entity.id) && detected->Selectable()==sketchPresentations_.at(entity.id)
            && !hits.contains(entity.id)) hits.push_back(entity.id);
    }
    return hits;
}
