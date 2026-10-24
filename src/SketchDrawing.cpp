#include "CadViewport.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QFontInfo>
#include <QMenu>
#include <QLocale>
#include <QSignalBlocker>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <Prs3d_PointAspect.hxx>
#include <Prs3d_Drawer.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Aspect_TypeOfDisplayText.hxx>
#include <algorithm>
#include <cmath>

namespace {
constexpr double pi=3.14159265358979323846,turn=2*pi;
double distance(Sketch::Point a,Sketch::Point b) {return std::hypot(a.x-b.x,a.y-b.y);}
Sketch::Point polar(Sketch::Point center,double radius,double angle) {return {center.x+radius*std::cos(angle),center.y+radius*std::sin(angle)};}
}
bool CadViewport::activateSketchWorkplane(const QString& requested,bool alignView) {
    QString id=requested;
    if(id.isEmpty()) {
        for(const auto& selected:selectedConstructionIds_) if(const auto* o=constructionObject(selected);o && o->kind==Construction::Kind::Workplane && o->visible && o->selectable) {
            if(!id.isEmpty()) {emit statusMessage(tr("Select one Workplane for 2D editing."));return false;}id=selected;
        }
        if(id.isEmpty()) for(const auto& o:constructions_) if(o.kind==Construction::Kind::Workplane && o.visible && o.selectable) {
            if(!id.isEmpty()) {emit statusMessage(tr("Choose a Workplane in the 2D toolbar or Structure Browser."));return false;}id=o.id;
        }
    }
    const auto* owner=constructionObject(id);
    if(!owner || owner->kind!=Construction::Kind::Workplane || !owner->visible || !owner->selectable) {
        emit statusMessage(tr("Create or select a visible, unlocked Workplane first (Modeling → Construction Geometry → Workplane)."));return false;
    }
    cancelCommandSession();initializeViewer();clearHistorySelection();
    activeSketchWorkplaneId_=id;refreshSketchPicking();
    emit activeSketchWorkplaneChanged(id);if(alignView) lookNormalToSketch();
    emit statusMessage(tr("2D editing: %1. Choose Line, Circle, Arc, Ellipse, Elliptical Arc or Infinite Line. Finish 2D returns to 3D picking.").arg(owner->name));return true;
}
void CadViewport::endSketchEditing() {
    if(activeSketchWorkplaneId_.isEmpty()) return;
    cancelCommandSession();clearSketchFeedback();clearSketchSelection();activeSketchWorkplaneId_.clear();
    refreshSketchPicking(true);emit activeSketchWorkplaneChanged({});
    if(!view_.IsNull()) view_->Redraw();
}
void CadViewport::validateActiveSketchContext() {
    if(activeSketchWorkplaneId_.isEmpty()) return;
    const auto* owner=constructionObject(activeSketchWorkplaneId_);
    if(!owner || owner->kind!=Construction::Kind::Workplane || !owner->visible || !owner->selectable) endSketchEditing();
    else {refreshSketchPicking();emit activeSketchWorkplaneChanged(activeSketchWorkplaneId_);}
}
void CadViewport::refreshSketchPicking(bool restore3D) {
    if(context_.IsNull() || (activeSketchWorkplaneId_.isEmpty() && !restore3D)) return;
    if(commandSession_ && commandSession_->projection) {configureSketchProjectionPicking();return;}
    const bool editing=!activeSketchWorkplaneId_.isEmpty();
    for(const auto& part:boxes_) if(part.active && part.visible) {
        context_->Deactivate(part.presentation);
        if(!editing) context_->Activate(part.presentation,selectionMode_==SelectionMode::Part?0:AIS_Shape::SelectionMode(TopAbs_FACE),Standard_False);
    }
    for(const auto& o:constructions_) if(constructionPresentations_.contains(o.id)) {
        auto p=constructionPresentations_.at(o.id);context_->Deactivate(p);
        if(!editing && o.visible && o.selectable) context_->Activate(p,o.kind==Construction::Kind::Workplane?AIS_Shape::SelectionMode(TopAbs_EDGE):0,Standard_False);
    }
    for(const auto& e:sketchEntities_) if(sketchPresentations_.contains(e.id)) {
        auto p=sketchPresentations_.at(e.id);context_->Deactivate(p);
        if(sketchPickable(e)) context_->Activate(p,0,Standard_False);
    }
}
void CadViewport::lookNormalToSketch() {
    const auto* owner=constructionObject(activeSketchWorkplaneId_);if(!owner || view_.IsNull()) return;
    const auto center=Construction::displayCenter(*owner);const auto camera=view_->Camera();
    const double distance=std::max(1.0,camera->Distance());
    camera->SetEyeAndCenter(center.Translated(gp_Vec(owner->frame.Direction())*distance),center);
    camera->SetUp(owner->frame.YDirection());fitConstructions({owner->id});clearSketchFeedback();
}
void CadViewport::setSketchSnapPixels(int pixels) {sketchSnapPixels_=std::clamp(pixels,1,100);clearSketchFeedback();}
void CadViewport::setSketchSnapping(bool enabled) {sketchSnapping_=enabled;clearSketchFeedback();}
std::optional<Sketch::Point> CadViewport::sketchPlanePoint(const QPoint& pixel) const {
    const auto* owner=constructionObject(activeSketchWorkplaneId_);if(!owner || view_.IsNull()) return {};
    double x,y,z,vx,vy,vz;view_->ConvertWithProj(pixel.x(),pixel.y(),x,y,z,vx,vy,vz);
    const gp_Vec ray(vx,vy,vz),normal(owner->frame.Direction());const double denominator=ray.Dot(normal);
    if(std::abs(denominator)<1.e-10) return {};
    const gp_Pnt origin(x,y,z);const auto hit=origin.Translated(ray*(gp_Vec(origin,owner->frame.Location()).Dot(normal)/denominator));
    const gp_Vec local(owner->frame.Location(),hit);
    Sketch::Point point{local.Dot(gp_Vec(owner->frame.XDirection())),local.Dot(gp_Vec(owner->frame.YDirection()))};
    if(!std::isfinite(point.x) || !std::isfinite(point.y)) return {};
    return point;
}
void CadViewport::startSketchDrawing(Sketch::DrawTool tool,Sketch::Role role) {
    if(activeSketchWorkplaneId_.isEmpty() && !activateSketchWorkplane()) return;
    const auto* owner=constructionObject(activeSketchWorkplaneId_);if(!owner) return;
    cancelCommandSession();clearHistorySelection();refreshSketchPicking();
    commandSession_.emplace();auto& session=*commandSession_;session.kind=CommandSession::Kind::Sketch;
    session.sketch=std::make_shared<SketchChange>(SketchChange{sketchEntities_,sketchEntities_});
    session.drawing=std::make_shared<SketchDrawingState>();auto& drawing=*session.drawing;
    drawing.tool=tool;drawing.candidate.workplaneId=owner->id;
    drawing.candidate.role=tool==Sketch::DrawTool::InfiniteLine?Sketch::Role::Construction:role;
    session.valid=false;session.state=CommandSession::State::Selection;
    for(const auto& target:Sketch::snapTargets(sketchEntities_,owner->id,{})) if(target.kind==Sketch::SnapKind::Intersection) drawing.intersections.push_back(target);
    sketchDrawingPanel_=new SketchDrawingPanel(tool,drawing.candidate.role,owner->name,this);
    sketchDrawingPanel_->useXY=[this]{useSketchNumeric(false);};sketchDrawingPanel_->usePolar=[this]{useSketchNumeric(true);};
    sketchDrawingPanel_->back=[this]{backSketchPoint();};sketchDrawingPanel_->confirmed=[this]{confirmCommandSession();};
    sketchDrawingPanel_->cancelled=[this]{cancelCommandSession();};
    sketchDrawingPanel_->roleChanged=[this] {
        if(!commandSession_ || !commandSession_->drawing || !sketchDrawingPanel_) return;
        commandSession_->drawing->candidate.role=static_cast<Sketch::Role>(sketchDrawingPanel_->role->currentData().toInt());
        refreshSketchDrawing();
    };
    sketchDrawingPanel_->directionChanged=[this] {
        if(!commandSession_ || !commandSession_->drawing) return;
        if(Sketch::ellipseTool(commandSession_->drawing->tool)) {changeSketchEllipseDirection();return;}
        // Keep the chosen endpoints. The other direction is the complementary
        // arc between them, not the same signed amount with a moved endpoint.
        commandSession_->drawing->sweep.reset();refreshSketchDrawing();
    };
    sketchDrawingPanel_->ellipseParametersChanged=[this]{editSketchEllipseParameters();};
    showCommandPanel(sketchDrawingPanel_);refreshSketchDrawing();setFocus();
}
bool CadViewport::sketchDrawingMove(const QPoint& pixel,Qt::KeyboardModifiers modifiers) {
    if(!commandSession_ || !commandSession_->drawing) return false;
    auto& d=*commandSession_->drawing;if(d.complete) return true;
    d.snap.reset();d.cursor=sketchPlanePoint(pixel);
    if(!d.cursor) {clearSketchFeedback();sketchDrawingPanel_->setStep(int(d.points.size()),false,false,tr("Workplane is edge-on; use Look Normal."));return true;}
    const auto* owner=constructionObject(activeSketchWorkplaneId_);
    if(sketchSnapping_ && !(modifiers&Qt::ShiftModifier)) {
        std::optional<Sketch::Point> anchor;
        if(!d.points.empty() && (d.tool==Sketch::DrawTool::Segment || d.tool==Sketch::DrawTool::InfiniteLine)) anchor=d.points.front();
        auto targets=Sketch::snapTargets(sketchEntities_,activeSketchWorkplaneId_,*d.cursor,anchor,false);
        targets.insert(targets.end(),d.intersections.begin(),d.intersections.end());
        if(d.tool==Sketch::DrawTool::Arc && d.points.size()==2) {
            const Sketch::Circle support{d.points[0],d.radius.value_or(distance(d.points[0],d.points[1]))};
            std::erase_if(targets,[&](const auto& target){return !Sketch::contains(support,target.point);});
        }
        if(d.tool==Sketch::DrawTool::EllipticalArc && d.points.size()>=3)
            std::erase_if(targets,[&](const auto& target){return !Sketch::contains(d.ellipse,target.point);});
        d.snap=Sketch::closestSnap(targets,{double(pixel.x()),double(pixel.y())},sketchSnapPixels_,[&](Sketch::Point p) {
            const auto world=Sketch::worldPoint(p,owner->frame);int x,y;view_->Convert(world.X(),world.Y(),world.Z(),x,y);return Sketch::Point{double(x),double(y)};
        });
        if(d.snap) d.cursor=d.snap->point;
    }
    refreshSketchDrawing();return true;
}
bool CadViewport::sketchDrawingClick(const QPoint& pixel,Qt::KeyboardModifiers modifiers) {
    if(!commandSession_ || !commandSession_->drawing) return false;
    if(commandSession_->drawing->complete) return true;
    sketchDrawingMove(pixel,modifiers);
    if(commandSession_->drawing->cursor) acceptSketchPoint(*commandSession_->drawing->cursor);
    return true;
}
void CadViewport::acceptSketchPoint(Sketch::Point point) {
    if(!commandSession_ || !commandSession_->drawing || !std::isfinite(point.x) || !std::isfinite(point.y)) return;
    auto& d=*commandSession_->drawing;if(d.complete) return;
    if(Sketch::ellipseTool(d.tool)) {acceptSketchEllipsePoint(point);return;}
    if(!d.points.empty()) {
        const auto anchor=d.tool==Sketch::DrawTool::Arc?d.points[0]:d.points.back();
        if(distance(anchor,point)<=1.e-7) {sketchDrawingPanel_->setStep(int(d.points.size()),false,false,tr("Choose a distinct point."));return;}
    }
    d.points.push_back(point);d.cursor.reset();d.snap.reset();
    d.complete=d.points.size()==(d.tool==Sketch::DrawTool::Arc?3:2);refreshSketchDrawing();
}
void CadViewport::useSketchNumeric(bool usePolar) {
    if(!commandSession_ || !commandSession_->drawing || !sketchDrawingPanel_) return;
    auto& d=*commandSession_->drawing;if(d.complete) return;
    sketchDrawingPanel_->finishEditing();d.snap.reset();
    if(Sketch::ellipseTool(d.tool)) {if(usePolar) useSketchEllipseNumeric();else acceptSketchEllipsePoint({sketchDrawingPanel_->x->value(),sketchDrawingPanel_->y->value()});return;}
    if(!usePolar) {acceptSketchPoint({sketchDrawingPanel_->x->value(),sketchDrawingPanel_->y->value()});return;}
    if(d.points.empty()) return;
    const double length=sketchDrawingPanel_->distance->value(),angle=sketchDrawingPanel_->angle->value()*pi/180;
    if(d.tool==Sketch::DrawTool::Arc && d.points.size()==2) {
        if(std::abs(angle)<=1.e-12 || std::abs(angle)>=turn) {sketchDrawingPanel_->setStep(2,false,false,tr("Enter a nonzero sweep smaller than 360 degrees."));return;}
        const double start=d.startAngle.value_or(std::atan2(d.points[1].y-d.points[0].y,d.points[1].x-d.points[0].x));
        if(angle<0) {const QSignalBlocker block(sketchDrawingPanel_->direction);sketchDrawingPanel_->direction->setCurrentIndex(1);}
        const double sweep=(sketchDrawingPanel_->direction->currentIndex()==1?-1:1)*std::abs(angle);
        d.sweep=sweep;acceptSketchPoint(polar(d.points[0],d.radius.value_or(distance(d.points[0],d.points[1])),start+sweep));
    } else {
        if(d.tool==Sketch::DrawTool::Circle || d.tool==Sketch::DrawTool::Arc) {d.radius=length;d.startAngle=angle;}
        acceptSketchPoint(polar(d.points[0],length,angle));
    }
}
void CadViewport::backSketchPoint() {
    if(!commandSession_ || !commandSession_->drawing) return;
    auto& d=*commandSession_->drawing;if(!d.points.empty()) d.points.pop_back();
    d.complete=false;d.cursor.reset();d.snap.reset();d.sweep.reset();
    if(Sketch::ellipseTool(d.tool)) {
        d.ellipseEnd.reset();if(d.points.size()<4) d.ellipseStart.reset();
        sketchDrawingPanel_->resetEllipseEdits();
        sketchDrawingPanel_->setEllipseValues(d.ellipse,d.ellipseStart.value_or(0),(sketchDrawingPanel_->direction->currentIndex()==1?-1:1)*pi/2,true);
        refreshSketchDrawing();return;
    }
    if(d.points.size()<2) {d.radius.reset();d.startAngle.reset();}
    refreshSketchDrawing();
}
void CadViewport::refreshSketchDrawing() {
    if(!commandSession_ || !commandSession_->drawing || !sketchDrawingPanel_) return;
    if(Sketch::ellipseTool(commandSession_->drawing->tool)) {refreshSketchEllipseDrawing();return;}
    auto& session=*commandSession_;auto& d=*session.drawing;auto points=d.points;
    if(d.cursor && !d.complete) points.push_back(*d.cursor);
    for(const auto& [id,p]:sketchPreviews_) context_->Remove(p,Standard_False);
    sketchPreviews_.clear();session.sketch->after=session.sketch->before;session.valid=false;QString error;
    if(points.size()>=2) {
        const auto a=points[0],b=points[1];const double size=distance(a,b);
        if(size>1.e-7) {
            if(d.tool==Sketch::DrawTool::Segment) d.candidate.geometry=Sketch::Segment{a,b};
            if(d.tool==Sketch::DrawTool::InfiniteLine) d.candidate.geometry=Sketch::InfiniteLine{a,{(b.x-a.x)/size,(b.y-a.y)/size}};
            if(d.tool==Sketch::DrawTool::Circle) d.candidate.geometry=Sketch::Circle{a,d.radius.value_or(size)};
            if(d.tool==Sketch::DrawTool::Arc && points.size()>=3) {
                const double start=d.startAngle.value_or(std::atan2(b.y-a.y,b.x-a.x)),end=std::atan2(points[2].y-a.y,points[2].x-a.x);
                const bool clockwise=sketchDrawingPanel_->direction->currentIndex()==1;
                const double sweep=d.sweep.value_or(Sketch::directedArcSweep(start,end,clockwise));
                d.candidate.geometry=Sketch::Arc{a,d.radius.value_or(size),start,sweep};
            } else if(d.tool==Sketch::DrawTool::Arc) {
                // Show its support circle while choosing the start and end; never store it as the arc result.
                d.candidate.geometry=Sketch::Circle{a,d.radius.value_or(size)};
            }
            if(Sketch::valid(d.candidate,error)) {
                try {
                    auto p=sketchPresentation(d.candidate,*constructionObject(activeSketchWorkplaneId_),true);
                    sketchPreviews_[d.candidate.id]=p;displaySketchPresentation(d.candidate,*constructionObject(activeSketchWorkplaneId_),p,true);
                    if(d.complete) {session.sketch->after.push_back(d.candidate);session.valid=true;}
                } catch(const Standard_Failure& e) {error=QString::fromUtf8(e.GetMessageString());}
            }
        } else error=tr("Choose distinct points.");
    }
    session.state=session.valid?CommandSession::State::Preview:CommandSession::State::Parameters;
    QString feedback=error;
    if(d.cursor) {
        const auto a=d.points.empty()?*d.cursor:d.points.front();
        const double length=d.tool==Sketch::DrawTool::Arc && d.points.size()==2
            ?d.radius.value_or(distance(a,d.points[1])):distance(a,*d.cursor);
        const double angle=std::atan2(d.cursor->y-a.y,d.cursor->x-a.x)*180/pi;
        double shownAngle=angle;
        if(d.tool==Sketch::DrawTool::Arc && d.points.size()==2) {
            const double start=d.startAngle.value_or(std::atan2(d.points[1].y-a.y,d.points[1].x-a.x));
            shownAngle=Sketch::directedArcSweep(start,angle*pi/180,sketchDrawingPanel_->direction->currentIndex()==1)*180/pi;
        }
        sketchDrawingPanel_->setCursor(*d.cursor,length,shownAngle);
        if(d.snap) feedback=d.snap->label;
    }
    if(d.complete && d.tool==Sketch::DrawTool::Arc) if(const auto* arc=std::get_if<Sketch::Arc>(&d.candidate.geometry)) sketchDrawingPanel_->setArcSweep(arc->sweepAngle*180/pi);
    sketchDrawingPanel_->setStep(int(d.points.size()),d.complete,session.valid,feedback);updateSketchFeedback();view_->Redraw();
}
void CadViewport::updateSketchFeedback() {
    if(!commandSession_ || !commandSession_->drawing) return;
    const auto& d=*commandSession_->drawing;const auto* owner=constructionObject(activeSketchWorkplaneId_);if(!owner) return;
    if(!d.points.empty()) showSketchMarker(sketchAnchorMarker_,d.points.front(),Quantity_Color(0.1,0.65,1,Quantity_TOC_RGB));
    else if(!sketchAnchorMarker_.IsNull()) context_->Erase(sketchAnchorMarker_,Standard_False);
    if(d.snap) showSketchMarker(sketchSnapMarker_,d.snap->point,Quantity_Color(Quantity_NOC_ORANGE));
    else if(!sketchSnapMarker_.IsNull()) context_->Erase(sketchSnapMarker_,Standard_False);
    if(!d.cursor) {if(!sketchCursorLabel_.IsNull()) context_->Erase(sketchCursorLabel_,Standard_False);return;}
    if(sketchCursorLabel_.IsNull()) {
        sketchCursorLabel_=new AIS_TextLabel;sketchCursorLabel_->SetHeight(13);sketchCursorLabel_->SetZoomable(false);
        sketchCursorLabel_->SetFont(QFontInfo(QFontDatabase::systemFont(QFontDatabase::GeneralFont)).family().toUtf8().constData());
        sketchCursorLabel_->SetZLayer(Graphic3d_ZLayerId_Topmost);sketchCursorLabel_->SetInfiniteState(true);sketchCursorLabel_->SetDisplayType(Aspect_TODT_SUBTITLE);
        sketchCursorLabel_->SetColorSubTitle(Quantity_Color(Quantity_NOC_GRAY20));
    }
    const auto world=Sketch::worldPoint(*d.cursor,owner->frame);const auto camera=view_->Camera();
    const auto right=gp_Vec(camera->Direction()).Crossed(gp_Vec(camera->Up())).Normalized();
    sketchCursorLabel_->SetPosition(world.Translated((right+gp_Vec(camera->Up()))*view_->Convert(12)));
    QString text=QString("%1X %2  Y %3").arg(d.snap?d.snap->label+" | ":QString()).arg(d.cursor->x,0,'f',6).arg(d.cursor->y,0,'f',6);
    if(!d.points.empty() && Sketch::ellipseTool(d.tool)) {
        auto support=d.ellipse;
        if(const auto* e=std::get_if<Sketch::Ellipse>(&d.candidate.geometry)) support=*e;
        if(const auto* a=std::get_if<Sketch::EllipticalArc>(&d.candidate.geometry)) support=a->ellipse;
        text+=QString(" | a %1  b %2  Axis %3°").arg(support.majorRadius,0,'f',6).arg(support.minorRadius,0,'f',6).arg(support.axisAngle*180/pi,0,'f',4);
        if(const auto* a=std::get_if<Sketch::EllipticalArc>(&d.candidate.geometry)) text+=QString(" | Sweep %1°").arg(a->sweepParameter*180/pi,0,'f',4);
    } else if(!d.points.empty()) {
        const bool arcEnd=d.tool==Sketch::DrawTool::Arc && d.points.size()==2;
        const double size=arcEnd?d.radius.value_or(distance(d.points[0],d.points[1])):distance(d.points[0],*d.cursor);
        double angle=std::atan2(d.cursor->y-d.points[0].y,d.cursor->x-d.points[0].x);
        if(arcEnd) {
            const double start=d.startAngle.value_or(std::atan2(d.points[1].y-d.points[0].y,d.points[1].x-d.points[0].x));
            angle=Sketch::directedArcSweep(start,angle,sketchDrawingPanel_->direction->currentIndex()==1);
        }
        text+=QString(" | %1 %2  %3 %4°").arg(d.tool==Sketch::DrawTool::Circle || d.tool==Sketch::DrawTool::Arc?"R":"L")
            .arg(size,0,'f',6).arg(arcEnd?"Sweep":"Angle").arg(angle*180/pi,0,'f',4);
    }
    sketchCursorLabel_->SetText(TCollection_ExtendedString(text.toStdU16String().c_str()));
    sketchCursorLabel_->SetColor(d.snap?Quantity_Color(Quantity_NOC_ORANGE):Quantity_Color(0.1,0.65,1,Quantity_TOC_RGB));
    context_->Display(sketchCursorLabel_,0,-1,Standard_False);context_->Deactivate(sketchCursorLabel_);context_->Redisplay(sketchCursorLabel_,Standard_False);
}
void CadViewport::showSketchMarker(Handle(AIS_Shape)& presentation,Sketch::Point point,const Quantity_Color& color) {
    const auto* owner=constructionObject(activeSketchWorkplaneId_);if(!owner) return;
    const auto shape=BRepBuilderAPI_MakeVertex(Sketch::worldPoint(point,owner->frame)).Shape();
    if(presentation.IsNull()) {presentation=new AIS_Shape(shape);presentation->SetZLayer(Graphic3d_ZLayerId_Topmost);presentation->SetInfiniteState(true);}
    else presentation->SetShape(shape);
    presentation->Attributes()->SetPointAspect(new Prs3d_PointAspect(Aspect_TOM_O_PLUS,color,8));
    context_->Display(presentation,Standard_False);context_->Deactivate(presentation);context_->Redisplay(presentation,Standard_False);
}
void CadViewport::clearSketchFeedback() {
    if(context_.IsNull()) return;
    for(auto* p:{&sketchSnapMarker_,&sketchAnchorMarker_}) if(!p->IsNull()) {context_->Remove(*p,Standard_False);p->Nullify();}
    if(!sketchCursorLabel_.IsNull()) {context_->Remove(sketchCursorLabel_,Standard_False);sketchCursorLabel_.Nullify();}
    if(commandSession_ && commandSession_->drawing) commandSession_->drawing->snap.reset();
}
void CadViewport::closeSketchDrawingPanel() {
    if(!sketchDrawingPanel_) return;
    auto* panel=sketchDrawingPanel_;sketchDrawingPanel_=nullptr;
    panel->confirmed={};panel->cancelled={};panel->useXY={};panel->usePolar={};panel->back={};panel->roleChanged={};panel->directionChanged={};panel->ellipseParametersChanged={};panel->hide();panel->deleteLater();
}
