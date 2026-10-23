#include "CadViewport.h"
#include "MainWindow.h"
#include "ConstructionPanel.h"
#include <QApplication>
#include <QAction>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QMenu>
#include <QTreeWidget>
#include <QTemporaryDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QTimer>
#include <QKeyEvent>
#include <BRepAdaptor_Curve.hxx>
#include <TopoDS.hxx>
#include <cmath>
#include <stdexcept>

namespace {
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void chooseFile(const QString& path) {
    QTimer::singleShot(50,[path] {
        for(auto* widget:QApplication::topLevelWidgets()) if(auto* d=qobject_cast<QFileDialog*>(widget);d && d->isVisible()) {
            d->setDirectory(QFileInfo(path).absolutePath());d->selectFile(QFileInfo(path).fileName());
            if(auto* edit=d->findChild<QLineEdit*>("fileNameEdit")) edit->setText(QFileInfo(path).fileName());
            QTimer::singleShot(50,d,[d]{QMetaObject::invokeMethod(d,"accept",Qt::DirectConnection);});return;
        }
    });
}
void key(QWidget& widget,int code) {QKeyEvent event(QEvent::KeyPress,code,Qt::NoModifier);QApplication::sendEvent(&widget,&event);}
}
struct SketchReleaseSessionTest {
    static void action(MainWindow& w,const QString& name) {
        auto* a=w.findChild<QAction*>(name);check(a && a->isEnabled(),qPrintable(name));a->trigger();QApplication::processEvents();
    }
    static void run() {
        MainWindow w;w.show();QApplication::processEvents();auto& v=*w.viewport_;QString error;
        check(v.boxes_.empty() && !w.findChild<QMenu*>("sketch.axes")->isEnabled(),"axes enabled without Edit Sketch");
        for(auto method:{Construction::Method::PlaneXY,Construction::Method::PlaneXZ,Construction::Method::PlaneYZ}) {
            const auto history=v.undoStack_.size();const auto count=v.constructions_.size();
            const auto id=QString("construction.method.%1").arg(int(method));
            action(w,id);check(v.commandSession_->valid && v.constructionPanel_->value->value()==0,"preset has no valid origin preview");
            v.constructionPanel_->value->setValue(-7.25);
            v.constructionPanel_->finishEditing();v.previewConstruction();
            check(v.commandSession_->construction->candidate.frame.Direct(),"preset preview not right-handed");
            key(*v.constructionPanel_,Qt::Key_Escape);
            check(v.constructions_.size()==count && v.undoStack_.size()==history && !v.commandSession_,"preset Cancel changed document");
            action(w,id);v.constructionPanel_->value->setValue(12.5);key(*v.constructionPanel_,Qt::Key_Return);
            check(v.constructions_.size()==count+1 && v.undoStack_.size()==history+1 && v.boxes_.empty(),"preset commit failed without Parts");
            const auto owner=v.constructions_.back();
            const gp_Dir normal=method==Construction::Method::PlaneXY?gp_Dir(0,0,1):method==Construction::Method::PlaneXZ?gp_Dir(0,-1,0):gp_Dir(1,0,0);
            check(owner.frame.Direction().Dot(normal)>1-1.e-12 && owner.frame.Location().IsEqual(gp_Pnt().Translated(gp_Vec(normal)*12.5),1.e-12),"preset frame/offset incorrect");
            v.undoPull();check(v.constructions_.size()==count,"preset Undo failed");v.redoPull();
            check(v.constructions_.back().id==owner.id && v.context_->NbSelected()==0,"preset Redo changed ID/selection");
            v.selectConstructions({owner.id});action(w,"sketch.edit");
            check(v.activeSketchWorkplaneId_==owner.id && w.findChild<QMenu*>("sketch.axes")->isEnabled(),"preset not immediately editable");
            const auto before=v.sketchEntities_;const auto undo=v.undoStack_.size();w.documentDirty_=false;
            action(w,"sketch.axes.both");check(v.sketchEntities_==before && v.commandSession_->sketch->after.size()==before.size()+2 && v.commandPanel_->isVisible(),"axes preview changed canonical values or has no OK/Cancel");
            v.commandButtons_->button(QDialogButtonBox::Cancel)->click();
            check(v.sketchEntities_==before && v.undoStack_.size()==undo && !w.documentDirty_,"axes Cancel changed document/history");
            // Both adds two ordinary entities in exactly one transaction.
            action(w,"sketch.axes.both");v.commandButtons_->button(QDialogButtonBox::Ok)->click();
            const auto confirmed=v.sketchEntities_;
            check(confirmed.size()==before.size()+2 && v.undoStack_.size()==undo+1 && w.documentDirty_,"Both is not one confirmed Undo step");
            const auto& x=confirmed[before.size()];const auto& y=confirmed.back();
            check(x.geometry==Sketch::Geometry(Sketch::InfiniteLine{{0,0},{1,0}})
                && y.geometry==Sketch::Geometry(Sketch::InfiniteLine{{0,0},{0,1}})
                && x.role==Sketch::Role::Construction && x.workplaneId==owner.id,"axis coordinates, role or owner incorrect");
            check(w.constructionRoot_->child(int(count))->childCount()==2 && v.sketchPickable(x),"axes not selectable or missing from browser");
            v.selectSketchEntities({x.id});v.undoPull();check(v.sketchEntities_==before && v.sketchSelection().empty() && w.structure_->selectedItems().empty(),"axes Undo failed selection cleanup");
            v.redoPull();check(v.sketchEntities_==confirmed && v.context_->NbSelected()==0,"axes Redo changed stable values/selection");
            w.documentDirty_=false;action(w,"sketch.axes.both");action(w,"sketch.axes.x");action(w,"sketch.axes.y");
            check(!v.commandSession_ && v.sketchEntities_==confirmed && v.undoStack_.size()==undo+1 && !w.documentDirty_,"repeated axes dirtied document or created duplicates/history");
            v.endSketchEditing();
        }
        // A rotated owner carries exact local axes; the displayed OCCT curves follow it.
        auto rotated=v.constructions_.back();gp_Trsf transform;transform.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),0.7);rotated.frame.Transform(transform);
        check(v.updateConstruction(rotated) && v.activateSketchWorkplane(rotated.id),"rotated Workplane not editable");
        const auto axesBefore=v.sketchEntities_;action(w,"sketch.axes.both");check(v.sketchEntities_==axesBefore && !v.commandSession_,"rotation caused duplicate axes");
        for(const auto& e:v.sketchEntities_) if(e.workplaneId==rotated.id) {
            const auto& line=std::get<Sketch::InfiniteLine>(e.geometry);
            BRepAdaptor_Curve displayed(TopoDS::Edge(v.sketchPresentations_.at(e.id)->Shape()));
            const gp_Dir expected=gp_Dir(gp_Vec(rotated.frame.XDirection())*line.direction.x+gp_Vec(rotated.frame.YDirection())*line.direction.y);
            check(displayed.GetType()==GeomAbs_Line && displayed.Line().Direction().IsParallel(expected,1.e-12)
                && displayed.Line().Distance(Sketch::worldPoint({0,0},rotated.frame))<1.e-10,"rotated axis display left local frame");
        }
        // Individual choices only add the missing line, even with hidden/locked sources.
        const auto owner=rotated.id;const auto yId=v.sketchEntities_.back().id;
        check(v.deleteSketchEntities({yId},error),qPrintable(error));action(w,"sketch.axes.y");key(v,Qt::Key_Return);
        check(v.sketchEntities_.size()==axesBefore.size(),"Local Y did not add exactly one axis");
        auto hidden=v.sketchEntities_.back();hidden.visible=false;hidden.selectable=false;check(v.updateSketchEntity(hidden,error),qPrintable(error));
        w.documentDirty_=false;const auto history=v.undoStack_.size();action(w,"sketch.axes.both");
        check(!v.commandSession_ && v.undoStack_.size()==history && !w.documentDirty_,"hidden/locked axis duplicated");
        auto x=v.sketchEntities_[v.sketchEntities_.size()-2];check(v.deleteSketchEntities({x.id},error),qPrintable(error));
        action(w,"sketch.axes.x");key(v,Qt::Key_Return);check(v.sketchEntities_.size()==axesBefore.size(),"Local X did not add one axis");
        // Save/Open use schema v3 and rebuild browser/selection, without Parts.
        QTemporaryDir dir;const auto path=dir.filePath("presets-axes.odc");w.documentPath_=path;
        NativeDocumentSnapshot saved,loaded;v.exportDocumentSnapshot(saved);
        const auto undo=v.undoStack_.size();check(w.saveDocument() && v.undoStack_.size()==undo,"axes Save cleared history");
        check(NativeDocumentStorage::load(path,loaded,error) && loaded.schemaVersion==NativeDocumentStorage::CurrentSchemaVersion && loaded.sketchEntities==saved.sketchEntities,"axes roundtrip changed exact values");
        for(size_t i=0;i<saved.constructions.size();++i) check(Construction::equivalent(saved.constructions[i],loaded.constructions[i]),"preset/rotated frame roundtrip changed");
        chooseFile(path);w.openDocument();
        check(v.sketchEntities_==saved.sketchEntities && v.undoStack_.empty() && v.redoStack_.empty() && v.activeSketchWorkplaneId_.isEmpty() && w.constructionRoot_->childCount()==3,"Open did not restore axes/reset editing context");
        check(v.activateSketchWorkplane(owner),"restored Workplane not editable");action(w,"sketch.axes.both");check(!v.commandSession_,"Open lost duplicate detection");
        // Existing 3D creation/history still preserves the 2D collection.
        v.addBox();const auto shapes=v.sketchEntities_;
        v.beginCommandSession(CadViewport::CommandSession::Kind::Move,{0},gp_Dir(1,0,0));
        check(v.previewCommand(12.5),"3D Move preview failed after axes Open");v.confirmCommandSession();v.undoPull();v.redoPull();
        check(v.sketchEntities_==shapes && v.activeSketchWorkplaneId_.isEmpty(),"3D operation disturbed 2D axes");
        w.documentDirty_=false;w.newDocument();check(v.sketchEntities_.empty() && v.constructions_.empty() && !w.findChild<QMenu*>("sketch.axes")->isEnabled(),"New retained axes/context");
        w.documentDirty_=false;
    }
};
void runSketchReleaseSessions() {SketchReleaseSessionTest::run();}
