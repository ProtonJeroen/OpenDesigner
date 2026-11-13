#include "CadViewport.h"
#include "MainWindow.h"
#include <QApplication>
#include <QAction>
#include <BRepAdaptor_Curve.hxx>
#include <TopoDS.hxx>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QDoubleSpinBox>
#include <Geom2dAPI_ProjectPointOnCurve.hxx>
#include <QTemporaryDir>
#include <stdexcept>
namespace {
void check(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
void editDialog(bool accept,bool change=false) {
    QTimer::singleShot(50,[accept,change] {
        for(auto* w:QApplication::topLevelWidgets()) if(auto* d=qobject_cast<QDialog*>(w);d && d->objectName()=="sketchCurveEditor") {
            if(change) {auto* table=d->findChild<QTableWidget*>("splinePoints");check(table && table->rowCount()==3,"spline editor lost through-point table");table->item(1,1)->setText("6");}
            auto* box=d->findChild<QDialogButtonBox*>();check(box,"missing curve editor buttons");
            box->button(accept?QDialogButtonBox::Ok:QDialogButtonBox::Cancel)->click();return;
        }
        throw std::runtime_error("curve editor did not open");
    });
}
}
struct SketchAdvancedCurveSessionTest {
    static void interactive(MainWindow& w) {
        auto& v=*w.viewport_;QString error;
        NativeDocumentSnapshot snapshot;Construction::Object wp;wp.id="spline-wp";wp.kind=Construction::Kind::Workplane;wp.name="Spline WP";wp.extent=100;
        wp.frame=gp_Ax3(gp_Pnt(10,20,30),gp_Dir(1,2,3),gp_Dir(2,-1,0));snapshot.constructions={wp};
        Sketch::Entity reference;reference.workplaneId=wp.id;reference.role=Sketch::Role::Construction;reference.geometry=Sketch::Segment{{-30,0},{-30,-20}};snapshot.sketchEntities={reference};
        check(v.importDocumentSnapshot(snapshot,error),qPrintable(error));check(v.activateSketchWorkplane(wp.id) && v.lookAtWorkplane(wp.id),"spline Workplane activation failed");QApplication::processEvents();w.documentDirty_=false;
        const auto initial=v.sketchEntities_;const auto history=v.undoStack_.size();
        const auto move=[&](Sketch::Point p,bool click) {
            auto world=Sketch::worldPoint(p,wp.frame);int x,y;v.view_->Convert(world.X(),world.Y(),world.Z(),x,y);QPoint at(x,y);
            const auto event=[&](QEvent::Type type) {QMouseEvent e(type,QPointF(at),QPointF(v.mapToGlobal(at)),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&v,&e);};
            event(QEvent::MouseMove);if(click) {event(QEvent::MouseButtonPress);event(QEvent::MouseButtonRelease);}
        };
        const auto enter=[&](QWidget* target,int code) {QKeyEvent e(QEvent::KeyPress,code,Qt::NoModifier);QApplication::sendEvent(target,&e);};
        const auto start=[&] {auto* action=w.findChild<QAction*>("sketch.spline");check(action && action->isEnabled(),"spline action unavailable");action->trigger();QApplication::processEvents();check(v.commandSession_ && v.commandSession_->drawing && v.sketchDrawingPanel_,"spline action did not start nonmodal drawing");};
        start();auto* panel=v.sketchDrawingPanel_;auto* ok=panel->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        check(!ok->isEnabled(),"empty spline can confirm");move({-30,0},true);
        check(v.commandSession_->drawing->points==std::vector<Sketch::Point>{{-30,0}},"spline did not snap to reference endpoint");
        move({-10,25},true);check(v.commandSession_->drawing->points.size()==2 && !ok->isEnabled(),"two points enabled confirmation");
        check(v.commandSession_->sketchDisplayCandidates.size()==1 && std::holds_alternative<Sketch::Segment>(v.commandSession_->sketchDisplayCandidates[0].geometry),"two-point feedback is not provisional segment");
        panel->x->setValue(999);enter(panel->x,Qt::Key_Return);check(v.commandSession_ && v.commandSession_->drawing->points.size()==2 && v.sketchEntities_==initial,"Enter appended numeric point or confirmed two points");
        move({15,5},true);check(ok->isEnabled(),"third spline point did not enable OK");move({35,30},true);
        panel->x->setValue(45);panel->y->setValue(0);panel->xyButton->click();check(v.commandSession_->drawing->points.size()==5,"numeric spline point did not append");
        panel->backButton->click();check(v.commandSession_->drawing->points.size()==4 && ok->isEnabled(),"Back did not remove spline point");
        const auto accepted=v.commandSession_->drawing->points;
        panel->x->setValue(accepted.back().x);panel->y->setValue(accepted.back().y);panel->xyButton->click();
        check(v.commandSession_->drawing->points==accepted,"duplicate interpolation point was accepted");
        const auto candidate=v.commandSession_->sketch->after;
        move({50,-20},false);check(v.commandSession_->drawing->points==accepted && v.commandSession_->sketch->after==candidate,"cursor changed accepted spline");
        check(std::get<Sketch::Spline>(v.commandSession_->sketchDisplayCandidates[0].geometry).points.size()==5,"cursor did not update live spline preview");
        check(v.sketchEntities_==initial && v.undoStack_.size()==history && !w.documentDirty_,"spline preview changed document/history");
        panel->x->setValue(888);enter(panel->x,Qt::Key_Return);
        check(!v.commandSession_ && v.sketchEntities_.size()==initial.size()+1 && v.undoStack_.size()==history+1,"spline confirmation not one transaction");
        const auto confirmed=v.sketchEntities_;const auto& spline=std::get<Sketch::Spline>(confirmed.back().geometry);check(spline.points==accepted,"confirmation included temporary or numeric point");
        const auto curve=Sketch::splineCurve(spline);
        for(auto point:accepted) {Geom2dAPI_ProjectPointOnCurve projection(gp_Pnt2d(point.x,point.y),curve);check(projection.NbPoints()>0 && projection.LowerDistance()<1.e-7,"exact spline missed accepted interpolation point");}
        BRepAdaptor_Curve rendered(TopoDS::Edge(v.sketchPresentations_.at(confirmed.back().id)->Shape()));
        for(double fraction:{0.,.25,.5,.75,1.}) {
            const double parameter=curve->FirstParameter()+fraction*(curve->LastParameter()-curve->FirstParameter());const auto local=curve->Value(parameter);
            check(rendered.Value(parameter).Distance(Sketch::worldPoint({local.X(),local.Y()},wp.frame))<1.e-7,"spline preview/commit lost Workplane transform");
        }
        v.selectSketchEntities({confirmed.back().id});editDialog(true);v.editSketchCurve();
        check(v.commandSession_->sketch->after==confirmed,"detailed spline editor lost interpolation points");v.finishSketchEdit(false);
        NativeDocumentSnapshot saved,loaded;QTemporaryDir temp;check(v.exportDocumentSnapshot(saved),"spline export failed");
        check(NativeDocumentStorage::save(temp.filePath("interactive.odc"),saved,error) && NativeDocumentStorage::load(temp.filePath("interactive.odc"),loaded,error) && loaded.sketchEntities==confirmed,"interactive spline persistence changed points/ID");
        v.undoPull();check(v.sketchEntities_==initial,"spline Undo lost initial geometry");v.redoPull();check(v.sketchEntities_==confirmed,"spline Redo changed values or ID");
        start();move({0,-20},true);move({10,-30},true);enter(v.sketchDrawingPanel_,Qt::Key_Escape);check(!v.commandSession_ && v.sketchEntities_==confirmed && v.sketchPreviews_.empty(),"Escape retained pending spline");
        start();move({0,-20},true);v.sketchDrawingPanel_->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();check(!v.commandSession_ && v.sketchEntities_==confirmed,"Cancel changed spline document");
        start();move({0,-20},true);v.startSketchDrawing(Sketch::DrawTool::Segment);check(v.commandSession_->drawing->points.empty() && v.sketchEntities_==confirmed,"tool switch retained pending spline");v.cancelCommandSession();
        start();move({0,-20},true);v.endSketchEditing();check(!v.commandSession_ && v.sketchEntities_==confirmed,"leaving Sketch retained pending spline");
        for(bool hidden:{false,true}) {
            auto unavailable=*v.constructionObject(wp.id);unavailable.visible=!hidden;unavailable.selectable=hidden;
            check(v.updateConstruction(unavailable),"Workplane eligibility fixture failed");
            v.startSketchDrawing(Sketch::DrawTool::Spline);check(!v.commandSession_ && v.sketchEntities_==confirmed,"spline started on hidden/locked Workplane");
            unavailable.visible=true;unavailable.selectable=true;check(v.updateConstruction(unavailable),"Workplane restoration failed");
        }
    }
    static void run() {
        MainWindow w;w.show();QApplication::processEvents();auto& v=*w.viewport_;QString error;
        v.startConstruction(Construction::Method::PlaneXY);v.confirmCommandSession();
        check(v.activateSketchWorkplane(v.constructions_.front().id),"active Workplane failed");
        const auto history=v.undoStack_.size();
        editDialog(false);v.editSketchCurve(true);
        check(v.sketchEntities_.empty() && v.undoStack_.size()==history && !v.commandSession_,"editor Cancel changed document/history");
        editDialog(true);v.editSketchCurve(true);
        check(v.sketchEntities_.empty() && v.commandSession_ && !v.sketchPreviews_.empty(),"spline Preview mutated document or is invisible");
        v.finishSketchEdit(false);check(v.sketchEntities_.empty() && v.undoStack_.size()==history,"spline preview Cancel changed history");
        editDialog(true);v.editSketchCurve(true);v.finishSketchEdit(true);
        check(v.sketchEntities_.size()==1 && v.undoStack_.size()==history+1,"spline OK is not one Undo step");
        const auto confirmed=v.sketchEntities_;const auto id=confirmed.front().id;
        v.selectSketchEntities({id});editDialog(true,true);v.editSketchCurve();
        check(v.sketchEntities_==confirmed,"point editor Preview mutated confirmed values");v.finishSketchEdit(true);
        check(v.sketchEntities_.front().id==id && std::get<Sketch::Spline>(v.sketchEntities_.front().geometry).points[1].y==6,"point editor did not preserve ID/update coordinate");
        v.undoPull();check(v.sketchEntities_==confirmed,"point edit Undo did not restore exact values");
        v.undoPull();check(v.sketchEntities_.empty(),"spline creation Undo failed");v.redoPull();check(v.sketchEntities_==confirmed,"spline Redo changed values/ID");
        v.selectSketchEntities({id});auto* duplicate=w.findChild<QAction*>("sketch.duplicate_curves");check(duplicate,"missing duplicate curve action");duplicate->trigger();
        check(v.commandSession_ && v.sketchEntities_==confirmed,"curve duplicate Preview mutated source");v.finishSketchEdit(true);
        check(v.sketchEntities_.size()==2 && v.sketchEntities_.back().id!=id && v.sketchEntities_.back().geometry==confirmed.front().geometry,"curve duplication changed exact geometry or reused ID");
        v.undoPull();check(v.sketchEntities_==confirmed,"duplicate Undo lost original curve");
        auto owner=v.constructions_.front();const auto before=BRepAdaptor_Curve(TopoDS::Edge(v.sketchPresentations_.at(id)->Shape())).Value(0);
        owner.frame.SetLocation(owner.frame.Location().Translated(gp_Vec(3,4,5)));check(v.updateConstruction(owner),"Workplane transform failed");
        const auto moved=BRepAdaptor_Curve(TopoDS::Edge(v.sketchPresentations_.at(id)->Shape())).Value(0);
        check(v.sketchEntities_==confirmed && moved.Distance(before.Translated(gp_Vec(3,4,5)))<1.e-9,"Workplane transform rewrote local spline or lost exact world placement");
        v.undoPull();check(v.sketchEntities_==confirmed,"owner Undo changed spline values");
        Sketch::Entity ellipse;ellipse.workplaneId=v.constructions_.front().id;ellipse.role=Sketch::Role::Construction;
        ellipse.geometry=Sketch::EllipticalArc{{{1.1234567890123,2.2345678901234},13.12345678901234,3.23456789012345,.1234567890123456},.3456789012345678,-1.234567890123456};
        auto values=confirmed;values.push_back(ellipse);check(v.previewSketchEntities(values,error),qPrintable(error));v.finishSketchEdit(true);
        v.selectSketchEntities({ellipse.id});editDialog(true);v.editSketchCurve();
        check(v.commandSession_ && v.commandSession_->sketch->after==values,"opening curve editor rounded untouched exact values");v.finishSketchEdit(false);
        v.undoPull();check(v.sketchEntities_==confirmed,"precision fixture Undo changed spline");
        NativeDocumentSnapshot saved;check(v.exportDocumentSnapshot(saved),qPrintable(error));QTemporaryDir temp;
        check(NativeDocumentStorage::save(temp.filePath("curves.odc"),saved,error),qPrintable(error));
        NativeDocumentSnapshot loaded;check(NativeDocumentStorage::load(temp.filePath("curves.odc"),loaded,error) && loaded.sketchEntities==confirmed,"session Save/Open lost spline values");
        interactive(w);w.documentDirty_=false;
    }
};
void runSketchAdvancedCurveSessions() {SketchAdvancedCurveSessionTest::run();}
