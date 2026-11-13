#include "CadViewport.h"
#include "MainWindow.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QWheelEvent>
#include <BRepAdaptor_Curve.hxx>
#include <TopoDS.hxx>
#include <cmath>
#include <stdexcept>

namespace {
constexpr double pi=3.14159265358979323846;
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
bool close(double a,double b,double tolerance=1.e-9) {return std::abs(a-b)<tolerance;}
void key(QWidget* widget,int code) {QKeyEvent event(QEvent::KeyPress,code,Qt::NoModifier);QApplication::sendEvent(widget,&event);}
void mouse(CadViewport& v,QEvent::Type type,QPoint point,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
    const bool move=type==QEvent::MouseMove;
    QMouseEvent event(type,QPointF(point),QPointF(v.mapToGlobal(point)),move?Qt::NoButton:Qt::LeftButton,
        type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,modifiers);
    QApplication::sendEvent(&v,&event);
}
void click(CadViewport& v,QPoint point,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
    mouse(v,QEvent::MouseButtonPress,point,modifiers);mouse(v,QEvent::MouseButtonRelease,point,modifiers);
}
}
struct SketchDrawingTest {
    static Sketch::Point arcPoint(const Sketch::Arc& arc,double fraction) {
        const double angle=arc.startAngle+fraction*arc.sweepAngle;return {arc.center.x+arc.radius*std::cos(angle),arc.center.y+arc.radius*std::sin(angle)};
    }
    static void checkPreview(CadViewport& v,const Sketch::Arc& arc) {
        check(v.sketchPreviews_.size()==1,"Arc preview missing");const auto edge=TopoDS::Edge(v.sketchPreviews_.begin()->second->Shape());
        BRepAdaptor_Curve curve(edge);const auto& frame=v.constructionObject(v.activeSketchWorkplaneId_)->frame;
        for(double t:{0.0,0.25,0.5,0.75,1.0}) {
            const double fraction=edge.Orientation()==TopAbs_REVERSED?1-t:t;
            const auto point=curve.Value(curve.FirstParameter()+fraction*(curve.LastParameter()-curve.FirstParameter()));
            check(point.Distance(Sketch::worldPoint(arcPoint(arc,t),frame))<1.e-7,"displayed Arc does not match signed model/domain");
        }
    }
    static void arcDirections() {
        MainWindow w;w.resize(1400,1000);w.show();QApplication::processEvents();auto& v=*w.viewport_;QString error;
        check(v.importDocumentSnapshot(fixture(),error),qPrintable(error));(v.activateSketchWorkplane("wp1")&&v.lookAtWorkplane("wp1"));v.setSketchSnapping(false);w.documentDirty_=false;
        struct Case {double start,end,sweep;bool clockwise;};
        for(const auto c:std::vector<Case>{{350,10,20,false},{350,10,-340,true},{10,350,-20,true},{10,350,340,false},{30,150,120,false},{30,150,-240,true}}) {
            const auto before=v.sketchEntities_;const auto history=v.undoStack_.size();
            v.startSketchDrawing(Sketch::DrawTool::Arc,Sketch::Role::Construction);xy(v,12,-8);polar(v,40,c.start);
            auto* panel=v.sketchDrawingPanel_;panel->direction->setCurrentIndex(c.clockwise?1:0);
            Sketch::Arc expected{{12,-8},40,c.start*pi/180,c.sweep*pi/180};const auto end=arcPoint(expected,1);
            mouse(v,QEvent::MouseMove,pixel(v,end));
            check(std::get<Sketch::Arc>(v.commandSession_->drawing->candidate.geometry).sweepAngle*(c.clockwise?-1:1)>0,"live Arc ignored selected direction");
            xy(v,end.x,end.y);check(v.commandSession_->valid,"exact endpoint Arc not confirmable");
            auto actual=std::get<Sketch::Arc>(v.commandSession_->drawing->candidate.geometry);
            check(close(actual.sweepAngle,expected.sweepAngle) && close(actual.startAngle,expected.startAngle),"CW/CCW or short/long seam Arc wrong");checkPreview(v,actual);
            const auto startBefore=arcPoint(actual,0),endBefore=arcPoint(actual,1);
            panel->direction->setCurrentIndex(c.clockwise?0:1);
            auto opposite=std::get<Sketch::Arc>(v.commandSession_->drawing->candidate.geometry);
            check(close(std::abs(actual.sweepAngle)+std::abs(opposite.sweepAngle),2*pi)
                && std::hypot(arcPoint(opposite,0).x-startBefore.x,arcPoint(opposite,0).y-startBefore.y)<1.e-8
                && std::hypot(arcPoint(opposite,1).x-endBefore.x,arcPoint(opposite,1).y-endBefore.y)<1.e-8,"direction change moved endpoints or chose wrong complement");checkPreview(v,opposite);
            panel->direction->setCurrentIndex(c.clockwise?1:0);confirm(v);
            const auto confirmed=v.sketchEntities_;check(confirmed.size()==before.size()+1 && v.undoStack_.size()==history+1,"Arc commit not one Undo step");
            v.selectSketchEntities({confirmed.back().id});v.undoPull();check(v.sketchEntities_==before && v.sketchSelection().empty(),"Arc Undo incorrect");v.redoPull();
            check(v.sketchEntities_==confirmed && v.context_->NbSelected()==0,"Arc Redo lost signed values or retained selection");
        }
        // A positive numeric amount obeys the explicit direction. Legacy negative
        // amounts select CW, and changing role must preserve the exact signed value.
        for(const auto values:std::vector<std::pair<bool,double>>{{true,45.123456789},{false,270.123456789},{false,-35.123456789}}) {
            v.startSketchDrawing(Sketch::DrawTool::Arc,Sketch::Role::Construction);xy(v,0,0);polar(v,25,350);
            auto* p=v.sketchDrawingPanel_;p->direction->setCurrentIndex(values.first?1:0);polar(v,25,values.second);
            const double expected=(values.first || values.second<0?-1:1)*std::abs(values.second)*pi/180;
            const auto arc=std::get<Sketch::Arc>(v.commandSession_->drawing->candidate.geometry);
            check(close(arc.sweepAngle,expected,1.e-12) && p->direction->currentIndex()==(expected<0?1:0),"numeric sweep overrode CW/CCW direction");
            p->role->setCurrentIndex(p->role->findData(int(Sketch::Role::Profile)));p->role->setCurrentIndex(p->role->findData(int(Sketch::Role::Construction)));
            check(std::get<Sketch::Arc>(v.commandSession_->drawing->candidate.geometry)==arc,"role toggle changed exact numeric Arc");checkPreview(v,arc);confirm(v);
        }
        // Real mouse releases must stay in the 2D tool, without legacy 3D fields
        // stealing focus from the direction control or Overdraw window.
        const auto count=v.sketchEntities_.size();v.startSketchDrawing(Sketch::DrawTool::Arc,Sketch::Role::Construction);
        for(const auto point:{Sketch::Point{-50,-50},Sketch::Point{-20,-50},Sketch::Point{-50,-20}}) {
            click(v,pixel(v,point));check(!v.commandPanel_->isVisible() && v.sketchDrawingPanel_->isVisible(),"Arc mouse release opened legacy 3D panel");
        }
        check(v.commandSession_->valid,"mouse Arc not complete");v.sketchDrawingPanel_->direction->setCurrentIndex(1);
        check(std::get<Sketch::Arc>(v.commandSession_->drawing->candidate.geometry).sweepAngle<0,"completed mouse Arc direction cannot change");
        key(v.sketchDrawingPanel_->direction,Qt::Key_Escape);check(v.sketchEntities_.size()==count && !v.commandSession_ && v.sketchPreviews_.empty(),"Arc Cancel left partial geometry");
        QTemporaryDir dir;NativeDocumentSnapshot saved,loaded;v.exportDocumentSnapshot(saved);
        check(NativeDocumentStorage::save(dir.filePath("arc-directions.odc"),saved,error) && NativeDocumentStorage::load(dir.filePath("arc-directions.odc"),loaded,error)
            && loaded.sketchEntities==saved.sketchEntities,"CW/CCW Arc roundtrip changed exact values");w.documentDirty_=false;
    }
    static NativeDocumentSnapshot fixture() {
        NativeDocumentSnapshot s;Construction::Object a;a.id="wp1";a.name="WP1";a.kind=Construction::Kind::Workplane;
        a.frame=gp_Ax3(gp_Pnt(23,37,80),gp_Dir(1,2,3),gp_Dir(2,-1,0));a.extent=200;
        auto b=a;b.id="wp2";b.name="WP2";b.frame.SetLocation(gp_Pnt(90,80,70));b.displayOffset=gp_Vec(30,40,0);
        s.constructions={a,b};return s;
    }
    static void action(MainWindow& w,const char* name) {
        auto* a=w.findChild<QAction*>(name);check(a && a->isEnabled(),name);a->trigger();QApplication::processEvents();
    }
    static void xy(CadViewport& v,double x,double y) {
        auto* p=v.sketchDrawingPanel_;check(p,"no drawing editor");p->x->setValue(x);p->y->setValue(y);p->xyButton->click();
    }
    static void polar(CadViewport& v,double length,double angle) {
        auto* p=v.sketchDrawingPanel_;p->distance->setValue(length);p->angle->setValue(angle);p->polarButton->click();
    }
    static void confirm(CadViewport& v) {
        auto* p=v.sketchDrawingPanel_;check(p,"drawing editor disappeared");
        auto* b=p->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);check(b->isEnabled(),"valid element cannot confirm");b->click();
    }
    static QPoint pixel(CadViewport& v,Sketch::Point local) {
        const auto world=Sketch::worldPoint(local,v.constructionObject(v.activeSketchWorkplaneId_)->frame);
        int x,y;v.view_->Convert(world.X(),world.Y(),world.Z(),x,y);return {x,y};
    }
    static void run() {
        MainWindow w;w.resize(1400,1000);w.show();QApplication::processEvents();auto& v=*w.viewport_;QString error;
        const auto base=fixture();check(v.importDocumentSnapshot(base,error),qPrintable(error));w.documentDirty_=false;
        check(w.findChild<QComboBox*>("sketchWorkplane")->count()==3,"Workplane chooser missing");
        check(!v.activateSketchWorkplane(),"ambiguous Workplane silently chosen");
        v.selectConstructions({"wp1"});const gp_Dir viewBefore=v.view_->Camera()->Direction();action(w,"sketch.edit");
        check(v.activeSketchWorkplaneId_=="wp1" && !w.documentDirty_,"activation dirtied document");
        // v0.5.3: activation never moves the camera; Look at Workplane is explicit.
        check(v.view_->Camera()->Direction().IsEqual(viewBefore,1.e-12),"Edit Sketch rotated the camera");
        action(w,"sketch.normal");check(!w.documentDirty_,"Look at Workplane dirtied document");
        check(v.view_->Camera()->Direction().Dot(base.constructions[0].frame.Direction())<-0.999999,"camera not normal to WP");
        check(w.constructionRoot_->child(0)->font(0).bold(),"active WP not identified in browser");
        w.findChild<QSpinBox*>("sketchSnapPixels")->setValue(14);check(v.sketchSnapPixels()==14,"pixel setting ignored");
        action(w,"sketch.line");check(!v.commandSession_->valid,"empty tool valid");
        v.confirmCommandSession();check(v.commandSession_.has_value() && v.undoStack_.empty(),"incomplete command committed");
        // Enter on a coordinate uses XY, then Enter on a dimension uses length/angle.
        auto* panel=v.sketchDrawingPanel_;panel->x->setValue(1.234567891);panel->y->setValue(-9.876543219);
        key(panel->x,Qt::Key_Return);check(v.commandSession_->drawing->points.size()==1,"Enter failed to accept XY");
        panel->distance->setValue(12.5);panel->angle->setValue(30);key(panel->angle,Qt::Key_Return);
        check(v.commandSession_->valid && v.sketchPreviews_.size()==1 && v.sketchEntities_.empty() && !w.documentDirty_,"preview mutated document");
        NativeDocumentSnapshot pending;v.exportDocumentSnapshot(pending);check(pending.sketchEntities.empty(),"Save included pending drawing");
        key(panel->angle,Qt::Key_Return);check(v.sketchEntities_.size()==1 && v.undoStack_.size()==1 && w.documentDirty_,"confirmation not one history step");
        auto segment=std::get<Sketch::Segment>(v.sketchEntities_[0].geometry);
        check(segment.start==Sketch::Point{1.234567891,-9.876543219} && close(segment.end.x-segment.start.x,12.5*std::cos(pi/6))
            && close(segment.end.y-segment.start.y,6.25),"numeric coordinates/length lost precision");
        check(w.constructionRoot_->child(0)->childCount()==1,"browser not rebuilt after drawing");
        v.selectSketchEntities({v.sketchEntities_[0].id});v.undoPull();
        check(v.sketchEntities_.empty() && v.sketchSelection().empty() && v.activeSketchWorkplaneId_=="wp1","Undo damaged editing context");
        v.redoPull();check(v.sketchEntities_.size()==1 && v.context_->NbSelected()==0,"Redo restored selection");
        // Cancel after a complete preview preserves document and Redo.
        v.undoPull();const auto redo=v.redoStack_.size();w.documentDirty_=false;
        action(w,"sketch.circle");xy(v,5,6);polar(v,7,0);key(v.sketchDrawingPanel_->x,Qt::Key_Escape);
        check(!v.commandSession_ && v.sketchPreviews_.empty() && v.sketchEntities_.empty() && v.redoStack_.size()==redo
            && !w.documentDirty_ && v.sketchSnapMarker_.IsNull(),"Cancel left geometry/history/feedback");v.redoPull();
        // Every supported type and role uses the same confirmed collection.
        action(w,"sketch.construction");action(w,"sketch.circle");xy(v,30,20);polar(v,7.123456789,0);confirm(v);
        check(std::get<Sketch::Circle>(v.sketchEntities_.back().geometry).radius==7.123456789
            && v.sketchEntities_.back().role==Sketch::Role::Construction,"construction circle lost exact radius/role");
        action(w,"sketch.line");xy(v,-20,40);polar(v,15,90);confirm(v);
        check(v.sketchEntities_.back().role==Sketch::Role::Construction,"finite construction line unavailable");
        action(w,"sketch.infinite");xy(v,0,-20);polar(v,10,45);confirm(v);
        check(std::holds_alternative<Sketch::InfiniteLine>(v.sketchEntities_.back().geometry),"infinite line not stored analytically");
        action(w,"sketch.construction");action(w,"sketch.circle");xy(v,50,50);polar(v,9,0);confirm(v);
        check(v.sketchEntities_.back().role==Sketch::Role::Profile,"profile circle role wrong");
        action(w,"sketch.arc");xy(v,10,10);polar(v,12.123456789,25.123456789);polar(v,12,-120.123456789);confirm(v);
        auto arc=std::get<Sketch::Arc>(v.sketchEntities_.back().geometry);
        check(arc.radius==12.123456789 && close(arc.startAngle,25.123456789*pi/180)
            && close(arc.sweepAngle,-120.123456789*pi/180),"arc numeric radius/start/signed sweep changed");
        action(w,"sketch.construction");action(w,"sketch.arc");xy(v,0,0);polar(v,20,0);
        v.sketchDrawingPanel_->direction->setCurrentIndex(1);v.setSketchSnapping(false);
        v.setFocus();mouse(v,QEvent::MouseMove,pixel(v,{0,80}));
        check(close(v.sketchDrawingPanel_->distance->value(),20),"arc-end feedback changed fixed radius");
        click(v,pixel(v,{0,20}));check(v.commandSession_->valid,"mouse arc did not complete");confirm(v);
        arc=std::get<Sketch::Arc>(v.sketchEntities_.back().geometry);
        check(close(arc.sweepAngle,-1.5*pi,0.03) && v.sketchEntities_.back().role==Sketch::Role::Construction,"clockwise construction arc wrong");
        // A Workplane switch cancels pending geometry and updates all ownership/UI state.
        const auto count=v.sketchEntities_.size(),history=v.undoStack_.size();w.documentDirty_=false;
        action(w,"sketch.line");xy(v,0,0);polar(v,20,0);
        auto* chooser=w.findChild<QComboBox*>("sketchWorkplane");chooser->setCurrentIndex(chooser->findData("wp2"));
        check(v.activeSketchWorkplaneId_=="wp2" && !v.commandSession_ && v.sketchEntities_.size()==count
            && v.undoStack_.size()==history && !w.documentDirty_,"WP switch committed/lost geometry");
        action(w,"sketch.line");xy(v,0,0);polar(v,10,0);confirm(v);
        check(v.sketchEntities_.back().workplaneId=="wp2" && w.constructionRoot_->child(1)->childCount()==1,"wrong Workplane ownership");
        // Edge-on input is rejected, then Look Normal restores usable exact ray projection.
        action(w,"sketch.line");const auto& wp=base.constructions[1];const auto camera=v.view_->Camera();
        camera->SetEyeAndCenter(wp.frame.Location().Translated(gp_Vec(wp.frame.XDirection())*500),wp.frame.Location());camera->SetUp(wp.frame.Direction());
        mouse(v,QEvent::MouseMove,QPoint(v.width()/2,v.height()/2));check(!v.commandSession_->drawing->cursor,"edge-on WP accepted arbitrary point");
        action(w,"sketch.normal");mouse(v,QEvent::MouseMove,pixel(v,{15,25}));
        check(v.commandSession_->drawing->cursor.has_value(),"Look Normal did not restore drawing");v.cancelCommandSession();
        check(!v.sketchDrawingPanel_ && v.sketchCursorLabel_.IsNull(),"drawing controls/label leaked");
        action(w,"sketch.line");xy(v,3,4);w.documentDirty_=false;const auto navigationHistory=v.undoStack_.size();
        const QPoint middle(v.width()/2,v.height()/2);
        for(const auto modifiers:{Qt::KeyboardModifiers{},Qt::KeyboardModifiers{Qt::ShiftModifier}}) {
            QMouseEvent press(QEvent::MouseButtonPress,QPointF(middle),QPointF(v.mapToGlobal(middle)),Qt::MiddleButton,Qt::MiddleButton,modifiers);
            QApplication::sendEvent(&v,&press);mouse(v,QEvent::MouseMove,middle+QPoint(35,20),modifiers);
            QMouseEvent release(QEvent::MouseButtonRelease,QPointF(middle+QPoint(35,20)),QPointF(v.mapToGlobal(middle+QPoint(35,20))),Qt::MiddleButton,Qt::NoButton,modifiers);
            QApplication::sendEvent(&v,&release);
        }
        const double beforeZoom=v.view_->Scale();
        QWheelEvent wheel(QPointF(middle),QPointF(v.mapToGlobal(middle)),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(&v,&wheel);
        check(v.view_->Scale()!=beforeZoom && v.commandSession_ && v.commandSession_->drawing->points.size()==1
            && v.undoStack_.size()==navigationHistory && !w.documentDirty_,"navigation confirmed/dirtied drawing");
        action(w,"sketch.normal");v.cancelCommandSession();
        // Save is exact and leaves the pending transaction/history untouched.
        QTemporaryDir dir;check(dir.isValid(),"temp directory failed");w.documentPath_=dir.filePath("drawing.odc");
        const auto confirmed=v.sketchEntities_;const auto savedHistory=v.undoStack_.size();action(w,"sketch.line");xy(v,1,2);polar(v,15,20);
        check(w.saveDocument(),"Save during drawing failed");NativeDocumentSnapshot loaded;
        check(NativeDocumentStorage::load(w.documentPath_,loaded,error) && loaded.sketchEntities==confirmed
            && v.commandSession_ && v.undoStack_.size()==savedHistory,"Save altered transaction or roundtrip");
        v.cancelCommandSession();check(v.importDocumentSnapshot(loaded,error),qPrintable(error));
        check(v.activeSketchWorkplaneId_.isEmpty() && v.undoStack_.empty() && !v.sketchDrawingPanel_,"Open retained editing/history");
        for(const auto& [id,presentation]:v.sketchPresentations_) {
            TColStd_ListOfInteger modes;v.context_->ActivatedModes(presentation,modes);
            check(!modes.IsEmpty(),"Open retained old Workplane picking restrictions");
        }
        (v.activateSketchWorkplane("wp1")&&v.lookAtWorkplane("wp1"));v.addBox();check(v.activeSketchWorkplaneId_.isEmpty(),"3D primitive did not exit 2D");
        snapCases(w);w.documentDirty_=false;
    }
    static void snapCases(MainWindow& w) {
        auto& v=*w.viewport_;using namespace Sketch;
        struct Case {std::vector<Geometry> sources;Point target;SnapKind kind;Point anchor{-100,-100};};
        const std::vector<Case> cases{
            {{Segment{{10,20},{110,20}}},{10,20},SnapKind::Endpoint},
            {{Segment{{-40,20},{60,20}}},{10,20},SnapKind::Midpoint},
            {{Circle{{10,20},20}},{10,20},SnapKind::Center},
            {{InfiniteLine{{10,0},{0,1}},InfiniteLine{{0,20},{1,0}}},{10,20},SnapKind::Intersection},
            {{InfiniteLine{{0,20},{1,0}},Circle{{30,20},20}},{10,20},SnapKind::Intersection},
            {{Circle{{0,0},25},Circle{{30,0},25}},{15,20},SnapKind::Intersection},
            {{Circle{{0,0},10}},{5,std::sqrt(75.0)},SnapKind::Tangent,{20,0}}
        };
        for(const auto& c:cases) for(double scale:{100.0,300.0,600.0}) {
            auto data=fixture();int i=0;for(const auto& shape:c.sources) {
                Entity e;e.id=QString("source%1").arg(++i);e.workplaneId="wp1";e.geometry=shape;e.role=Role::Construction;data.sketchEntities.push_back(e);
            }
            QString error;check(v.importDocumentSnapshot(data,error),qPrintable(error));(v.activateSketchWorkplane("wp1")&&v.lookAtWorkplane("wp1"));
            v.view_->SetScale(scale);v.view_->Redraw();v.setSketchSnapping(true);v.setSketchSnapPixels(10);
            v.startSketchDrawing(DrawTool::Segment);xy(v,c.anchor.x,c.anchor.y);
            const QPoint pos=pixel(v,c.target)+QPoint(3,2);mouse(v,QEvent::MouseMove,pos);
            check(v.commandSession_->drawing->snap && v.commandSession_->drawing->snap->kind==c.kind,"wrong snap at zoom level");
            const auto snapped=v.commandSession_->drawing->cursor;
            check(close(snapped->x,c.target.x,1.e-8) && close(snapped->y,c.target.y,1.e-8),"snap stored screen approximation");
            check(!v.sketchSnapMarker_.IsNull() && v.context_->IsDisplayed(v.sketchSnapMarker_),"snap marker not displayed");
            mouse(v,QEvent::MouseMove,pos,Qt::ShiftModifier);check(!v.commandSession_->drawing->snap,"Shift did not bypass snapping");
            v.setSketchSnapping(false);mouse(v,QEvent::MouseMove,pos);check(!v.commandSession_->drawing->snap,"disabled snap active");v.setSketchSnapping(true);
            click(v,pos);check(v.commandSession_->valid,"snapped mouse click invalid");confirm(v);
            const auto line=std::get<Segment>(v.sketchEntities_.back().geometry);
            check(close(line.end.x,c.target.x,1.e-8) && close(line.end.y,c.target.y,1.e-8),"confirmed snap lost precision");
            v.undoPull();v.redoPull();check(v.context_->NbSelected()==0,"drawing history restored selection");
        }
        // New cancels a partially entered draw and resets context/history.
        v.startSketchDrawing(DrawTool::Circle);xy(v,0,0);w.documentDirty_=false;w.newDocument();
        check(v.activeSketchWorkplaneId_.isEmpty() && !v.commandSession_ && v.sketchEntities_.empty()
            && v.undoStack_.empty() && v.sketchSnapMarker_.IsNull(),"New retained editing state");
    }
};
void runSketchDrawing() {SketchDrawingTest::run();}
void runSketchArcDirections() {SketchDrawingTest::arcDirections();}
