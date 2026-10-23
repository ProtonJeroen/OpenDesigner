#include "CadViewport.h"
#include <QComboBox>
#include <QPushButton>
#include <Graphic3d_ZLayerId.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <algorithm>
#include <cmath>

void CadViewport::startSketchOverdraw() {
    const auto* owner=constructionObject(activeSketchWorkplaneId_);
    if(!owner || !owner->visible || !owner->selectable) {emit statusMessage(tr("Activate a visible, unlocked Workplane before Overdraw."));return;}
    cancelCommandSession();clearHistorySelection();refreshSketchPicking();
    commandSession_.emplace();auto& session=*commandSession_;session.kind=CommandSession::Kind::Sketch;
    session.sketch=std::make_shared<SketchChange>(SketchChange{sketchEntities_,sketchEntities_});
    session.overdraw=std::make_shared<SketchOverdrawState>();session.valid=false;
    auto& state=*session.overdraw;
    Sketch::overdrawCandidates(sketchEntities_,owner->id,state.graph,state.error);
    sketchOverdrawPanel_=new SketchOverdrawPanel(owner->name,this);
    sketchOverdrawPanel_->choose=[this]{chooseOverdrawPiece();};
    sketchOverdrawPanel_->back=[this]{backOverdrawPiece();};
    sketchOverdrawPanel_->newStart=[this]{newOverdrawStart();};
    sketchOverdrawPanel_->confirmed=[this]{confirmCommandSession();};
    sketchOverdrawPanel_->cancelled=[this]{cancelCommandSession();};
    sketchOverdrawPanel_->candidateChanged=[this](int index) {
        if(!commandSession_ || !commandSession_->overdraw) return;
        auto& d=*commandSession_->overdraw;
        auto& selected=d.currentNode?d.hover:d.nodeHover;selected.reset();
        if(index>=0 && size_t(index)<d.hits.size()) selected=d.hits[size_t(index)];
        refreshOverdraw();
    };
    showCommandPanel(sketchOverdrawPanel_);refreshOverdraw();setFocus();
}
bool CadViewport::overdrawMove(const QPoint& pixel) {
    if(!commandSession_ || !commandSession_->overdraw) return false;
    auto& d=*commandSession_->overdraw;if(!d.error.isEmpty()) return true;
    const auto cursor=sketchPlanePoint(pixel);
    d.feedback=cursor?QString():tr("Workplane is edge-on; use Look Normal.");
    const auto* owner=constructionObject(activeSketchWorkplaneId_);
    const auto project=[&](Sketch::Point p) {
        const auto world=Sketch::worldPoint(p,owner->frame);int x,y;view_->Convert(world.X(),world.Y(),world.Z(),x,y);return Sketch::Point{double(x),double(y)};
    };
    std::vector<size_t> hits;
    if(cursor) {
        if(!d.currentNode) {
            for(size_t n=0;n<d.graph.nodes.size();++n) {
                const auto p=project(d.graph.nodes[n]);if(std::hypot(p.x-pixel.x(),p.y-pixel.y())<=sketchSnapPixels_) hits.push_back(n);
            }
        } else {
            std::vector<size_t> selected;for(const auto& pick:d.picks) selected.push_back(pick.piece);
            hits=Sketch::overdrawHits(d.graph,*d.currentNode,selected,*cursor,{double(pixel.x()),double(pixel.y())},sketchSnapPixels_,project);
        }
    }
    auto& selected=d.currentNode?d.hover:d.nodeHover;
    if(hits!=d.hits) selected.reset();
    d.hits=std::move(hits);
    if(d.hits.size()==1) selected=d.hits.front();
    if(d.hits.empty()) selected.reset();
    refreshOverdraw();return true;
}
bool CadViewport::overdrawClick(const QPoint& pixel) {
    if(!commandSession_ || !commandSession_->overdraw) return false;
    overdrawMove(pixel);chooseOverdrawPiece();return true;
}
void CadViewport::chooseOverdrawPiece() {
    if(!commandSession_ || !commandSession_->overdraw) return;
    auto& d=*commandSession_->overdraw;if(!d.error.isEmpty()) return;
    if(!d.currentNode) {
        if(d.nodeHover) {d.currentNode=d.nodeHover;d.nodeHover.reset();d.hits.clear();}
        else d.feedback=d.hits.size()>1?tr("Several boundary points overlap on screen. Explicitly choose a start in the list."):tr("Pick a meeting point or a finite construction endpoint first.");
    } else if(d.hover) {
        const auto index=*d.hover;const auto& piece=d.graph.pieces[index];
        if(std::any_of(d.picks.begin(),d.picks.end(),[&](const auto& p){return p.piece==index;})) return;
        const auto from=*d.currentNode,to=piece.start==from?piece.end:piece.start;
        if(piece.start!=from && piece.end!=from) return;
        Sketch::Entity profile;profile.workplaneId=activeSketchWorkplaneId_;profile.role=Sketch::Role::Profile;
        profile.name=tr("Overdraw %1").arg(d.picks.size()+1);profile.geometry=Sketch::directedPiece(piece,from);
        d.picks.push_back({index,from,to,profile});d.currentNode=to;d.hover.reset();d.hits.clear();d.feedback.clear();
    } else d.feedback=d.hits.size()>1?tr("Ambiguous pieces: explicitly choose the desired portion in the list or move farther along it."):tr("Point along an unchosen piece connected to the blue point.");
    refreshOverdraw();
}
void CadViewport::backOverdrawPiece() {
    if(!commandSession_ || !commandSession_->overdraw) return;
    auto& d=*commandSession_->overdraw;
    if(!d.picks.empty()) {d.currentNode=d.picks.back().from;d.picks.pop_back();}else d.currentNode.reset();
    if(!d.graph.pieces.empty()) d.error.clear();
    d.hits.clear();d.hover.reset();d.nodeHover.reset();d.feedback.clear();refreshOverdraw();
}
void CadViewport::newOverdrawStart() {
    if(!commandSession_ || !commandSession_->overdraw) return;
    auto& d=*commandSession_->overdraw;d.currentNode.reset();d.nodeHover.reset();d.hover.reset();d.hits.clear();d.feedback.clear();refreshOverdraw();
}
void CadViewport::refreshOverdraw() {
    if(!commandSession_ || !commandSession_->overdraw || !sketchOverdrawPanel_) return;
    auto& session=*commandSession_;auto& d=*session.overdraw;const auto* owner=constructionObject(activeSketchWorkplaneId_);
    if(!owner) return;
    session.sketch->after=session.sketch->before;session.valid=false;
    if(!overdrawHover_.IsNull()) {context_->Remove(overdrawHover_,Standard_False);overdrawHover_.Nullify();}
    try {
        const bool rebuild=sketchPreviews_.size()!=d.picks.size() || std::any_of(d.picks.begin(),d.picks.end(),[&](const auto& p){return !sketchPreviews_.contains(p.profile.id);});
        if(rebuild) {
            for(const auto& [id,p]:sketchPreviews_) context_->Remove(p,Standard_False);
            sketchPreviews_.clear();
            for(const auto& pick:d.picks) {
                auto p=sketchPresentation(pick.profile,*owner,true);p->SetZLayer(Graphic3d_ZLayerId_Topmost);
                sketchPreviews_[pick.profile.id]=p;displaySketchPresentation(pick.profile,*owner,p,true);
            }
        }
        if(d.error.isEmpty() && d.hover) {
            Sketch::Entity e;e.workplaneId=owner->id;e.geometry=Sketch::directedPiece(d.graph.pieces[*d.hover],*d.currentNode);
            auto p=sketchPresentation(e,*owner,true);const Quantity_Color green(Quantity_NOC_GREEN);
            p->SetColor(green);p->SetWidth(3);p->Attributes()->SetWireAspect(new Prs3d_LineAspect(green,Aspect_TOL_SOLID,3));
            p->SetZLayer(Graphic3d_ZLayerId_Topmost);displaySketchPresentation(e,*owner,p,true);overdrawHover_=p;
        }
        if(d.error.isEmpty() && !d.picks.empty()) {
            for(const auto& pick:d.picks) session.sketch->after.push_back(pick.profile);
            session.valid=true;
        }
    } catch(const Standard_Failure& failure) {d.error=tr("Cannot preview this piece: %1. Use Back or Cancel.").arg(QString::fromUtf8(failure.GetMessageString()));}
    if(d.currentNode) showSketchMarker(sketchAnchorMarker_,d.graph.nodes[*d.currentNode],Quantity_Color(0.1,0.65,1,Quantity_TOC_RGB));
    else if(!sketchAnchorMarker_.IsNull()) context_->Erase(sketchAnchorMarker_,Standard_False);
    if(d.nodeHover) showSketchMarker(sketchSnapMarker_,d.graph.nodes[*d.nodeHover],Quantity_Color(Quantity_NOC_ORANGE));
    else if(!sketchSnapMarker_.IsNull()) context_->Erase(sketchSnapMarker_,Standard_False);
    QStringList labels;
    for(auto index:d.hits) {
        if(!d.currentNode) {const auto p=d.graph.nodes[index];labels<<tr("Start at X %1, Y %2").arg(p.x,0,'g',12).arg(p.y,0,'g',12);continue;}
        const auto& piece=d.graph.pieces[index];const auto directed=Sketch::directedPiece(piece,*d.currentNode);const auto* arc=std::get_if<Sketch::Arc>(&directed);
        QString source=piece.sourceId;for(const auto& e:session.sketch->before) if(e.id==source && !e.name.isEmpty()) {source=e.name;break;}
        labels<<tr("%1 — piece %2: %3").arg(source).arg(index+1).arg(arc?tr("arc R %1, sweep %2°").arg(arc->radius,0,'g',9).arg(arc->sweepAngle*180/3.141592653589793,0,'g',9):tr("line segment"));
    }
    const auto selected=d.currentNode?d.hover:d.nodeHover;int selectedIndex=-1;
    if(selected) for(size_t i=0;i<d.hits.size();++i) if(d.hits[i]==*selected) selectedIndex=int(i);
    QString prompt=!d.currentNode?tr("Pick a meeting point or finite endpoint on construction geometry."):tr("Point along the next piece; green is proposed, click chooses blue. OK confirms all chosen pieces.");
    if(d.currentNode && d.hits.empty()) {
        bool available=false;
        for(size_t i=0;i<d.graph.pieces.size();++i) {
            const auto& p=d.graph.pieces[i];
            if((p.start==*d.currentNode || p.end==*d.currentNode) && std::none_of(d.picks.begin(),d.picks.end(),[&](const auto& pick){return pick.piece==i;})) {available=true;break;}
        }
        if(!available) prompt=tr("No unchosen pieces meet this endpoint. Use New start, Back or OK.");
    }
    if(d.hits.size()>1 && !selected) prompt=tr("Ambiguous: explicitly choose one of the %1 candidates in the list, or move to separate them.").arg(d.hits.size());
    if(!d.feedback.isEmpty()) prompt=d.feedback;
    if(!d.error.isEmpty()) prompt=tr("Overdraw unavailable: %1").arg(d.error);
    session.state=session.valid?CommandSession::State::Preview:CommandSession::State::Selection;
    sketchOverdrawPanel_->update(prompt,d.graph.notice,labels,selectedIndex,int(d.picks.size()),d.currentNode.has_value(),session.valid);
    emit statusMessage(prompt);view_->Redraw();
}
void CadViewport::closeOverdrawPanel() {
    if(!overdrawHover_.IsNull()) {context_->Remove(overdrawHover_,Standard_False);overdrawHover_.Nullify();}
    if(!sketchOverdrawPanel_) return;
    auto* p=sketchOverdrawPanel_;sketchOverdrawPanel_=nullptr;
    p->choose={};p->back={};p->newStart={};p->confirmed={};p->cancelled={};p->candidateChanged={};p->hide();p->deleteLater();
}
