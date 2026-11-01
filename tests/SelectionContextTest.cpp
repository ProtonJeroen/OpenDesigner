#include "CadViewport.h"
#include "MainWindow.h"
#include <QApplication>
#include <QComboBox>
#include <QToolBar>
#include <QToolButton>
#include <QMenuBar>
#include <QSignalBlocker>
#include <StdSelect_ViewerSelector3d.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <QMenu>
#include <QAction>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QTreeWidget>
#include <QLineEdit>
#include <QLabel>
#include <QKeyEvent>
#include <StdSelect_BRepOwner.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <Quantity_Color.hxx>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
QAction* action(QMenu& menu,const char* id) {
    auto* found=menu.findChild<QAction*>(QString::fromLatin1(id));require(found,"context action missing");return found;
}
}
struct SelectionContextTest {
    static QPoint project(const CadViewport& v,const gp_Pnt& p) {
        Standard_Integer x,y;v.view_->Convert(p.X(),p.Y(),p.Z(),x,y);return QPoint(x,y);
    }
    static void click(CadViewport& v,const QPoint& point,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(point),QPointF(point),Qt::LeftButton,Qt::LeftButton,modifiers);
        v.mousePressEvent(&press);
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(point),QPointF(point),Qt::LeftButton,Qt::NoButton,modifiers);
        v.mouseReleaseEvent(&release);
    }
    static auto owners(CadViewport& v) {
        std::vector<Handle(SelectMgr_EntityOwner)> result;
        for(v.context_->InitSelected();v.context_->MoreSelected();v.context_->NextSelected())result.push_back(v.context_->SelectedOwner());
        return result;
    }
    static bool popup(CadViewport& v,const QPoint& point) {
        bool shown=false;
        QTimer timer;timer.setSingleShot(true);
        QObject::connect(&timer,&QTimer::timeout,[&]() {
            if(auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget())) {shown=true;menu->close();}
        });
        timer.start(0);
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(point),QPointF(point),Qt::RightButton,Qt::RightButton,Qt::NoModifier);
        v.mousePressEvent(&press);
        QContextMenuEvent event(QContextMenuEvent::Mouse,point,v.mapToGlobal(point));v.contextMenuEvent(&event);
        timer.stop();return shown;
    }
    static TopoDS_Face topFace(const TopoDS_Shape& shape) {
        TopoDS_Face result;double z=-1.e30;
        for(TopExp_Explorer f(shape,TopAbs_FACE);f.More();f.Next()) {
            const auto face=TopoDS::Face(f.Current());const BRepAdaptor_Surface surface(face);
            if(surface.GetType()==GeomAbs_Plane && surface.Plane().Axis().Direction().Z()>0.999 && surface.Plane().Location().Z()>z) {result=face;z=surface.Plane().Location().Z();}
        }
        require(!result.IsNull(),"missing top face");return result;
    }
    static void chooseFace(CadViewport& v,size_t index,const TopoDS_Face& face) {
        v.selectParts({});
        Handle(StdSelect_BRepOwner) owner=new StdSelect_BRepOwner(face,v.boxes_[index].presentation,5,Standard_True);
        v.context_->AddOrRemoveSelected(owner,Standard_False);
    }
    static void historyDeselect() {
        // Exercise the real shared actions through all three UI entry points.
        for(int operation=0;operation<3;++operation) for(int route=0;route<4;++route) {
            MainWindow window;window.show();window.activateWindow();QApplication::processEvents();
            auto& v=*window.findChild<CadViewport*>();auto* tree=window.findChild<QTreeWidget*>();auto* root=tree->topLevelItem(0);
            v.addBox();v.addBox();v.addBox();v.setPartVisible(2,false);
            v.applyPartState(1,{50,0,0},{150,80,60},gp_Trsf());
            v.renamePart(0,"History identity");v.setPartColor(0,Qt::red);
            const auto before=v.boxes_[0];const auto beforeSecond=v.boxes_[1];
            const auto original=topFace(before.shape);
            if(operation<2) {
                chooseFace(v,0,original);
                if(operation==0) require(v.startMoveFace(0,original),"history Move start failed");
                else {require(v.startRotateFace(0,original),"history Rotate start failed");v.chooseFaceRotationAxis(2);}
                v.dragDimension_->setText(operation==0?"5":"12");v.confirmCommandSession();
            } else require(v.applyBoolean(CadViewport::BooleanOperation::Unite,{0,1}),"history Boolean setup failed");
            require(v.undoStack_.size()==1&&v.redoStack_.empty(),"history setup did not produce one committed operation");
            const size_t result=operation==2?v.boxes_.size()-1:0;
            const auto after=v.boxes_[result];const auto slotCount=v.boxes_.size();
            auto* toolbar=window.findChild<QToolBar*>();require(toolbar,"modeling toolbar missing");
            QMenu* edit=nullptr;
            for(auto* item:window.menuBar()->actions()) if(item->menu()&&item->text().contains("Edit")) edit=item->menu();
            require(edit&&edit->actions().contains(v.undoAction())&&edit->actions().contains(v.redoAction()),"Edit uses a different history action");
            const auto trigger=[&](bool redo) {
                auto* shared=redo?v.redoAction():v.undoAction();require(shared->isEnabled(),"history action unexpectedly disabled");
                if(route==0) {
                    auto* button=qobject_cast<QToolButton*>(toolbar->widgetForAction(shared));require(button,"history toolbar button missing");button->click();
                } else if(route==1) shared->trigger(); // Exact action owned by Edit.
                else {
                    QWidget* receiver=&v;
                    if(route==3) {v.commandPanel_->activateWindow();v.dragDimension_->setFocus();receiver=v.dragDimension_;}
                    else {window.activateWindow();v.setFocus();}
                    QApplication::processEvents();
                    QKeyEvent press(QEvent::KeyPress,redo?Qt::Key_Y:Qt::Key_Z,Qt::ControlModifier);
                    QApplication::sendEvent(receiver,&press);
                    QKeyEvent release(QEvent::KeyRelease,redo?Qt::Key_Y:Qt::Key_Z,Qt::ControlModifier);
                    QApplication::sendEvent(receiver,&release);QApplication::processEvents();
                }
            };
            std::vector<Handle(SelectMgr_EntityOwner)> staleOwners;
            const auto seedSelection=[&](size_t index) {
                v.selectedPartIndices_={index};v.context_->ClearSelected(false);
                TopExp_Explorer edge(v.boxes_[index].shape,TopAbs_EDGE),face(v.boxes_[index].shape,TopAbs_FACE);
                Handle(StdSelect_BRepOwner) faceOwner=new StdSelect_BRepOwner(face.Current(),v.boxes_[index].presentation,5,true);
                Handle(StdSelect_BRepOwner) edgeOwner=new StdSelect_BRepOwner(edge.Current(),v.boxes_[index].presentation,5,true);
                v.context_->AddOrRemoveSelected(faceOwner,false);v.context_->AddOrRemoveSelected(edgeOwner,false);
                v.context_->AddOrRemoveSelected(v.boxes_[index].presentation,false);
                staleOwners={faceOwner,edgeOwner};
                require(v.context_->NbSelected()>0&&!v.selectedPartIndices_.empty(),"selection fixture was empty");
                {const QSignalBlocker blocked(tree);tree->setCurrentItem(root->child(static_cast<int>(index)));root->child(static_cast<int>(index))->setSelected(true);root->child(2)->setSelected(true);}
                require(!tree->selectedItems().empty(),"browser selection fixture was empty");
            };
            const auto assertClear=[&]() {
                require(v.selectedPartIndices_.empty()&&v.selectionContext().kind==CadViewport::SelectionContext::Kind::None
                    &&v.context_->NbSelected()==0,"history retained internal/AIS face, edge or Part selection");
                require(!v.context_->HasDetected()&&v.context_->MainSelector()->NbPicked()==0,"history retained detected/picked topology owners");
                for(const auto& owner:staleOwners) require(!owner->IsSelected()&&!v.context_->IsHilighted(owner),"old subshape owner remained selected/highlighted");
                for(const auto& part:v.boxes_) if(part.active&&part.visible) require(!v.context_->IsHilighted(part.presentation),"Part selection highlight remained");
                require(tree->selectedItems().empty()&&tree->currentItem()==nullptr,"history retained Structure Browser selection/focus");
                require(!v.commandSession_&&!v.moveFacePicking_&&!v.faceAxisPicking_&&!v.alignFacePickingReference_
                    &&!v.movePartMode_&&!v.directMoveMode_&&!v.rotateMode_&&!v.pullMode_,"history retained active tools");
                require(v.faceArrow_.IsNull()&&v.moveFaceArrow_.IsNull()&&v.faceRotationManipulator_.IsNull()&&v.multiFaceHighlight_.IsNull(),"history retained face tools/highlights");
                for(const auto& arrow:v.coneArrows_) require(arrow.IsNull(),"history retained cone arrow");
                for(const auto& arrow:v.moveAxes_) require(arrow.IsNull(),"history retained move axes");
                for(const auto& ring:v.rotationRings_) require(ring.IsNull(),"history retained rotation rings");
                require(v.multiFaceParts_.empty()&&!v.commandPanel_->isVisible()&&!v.dragDimension_->isVisible(),"history retained command UI or subshape references");
                std::unique_ptr<QMenu> menu(v.createSelectionMenu(QPoint(-20,-20)));
                require(menu&&menu->actions().size()==4&&menu->actions()[0]==v.undoAction()&&menu->actions()[1]==v.redoAction(),"history did not restore background Undo/Redo menu");
                require(popup(v,QPoint(-20,-20)),"empty-space right click after history did not open a menu");
            };
            // Undo while a separate unconfirmed Move candidate is visible.
            TopExp_Explorer currentFace(after.shape,TopAbs_FACE);
            require(v.startMoveFace(result,TopoDS::Face(currentFace.Current()))&&v.previewCommand(2),"pending Move preview failed");
            const auto undoPreview=v.commandSession_->preview;seedSelection(result);
            trigger(false);assertClear();
            require(v.context_->DisplayStatus(undoPreview)==PrsMgr_DisplayStatus_None,"Undo retained unconfirmed preview");
            require(v.undoStack_.empty()&&v.redoStack_.size()==1&&v.boxes_[0].shape.IsSame(before.shape),"Undo committed preview or restored wrong geometry");
            if(operation==2) require(v.isPartActive(0)&&v.isPartActive(1)&&!v.isPartActive(result)&&v.boxes_[1].shape.IsSame(beforeSecond.shape),"Boolean Undo lost operands");
            // Redo while a separate unconfirmed Rotate candidate/ring is visible.
            require(v.startRotateFace(0,topFace(v.boxes_[0].shape)),"pending Rotate start failed");v.chooseFaceRotationAxis(2);
            require(v.previewCommand(3),"pending Rotate preview failed");const auto redoPreview=v.commandSession_->preview;seedSelection(0);
            trigger(true);assertClear();
            require(v.context_->DisplayStatus(redoPreview)==PrsMgr_DisplayStatus_None,"Redo retained unconfirmed preview");
            require(v.undoStack_.size()==1&&v.redoStack_.empty()&&v.boxes_[result].shape.IsSame(after.shape),"Redo did not restore exactly the cached operation");
            require(v.boxes_.size()==slotCount&&BRepCheck_Analyzer(v.boxes_[result].shape).IsValid()&&!v.boxes_[2].visible,"history changed slots, validity or hidden Part visibility");
            require(v.boxes_[0].name==before.name&&v.boxes_[0].color==before.color,"history lost source Part properties");
        }
    }
    static void zoomMenus() {
        MainWindow window;window.show();QApplication::processEvents();auto& v=*window.findChild<CadViewport*>();
        v.addBox();v.addBox();
        auto& part=v.boxes_[0];part.orientation.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),0.4);
        part.orientation.SetTranslationPart(gp_Vec(500,200,100));part.presentation->SetLocalTransformation(part.orientation);
        v.selectParts({});const auto title=window.windowTitle();
        const auto undo=v.undoStack_.size(),redo=v.redoStack_.size();
        std::unique_ptr<QMenu> background(v.createSelectionMenu(QPoint(-20,-20)));
        require(action(*background,"context.fit_all")->text()=="Fit All" && !background->findChild<QAction*>("context.fit_selected"),"wrong empty-selection zoom menu");
        action(*background,"context.fit_all")->trigger();const double allScale=v.view_->Camera()->Scale();
        v.selectParts({0});const auto partOwners=owners(v);
        std::unique_ptr<QMenu> selected(v.createSelectionMenu(QPoint(-20,-20)));
        require(action(*selected,"context.fit_selected")->text()=="Fit Selected" && !selected->findChild<QAction*>("context.fit_all"),"wrong selected zoom menu");
        action(*selected,"context.fit_selected")->trigger();
        require(v.view_->Camera()->Scale()<allScale && v.view_->Camera()->Center().Distance(gp_Pnt(50,40,30).Transformed(part.orientation))<1.e-3,"Fit Selected did not frame the located Part");
        require(owners(v)==partOwners && popup(v,QPoint(-20,-20)),"Part background zoom menu lost selection");
        chooseFace(v,0,topFace(part.shape));const auto faceOwners=owners(v);
        selected.reset(v.createSelectionMenu(QPoint(-20,-20)));action(*selected,"context.fit_selected")->trigger();
        require(v.view_->Camera()->Center().Distance(gp_Pnt(50,40,60).Transformed(part.orientation))<1.e-3,"Face zoom framed whole Part or applied placement incorrectly");
        require(owners(v)==faceOwners && popup(v,QPoint(-20,-20)),"Face background zoom menu lost selection");
        v.selectParts({0,1});selected.reset(v.createSelectionMenu(QPoint()));
        action(*selected,"context.fit_selected")->trigger();require(v.view_->Camera()->Scale()>allScale*0.8,"multi-Part zoom ignored some selected geometry");
        require(v.undoStack_.size()==undo && v.redoStack_.size()==redo && window.windowTitle()==title && !v.commandSession_,"zoom mutated document/history or started a modeling command");
    }
    static void run() {
        zoomMenus();
        MainWindow window; window.show(); QApplication::processEvents();
        auto& v=*window.findChild<CadViewport*>();
        auto* tree=window.findChild<QTreeWidget*>(); auto* root=tree->topLevelItem(0);
        require(!window.findChild<QComboBox*>("selectionMode"),"obsolete selection dropdown remains");
        // The background uses the same QAction objects as Edit/toolbar, not a
        // second history or a modeling command.
        std::unique_ptr<QMenu> background(v.createSelectionMenu(QPoint(5,5)));
        require(background && background->actions().size()==4,"missing background history menu");
        require(background->actions()[0]==v.undoAction() && background->actions()[1]==v.redoAction(),"history actions were duplicated");
        require(!v.undoAction()->isEnabled() && !v.redoAction()->isEnabled(),"empty history actions enabled");
        require(v.undoAction()->shortcut()==QKeySequence(Qt::CTRL|Qt::Key_Z)
            && v.redoAction()->shortcut()==QKeySequence(Qt::CTRL|Qt::Key_Y),"wrong history shortcuts");
        require(popup(v,QPoint(5,5)) && v.selectionContext().kind==CadViewport::SelectionContext::Kind::None
            && !v.commandSession_,"background menu selected geometry or started a command");
        v.rotating_=true; require(!popup(v,QPoint(5,5)),"context menu interrupted camera orbit");v.rotating_=false;
        v.panning_=true; require(!popup(v,QPoint(5,5)),"context menu interrupted camera pan");v.panning_=false;
        v.addBox(); v.addBox(); v.fitAll();
        const auto p0=project(v,gp_Pnt(50,40,60)),p1=project(v,gp_Pnt(175,40,60));
        const auto idle=[&]() {
            require(!v.commandSession_ && !v.moveFacePicking_ && !v.pullMode_ && !v.movePartMode_ && !v.directMoveMode_ && !v.rotateMode_,"command remained active");
            require(!v.arrowVisible_ && v.moveFaceArrow_.IsNull() && v.faceRotationManipulator_.IsNull()
                    && v.moveAxes_[0].IsNull() && v.rotationRings_[0].IsNull(),"orphan manipulators remain");
            require(!v.commandPanel_->isVisible() && !v.dragDimension_->isVisible(),"orphan command UI remains");
        };
        v.setDirectMoveMode(true); idle(); v.setRotateMode(true); idle();
        click(v,p0); idle(); require(v.selectionContext().kind==CadViewport::SelectionContext::Kind::Faces,"default click not Face");
        const auto faceOwners=owners(v);
        require(popup(v,p0) && owners(v)==faceOwners,"selected Face context menu changed selection");
        require(popup(v,QPoint(-20,-20)) && owners(v)==faceOwners,"background menu lost selected Face");
        click(v,p1,Qt::ControlModifier); idle(); require(v.selectionContext().faces.size()==2,"Ctrl did not add Face");
        click(v,p0,Qt::AltModifier); click(v,p1,Qt::AltModifier|Qt::ControlModifier); idle();
        require(v.selectedPartIndices_==std::vector<size_t>({0,1}),"Alt/Ctrl+Alt Part selection failed");
        const auto selected=owners(v);
        QKeyEvent altRelease(QEvent::KeyRelease,Qt::Key_Alt,Qt::NoModifier); QApplication::sendEvent(&v,&altRelease);
        require(owners(v)==selected && popup(v,p0) && owners(v)==selected,"Alt release/right click changed Part selection");
        require(popup(v,QPoint(-20,-20)) && owners(v)==selected,"background menu cleared/replaced existing selection");
        std::unique_ptr<QMenu> parts(v.createSelectionMenu(p0)); action(*parts,"context.move_part")->trigger();
        require(v.directMoveMode_ && !v.moveAxes_[0].IsNull() && v.rotationRings_[0].IsNull(),"explicit Move activation failed");
        QKeyEvent emptyConfirm(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier); QApplication::sendEvent(&v,&emptyConfirm); idle();
        action(*parts,"context.move_part")->trigger();
        const auto before=v.boxes_[0].orientation; const auto history=v.undoStack_.size();
        v.beginCommandSession(CadViewport::CommandSession::Kind::Move,{0,1},gp_Dir(1,0,0)); v.previewCommand(12);
        v.dragDimension_->setText("12"); v.confirmCommandSession(); idle();
        require(v.undoStack_.size()==history+1 && v.selectedPartIndices_==std::vector<size_t>({0,1}),"Move confirm/history/selection failed");
        v.selectParts({});
        background.reset(v.createSelectionMenu(QPoint(5,5)));
        require(v.undoAction()->isEnabled() && !v.redoAction()->isEnabled(),"commit history availability incorrect");
        require(popup(v,QPoint(-20,-20)) && !v.commandSession_,"history background menu missing");
        background->actions()[0]->trigger(); idle();
        require(!v.undoAction()->isEnabled() && v.redoAction()->isEnabled() && v.undoStack_.size()==history,"background Undo did not update history/actions");
        background->actions()[1]->trigger(); idle();
        require(v.undoAction()->isEnabled() && !v.redoAction()->isEnabled() && v.undoStack_.size()==history+1,"background Redo did not update history/actions");
        background->actions()[0]->trigger(); idle();
        v.selectParts({0,1});
        action(*parts,"context.rotate_part")->trigger();
        require(v.rotateMode_ && !v.rotationRings_[0].IsNull() && v.moveAxes_[0].IsNull(),"exclusive Rotate activation failed");
        v.beginCommandSession(CadViewport::CommandSession::Kind::Rotate,{0,1},gp_Dir(0,0,1),v.selectionCenter()); v.previewCommand(25);
        v.cancelCommandSession(); idle(); require(v.undoStack_.size()==history,"Cancel added history");
        action(*parts,"context.move_part")->trigger();
        v.beginCommandSession(CadViewport::CommandSession::Kind::Move,{0},gp_Dir(1,0,0)); v.previewCommand(17);
        click(v,p0); idle(); require(v.selectionContext().kind==CadViewport::SelectionContext::Kind::Faces,"new Face selection lost on tool cancellation");
        require(v.boxes_[0].orientation.TranslationPart().IsEqual(before.TranslationPart(),1.e-10),"tool switch committed preview");
        std::unique_ptr<QMenu> faces(v.createSelectionMenu(p0));
        require(action(*faces,"context.move_face") && action(*faces,"context.resize"),"supported Face actions missing");
        action(*faces,"context.resize")->trigger(); require(v.arrowVisible_ && v.pullMode_,"explicit Resize did not show arrow");
        const auto pullShape=v.boxes_[0].shape; const auto pullMax=v.boxes_[0].max;
        require(v.beginArrowDrag(project(v,gp_Pnt(50,40,75))),"explicit Box Pull drag failed");
        v.previewCommand(5); require(v.undoStack_.size()==history,"Pull preview created history");
        v.cancelCommandSession(); idle(); require(v.boxes_[0].shape.IsSame(pullShape) && v.boxes_[0].max==pullMax,"Pull Cancel did not restore exact geometry");
        click(v,p0); v.startPrimitivePull(); require(v.beginArrowDrag(project(v,gp_Pnt(50,40,75))),"second explicit Pull failed");
        v.dragDimension_->setText("5"); v.confirmCommandSession(); idle(); require(v.undoStack_.size()==history+1,"Pull did not commit exactly once");
        v.undoPull(); idle(); require(v.boxes_[0].shape.IsSame(pullShape),"Pull Undo did not restore original");
        // Multi-Face Box Pull commits/rolls back the entire group in one history entry.
        click(v,p0); click(v,p1,Qt::ControlModifier); v.startPrimitivePull();
        auto groupGrab=[&]() {
            const auto& part=v.boxes_[v.arrowPartIndex_];
            return project(v,gp_Pnt((part.min[0]+part.max[0])/2,(part.min[1]+part.max[1])/2,part.max[2]+15));
        };
        const auto groupShape0=v.boxes_[0].shape,groupShape1=v.boxes_[1].shape;
        require(v.beginArrowDrag(groupGrab()) && v.commandSession_->changes.size()==2,"group Pull did not start one shared session");
        v.previewCommand(7); v.cancelCommandSession(); idle();
        require(v.boxes_[0].shape.IsSame(groupShape0) && v.boxes_[1].shape.IsSame(groupShape1),"group Pull Cancel changed source shapes");
        v.startPrimitivePull(); require(v.beginArrowDrag(groupGrab()),"group Pull restart failed");
        v.dragDimension_->setText("7"); v.confirmCommandSession(); idle();
        require(v.undoStack_.size()==history+1 && v.selectedPartIndices_.size()==2,"group Pull confirm/history/selection failed");
        v.undoPull(); idle(); require(v.boxes_[0].shape.IsSame(groupShape0) && v.boxes_[1].shape.IsSame(groupShape1) && v.selectedPartIndices_.empty() && v.context_->NbSelected()==0,"group Pull Undo failed");
        v.redoPull(); idle(); require(v.boxes_[0].max[2]==67 && v.boxes_[1].max[2]==67,"group Pull Redo failed"); v.undoPull();
        v.selectParts({}); require(popup(v,p0),"right click without selection did not pick Face"); idle();
        require(v.selectionContext().kind==CadViewport::SelectionContext::Kind::Faces,"default right click did not retain Face type");
        action(*faces,"context.move_face")->trigger(); require(v.commandSession_ && !v.moveFaceArrow_.IsNull(),"explicit Move Face failed");
        v.previewCommand(2); v.cancelCommandSession(); idle();
        v.selectParts({0}); std::unique_ptr<QMenu> boolean(v.createSelectionMenu(p0)); action(*boolean,"context.unite")->trigger();
        require(v.commandSession_ && v.commandSession_->target==0,"Boolean lost Target");
        tree->clearSelection(); root->child(1)->setSelected(true);
        require(v.commandSession_ && v.commandSession_->tools==std::vector<size_t>{1},"browser Tools selection ended Boolean");
        require(v.previewBooleanSession(),"Boolean preview failed");
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(p1),QPointF(p1),Qt::LeftButton,Qt::NoButton,Qt::NoModifier); v.mouseReleaseEvent(&release);
        require(!v.dragDimension_->isVisible(),"Boolean tool release opened numeric field");
        v.cancelCommandSession(); idle();
        v.selectParts({0}); v.uniteSelectedParts(); v.selectBooleanParts({1},true); v.confirmCommandSession(); idle();
        require(v.undoStack_.size()==history+1,"Boolean history failed"); v.undoPull(); idle();
        v.selectParts({0}); v.setDirectMoveMode(true); v.duplicateSelectedParts(); idle();
        v.addSphere(); const size_t sphere=v.boxes_.size()-1; TopExp_Explorer sf(v.boxes_[sphere].shape,TopAbs_FACE); chooseFace(v,sphere,TopoDS::Face(sf.Current()));
        std::unique_ptr<QMenu> sphereMenu(v.createSelectionMenu(p0));
        require(!sphereMenu->findChild<QAction*>("context.move_face") && sphereMenu->findChild<QAction*>("context.resize"),"unsupported actions were displayed");

        chooseFace(v,0,topFace(v.boxes_[0].shape));
        std::unique_ptr<QMenu> faceMenu(v.createSelectionMenu(p0));
        require(faceMenu && faceMenu->findChild<QAction*>("context.rotate_face")
                && faceMenu->findChild<QAction*>("context.align_face"),"supported Rotate/Align Face actions missing");
        const auto rotationHistory=v.undoStack_.size();
        const auto targetName=v.boxes_[0].name;
        const auto targetColor=v.boxes_[0].color;
        action(*faceMenu,"context.rotate_face")->trigger();
        require(v.commandSession_ && v.commandSession_->kind==CadViewport::CommandSession::Kind::RotateFace
                && v.faceAxisPicking_ && v.faceRotationManipulator_.IsNull(),"Rotate Face axis selection phase did not start cleanly");
        v.chooseFaceRotationAxis(1);
        require(v.commandSession_->valid && !v.faceRotationManipulator_.IsNull(),"world reference axis did not create the orange manipulator");
        const auto originalFaceShape=v.boxes_[0].shape;
        require(v.previewCommand(15.0) && v.boxes_[0].shape.IsSame(originalFaceShape),"Rotate Face preview changed canonical geometry");
        v.cancelCommandSession(); idle();
        require(v.undoStack_.size()==rotationHistory && v.boxes_[0].shape.IsSame(originalFaceShape),"Rotate Face Cancel changed geometry or history");
        require(v.selectionContext().kind==CadViewport::SelectionContext::Kind::Faces,
                "Rotate Face Cancel did not restore passive Face selection mode");

        chooseFace(v,0,topFace(v.boxes_[0].shape));
        faceMenu.reset(v.createSelectionMenu(p0));
        action(*faceMenu,"context.rotate_face")->trigger(); v.chooseFaceRotationAxis(1);
        v.dragDimension_->setText("-15"); v.confirmCommandSession(); idle();
        require(v.undoStack_.size()==rotationHistory+1,"Rotate Face did not create one Undo step");
        v.undoPull(); idle(); require(v.undoStack_.size()==rotationHistory,"Rotate Face Undo failed");
        v.redoPull(); idle(); require(v.undoStack_.size()==rotationHistory+1,"Rotate Face Redo failed");
        require(v.boxes_[0].name==targetName && v.boxes_[0].color==targetColor,"Rotate Face changed target identity metadata");
        v.undoPull(); idle();

        chooseFace(v,0,topFace(v.boxes_[0].shape));
        faceMenu.reset(v.createSelectionMenu(p0));
        action(*faceMenu,"context.align_face")->trigger();
        require(v.alignFacePickingReference_ && v.commandSession_->kind==CadViewport::CommandSession::Kind::AlignFace,
                "Align Face did not start its separate reference-selection phase");
        require(v.faceAlignTargetLabel_->text().startsWith("Target:")
                && v.faceAlignReferenceLabel_->text().startsWith("Reference:"),"Align Face did not display separate operand roles");
        gp_Trsf elevated; elevated.SetTranslation(gp_Vec(0,0,100));
        v.boxes_[1].orientation=elevated; v.boxes_[1].presentation->SetLocalTransformation(elevated);
        v.context_->Redisplay(v.boxes_[1].presentation,Standard_False);
        const auto referenceShape=v.boxes_[1].shape;
        const auto referenceOrientation=v.boxes_[1].orientation;
        require(v.pickAlignReference(project(v,gp_Pnt(175,40,160))),"reference selection phase did not consume the pick");
        require(v.commandSession_->hasReference && v.commandSession_->valid,"Align Face did not produce a reference preview");
        require(v.faceAlignReferenceLabel_->text().contains(v.boxes_[1].name),"Align Face reference label was not updated");
        const auto originalAlignShape=v.boxes_[0].shape;
        require(v.boxes_[0].shape.IsSame(originalAlignShape),"Align Face preview changed canonical geometry");
        v.cancelCommandSession(); idle();
        require(v.boxes_[0].shape.IsSame(originalAlignShape),"Align Face Cancel changed target geometry");

        chooseFace(v,0,topFace(v.boxes_[0].shape));
        faceMenu.reset(v.createSelectionMenu(p0));
        action(*faceMenu,"context.align_face")->trigger();
        require(v.pickAlignReference(project(v,gp_Pnt(175,40,160))),"second reference selection was not consumed");
        require(v.commandSession_->hasReference && v.commandSession_->valid,"second Align Face preview failed");
        const auto alignHistory=v.undoStack_.size();
        v.confirmCommandSession(); idle();
        require(v.undoStack_.size()==alignHistory+1,"Align Face did not create one Undo step");
        bool referenceTransformUnchanged=true;
        for(int row=1;row<=3;++row) for(int column=1;column<=4;++column)
            if(std::abs(v.boxes_[1].orientation.Value(row,column)-referenceOrientation.Value(row,column))>1.e-8)
                referenceTransformUnchanged=false;
        require(v.boxes_[1].shape.IsSame(referenceShape) && referenceTransformUnchanged,
                "Align modified the reference Part");
        v.undoPull(); idle(); v.redoPull(); idle(); v.undoPull(); idle();
    }

};
void runNativeDocumentSessions();
void runPartlessDocuments();
void runConstructionSessions();
void runSketchSessions();
void runSketchDisplayBoundsSessions();
void runWorkplaneRendering();
void runSketchDrawing();
void runSketchOrthogonalLines();
void runSketchArcDirections();
void runSketchOverdrawSessions();
void runSketchOverdrawTraversal();
void runSketchConstructionSessions();
void runSketchReleaseSessions();
void runSketchPointSessions();
void runCommandPanelSessions();
void runSketchProjectionSessions();
void runProfileExtrusionSessions();
void runProfileRevolutionSessions();
void runActivePartSessions();
void runDeletePartSessions();
void runSketchEllipseSessions();
void runSketchEllipseDrawing();
int main(int argc,char** argv) {
#if defined(Q_OS_LINUX)
    if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && !qEnvironmentVariableIsEmpty("DISPLAY"))qputenv("QT_QPA_PLATFORM","xcb");
