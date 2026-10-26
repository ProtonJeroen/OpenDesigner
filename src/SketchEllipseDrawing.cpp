#include "CadViewport.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSignalBlocker>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>

namespace {
constexpr double radians=std::numbers::pi/180;
bool supportValid(const Sketch::Ellipse& e,QString& error) {
    Sketch::Entity entity;entity.workplaneId="preview";entity.geometry=e;
    if(Sketch::valid(entity,error)) return true;
    error=QObject::tr("Use finite radii: major ≥ minor > 1e-9 mm. Axes are never swapped automatically.");return false;
}
bool mouseInput(Sketch::Ellipse& e,std::optional<double>& start,std::optional<double>& end,
                std::optional<double>& sweep,size_t step,Sketch::Point p,bool clockwise,QString& error) {
    if(step==0) {e.center=p;return true;}
    const double dx=p.x-e.center.x,dy=p.y-e.center.y;
    if(step==1) {
        e.majorRadius=std::hypot(dx,dy);e.axisAngle=std::atan2(dy,dx);e.minorRadius=e.majorRadius<=2.e-9?e.majorRadius:e.majorRadius/2;
    } else if(step==2) e.minorRadius=std::abs(-std::sin(e.axisAngle)*dx+std::cos(e.axisAngle)*dy);
    if(!supportValid(e,error)) return false;
    if(step>=3) {
        if(std::hypot(dx,dy)<=1.e-9) {error=QObject::tr("The center does not define a unique point on the ellipse.");return false;}
        const auto nearest=Sketch::nearestPoint(e,p);
        const auto parameter=nearest?Sketch::ellipseParameter(e,*nearest):std::nullopt;
        if(!parameter) {error=QObject::tr("Cannot resolve an exact on-ellipse point.");return false;}
        if(step==3) start=*parameter;
        else {end=*parameter;sweep=Sketch::directedArcSweep(*start,*end,clockwise);}
    }
    return true;
}
bool numericInput(Sketch::Ellipse& e,std::optional<double>& start,std::optional<double>& end,
                  std::optional<double>& sweep,size_t step,bool complete,SketchDrawingPanel& p,QString& error) {
    if(complete) {
        if(p.ellipseInputEdited(p.x)) e.center.x=p.x->value();
        if(p.ellipseInputEdited(p.y)) e.center.y=p.y->value();
        if(p.ellipseInputEdited(p.majorRadius)) e.majorRadius=p.majorRadius->value();
        if(p.ellipseInputEdited(p.axisAngle)) e.axisAngle=p.axisAngle->value()*radians;
        if(p.ellipseInputEdited(p.minorRadius)) e.minorRadius=p.minorRadius->value();
        if(!supportValid(e,error)) return false;
        if(start) {
            if(p.ellipseInputEdited(p.startParameter)) start=p.startParameter->value()*radians;
            if(p.ellipseInputEdited(p.sweepParameter)) sweep=p.sweepParameter->value()*radians;
            end=*start+*sweep;
        }
        return true;
    }
    if(step==0) e.center={p.x->value(),p.y->value()};
    if(step==1 || complete) {
        e.majorRadius=p.majorRadius->value();e.axisAngle=p.axisAngle->value()*radians;
        if(!complete) e.minorRadius=e.majorRadius<=2.e-9?e.majorRadius:e.majorRadius/2;
    }
    if(step==2 || complete) e.minorRadius=p.minorRadius->value();
    if(step>=1 && !supportValid(e,error)) return false;
    if(step==3 || (complete && start)) start=p.startParameter->value()*radians;
    if(step==4 || (complete && start)) {sweep=p.sweepParameter->value()*radians;end=*start+*sweep;}
    return true;
}
Sketch::Point acceptedPoint(const Sketch::Ellipse& e,const std::optional<double>& start,const std::optional<double>& end,size_t step) {
    if(step==0) return e.center;
    return Sketch::ellipsePoint(e,step==1?0:step==2?std::numbers::pi/2:step==3?*start:*end);
}
}
void CadViewport::acceptSketchEllipsePoint(Sketch::Point point) {
    auto& d=*commandSession_->drawing;auto e=d.ellipse;auto start=d.ellipseStart,end=d.ellipseEnd,sweep=d.sweep;QString error;
    if(!mouseInput(e,start,end,sweep,d.points.size(),point,sketchDrawingPanel_->direction->currentIndex()==1,error)) {
        sketchDrawingPanel_->setStep(int(d.points.size()),false,false,error);return;
    }
    if(d.points.size()==4) {
        auto candidate=d.candidate;candidate.geometry=Sketch::EllipticalArc{e,*start,*sweep};
        if(!Sketch::valid(candidate,error)) {sketchDrawingPanel_->setStep(4,false,false,tr("Choose a nonzero arc shorter than a full turn."));return;}
    }
    d.points.push_back(acceptedPoint(e,start,end,d.points.size()));d.ellipse=e;d.ellipseStart=start;d.ellipseEnd=end;d.sweep=sweep;
    d.complete=d.points.size()==(d.tool==Sketch::DrawTool::Ellipse?3:5);d.cursor.reset();d.snap.reset();
    sketchDrawingPanel_->resetEllipseEdits();sketchDrawingPanel_->setEllipseValues(e,start.value_or(0),sweep.value_or(std::numbers::pi/2),true);
    refreshSketchDrawing();
}
void CadViewport::useSketchEllipseNumeric() {
    auto& d=*commandSession_->drawing;auto e=d.ellipse;auto start=d.ellipseStart,end=d.ellipseEnd,sweep=d.sweep;QString error;
    if(!numericInput(e,start,end,sweep,d.points.size(),false,*sketchDrawingPanel_,error)) {
        sketchDrawingPanel_->setStep(int(d.points.size()),false,false,error);return;
    }
    if(d.points.size()==4) {
        auto candidate=d.candidate;candidate.geometry=Sketch::EllipticalArc{e,*start,*sweep};
        if(!Sketch::valid(candidate,error)) {sketchDrawingPanel_->setStep(4,false,false,tr("Enter a nonzero signed sweep smaller than 360 degrees."));return;}
    }
    d.points.push_back(acceptedPoint(e,start,end,d.points.size()));d.ellipse=e;d.ellipseStart=start;d.ellipseEnd=end;d.sweep=sweep;
    d.complete=d.points.size()==(d.tool==Sketch::DrawTool::Ellipse?3:5);d.cursor.reset();d.snap.reset();
    if(sweep) {const QSignalBlocker block(sketchDrawingPanel_->direction);sketchDrawingPanel_->direction->setCurrentIndex(*sweep<0?1:0);}
    sketchDrawingPanel_->resetEllipseEdits();sketchDrawingPanel_->setEllipseValues(e,start.value_or(0),sweep.value_or(std::numbers::pi/2),true);
    refreshSketchDrawing();
}
void CadViewport::editSketchEllipseParameters() {
    if(!commandSession_ || !commandSession_->drawing || !sketchDrawingPanel_) return;
    auto& d=*commandSession_->drawing;if(!Sketch::ellipseTool(d.tool)) return;
    // Numeric input owns the current preview until accepted or an explicit mouse
    // click. Pointer motion must not overwrite pending exact values.
    d.cursor.reset();d.snap.reset();
    if(sketchDrawingPanel_->sweepParameter->hasFocus() || d.complete || d.points.size()==4) {
        const QSignalBlocker block(sketchDrawingPanel_->direction);
        sketchDrawingPanel_->direction->setCurrentIndex(sketchDrawingPanel_->sweepParameter->value()<0?1:0);
    }
    refreshSketchDrawing();
}
void CadViewport::changeSketchEllipseDirection() {
    auto& d=*commandSession_->drawing;auto* p=sketchDrawingPanel_;const bool clockwise=p->direction->currentIndex()==1;
    if(d.complete && !commandSession_->valid) {
        const QSignalBlocker block(p->direction);p->direction->setCurrentIndex(d.sweep.value_or(1)<0?1:0);
        emit statusMessage(tr("Fix the invalid ellipse parameters before changing direction."));return;
    }
    if(!d.complete && d.points.size()==4 && p->hasEllipseEdits()) {
        const double pendingEnd=*d.ellipseStart+p->sweepParameter->value()*radians;
        p->sweepParameter->setValue(Sketch::directedArcSweep(*d.ellipseStart,pendingEnd,clockwise)/radians);
        return;
    }
    if(d.ellipseStart && d.ellipseEnd) d.sweep=Sketch::directedArcSweep(*d.ellipseStart,*d.ellipseEnd,clockwise);
    const double value=d.sweep.value_or((clockwise?-1:1)*std::abs(p->sweepParameter->value()*radians));
    {const QSignalBlocker block(p->sweepParameter);p->sweepParameter->setValue(value/radians);}
    p->resetEllipseEdits();refreshSketchDrawing();
}
void CadViewport::refreshSketchEllipseDrawing() {
    auto& session=*commandSession_;auto& d=*session.drawing;auto* p=sketchDrawingPanel_;
    auto e=d.ellipse;auto start=d.ellipseStart,end=d.ellipseEnd,sweep=d.sweep;QString error;
    const auto n=d.points.size();const bool numeric=p->hasEllipseEdits();
    const bool input=numeric || (d.cursor && !d.complete);
    bool usable=true;
    if(numeric) usable=numericInput(e,start,end,sweep,n,d.complete,*p,error);
    else if(input) usable=mouseInput(e,start,end,sweep,n,*d.cursor,p->direction->currentIndex()==1,error);
    session.sketch->after=session.sketch->before;session.sketchDisplayCandidates.clear();session.valid=false;std::set<QString> shown;
    std::vector<Sketch::Entity> candidates;
    const auto present=[&](Sketch::Entity entity,const QString& id) {entity.id=id;candidates.push_back(entity);};
    try {
        if(usable && (n>0 || input)) {
            if(n>0 && supportValid(e,error)) {
                d.candidate.geometry=d.tool==Sketch::DrawTool::EllipticalArc && start && sweep && (n>=5 || (n==4 && input))
                    ?Sketch::Geometry{Sketch::EllipticalArc{e,*start,*sweep}}:Sketch::Geometry{e};
                if(Sketch::valid(d.candidate,error)) {
                    present(d.candidate,d.candidate.id);
                    if(d.complete) {
                        d.ellipse=e;d.ellipseStart=start;d.ellipseEnd=end;d.sweep=sweep;
                        d.points[0]=e.center;d.points[1]=Sketch::ellipsePoint(e,0);d.points[2]=Sketch::ellipsePoint(e,std::numbers::pi/2);
                        if(start) {d.points[3]=Sketch::ellipsePoint(e,*start);d.points[4]=Sketch::ellipsePoint(e,*end);}
                        session.sketch->after.push_back(d.candidate);session.valid=true;
                    }
                }
            }
            if(n<=2 && n>0 && e.majorRadius>1.e-9) {
                auto axis=d.candidate;axis.geometry=Sketch::Segment{e.center,Sketch::ellipsePoint(e,0)};present(axis,d.candidate.id+":axis");
            }
        }
        session.sketchDisplayCandidates=candidates;
        for(const auto& entity:candidates) {
            auto fresh=sketchPresentation(entity,*constructionObject(activeSketchWorkplaneId_),true);
            auto [it,inserted]=sketchPreviews_.try_emplace(entity.id,fresh);
            if(!inserted) {it->second->SetShape(fresh->Shape());it->second->SetAttributes(fresh->Attributes());context_->Redisplay(it->second,Standard_False);}
            displaySketchPresentation(entity,*constructionObject(activeSketchWorkplaneId_),it->second,true);shown.insert(entity.id);
        }
    } catch(const Standard_Failure& failure) {session.sketchDisplayCandidates.clear();error=QString::fromUtf8(failure.GetMessageString());session.valid=false;session.sketch->after=session.sketch->before;}
    for(auto it=sketchPreviews_.begin();it!=sketchPreviews_.end();) {
        if(!shown.contains(it->first)) {context_->Remove(it->second,Standard_False);it=sketchPreviews_.erase(it);}else ++it;
    }
    session.state=session.valid?CommandSession::State::Preview:CommandSession::State::Parameters;
    if(usable && !numeric) p->setEllipseValues(e,start.value_or(0),sweep.value_or(p->sweepParameter->value()*radians));
    if(d.snap && error.isEmpty()) error=d.snap->label;
    p->setStep(int(n),d.complete,session.valid,error);updateSketchFeedback();
    if(n==0 && input) showSketchMarker(sketchAnchorMarker_,e.center,Quantity_Color(0.1,0.65,1,Quantity_TOC_RGB));
    else if(start && n>=3) showSketchMarker(sketchAnchorMarker_,Sketch::ellipsePoint(e,*start),Quantity_Color(0.1,0.65,1,Quantity_TOC_RGB));
    updateConstructionLabels();view_->Redraw();
}
