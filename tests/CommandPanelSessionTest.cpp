#include "CadViewport.h"
#include "MainWindow.h"
#include "ConstructionPanel.h"
#include "CommandPanelPositioner.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QDockWidget>
#include <QPointer>
#include <QPixmap>
#include <QScreen>
#include <QDebug>
#include <QScrollArea>
#include <QScrollBar>
#include <QLayout>
#include <QPushButton>
#include <QEventLoop>
#include <QTimer>
#include <BRepAdaptor_Surface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <stdexcept>
#include <iostream>

namespace {
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void settle() {for(int i=0;i<3;++i) {QApplication::processEvents();QGuiApplication::sync();QEventLoop loop;QTimer::singleShot(10,&loop,&QEventLoop::quit);loop.exec();}}
void key(QWidget& widget,int code) {QKeyEvent event(QEvent::KeyPress,code,Qt::NoModifier);QApplication::sendEvent(&widget,&event);settle();}
}
struct CommandPanelSessionTest {
    static QRect bounds(CadViewport& v) {return {v.mapToGlobal(QPoint()),v.size()};}
    static void anchored(CadViewport& v,QWidget* panel) {
        settle();check(panel && panel->isVisible() && panel->findChild<QScrollArea*>(),"command panel was not registered/shown");
        const auto b=bounds(v),p=panel->frameGeometry();
        check(b.contains(p) && std::abs(b.right()-p.right()-12)<=1 && std::abs(p.top()-b.top()-12)<=1,"command initial frame is not viewport upper-right +12");
    }
    static void completeForm(QWidget* panel) {
        settle();auto* scroll=panel->findChild<QScrollArea*>();check(scroll,"missing command scroll area");
        if(scroll->horizontalScrollBar()->maximum() || scroll->verticalScrollBar()->maximum())
            std::cerr<<panel->windowTitle().toStdString()<<" has unexpected scroll ranges "<<scroll->horizontalScrollBar()->maximum()<<','<<scroll->verticalScrollBar()->maximum()<<'\n';
        check(!scroll->horizontalScrollBar()->maximum() && !scroll->verticalScrollBar()->maximum(),"fitting command form unexpectedly scrolls");
        // Check complete controls, not merely their centers or the outer frame.
        for(auto* child:scroll->widget()->findChildren<QWidget*>()) {
            if(!child->isVisibleTo(panel) || (!qobject_cast<QLineEdit*>(child) && !qobject_cast<QDoubleSpinBox*>(child)
                && !qobject_cast<QComboBox*>(child) && !qobject_cast<QPushButton*>(child))) continue;
            check(scroll->viewport()->rect().contains(QRect(child->mapTo(scroll->viewport(),QPoint()),child->size())),"command field/button clipped by scroll viewport");
            if(qobject_cast<QDoubleSpinBox*>(child) || qobject_cast<QComboBox*>(child))
                check(child->width()>=child->sizeHint().width(),"command input narrower than natural width");
        }
        const auto size=panel->size();for(int i=0;i<5;++i) {panel->resize(size);settle();}
        check(panel->size()==size,"command repeatedly shrank while processing resize events");
    }
    static QPoint place(CadViewport& v,QWidget* panel) {
        const auto b=bounds(v);const auto frame=panel->frameGeometry();
        const auto at=b.topLeft()+QPoint(std::min(35,b.width()-frame.width()),std::min(30,b.height()-frame.height()));
        panel->move(panel->pos()+at-frame.topLeft());
        for(int i=0;i<30;++i) {settle();if(panel->frameGeometry().topLeft()==at) return at;}
        check(false,"native manual placement did not settle at the requested position");return at;
    }
    static void retained(CadViewport& v,QWidget* panel,QPoint at) {
        settle();if(!bounds(v).contains(panel->frameGeometry()) || panel->frameGeometry().topLeft()!=at) {
            const auto p=panel->frameGeometry(),b=bounds(v);std::cerr<<panel->windowTitle().toStdString()<<" expected "<<at.x()<<','<<at.y()<<" actual "<<p.x()<<','<<p.y()<<' '<<p.width()<<'x'<<p.height()<<" bounds "<<b.x()<<','<<b.y()<<' '<<b.width()<<'x'<<b.height()<<'\n';
        }
        check(bounds(v).contains(panel->frameGeometry()) && panel->frameGeometry().topLeft()==at,"preview update moved a visible user-placed panel");
    }
    static void run() {
        MainWindow window;window.show();settle();auto& v=*window.viewport_;v.addBox();
        // These are the three Fedora KDE sizing regressions. Start Move Face
        // before any other generic command, whose controls were initially hidden.
        // Native styles/layouts may query this hint before the first command.
        (void)v.commandPanel_->sizeHint();
        TopoDS_Face face;
        for(TopExp_Explorer f(v.boxes_[0].shape,TopAbs_FACE);f.More();f.Next()) {
            const auto candidate=TopoDS::Face(f.Current());const BRepAdaptor_Surface surface(candidate);
            if(surface.GetType()==GeomAbs_Plane && surface.Plane().Axis().Direction().Z()>0.999) {face=candidate;break;}
        }
        check(!face.IsNull() && v.startMoveFace(0,face),"cannot start actual Move Face sizing regression");
        anchored(v,v.commandPanel_);completeForm(v.commandPanel_);
        const auto capture=qEnvironmentVariable("OPENDESIGNER_PANEL_CAPTURE");
        if(!capture.isEmpty()) {
            QString diagnostic;QDebug(&diagnostic)<<"GUI capture platform"<<QGuiApplication::platformName();
            for(auto* screen:QGuiApplication::screens()) QDebug(&diagnostic)<<screen->name()<<screen->geometry()<<"DPR"<<screen->devicePixelRatio();
            std::cout<<diagnostic.toStdString()<<'\n';
            // XWayland screen readback may be black under KWin. Render the Qt
            // controls directly; this does not capture OCCT's native drawable.
            check(window.grab().save(capture+"-window.png"),"cannot capture command-panel window");
            check(v.commandPanel_->grab().save(capture+"-panel.png"),"cannot capture command-panel controls");
        }
        check(v.previewCommand(2),"Move Face sizing preview failed");completeForm(v.commandPanel_);v.cancelCommandSession();
        v.startConstruction(Construction::Method::PlaneFace);anchored(v,v.constructionPanel_);completeForm(v.constructionPanel_);
        v.fitAll();v.chooseConstructionRole(0);int x,y;v.view_->Convert(50,40,60,x,y);
        check(v.captureConstructionReference(QPoint(x,y)) && v.commandSession_->valid,"From planar face sizing reference failed");
        completeForm(v.constructionPanel_);v.cancelCommandSession();
        // Every generic 3D command kind uses the same registered panel. Actual
        // modeling validation/preview/transactions remain in the full 3D suites.
        for(auto kind:{CadViewport::CommandSession::Kind::Move,CadViewport::CommandSession::Kind::Rotate,
            CadViewport::CommandSession::Kind::MoveFace,CadViewport::CommandSession::Kind::RotateFace,
            CadViewport::CommandSession::Kind::AlignFace,CadViewport::CommandSession::Kind::DeleteFace}) {
            v.beginCommandSession(kind,{0},gp_Dir(1,0,0));anchored(v,v.commandPanel_);completeForm(v.commandPanel_);key(v,Qt::Key_Escape);check(!v.commandSession_,"Escape lost after panel wrapping");
        }
        v.beginCommandSession(CadViewport::CommandSession::Kind::Move,{0},gp_Dir(1,0,0));anchored(v,v.commandPanel_);
        const auto history=v.undoStack_.size();window.documentDirty_=false;auto at=place(v,v.commandPanel_);
        v.commandPanel_->activateWindow();v.dragDimension_->setFocus();settle();const auto* focus=QApplication::focusWidget();
        check(v.previewCommand(12),"3D preview failed");v.showCommandControls(QPoint(5,5));retained(v,v.commandPanel_,at);
        check(QApplication::focusWidget()==focus && !window.documentDirty_ && v.undoStack_.size()==history,"position/preview changed focus, dirty state or history");
        v.dragDimension_->setText("12");key(*v.dragDimension_,Qt::Key_Return);
        check(!v.commandSession_ && v.undoStack_.size()==history+1,"Enter no longer confirms one transaction");v.undoPull();v.redoPull();
        v.startConstruction(Construction::Method::PlaneXY);anchored(v,v.constructionPanel_);at=place(v,v.constructionPanel_);
        v.constructionPanel_->name->setText("WP2");v.constructionPanel_->setValue(15);retained(v,v.constructionPanel_,at);v.confirmCommandSession();settle();
        const auto owner=v.constructions_.front().id;check(v.activateSketchWorkplane(owner),"cannot activate Workplane");
        v.startSketchDrawing(Sketch::DrawTool::Arc,Sketch::Role::Construction);anchored(v,v.sketchDrawingPanel_);completeForm(v.sketchDrawingPanel_);at=place(v,v.sketchDrawingPanel_);
        const auto drawingHistory=v.undoStack_.size();v.acceptSketchPoint({0,0});v.acceptSketchPoint({10,0});completeForm(v.sketchDrawingPanel_);
        v.acceptSketchPoint({0,10});completeForm(v.sketchDrawingPanel_);retained(v,v.sketchDrawingPanel_,at);
        window.documentDirty_=false;
        QWheelEvent wheel(QPointF(80,300),QPointF(v.mapToGlobal(QPoint(80,300))),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(&v,&wheel);settle();check(v.commandSession_ && !window.documentDirty_,"navigation closed/dirtied command");
        key(*v.sketchDrawingPanel_,Qt::Key_Escape);check(v.sketchEntities_.empty() && v.undoStack_.size()==drawingHistory,"2D Cancel left geometry/history");
        Sketch::Entity line;line.workplaneId=owner;line.role=Sketch::Role::Construction;line.geometry=Sketch::Segment{{-10,0},{10,0}};
        auto circle=line;circle.id="panel-circle-1";circle.geometry=Sketch::Circle{{},5};auto other=circle;other.id="panel-circle-2";other.geometry=Sketch::Circle{{15,0},3};
        QString error;check(v.previewSketchEntities({line,circle,other},error),qPrintable(error));v.confirmCommandSession();
        v.startSketchAxes(true,true);anchored(v,v.commandPanel_);v.cancelCommandSession();
        v.startSketchOverdraw();anchored(v,v.sketchOverdrawPanel_);at=place(v,v.sketchOverdrawPanel_);v.refreshOverdraw();retained(v,v.sketchOverdrawPanel_,at);v.cancelCommandSession();
        v.selectSketchEntities({line.id});v.startSketchConstruction(Sketch::ConstructionTool::OffsetLine);anchored(v,v.sketchConstructionPanel_);
        at=place(v,v.sketchConstructionPanel_);v.sketchConstructionPanel_->distance->setValue(-25);retained(v,v.sketchConstructionPanel_,at);v.cancelCommandSession();
        v.selectSketchEntities({circle.id,other.id});v.startSketchConstruction(Sketch::ConstructionTool::CommonTangent);anchored(v,v.sketchConstructionPanel_);
        at=place(v,v.sketchConstructionPanel_);v.sketchConstructionPanel_->solutions->setCurrentIndex(1);retained(v,v.sketchConstructionPanel_,at);
        // Docking/window layout changes must not leave a Tool window behind.
        auto* dock=window.findChild<QDockWidget*>();check(dock,"Structure dock missing");dock->hide();settle();check(bounds(v).contains(v.sketchConstructionPanel_->frameGeometry()),"dock hide lost command frame");
        dock->show();window.resize(950,650);settle();check(bounds(v).contains(v.sketchConstructionPanel_->frameGeometry()),"resize/docking lost command frame");
        v.cancelCommandSession();v.endSketchEditing();v.addBox();v.selectParts({0,1});v.beginBooleanSession(CadViewport::BooleanOperation::Unite);anchored(v,v.commandPanel_);
        at=place(v,v.commandPanel_);v.updateBooleanControls();retained(v,v.commandPanel_,at);key(v,Qt::Key_Escape);
        check(!v.commandSession_ && v.dragDimension_->parentWidget()==&v,"Boolean Cancel did not restore normal numeric lifecycle");
        window.documentDirty_=false;
    }
};
void runCommandPanelSessions() {CommandPanelSessionTest::run();}
