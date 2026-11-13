// v0.5.3 unified reference picking and Move Face direction sources.
// See docs/WORKPLANE_REFERENCES.md. Geometry is resolved only through
// References::resolve; this file adapts the viewport (pixels, AIS) to it.
#include "CadViewport.h"
#include "SketchDisplayBounds.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <StdSelect_BRepOwner.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <algorithm>
#include <cmath>

namespace {
double segmentDistance(Sketch::Point p,Sketch::Point a,Sketch::Point b) {
    const double dx=b.x-a.x,dy=b.y-a.y,length2=dx*dx+dy*dy;
    const double t=length2>0?std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/length2,0.0,1.0):0.0;
    return std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);
}
// Below this |d . n| a direction has no usable component along the Face normal.
constexpr double ParallelTolerance=1.e-9;
}

References::Model CadViewport::referenceModel() const {
    References::Model model;
    for(size_t i=0;i<boxes_.size();++i) {
        const auto& part=boxes_[i];
        model.parts.push_back({partId(i),part.name,part.shape,part.orientation,part.visible});
    }
    model.constructions=constructions_;model.sketches=sketchEntities_;
    // View-level Hide Workplanes: the resolver refuses Workplanes and their
    // Sketch geometry with the existing hidden-reference diagnostics.
    if(!showWorkplanes_) for(auto& o:model.constructions) if(o.kind==Construction::Kind::Workplane) o.visible=false;
    return model;
}
bool CadViewport::resolveReference(const References::Target& target,References::Resolved& result,QString& error) const {
    return References::resolve(referenceModel(),target,result,error);
}
std::optional<References::Target> CadViewport::pickReference(const QPoint& pixel,const std::vector<References::Kind>& accepted,QString& error) {
    using References::Kind;
    error.clear();initializeViewer();if(view_.IsNull() || context_.IsNull()) {error=tr("The viewport is not ready.");return {};}
    const auto accepts=[&](Kind kind){return std::find(accepted.begin(),accepted.end(),kind)!=accepted.end();};
    const Sketch::Point cursor{double(pixel.x()),double(pixel.y())};
    const auto project=[&](const gp_Pnt& p){int x,y;view_->Convert(p.X(),p.Y(),p.Z(),x,y);return Sketch::Point{double(x),double(y)};};
    const double radius=sketchSnapPixels_;
    const auto visibleWorkplane=[&](const QString& id)->const Construction::Object* {
        const auto* wp=constructionObject(id);return wp && wp->kind==Construction::Kind::Workplane && constructionShown(*wp)?wp:nullptr;
    };
    // 1. Sketch points: exactly the cursor-independent Sketch snap targets.
    if(accepts(Kind::SketchPoint)) {
        std::optional<Sketch::PointReference> best;double bestDistance=radius;
        for(const auto& wp:constructions_) if(visibleWorkplane(wp.id)) {
            auto targets=Sketch::snapTargets(sketchEntities_,wp.id,{},{},true);
            std::erase_if(targets,[](const auto& t){return !t.reference;});
            const auto snap=Sketch::closestSnap(targets,cursor,radius,[&](Sketch::Point p){return project(Sketch::worldPoint(p,wp.frame));});
            if(!snap) continue;
            const auto screen=project(Sketch::worldPoint(snap->point,wp.frame));
            const double d=std::hypot(screen.x-cursor.x,screen.y-cursor.y);
            if(!best || d<bestDistance) {best=snap->reference;bestDistance=d;}
        }
        if(best) return References::Target{Kind::SketchPoint,best->entity,{},*best};
    }
    // 2. Construction Points.
    if(accepts(Kind::Point)) {
        const Construction::Object* best=nullptr;double bestDistance=radius;
        for(const auto& o:constructions_) if(o.kind==Construction::Kind::Point && o.visible && o.selectable) {
            const auto s=project(o.frame.Location());const double d=std::hypot(s.x-cursor.x,s.y-cursor.y);
            if(d<=bestDistance) {best=&o;bestDistance=d;}
        }
        if(best) return References::Target{Kind::Point,best->id};
    }
    // 3. Sketch lines and Axes by their displayed segments.
    if(accepts(Kind::SketchLine) || accepts(Kind::Axis)) {
        std::optional<References::Target> best;double bestDistance=radius;
        if(accepts(Kind::SketchLine)) for(const auto& e:sketchEntities_) if(e.visible && e.selectable) {
            const auto* wp=visibleWorkplane(e.workplaneId);if(!wp) continue;
            std::optional<Sketch::Segment> shown;
            if(const auto* s=std::get_if<Sketch::Segment>(&e.geometry)) shown=*s;
            else if(const auto* l=std::get_if<Sketch::InfiniteLine>(&e.geometry)) shown=Sketch::clipLine(*l,Sketch::displayRectangle(*wp,sketchEntities_));
            if(!shown) continue;
            const double d=segmentDistance(cursor,project(Sketch::worldPoint(shown->start,wp->frame)),project(Sketch::worldPoint(shown->end,wp->frame)));
            if(d<=bestDistance) {best=References::Target{Kind::SketchLine,e.id};bestDistance=d;}
        }
        if(accepts(Kind::Axis)) for(const auto& o:constructions_) if(o.kind==Construction::Kind::Axis && o.visible && o.selectable) {
            const auto ends=Construction::displayBounds(o);if(ends.size()!=2) continue;
            const double d=segmentDistance(cursor,project(ends[0]),project(ends[1]));
            if(d<=bestDistance) {best=References::Target{Kind::Axis,o.id};bestDistance=d;}
        }
        if(best) return best;
    }
    // 4. OCCT detection of Part subshapes and Workplane frames.
    if(!accepts(Kind::Vertex) && !accepts(Kind::Edge) && !accepts(Kind::Face) && !accepts(Kind::Workplane)) {
        error=tr("No supported reference at this position.");return {};
    }
    std::vector<int> partModes;
    if(accepts(Kind::Vertex)) partModes.push_back(AIS_Shape::SelectionMode(TopAbs_VERTEX));
    if(accepts(Kind::Edge)) partModes.push_back(AIS_Shape::SelectionMode(TopAbs_EDGE));
    if(accepts(Kind::Face)) partModes.push_back(AIS_Shape::SelectionMode(TopAbs_FACE));
    for(const auto& part:boxes_) if(part.active && part.visible) {
        context_->Deactivate(part.presentation);
        for(const int mode:partModes) context_->Activate(part.presentation,mode,Standard_False);
    }
    for(const auto& o:constructions_) if(constructionPresentations_.contains(o.id)) {
        const auto& p=constructionPresentations_.at(o.id);context_->Deactivate(p);
        if(o.kind==Construction::Kind::Workplane && accepts(Kind::Workplane) && constructionShown(o) && o.selectable)
            context_->Activate(p,AIS_Shape::SelectionMode(TopAbs_EDGE),Standard_False);
    }
    context_->MoveTo(pixel.x(),pixel.y(),view_,Standard_False);
    std::optional<References::Target> result;
    if(context_->HasDetected()) {
        const auto detected=context_->DetectedInteractive();
        const auto owner=Handle(StdSelect_BRepOwner)::DownCast(context_->DetectedOwner());
        for(const auto& [id,p]:constructionPresentations_) if(p==detected) result=References::Target{Kind::Workplane,id};
        if(!result && !owner.IsNull()) for(size_t i=0;i<boxes_.size();++i) if(boxes_[i].presentation==detected) {
            const auto type=owner->Shape().ShapeType();
            const auto kind=type==TopAbs_VERTEX?Kind::Vertex:type==TopAbs_EDGE?Kind::Edge:Kind::Face;
            if(accepts(kind)) result=References::Target{kind,partId(i),owner->Shape()};
            break;
        }
    }
    // Restore the ordinary 3D picking state of an open face command.
    context_->ClearDetected(Standard_False);
    for(const auto& part:boxes_) if(part.active && part.visible) {
        context_->Deactivate(part.presentation);
        context_->Activate(part.presentation,AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
    }
    for(const auto& o:constructions_) if(constructionPresentations_.contains(o.id)) {
        const auto& p=constructionPresentations_.at(o.id);context_->Deactivate(p);
        if(activeSketchWorkplaneId_.isEmpty() && constructionShown(o) && o.selectable)
            context_->Activate(p,o.kind==Construction::Kind::Workplane?AIS_Shape::SelectionMode(TopAbs_EDGE):0,Standard_False);
    }
    if(!result) error=tr("No supported reference at this position.");
    return result;
}

// ---- Move Face direction ------------------------------------------------
void CadViewport::initializeMoveDirectionControls() {
    moveDirectionSource_=new QComboBox(commandPanel_);moveDirectionSource_->setObjectName("moveFaceDirectionSource");
    moveDirectionSource_->addItems({tr("Direction: Face normal"),tr("Direction: Edge / Sketch line / Axis / Face"),tr("Direction: Point to Point")});
    moveDirectionSource_->setToolTip(tr("Translate the Face along a reference direction. A planar Face moves by the direction's component along its normal."));
    moveDirectionPick_=new QPushButton(tr("Pick direction reference"),commandPanel_);moveDirectionPick_->setObjectName("moveFaceDirectionPick");
    moveDirectionReverse_=new QCheckBox(tr("Reverse direction"),commandPanel_);moveDirectionReverse_->setObjectName("moveFaceDirectionReverse");
    moveDirectionLabel_=new QLabel(commandPanel_);moveDirectionLabel_->setObjectName("moveFaceDirectionLabel");moveDirectionLabel_->setWordWrap(true);
    for(QWidget* w:std::array<QWidget*,4>{moveDirectionSource_,moveDirectionPick_,moveDirectionReverse_,moveDirectionLabel_}) {commandLayout_->addWidget(w);w->hide();}
    connect(moveDirectionSource_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index) {
        if(!commandSession_ || commandSession_->kind!=CommandSession::Kind::MoveFace) return;
        setMoveFaceDirection(MoveDirectionSource(index),{},moveDirectionReverse_->isChecked());
        moveDirectionPicking_=index!=0;updateMoveDirectionControls();
    });
    connect(moveDirectionPick_,&QPushButton::clicked,this,[this] {
        if(!commandSession_ || commandSession_->kind!=CommandSession::Kind::MoveFace) return;
        if(commandSession_->directionSource==MoveDirectionSource::PointToPoint && commandSession_->directionReferences.size()>=2)
            commandSession_->directionReferences.clear();
        moveDirectionPicking_=commandSession_->directionSource!=MoveDirectionSource::FaceNormal;updateMoveDirectionControls();
    });
    connect(moveDirectionReverse_,&QCheckBox::toggled,this,[this](bool reverse) {
        if(!commandSession_ || commandSession_->kind!=CommandSession::Kind::MoveFace) return;
        setMoveFaceDirection(commandSession_->directionSource,commandSession_->directionReferences,reverse);
    });
}
void CadViewport::updateMoveDirectionControls() {
    const bool planar=commandSession_ && commandSession_->kind==CommandSession::Kind::MoveFace && commandSession_->moveFeature==MoveFaceFeature::Planar;
    for(QWidget* w:std::array<QWidget*,4>{moveDirectionSource_,moveDirectionPick_,moveDirectionReverse_,moveDirectionLabel_}) w->setVisible(planar);
    if(!planar) {moveDirectionPicking_=false;return;}
    const auto& s=*commandSession_;
    {const QSignalBlocker a(moveDirectionSource_),b(moveDirectionReverse_);
     moveDirectionSource_->setCurrentIndex(int(s.directionSource));moveDirectionReverse_->setChecked(s.directionReverse);}
    moveDirectionPick_->setEnabled(s.directionSource!=MoveDirectionSource::FaceNormal);
    QStringList names;
    for(const auto& t:s.directionReferences) {References::Resolved r;QString e;names<<(resolveReference(t,r,e)?r.label:tr("stale %1").arg(References::kindName(t.kind)));}
    QString text;
    if(s.directionSource==MoveDirectionSource::FaceNormal) text=tr("Face normal");
    else if(s.directionSource==MoveDirectionSource::Reference) text=names.isEmpty()?tr("Pick a straight Edge, Sketch line, Axis, planar Face or Workplane."):names.front();
    else text=names.isEmpty()?tr("Pick the first point (Point, Vertex or Sketch point)."):names.size()==1?tr("%1 → pick the second point.").arg(names.front()):names.join(QStringLiteral(" → "));
    if(moveDirectionPicking_) text+=tr(" (picking)");
    moveDirectionLabel_->setText(text);
}
bool CadViewport::setMoveFaceDirection(MoveDirectionSource source,const std::vector<References::Target>& references,bool reverse) {
    if(!commandSession_ || commandSession_->kind!=CommandSession::Kind::MoveFace) return false;
    auto& s=*commandSession_;
    if(s.moveFeature!=MoveFaceFeature::Planar && source!=MoveDirectionSource::FaceNormal) {
        emit statusMessage(tr("Direction references are certified for planar Faces only; cylindrical walls use their lateral reference."));return false;
    }
    s.directionSource=source;s.directionReverse=reverse;
    s.directionReferences=source==MoveDirectionSource::FaceNormal?std::vector<References::Target>{}:references;
    const size_t needed=source==MoveDirectionSource::PointToPoint?2:source==MoveDirectionSource::Reference?1:0;
    if(s.directionReferences.size()>needed) s.directionReferences.resize(needed);
    if(s.directionReferences.size()==needed) moveDirectionPicking_=false;
    updateMoveDirectionControls();
    return updateCommandParameter(false);
}
bool CadViewport::pickMoveDirection(const QPoint& point) {
    if(!commandSession_ || commandSession_->kind!=CommandSession::Kind::MoveFace || !moveDirectionPicking_) return false;
    using References::Kind;
    auto& s=*commandSession_;
    const auto accepted=s.directionSource==MoveDirectionSource::PointToPoint
        ?std::vector<Kind>{Kind::SketchPoint,Kind::Point,Kind::Vertex}
        :std::vector<Kind>{Kind::SketchLine,Kind::Axis,Kind::Edge,Kind::Face,Kind::Workplane};
    QString error;const auto target=pickReference(point,accepted,error);
    if(!target) {emit statusMessage(tr("Move Face direction: %1").arg(error));updateMoveDirectionControls();return true;}
    References::Resolved resolved;
    if(!resolveReference(*target,resolved,error)) {emit statusMessage(tr("Move Face direction: %1").arg(error));return true;}
    if(s.directionSource==MoveDirectionSource::Reference) {
        gp_Dir d;if(!References::direction(resolved,d,error)) {emit statusMessage(tr("Move Face direction: %1").arg(error));return true;}
    }
    auto references=s.directionReferences;
    if(s.directionSource==MoveDirectionSource::Reference || references.size()>=2) references.clear();
    references.push_back(*target);
    setMoveFaceDirection(s.directionSource,references,s.directionReverse);
    view_->Redraw();return true;
}
bool CadViewport::moveFaceDirection(const CommandSession& s,gp_Dir& direction,double& normalFactor,QString& error) const {
    direction=s.faceNormal;
    if(s.directionSource==MoveDirectionSource::Reference) {
        if(s.directionReferences.size()!=1) {error=tr("Pick a direction reference (Edge, Sketch line, Axis, planar Face or Workplane).");return false;}
        References::Resolved r;if(!resolveReference(s.directionReferences[0],r,error) || !References::direction(r,direction,error)) return false;
    } else if(s.directionSource==MoveDirectionSource::PointToPoint) {
        if(s.directionReferences.size()!=2) {error=tr("Pick two points for the Point to Point direction.");return false;}
        References::Resolved a,b;double length=0;
        if(!resolveReference(s.directionReferences[0],a,error) || !resolveReference(s.directionReferences[1],b,error)
           || !References::pointToPoint(a,b,direction,length,error)) return false;
    }
    if(s.directionReverse) direction.Reverse();
    normalFactor=direction.Dot(s.faceNormal);
    if(std::abs(normalFactor)<ParallelTolerance) {
        error=tr("The direction is parallel to the Face; translating a planar Face along it does not change the solid.");return false;
    }
    error.clear();return true;
}