#endif
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv);
    try {
        if(app.arguments().contains("--delete-parts")) runDeletePartSessions();
        else if(app.arguments().contains("--active-part")) runActivePartSessions();
        else if(app.arguments().contains("--revolution")) runProfileRevolutionSessions();
        else if(app.arguments().contains("--extrusion")) runProfileExtrusionSessions();
        else if(app.arguments().contains("--workplane-rendering")) runWorkplaneRendering();
        else if(app.arguments().contains("--orthogonal-lines")) runSketchOrthogonalLines();
        else if(app.arguments().contains("--display-bounds")) runSketchDisplayBoundsSessions();
        else if(app.arguments().contains("--ellipse-drawing")) runSketchEllipseDrawing();
        else if(app.arguments().contains("--ellipses")) runSketchEllipseSessions();
        else if(app.arguments().contains("--projection")) runSketchProjectionSessions();
        else if(app.arguments().contains("--command-panels")) runCommandPanelSessions();
        else if(app.arguments().contains("--sketch-points")) runSketchPointSessions();
        else if(app.arguments().contains("--sketch-release")) runSketchReleaseSessions();
        else if(app.arguments().contains("--partless")) runPartlessDocuments();
        else if(app.arguments().contains("--arc-direction")) runSketchArcDirections();
        else if(app.arguments().contains("--overdraw-traversal")) runSketchOverdrawTraversal();
        else if(app.arguments().contains("--sketch-construction")) runSketchConstructionSessions();
        else if(app.arguments().contains("--overdraw")) runSketchOverdrawSessions();
        else if(app.arguments().contains("--drawing")) runSketchDrawing();
        else if(app.arguments().contains("--sketch")) runSketchSessions();
        else if(app.arguments().contains("--documents")) runNativeDocumentSessions();
        else if(app.arguments().contains("--construction")) runConstructionSessions();
        else { SelectionContextTest::run(); SelectionContextTest::historyDeselect(); }
        std::cout<<"Selection, command lifecycle and document checks passed.\n";return 0;
    }
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
