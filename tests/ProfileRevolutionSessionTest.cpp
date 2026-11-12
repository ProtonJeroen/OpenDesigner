#include "CadViewport.h"
#include "MainWindow.h"
#include "CommandPanelTestHelpers.h"
#include <QApplication>
#include <QAction>
#include <QMenu>
#include <QToolBar>
#include <QTreeWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <QScrollArea>
#include <QScrollBar>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QLocale>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TColStd_ListOfInteger.hxx>
#include <numbers>
#include <stdexcept>
#include <source_location>
#include <sstream>
#include <iomanip>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
double volume(const TopoDS_Shape& shape){GProp_GProps p;BRepGProp::VolumeProperties(shape,p);return p.Mass();}
gp_Pnt centre(const TopoDS_Shape& shape){GProp_GProps p;BRepGProp::VolumeProperties(shape,p);return p.CentreOfMass();}
void key(QWidget* widget,int code){QKeyEvent event(QEvent::KeyPress,code,Qt::NoModifier);QApplication::sendEvent(widget,&event);QApplication::processEvents();}
Sketch::Entity entity(QString id,Sketch::Geometry geometry,Sketch::Role role=Sketch::Role::Construction){Sketch::Entity e;e.id=id;e.workplaneId="revolve-wp";e.name=id;e.geometry=geometry;e.role=role;return e;}
}
struct ProfileRevolutionSessionTest {
 static void click(CadViewport& v,const gp_Pnt& p) {
    Standard_Integer x,y;v.view_->Convert(p.X(),p.Y(),p.Z(),x,y);
    QMouseEvent press(QEvent::MouseButtonPress,QPointF(x,y),QPointF(x,y),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);v.mousePressEvent(&press);
    QMouseEvent release(QEvent::MouseButtonRelease,QPointF(x,y),QPointF(x,y),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);v.mouseReleaseEvent(&release);
 }
 static void preview(CadViewport& v,double expected,const std::source_location location=std::source_location::current()) {
    const bool candidate=v.commandSession_ && !v.commandSession_->candidate.IsNull();
    const double actual=candidate?volume(v.commandSession_->candidate):0;
    if(!v.commandSession_ || !v.commandSession_->valid || v.commandSession_->preview.IsNull()
       || !candidate || !(std::abs(actual-expected)<expected*1.e-9)) {
        std::ostringstream message;message<<std::setprecision(17)<<"Revolve preview volume/state incorrect at "<<location.file_name()<<":"<<location.line()
            <<"; expected="<<expected<<" actual=";
        if(candidate)message<<actual;else message<<"<no candidate>";
        message<<" candidate="<<candidate<<" locale="<<QLocale().name().toStdString();
        if(v.commandSession_)message<<" valid="<<v.commandSession_->valid<<" previewNull="<<v.commandSession_->preview.IsNull()
            <<" mode="<<int(v.commandSession_->sweep->operation)<<" state="<<int(v.commandSession_->state);
        if(v.sweepControls_) {
            const auto text=v.sweepControls_->findChild<QLineEdit*>("revolutionAngle")->text();bool ok=false;const double angle=QLocale().toDouble(text,&ok);
            message<<" angleText="<<text.toStdString()<<" parsed="<<ok<<" angle="<<angle
                <<" reverse="<<v.sweepControls_->findChild<QCheckBox*>("revolutionReverse")->isChecked()
                <<" status="<<v.sweepControls_->findChild<QLabel*>("extrusionStatus")->text().toStdString();
        }
        throw std::runtime_error(message.str());
    }
    TColStd_ListOfInteger modes;v.context_->ActivatedModes(v.commandSession_->preview,modes);check(modes.IsEmpty(),"Revolve preview is selectable");
 }
 static void operation(CadViewport& v,ModelingOperation mode) {
    auto* combo=v.sweepControls_->findChild<QComboBox*>("extrusionOperation");combo->setCurrentIndex(combo->findData(int(mode)));
 }
 static void materialModes() {
    constexpr double pi=std::numbers::pi;
    MainWindow window;window.resize(1400,1000);window.show();QApplication::processEvents();auto& v=*window.viewport_;v.initializeViewer();
    gp_Trsf placement;placement.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),.4);placement.SetTranslationPart(gp_Vec(100,50,20));
    NativeDocumentSnapshot document;NativePartState first;first.id="revolve-target-1";first.name="Stable target";first.color=QColor(110,160,190);
    first.shape=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(),gp_Dir(0,1,0)),30,10).Shape();first.transform=placement;
    NativePartState second=first;second.id="revolve-target-2";second.name="Override target";
    second.shape=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,20,0),gp_Dir(0,1,0)),35,5).Shape();document.parts={first,second};
    QString error;check(v.importDocumentSnapshot(document,error),"Revolve Boolean fixture import failed");
    Construction::Object wp;wp.id="revolve-wp";wp.name="Moved Workplane";wp.kind=Construction::Kind::Workplane;wp.frame=gp_Ax3().Transformed(placement);
    v.restoreConstructions({wp});std::vector<Sketch::Entity> sketch;
    const Sketch::Point points[]{{20,0},{40,0},{40,30},{20,30}};
    for(int i=0;i<4;++i)sketch.push_back(entity(QString("material-profile-%1").arg(i),Sketch::Segment{points[i],points[(i+1)%4]},Sketch::Role::Profile));
    sketch.push_back(entity("material-axis",Sketch::InfiniteLine{{0,0},{0,1}}));v.restoreSketchEntities(sketch);
    (v.activateSketchWorkplane(wp.id)&&v.lookAtWorkplane(wp.id));v.setActivePart(first.id);v.selectSketchEntities({sketch.front().id});
    const auto original=v.boxes_[0],other=v.boxes_[1];const auto history=v.undoStack_.size();window.documentDirty_=false;
    const auto start=[&] {
        v.startProfileRevolution();auto* combo=v.sweepControls_->findChild<QComboBox*>("revolutionAxis");combo->setCurrentIndex(combo->findData("material-axis"));
    };
    start();operation(v,ModelingOperation::AddMaterial);preview(v,40000*pi);
    CommandPanelTest::waitForInitialAnchor(*v.commandPanel_,v,"Revolve material panel anchor");
    check(!v.targetPicking() && v.commandSession_->sweep->target==std::optional<size_t>(0) && v.activePartId()==first.id,"Add did not resolve eligible Active Part");
    check(v.boxes_[0].shape.IsSame(original.shape) && v.undoStack_.size()==history && !window.documentDirty_,"Add preview changed target/history");
    NativeDocumentSnapshot pending;check(v.exportDocumentSnapshot(pending) && pending.parts.size()==2
        && std::abs(volume(pending.parts[0].shape)-9000*pi)<1.e-5,"Boolean preview leaked into persistence");
    v.boxes_[0].orientation.SetTranslation(gp_Vec(999,0,0));v.refreshProfileSweep();
    check(!v.commandSession_->valid && v.commandSession_->preview.IsNull(),"stale target placement retained Boolean preview");
    v.boxes_[0].orientation=original.orientation;v.refreshProfileSweep();preview(v,40000*pi);
    auto* angle=v.sweepControls_->findChild<QLineEdit*>("revolutionAngle");angle->setText("90");preview(v,16750*pi);
    v.sweepControls_->findChild<QCheckBox*>("revolutionReverse")->setChecked(true);preview(v,16750*pi);
    angle->setText("bad");check(!v.commandSession_->valid && v.commandSession_->preview.IsNull(),"invalid Boolean angle retained preview");
    angle->setText("360");operation(v,ModelingOperation::RemoveMaterial);preview(v,4000*pi);
    check(!v.context_->IsDisplayed(v.boxes_[0].presentation),"Remove did not preview the cut target");
    v.sweepControls_->findChild<QPushButton*>("pickRevolutionAxis")->click();
    check(v.commandSession_->sweep->axisPicking && !v.targetPicking() && v.commandSession_->preview.IsNull()
        && v.context_->IsDisplayed(v.boxes_[0].presentation),"axis repick did not restore original target/clear Boolean preview");
    auto* axisCombo=v.sweepControls_->findChild<QComboBox*>("revolutionAxis");axisCombo->setCurrentIndex(axisCombo->findData("material-axis"));preview(v,4000*pi);
    operation(v,ModelingOperation::NewSolid);preview(v,36000*pi);
    check(!v.commandSession_->sweep->target && !v.targetPicking() && v.context_->IsDisplayed(v.boxes_[0].presentation),"New Solid retained material target/hidden original");
    operation(v,ModelingOperation::RemoveMaterial);preview(v,4000*pi);
    v.sweepControls_->findChild<QPushButton*>("pickTargetPart")->click();check(v.targetPicking() && !v.commandSession_->sweep->axisPicking,"target picker conflicted with axis picker");
    const auto normal=wp.frame.Direction();v.view_->SetProj(normal.X(),normal.Y(),normal.Z());v.fitAll();click(v,gp_Pnt(0,22,35).Transformed(placement));
    check(v.commandSession_->sweep->target==std::optional<size_t>(1) && !v.targetPicking() && v.activePartId()==first.id,"viewport target override failed or changed Active Part");preview(v,2000*pi);
    v.cancelCommandSession();check(v.boxes_[0].shape.IsSame(original.shape) && v.boxes_[1].shape.IsSame(other.shape)
        && v.context_->IsDisplayed(v.boxes_[0].presentation) && v.context_->IsDisplayed(v.boxes_[1].presentation)
        && !window.documentDirty_ && v.undoStack_.size()==history && v.context_->IsSelected(v.sketchPresentations_.at(sketch.front().id)),"Boolean Cancel changed geometry/history/selection/display");
    // Existing in-place transaction preserves identity, material, placement and slot.
    Handle(StdSelect_BRepOwner) oldTargetFace=new StdSelect_BRepOwner(TopExp_Explorer(original.shape,TopAbs_FACE).Current(),v.boxes_[0].presentation,5,Standard_True);
    Handle(StdSelect_BRepOwner) otherFace=new StdSelect_BRepOwner(TopExp_Explorer(other.shape,TopAbs_FACE).Current(),v.boxes_[1].presentation,5,Standard_True);
    v.context_->AddOrRemoveSelected(oldTargetFace,Standard_False);v.context_->AddOrRemoveSelected(otherFace,Standard_False);
    start();operation(v,ModelingOperation::AddMaterial);v.confirmCommandSession();
    check(!v.commandSession_ && v.boxes_.size()==2 && v.undoStack_.size()==history+1 && v.boxes_[0].persistentId==first.id
        && v.boxes_[0].name==first.name && v.boxes_[0].color==original.color && v.boxes_[0].visible==original.visible
        && window.root_->childCount()==2 && v.activePartId()==first.id && v.boxes_[1].shape.IsSame(other.shape),"Add lost target identity/material/browser/activation");
    for(int row=1;row<=3;++row)for(int col=1;col<=4;++col)check(v.boxes_[0].orientation.Value(row,col)==original.orientation.Value(row,col),"Add changed target placement");
    check(std::abs(volume(v.boxes_[0].shape)-40000*pi)<1.e-5 && v.context_->IsSelected(v.sketchPresentations_.at(sketch.front().id)),"Add committed a tool or lost source selection");
    check(!v.context_->IsSelected(oldTargetFace) && v.context_->IsSelected(otherFace),"confirmation reused stale target topology or lost other Part selection");
    const auto added=v.boxes_[0];v.undoPull();check(v.boxes_[0].shape.IsSame(original.shape) && v.activePartId()==first.id,"Add Undo failed");
    (v.activateSketchWorkplane(wp.id)&&v.lookAtWorkplane(wp.id));v.startProfileRevolution();v.cancelCommandSession();check(v.redoStack_.size()==1,"material Cancel cleared Redo");v.redoPull();
    check(v.boxes_[0].shape.IsSame(added.shape) && v.boxes_[0].persistentId==first.id,"Add Redo lost target state");
    (v.activateSketchWorkplane(wp.id)&&v.lookAtWorkplane(wp.id));start();operation(v,ModelingOperation::AddMaterial);
    check(!v.commandSession_->valid && v.commandSession_->preview.IsNull(),"unchanged Add accepted");
    operation(v,ModelingOperation::RemoveMaterial);v.chooseExtrusionTarget({1});preview(v,2000*pi);v.confirmCommandSession();
    check(v.boxes_.size()==2 && v.undoStack_.size()==history+2 && v.boxes_[1].persistentId==second.id && v.boxes_[1].name==second.name
        && v.boxes_[1].color==other.color && v.boxes_[0].shape.IsSame(added.shape) && v.activePartId()==first.id,"Remove lost target identity/overrode another Part");
    const auto removed=v.boxes_[1];v.undoPull();check(v.boxes_[1].shape.IsSame(other.shape),"Remove Undo failed");v.redoPull();check(v.boxes_[1].shape.IsSame(removed.shape),"Remove Redo failed");
    NativeDocumentSnapshot saved,loaded;QTemporaryDir directory;check(v.exportDocumentSnapshot(saved)
        && NativeDocumentStorage::save(directory.filePath("material.odc"),saved,error)
        && NativeDocumentStorage::load(directory.filePath("material.odc"),loaded,error),"Revolve Boolean native roundtrip failed");
    check(loaded.parts.size()==2 && loaded.parts[0].id==first.id && loaded.parts[1].id==second.id && loaded.activePartId==first.id
        && loaded.parts[0].color==first.color && loaded.sketchEntities==sketch
        && std::abs(volume(loaded.parts[0].shape)-40000*pi)<1.e-5 && std::abs(volume(loaded.parts[1].shape)-2000*pi)<1.e-5,"native Boolean result/properties not restored");
    for(int row=1;row<=3;++row)for(int col=1;col<=4;++col)check(std::abs(loaded.parts[1].transform.Value(row,col)-other.orientation.Value(row,col))<1.e-12,"Remove/native roundtrip changed placement");
    // Absent or hidden Active Part requests viewport picking, never ordinary selection.
    v.setActivePart({});(v.activateSketchWorkplane(wp.id)&&v.lookAtWorkplane(wp.id));window.documentDirty_=false;v.startProfileRevolution();operation(v,ModelingOperation::RemoveMaterial);
    check(v.targetPicking() && !v.commandSession_->sweep->axisPicking && !v.commandSession_->valid,"missing Active Part did not request target picking");
    key(v.sweepControls_->findChild<QLineEdit*>("revolutionAngle"),Qt::Key_Escape);
    check(v.commandSession_ && !v.targetPicking() && v.boxes_[0].shape.IsSame(added.shape),"Escape during target picking modified/cancelled the operation");
    v.sweepControls_->findChild<QPushButton*>("pickTargetPart")->click();click(v,gp_Pnt(0,5,30).Transformed(placement));
    check(v.commandSession_->sweep->axisPicking && !v.targetPicking(),"target-first workflow did not resume axis picking");
    v.selectSketchEntities({"material-axis"});preview(v,4000*pi);
    key(v.sweepControls_->findChild<QLineEdit*>("revolutionAngle"),Qt::Key_Escape);check(!v.commandSession_ && v.boxes_[0].shape.IsSame(added.shape) && !window.documentDirty_,"Escape changed material geometry");
    v.setActivePart(first.id);v.setPartVisible(0,false);(v.activateSketchWorkplane(wp.id)&&v.lookAtWorkplane(wp.id));start();operation(v,ModelingOperation::AddMaterial);
    check(v.targetPicking() && !v.commandSession_->valid && v.sweepControls_->findChild<QLabel*>("extrusionStatus")->text().contains("hidden"),"hidden Active Part was accepted");
    operation(v,ModelingOperation::NewSolid);preview(v,36000*pi);check(!v.targetPicking() && !v.isPartVisible(0),"mode switch redisplayed hidden target");v.cancelCommandSession();
    window.documentDirty_=false;window.close();
 }
 static void run() {
    constexpr double pi=std::numbers::pi;
    MainWindow window;window.resize(1400,1000);window.show();QApplication::processEvents();auto& v=*window.viewport_;v.initializeViewer();
    auto* action=window.findChild<QAction*>("sketch.revolve");check(action && !action->isEnabled(),"Revolve must require an active Sketch Workplane");
    NativeDocumentSnapshot document;NativePartState part;part.id="axis-part";part.name="Original Part";part.color=QColor(120,150,180);
    part.shape=BRepPrimAPI_MakeBox(gp_Pnt(0,0,-10),10,30,10).Shape();part.transform.SetTranslation(gp_Vec(-10,0,0));document.parts={part};
    QString error;check(v.importDocumentSnapshot(document,error),"Revolve Part fixture import failed");
    Construction::Object wp;wp.id="revolve-wp";wp.name="Revolve Workplane";wp.kind=Construction::Kind::Workplane;
    Construction::Object axis;axis.id="construction-axis";axis.name="Y axis";axis.kind=Construction::Kind::Axis;axis.frame=gp_Ax3(gp_Pnt(),gp_Dir(0,1,0));
    v.restoreConstructions({wp,axis});std::vector<Sketch::Entity> sketch;
    for(int region=0;region<2;++region) {
        const double x=region?50:20,y=region?40:0,w=region?10:20,h=region?10:30;
        const Sketch::Point points[]{{x,y},{x+w,y},{x+w,y+h},{x,y+h}};
        for(int i=0;i<4;++i)sketch.push_back(entity(QString("profile-%1-%2").arg(region).arg(i),Sketch::Segment{points[i],points[(i+1)%4]},Sketch::Role::Profile));
    }
    sketch.push_back(entity("sketch-axis",Sketch::InfiniteLine{{0,0},{0,1}}));
    sketch.push_back(entity("crossing-axis",Sketch::Segment{{30,-10},{30,70}}));
    sketch.push_back(entity("curved-axis",Sketch::Circle{{-40,40},5}));
    auto hidden=entity("hidden-axis",Sketch::Segment{{-10,0},{-10,50}});hidden.visible=false;sketch.push_back(hidden);
    auto locked=entity("locked-axis",Sketch::Segment{{-20,0},{-20,50}});locked.selectable=false;sketch.push_back(locked);
    v.restoreSketchEntities(sketch);(v.activateSketchWorkplane(wp.id)&&v.lookAtWorkplane(wp.id));v.setActivePart(part.id);v.selectSketchEntities({sketch.front().id});
    const auto original=v.boxes_[0];const auto history=v.undoStack_.size();window.documentDirty_=false;
    check(action->isEnabled() && window.findChild<QMenu*>("modeling3DMenu")->actions().count(action)==1
        && window.findChild<QToolBar*>("modeling3DToolbar")->actions().count(action)==1,"Revolve action not shared by menu/toolbar");
    check(v.context_->IsSelected(v.sketchPresentations_.at(sketch.front().id)),"initial Sketch selection fixture missing");
    action->trigger();CommandPanelTest::waitForInitialAnchor(*v.commandPanel_,v,"Revolve initial panel anchor");
    check(v.commandPanel_->windowTitle()=="Revolve" && !v.commandSession_->valid && v.commandSession_->sweep->axisPicking,"Revolve did not request axis");
    auto* combo=v.sweepControls_->findChild<QComboBox*>("revolutionAxis");auto* angle=v.sweepControls_->findChild<QLineEdit*>("revolutionAngle");
    auto* reverse=v.sweepControls_->findChild<QCheckBox*>("revolutionReverse");auto* regions=v.sweepControls_->findChild<QListWidget*>("cutProfiles");
    check(combo->findData("hidden-axis")<0 && combo->findData("locked-axis")<0,"unavailable sketch axes offered");
    v.view_->SetProj(0,0,1);v.fitAll();click(v,gp_Pnt(0,70,0));preview(v,47000*pi);
    check(v.activePartId()==part.id,"axis picking changed Active Part");
    check(v.selectedSketchIds_==QStringList{sketch.front().id},"axis picking changed Sketch selection metadata");
    check(v.context_->IsSelected(v.sketchPresentations_.at(sketch.front().id)),"axis picking lost ordinary Sketch highlight/selection");
    regions->item(1)->setSelected(false);preview(v,36000*pi);
    angle->setText("90");preview(v,9000*pi);check(centre(v.commandSession_->candidate).Z()<0,"positive angle direction wrong");
    reverse->setChecked(true);preview(v,9000*pi);check(centre(v.commandSession_->candidate).Z()>0,"reverse direction ignored");
    angle->setText("-90");check(centre(v.commandSession_->candidate).Z()<0,"signed angle/reverse composition wrong");
    // Enter the same 45.5-degree value using the panel's numeric locale (e.g.
    // Dutch "45,5"); a hard-coded decimal point is invalid under nl_NL.
    reverse->setChecked(false);angle->setText(QLocale().toString(45.5,'f',1));preview(v,36000*pi*45.5/360);
    angle->setText("0");v.confirmCommandSession();check(v.commandSession_ && !v.commandSession_->valid && v.commandSession_->preview.IsNull()
        && !v.commandButtons_->button(QDialogButtonBox::Ok)->isEnabled(),"invalid angle left stale preview/committed");
    angle->setText("360");regions->clearSelection();check(!v.commandSession_->valid && v.commandSession_->preview.IsNull(),"empty profile batch accepted");regions->item(0)->setSelected(true);
    combo->setCurrentIndex(combo->findData("crossing-axis"));check(!v.commandSession_->valid && v.commandSession_->preview.IsNull()
        && v.sweepControls_->findChild<QLabel*>("extrusionStatus")->text().contains("crosses"),"crossing axis retained stale preview");
    combo->setCurrentIndex(combo->findData("construction-axis"));preview(v,36000*pi);
    // Existing scroll wrapper must keep all controls reachable at normal/HiDPI.
    auto* scroll=v.commandPanel_->findChild<QScrollArea*>("commandPanelScrollArea");QElapsedTimer timer;timer.start();
    for(;;) {
        QApplication::processEvents();scroll->ensureWidgetVisible(v.commandButtons_,0,0);
        if(scroll->horizontalScrollBar()->maximum()==0 && QRect(scroll->viewport()->mapToGlobal(QPoint()),scroll->viewport()->size())
            .contains(QRect(v.commandButtons_->mapToGlobal(QPoint()),v.commandButtons_->size())))break;
        check(timer.elapsed()<2000,"Revolve buttons inaccessible");QEventLoop loop;QTimer::singleShot(10,&loop,&QEventLoop::quit);loop.exec();
    }
    CommandPanelTest::waitForInitialAnchor(*v.commandPanel_,v,"Revolve resized panel anchor");
    NativeDocumentSnapshot previewSnapshot;check(v.exportDocumentSnapshot(previewSnapshot) && previewSnapshot.parts.size()==1
        && previewSnapshot.sketchEntities==sketch && v.boxes_[0].shape.IsSame(original.shape) && v.undoStack_.size()==history && !window.documentDirty_,"Revolve preview changed document/history/storage");
    key(angle,Qt::Key_Escape);check(!v.commandSession_ && v.activePartId()==part.id && v.selectedSketchIds_==QStringList{sketch.front().id}
        && v.context_->IsSelected(v.sketchPresentations_.at(sketch.front().id)) && v.boxes_[0].shape.IsSame(original.shape) && !window.documentDirty_,"Escape did not preserve original context/geometry");
    v.view_->SetProj(0,0,1);v.fitAll();Standard_Integer hoverX,hoverY;v.view_->Convert(0,70,0,hoverX,hoverY);
    v.context_->MoveTo(hoverX,hoverY,v.view_,Standard_True);
    check(v.context_->HasDetected() && v.context_->DetectedInteractive()==v.sketchPresentations_.at("sketch-axis"),"normal sketch hover was not restored after Cancel");
    action->trigger();combo=v.sweepControls_->findChild<QComboBox*>("revolutionAxis");v.selectSketchEntities({"sketch-axis"});
    v.sweepControls_->findChild<QListWidget*>("cutProfiles")->item(1)->setSelected(false);
    preview(v,36000*pi);key(v.sweepControls_->findChild<QLineEdit*>("revolutionAngle"),Qt::Key_Return);
    check(!v.commandSession_ && v.boxes_.size()==2 && v.undoStack_.size()==history+1 && window.root_->childCount()==2
        && v.boxes_[1].name.startsWith("Revolve") && window.documentDirty_,"Revolve confirmation did not create one Part/history step");
    const auto id=v.boxes_[1].persistentId;check(!id.isEmpty() && v.activePartId()==part.id && v.selectedSketchIds_==QStringList{sketch.front().id}
        && v.context_->IsSelected(v.sketchPresentations_.at(sketch.front().id)) && v.activeSketchWorkplaneId_==wp.id
        && v.boxes_[0].shape.IsSame(original.shape) && v.boxes_[0].color==original.color,"Revolve confirmation changed sources/selection/activation/material");
    v.undoPull();check(!v.isPartActive(1) && v.activePartId()==part.id,"Revolve Undo removed wrong Part/activation");
    (v.activateSketchWorkplane(wp.id)&&v.lookAtWorkplane(wp.id));action->trigger();v.cancelCommandSession();check(v.redoStack_.size()==1,"Revolve Cancel cleared Redo");
    v.redoPull();check(v.isPartActive(1) && v.boxes_[1].persistentId==id && std::abs(volume(v.boxes_[1].shape)-36000*pi)<1.e-5,"Revolve Redo lost identity/geometry");
    QTemporaryDir directory;NativeDocumentSnapshot saved,loaded;check(v.exportDocumentSnapshot(saved),"Revolve snapshot failed");
    check(NativeDocumentStorage::save(directory.filePath("revolve.odc"),saved,error) && NativeDocumentStorage::load(directory.filePath("revolve.odc"),loaded,error),"Revolve Save/Open failed");
    check(loaded.parts.size()==2 && loaded.parts[1].id==id && loaded.activePartId==part.id && loaded.sketchEntities==sketch
        && std::abs(volume(loaded.parts[1].shape)-36000*pi)<1.e-5,"Revolve persistence lost source/solid/identity");
    (v.activateSketchWorkplane(wp.id)&&v.lookAtWorkplane(wp.id));action->trigger();check(v.importDocumentSnapshot(loaded,error) && !v.commandSession_ && v.undoStack_.empty(),"Open did not discard Revolve session");
    const auto reopenedOriginal=v.boxes_[0];
    // Linear Part edge resolution must apply its instance transform exactly once.
    (v.activateSketchWorkplane(wp.id)&&v.lookAtWorkplane(wp.id));
    Handle(StdSelect_BRepOwner) selectedFaceOwner=new StdSelect_BRepOwner(TopExp_Explorer(v.boxes_[0].shape,TopAbs_FACE).Current(),v.boxes_[0].presentation,5,Standard_True);
    v.context_->AddOrRemoveSelected(selectedFaceOwner,Standard_False);action->trigger();
    TopoDS_Edge edge;
    for(TopExp_Explorer it(v.boxes_[0].shape,TopAbs_EDGE);it.More();it.Next()) {
        BRepAdaptor_Curve curve(TopoDS::Edge(it.Current()));const auto a=curve.Value(curve.FirstParameter()),b=curve.Value(curve.LastParameter());
        if(std::abs(a.X()-10)<1.e-7 && std::abs(b.X()-10)<1.e-7 && std::abs(a.Z())<1.e-7 && std::abs(b.Z())<1.e-7 && std::abs(a.Y()-b.Y())>1) {edge=TopoDS::Edge(it.Current());break;}
    }
    check(!edge.IsNull(),"linear Part axis fixture missing");
    Handle(StdSelect_BRepOwner) owner=new StdSelect_BRepOwner(edge,v.boxes_[0].presentation,5,Standard_True);
    // Avoid coincident sketch/construction axis presentations in this Part-edge pick.
    v.context_->Deactivate(v.sketchPresentations_.at("sketch-axis"));v.context_->Deactivate(v.constructionPresentations_.at(axis.id));
    v.view_->SetProj(0,0,1);v.fitAll();click(v,gp_Pnt(0,15,0));
    check(v.commandSession_->sweep->axisPartId==part.id,"viewport did not resolve the linear Part edge");preview(v,47000*pi);
    check(v.chooseRevolutionAxis(owner),"linear Part edge rejected");
    check(v.commandSession_->sweep->axis->Location().Distance(gp_Pnt(0,0,0))<31 && std::abs(v.commandSession_->sweep->axis->Location().X())<1.e-7,"Part axis placement applied incorrectly");
    v.boxes_[0].orientation.SetTranslation(gp_Vec(-11,0,0));v.refreshProfileSweep();check(!v.commandSession_->valid && v.commandSession_->preview.IsNull(),"stale Part axis retained preview");v.boxes_[0].orientation=original.orientation;
    v.sweepControls_->findChild<QPushButton*>("pickRevolutionAxis")->click();
    auto curvedOwner=v.sketchPresentations_.at("curved-axis")->GlobalSelOwner();check(!v.chooseRevolutionAxis(curvedOwner) && !v.commandSession_->valid,"curved axis accepted");
    v.commandButtons_->button(QDialogButtonBox::Cancel)->click();check(!v.commandSession_ && v.boxes_[0].shape.IsSame(reopenedOriginal.shape),"Panel Cancel changed geometry");
    check(v.context_->IsSelected(selectedFaceOwner),"Cancel lost original local face highlight/selection");
    // Switching commands and source edits discard transient Revolve geometry.
    action->trigger();v.startProfileExtrusion();check(v.commandSession_ && v.commandSession_->kind==CadViewport::CommandSession::Kind::Extrude,"command switch failed");v.cancelCommandSession();
    action->trigger();auto changed=sketch;changed[0].visible=false;v.restoreSketchEntities(changed);check(!v.commandSession_,"source edit did not cancel Revolve");
    auto open=sketch;open.erase(open.begin());v.restoreSketchEntities(open);action->trigger();
    check(v.commandSession_ && !v.commandSession_->valid && v.commandSession_->preview.IsNull()
        && !v.sweepControls_->findChild<QLabel*>("extrusionStatus")->text().isEmpty(),"invalid Profile did not produce clear refusal");
    v.confirmCommandSession();check(v.commandSession_ && v.boxes_.size()==2,"invalid Profile was committed");v.cancelCommandSession();
    window.documentDirty_=false;window.close();
 }
};
void runProfileRevolutionSessions(){ProfileRevolutionSessionTest::run();ProfileRevolutionSessionTest::materialModes();}
