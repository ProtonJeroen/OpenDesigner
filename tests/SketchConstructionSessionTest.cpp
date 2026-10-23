#include "CadViewport.h"
#include "MainWindow.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTreeWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QLocale>
#include <Image_AlienPixMap.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <cmath>
#include <stdexcept>

namespace {
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void key(QWidget* widget,int code) {QKeyEvent e(QEvent::KeyPress,code,Qt::NoModifier);QApplication::sendEvent(widget,&e);}
void chooseFile(const QString& path) {
    QTimer::singleShot(50,[path] {
        for(auto* widget:QApplication::topLevelWidgets()) if(auto* d=qobject_cast<QFileDialog*>(widget);d && d->isVisible()) {
            d->setDirectory(QFileInfo(path).absolutePath());d->selectFile(QFileInfo(path).fileName());
            if(auto* edit=d->findChild<QLineEdit*>("fileNameEdit")) edit->setText(QFileInfo(path).fileName());
            QTimer::singleShot(50,d,[d]{QMetaObject::invokeMethod(d,"accept",Qt::DirectConnection);});return;
        }
    });
}
}
struct SketchConstructionSessionTest {
    static NativeDocumentSnapshot fixture() {
        NativeDocumentSnapshot snapshot;Construction::Object wp;wp.id="wp";wp.name="WP1";wp.kind=Construction::Kind::Workplane;wp.extent=120;
        wp.frame=gp_Ax3(gp_Pnt(10,20,30),gp_Dir(1,2,3),gp_Dir(2,-1,0));auto wp2=wp;wp2.id="wp2";wp2.name="WP2";wp2.frame.SetLocation(gp_Pnt(-300,0,0));snapshot.constructions={wp,wp2};
        Sketch::Entity line;line.id="profile";line.name="Profile reference";line.workplaneId="wp";line.geometry=Sketch::Segment{{-60,0},{60,0}};
        auto infinite=line;infinite.id="infinite";infinite.name="Infinite reference";infinite.role=Sketch::Role::Construction;infinite.geometry=Sketch::InfiniteLine{{0,-30},{1,0}};
        auto c1=infinite;c1.id="c1";c1.name="Circle 1";c1.geometry=Sketch::Circle{{-30,40},10};auto c2=c1;c2.id="c2";c2.name="Circle 2";c2.geometry=Sketch::Circle{{30,40},15};
        auto hidden=line;hidden.id="hidden";hidden.visible=false;auto locked=line;locked.id="locked";locked.selectable=false;auto other=line;other.id="other";other.workplaneId="wp2";
        auto profileCircle=c1;profileCircle.id="profile-circle";profileCircle.role=Sketch::Role::Profile;profileCircle.geometry=Sketch::Circle{{0,-65},8};
        snapshot.sketchEntities={line,infinite,c1,c2,hidden,locked,other,profileCircle};return snapshot;
    }
    static void action(MainWindow& w,const char* id) {auto* a=w.findChild<QAction*>(id);check(a && a->isEnabled(),id);a->trigger();QApplication::processEvents();}
    static void mouse(CadViewport& v,Sketch::Point p,bool click=false) {
        const auto world=Sketch::worldPoint(p,v.constructionObject(v.activeSketchWorkplaneId_)->frame);int x,y;v.view_->Convert(world.X(),world.Y(),world.Z(),x,y);
        const QPoint at(x,y);QMouseEvent move(QEvent::MouseMove,QPointF(at),QPointF(v.mapToGlobal(at)),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&v,&move);
        if(click) {
            QMouseEvent press(QEvent::MouseButtonPress,QPointF(at),QPointF(v.mapToGlobal(at)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&v,&press);
            QMouseEvent release(QEvent::MouseButtonRelease,QPointF(at),QPointF(v.mapToGlobal(at)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&v,&release);
        }
    }
    static void render(CadViewport& v) {
        Image_AlienPixMap image;check(v.view_->ToPixMap(image,1000,700),"construction preview render failed");int green=0,blue=0;
        for(int y=0;y<700;++y) for(int x=0;x<1000;++x) {
            const auto c=image.PixelColor(x,y).GetRGB();if(c.Green()>c.Red()+0.2 && c.Green()>c.Blue()+0.2) ++green;
            if(c.Blue()>c.Red()+0.3 && c.Blue()>c.Green()+0.05 && c.Green()>c.Red()+0.1) ++blue;
        }
        if(!qgetenv("OPENDESIGNER_CONSTRUCTION_LINE_CAPTURE").isEmpty()) check(image.Save(qgetenv("OPENDESIGNER_CONSTRUCTION_LINE_CAPTURE").constData()),"capture failed");
        check(green>10 && blue>10,"green alternatives or blue chosen tangent missing from viewport");
    }
    static void run() {
        MainWindow w;w.resize(1600,1100);w.show();QApplication::processEvents();auto& v=*w.viewport_;QString error;const auto initial=fixture();
        check(v.importDocumentSnapshot(initial,error),"fixture import failed");w.documentDirty_=false;
        check(!w.findChild<QAction*>("sketch.offset_line")->isEnabled() && !w.findChild<QAction*>("sketch.common_tangent")->isEnabled(),"new tools enabled outside active WP");
        v.startSketchConstruction(Sketch::ConstructionTool::OffsetLine);check(!v.commandSession_,"tool implicitly activated WP");v.activateSketchWorkplane("wp");
        v.selectSketchEntities({"profile"});action(w,"sketch.offset_line");auto* p=v.sketchConstructionPanel_;
        check(p && p->references[0]->currentData()=="profile" && v.commandSession_->valid,"Profile preselection not accepted by Offset");
        for(const auto* id:{"hidden","locked","other","c1"}) check(p->references[0]->findData(id)==-1,"ineligible Offset reference in list");
        p->output->setCurrentIndex(1);p->distance->setValue(-12.3456789);
        const auto candidate=std::get<Sketch::Segment>(v.commandSession_->lineConstruction->candidate.geometry);
        check(candidate.start==Sketch::Point{-60,-12.3456789} && candidate.end==Sketch::Point{60,-12.3456789},"finite offset dimensions changed on rotated WP");
        check(v.sketchEntities_==initial.sketchEntities && v.undoStack_.empty() && !w.documentDirty_ && v.sketchPreviews_.size()==1,"Offset preview mutated canonical document/history");
        p->mouseSide->setChecked(true);mouse(v,{0,20});check(p->distance->value()==12.3456789,"mouse did not select positive offset side");
        mouse(v,{0,-20},true);check(p->distance->value()==-12.3456789 && !p->mouseSide->isChecked(),"mouse side did not lock without changing distance");
        p->side->setCurrentIndex(0);check(p->distance->value()>0,"explicit side did not update sign");
        p->distance->findChild<QLineEdit*>()->setText(p->distance->locale().toString(6.25,'f',2));key(p->distance,Qt::Key_Return);
        check(!v.commandSession_ && !v.sketchConstructionPanel_ && v.sketchEntities_.size()==initial.sketchEntities.size()+1 && v.undoStack_.size()==1 && w.documentDirty_,"Enter did not interpret/commit Offset as one step");
        const auto offset=v.sketchEntities_.back();check(offset.role==Sketch::Role::Construction && std::get<Sketch::Segment>(offset.geometry).start.y==6.25,"Offset stored wrong role or unaccepted numeric value");
        const auto confirmed=v.sketchEntities_;v.selectSketchEntities({offset.id});v.undoPull();check(v.sketchEntities_==initial.sketchEntities && v.sketchSelection().empty() && w.structure_->selectedItems().empty(),"Offset Undo retained selection or geometry");
        v.redoPull();check(v.sketchEntities_==confirmed && v.context_->NbSelected()==0,"Offset Redo changed ID or retained selection");
        auto changed=initial.sketchEntities[0];changed.geometry=Sketch::Segment{{0,0},{10,10}};check(v.updateSketchEntity(changed,error) && v.sketchEntities_.back()==offset,"source edit changed offset result");
        check(v.deleteSketchEntities({"profile"},error) && v.sketchEntities_.back()==offset,"source delete removed offset result");v.undoPull();v.undoPull();
        // Viewport picking and a real Structure Browser row supply armed roles.
        action(w,"sketch.offset_line");check(v.commandSession_->lineConstruction->pickRole==0,"missing reference was guessed");mouse(v,{40,0},true);
        check(v.commandSession_->lineConstruction->pickRole==-1,"viewport line reference not accepted");p=v.sketchConstructionPanel_;
        p->pickButtons[0]->click();
        QTreeWidgetItem* infiniteItem=nullptr;
        for(int i=0;i<w.constructionRoot_->child(0)->childCount();++i) if(w.constructionRoot_->child(0)->child(i)->data(0,Qt::UserRole)=="infinite") infiniteItem=w.constructionRoot_->child(0)->child(i);
        check(infiniteItem,"infinite browser row absent");infiniteItem->setSelected(true);
        check(v.commandSession_ && p->references[0]->currentData()=="infinite","browser operand cancelled or did not update Offset");
        p->output->setCurrentIndex(1);check(!v.commandSession_->valid && v.sketchPreviews_.empty(),"infinite-to-finite Offset fabricated a span");
        v.confirmCommandSession();check(v.commandSession_.has_value(),"missing interval committed");
        p->explicitInterval->setChecked(true);p->intervalStart->setValue(-15);p->intervalLength->setValue(45);p->distance->setValue(-5);
        check(v.commandSession_->valid && std::get<Sketch::Segment>(v.commandSession_->lineConstruction->candidate.geometry)==Sketch::Segment{{-15,-35},{30,-35}},"explicit finite placement wrong");
        p->distance->setValue(0);check(!v.commandSession_->valid && v.sketchPreviews_.empty(),"zero Offset kept stale preview");p->distance->setValue(-5);
        QTemporaryDir dir;w.documentPath_=dir.filePath("lines.odc");const auto history=v.undoStack_.size(),redo=v.redoStack_.size();
        check(w.saveDocument() && v.commandSession_ && v.undoStack_.size()==history && v.redoStack_.size()==redo,"Save closed Offset/history");
        NativeDocumentSnapshot loaded;check(NativeDocumentStorage::load(w.documentPath_,loaded,error) && loaded.sketchEntities==confirmed,"Save included pending Offset");
        key(p->intervalLength,Qt::Key_Escape);check(v.sketchEntities_==confirmed && !v.commandSession_ && !w.documentDirty_,"Offset Cancel changed document or dirty state");
        // Explicitly choose all four common tangents; no automatic first solution.
        v.selectSketchEntities({"c1","c2"});action(w,"sketch.common_tangent");p=v.sketchConstructionPanel_;
        check(p && p->solutions->count()==5 && !v.commandSession_->valid && v.constructionLineAlternatives_.size()==4,"tangents missing or first solution auto-selected");
        check(p->references[0]->findData("profile-circle")==-1,"Profile circle accepted as construction tangent reference");
        p->output->setCurrentIndex(1);
        for(int i=1;i<=4;++i) {
            p->solutions->setCurrentIndex(i);check(v.commandSession_->valid && v.sketchPreviews_.size()==1 && v.constructionLineAlternatives_.size()==3,"explicit tangent selection has wrong preview count");
            const auto& d=*v.commandSession_->lineConstruction;const auto segment=std::get<Sketch::Segment>(d.candidate.geometry);const auto& solution=d.solutions[size_t(i-1)];
            check(segment.start==solution.first && segment.end==solution.second && !v.sketchSnapMarker_.IsNull() && !v.sketchAnchorMarker_.IsNull(),"finite tangent contacts or markers wrong");
            for(const auto& alternative:v.constructionLineAlternatives_) {TColStd_ListOfInteger modes;v.context_->ActivatedModes(alternative,modes);check(modes.IsEmpty(),"tangent alternative intercepts normal selection");}
        }
        render(v);check(v.sketchEntities_==confirmed && !w.documentDirty_,"tangent candidate changed source document");
        check(w.saveDocument() && v.commandSession_ && NativeDocumentStorage::load(w.documentPath_,loaded,error) && loaded.sketchEntities==confirmed,"Save captured pending tangent");
        auto invalid=loaded;invalid.sketchEntities[0].workplaneId="missing";
        check(!v.importDocumentSnapshot(invalid,error) && v.commandSession_ && v.commandSession_->valid,"failed Open damaged tangent command");
        p->buttons->button(QDialogButtonBox::Ok)->click();
        const auto tangent=v.sketchEntities_.back();const auto withTangent=v.sketchEntities_;
        check(tangent.role==Sketch::Role::Construction && v.undoStack_.size()==history+1 && !v.sketchConstructionPanel_ && v.constructionLineAlternatives_.empty() && v.sketchSnapMarker_.IsNull(),"tangent commit not one step or leaked overlays");
        v.selectSketchEntities({tangent.id});v.undoPull();check(v.sketchEntities_==confirmed && v.sketchSelection().empty(),"tangent Undo incorrect");v.redoPull();check(v.sketchEntities_==withTangent && v.context_->NbSelected()==0,"tangent Redo changed identity/selection");
        check(v.deleteSketchEntities({"c1","c2"},error) && v.sketchEntities_.back()==tangent,"tangent depends on source deletion");v.undoPull();
        check(w.saveDocument(),"Save tangent document failed");chooseFile(dir.filePath("lines-as.odc"));check(w.saveDocumentAs(),"Save As failed");
        const auto savedHistory=v.undoStack_.size(),savedRedo=v.redoStack_.size();
        v.selectSketchEntities({"c1","c2"});action(w,"sketch.common_tangent");p=v.sketchConstructionPanel_;p->solutions->setCurrentIndex(1);
        check(w.saveDocument() && v.undoStack_.size()==savedHistory && v.redoStack_.size()==savedRedo,"Save altered tangent history");
        key(p->solutions,Qt::Key_Escape);check(v.sketchEntities_==withTangent && !w.documentDirty_ && !v.commandSession_ && v.constructionLineAlternatives_.empty(),"tangent Cancel changed document");
        // Actual Open clears pending tools and reconstructs browser values.
        action(w,"sketch.common_tangent");chooseFile(w.documentPath_);w.openDocument();
        check(v.sketchEntities_==withTangent && !v.commandSession_ && !v.sketchConstructionPanel_ && v.constructionLineAlternatives_.empty()
            && v.undoStack_.empty() && v.redoStack_.empty() && v.activeSketchWorkplaneId_.isEmpty(),"Open did not restore/reset construction lines");
        // No-solution and touching cases remain repairable/cancellable.
        auto touching=initial;touching.sketchEntities[2].geometry=Sketch::Circle{{0,0},20};touching.sketchEntities[3].geometry=Sketch::Circle{{10,0},10};
        check(v.importDocumentSnapshot(touching,error),"touching import failed");v.activateSketchWorkplane("wp");v.selectSketchEntities({"c1","c2"});action(w,"sketch.common_tangent");p=v.sketchConstructionPanel_;
        check(p->solutions->count()==2,"shared-contact infinite tangent missing or duplicated");p->solutions->setCurrentIndex(1);check(v.commandSession_->valid,"unique shared-contact infinite tangent invalid");
        p->output->setCurrentIndex(1);check(p->solutions->count()==1 && !v.commandSession_->valid && v.sketchPreviews_.empty()
            && p->findChild<QLabel*>("constructionLinePrompt")->text().contains("zero finite span"),"zero finite contact span silently accepted");
        p->output->setCurrentIndex(0);p->solutions->setCurrentIndex(1);check(v.commandSession_->valid,"output repair failed");v.cancelCommandSession();
        auto concentric=initial;concentric.sketchEntities[2].geometry=Sketch::Circle{{0,0},20};concentric.sketchEntities[3].geometry=Sketch::Circle{{0,0},10};
        check(v.importDocumentSnapshot(concentric,error),"concentric import failed");v.activateSketchWorkplane("wp");v.selectSketchEntities({"c1","c2"});action(w,"sketch.common_tangent");p=v.sketchConstructionPanel_;
        check(!v.commandSession_->valid && p->findChild<QLabel*>("constructionLinePrompt")->text().contains("Concentric"),"concentric case has no useful message");key(p,Qt::Key_Escape);
        // Ambiguous acquisition uses lists rather than guessing; WP changes cancel.
        check(v.importDocumentSnapshot(initial,error),"reset failed");v.activateSketchWorkplane("wp");v.view_->Camera()->SetScale(10000);action(w,"sketch.offset_line");mouse(v,{0,0},true);
        check(v.commandSession_->lineConstruction->referenceHits.size()>1 && v.commandSession_->lineConstruction->pickRole==0,"ambiguous reference was guessed");
        p=v.sketchConstructionPanel_;p->references[0]->setCurrentIndex(p->references[0]->findData("profile"));check(v.commandSession_->valid,"explicit reference list did not resolve ambiguity");
        v.activateSketchWorkplane("wp2");check(!v.commandSession_ && !v.sketchConstructionPanel_ && v.sketchEntities_==initial.sketchEntities,"WP switch committed or leaked Offset");
        // New and switching to Overdraw clean all construction-line UI/overlays.
        v.activateSketchWorkplane("wp");v.selectSketchEntities({"c1","c2"});action(w,"sketch.common_tangent");v.startSketchOverdraw();
        check(v.commandSession_->overdraw && !v.sketchConstructionPanel_ && v.constructionLineAlternatives_.empty(),"Overdraw switch leaked tangent tool");v.cancelCommandSession();
        action(w,"sketch.offset_line");w.documentDirty_=false;w.newDocument();
        check(!v.commandSession_ && !v.sketchConstructionPanel_ && v.sketchEntities_.empty() && v.constructionLineAlternatives_.empty(),"New retained construction-line session");w.documentDirty_=false;
    }
};
void runSketchConstructionSessions() {SketchConstructionSessionTest::run();}
