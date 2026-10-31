#include "CadViewport.h"
#include "MainWindow.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QCheckBox>
#include <QListWidget>
#include "CommandPanelTestHelpers.h"
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QMouseEvent>
#include <QTreeWidget>
#include <QTemporaryDir>
#include <QKeyEvent>
#include <BRepPrimAPI_MakeBox.hxx>
#include <TColStd_ListOfInteger.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <stdexcept>
namespace {
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
double volume(const TopoDS_Shape& s){GProp_GProps p;BRepGProp::VolumeProperties(s,p);return p.Mass();}
void key(QWidget* w,int k){QKeyEvent e(QEvent::KeyPress,k,Qt::NoModifier);QApplication::sendEvent(w,&e);QApplication::processEvents();}
}
struct ProfileExtrusionSessionTest {
 static void run(){
    MainWindow w;w.resize(1400,1000);w.show();QApplication::processEvents();auto& v=*w.viewport_;v.initializeViewer();
    Construction::Object wp;wp.id="extrude-wp";wp.name="WP1";wp.kind=Construction::Kind::Workplane;
    v.restoreConstructions({wp});
    const Sketch::Point p[]{{0,0},{100,0},{100,50},{0,50}};std::vector<Sketch::Entity> sketch;
    for(size_t i=0;i<4;++i){Sketch::Entity e;e.id="profile-"+QString::number(i);e.workplaneId=wp.id;e.geometry=Sketch::Segment{p[i],p[(i+1)%4]};sketch.push_back(e);}
    v.restoreSketchEntities(sketch);v.activateSketchWorkplane(wp.id);v.selectSketchEntities({sketch.front().id});w.documentDirty_=false;
    auto* action=w.findChild<QAction*>("sketch.extrude");check(action && action->isEnabled(),"Extrude menu/toolbar action unavailable");action->trigger();QApplication::processEvents();
    check(v.commandSession_ && v.commandSession_->valid && !v.commandSession_->preview.IsNull(),"New Solid preview missing");
    check(v.boxes_.empty() && v.undoStack_.empty() && !w.documentDirty_,"preview changed document/history");
    TColStd_ListOfInteger modes;v.context_->ActivatedModes(v.commandSession_->preview,modes);check(modes.IsEmpty(),"preview is selectable");
    NativeDocumentSnapshot previewSnapshot;check(v.exportDocumentSnapshot(previewSnapshot) && previewSnapshot.parts.empty() && previewSnapshot.sketchEntities==sketch,"snapshot included unconfirmed solid");
    check(v.commandPanel_->isVisible() && v.commandButtons_->button(QDialogButtonBox::Ok)->isVisible(),"command panel buttons missing");
    auto* depth=v.extrusionControls_->findChild<QLineEdit*>("extrusionDistance");depth->setText("30");check(std::abs(volume(v.commandSession_->candidate)-150000)<1.e-6,"depth not refreshed from source");
    depth->setText("bad");check(!v.commandSession_->valid && v.commandSession_->preview.IsNull() && !v.commandButtons_->button(QDialogButtonBox::Ok)->isEnabled(),"invalid input retained stale preview");
    depth->setText("20");key(depth,Qt::Key_Escape);check(!v.commandSession_ && v.boxes_.empty() && v.undoStack_.empty() && !w.documentDirty_,"Cancel changed New Solid document");
    check(v.selectedSketchIds_==QStringList{sketch.front().id} && v.context_->IsSelected(v.sketchPresentations_.at(sketch.front().id)),"Cancel did not restore Sketch selection");
    v.startProfileExtrusion();key(v.extrusionControls_->findChild<QLineEdit*>("extrusionDistance"),Qt::Key_Return);
    check(v.boxes_.size()==1 && v.isPartActive(0) && v.undoStack_.size()==1 && w.documentDirty_,"New Solid not one commit");
    check(v.sketchEntities_==sketch && v.constructions_.size()==1 && std::abs(volume(v.boxes_[0].shape)-100000)<1.e-6,"sources consumed/volume wrong");
    const auto id=v.boxes_[0].persistentId;check(!id.isEmpty() && w.root_->childCount()==1,"Part identity/browser missing");
    v.setActivePart(id);v.undoPull();check(!v.isPartActive(0) && v.activePartId().isEmpty(),"Undo did not remove new Part/active reference");const auto redo=v.redoStack_.size();
    v.activateSketchWorkplane(wp.id);v.startProfileExtrusion();v.cancelCommandSession();check(v.redoStack_.size()==redo,"Cancel cleared Redo");
    v.redoPull();check(v.isPartActive(0) && v.boxes_[0].persistentId==id && v.selectedPartIndices_.empty() && v.activePartId().isEmpty(),"Redo changed identity/selection");
    // Add to a transformed existing Part; source sketch sits at its world top.
    v.boxes_[0].shape=BRepPrimAPI_MakeBox(100,50,10).Shape();v.boxes_[0].orientation.SetTranslation(gp_Vec(30,40,0));
    v.boxes_[0].name="Stable target";v.boxes_[0].color=QColor(120,150,190);v.applyFaceState(0,v.boxes_[0]);
    wp.frame=gp_Ax3(gp_Pnt(30,40,10),gp_Dir(0,0,1));v.restoreConstructions({wp});
    std::vector<Sketch::Entity> boss=sketch;const Sketch::Point q[]{{10,10},{30,10},{30,30},{10,30}};
    for(size_t i=0;i<4;++i)boss[i].geometry=Sketch::Segment{q[i],q[(i+1)%4]};v.restoreSketchEntities(boss);
    v.activateSketchWorkplane(wp.id);const auto before=v.boxes_[0];const auto history=v.undoStack_.size();w.documentDirty_=false;
    v.startProfileExtrusion();auto* mode=v.extrusionControls_->findChild<QComboBox*>("extrusionOperation");mode->setCurrentIndex(1);
    check(!v.commandSession_->valid,"Add accepted no target");v.selectParts({0});check(v.commandSession_->valid,"Browser target selection failed");
    check(std::abs(volume(v.commandSession_->candidate)-58000)<1.e-6 && std::abs(volume(v.boxes_[0].shape)-50000)<1.e-6,"preview is tool/modified original");
    check(!w.documentDirty_ && v.undoStack_.size()==history,"Add preview dirtied/history changed");
    v.cancelCommandSession();check(v.boxes_[0].shape.IsSame(before.shape) && v.undoStack_.size()==history,"Add Cancel changed target");
    v.startProfileExtrusion();v.extrusionControls_->findChild<QComboBox*>("extrusionOperation")->setCurrentIndex(1);v.chooseExtrusionTarget({0});v.confirmCommandSession();
    check(v.boxes_.size()==1 && v.undoStack_.size()==history+1 && v.boxes_[0].persistentId==id && v.boxes_[0].name==before.name
        && v.boxes_[0].color==before.color && v.boxes_[0].orientation.TranslationPart().IsEqual(before.orientation.TranslationPart(),1.e-12),"Add identity/properties/placement changed");
    check(std::abs(volume(v.boxes_[0].shape)-58000)<1.e-6 && BRepCheck_Analyzer(v.boxes_[0].shape).IsValid(),"Add local B-Rep invalid");
    v.undoPull();check(v.boxes_[0].shape.IsSame(before.shape) && v.selectedPartIndices_.empty(),"Add Undo did not restore exact target");v.redoPull();check(std::abs(volume(v.boxes_[0].shape)-58000)<1.e-6,"Add Redo wrong");
    // In-place Cut lifecycle, preview cancellation, history and persistence.
    v.activateSketchWorkplane(wp.id);w.documentDirty_=false;const auto cutBefore=v.boxes_[0];const auto cutHistory=v.undoStack_.size();
    auto* cutAction=w.findChild<QAction*>("sketch.cutExtrude");check(cutAction && cutAction->isEnabled(),"Cut action missing");cutAction->trigger();
    CommandPanelTest::waitForInitialAnchor(*v.commandPanel_,v,"Cut panel anchor changed");
    check(v.targetPicking(),"Cut without Active Part did not request viewport target");
    key(v.extrusionControls_->findChild<QLineEdit*>("extrusionDistance"),Qt::Key_Escape);
    check(v.commandSession_ && !v.targetPicking(),"Escape cancelled entire Cut command");
    v.extrusionControls_->findChild<QPushButton*>("pickTargetPart")->click();
    check(v.targetPicking(),"Pick button did not resume target picking");
    v.view_->SetProj(0,0,1);v.fitAll();Standard_Integer pickX,pickY;
    v.view_->Convert(35,45,10,pickX,pickY);
    QMouseEvent targetClick(QEvent::MouseButtonPress,QPointF(pickX,pickY),QPointF(pickX,pickY),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    v.mousePressEvent(&targetClick);
    QMouseEvent targetRelease(QEvent::MouseButtonRelease,QPointF(pickX,pickY),QPointF(pickX,pickY),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    v.mouseReleaseEvent(&targetRelease);
    check(v.dragDimension_->isHidden(),"target click opened transform controls");
    check(v.commandSession_->extrusion->target==std::optional<size_t>(0) && !v.targetPicking() && v.activePartId().isEmpty(),"Cut viewport target pick failed/activated Part");
    auto* cutDepth=v.extrusionControls_->findChild<QLineEdit*>("extrusionDistance");cutDepth->setText("5");
    check(v.commandSession_->valid && std::abs(volume(v.commandSession_->candidate)-56000)<1.e-6,"Cut blind preview wrong");
    check(v.boxes_[0].shape.IsSame(cutBefore.shape) && v.undoStack_.size()==cutHistory && !w.documentDirty_,"Cut preview mutated target");
    check(!v.context_->IsDisplayed(v.boxes_[0].presentation),"Cut preview did not hide original display");
    cutDepth->setText("bad");v.confirmCommandSession();check(v.commandSession_ && !v.commandSession_->valid && v.boxes_[0].shape.IsSame(cutBefore.shape)
        && v.undoStack_.size()==cutHistory && !w.documentDirty_ && v.context_->IsDisplayed(v.boxes_[0].presentation),"Failed Cut mutated document/display");cutDepth->setText("5");
    auto* profiles=v.extrusionControls_->findChild<QListWidget*>("cutProfiles");profiles->clearSelection();check(!v.commandSession_->valid,"Empty profiles accepted");profiles->selectAll();
    v.cancelCommandSession();check(v.boxes_[0].shape.IsSame(cutBefore.shape) && !v.commandSession_ && v.context_->IsDisplayed(v.boxes_[0].presentation),"Cut cancel mutated target");
    v.setActivePart(id);v.startCutExtrusion();
    check(!v.targetPicking() && v.commandSession_->extrusion->target==std::optional<size_t>(0),"Cut did not use Active Part");
    v.extrusionControls_->findChild<QCheckBox*>("cutReverse")->setChecked(true);
    v.extrusionControls_->findChild<QComboBox*>("cutMode")->setCurrentIndex(1);
    check(v.commandSession_->valid && std::abs(volume(v.commandSession_->candidate)-54000)<1.e-6,"Cut through preview wrong");
    v.confirmCommandSession();check(v.boxes_.size()==1 && v.undoStack_.size()==cutHistory+1 && v.boxes_[0].persistentId==id
        && v.boxes_[0].name==cutBefore.name && v.boxes_[0].color==cutBefore.color && w.root_->childCount()==1,"Cut lost identity/material/browser");
    check(v.activePartId()==id,"Cut replaced Active Part identity");
    v.undoPull();check(v.activePartId()==id && v.boxes_[0].shape.IsSame(cutBefore.shape),"Cut undo wrong");v.redoPull();check(std::abs(volume(v.boxes_[0].shape)-54000)<1.e-6,"Cut redo wrong");
    QTemporaryDir directory;NativeDocumentSnapshot snap,loaded;QString error;check(v.exportDocumentSnapshot(snap),"snapshot failed");
    const auto path=directory.filePath("extrusion.odc");check(NativeDocumentStorage::save(path,snap,error) && NativeDocumentStorage::load(path,loaded,error),"native roundtrip failed");
    check(loaded.activePartId==id,"Cut roundtrip lost active reference");
    check(loaded.parts.size()==1 && loaded.parts[0].color==cutBefore.color && loaded.parts[0].name==cutBefore.name && std::abs(volume(loaded.parts[0].shape)-54000)<1.e-6,"Cut save lost geometry");
    check(loaded.parts.size()==1 && loaded.parts[0].id==id && loaded.sketchEntities==boss && loaded.constructions.size()==1,"native storage lost source/Part");
    v.activateSketchWorkplane(wp.id);v.startProfileExtrusion();check(v.importDocumentSnapshot(loaded,error) && !v.commandSession_ && v.undoStack_.empty(),"Open did not reset extrusion/history");
    v.activateSketchWorkplane(wp.id);v.startProfileExtrusion();v.startSketchDrawing(Sketch::DrawTool::Segment,Sketch::Role::Construction);
    check(v.commandSession_ && !v.commandSession_->extrusion,"command switch did not cancel extrusion");v.cancelCommandSession();
    w.documentDirty_=false;w.close();
 }
};
void runProfileExtrusionSessions(){ProfileExtrusionSessionTest::run();}
