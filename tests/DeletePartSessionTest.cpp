#include "CadViewport.h"
#include "MainWindow.h"
#include <QApplication>
#include <QTreeWidget>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QSignalBlocker>
#include <Prs3d_ShadingAspect.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <StdSelect_ViewerSelector3d.hxx>
#include <Graphic3d_NameOfMaterial.hxx>
#include <stdexcept>
namespace {
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
void key(QWidget& widget,int key) {
    QKeyEvent press(QEvent::KeyPress,key,Qt::NoModifier);QApplication::sendEvent(&widget,&press);
    QKeyEvent release(QEvent::KeyRelease,key,Qt::NoModifier);QApplication::sendEvent(&widget,&release);
}
void focus(QWidget& widget) {
    widget.window()->activateWindow();widget.setFocus();QElapsedTimer timer;timer.start();
    while(QApplication::focusWidget()!=&widget && timer.elapsed()<2000) {
        QEventLoop loop;QTimer::singleShot(10,&loop,&QEventLoop::quit);loop.exec();
    }
    check(QApplication::focusWidget()==&widget,"test keyboard focus unavailable");
}
}
struct DeletePartSessionTest {
static void run() {
    MainWindow window;window.resize(1280,900);window.show();QApplication::processEvents();
    auto& v=*window.viewport_;auto& tree=*window.structure_;v.addBox();v.addCylinder();v.addCone();
    const auto first=v.partId(0),second=v.partId(1),third=v.partId(2);
    v.renamePart(0,"Keep identity");v.setPartColor(0,QColor(31,91,173));
    gp_Trsf placement;placement.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),0.4);placement.SetTranslationPart(gp_Vec(13,27,42));
    v.applyPartState(0,v.boxes_[0].min,v.boxes_[0].max,placement);
    v.boxes_[0].presentation->SetMaterial(Graphic3d_NOM_GOLD);
    v.setActivePart(first);
    check(!v.deleteSelectedParts() && v.undoStack_.empty(),"empty selection deleted a Part");
    v.selectParts({1});const auto owner=v.boxes_[1].presentation->GlobalSelOwner();
    focus(v);key(v,Qt::Key_Delete);
    check(!v.isPartActive(1) && v.isPartActive(0) && v.activePartId()==first && v.undoStack_.size()==1,"keyboard deletion changed wrong/active Part");
    check(v.context_->DisplayStatus(v.boxes_[1].presentation)==PrsMgr_DisplayStatus_None
        && !owner->IsSelected() && !v.context_->HasDetected() && v.context_->MainSelector()->NbPicked()==0,"deletion retained AIS owners/detection");
    v.undoPull();check(v.partId(1)==second && v.selectedPartIndices_.empty(),"Undo did not restore UUID/clear selection");
    // No part deletion from local face, edge or vertex selection, even when
    // the old whole-Part index remains cached.
    for(const auto type:{TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX}) {
        v.selectParts({1});TopExp_Explorer it(v.boxes_[1].shape,type);
        v.context_->ClearSelected(false);
        Handle(StdSelect_BRepOwner) local=new StdSelect_BRepOwner(it.Current(),v.boxes_[1].presentation,5,true);
        v.context_->AddOrRemoveSelected(local,false);const auto history=v.undoStack_.size();
        focus(v);key(v,Qt::Key_Delete);
        check(v.isPartActive(1) && v.undoStack_.size()==history && !v.deleteSelectedParts(),"local selection deleted its owning Part");
        std::unique_ptr<QMenu> menu(v.createSelectionMenu(QPoint(-10,-10)));
        check(!menu->findChild<QAction*>("context.delete_part"),"local selection offered Delete Part");
    }
    // Actual inline browser editor retains Delete as a text-editing key.
    v.selectParts({0});auto* row=window.root_->child(0);tree.setCurrentItem(row,0);tree.editItem(row,0);QApplication::processEvents();
    auto* editor=qobject_cast<QLineEdit*>(QApplication::focusWidget());check(editor,"browser editor not opened");
    editor->setText("abcdef");editor->setCursorPosition(1);key(*editor,Qt::Key_Delete);
    check(editor->text()=="acdef" && v.isPartActive(0) && v.undoStack_.empty(),"Delete interfered with inline text editing");key(*editor,Qt::Key_Return);
    // Armed commands and active sketch editing cannot be interrupted by Delete.
    TopExp_Explorer face(v.boxes_[0].shape,TopAbs_FACE);
    check(v.startMoveFace(0,TopoDS::Face(face.Current())),"Move Face guard fixture failed");
    focus(v);key(v,Qt::Key_Delete);
    check(v.commandSession_ && v.isPartActive(0) && !v.deleteParts({0}),"Delete interrupted active modeling command");v.cancelCommandSession();
    Construction::Object wp;wp.id="delete-wp";wp.name="WP";wp.kind=Construction::Kind::Workplane;
    v.restoreConstructions({wp});check(v.activateSketchWorkplane(wp.id,false),"sketch guard fixture failed");
    focus(v);key(v,Qt::Key_Delete);check(!v.deleteParts({0}) && v.isPartActive(0),"Delete interrupted sketch editing");v.endSketchEditing();
    // Hidden rows are deletable through browser focus and restore as hidden.
    v.setPartVisible(2,false);tree.clearSelection();tree.setCurrentItem(window.root_->child(2));window.root_->child(2)->setSelected(true);
    focus(tree);key(tree,Qt::Key_Delete);
    check(!v.isPartActive(2) && v.activePartId()==first,"hidden browser deletion failed");
    v.undoPull();check(v.partId(2)==third && !v.isPartVisible(2) && !window.root_->child(2)->isHidden()
        && !v.context_->IsDisplayed(v.boxes_[2].presentation),"Undo lost hidden browser/AIS state");
    // Viewport menu uses the same transaction and strict whole-Part selection.
    v.selectParts({1});std::unique_ptr<QMenu> menu(v.createSelectionMenu(QPoint(-10,-10)));
    auto* remove=menu->findChild<QAction*>("context.delete_part");check(remove,"viewport Delete Part action missing");remove->trigger();
    check(!v.isPartActive(1) && v.activePartId()==first,"viewport menu deleted wrong Part");v.undoPull();
    const auto before=v.boxes_;const auto material=before[0].presentation->Attributes()->ShadingAspect()->Material();
    const auto undoCount=v.undoStack_.size();
    // Native browser context menu deletes a mixed visible/hidden batch once.
    {const QSignalBlocker blocker(&tree);tree.clearSelection();window.root_->child(0)->setSelected(true);window.root_->child(2)->setSelected(true);}
    bool offered=false;
    QTimer::singleShot(0,&window,[&] {
        auto* popup=qobject_cast<QMenu*>(QApplication::activePopupWidget());if(!popup)return;
        auto* action=popup->findChild<QAction*>("structure.delete_part");offered=action && action->isEnabled();
        if(offered){popup->setActiveAction(action);key(*popup,Qt::Key_Return);}else popup->close();
    });
    emit tree.customContextMenuRequested(tree.visualItemRect(window.root_->child(0)).center());
    check(offered && !v.isPartActive(0) && !v.isPartActive(2) && v.isPartActive(1) && v.undoStack_.size()==undoCount+1,"multi-Part menu deletion was not atomic");
    check(v.activePartId().isEmpty() && window.root_->child(0)->isHidden() && window.root_->child(2)->isHidden(),"deleted Active Part/browser remained active");
    check(v.context_->DisplayStatus(before[0].presentation)==PrsMgr_DisplayStatus_None
        && v.context_->DisplayStatus(before[2].presentation)==PrsMgr_DisplayStatus_None,"deleted batch retained AIS objects");
    v.undoPull();check(v.activePartId().isEmpty(),"Undo silently reactivated deleted Part");
    for(size_t i=0;i<before.size();++i) {
        const auto& restored=v.boxes_[i];const auto& saved=before[i];
        check(restored.active && restored.persistentId==saved.persistentId && restored.shape.IsSame(saved.shape)
            && restored.name==saved.name && restored.color==saved.color && restored.visible==saved.visible
            && restored.presentation==saved.presentation && window.root_->child(int(i))->data(0,Qt::UserRole).toULongLong()==i,"Undo lost Part state/browser order");
        for(int r=1;r<=3;++r)for(int c=1;c<=4;++c)
            check(restored.orientation.Value(r,c)==saved.orientation.Value(r,c),"Undo changed transform");
    }
    check(v.boxes_[0].presentation->Attributes()->ShadingAspect()->Material().IsEqual(material),"Undo changed material");
    const auto redoCount=v.redoStack_.size();
    check(!v.deleteParts({1,9999}) && v.isPartActive(1) && v.redoStack_.size()==redoCount,"invalid batch partially applied or destroyed Redo");
    v.redoPull();check(!v.isPartActive(0) && v.partId(1)==second && !v.isPartActive(2),"Redo did not remove the same UUIDs");
    // Native Save/Open excludes tombstones and preserves survivor identity/order.
    NativeDocumentSnapshot snapshot,loaded;QString error;QTemporaryDir temp;
    check(v.exportDocumentSnapshot(snapshot) && snapshot.parts.size()==1 && snapshot.parts[0].id==second,"snapshot retained deleted parts");
    const auto path=temp.filePath("deleted.odc");
    check(NativeDocumentStorage::save(path,snapshot,error) && NativeDocumentStorage::load(path,loaded,error)
        && v.importDocumentSnapshot(loaded,error) && v.partSlotCount()==1 && v.partId(0)==second && v.undoStack_.empty(),"Save/Open lost deletion or survivor identity");
    check(v.deleteParts({0,0}) && v.undoStack_.size()==1 && !v.isPartActive(0),"duplicate request or deleting last Part failed");
    check(v.exportDocumentSnapshot(snapshot) && snapshot.parts.empty(),"last Part remained in snapshot");
    v.undoPull();check(v.partId(0)==second,"Undo last Part failed");v.redoPull();check(!v.isPartActive(0),"Redo last Part failed");
    window.documentDirty_=false;window.close();
}
};
void runDeletePartSessions(){DeletePartSessionTest::run();}
