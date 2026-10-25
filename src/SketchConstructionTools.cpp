#include "CadViewport.h"
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSignalBlocker>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <algorithm>
#include <cmath>

namespace {
bool eligible(const Sketch::Entity& e,const QString& owner,Sketch::ConstructionTool tool) {
    return e.workplaneId==owner && e.visible && e.selectable
        && (tool==Sketch::ConstructionTool::OffsetLine?Sketch::straightReference(e.geometry)
            :e.role==Sketch::Role::Construction && Sketch::circularReference(e.geometry));
}
}
void CadViewport::startSketchConstruction(Sketch::ConstructionTool tool) {
    const auto* owner=constructionObject(activeSketchWorkplaneId_);
    if(!owner || !owner->visible || !owner->selectable) {emit statusMessage(tr("Activate a visible, unlocked Workplane first."));return;}
    const auto preselected=selectedSketchIds_;
    cancelCommandSession();clearHistorySelection();refreshSketchPicking();
    commandSession_.emplace();auto& session=*commandSession_;session.kind=CommandSession::Kind::Sketch;
    session.sketch=std::make_shared<SketchChange>(SketchChange{sketchEntities_,sketchEntities_});
    session.lineConstruction=std::make_shared<SketchConstructionState>();auto& d=*session.lineConstruction;d.tool=tool;
    d.candidate.workplaneId=owner->id;d.candidate.role=Sketch::Role::Construction;
    d.candidate.name=tool==Sketch::ConstructionTool::OffsetLine?tr("Offset construction line"):tr("Common tangent construction line");
    session.valid=false;
    auto* panel=sketchConstructionPanel_=new SketchConstructionPanel(tool,owner->name,this);
    QStringList choices;
    for(const auto& e:sketchEntities_) if(eligible(e,owner->id,tool)) {
        choices<<e.id;
        for(auto* combo:panel->references) if(combo) combo->addItem(tr("%1 — %2 [%3]").arg(e.name.isEmpty()?Sketch::typeName(e.geometry):e.name,Sketch::typeName(e.geometry),e.id.left(8)),e.id);
    }
    const int roles=tool==Sketch::ConstructionTool::OffsetLine?1:2;
    if(!preselected.empty() && preselected.size()<=roles && std::all_of(preselected.begin(),preselected.end(),[&](const auto& id){return choices.contains(id);}))
        for(qsizetype i=0;i<preselected.size();++i) panel->references[i]->setCurrentIndex(panel->references[i]->findData(preselected[i]));
    const auto nextRole=[this,roles] {
        auto& state=*commandSession_->lineConstruction;state.pickRole=-1;
        for(int i=0;i<roles;++i) if(sketchConstructionPanel_->references[i]->currentData().toString().isEmpty()) {state.pickRole=i;break;}
        refreshSketchConstruction();
    };
    panel->referenceChanged=[nextRole](int){nextRole();};
    panel->pickReference=[this](int role) {commandSession_->lineConstruction->pickRole=role;refreshSketchConstruction();setFocus();};
    panel->parametersChanged=[this]{refreshSketchConstruction();};
    panel->solutionChanged=[this](int index) {commandSession_->lineConstruction->selected=index;refreshSketchConstruction(false);};
    panel->confirmed=[this]{confirmCommandSession();};panel->cancelled=[this]{cancelCommandSession();};
    showCommandPanel(panel);nextRole();setFocus();
}
bool CadViewport::acceptSketchConstructionReference(const QString& id) {
    if(!commandSession_ || !commandSession_->lineConstruction || !sketchConstructionPanel_) return false;
    auto& d=*commandSession_->lineConstruction;if(d.pickRole<0) return false;
    auto* combo=sketchConstructionPanel_->references[d.pickRole];const int index=combo->findData(id);
    if(index<=0) {sketchConstructionPanel_->setStatus(tr("This object cannot supply the requested 2D reference."),false);return false;}
    {const QSignalBlocker block(combo);combo->setCurrentIndex(index);}
    d.pickRole=-1;
    for(int i=0;i<(d.tool==Sketch::ConstructionTool::OffsetLine?1:2);++i)
        if(sketchConstructionPanel_->references[i]->currentData().toString().isEmpty()) {d.pickRole=i;break;}
    refreshSketchConstruction();return true;
}
void CadViewport::refreshSketchConstruction(bool recompute) {
    if(!commandSession_ || !commandSession_->lineConstruction || !sketchConstructionPanel_) return;
    auto& session=*commandSession_;auto& d=*session.lineConstruction;auto* panel=sketchConstructionPanel_;
    const auto* owner=constructionObject(activeSketchWorkplaneId_);if(!owner) return;
    for(const auto& [id,p]:sketchPreviews_) context_->Remove(p,Standard_False);
    sketchPreviews_.clear();for(const auto& p:constructionLineAlternatives_) context_->Remove(p,Standard_False);constructionLineAlternatives_.clear();clearSketchFeedback();
    session.sketch->after=session.sketch->before;session.valid=false;d.referenceHits.clear();d.solutionHits.clear();
    const auto source=[&](int role)->const Sketch::Entity* {
        const auto id=panel->references[role]->currentData().toString();
        for(const auto& e:session.sketch->before) if(e.id==id && eligible(e,owner->id,d.tool)) return &e;
        return nullptr;
    };
    QString message;
    if(d.pickRole>=0) message=tr("Pick reference %1 in the viewport or Structure Browser, or choose it in the list.").arg(d.pickRole+1);
    else if(d.tool==Sketch::ConstructionTool::OffsetLine) {
        if(const auto* first=source(0)) {
            const auto output=static_cast<Sketch::LineOutput>(panel->output->currentData().toInt());
            const bool needInterval=output==Sketch::LineOutput::Finite && std::holds_alternative<Sketch::InfiniteLine>(first->geometry);
            panel->setIntervalVisible(needInterval);
            std::optional<Sketch::LineInterval> interval;
            if(needInterval && panel->explicitInterval->isChecked()) interval=Sketch::LineInterval{panel->intervalStart->value(),panel->intervalLength->value()};
            session.valid=Sketch::offsetConstructionLine(first->geometry,panel->distance->value(),output,interval,d.candidate.geometry,message);
            if(session.valid) message=panel->mouseSide->isChecked()?tr("Move to the desired side; click to lock it. The numeric distance stays exact. OK confirms.")
                :tr("Set the signed perpendicular distance or Left/Right side. Blue is temporary; OK confirms one Construction line.");
        } else message=tr("Choose a straight reference on the active Workplane.");
    } else {
        if(recompute) {
            d.selected=-1;d.solutions.clear();d.lines.clear();d.error.clear();d.notice.clear();
            const auto* first=source(0);const auto* second=source(1);
            if(!first || !second) d.error=tr("Choose two Construction circles or arcs on the active Workplane.");
            else if(first->id==second->id) d.error=tr("Select two different circular objects.");
            else {
                std::vector<Sketch::CommonTangent> candidates;
                if(Sketch::commonTangents(first->geometry,second->geometry,candidates,d.error)) {
                    const auto output=static_cast<Sketch::LineOutput>(panel->output->currentData().toInt());int omitted=0;
                    for(const auto& tangent:candidates) {
                        Sketch::Geometry line;QString why;
                        if(Sketch::commonTangentLine(tangent,output,line,why)) {d.solutions.push_back(tangent);d.lines.push_back(line);}else ++omitted;
                    }
                    if(omitted) d.notice=tr("%1 shared-contact solution(s) have zero finite span; choose Infinite to use them.").arg(omitted);
                    if(d.lines.empty()) d.error=d.notice.isEmpty()?tr("No valid common tangent for this output."):d.notice;
                }
            }
            const QSignalBlocker block(panel->solutions);panel->solutions->clear();panel->solutions->addItem(tr("Choose a solution explicitly…"));
            for(size_t i=0;i<d.solutions.size();++i) {
                const auto& t=d.solutions[i];panel->solutions->addItem(tr("%1: %2 — (%3, %4) → (%5, %6)").arg(i+1).arg(t.internal?tr("Internal"):tr("External"))
                    .arg(t.first.x,0,'g',10).arg(t.first.y,0,'g',10).arg(t.second.x,0,'g',10).arg(t.second.y,0,'g',10));
            }
            panel->solutions->setCurrentIndex(0);
        }
        session.valid=d.selected>=0 && size_t(d.selected)<d.lines.size() && d.error.isEmpty();
        if(session.valid) {d.candidate.geometry=d.lines[size_t(d.selected)];message=tr("Blue solution chosen; orange marks the contacts. OK confirms one independent Construction line.");}
        else message=d.error.isEmpty()?tr("Choose one of %1 green candidates explicitly in the list or click a separate line.").arg(d.lines.size()):d.error;
        if(!d.notice.isEmpty() && message!=d.notice) message+='\n'+d.notice;
    }
    try {
        if(d.pickRole<0 && d.tool==Sketch::ConstructionTool::CommonTangent && d.error.isEmpty()) {
            for(size_t i=0;i<d.lines.size();++i) if(int(i)!=d.selected) {
                auto e=d.candidate;e.geometry=d.lines[i];auto p=sketchPresentation(e,*owner,true);const Quantity_Color color(0.25,0.85,0.35,Quantity_TOC_RGB);
                p->SetColor(color);p->Attributes()->SetWireAspect(new Prs3d_LineAspect(color,Aspect_TOL_DASH,2));p->SetZLayer(Graphic3d_ZLayerId_Topmost);
                constructionLineAlternatives_.push_back(p);displaySketchPresentation(e,*owner,p,true);
            }
        }
        if(session.valid) {
            auto p=sketchPresentation(d.candidate,*owner,true);p->SetZLayer(Graphic3d_ZLayerId_Topmost);sketchPreviews_[d.candidate.id]=p;displaySketchPresentation(d.candidate,*owner,p,true);
            if(d.tool==Sketch::ConstructionTool::CommonTangent) {
                const auto& t=d.solutions[size_t(d.selected)];showSketchMarker(sketchAnchorMarker_,t.first,Quantity_Color(Quantity_NOC_ORANGE));showSketchMarker(sketchSnapMarker_,t.second,Quantity_Color(Quantity_NOC_ORANGE));
            }
            session.sketch->after.push_back(d.candidate);
        }
    } catch(const Standard_Failure& failure) {
        session.valid=false;session.sketch->after=session.sketch->before;
        for(const auto& [id,p]:sketchPreviews_) context_->Remove(p,Standard_False);sketchPreviews_.clear();
        for(const auto& p:constructionLineAlternatives_) context_->Remove(p,Standard_False);constructionLineAlternatives_.clear();clearSketchFeedback();
        message=tr("Cannot preview the construction line: %1").arg(QString::fromUtf8(failure.GetMessageString()));
    }
    session.state=session.valid?CommandSession::State::Preview:CommandSession::State::Selection;
    panel->setStatus(message,session.valid);emit statusMessage(message);updateConstructionLabels();view_->Redraw();
}
bool CadViewport::sketchConstructionMove(const QPoint& pixel) {
    if(!commandSession_ || !commandSession_->lineConstruction) return false;
    auto& d=*commandSession_->lineConstruction;auto* panel=sketchConstructionPanel_;const auto* owner=constructionObject(activeSketchWorkplaneId_);
    const auto cursor=sketchPlanePoint(pixel);d.referenceHits.clear();d.solutionHits.clear();
    if(!cursor) {panel->setStatus(tr("Workplane is edge-on; use Look Normal. Numeric input remains available."),commandSession_->valid);return true;}
    const auto projected=[&](Sketch::Point p) {const auto world=Sketch::worldPoint(p,owner->frame);int x,y;view_->Convert(world.X(),world.Y(),world.Z(),x,y);return Sketch::Point{double(x),double(y)};};
    const auto nearby=[&](const Sketch::Geometry& g)->std::optional<Sketch::Point> {
        const auto p=Sketch::nearestPoint(g,*cursor);if(!p) return {};
        const auto at=projected(*p);if(std::hypot(at.x-pixel.x(),at.y-pixel.y())>sketchSnapPixels_) return {};return p;
    };
    if(d.pickRole>=0) {
        std::optional<Sketch::Point> target;
        for(const auto& e:commandSession_->sketch->before) if(eligible(e,owner->id,d.tool)) if(const auto p=nearby(e.geometry)) {d.referenceHits<<e.id;target=p;}
        if(d.referenceHits.size()==1) {showSketchMarker(sketchSnapMarker_,*target,Quantity_Color(Quantity_NOC_ORANGE));panel->setStatus(tr("Click to use this reference for role %1.").arg(d.pickRole+1),false);}
        else {clearSketchFeedback();panel->setStatus(d.referenceHits.size()>1?tr("References overlap on screen; choose the named reference explicitly in the list."):tr("Point at reference %1; acquisition radius is %2 px.").arg(d.pickRole+1).arg(sketchSnapPixels_),false);}
        view_->Redraw();
    } else if(d.tool==Sketch::ConstructionTool::OffsetLine && panel->mouseSide->isChecked()) {
        for(const auto& e:commandSession_->sketch->before) if(e.id==panel->references[0]->currentData().toString()) {
            Sketch::Point origin,direction;
            if(const auto* s=std::get_if<Sketch::Segment>(&e.geometry)) {origin=s->start;direction={s->end.x-origin.x,s->end.y-origin.y};}
            else {const auto& l=std::get<Sketch::InfiniteLine>(e.geometry);origin=l.origin;direction=l.direction;}
            const double signedSide=direction.x*(cursor->y-origin.y)-direction.y*(cursor->x-origin.x);
            if(std::abs(signedSide)>1.e-7*std::hypot(direction.x,direction.y)) {
                const double value=std::copysign(std::abs(panel->distance->value()),signedSide);if(value!=panel->distance->value()) panel->distance->setValue(value);
            }
            break;
        }
    } else if(d.tool==Sketch::ConstructionTool::CommonTangent) {
        for(size_t i=0;i<d.lines.size();++i) if(nearby(d.lines[i])) d.solutionHits.push_back(i);
        if(d.solutionHits.size()>1) panel->setStatus(tr("Several tangent candidates overlap here. Choose explicitly in the solution list."),commandSession_->valid);
    }
    return true;
}
bool CadViewport::sketchConstructionClick(const QPoint& pixel) {
    if(!commandSession_ || !commandSession_->lineConstruction) return false;
    sketchConstructionMove(pixel);auto& d=*commandSession_->lineConstruction;
    if(d.pickRole>=0) {if(d.referenceHits.size()==1) acceptSketchConstructionReference(d.referenceHits.front());}
    else if(d.tool==Sketch::ConstructionTool::OffsetLine && sketchConstructionPanel_->mouseSide->isChecked()) sketchConstructionPanel_->mouseSide->setChecked(false);
    else if(d.tool==Sketch::ConstructionTool::CommonTangent && d.solutionHits.size()==1) sketchConstructionPanel_->solutions->setCurrentIndex(int(d.solutionHits.front())+1);
    return true;
}
void CadViewport::closeSketchConstructionPanel() {
    for(const auto& p:constructionLineAlternatives_) context_->Remove(p,Standard_False);constructionLineAlternatives_.clear();
    if(!sketchConstructionPanel_) return;
    auto* p=sketchConstructionPanel_;sketchConstructionPanel_=nullptr;
    p->parametersChanged={};p->referenceChanged={};p->pickReference={};p->solutionChanged={};p->confirmed={};p->cancelled={};p->hide();p->deleteLater();
}
