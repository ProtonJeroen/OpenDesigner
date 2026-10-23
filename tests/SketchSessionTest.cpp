#include "CadViewport.h"
#include "MainWindow.h"
#include "ConstructionPanel.h"
#include <QApplication>
#include <QTreeWidget>
#include <QTemporaryDir>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QFileDialog>
#include <QLineEdit>
#include <QFileInfo>
#include <QMouseEvent>
#include <QKeyEvent>
#include <Prs3d_LineAspect.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <stdexcept>

namespace {
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void chooseFile(const QString& path) {
    QTimer::singleShot(50,[path] {
        for(auto* w:QApplication::topLevelWidgets()) if(auto* dialog=qobject_cast<QFileDialog*>(w);dialog && dialog->isVisible()) {
            dialog->setDirectory(QFileInfo(path).absolutePath());
            dialog->selectFile(QFileInfo(path).fileName());
            if(auto* edit=dialog->findChild<QLineEdit*>("fileNameEdit")) edit->setText(QFileInfo(path).fileName());QTimer::singleShot(50,dialog,[dialog]{QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);});return;
        }
    });
}
}
struct SketchSessionTest {
    static void run() {
        MainWindow window;window.show();QApplication::processEvents();auto& v=*window.viewport_;
        v.addBox();
        v.startConstruction(Construction::Method::PlaneXYZ);
        v.constructionPanel_->setVector(gp_Vec(0,0,60));v.confirmCommandSession();const auto wp=v.constructions_.back();
        Sketch::Entity segment;segment.workplaneId=wp.id;segment.name="Profile α";segment.geometry=Sketch::Segment{{20,20},{60,20}};
        auto circle=segment;circle.id="circle";circle.name="Construction circle";circle.role=Sketch::Role::Construction;circle.geometry=Sketch::Circle{{35,45},10};
        auto arc=segment;arc.id="arc";arc.geometry=Sketch::Arc{{70,55},8,0,-1.5};
        auto line=segment;line.id="infinite";line.role=Sketch::Role::Construction;line.geometry=Sketch::InfiniteLine{{0,70},{1,0}};
        const std::vector<Sketch::Entity> entities{segment,circle,arc,line};QString error;
        const auto initialHistory=v.undoStack_.size();window.documentDirty_=false;
        check(v.previewSketchEntities(entities,error),qPrintable(error));
        check(v.sketchEntities_.empty() && v.undoStack_.size()==initialHistory && !window.documentDirty_,"preview changed document/history/dirty state");
        check(v.sketchPreviews_.size()==4 && v.commandPanel_->isVisible(),"preview presentation missing");
        NativeDocumentSnapshot snapshot;check(v.exportDocumentSnapshot(snapshot) && snapshot.sketchEntities.empty(),"Save captured unconfirmed entities");
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&v,&escape);
        check(!v.commandSession_ && v.sketchPreviews_.empty() && v.sketchEntities_.empty() && !window.documentDirty_,"Esc failed to cancel 2D transaction");
        check(v.previewSketchEntities(entities,error),qPrintable(error));v.confirmCommandSession();
        check(v.sketchEntities_==entities && v.undoStack_.size()==initialHistory+1 && window.documentDirty_,"2D confirmation not one operation");
        window.documentDirty_=false;
        check(v.previewSketchEntities(entities,error),qPrintable(error));v.confirmCommandSession();
        check(v.undoStack_.size()==initialHistory+1 && !window.documentDirty_,"unchanged confirmation created history or dirtied document");
        auto* planeRow=window.constructionRoot_->child(0);
        check(planeRow->data(0,Qt::UserRole).toString()==wp.id && planeRow->childCount()==4,"2D browser rows not under owning Workplane");
        check(planeRow->child(0)->text(0)==segment.name && planeRow->child(1)->toolTip(0).contains("Construction"),"browser identity/role missing");
        check(v.sketchPresentations_.at(circle.id)->Attributes()->WireAspect()->Aspect()->Type()==Aspect_TOL_DASH
            && v.sketchPresentations_.at(segment.id)->Attributes()->WireAspect()->Aspect()->Type()==Aspect_TOL_SOLID,"roles have no visual line distinction");
        v.view_->SetProj(V3d_Zpos);v.fitAll();v.view_->Redraw();
        const auto p=Sketch::worldPoint({40,20},wp.frame);int x,y;v.view_->Convert(p.X(),p.Y(),p.Z(),x,y);
        check(v.sketchMousePress(QPoint(x,y),Qt::NoModifier) && v.sketchSelection()==QStringList{segment.id},"viewport 2D pick failed");
        check(v.selectionContext().kind==CadViewport::SelectionContext::Kind::Sketches,"2D selection misclassified as Face/Part");
        check(v.sketchMousePress(QPoint(x,y),Qt::ControlModifier) && v.sketchSelection().empty(),"Ctrl did not remove 2D selection");
        v.selectSketchEntities({segment.id,circle.id});
        std::unique_ptr<QMenu> menu(v.createSelectionMenu(QPoint()));check(menu->findChild<QAction*>("context.delete_sketch"),"2D context menu missing");
        v.undoPull();check(v.sketchEntities_.empty() && v.sketchSelection().empty() && window.structure_->selectedItems().empty(),"Undo retained 2D geometry/selection");
        v.redoPull();check(v.sketchEntities_==entities && v.context_->NbSelected()==0,"Redo failed or restored selection");
        // Browser eye, hidden and locked entities, and restoration via shared history.
        window.structure_->itemClicked(window.constructionRoot_->child(0)->child(0),1);
        check(!v.sketchEntities_[0].visible && !v.context_->IsDisplayed(v.sketchPresentations_.at(segment.id)),"browser eye did not hide 2D geometry");
        v.undoPull();check(v.sketchEntities_[0].visible,"visibility Undo failed");
        auto locked=v.sketchEntities_[0];locked.selectable=false;check(v.updateSketchEntity(locked,error),qPrintable(error));
        v.selectSketchEntities({locked.id});check(v.context_->NbSelected()==0,"locked 2D entity selectable");v.undoPull();
        v.selectParts({0});check(v.sketchSelection().empty() && v.selectionContext().kind==CadViewport::SelectionContext::Kind::Parts,"2D selection leaked into 3D selection");
        // A real Alt click must pass through a 2D entity onto the solid.
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(x,y),QPointF(v.mapToGlobal(QPoint(x,y))),Qt::LeftButton,Qt::LeftButton,Qt::AltModifier);
        QApplication::sendEvent(&v,&press);
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(x,y),QPointF(v.mapToGlobal(QPoint(x,y))),Qt::LeftButton,Qt::NoButton,Qt::AltModifier);
        QApplication::sendEvent(&v,&release);
        check(v.selectionContext().kind==CadViewport::SelectionContext::Kind::Parts,"Alt Part pick blocked by 2D geometry");
        // Workplane preview carries its contents, but Save and Cancel retain confirmed local values.
        v.selectSketchEntities({segment.id});
        v.startConstruction(Construction::Method::PlaneMove,wp.id);
        v.constructionPanel_->setVector(gp_Vec(10,20,30));
        check(v.sketchPreviews_.size()==4 && v.sketchEntities_==entities,"Workplane preview did not carry 2D geometry independently");
        v.exportDocumentSnapshot(snapshot);check(snapshot.sketchEntities==entities && snapshot.constructions[0].frame.Location().IsEqual(wp.frame.Location(),1.e-10),"WP preview leaked into Save");
        v.cancelCommandSession();check(v.sketchPreviews_.empty() && v.constructions_[0].frame.Location().IsEqual(wp.frame.Location(),1.e-10),"WP Cancel changed frame");
        check(v.sketchSelection()==QStringList{segment.id} && v.selectionContext().kind==CadViewport::SelectionContext::Kind::Sketches,"WP Cancel restored stale 2D selection owners");
        const auto unchangedPlaneHistory=v.undoStack_.size();
        v.startConstruction(Construction::Method::PlaneMove,wp.id);v.confirmCommandSession();
        check(v.undoStack_.size()==unchangedPlaneHistory && v.context_->IsDisplayed(v.sketchPresentations_.at(segment.id)),"unchanged WP confirmation hid 2D contents or created history");
        auto transformed=wp;gp_Trsf rotation;rotation.SetRotation(gp_Ax1(gp_Pnt(10,20,30),gp_Dir(1,2,3)),0.8);transformed.frame.Transform(rotation);
        check(v.updateConstruction(transformed),"WP transform failed");
        Bnd_Box actual,expected;BRepBndLib::AddOptimal(v.sketchPresentations_.at(segment.id)->Shape(),actual,Standard_False,Standard_False);
        BRepBndLib::AddOptimal(Sketch::shape(segment,transformed.frame),expected,Standard_False,Standard_False);
        check(actual.CornerMin().IsEqual(expected.CornerMin(),1.e-9) && actual.CornerMax().IsEqual(expected.CornerMax(),1.e-9)
            && v.sketchEntities_==entities,"WP transform did not carry 2D shape or changed local coordinates");
        v.undoPull();v.redoPull();check(v.sketchEntities_==entities,"WP history damaged local geometry");
        // Workplane hide/lock propagates without changing entity properties.
        auto hidden=transformed;hidden.visible=false;check(v.updateConstruction(hidden),"hide WP failed");
        check(!v.context_->IsDisplayed(v.sketchPresentations_.at(segment.id)) && v.sketchEntities_==entities,"hidden WP leaked 2D geometry");v.undoPull();
        auto lockedPlane=transformed;lockedPlane.selectable=false;v.updateConstruction(lockedPlane);
        v.selectSketchEntities({segment.id});check(v.context_->NbSelected()==0,"locked WP allowed child picks");v.undoPull();
        const auto beforeDelete=v.undoStack_.size();v.deleteConstructions({wp.id});
        check(v.constructions_.empty() && v.sketchEntities_.empty() && v.undoStack_.size()==beforeDelete+1,"WP deletion was not atomic");
        v.undoPull();check(v.constructions_.size()==1 && v.sketchEntities_==entities,"WP Undo did not restore contents");
        const auto before3D=v.boxes_[0].orientation;
        v.selectParts({0});v.beginCommandSession(CadViewport::CommandSession::Kind::Move,{0},gp_Dir(1,0,0),gp_Pnt());
        check(v.previewCommand(12),"3D Move preview failed");v.closeCommandSession(true);
        const auto history3D=v.undoStack_.size();
        check(v.deleteSketchEntities({circle.id,arc.id},error) && v.undoStack_.size()==history3D+1,"group 2D Delete not one Undo step");
        v.undoPull();check(v.sketchEntities_==entities,"2D Delete Undo failed");
        v.undoPull();check(v.boxes_[0].orientation.TranslationPart().IsEqual(before3D.TranslationPart(),1.e-9),"mixed history failed to undo 3D Move");
        v.redoPull();v.redoPull();v.undoPull();check(v.sketchEntities_==entities,"mixed Redo/Undo corrupted sketches");
        // Save/Save As retain selection, both history stacks and pending confirmed-state rules.
        QTemporaryDir dir;check(dir.isValid(),"temporary directory failed");
        v.selectSketchEntities({segment.id});const auto history=v.undoStack_.size(),redo=v.redoStack_.size();
        window.documentPath_=dir.filePath("native.odc");check(window.saveDocument(),"Save failed");
        chooseFile(dir.filePath("copy.odc"));check(window.saveDocumentAs(),"Save As failed");
        check(v.undoStack_.size()==history && v.redoStack_.size()==redo && v.sketchSelection()==QStringList{segment.id},"Save cleared history or selection");
        auto orphan=entities;orphan[0].workplaneId="missing";
        check(!v.previewSketchEntities(orphan,error) && v.sketchEntities_==entities && v.undoStack_.size()==history,"failed edit damaged document");
        v.exportDocumentSnapshot(snapshot);auto invalid=snapshot;invalid.sketchEntities[0].id=wp.id;
        check(!v.importDocumentSnapshot(invalid,error) && v.sketchEntities_==entities && v.undoStack_.size()==history,"failed Open damaged document");
        window.documentDirty_=false;chooseFile(dir.filePath("copy.odc"));window.openDocument();
        check(v.sketchEntities_==entities && v.undoStack_.empty() && v.redoStack_.empty() && v.sketchSelection().empty()
            && window.constructionRoot_->child(0)->childCount()==4 && !window.documentDirty_,"Open did not rebuild clean document/browser");
        v.previewSketchEntities({},error);window.newDocument();
        check(v.sketchEntities_.empty() && v.sketchPresentations_.empty() && v.sketchPreviews_.empty()
            && !v.commandSession_ && v.undoStack_.empty() && window.constructionRoot_->childCount()==0,"New retained 2D session/data");
        window.documentDirty_=false;
    }
};
void runSketchSessions() {SketchSessionTest::run();}
