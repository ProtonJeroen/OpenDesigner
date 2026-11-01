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
#include <QToolBar>
#include <QMenu>
#include <QLabel>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QScrollArea>
#include <QScrollBar>
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
 static void operation(CadViewport& v,ModelingOperation mode) {
    check(v.commandSession_ && v.extrusionControls_,"operation switch requires an open Extrude session");
    auto* combo=v.extrusionControls_->findChild<QComboBox*>("extrusionOperation");
    combo->setCurrentIndex(combo->findData(int(mode)));
 }

 static void activeTargetPrecedence() {
    MainWindow window;window.resize(1400,1000);window.show();QApplication::processEvents();
    auto& v=*window.viewport_;v.initializeViewer();
    NativeDocumentSnapshot document;
    NativePartState first;first.id="active-cut-part-1";first.name="Part 1";first.shape=BRepPrimAPI_MakeBox(100,100,20).Shape();
    NativePartState second;second.id="selected-cut-part-2";second.name="Part 2";second.shape=BRepPrimAPI_MakeBox(gp_Pnt(150,0,0),100,100,40).Shape();
    document.parts={first,second};QString error;check(v.importDocumentSnapshot(document,error),"two-Part fixture import failed");
    Construction::Object wp;wp.id="two-part-cut-wp";wp.name="Cut Workplane";wp.kind=Construction::Kind::Workplane;
    v.restoreConstructions({wp});std::vector<Sketch::Entity> profiles;
    for(int region=0;region<2;++region) {
        const double x=10+150*region;const Sketch::Point corners[]{{x,10},{x+10,10},{x+10,20},{x,20}};
        for(int edge=0;edge<4;++edge) {
            Sketch::Entity entity;entity.id=QString("target-profile-%1-%2").arg(region).arg(edge);entity.workplaneId=wp.id;
            entity.geometry=Sketch::Segment{corners[edge],corners[(edge+1)%4]};profiles.push_back(entity);
        }
    }
    v.restoreSketchEntities(profiles);v.activateSketchWorkplane(wp.id,false);v.setActivePart(first.id);
    // Retain a sketch source while seeding the ordinary OCCT whole-Part selection.
    // Going through the Part selection-mode action synchronizes its browser state.
    v.context_->ClearSelected(false);v.context_->AddOrRemoveSelected(v.boxes_[1].presentation,false);
    v.setSelectionMode(CadViewport::SelectionMode::Part);
    check(v.selectedPartIndices_==std::vector<size_t>{1} && v.context_->IsSelected(v.boxes_[1].presentation),"Part 2 ordinary selection fixture failed");
    int pickRequests=0;QObject::connect(&v,&CadViewport::statusMessage,&window,[&](const QString& message){if(message=="Select target part")++pickRequests;});
    auto* cut=window.findChild<QAction*>("sketch.extrude");check(cut && cut->isEnabled(),"Extrude action unavailable with sketch source");cut->trigger();operation(v,ModelingOperation::RemoveMaterial);
    CommandPanelTest::waitForInitialAnchor(*v.commandPanel_,v,"two-Part Cut panel anchor");
    auto* target=v.extrusionControls_->findChild<QComboBox*>("extrusionTarget");
    check(target->isEnabled() && target->currentText()==first.name && target->currentData(Qt::UserRole+1).toString()==first.id,"panel did not resolve Active Part ahead of ordinary selection");
    check(v.commandSession_->extrusion->target==std::optional<size_t>(0) && v.commandSession_->valid
        && std::abs(volume(v.commandSession_->candidate)-198000)<1.e-6,"preview did not cut Active Part 1");
    check(pickRequests==0 && !v.targetPicking(),"eligible Active Part initialization entered viewport picking");
    check(v.boxes_[0].shape.IsSame(first.shape) && v.boxes_[1].shape.IsSame(second.shape),"preview modified canonical geometry");
    // Explicit viewport override uses Part 2, without modifying Active Part.
    v.extrusionControls_->findChild<QPushButton*>("pickTargetPart")->click();
    check(v.targetPicking(),"explicit target override did not start picking");
    v.view_->SetProj(0,0,1);v.fitAll();Standard_Integer x,y;v.view_->Convert(200,50,40,x,y);
    QMouseEvent click(QEvent::MouseButtonPress,QPointF(x,y),QPointF(x,y),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);v.mousePressEvent(&click);
    QMouseEvent release(QEvent::MouseButtonRelease,QPointF(x,y),QPointF(x,y),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);v.mouseReleaseEvent(&release);
    check(v.commandSession_->extrusion->target==std::optional<size_t>(1) && target->currentData(Qt::UserRole+1).toString()==second.id
        && v.activePartId()==first.id && !v.targetPicking() && std::abs(volume(v.commandSession_->candidate)-398000)<1.e-6,"viewport override did not preserve Active Part/use Part 2");
    operation(v,ModelingOperation::AddMaterial);
    check(v.commandSession_->extrusion->target==std::optional<size_t>(0) && !v.targetPicking()
        && v.activePartId()==first.id && v.context_->IsDisplayed(v.boxes_[1].presentation)
        && v.commandSession_->preview.IsNull(),"Remove to Add did not restore override/reset to Active Part/clear failed preview");
    target->setCurrentIndex(target->findData(QVariant::fromValue<qulonglong>(1)));
    check(v.commandSession_->extrusion->target==std::optional<size_t>(1) && v.activePartId()==first.id
        && !v.targetPicking(),"Add explicit dropdown override changed activation or ignored target");
    operation(v,ModelingOperation::RemoveMaterial);
    check(v.commandSession_->valid && std::abs(volume(v.commandSession_->candidate)-198000)<1.e-6,"Add to Remove retained stale target/result");
    v.cancelCommandSession();check(v.boxes_[0].shape.IsSame(first.shape) && v.boxes_[1].shape.IsSame(second.shape),"override Cancel changed geometry");
    // Cancel restores ordinary Part 2 selection; restart must again prefer Part 1.
    check(v.selectedPartIndices_==std::vector<size_t>{1},"Cancel lost original Part 2 selection");
    cut->trigger();operation(v,ModelingOperation::RemoveMaterial);target=v.extrusionControls_->findChild<QComboBox*>("extrusionTarget");
    check(v.commandSession_->extrusion->target==std::optional<size_t>(0) && target->currentText()==first.name,"restart lost Active Part precedence");
    const auto previewVolume=volume(v.commandSession_->candidate);const auto history=v.undoStack_.size();
    v.commandButtons_->button(QDialogButtonBox::Ok)->click();
    check(!v.commandSession_ && v.undoStack_.size()==history+1 && v.undoStack_.back().partIndex==0
        && std::abs(volume(v.boxes_[0].shape)-previewVolume)<1.e-6 && std::abs(previewVolume-198000)<1.e-6
        && v.boxes_[1].shape.IsSame(second.shape) && v.activePartId()==first.id,"commit did not cut only the previewed Active Part");
    // A hidden Active Part cannot be silently used by either material mode.
    v.setPartVisible(0,false);v.activateSketchWorkplane(wp.id,false);
    cut->trigger();operation(v,ModelingOperation::AddMaterial);
    check(v.targetPicking() && !v.commandSession_->valid && !v.commandSession_->extrusion->target
        && v.extrusionControls_->findChild<QLabel*>("extrusionStatus")->text().contains("hidden"),"Add did not reject hidden Active Part and request picking");
    operation(v,ModelingOperation::NewSolid);
    check(!v.targetPicking() && v.commandSession_->valid,"New Solid did not stop pending target picking");
    operation(v,ModelingOperation::RemoveMaterial);
    check(v.targetPicking() && !v.commandSession_->valid && !v.commandSession_->extrusion->target
        && v.activePartId()==first.id,"Remove did not reject hidden Active Part/preserve activation");
    v.cancelCommandSession();
    check(!v.isPartVisible(0) && v.boxes_[1].shape.IsSame(second.shape),"Cancel restored a hidden target or modified another Part");
    window.documentDirty_=false;window.close();
 }
 static void run(){
    MainWindow w;w.resize(1400,1000);w.show();QApplication::processEvents();auto& v=*w.viewport_;v.initializeViewer();
    Construction::Object wp;wp.id="extrude-wp";wp.name="WP1";wp.kind=Construction::Kind::Workplane;
    v.restoreConstructions({wp});
    const Sketch::Point p[]{{0,0},{100,0},{100,50},{0,50}};std::vector<Sketch::Entity> sketch;
    for(size_t i=0;i<4;++i){Sketch::Entity e;e.id="profile-"+QString::number(i);e.workplaneId=wp.id;e.geometry=Sketch::Segment{p[i],p[(i+1)%4]};sketch.push_back(e);}
    v.restoreSketchEntities(sketch);v.activateSketchWorkplane(wp.id);v.selectSketchEntities({sketch.front().id});w.documentDirty_=false;
    auto* action=w.findChild<QAction*>("sketch.extrude");check(action && action->isEnabled(),"Extrude menu/toolbar action unavailable");action->trigger();QApplication::processEvents();
    check(action->text()=="Extrude" && !w.findChild<QAction*>("sketch.cutExtrude"),"separate extrusion UI actions remain");
    check(w.findChild<QMenu*>("modeling3DMenu")->actions().count(action)==1
        && w.findChild<QToolBar*>("modeling3DToolbar")->actions().count(action)==1,"Extrude action not shared by menu and toolbar");
    auto* operations=v.extrusionControls_->findChild<QComboBox*>("extrusionOperation");
    check(operations->count()==3 && operations->isEnabled() && v.commandPanel_->windowTitle()=="Extrude","unified operation selector missing");
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
    check(!v.commandSession_->valid && v.targetPicking(),"Add without Active Part did not request a target");v.selectParts({0});check(v.commandSession_->valid,"Browser target selection failed");
    check(std::abs(volume(v.commandSession_->candidate)-58000)<1.e-6 && std::abs(volume(v.boxes_[0].shape)-50000)<1.e-6,"preview is tool/modified original");
    check(!w.documentDirty_ && v.undoStack_.size()==history,"Add preview dirtied/history changed");
    v.cancelCommandSession();check(v.boxes_[0].shape.IsSame(before.shape) && v.undoStack_.size()==history,"Add Cancel changed target");
    v.setActivePart(id);w.documentDirty_=false;v.startProfileExtrusion();operation(v,ModelingOperation::AddMaterial);
    check(!v.targetPicking() && v.commandSession_->extrusion->target==std::optional<size_t>(0)
        && v.commandSession_->valid,"Add did not initialize from eligible Active Part");
    auto* switchDepth=v.extrusionControls_->findChild<QLineEdit*>("extrusionDistance");
    operation(v,ModelingOperation::RemoveMaterial);
    auto* cutMode=v.extrusionControls_->findChild<QComboBox*>("cutMode");
    auto* reverse=v.extrusionControls_->findChild<QCheckBox*>("cutReverse");
    // HiDPI can constrain the viewport below the form's natural height. Check
    // full control reachability through the existing scroll wrapper as well as
    // native placement, after asynchronous nested layout changes settle.
    auto* scroll=v.commandPanel_->findChild<QScrollArea*>("commandPanelScrollArea");
    QElapsedTimer layoutTimer;layoutTimer.start();
    for(;;) {
        QApplication::processEvents();
        const auto reachable=[&](QWidget* widget) {
            scroll->ensureWidgetVisible(widget,0,0);
            return QRect(scroll->viewport()->mapToGlobal(QPoint()),scroll->viewport()->size())
                .contains(QRect(widget->mapToGlobal(QPoint()),widget->size()));
        };
        if(!cutMode->isHidden() && !reverse->isHidden() && scroll->horizontalScrollBar()->maximum()==0
            && reachable(reverse) && reachable(v.commandButtons_))break;
        check(layoutTimer.elapsed()<2000,"Remove controls/buttons inaccessible after mode switch");
        QEventLoop loop;QTimer::singleShot(10,&loop,&QEventLoop::quit);loop.exec();
    }
    scroll->verticalScrollBar()->setValue(0);
    CommandPanelTest::waitForInitialAnchor(*v.commandPanel_,v,"mode switch lost panel anchor");
    reverse->setChecked(true);cutMode->setCurrentIndex(1);switchDepth->setText("bad");
    check(v.commandSession_->valid && !switchDepth->isEnabled()
        && std::abs(volume(v.commandSession_->candidate)-46000)<1.e-6
        && !v.context_->IsDisplayed(v.boxes_[0].presentation),"Remove Through All switch preview incorrect");
    operation(v,ModelingOperation::NewSolid);
    check(switchDepth->isEnabled() && cutMode->isHidden() && reverse->isHidden()
        && !v.targetPicking() && !v.commandSession_->extrusion->target && v.commandSession_->preview.IsNull()
        && !v.commandSession_->valid && v.context_->IsDisplayed(v.boxes_[0].presentation),"New Solid switch retained Remove state/hidden target/stale preview");
    switchDepth->setText("-5");check(v.commandSession_->valid && std::abs(volume(v.commandSession_->candidate)-2000)<1.e-6,"New Solid lost signed direction after Remove");
    operation(v,ModelingOperation::AddMaterial);
    check(!v.commandSession_->valid && v.commandSession_->preview.IsNull(),"unchanged Add retained New Solid preview");
    check(!v.commandButtons_->button(QDialogButtonBox::Ok)->isEnabled(),"failed mode switch left OK enabled");
    switchDepth->setText("20");
    check(v.commandSession_->valid && std::abs(volume(v.commandSession_->candidate)-58000)<1.e-6
        && !w.documentDirty_ && v.undoStack_.size()==history && v.boxes_[0].shape.IsSame(before.shape),"mode switches mutated geometry/history or lost Add preview");
    v.confirmCommandSession();
    check(v.boxes_.size()==1 && v.undoStack_.size()==history+1 && v.boxes_[0].persistentId==id && v.boxes_[0].name==before.name
        && v.boxes_[0].color==before.color && v.boxes_[0].orientation.TranslationPart().IsEqual(before.orientation.TranslationPart(),1.e-12),"Add identity/properties/placement changed");
    check(std::abs(volume(v.boxes_[0].shape)-58000)<1.e-6 && BRepCheck_Analyzer(v.boxes_[0].shape).IsValid(),"Add local B-Rep invalid");
    v.undoPull();check(v.boxes_[0].shape.IsSame(before.shape) && v.selectedPartIndices_.empty(),"Add Undo did not restore exact target");v.redoPull();check(std::abs(volume(v.boxes_[0].shape)-58000)<1.e-6,"Add Redo wrong");
    // In-place Cut lifecycle, preview cancellation, history and persistence.
    v.setActivePart({});
    v.activateSketchWorkplane(wp.id);w.documentDirty_=false;const auto cutBefore=v.boxes_[0];const auto cutHistory=v.undoStack_.size();
    action->trigger();operation(v,ModelingOperation::RemoveMaterial);
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
    v.setActivePart(id);v.startProfileExtrusion();operation(v,ModelingOperation::RemoveMaterial);
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
    activeTargetPrecedence();
 }
};
void runProfileExtrusionSessions(){ProfileExtrusionSessionTest::run();}
