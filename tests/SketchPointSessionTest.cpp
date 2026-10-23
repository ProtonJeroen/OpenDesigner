#include "CadViewport.h"
#include "MainWindow.h"
#include "ConstructionPanel.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <Image_AlienPixMap.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_PointAspect.hxx>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void key(QWidget& w,int code) {QKeyEvent e(QEvent::KeyPress,code,Qt::NoModifier);QApplication::sendEvent(&w,&e);}
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
struct SketchPointSessionTest {
    static QPoint pixel(CadViewport& v,Sketch::Point p) {
        const auto world=Sketch::worldPoint(p,v.constructionObject(v.activeSketchWorkplaneId_)->frame);
        int x,y;v.view_->Convert(world.X(),world.Y(),world.Z(),x,y);return QPoint(x,y);
    }
    static void click(CadViewport& v,QPoint p) {
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(p),QPointF(v.mapToGlobal(p)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(p),QPointF(v.mapToGlobal(p)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(&v,&press);QApplication::sendEvent(&v,&release);
    }
    static void marker(CadViewport& v,const Sketch::Entity& point) {
        v.selectSketchEntities({});const auto* owner=v.constructionObject(point.workplaneId);
        v.context_->Erase(v.constructionPresentations_.at(owner->id),Standard_False);
        const auto position=std::get<Sketch::ConstructionPoint>(point.geometry).position;
        const auto world=Sketch::worldPoint(position,owner->frame);const auto camera=v.view_->Camera();
        camera->SetEyeAndCenter(world.Translated(gp_Vec(owner->frame.Direction())*500),world);camera->SetUp(owner->frame.YDirection());
        std::pair<int,int> previous{};
        for(double scale:{50.0,200.0,800.0}) {
            camera->SetScale(scale);v.view_->ZFitAll();v.view_->Redraw();
            Image_AlienPixMap image;check(v.view_->ToPixMap(image,v.width(),v.height()),"point marker render failed");
            const auto at=pixel(v,position);int minX=10000,minY=10000,maxX=-1,maxY=-1,count=0;
            for(int y=std::max(0,at.y()-30);y<std::min(int(image.SizeY()),at.y()+31);++y)
                for(int x=std::max(0,at.x()-30);x<std::min(int(image.SizeX()),at.x()+31);++x) {
                    const auto c=image.PixelColor(x,y).GetRGB();
                    if(c.Red()>0.7 && c.Blue()>c.Green()+0.1 && c.Red()>c.Green()+0.2) {++count;minX=std::min(minX,x);minY=std::min(minY,y);maxX=std::max(maxX,x);maxY=std::max(maxY,y);}
                }
            check(count>=5,"confirmed point marker is not visibly rendered");const std::pair size{maxX-minX+1,maxY-minY+1};
            if(previous.first) check(std::abs(size.first-previous.first)<=1 && std::abs(size.second-previous.second)<=1,"marker size varies with camera zoom");
            previous=size;
        }
        v.context_->Display(v.constructionPresentations_.at(owner->id),Standard_False);v.lookNormalToSketch();
    }
    static void run() {
        MainWindow w;w.show();QApplication::processEvents();auto& v=*w.viewport_;QString error;
        v.startConstruction(Construction::Method::PlaneXYZ);v.confirmCommandSession();
        auto owner=v.constructions_.front();owner.frame=gp_Ax3(gp_Pnt(10,20,30),gp_Dir(1,2,3),gp_Dir(2,-1,0));check(v.updateConstruction(owner),"rotated owner setup failed");
        check(v.activateSketchWorkplane(owner.id),"cannot edit owner");
        Sketch::Entity point;point.workplaneId=owner.id;point.role=Sketch::Role::Construction;
        const Sketch::Point exact{12.345678901234567,-7.987654321012345};point.geometry=Sketch::ConstructionPoint{exact};
        const auto history=v.undoStack_.size();w.documentDirty_=false;
        check(v.previewSketchEntities({point},error),qPrintable(error));
        check(v.sketchEntities_.empty() && !w.documentDirty_ && v.sketchPreviews_.at(point.id)->Shape().ShapeType()==TopAbs_VERTEX,"point preview changed model or is not a vertex");
        key(v,Qt::Key_Escape);check(v.sketchEntities_.empty() && v.sketchPreviews_.empty() && v.undoStack_.size()==history && !w.documentDirty_,"point Cancel changed document/history");
        check(v.previewSketchEntities({point},error),qPrintable(error));key(v,Qt::Key_Return);
        check(v.sketchEntities_==std::vector{point} && v.undoStack_.size()==history+1 && w.documentDirty_,"point commit not one transaction");
        check(w.constructionRoot_->child(0)->child(0)->text(0)=="Construction Point","point browser type incorrect");
        marker(v,point);click(v,pixel(v,exact));
        check(v.sketchSelection()==QStringList{point.id} && v.selectionContext().kind==CadViewport::SelectionContext::Kind::Sketches,"point viewport pick became a Face/Part or failed");
        v.undoPull();check(v.sketchEntities_.empty() && v.sketchSelection().empty() && w.structure_->selectedItems().empty(),"point Undo retained geometry/selection");
        v.redoPull();check(v.sketchEntities_==std::vector{point} && v.context_->NbSelected()==0,"point Redo lost stable values/selection");
        auto updated=point;updated.name="Named point";check(v.updateSketchEntity(updated,error),qPrintable(error));point=updated;
        auto* row=w.constructionRoot_->child(0)->child(0);row->setSelected(true);QApplication::processEvents();
        check(v.sketchSelection()==QStringList{point.id},"point browser selection failed");
        updated.visible=false;check(v.updateSketchEntity(updated,error),qPrintable(error));
        check(!v.context_->IsDisplayed(v.sketchPresentations_.at(point.id)) && !v.sketchPickable(updated),"hidden point remains pickable/visible");v.undoPull();
        updated=point;updated.selectable=false;check(v.updateSketchEntity(updated,error),qPrintable(error));v.selectSketchEntities({point.id});
        check(v.context_->NbSelected()==0 && !v.sketchPickable(updated),"locked point entered OCCT selection");v.undoPull();
        auto hiddenOwner=owner;hiddenOwner.visible=false;check(v.updateConstruction(hiddenOwner),"hide owner failed");
        check(!v.context_->IsDisplayed(v.sketchPresentations_.at(point.id)),"hidden owner leaked point");v.undoPull();
        auto lockedOwner=owner;lockedOwner.selectable=false;check(v.updateConstruction(lockedOwner),"lock owner failed");v.selectSketchEntities({point.id});check(v.context_->NbSelected()==0,"locked owner allowed point pick");v.undoPull();
        auto moved=owner;gp_Trsf t;t.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,0,0)),0.7);t.SetTranslationPart(gp_Vec(15,-30,40));moved.frame.Transform(t);
        check(v.updateConstruction(moved),"move/rotate owner failed");
        check(v.sketchEntities_==std::vector{point} && BRep_Tool::Pnt(TopoDS::Vertex(v.sketchPresentations_.at(point.id)->Shape())).IsEqual(Sketch::worldPoint(exact,moved.frame),1.e-12),"owner transform rewrote local point or lost display placement");v.undoPull();
        check(v.activateSketchWorkplane(owner.id),"cannot reactivate owner");
        // Curve-only tools must not accept points as references or boundaries.
        v.startSketchOverdraw();check(!v.commandSession_->valid && !v.commandSession_->overdraw->error.isEmpty(),"point became Overdraw source");v.cancelCommandSession();
        for(auto tool:{Sketch::ConstructionTool::OffsetLine,Sketch::ConstructionTool::CommonTangent}) {
            v.startSketchConstruction(tool);check(!v.commandSession_->valid && v.sketchConstructionPanel_->references[0]->count()==1,"point offered as curve reference");
            v.selectSketchEntities({point.id});check(v.commandSession_ && !v.commandSession_->valid,"point reference pick ended/validated curve tool");v.cancelCommandSession();
        }
        for(double zoom:{120.0,500.0}) {
            v.view_->Camera()->SetScale(zoom);v.startSketchDrawing(Sketch::DrawTool::Segment);
            v.sketchDrawingMove(pixel(v,exact)+QPoint(4,0),Qt::NoModifier);
            check(v.commandSession_->drawing->snap && v.commandSession_->drawing->snap->kind==Sketch::SnapKind::ConstructionPoint
                && v.commandSession_->drawing->cursor==exact,"GUI point snap lost exact coordinates or zoom radius");v.cancelCommandSession();
        }
        v.startSketchDrawing(Sketch::DrawTool::Segment);v.sketchDrawingClick(pixel(v,exact)+QPoint(4,0),Qt::NoModifier);
        v.acceptSketchPoint({exact.x+15,exact.y+5});v.confirmCommandSession();
        check(std::get<Sketch::Segment>(v.sketchEntities_.back().geometry).start==exact,"drawing did not store exact snapped point");const auto line=v.sketchEntities_.back();
        check(v.deleteSketchEntities({point.id},error) && v.sketchEntities_==std::vector{line},"point deletion removed dependent line");v.undoPull();
        const auto confirmed=v.sketchEntities_;v.selectSketchEntities({point.id});
        QTemporaryDir dir;const auto path=dir.filePath("points.odc");w.documentPath_=path;
        const auto undo=v.undoStack_.size(),redo=v.redoStack_.size();check(w.saveDocument(),"point GUI Save failed");
        chooseFile(dir.filePath("copy.odc"));check(w.saveDocumentAs(),"point GUI Save As failed");
        check(v.undoStack_.size()==undo && v.redoStack_.size()==redo && v.sketchSelection()==QStringList{point.id},"point Save/Save As cleared history/selection");
        auto pending=point;pending.id="pending-point";pending.geometry=Sketch::ConstructionPoint{{77,88}};auto candidates=confirmed;candidates.push_back(pending);
        check(v.previewSketchEntities(candidates,error),qPrintable(error));check(w.saveDocument() && v.commandSession_,"point preview Save lost command");
        NativeDocumentSnapshot saved;check(NativeDocumentStorage::load(w.documentPath_,saved,error) && saved.sketchEntities==confirmed,"Save included pending point");
        auto invalid=saved;invalid.schemaVersion=3;check(!v.importDocumentSnapshot(invalid,error) && v.commandSession_ && v.sketchEntities_==confirmed,"v3 point import damaged active document");
        invalid=saved;invalid.schemaVersion=99;check(!v.importDocumentSnapshot(invalid,error) && v.sketchEntities_==confirmed,"future import damaged point document");
        chooseFile(w.documentPath_);w.openDocument();
        check(v.sketchEntities_==confirmed && v.undoStack_.empty() && v.redoStack_.empty() && v.sketchSelection().empty() && !v.commandSession_ && v.activeSketchWorkplaneId_.isEmpty()
            && w.constructionRoot_->child(0)->childCount()==2,"point Open lost data or retained session/history/selection");
        v.addBox();v.beginCommandSession(CadViewport::CommandSession::Kind::Move,{0},gp_Dir(1,0,0));check(v.previewCommand(12),"3D Move preview failed");v.confirmCommandSession();v.undoPull();v.redoPull();
        check(v.sketchEntities_==confirmed,"3D history disturbed points");
        const auto deleteHistory=v.undoStack_.size();v.deleteConstructions({owner.id});check(v.sketchEntities_.empty() && v.undoStack_.size()==deleteHistory+1,"owner deletion did not include point");
        v.undoPull();check(v.sketchEntities_==confirmed,"owner Undo did not restore point");
        check(v.activateSketchWorkplane(owner.id),"cannot edit reopened owner");check(v.previewSketchEntities(candidates,error),qPrintable(error));
        w.documentDirty_=false;w.newDocument();check(v.sketchEntities_.empty() && v.sketchPresentations_.empty() && v.sketchPreviews_.empty() && !v.commandSession_ && v.undoStack_.empty(),"New retained points or pending presentations");w.documentDirty_=false;
    }
};
void runSketchPointSessions() {SketchPointSessionTest::run();}
