#include "CadViewport.h"
#include "MainWindow.h"
#include <QApplication>
#include <QEventLoop>
#include <QTimer>
#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <BRepAdaptor_Curve.hxx>
#include <TopoDS.hxx>
#include <cmath>
#include <memory>
#include <numbers>
#include <stdexcept>

namespace {
constexpr double radians=std::numbers::pi/180;
void settle() {for(int i=0;i<3;++i) {QApplication::processEvents();QGuiApplication::sync();QEventLoop loop;QTimer::singleShot(10,&loop,&QEventLoop::quit);loop.exec();}}
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
bool near(Sketch::Point a,Sketch::Point b,double tolerance=1.e-9) {return std::hypot(a.x-b.x,a.y-b.y)<tolerance;}
void key(QWidget* w,int code) {QKeyEvent event(QEvent::KeyPress,code,Qt::NoModifier);QApplication::sendEvent(w,&event);}
void mouse(CadViewport& v,QEvent::Type type,QPoint p,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
    QMouseEvent event(type,QPointF(p),QPointF(v.mapToGlobal(p)),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
        type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,modifiers);QApplication::sendEvent(&v,&event);
}
}
struct SketchEllipseDrawingTest {
    static NativeDocumentSnapshot fixture() {
        NativeDocumentSnapshot s;Construction::Object wp;wp.id="wp";wp.name="WP1";wp.kind=Construction::Kind::Workplane;
        wp.frame=gp_Ax3(gp_Pnt(10,20,30),gp_Dir(1,2,3),gp_Dir(2,-1,0));s.constructions.push_back(wp);
        wp.id="wp2";wp.name="WP2";wp.frame=gp_Ax3(gp_Pnt(-30,40,50),gp_Dir(0,0,1));s.constructions.push_back(wp);return s;
    }
    static QPoint pixel(CadViewport& v,Sketch::Point p) {
        const auto world=Sketch::worldPoint(p,v.constructionObject(v.activeSketchWorkplaneId_)->frame);int x,y;
        v.view_->Convert(world.X(),world.Y(),world.Z(),x,y);return {x,y};
    }
    static void click(CadViewport& v,Sketch::Point p) {const auto at=pixel(v,p);mouse(v,QEvent::MouseMove,at);mouse(v,QEvent::MouseButtonPress,at);mouse(v,QEvent::MouseButtonRelease,at);}
    static void center(CadViewport& v,double x=1.234567890123,double y=-7.654321098765) {
        v.sketchDrawingPanel_->x->setValue(x);v.sketchDrawingPanel_->y->setValue(y);key(v.sketchDrawingPanel_->x,Qt::Key_Return);
    }
    static void major(CadViewport& v,double a=25.123456789012,double degrees=27.123456789012) {
        auto* p=v.sketchDrawingPanel_;p->majorRadius->setValue(a);p->axisAngle->setValue(degrees);key(p->axisAngle,Qt::Key_Return);
    }
    static void minor(CadViewport& v,double b=8.987654321012) {v.sketchDrawingPanel_->minorRadius->setValue(b);v.sketchDrawingPanel_->polarButton->click();}
    static void arc(CadViewport& v,double start,double sweep) {
        v.sketchDrawingPanel_->startParameter->setValue(start);v.sketchDrawingPanel_->polarButton->click();
        v.sketchDrawingPanel_->sweepParameter->setValue(sweep);v.sketchDrawingPanel_->polarButton->click();
    }
    static QPushButton* ok(CadViewport& v) {return v.sketchDrawingPanel_->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);}
    static void begin(CadViewport& v,Sketch::DrawTool tool) {v.startSketchDrawing(tool);check(v.commandSession_.has_value(),"ellipse tool did not start");}
    static void run() {
        MainWindow w;w.show();QApplication::processEvents();auto& v=*w.viewport_;QString error;
        const auto base=fixture();check(v.importDocumentSnapshot(base,error) && v.activateSketchWorkplane("wp"),qPrintable(error));
        auto* action=w.findChild<QAction*>("sketch.ellipse");auto* arcAction=w.findChild<QAction*>("sketch.elliptical_arc");
        check(action && arcAction,"ellipse menu/toolbar actions missing");
        std::unique_ptr<QMenu> menu(v.createSelectionMenu({0,0}));int commands=0;
        for(auto* a:menu->findChildren<QAction*>()) if(a->text().contains("Ellipse") || a->text().contains("Elliptical Arc")) ++commands;
        check(commands==4,"profile/construction ellipse context commands missing");
        action->trigger();auto* p=v.sketchDrawingPanel_;check(p->x->isVisible() && !p->majorRadius->isVisible() && !ok(v)->isEnabled(),"wrong initial ellipse fields");
        center(v);check(p->majorRadius->isVisible() && !p->minorRadius->isVisible(),"major step fields incorrect");
        major(v);check(p->minorRadius->isVisible() && !p->majorRadius->isVisible(),"minor step fields incorrect");
        p->minorRadius->setValue(8.987654321012);const auto exactMinor=p->minorRadius->value();
        mouse(v,QEvent::MouseMove,pixel(v,{100,100}));check(p->minorRadius->value()==exactMinor,"pointer overwrote pending numeric value");
        check(!ok(v)->isEnabled() && v.sketchEntities_.empty(),"partial numeric preview committed");minor(v);
        const auto ellipse=std::get<Sketch::Ellipse>(v.commandSession_->drawing->candidate.geometry);
        check(ellipse.center==Sketch::Point{1.234567890123,-7.654321098765} && ellipse.majorRadius==25.123456789012
            && ellipse.minorRadius==exactMinor && ellipse.axisAngle==27.123456789012*radians,"numeric ellipse values rounded or inferred from points");
        check(ok(v)->isEnabled() && v.sketchPreviews_.size()==1 && v.sketchEntities_.empty(),"complete ellipse preview invalid");
        for(const auto& [id,preview]:v.sketchPreviews_) {Quantity_Color color;preview->Color(color);check(color.Blue()>0.9 && color.Red()<0.2,"ellipse preview is not blue");}
        p->role->setCurrentIndex(p->role->findData(int(Sketch::Role::Construction)));
        const auto history=v.undoStack_.size();key(p->minorRadius,Qt::Key_Return);
        check(v.sketchEntities_.size()==1 && v.sketchEntities_.back().geometry==Sketch::Geometry{ellipse}
            && v.sketchEntities_.back().role==Sketch::Role::Construction && v.undoStack_.size()==history+1,"ellipse did not create one exact transaction");
        const auto confirmed=v.sketchEntities_.back();click(v,Sketch::ellipsePoint(ellipse,0.8));
        check(v.sketchSelection()==QStringList{confirmed.id},"newly drawn ellipse not viewport-selectable");v.undoPull();v.redoPull();
        check(v.sketchEntities_.back()==confirmed && v.sketchSelection().empty(),"ellipse Undo/Redo lost values/UUID or retained selection");
        check(w.constructionRoot_->child(0)->child(0)->text(0)=="Ellipse","ellipse browser name incorrect");
        // Mouse acquisition: minor length is the perpendicular component only.
        v.setSketchSnapping(false);begin(v,Sketch::DrawTool::Ellipse);click(v,{-20,20});click(v,{20,20});
        const auto majorBefore=v.commandSession_->drawing->ellipse;click(v,{80,32});
        auto mouseEllipse=std::get<Sketch::Ellipse>(v.commandSession_->drawing->candidate.geometry);
        check(v.commandSession_->valid && std::abs(mouseEllipse.minorRadius-12)<0.5 && mouseEllipse.majorRadius==majorBefore.majorRadius
            && mouseEllipse.axisAngle==majorBefore.axisAngle,"mouse minor step changed fixed major axis or used cursor distance");
        v.confirmCommandSession();
        for(double sweep:{70.123456789012,-70.123456789012,270.0,-270.0,20.0}) {
            arcAction->trigger();center(v);major(v);minor(v);arc(v,350.123456789012,sweep);
            check(v.commandSession_->valid && ok(v)->isEnabled(),"numeric elliptical arc did not complete");
            const auto original=std::get<Sketch::EllipticalArc>(v.commandSession_->drawing->candidate.geometry);
            check(original.startParameter==350.123456789012*radians && original.sweepParameter==sweep*radians,"signed arc numeric values changed");
            const auto endpoints=Sketch::ellipticalArcEndpoints(original);p=v.sketchDrawingPanel_;
            p->direction->setCurrentIndex(original.sweepParameter<0?0:1);
            const auto opposite=std::get<Sketch::EllipticalArc>(v.commandSession_->drawing->candidate.geometry);
            const auto otherEnds=Sketch::ellipticalArcEndpoints(opposite);
            check(near(endpoints[0],otherEnds[0]) && near(endpoints[1],otherEnds[1]) && opposite.sweepParameter*original.sweepParameter<0
                && std::abs(std::abs(opposite.sweepParameter)+std::abs(original.sweepParameter)-2*std::numbers::pi)<1.e-10,"direction switch moved endpoints or selected wrong route");
            p->role->setCurrentIndex(p->role->findData(int(Sketch::Role::Construction)));
            const auto size=v.undoStack_.size();v.confirmCommandSession();
            check(v.undoStack_.size()==size+1 && std::holds_alternative<Sketch::EllipticalArc>(v.sketchEntities_.back().geometry),"arc confirmation changed type/history");
            const auto saved=v.sketchEntities_.back();v.undoPull();v.redoPull();check(v.sketchEntities_.back()==saved,"arc Redo changed UUID/parameters");
        }
        // Back every acquisition step, including complete preview, without history.
        begin(v,Sketch::DrawTool::EllipticalArc);center(v);major(v);minor(v);arc(v,10,-80);
        const auto size=v.undoStack_.size(),count=v.sketchEntities_.size();
        for(int step=4;step>=0;--step) {v.sketchDrawingPanel_->backButton->click();check(v.commandSession_->drawing->points.size()==size_t(step) && !ok(v)->isEnabled(),"Back skipped/reset a step or enabled incomplete OK");}
        center(v);major(v,10,40);minor(v,10);arc(v,350,20);check(v.commandSession_->valid,"equal radii arc rejected after Back");
        key(v.sketchDrawingPanel_,Qt::Key_Escape);check(v.undoStack_.size()==size && v.sketchEntities_.size()==count && v.sketchPreviews_.empty(),"Cancel changed history or left previews");
        begin(v,Sketch::DrawTool::Ellipse);center(v);major(v,10,0);p=v.sketchDrawingPanel_;p->minorRadius->setValue(11);p->polarButton->click();
        check(v.commandSession_->drawing->points.size()==2 && !ok(v)->isEnabled() && v.commandSession_->drawing->ellipse.majorRadius==10,"invalid minor radius swapped axes or advanced");
        p->minorRadius->setValue(0);p->polarButton->click();check(!ok(v)->isEnabled(),"zero minor radius accepted");minor(v,10);
        p->minorRadius->setValue(12);check(!ok(v)->isEnabled() && !v.commandSession_->valid,"invalid complete edit left OK active");p->minorRadius->setValue(10);
        p->majorRadius->setValue(12.5);check(v.commandSession_->valid && std::get<Sketch::Ellipse>(v.commandSession_->drawing->candidate.geometry).majorRadius==12.5,"complete parameter edit not live");
        v.cancelCommandSession();
        begin(v,Sketch::DrawTool::Ellipse);center(v);major(v,0,0);check(v.commandSession_->drawing->points.size()==1 && !ok(v)->isEnabled(),"degenerate major axis accepted");v.cancelCommandSession();
        begin(v,Sketch::DrawTool::Ellipse);center(v);p=v.sketchDrawingPanel_;p->majorRadius->setLocale(QLocale("nl_NL"));
        p->majorRadius->findChild<QLineEdit*>()->setText("12,5");key(p->majorRadius,Qt::Key_Return);
        check(v.commandSession_->drawing->points.size()==2 && v.commandSession_->drawing->ellipse.majorRadius==12.5,"localized numeric input was not interpreted before Enter");v.cancelCommandSession();
        begin(v,Sketch::DrawTool::EllipticalArc);center(v);major(v);minor(v);p=v.sketchDrawingPanel_;p->startParameter->setValue(0);p->polarButton->click();
        for(double bad:{0.0,360.0,-360.0}) {p->sweepParameter->setValue(bad);p->polarButton->click();check(v.commandSession_->drawing->points.size()==4 && !ok(v)->isEnabled(),"zero/full-turn arc accepted");}v.cancelCommandSession();
        begin(v,Sketch::DrawTool::EllipticalArc);center(v);major(v);minor(v);p=v.sketchDrawingPanel_;
        p->startParameter->setValue(350);p->polarButton->click();p->sweepParameter->setValue(60);
        const auto pendingArc=std::get<Sketch::EllipticalArc>(v.commandSession_->drawing->candidate.geometry);
        p->direction->setCurrentIndex(1);const auto reversePending=std::get<Sketch::EllipticalArc>(v.commandSession_->drawing->candidate.geometry);
        check(v.commandSession_->drawing->points.size()==4 && !ok(v)->isEnabled() && reversePending.sweepParameter<0
            && near(Sketch::ellipticalArcEndpoints(pendingArc)[1],Sketch::ellipticalArcEndpoints(reversePending)[1]),"numeric live CW switch moved endpoint or prematurely completed");v.cancelCommandSession();
        for(int stage=0;stage<=5;++stage) {
            begin(v,Sketch::DrawTool::EllipticalArc);const auto before=v.sketchEntities_;const auto historyBefore=v.undoStack_.size();
            if(stage>0) center(v);if(stage>1) major(v);if(stage>2) minor(v);
            if(stage>3) {v.sketchDrawingPanel_->startParameter->setValue(30);v.sketchDrawingPanel_->polarButton->click();}
            if(stage>4) {v.sketchDrawingPanel_->sweepParameter->setValue(120);v.sketchDrawingPanel_->polarButton->click();}
            if(stage%2) v.sketchDrawingPanel_->close();else key(v.sketchDrawingPanel_,Qt::Key_Escape);
            check(!v.commandSession_ && v.sketchPreviews_.empty() && v.sketchEntities_==before && v.undoStack_.size()==historyBefore,"close/Cancel at acquisition step modified geometry/history");
        }
        // Real mouse arc chooses nearest on-ellipse points, not polar angles.
        begin(v,Sketch::DrawTool::EllipticalArc);center(v,0,0);major(v,30,25);minor(v,10);
        const auto support=v.commandSession_->drawing->ellipse;click(v,Sketch::ellipsePoint(support,5.8));click(v,Sketch::ellipsePoint(support,0.5));
        check(v.commandSession_->valid && std::get<Sketch::EllipticalArc>(v.commandSession_->drawing->candidate.geometry).sweepParameter>0,"mouse seam-crossing arc failed");v.cancelCommandSession();
        // Save confirmed geometry, excluding the candidate, with stable history.
        QTemporaryDir dir;w.documentPath_=dir.filePath("drawn-v5.odc");const auto values=v.sketchEntities_;const auto undo=v.undoStack_.size();
        begin(v,Sketch::DrawTool::Ellipse);center(v);major(v);minor(v);check(w.saveDocument() && v.commandSession_ && v.undoStack_.size()==undo,"Save lost drawing session/history");
        NativeDocumentSnapshot loaded;check(NativeDocumentStorage::load(w.documentPath_,loaded,error) && loaded.schemaVersion==5 && loaded.sketchEntities==values,"schema-v5 save lost exact values or included preview");
        check(v.importDocumentSnapshot(loaded,error) && !v.commandSession_ && v.undoStack_.empty() && v.sketchEntities_==values,"Open retained temporary drawing/history");
        check(v.activateSketchWorkplane("wp"),"cannot reactivate sketch");begin(v,Sketch::DrawTool::Ellipse);center(v);check(v.activateSketchWorkplane("wp2") && !v.commandSession_,"Workplane switch retained drawing");
        begin(v,Sketch::DrawTool::Ellipse);center(v);v.startSketchDrawing(Sketch::DrawTool::Circle);check(v.commandSession_->drawing->tool==Sketch::DrawTool::Circle && v.commandSession_->drawing->points.empty(),"tool switch retained ellipse points");v.cancelCommandSession();
        snapAndPanel(w);w.documentDirty_=false;
    }
    static void snapAndPanel(MainWindow& w) {
        auto& v=*w.viewport_;auto data=fixture();Sketch::Entity e;e.workplaneId="wp";e.geometry=Sketch::EllipticalArc{{{10,20},30,10,0.3},5.8,-2.0};data.sketchEntities={e};QString error;
        check(v.importDocumentSnapshot(data,error) && v.activateSketchWorkplane("wp"),qPrintable(error));v.setSketchSnapping(true);v.setSketchSnapPixels(10);
        const auto arc=std::get<Sketch::EllipticalArc>(e.geometry),mid=arc;const auto ends=Sketch::ellipticalArcEndpoints(arc);
        const std::vector<std::pair<Sketch::Point,Sketch::SnapKind>> targets{{arc.ellipse.center,Sketch::SnapKind::Center},{ends[0],Sketch::SnapKind::Endpoint},{ends[1],Sketch::SnapKind::Endpoint},{Sketch::ellipsePoint(mid.ellipse,mid.startParameter+mid.sweepParameter/2),Sketch::SnapKind::Midpoint}};
        for(double zoom:{100.0,300.0}) for(const auto& [target,kind]:targets) {
            v.view_->Camera()->SetScale(zoom);v.view_->Redraw();v.startSketchDrawing(Sketch::DrawTool::Segment);center(v,-80,-80);
            const auto point=pixel(v,target)+QPoint(2,1);mouse(v,QEvent::MouseMove,point);
            check(v.commandSession_->drawing->snap && v.commandSession_->drawing->snap->kind==kind && v.commandSession_->drawing->cursor==target,"GUI elliptic snap lost exact target/priority at zoom");
            mouse(v,QEvent::MouseMove,point,Qt::ShiftModifier);check(!v.commandSession_->drawing->snap,"Shift did not bypass ellipse snap");v.cancelCommandSession();
        }
        begin(v,Sketch::DrawTool::EllipticalArc);center(v);major(v);minor(v);arcInput(v);
        auto* p=v.sketchDrawingPanel_;settle();
        const QRect bounds(v.mapToGlobal(QPoint()),v.size());check(bounds.contains(p->frameGeometry()),"ellipse command panel outside viewport");
        const auto frame=p->frameGeometry();check(std::abs(bounds.right()-frame.right()-12)<=1 && std::abs(frame.top()-bounds.top()-12)<=1,"ellipse command panel not viewport upper-right +12");
        p->move(p->pos()+QPoint(-40,20));settle();const auto moved=p->pos();p->minorRadius->setValue(4);
        settle();check(p->pos()==moved,"parameter edit reset dragged ellipse panel");
        w.resize(760,580);settle();
        check(QRect(v.mapToGlobal(QPoint()),v.size()).contains(p->frameGeometry()),"small/HiDPI ellipse panel not clamped");
        key(p,Qt::Key_Escape);begin(v,Sketch::DrawTool::Ellipse);center(v);w.documentDirty_=false;w.newDocument();
        check(!v.commandSession_ && v.sketchPreviews_.empty() && v.sketchEntities_.empty(),"New retained ellipse drawing state");
    }
    static void arcInput(CadViewport& v) {arc(v,30,100);}
};
void runSketchEllipseDrawing() {SketchEllipseDrawingTest::run();}
