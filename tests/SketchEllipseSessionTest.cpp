#include "CadViewport.h"
#include "MainWindow.h"
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <BRepAdaptor_Curve.hxx>
#include <TopoDS.hxx>
#include <stdexcept>

namespace {
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void key(QWidget& w,int code) {QKeyEvent e(QEvent::KeyPress,code,Qt::NoModifier);QApplication::sendEvent(&w,&e);}
void chooseFile(const QString& path) {
    QTimer::singleShot(50,[path] {
        for(auto* widget:QApplication::topLevelWidgets()) if(auto* dialog=qobject_cast<QFileDialog*>(widget);dialog && dialog->isVisible()) {
            dialog->setDirectory(QFileInfo(path).absolutePath());dialog->selectFile(QFileInfo(path).fileName());
            if(auto* edit=dialog->findChild<QLineEdit*>("fileNameEdit")) edit->setText(QFileInfo(path).fileName());
            QTimer::singleShot(50,dialog,[dialog]{QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);});return;
        }
    });
}
}
struct SketchEllipseSessionTest {
    static void clickCurve(CadViewport& v,const Sketch::Entity& e) {
        const auto& ellipse=std::get<Sketch::Ellipse>(e.geometry);
        const auto world=Sketch::worldPoint(Sketch::ellipsePoint(ellipse,0.8),v.constructionObject(e.workplaneId)->frame);
        int x,y;v.view_->Convert(world.X(),world.Y(),world.Z(),x,y);const QPoint point(x,y);
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(point),QPointF(v.mapToGlobal(point)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(point),QPointF(v.mapToGlobal(point)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(&v,&press);QApplication::sendEvent(&v,&release);
    }
    static void run() {
        MainWindow window;window.show();QApplication::processEvents();auto& v=*window.viewport_;QString error;
        v.startConstruction(Construction::Method::PlaneXY);v.confirmCommandSession();
        auto owner=v.constructions_.front();owner.frame=gp_Ax3(gp_Pnt(10,20,30),gp_Dir(1,2,3),gp_Dir(2,-1,0));
        check(v.updateConstruction(owner) && v.activateSketchWorkplane(owner.id),"cannot create/activate rotated owner");
        Sketch::Entity ellipse;ellipse.workplaneId=owner.id;ellipse.role=Sketch::Role::Construction;
        ellipse.geometry=Sketch::Ellipse{{-25,0},20,9,0.35};
        auto arc=ellipse;arc.id="independent-profile-arc";arc.role=Sketch::Role::Profile;
        arc.geometry=Sketch::EllipticalArc{{{25,0},15,7,-0.2},5.8,-4.0};
        const std::vector entities{ellipse,arc};const auto history=v.undoStack_.size();window.documentDirty_=false;
        check(v.previewSketchEntities(entities,error),qPrintable(error));
        check(v.sketchEntities_.empty() && !window.documentDirty_ && v.sketchPreviews_.size()==2,"ellipse preview changed document");
        key(v,Qt::Key_Escape);check(v.sketchEntities_.empty() && v.sketchPreviews_.empty() && v.undoStack_.size()==history,"ellipse Cancel changed history");
        check(v.previewSketchEntities(entities,error),qPrintable(error));key(v,Qt::Key_Return);
        check(v.sketchEntities_==entities && v.undoStack_.size()==history+1 && window.documentDirty_,"ellipse confirmation not one step");
        auto* row=window.constructionRoot_->child(0);
        check(row->childCount()==2 && row->child(0)->text(0)=="Ellipse" && row->child(1)->text(0)=="Elliptical Arc","conic browser labels incorrect");
        for(const auto& e:entities) check(BRepAdaptor_Curve(TopoDS::Edge(v.sketchPresentations_.at(e.id)->Shape())).GetType()==GeomAbs_Ellipse,"preview/confirmed shape lost exact ellipse");
        v.lookNormalToSketch();clickCurve(v,ellipse);
        check(v.sketchSelection()==QStringList{ellipse.id} && v.selectionContext().kind==CadViewport::SelectionContext::Kind::Sketches,"ellipse viewport picking failed or became 3D selection");
        v.undoPull();check(v.sketchEntities_.empty() && v.sketchSelection().empty() && window.structure_->selectedItems().empty(),"ellipse Undo retained selection");
        v.redoPull();check(v.sketchEntities_==entities && v.context_->NbSelected()==0,"ellipse Redo lost IDs or retained selection");
        auto hidden=ellipse;hidden.visible=false;check(v.updateSketchEntity(hidden,error),qPrintable(error));
        check(!v.sketchPickable(hidden) && !v.context_->IsDisplayed(v.sketchPresentations_.at(ellipse.id)),"hidden ellipse displayed/pickable");v.undoPull();
        auto locked=ellipse;locked.selectable=false;check(v.updateSketchEntity(locked,error),qPrintable(error));v.selectSketchEntities({ellipse.id});
        check(!v.sketchPickable(locked) && v.context_->NbSelected()==0,"locked ellipse selected");v.undoPull();
        auto hiddenOwner=owner;hiddenOwner.visible=false;check(v.updateConstruction(hiddenOwner),"hide owner failed");
        check(!v.context_->IsDisplayed(v.sketchPresentations_.at(ellipse.id)),"hidden owner leaked ellipse");v.undoPull();
        auto lockedOwner=owner;lockedOwner.selectable=false;check(v.updateConstruction(lockedOwner),"lock owner failed");
        v.selectSketchEntities({ellipse.id});check(v.context_->NbSelected()==0,"locked owner allowed ellipse pick");v.undoPull();
        auto moved=owner;gp_Trsf t;t.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(0,1,0)),0.6);t.SetTranslationPart(gp_Vec(50,20,-10));moved.frame.Transform(t);
        check(v.updateConstruction(moved),"owner transform failed");
        const auto world=Sketch::worldPoint(Sketch::ellipsePoint(std::get<Sketch::Ellipse>(ellipse.geometry),0.4),moved.frame);
        check(v.sketchEntities_==entities && BRepAdaptor_Curve(TopoDS::Edge(v.sketchPresentations_.at(ellipse.id)->Shape())).Value(0.4).IsEqual(world,1.e-9),"owner transform rewrote local ellipse or lost placement");v.undoPull();
        check(v.deleteSketchEntities({ellipse.id},error) && v.sketchEntities_==std::vector{arc},"ellipse deletion changed independent arc");v.undoPull();
        QTemporaryDir dir;window.documentPath_=dir.filePath("ellipses.odc");v.selectSketchEntities({arc.id});
        const auto undo=v.undoStack_.size(),redo=v.redoStack_.size();check(window.saveDocument(),"ellipse Save failed");
        chooseFile(dir.filePath("copy.odc"));check(window.saveDocumentAs(),"ellipse Save As failed");
        check(v.undoStack_.size()==undo && v.redoStack_.size()==redo && v.sketchSelection()==QStringList{arc.id},"Save/Save As cleared ellipse selection/history");
        auto pending=ellipse;pending.id="pending-ellipse";auto candidates=entities;candidates.push_back(pending);
        check(v.previewSketchEntities(candidates,error) && window.saveDocument() && v.commandSession_,"Save lost pending ellipse session");
        NativeDocumentSnapshot saved;check(NativeDocumentStorage::load(window.documentPath_,saved,error) && saved.schemaVersion==NativeDocumentStorage::CurrentSchemaVersion && saved.sketchEntities==entities,"Save included pending ellipse or lost exact values");
        for(int version:{3,4,6}) {auto invalid=saved;invalid.schemaVersion=version;check(!v.importDocumentSnapshot(invalid,error) && v.commandSession_ && v.sketchEntities_==entities,"invalid schema import damaged current ellipse document");}
        chooseFile(window.documentPath_);window.openDocument();
        check(v.sketchEntities_==entities && v.undoStack_.empty() && v.redoStack_.empty() && v.sketchSelection().empty() && !v.commandSession_
            && window.constructionRoot_->child(0)->childCount()==2,"Open failed to restore conics/reset session/history/browser");
        v.addBox();v.beginCommandSession(CadViewport::CommandSession::Kind::Move,{0},gp_Dir(1,0,0));
        check(v.previewCommand(12),"3D Move preview failed with conics present");v.confirmCommandSession();v.undoPull();v.redoPull();
        check(v.sketchEntities_==entities,"3D history disturbed conics");
        const auto deleteHistory=v.undoStack_.size();v.deleteConstructions({owner.id});
        check(v.sketchEntities_.empty() && v.undoStack_.size()==deleteHistory+1,"owner deletion lost grouped conic semantics");v.undoPull();
        check(v.sketchEntities_==entities,"owner Undo lost elliptic values");
        check(v.previewSketchEntities(candidates,error),qPrintable(error));window.documentDirty_=false;window.newDocument();
        check(v.sketchEntities_.empty() && v.sketchPreviews_.empty() && !v.commandSession_ && v.undoStack_.empty(),"New retained elliptic values or session");window.documentDirty_=false;
    }
};
void runSketchEllipseSessions() {SketchEllipseSessionTest::run();}
