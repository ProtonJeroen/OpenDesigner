#include "MainWindow.h"
#include "CadViewport.h"
#include <QApplication>
#include <QTimer>
#include <QFileDialog>
#include <QMessageBox>
#include <QAbstractButton>
#include <QTreeWidget>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QCloseEvent>
#include <QAction>
#include <Graphic3d_Camera.hxx>
#include <Bnd_Box.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <cmath>
#include <stdexcept>

namespace {
void check(bool ok,const char* text) { if(!ok) throw std::runtime_error(text); }
void chooseFile(const QString& path) {
    QTimer::singleShot(50,[path] {
        for(auto* widget:QApplication::topLevelWidgets()) if(auto* dialog=qobject_cast<QFileDialog*>(widget)) {
            if(!dialog->isVisible()) continue;
            dialog->setDirectory(QFileInfo(path).absolutePath());
            dialog->selectFile(QFileInfo(path).fileName());
            if (auto* edit=dialog->findChild<QLineEdit*>("fileNameEdit")) edit->setText(QFileInfo(path).fileName());
            QTimer::singleShot(50,dialog,[dialog] { QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection); });
            return;
        }
    });
}
void answer(QMessageBox::StandardButton button) {
    QTimer::singleShot(50,[button] {
        for(auto* widget:QApplication::topLevelWidgets()) if(auto* dialog=qobject_cast<QMessageBox*>(widget)) {
            if(dialog->isVisible()) { dialog->button(button)->click(); return; }
        }
    });
}
}
struct NativeDocumentSessionTest {
    static void partless() {
        QTemporaryDir temp;check(temp.isValid(),"partless temporary directory failed");
        MainWindow window;window.show();QApplication::processEvents();auto& v=*window.viewport_;
        for(int scene=0;scene<4;++scene) {
            if(scene==1) {v.startConstruction(Construction::Method::PlaneXYZ);v.confirmCommandSession();}
            if(scene>=2) {
                std::vector<Sketch::Entity> entities;
                Sketch::Entity line;line.workplaneId=v.constructions_.front().id;
                line.role=scene==2?Sketch::Role::Construction:Sketch::Role::Profile;line.geometry=Sketch::Segment{{-20,10},{30,10}};entities.push_back(line);
                auto circle=line;circle.id="partless-circle";circle.geometry=Sketch::Circle{{5,20},12};entities.push_back(circle);
                QString error;check(v.previewSketchEntities(entities,error,false),qPrintable(error));v.finishSketchEdit(true);
            }
            if(scene>0) check(v.activateSketchWorkplane(v.constructions_.front().id),"partless Workplane activation failed");
            QApplication::processEvents();NativeDocumentSnapshot captured,loaded;QString error;
            check(v.exportDocumentSnapshot(captured),"partless capture failed");
            check(v.boxes_.empty() && captured.camera.eye.SquareDistance(captured.camera.center)>1.e-16,"planar fit left a degenerate live camera or fabricated Part");
            const auto path=temp.filePath(QString("partless-%1.odc").arg(scene));
            if(!NativeDocumentStorage::save(path,captured,error)) throw std::runtime_error(QString("Partless scene %1: %2; camera scale %3 eye (%4,%5,%6) center (%7,%8,%9), up (%10,%11,%12)")
                .arg(scene).arg(error).arg(captured.camera.scale,0,'g',16)
                .arg(captured.camera.eye.X()).arg(captured.camera.eye.Y()).arg(captured.camera.eye.Z())
                .arg(captured.camera.center.X()).arg(captured.camera.center.Y()).arg(captured.camera.center.Z())
                .arg(captured.camera.up.X()).arg(captured.camera.up.Y()).arg(captured.camera.up.Z()).toStdString());
            check(NativeDocumentStorage::load(path,loaded,error),qPrintable(error));
            check(loaded.parts.empty() && loaded.constructions.size()==captured.constructions.size() && loaded.sketchEntities==captured.sketchEntities,"partless roundtrip changed contents or fabricated Part");
            for(size_t i=0;i<captured.constructions.size();++i) check(Construction::equivalent(loaded.constructions[i],captured.constructions[i]),"partless roundtrip changed Workplane");
            window.documentPath_=path;const auto history=v.undoStack_.size();
            if(!v.sketchEntities_.empty()) v.selectSketchEntities({v.sketchEntities_.front().id});
            const auto selected=v.sketchSelection();check(window.saveDocument(),"partless GUI Save failed");
            check(v.undoStack_.size()==history && v.sketchSelection()==selected,"partless Save cleared history/selection");
            chooseFile(path);window.openDocument();
            check(window.root_->childCount()==0 && window.constructionRoot_->childCount()==int(captured.constructions.size()),"partless Open browser rebuild failed");
            check(v.undoStack_.empty() && v.redoStack_.empty() && v.sketchSelection().empty() && v.activeSketchWorkplaneId_.isEmpty(),"partless Open retained tools/history/selection");
            NativeDocumentSnapshot restored;v.exportDocumentSnapshot(restored);
            check(restored.camera.eye.IsEqual(captured.camera.eye,1.e-8) && restored.camera.center.IsEqual(captured.camera.center,1.e-8)
                && restored.camera.up.IsEqual(captured.camera.up,1.e-8) && std::abs(restored.camera.scale-captured.camera.scale)<1.e-8,"partless Open changed camera");
            v.fitAll();check(v.exportDocumentSnapshot(restored) && NativeDocumentStorage::save(path,restored,error),"partless Fit All made camera unsavable");
        }
        // External OCCT fitting can collapse an orthographic camera too. Save
        // canonicalizes a copy using OCCT's retained Direction, without mutation.
        auto camera=v.view_->Camera();camera->SetEyeAndCenter(gp_Pnt(0,0,5),gp_Pnt());camera->SetUp(gp_Dir(0,1,0));
        Bnd_Box plane;plane.Add(gp_Pnt(-100,-100,0));plane.Add(gp_Pnt(100,100,0));v.view_->FitAll(plane,0.05,Standard_False);
        const auto eye=camera->Eye(),center=camera->Center();const auto direction=camera->Direction();const double scale=camera->Scale();
        NativeDocumentSnapshot recovered;check(v.exportDocumentSnapshot(recovered),"collapsed camera capture failed");
        check(camera->Eye().IsEqual(eye,0) && camera->Center().IsEqual(center,0) && camera->Scale()==scale,"Save capture mutated live camera");
        check(recovered.camera.eye.SquareDistance(recovered.camera.center)>1.e-16 && recovered.camera.scale==scale
            && gp_Dir(gp_Vec(recovered.camera.eye,recovered.camera.center)).Dot(direction)>1-1.e-12,"capture lost orthographic direction/zoom");
        QString error;check(NativeDocumentStorage::save(temp.filePath("recovered.odc"),recovered,error),qPrintable(error));
        auto invalid=recovered;invalid.camera.eye=invalid.camera.center;
        check(!NativeDocumentStorage::save(temp.filePath("invalid.odc"),invalid,error),"storage silently accepted irrecoverable camera metadata");
        window.documentDirty_=false;window.newDocument();
        check(v.exportDocumentSnapshot(recovered) && recovered.parts.empty() && recovered.constructions.empty()
            && recovered.sketchEntities.empty() && NativeDocumentStorage::save(temp.filePath("new-empty.odc"),recovered,error),"New empty document cannot save");
        window.documentDirty_=false;
    }
    static void run() {
        QTemporaryDir temp; check(temp.isValid(),"session temporary directory failed");
        MainWindow window; window.show(); QApplication::processEvents();
        check(window.saveAction_->isEnabled(),"empty Untitled document cannot be saved");
        auto& v=*window.viewport_;
        v.addBox(); v.addSphere();
        v.renamePart(0,"Saved box"); v.setPartColor(0,QColor(20,90,180)); v.setPartVisible(1,false);
        for(int i=0;i<3;++i) {
            v.beginCommandSession(CadViewport::CommandSession::Kind::Move,{0},gp_Dir(1,0,0));
            v.dragDimension_->setText("10"); v.confirmCommandSession();
        }
        v.undoPull(); v.selectParts({0});
        const auto undo=v.undoStack_.size(),redo=v.redoStack_.size();
        check(undo==2 && redo==1,"history fixture incomplete");
        const auto selected=v.selectedPartIndices_;
        const auto sourceShape=v.boxes_[0].shape;
        window.documentPath_=temp.filePath("document.odc");
        check(window.saveDocument(),"Save failed");
        check(!window.documentDirty_ && !window.windowTitle().endsWith("*"),"Save did not clear dirty indicator");
        const auto checkHistory=[&] {
            check(v.undoStack_.size()==undo && v.redoStack_.size()==redo,"Save cleared history");
            check(v.selectedPartIndices_==selected && v.context_->NbSelected()==1,"Save cleared selection");
            check(v.boxes_[0].shape.IsSame(sourceShape),"Save replaced geometry");
        };
        checkHistory();
        const double scale=v.view_->Camera()->Scale(); v.view_->SetScale(scale*1.2);
        check(!window.documentDirty_,"camera navigation dirtied document");
        check(window.saveDocument(),"camera-only Save failed");
        NativeDocumentSnapshot stored; QString error;
        check(NativeDocumentStorage::load(window.documentPath_,stored,error),"saved document cannot be read");
        check(std::abs(stored.camera.scale-v.view_->Camera()->Scale())<1.e-7,"Save did not capture current camera");
        chooseFile(temp.filePath("save-as")); check(window.saveDocumentAs(),"Save As failed");
        if (!window.documentPath_.endsWith("save-as.odc")) throw std::runtime_error("Save As path: "+window.documentPath_.toStdString());
        checkHistory();
        const QString goodPath=window.documentPath_;
        v.dragIndex_=0; v.dragAxis_=2; v.dragMax_=true;
        v.dragMin_=v.boxes_[0].min; v.dragMaxBounds_=v.boxes_[0].max;
        v.beginPullSession(); check(v.previewCommand(5),"pending Pull fixture failed");
        check(window.saveDocument() && v.commandSession_.has_value(),"Save cancelled pending Pull");
        check(NativeDocumentStorage::load(goodPath,stored,error),"pending Pull Save cannot be read");
        GProp_GProps mass; BRepGProp::VolumeProperties(stored.parts[0].shape,mass);
        check(std::abs(mass.Mass()-480000)<1.e-5,"Save serialized unconfirmed Pull geometry");
        v.cancelCommandSession(); checkHistory();
        // Failed Save leaves identity, dirty state, selection and history intact.
        window.documentPath_=temp.path(); window.documentDirty_=true;
        answer(QMessageBox::Ok); check(!window.saveDocument(),"Save to directory unexpectedly succeeded");
        check(window.documentDirty_,"failed Save cleared dirty state"); checkHistory();
        window.documentPath_=goodPath; window.documentDirty_=false;
        const QString corruptPath=temp.filePath("invalid.odc"); QFile corrupt(corruptPath);
        check(corrupt.open(QIODevice::WriteOnly),"corrupt fixture failed"); corrupt.write("broken"); corrupt.close();
        chooseFile(corruptPath);
        // Dismiss the error after the file chooser has completed.
        QTimer errorTimer; QObject::connect(&errorTimer,&QTimer::timeout,[] {
            for(auto* widget:QApplication::topLevelWidgets()) if(auto* box=qobject_cast<QMessageBox*>(widget))
                if(box->isVisible()) box->accept();
        }); errorTimer.start(50); window.openDocument(); errorTimer.stop();
        check(window.documentPath_==goodPath,"failed Open replaced filename"); checkHistory();
        auto invalid=stored; invalid.camera.projectionType=99;
        check(!v.importDocumentSnapshot(invalid,error),"invalid camera import succeeded"); checkHistory();
        window.documentDirty_=true; answer(QMessageBox::Cancel); window.newDocument(); checkHistory();
        check(window.documentDirty_,"cancelled New cleared dirty state");
        QCloseEvent close; answer(QMessageBox::Cancel); window.closeEvent(&close);
        check(!close.isAccepted(),"close ignored unsaved-changes cancellation");
        answer(QMessageBox::Save); check(window.maybeSave(),"unsaved Save prompt failed"); checkHistory();
        // Successful Open cancels a pending preview, resets history/tools and
        // reconstructs every browser row, including hidden Parts.
        v.beginCommandSession(CadViewport::CommandSession::Kind::Move,{0},gp_Dir(1,0,0)); v.previewCommand(7);
        window.documentDirty_=false; chooseFile(goodPath); window.openDocument();
        check(!v.commandSession_ && v.undoStack_.empty() && v.redoStack_.empty(),"Open retained session/history");
        check(v.selectedPartIndices_.empty() && v.context_->NbSelected()==0 && !v.arrowVisible_,"Open retained selection/tools");
        check(window.root_->childCount()==2 && window.root_->child(0)->text(0)=="Saved box","Open did not rebuild browser");
        check(!v.isPartVisible(1) && !v.context_->IsDisplayed(v.boxes_[1].presentation),"Open displayed hidden Part");
        check(!window.documentDirty_,"Open dirtied document");
        v.addBox();
        for (int i=0;i<2;++i) {
            v.beginCommandSession(CadViewport::CommandSession::Kind::Move,{0},gp_Dir(1,0,0));
            v.dragDimension_->setText("4"); v.confirmCommandSession();
        }
        v.undoPull();
        check(!v.undoStack_.empty() && !v.redoStack_.empty(),"New reset fixture needs both history stacks");
        v.beginCommandSession(CadViewport::CommandSession::Kind::Move,{0},gp_Dir(1,0,0)); v.previewCommand(7);
        answer(QMessageBox::Discard); window.newDocument();
        check(v.boxes_.empty() && v.undoStack_.empty() && v.redoStack_.empty() && !v.commandSession_,"New retained model/history/session");
        check(window.root_->childCount()==0 && window.documentPath_.isEmpty() && !window.documentDirty_,"New retained browser/path/dirty state");
        check(window.windowTitle().contains("Untitled"),"New title incorrect");
    }
};
void runNativeDocumentSessions() { NativeDocumentSessionTest::run(); }
void runPartlessDocuments() { NativeDocumentSessionTest::partless(); }
