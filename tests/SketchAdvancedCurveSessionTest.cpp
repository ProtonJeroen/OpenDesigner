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
        NativeDocumentSnapshot saved;check(v.exportDocumentSnapshot(saved),qPrintable(error));QTemporaryDir temp;
        check(NativeDocumentStorage::save(temp.filePath("curves.odc"),saved,error),qPrintable(error));
        NativeDocumentSnapshot loaded;check(NativeDocumentStorage::load(temp.filePath("curves.odc"),loaded,error) && loaded.sketchEntities==confirmed,"session Save/Open lost spline values");
        w.documentDirty_=false;
    }
};
void runSketchAdvancedCurveSessions() {SketchAdvancedCurveSessionTest::run();}
