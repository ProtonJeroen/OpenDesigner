#include "CadViewport.h"
#include "SketchCurveOperations.h"
#include "SketchProfileValidation.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

void CadViewport::startSketchExtend() {
    const auto* owner=constructionObject(activeSketchWorkplaneId_);
    if(!owner || !constructionShown(*owner) || !owner->selectable) {
        emit statusMessage(tr("Activate a visible, unlocked Workplane before Extend."));return;
    }
    cancelCommandSession();resetModelingTools();clearHistorySelection();refreshSketchPicking();
    commandSession_.emplace();auto& session=*commandSession_;session.kind=CommandSession::Kind::Sketch;
    session.sketch=std::make_shared<SketchChange>(SketchChange{sketchEntities_,sketchEntities_});session.extend=std::make_shared<SketchExtendState>();session.valid=false;
    if(!sketchExtendPrompt_) {
        sketchExtendPrompt_=new QLabel(commandPanel_);sketchExtendPrompt_->setObjectName("sketchExtendPrompt");
        sketchExtendPrompt_->setWordWrap(true);sketchExtendPrompt_->setMinimumWidth(300);commandLayout_->insertWidget(0,sketchExtendPrompt_);
    }
    const auto prompt=tr("Click near the endpoint to extend, then click one boundary curve. One Extend per OK.");
    sketchExtendPrompt_->setText(prompt);sketchExtendPrompt_->show();commandPanel_->setWindowTitle(tr("Extend"));
    commandButtons_->show();commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(false);
    showCommandPanel(commandPanel_);setFocus();emit statusMessage(prompt);
}
bool CadViewport::sketchExtendMove(const QPoint& pixel) {
    if(!commandSession_ || !commandSession_->extend) return false;
    if(!commandSession_->valid) context_->MoveTo(pixel.x(),pixel.y(),view_,Standard_True);
    return true;
}
bool CadViewport::sketchExtendClick(const QPoint& pixel) {
    if(!commandSession_ || !commandSession_->extend) return false;
    const auto feedback=[&](const QString& message) {
        sketchExtendPrompt_->setText(message);showCommandPanel(commandPanel_);emit statusMessage(message);
    };
    if(commandSession_->valid) {feedback(tr("Review the blue extended geometry. OK/Enter confirms; Cancel/Escape discards it."));return true;}
    const auto* owner=constructionObject(activeSketchWorkplaneId_);
    if(!owner || !constructionShown(*owner) || !owner->selectable) {cancelCommandSession();return true;}
    const auto cursor=sketchPlanePoint(pixel);
    if(!cursor) {feedback(tr("The Workplane is edge-on. Use Look at Workplane before Extend."));return true;}
    const auto hits=sketchCurveHits(pixel);
    if(hits.size()!=1) {
        feedback(hits.empty()?tr("Click a visible, unlocked curve on the active Workplane.")
            :tr("Several curves are under the pointer. Move away from intersections and select one curve."));return true;
    }
    auto state=*commandSession_->extend;
    const auto picked=std::find_if(sketchEntities_.begin(),sketchEntities_.end(),[&](const auto& e){return e.id==hits.front();});
    if(state.sourceId.isEmpty()) {
        const auto ends=SketchCurveOperations::extensionEnds(picked->geometry);
        if(ends.status!=SketchCurveOperations::Status::Success) {feedback(tr("Extend unavailable: %1").arg(ends.message));return true;}
        const auto distance=[&](Sketch::Point p) {
            const auto world=Sketch::worldPoint(p,owner->frame);int x,y;view_->Convert(world.X(),world.Y(),world.Z(),x,y);
            return std::hypot(double(x-pixel.x()),double(y-pixel.y()));
        };
        const bool start=distance(ends.start)<=sketchSnapPixels_,end=distance(ends.end)<=sketchSnapPixels_;
        if(start==end) {feedback(start?tr("Both endpoints overlap on screen. Zoom in and select one endpoint.")
            :tr("Click closer to the endpoint you want to extend; the middle of a curve does not choose a side."));return true;}
        state.sourceId=picked->id;state.atStart=start;*commandSession_->extend=state;
        showSketchMarker(sketchAnchorMarker_,start?ends.start:ends.end,Quantity_Color(0.1,0.65,1,Quantity_TOC_RGB));
        feedback(tr("%1 endpoint selected on %2. Now click a boundary curve. Cancel/Escape starts over.")
            .arg(start?tr("Start"):tr("End"),picked->name.isEmpty()?picked->id:picked->name));view_->Redraw();return true;
    }
    if(picked->id==state.sourceId) {feedback(tr("The endpoint is selected. Click a different curve as the boundary, or Cancel/Escape to start over."));return true;}
    const auto source=std::find_if(sketchEntities_.begin(),sketchEntities_.end(),[&](const auto& e){return e.id==state.sourceId;});
    if(source==sketchEntities_.end() || source->workplaneId!=owner->id || !sketchPickable(*source)) {cancelCommandSession();emit statusMessage(tr("The extension source is stale, hidden or locked. Start Extend again."));return true;}
    const auto result=SketchCurveOperations::extend(source->geometry,state.atStart?SketchCurveOperations::Endpoint::Start:SketchCurveOperations::Endpoint::End,picked->geometry);
    if(result.status!=SketchCurveOperations::Status::Success) {feedback(tr("Extend unavailable: %1").arg(result.message));return true;}
    auto after=sketchEntities_;after[size_t(source-sketchEntities_.begin())].geometry=*result.geometry;
    const auto role=source->role;const auto workplaneId=owner->id;
    QString error;
    if(!previewSketchEntities(after,error,false)) {feedback(tr("Extend preview failed: %1").arg(error));return true;}
    commandSession_->extend=std::make_shared<SketchExtendState>(state);commandSession_->valid=true;
    sketchExtendPrompt_->show();commandButtons_->show();commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(true);commandPanel_->setWindowTitle(tr("Extend Preview"));
    QString prompt=tr("Blue shows the exact extension. OK/Enter confirms one Extend; Cancel/Escape restores the original. The boundary is unchanged.");
    if(role==Sketch::Role::Profile) {
        const auto profile=SketchProfileValidation::validate(after,workplaneId);
        if(profile.status==SketchProfileValidation::Status::Valid) prompt+=tr(" The closed Profile passes topology validation; Profile Builder still certifies the wire and face.");
        else if(!profile.diagnostics.empty() && profile.diagnostics.front().code==SketchProfileValidation::Code::OpenContour)
            prompt+=tr(" This Sketch extension is valid, but the Profile is still open.");
        else prompt+=tr(" This Sketch extension is valid; the Profile is not certified: %1").arg(profile.diagnostics.empty()?tr("unknown validation result"):profile.diagnostics.front().message);
    }
    feedback(prompt);setFocus();return true;
}
