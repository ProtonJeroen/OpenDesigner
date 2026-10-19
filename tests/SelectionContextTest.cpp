#include "CadViewport.h"
#include "MainWindow.h"
#include <QApplication>
#include <QComboBox>
#include <QMenu>
#include <QAction>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QTreeWidget>
#include <QLineEdit>
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
    static void run() {
        MainWindow window; window.show(); QApplication::processEvents();
        auto& v=*window.findChild<CadViewport*>();
        auto* tree=window.findChild<QTreeWidget*>(); auto* root=tree->topLevelItem(0);
        require(!window.findChild<QComboBox*>("selectionMode"),"obsolete selection dropdown remains");
        v.addBox(); v.addBox(); v.fitAll();
        const auto p0=project(v,gp_Pnt(50,40,60)),p1=project(v,gp_Pnt(175,40,60));
        const auto idle=[&]() {
            require(!v.commandSession_ && !v.moveFacePicking_ && !v.pullMode_ && !v.movePartMode_ && !v.directMoveMode_ && !v.rotateMode_,"command remained active");
            require(!v.arrowVisible_ && v.moveFaceArrow_.IsNull() && v.moveAxes_[0].IsNull() && v.rotationRings_[0].IsNull(),"orphan manipulators remain");
            require(!v.commandPanel_->isVisible() && !v.dragDimension_->isVisible(),"orphan command UI remains");
        };
        v.setDirectMoveMode(true); idle(); v.setRotateMode(true); idle();
        click(v,p0); idle(); require(v.selectionContext().kind==CadViewport::SelectionContext::Kind::Faces,"default click not Face");
        click(v,p1,Qt::ControlModifier); idle(); require(v.selectionContext().faces.size()==2,"Ctrl did not add Face");
        click(v,p0,Qt::AltModifier); click(v,p1,Qt::AltModifier|Qt::ControlModifier); idle();
        require(v.selectedPartIndices_==std::vector<size_t>({0,1}),"Alt/Ctrl+Alt Part selection failed");
        const auto selected=owners(v);
        QKeyEvent altRelease(QEvent::KeyRelease,Qt::Key_Alt,Qt::NoModifier); QApplication::sendEvent(&v,&altRelease);
        require(owners(v)==selected && popup(v,p0) && owners(v)==selected,"Alt release/right click changed Part selection");
        require(!popup(v,QPoint(-20,-20)),"background opened unrelated menu");
        std::unique_ptr<QMenu> parts(v.createSelectionMenu(p0)); action(*parts,"context.move_part")->trigger();
        require(v.directMoveMode_ && !v.moveAxes_[0].IsNull() && v.rotationRings_[0].IsNull(),"explicit Move activation failed");
        QKeyEvent emptyConfirm(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier); QApplication::sendEvent(&v,&emptyConfirm); idle();
        action(*parts,"context.move_part")->trigger();
        const auto before=v.boxes_[0].orientation; const auto history=v.undoStack_.size();
        v.beginCommandSession(CadViewport::CommandSession::Kind::Move,{0,1},gp_Dir(1,0,0)); v.previewCommand(12);
        v.dragDimension_->setText("12"); v.confirmCommandSession(); idle();
        require(v.undoStack_.size()==history+1 && v.selectedPartIndices_==std::vector<size_t>({0,1}),"Move confirm/history/selection failed");
        v.undoPull(); idle();
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
        v.undoPull(); idle(); require(v.boxes_[0].shape.IsSame(groupShape0) && v.boxes_[1].shape.IsSame(groupShape1) && v.selectedPartIndices_.size()==2,"group Pull Undo failed");
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
    }

};
int main(int argc,char** argv) {
#if defined(Q_OS_LINUX)
    if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && !qEnvironmentVariableIsEmpty("DISPLAY"))qputenv("QT_QPA_PLATFORM","xcb");
#endif
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv);
    try {SelectionContextTest::run();std::cout<<"Selection modes, context menus, shared commands and Boolean Move Face passed.\n";return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
