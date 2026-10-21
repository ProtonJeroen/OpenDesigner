#include "CadViewport.h"
#include "MainWindow.h"
#include "ConstructionPanel.h"
#include <QApplication>
#include <QAction>
#include <QTreeWidget>
#include <QTemporaryDir>
#include <QMenu>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>
#include <QAbstractButton>
#include <Image_AlienPixMap.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <iostream>
#include <stdexcept>
using namespace Construction;
namespace {
void check(bool ok,const char* text) {if(!ok) throw std::runtime_error(text);}
void chooseFile(const QString& path) {
    QTimer::singleShot(50,[path] {
        for(auto* widget:QApplication::topLevelWidgets()) if(auto* dialog=qobject_cast<QFileDialog*>(widget);dialog && dialog->isVisible()) {
            dialog->selectFile(path);QTimer::singleShot(50,dialog,[dialog]{QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);});return;
        }
    });
}
void discardPrompt() {
    QTimer::singleShot(50,[] {
        for(auto* widget:QApplication::topLevelWidgets()) if(auto* dialog=qobject_cast<QMessageBox*>(widget);dialog && dialog->isVisible()) {
            dialog->button(QMessageBox::Discard)->click();return;
        }
    });
}
}
struct ConstructionSessionTest {
    static void namesAndLabels() {
        MainWindow window;window.show();QApplication::processEvents();auto& v=*window.viewport_;
        const auto create=[&](Method method,const char* name) {
            v.startConstruction(method);check(v.constructionPanel_->name->text()==name,"default name sequence incorrect");
            v.confirmCommandSession();return v.constructions_.back().id;
        };
        v.startConstruction(Method::PointXYZ);check(v.constructionPanel_->name->text()=="PT1","initial Point name incorrect");v.cancelCommandSession();
        const auto p0=create(Method::PointXYZ,"PT1");
        v.startConstruction(Method::PointXYZ);v.constructionPanel_->setVector(gp_Vec(400,200,100));v.confirmCommandSession();const auto p1=v.constructions_.back().id;
        check(v.constructions_.back().name=="PT2","Point numbering not independent");
        const auto wp=create(Method::PlaneXYZ,"WP1");
        v.startConstruction(Method::AxisTwoPoints);
        check(v.constructionPanel_->name->text()=="AX1","Axis numbering not independent");
        v.constructionPanel_->roles[0]->setCurrentIndex(v.constructionPanel_->roles[0]->findData(p0));
        v.constructionPanel_->roles[1]->setCurrentIndex(v.constructionPanel_->roles[1]->findData(p1));v.confirmCommandSession();const auto ax=v.constructions_.back().id;
        check(v.constructionLabels_.size()==4 && v.constructionPreviewLabel_.IsNull(),"labels or preview cleanup missing");
        v.fitConstructions({wp,ax,p0,p1});
        window.documentDirty_=false;v.selectConstructions({p1});
        const auto history=v.undoStack_.size();
        std::unique_ptr<QMenu> zoom(v.createSelectionMenu(QPoint(-20,-20)));
        auto* fit=zoom->findChild<QAction*>("context.fit_selected");
        check(fit && fit->text()=="Fit Selected","construction selection lacks Fit Selected");fit->trigger();
        check(v.view_->Camera()->Center().Distance(v.constructionObject(p1)->frame.Location())<1.e-5
            && v.constructionSelection()==QStringList{p1} && v.undoStack_.size()==history && !window.documentDirty_,"construction zoom changed selection/history/dirty state or framed wrong object");
        v.fitConstructions({wp,ax,p0,p1});
        for(const auto& object:v.constructions_) {
            const auto label=v.constructionLabels_.at(object.id);
            check(v.context_->IsDisplayed(label) && QString::fromUtf16(label->Text().ToExtString())==object.name,"displayed name label incorrect");
            TColStd_ListOfInteger modes;v.context_->ActivatedModes(label,modes);check(modes.IsEmpty(),"name label intercepts geometry selection");
            check(label->ZLayer()==Graphic3d_ZLayerId_Topmost,"label hidden behind model");
        }
        const auto checkCorner=[&] {
            const auto object=*v.constructionObject(wp);const auto label=v.constructionLabels_.at(wp);
            int lx,ly;v.view_->Convert(label->Position().X(),label->Position().Y(),label->Position().Z(),lx,ly);
            int best=-1000000,bx=0,by=0;
            for(const auto& corner:displayBounds(object)) {int x,y;v.view_->Convert(corner.X(),corner.Y(),corner.Z(),x,y);if(y-x>best) {best=y-x;bx=x;by=y;}}
            check(lx<bx && ly>by,"Workplane label not below-left of projected lower-left corner");
        };
        checkCorner();window.documentDirty_=false;
        if(!qEnvironmentVariableIsEmpty("OPENDESIGNER_CONSTRUCTION_CAPTURE")) {
            v.view_->Redraw();Image_AlienPixMap pixels;
            check(v.view_->ToPixMap(pixels,v.width(),v.height())
                && pixels.Save((qgetenv("OPENDESIGNER_CONSTRUCTION_CAPTURE")+".names.ppm").constData()),"name label viewport capture failed");
        }
        v.view_->SetProj(V3d_XnegYposZpos);v.updateConstructionLabels();checkCorner();
        check(!window.documentDirty_,"camera label refresh dirtied document");
        auto point=*v.constructionObject(p0);point.name="Reference α";point.visible=false;v.updateConstruction(point);
        check(!v.context_->IsDisplayed(v.constructionLabels_.at(p0)),"hidden Point retained floating label");
        point.visible=true;v.updateConstruction(point);
        check(QString::fromUtf16(v.constructionLabels_.at(p0)->Text().ToExtString())==point.name,"rename did not refresh label");
        create(Method::PointXYZ,"PT3");v.undoPull();create(Method::PointXYZ,"PT4");
        v.deleteConstructions({p1});create(Method::PointXYZ,"PT5");
        NativeDocumentSnapshot snapshot;QString error;QTemporaryDir directory;v.exportDocumentSnapshot(snapshot);
        check(NativeDocumentStorage::save(directory.filePath("names.odc"),snapshot,error),"name counter Save failed");
        check(NativeDocumentStorage::load(directory.filePath("names.odc"),snapshot,error) && v.importDocumentSnapshot(snapshot,error),"name counter Open failed");
        create(Method::PointXYZ,"PT6");
        v.resetDocument();check(v.constructionLabels_.empty(),"New retained labels");create(Method::PointXYZ,"PT1");
        window.documentDirty_=false;
    }
    static void midplane() {
        MainWindow window;window.show();QApplication::processEvents();auto& v=*window.viewport_;v.addBox();
        std::vector<TopoDS_Face> caps;
        for(TopExp_Explorer it(v.boxes_[0].shape,TopAbs_FACE);it.More();it.Next()) {
            auto face=TopoDS::Face(it.Current());BRepAdaptor_Surface surface(face);
            if(surface.GetType()==GeomAbs_Plane && std::abs(surface.Plane().Axis().Direction().Z())>0.99) caps.push_back(face);
        }
        check(caps.size()==2,"box caps unavailable for Midplane");
        v.context_->ClearSelected(Standard_False);v.selectedPartIndices_.clear();
        for(const auto& face:caps) v.context_->AddOrRemoveSelected(new StdSelect_BRepOwner(face,v.boxes_[0].presentation,0,Standard_True),Standard_False);
        std::unique_ptr<QMenu> menu(v.createSelectionMenu(QPoint()));check(bool(menu),"Face context menu missing");
        auto* action=menu->findChild<QAction*>("context.midplane");check(action,"two planar Faces lack Midplane context action");action->trigger();
        check(v.commandSession_ && v.commandSession_->valid,"two preselected Faces did not produce Midplane preview");
        check(std::abs(v.commandSession_->construction->candidate.frame.Location().Z()-30)<1.e-8,"parallel Face midplane not halfway through box");
        const auto history=v.undoStack_.size();check(v.constructions_.empty(),"Midplane preview mutated document");
        auto* panel=v.constructionPanel_;check(panel->midplaneSolution,"bisector solution selector missing");
        panel->midplaneSolution->setCurrentIndex(1);check(!v.commandSession_->valid && v.commandSession_->preview.IsNull(),"parallel alternate retained invalid preview");
        panel->midplaneSolution->setCurrentIndex(0);check(v.commandSession_->valid,"parallel solution did not recover");
        v.cancelCommandSession();check(v.constructions_.empty() && v.undoStack_.size()==history,"Midplane Cancel changed history/document");
        v.startConstruction(Method::PlaneMidplane);check(v.commandSession_->valid,"Midplane Cancel lost Face preselection");v.confirmCommandSession();
        const auto parallel=*v.constructionObject(v.constructions_.back().id);
        check(v.undoStack_.size()==history+1,"Midplane commit not one history step");v.undoPull();check(v.constructions_.empty() && v.context_->NbSelected()==0,"Midplane Undo failed");v.redoPull();
        // Create a second Workplane whose normal differs by 60 degrees.
        v.startConstruction(Method::PlaneXYZ);v.constructionPanel_->angles[1]->setValue(60);v.confirmCommandSession();const auto sloped=v.constructions_.back();
        v.selectConstructions({parallel.id,sloped.id});QMenu workplanes;v.populateConstructionMenu(workplanes);
        auto* workplaneAction=workplanes.findChild<QAction*>("context.midplane");check(workplaneAction,"Workplane pair context action missing");workplaneAction->trigger();
        check(v.commandSession_->valid,"Workplane pair Midplane invalid");
        const auto first=v.commandSession_->construction->candidate;
        v.constructionPanel_->midplaneSolution->setCurrentIndex(1);check(v.commandSession_->valid,"wedge alternate bisector invalid");
        const auto second=v.commandSession_->construction->candidate;
        check(std::abs(first.frame.Direction().Dot(second.frame.Direction()))<1.e-9,"selector only reversed normal instead of choosing other plane");
        v.constructionPanel_->reverse->setChecked(true);
        check(v.commandSession_->construction->candidate.frame.Direction().Dot(second.frame.Direction())<-0.999,"Reverse normal interfered with solution choice");
        v.confirmCommandSession();const auto wedge=v.constructions_.back();
        QTemporaryDir temp;NativeDocumentSnapshot snapshot,loaded;QString error;v.exportDocumentSnapshot(snapshot);
        check(NativeDocumentStorage::save(temp.filePath("midplane.odc"),snapshot,error) && NativeDocumentStorage::load(temp.filePath("midplane.odc"),loaded,error),"Midplane native round-trip failed");
        check(equivalent(loaded.constructions.back(),wedge),"Midplane frame/properties changed on Open");
        v.deleteConstructions({parallel.id,sloped.id});check(equivalent(*v.constructionObject(wedge.id),wedge),"Midplane depends on source Workplanes");
        // A Face and Workplane can also supply the two roles together.
        v.startConstruction(Method::PlaneMidplane);panel=v.constructionPanel_;
        const auto& choices=v.commandSession_->construction->choices[0];
        auto faceChoice=std::find_if(choices.begin(),choices.end(),[](const auto& choice){return choice.objectId.isEmpty() && std::abs(choice.reference.frame.Direction().Z())>0.99;});
        check(faceChoice!=choices.end(),"Face reference missing from mixed Midplane roles");
        panel->roles[0]->setCurrentIndex(panel->roles[0]->findData(faceChoice->key));panel->roles[1]->setCurrentIndex(panel->roles[1]->findData(wedge.id));
        check(v.commandSession_->valid,"mixed Face/Workplane Midplane rejected");v.cancelCommandSession();
        window.documentDirty_=false;
    }
    static void run() {
        namesAndLabels();
        midplane();
        MainWindow window;window.show();QApplication::processEvents();auto& v=*window.viewport_;
        v.addBox();const auto solid=v.boxes_[0].shape;v.selectParts({0});
        const auto selectedOwner=v.context_->FirstSelectedObject();
        const auto history=v.undoStack_.size();
        auto createPoint=[&](const gp_Vec& p) {
            v.startConstruction(Method::PointXYZ);v.constructionPanel_->setVector(p);check(v.commandSession_->valid,"XYZ preview invalid");
            const auto count=v.constructions_.size();v.confirmCommandSession();check(v.constructions_.size()==count+1,"point not committed");return v.constructions_.back().id;
        };
        v.startConstruction(Method::PointXYZ);v.constructionPanel_->setVector(gp_Vec(10,20,30));
        check(v.constructions_.empty() && v.undoStack_.size()==history,"preview mutated document/history");
        check(!v.commandSession_->preview.IsNull() && !v.constructionPanel_->isModal(),"nonmodal preview missing");
        NativeDocumentSnapshot previewSnapshot;check(v.exportDocumentSnapshot(previewSnapshot) && previewSnapshot.constructions.empty(),"Save included unconfirmed construction");
        v.cancelCommandSession();check(!v.commandSession_ && v.constructions_.empty(),"Cancel committed point");
        check(v.context_->FirstSelectedObject()==selectedOwner,"Cancel did not restore Part selection");
        v.startConstruction(Method::PointXYZ,"missing-object-id");check(!v.commandSession_ && v.constructions_.empty(),"stale coordinate edit created a replacement object");
        const auto p0=createPoint(gp_Vec(0,0,0)),p1=createPoint(gp_Vec(10,0,0)),p2=createPoint(gp_Vec(0,10,0));
        check(window.constructionRoot_->childCount()==3 && window.root_->childCount()==1,"typed browser rows incorrect");
        // Real OCCT Face and Vertex picking while a command owns an operand role.
        v.fitAll();
        for(const auto method:{Method::PlaneFace,Method::PointVertex}) {
            v.startConstruction(method);v.chooseConstructionRole(0);int sx,sy;
            const gp_Pnt pick=method==Method::PlaneFace?gp_Pnt(50,40,60):gp_Pnt(0,0,60);
            v.view_->Convert(pick.X(),pick.Y(),pick.Z(),sx,sy);
            check(v.captureConstructionReference(QPoint(sx,sy)) && v.commandSession_->valid,"real Face/Vertex reference picking failed");
            check(v.commandSession_->construction->candidate.frame.Location().Z()>59.9,"picked local reference was misplaced");
            v.cancelCommandSession();
        }
        v.startConstruction(Method::PlaneThreePoints);
        auto* panel=v.constructionPanel_;
        panel->roles[0]->setCurrentIndex(panel->roles[0]->findData(p0));panel->roles[1]->setCurrentIndex(panel->roles[1]->findData(p1));
        panel->roles[2]->setCurrentIndex(panel->roles[2]->findData(p2));check(v.commandSession_->valid,"three-point plane preview");
        v.confirmCommandSession();const auto planeId=v.constructions_.back().id;const auto originalPlane=*v.constructionObject(planeId);
        check(originalPlane.kind==Kind::Workplane && originalPlane.frame.Direction().Z()>0.999,"plane normal incorrect");
        v.startConstruction(Method::AxisTwoPoints);panel=v.constructionPanel_;
        v.chooseConstructionRole(0);v.selectConstructions({p0});check(v.commandSession_ && v.commandSession_->construction->pickRole==-1,"browser operand cancelled session");
        v.chooseConstructionRole(1);v.selectConstructions({p1});check(v.commandSession_->valid,"axis operands incomplete");
        v.confirmCommandSession();const auto axisId=v.constructions_.back().id;
        const auto originalAxis=*v.constructionObject(axisId);
        // Creation is independent of later source movement and deletion.
        auto changedPoint=*v.constructionObject(p1);changedPoint.frame.SetLocation(gp_Pnt(500,600,700));v.updateConstruction(changedPoint);
        v.deleteConstructions({p2});check(equivalent(originalPlane,*v.constructionObject(planeId)) && equivalent(originalAxis,*v.constructionObject(axisId)),"persistent reference dependency");
        v.undoPull();v.undoPull();check(v.selectedConstructionIds_.empty() && v.context_->NbSelected()==0 && window.structure_->selectedItems().empty(),"Undo retained construction selection");
        v.redoPull();v.redoPull();check(v.selectedConstructionIds_.empty() && v.context_->NbSelected()==0,"Redo retained selection");
        auto modified=*v.constructionObject(axisId);modified.name="Hidden locked α";modified.visible=false;modified.selectable=false;v.updateConstruction(modified);
        check(!v.context_->IsDisplayed(v.constructionPresentations_.at(axisId)),"hidden construction displayed");
        v.selectConstructions({axisId});check(v.context_->NbSelected()==0,"locked hidden object picked");
        modified.visible=true;modified.selectable=true;v.updateConstruction(modified);
        v.selectConstructions({axisId});check(v.selectionContext().kind==CadViewport::SelectionContext::Kind::Constructions,"construction selection untyped");
        QMenu menu;v.populateConstructionMenu(menu);check(menu.actions().size()>=6,"construction context actions absent");
        v.startConstruction(Method::AxisMove,axisId);check(v.commandSession_->valid && v.constructionHandles_.size()==3,"move handles absent");
        check(v.constructionHandles_[0]->ZLayer()==Graphic3d_ZLayerId_Topmost,"construction arrows are hidden behind model material");
        const auto before=v.constructions_;const auto beforeHistory=v.undoStack_.size();
        v.constructionPanel_->setVector(gp_Vec(12,23,34));
        check(equivalent(*v.constructionObject(axisId),modified),"Move preview mutated object");
        NativeDocumentSnapshot snapshot;v.exportDocumentSnapshot(snapshot);
        auto storedAxis=std::find_if(snapshot.constructions.begin(),snapshot.constructions.end(),[&](const auto& o){return o.id==axisId;});
        check(storedAxis!=snapshot.constructions.end() && equivalent(*storedAxis,modified),"Save captured unconfirmed move");
        v.cancelCommandSession();check(v.undoStack_.size()==beforeHistory && v.constructionHandles_.empty(),"Cancel retained handles or history");
        v.startConstruction(Method::AxisMove,axisId);v.constructionPanel_->setVector(gp_Vec(12,23,34));v.confirmCommandSession();
        check(v.constructionObject(axisId)->frame.Location().IsEqual(gp_Pnt(12,23,34),1.e-8),"Move failed");
        check(v.undoStack_.size()==beforeHistory+1,"Move not one history step");v.undoPull();check(equivalent(*v.constructionObject(axisId),modified),"Move Undo failed");v.redoPull();
        // A real projected arrow drag uses the same candidate calculation.
        v.startConstruction(Method::PlaneMove,planeId);
        v.chooseConstructionRole(0);check(!v.commandSession_->valid,"Pick retained the previous operand as a confirmable candidate");
        v.constructionPanel_->roles[0]->setCurrentIndex(v.constructionPanel_->roles[0]->findData(planeId));
        check(v.commandSession_->construction->pickRole==-1 && v.commandSession_->valid && v.constructionHandles_.size()==3,"dropdown did not end reference picking and restore handles");
        const auto center=originalPlane.frame.Location();
        const auto end=center.Translated(gp_Vec(1,0,0)*v.constructionHandleLength_*0.85);
        int x,y,ex,ey;v.view_->Convert(end.X(),end.Y(),end.Z(),x,y);v.view_->Convert(center.X(),center.Y(),center.Z(),ex,ey);
        check(v.constructionMousePress(QPoint(x,y),Qt::NoModifier) && v.constructionDragAxis_>=0,"projected orange arrow not draggable");
        v.constructionMouseMove(QPoint(x+(x-ex)/2,y+(y-ey)/2),Qt::ShiftModifier);v.constructionMouseRelease();
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(x,y),QPointF(v.mapToGlobal(QPoint(x,y))),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(&v,&release);
        check(!v.commandPanel_->isVisible() && v.constructionPanel_->isVisible(),"construction drag opened the legacy dimension editor");
        check(v.commandSession_->construction->candidate.frame.Location().Distance(center)>0.1,"arrow drag did not move preview");v.cancelCommandSession();
        v.startConstruction(Method::PlaneRotate,planeId);check(v.constructionHandles_.size()==3,"rotate rings absent");
        v.constructionPanel_->setVector(gp_Vec(1,0,0));v.constructionPanel_->setValue(90);v.confirmCommandSession();
        check(v.constructionObject(planeId)->frame.Direction().Y()<-0.999,"plane rotation failed");v.undoPull();
        // Invalid inputs remove stale preview; numeric repair re-enables confirmation.
        v.startConstruction(Method::AxisPointDirection);panel=v.constructionPanel_;
        panel->roles[0]->setCurrentIndex(panel->roles[0]->findData(p0));panel->setVector(gp_Vec());
        check(!v.commandSession_->valid && v.commandSession_->preview.IsNull(),"zero direction kept a stale preview");
        const auto size=v.constructions_.size();v.confirmCommandSession();check(v.commandSession_ && v.constructions_.size()==size,"invalid command committed");
        panel->setVector(gp_Vec(0,0,1));check(v.commandSession_->valid,"valid input did not repair preview");v.cancelCommandSession();
        // Mixed history remains chronological across Part and construction operations.
        check(v.boxes_[0].shape.IsSame(solid),"construction changed source B-Rep before Part history");
        const auto beforeTransform=v.boxes_[0].orientation;
        v.beginCommandSession(CadViewport::CommandSession::Kind::Move,{0},gp_Dir(1,0,0));v.dragDimension_->setText("10");v.confirmCommandSession();
        createPoint(gp_Vec(44,55,66));v.undoPull();check(v.boxes_[0].orientation.TranslationPart().X()==10,"construction Undo changed Part");
        v.undoPull();check(v.boxes_[0].orientation.TranslationPart().IsEqual(beforeTransform.TranslationPart(),1.e-9),"Part Undo ordering damaged");
        v.redoPull();v.redoPull();
        GProp_GProps originalProperties,restoredProperties;BRepGProp::VolumeProperties(solid,originalProperties);BRepGProp::VolumeProperties(v.boxes_[0].shape,restoredProperties);
        check(std::abs(originalProperties.Mass()-restoredProperties.Mass())<1.e-7,"mixed history altered solid volume");
        QTemporaryDir temp;check(temp.isValid(),"temporary directory");
        window.documentPath_=temp.filePath("construction.odc");v.selectConstructions({planeId});
        const auto undo=v.undoStack_.size(),redo=v.redoStack_.size();const auto selection=v.selectedConstructionIds_;
        check(window.saveDocument(),"native Save with constructions failed");
        check(v.undoStack_.size()==undo && v.redoStack_.size()==redo && v.selectedConstructionIds_==selection,"Save lost construction history/selection");
        chooseFile(temp.filePath("saved-as.odc"));check(window.saveDocumentAs(),"construction Save As failed");
        check(v.undoStack_.size()==undo && v.redoStack_.size()==redo && v.selectedConstructionIds_==selection,"Save As lost construction history/selection");
        v.startConstruction(Method::PlaneMove,planeId);v.constructionPanel_->setVector(gp_Vec(100,200,300));
        check(window.saveDocument() && v.commandSession_ && v.constructionPanel_->isVisible(),"Save closed active construction preview");
        NativeDocumentSnapshot confirmedSnapshot;check(NativeDocumentStorage::load(window.documentPath_,confirmedSnapshot,tempError_),"Save during preview load");
        const auto savedPlane=std::find_if(confirmedSnapshot.constructions.begin(),confirmedSnapshot.constructions.end(),[&](const auto& o){return o.id==planeId;});
        check(savedPlane!=confirmedSnapshot.constructions.end() && equivalent(*savedPlane,*v.constructionObject(planeId)),"Save stored unconfirmed geometry");
        v.cancelCommandSession();
        const auto dirty=window.documentDirty_;v.view_->SetScale(v.view_->Scale()*1.1);check(window.documentDirty_==dirty,"camera dirtied construction document");
        check(NativeDocumentStorage::load(window.documentPath_,snapshot,tempError_),"construction file load failed");
        const auto storedCount=snapshot.constructions.size();
        Image_AlienPixMap pixels;check(v.view_->ToPixMap(pixels,800,600),"construction viewport failed to render");
        if(!qEnvironmentVariableIsEmpty("OPENDESIGNER_CONSTRUCTION_CAPTURE"))
            check(pixels.Save(qgetenv("OPENDESIGNER_CONSTRUCTION_CAPTURE").constData()),"construction viewport capture failed");
        v.startConstruction(Method::PointXYZ);v.constructionPanel_->setVector(gp_Vec(700,800,900));
        auto invalid=snapshot;invalid.constructions[0].extent=-1;
        check(!v.importDocumentSnapshot(invalid,tempError_) && v.commandSession_ && v.undoStack_.size()==undo,"failed Open damaged session/document");
        chooseFile(window.documentPath_);window.openDocument();
        check(!v.commandSession_ && v.constructionHandles_.empty() && v.undoStack_.empty() && v.redoStack_.empty() && v.selectedConstructionIds_.empty(),"Open did not reset construction tools/history");
        check(v.constructions_.size()==storedCount && window.constructionRoot_->childCount()==int(storedCount),"Open browser reconstruction failed");
        check(v.constructionObject(axisId)->name=="Hidden locked α","Open lost construction properties");
        // Property edits participate in the same dirty prompt and New path.
        auto property=*v.constructionObject(axisId);property.selectable=false;v.updateConstruction(property);
        check(window.documentDirty_,"construction property change did not dirty document");
        discardPrompt();window.newDocument();
        check(v.constructions_.empty() && v.constructionPresentations_.empty() && window.constructionRoot_->childCount()==0,"New retained construction objects");
        window.documentDirty_=false;
    }
    static QString tempError_;
};
QString ConstructionSessionTest::tempError_;
void runConstructionSessions() {ConstructionSessionTest::run();}
