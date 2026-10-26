#include "CadViewport.h"
#include "MainWindow.h"
#include "SketchConstruction.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolBar>
#include <QTreeWidget>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void key(QWidget* w,int code) {QKeyEvent event(QEvent::KeyPress,code,Qt::NoModifier);QApplication::sendEvent(w,&event);}
void typeCoordinate(QDoubleSpinBox* field,double value) {
    auto* edit=field->findChild<QLineEdit*>();edit->setFocus();edit->selectAll();
    for(const auto c:field->locale().toString(value,'f',9)) {
        QKeyEvent event(QEvent::KeyPress,c.isDigit()?Qt::Key_0+c.digitValue():c=='-'?Qt::Key_Minus:Qt::Key_Period,Qt::NoModifier,QString(c));
        QApplication::sendEvent(edit,&event);
    }
}
}
struct SketchOrthogonalLineTest {
    static QPoint pixel(CadViewport& v,Sketch::Point p) {
        const auto world=Sketch::worldPoint(p,v.constructionObject(v.activeSketchWorkplaneId_)->frame);
        int x,y;v.view_->Convert(world.X(),world.Y(),world.Z(),x,y);return {x,y};
    }
    static void mouse(CadViewport& v,QPoint p,QEvent::Type type=QEvent::MouseMove) {
        const auto button=type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton;
        QMouseEvent event(type,QPointF(p),QPointF(v.mapToGlobal(p)),button,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(&v,&event);
    }
    static void line(const Sketch::Entity& e,bool horizontal,double position) {
        check(e.role==Sketch::Role::Construction && std::holds_alternative<Sketch::InfiniteLine>(e.geometry),"H/V line lost infinite Construction type");
        const auto& g=std::get<Sketch::InfiniteLine>(e.geometry);
        check(g.direction==(horizontal?Sketch::Point{1,0}:Sketch::Point{0,1}),"H/V direction is not exact local XY");
        check(g.origin==(horizontal?Sketch::Point{0,position}:Sketch::Point{position,0}),"wrong offset or irrelevant coordinate stored");
    }
    static void preview(CadViewport& v,const Construction::Object& wp,const Sketch::Entity& e) {
        check(v.sketchPreviews_.contains(e.id),"H/V live preview missing");
        const auto p=v.sketchPreviews_.at(e.id);check(v.context_->IsDisplayed(p),"H/V preview not displayed");
        check(p->Shape().ShapeType()==TopAbs_EDGE,"outside offset did not enlarge the frame");
        const auto r=v.workplaneDisplayRectangle(wp);
        const auto edge=TopoDS::Edge(p->Shape());
        for(const auto& vertex:{TopExp::FirstVertex(edge),TopExp::LastVertex(edge)}) {
            const gp_Vec delta(wp.frame.Location(),BRep_Tool::Pnt(vertex));
            const double x=delta.Dot(gp_Vec(wp.frame.XDirection())),y=delta.Dot(gp_Vec(wp.frame.YDirection()));
            check(std::min({std::abs(x-r.minX),std::abs(x-r.maxX),std::abs(y-r.minY),std::abs(y-r.maxY)})<1.e-7,"H/V preview does not reach the outer border");
        }
        Quantity_Color color;p->Color(color);check(color.Blue()>0.9 && color.Green()>0.5 && color.Red()<0.2,"preview is not blue");
    }
    static void run() {
        MainWindow w;w.resize(1400,1000);w.show();QApplication::processEvents();auto& v=*w.viewport_;QString error;
        auto* free=w.findChild<QAction*>("sketch.infinite");auto* h=w.findChild<QAction*>("sketch.infinite.horizontal");auto* vertical=w.findChild<QAction*>("sketch.infinite.vertical");
        check(free && h && vertical && !h->icon().isNull() && !vertical->icon().isNull(),"H/V actions or icons missing");
        bool menu=false,toolbar=false;
        for(auto* container:w.findChildren<QWidget*>()) if(qobject_cast<QMenu*>(container) || qobject_cast<QToolBar*>(container)) {
            const auto actions=container->actions();const int index=actions.indexOf(free);
            if(index<0) continue;
            check(actions.value(index+1)==h && actions.value(index+2)==vertical,"H/V actions not next to free Infinite Line");
            if(qobject_cast<QMenu*>(container)) menu=true;else toolbar=true;
        }
        check(menu && toolbar,"H/V tools missing from menu or toolbar");
        for(bool horizontal:{true,false}) {
            NativeDocumentSnapshot base;Construction::Object wp;wp.id="wp1";wp.name="WP1";wp.kind=Construction::Kind::Workplane;
            wp.frame=gp_Ax3(gp_Pnt(20,-30,70),gp_Dir(1,2,3),gp_Dir(2,-1,0));
            auto other=wp;other.id="wp2";other.name="WP2";other.frame.SetLocation(gp_Pnt(-100,150,0));base.constructions={wp,other};
            Sketch::Entity anchor;anchor.workplaneId=wp.id;anchor.role=Sketch::Role::Construction;
            const Sketch::Point target=horizontal?Sketch::Point{1000,35}:Sketch::Point{-120,1000};anchor.geometry=Sketch::ConstructionPoint{target};
            check(v.importDocumentSnapshot(base,error),qPrintable(error));check(v.activateSketchWorkplane(wp.id),"empty Workplane activation failed");
            v.startSketchDrawing(horizontal?Sketch::DrawTool::HorizontalInfiniteLine:Sketch::DrawTool::VerticalInfiniteLine);
            (horizontal?v.sketchDrawingPanel_->y:v.sketchDrawingPanel_->x)->setValue(200);
            preview(v,wp,v.commandSession_->drawing->candidate);
            check(v.sketchEntities_.empty() && v.boxes_.empty(),"H/V tool needs or creates auxiliary geometry");v.cancelCommandSession();
            base.sketchEntities={anchor};
            check(v.importDocumentSnapshot(base,error),qPrintable(error));check(v.activateSketchWorkplane(wp.id),"Workplane activation failed");
            v.setSketchSnapPixels(12);v.setSketchSnapping(true);w.documentDirty_=false;
            auto* action=horizontal?h:vertical;action->trigger();auto* panel=v.sketchDrawingPanel_;
            check(panel && panel->role->count()==1 && panel->role->currentData().toInt()==int(Sketch::Role::Construction),"H/V offers Profile role");
            check((horizontal?panel->y:panel->x)->isVisible() && !(horizontal?panel->x:panel->y)->isVisible()
                && !panel->distance->isVisible() && !panel->angle->isVisible() && !panel->polarButton->isVisible(),"H/V editor requires extra coordinates/length/angle");
            (horizontal?panel->y:panel->x)->setFocus();
            const auto at=pixel(v,target)+QPoint(3,2);mouse(v,at);
            check(v.commandSession_->drawing->snap.has_value(),"H/V did not use existing pixel snapping");
            line(v.commandSession_->drawing->candidate,horizontal,horizontal?35:-120);preview(v,wp,v.commandSession_->drawing->candidate);
            check(!v.commandSession_->drawing->complete && v.sketchEntities_==base.sketchEntities && !w.documentDirty_,"hover preview committed geometry");
            mouse(v,at,QEvent::MouseButtonPress);mouse(v,at,QEvent::MouseButtonRelease);
            check(v.commandSession_->valid && v.commandSession_->drawing->complete && v.commandSession_->drawing->points.size()==1,"H/V needs more than one click");
            check((horizontal?panel->y:panel->x)->value()==(horizontal?35:-120),"mouse pick left a stale numeric offset");
            const auto frozen=v.commandSession_->drawing->candidate;mouse(v,pixel(v,{30,40}));check(v.commandSession_->drawing->candidate==frozen,"mouse moved accepted offset");
            panel->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            check(v.sketchEntities_.size()==2 && v.undoStack_.size()==1,"mouse H/V not one Undo transaction");line(v.sketchEntities_.back(),horizontal,horizontal?35:-120);
            const auto confirmed=v.sketchEntities_;v.selectSketchEntities({confirmed.back().id});v.undoPull();check(v.sketchEntities_==base.sketchEntities && v.sketchSelection().empty(),"H/V Undo failed");
            const auto redo=v.redoStack_.size();w.documentDirty_=false;
            const auto priorFrame=v.workplaneDisplayRectangles_.at(wp.id);
            action->trigger();panel=v.sketchDrawingPanel_;auto* coordinate=horizontal?panel->y:panel->x;
            const double offset=horizontal?350.123456789:-420.987654321;coordinate->setValue(offset);
            line(v.commandSession_->drawing->candidate,horizontal,offset);preview(v,wp,v.commandSession_->drawing->candidate);
            check(v.workplaneDisplayRectangles_.at(wp.id)!=priorFrame,"outside H/V preview did not refresh displayed frame");
            check(v.undoStack_.empty() && v.redoStack_.size()==redo && !w.documentDirty_,"numeric preview changed history/document");
            NativeDocumentSnapshot pending;check(v.exportDocumentSnapshot(pending) && pending.sketchEntities==base.sketchEntities,"Save included numeric preview");
            key(coordinate,Qt::Key_Escape);check(!v.commandSession_ && v.sketchPreviews_.empty() && v.redoStack_.size()==redo && !w.documentDirty_,"H/V Cancel changed geometry/history");
            check(v.workplaneDisplayRectangles_.at(wp.id)==priorFrame,"H/V Cancel kept enlarged frame");
            v.redoPull();check(v.sketchEntities_==confirmed && v.context_->NbSelected()==0,"H/V Redo lost values/selection");
            // Context menu uses the same tool, including numeric Enter and edits.
            std::unique_ptr<QMenu> context(v.createSelectionMenu(QPoint(-10,-10)));
            auto* contextAction=context->findChild<QAction*>(horizontal?"context.infinite.horizontal":"context.infinite.vertical");
            check(contextAction && !contextAction->icon().isNull(),"H/V context action missing");contextAction->trigger();
            panel=v.sketchDrawingPanel_;coordinate=horizontal?panel->y:panel->x;coordinate->setValue(offset);
            line(v.commandSession_->drawing->candidate,horizontal,offset);preview(v,wp,v.commandSession_->drawing->candidate);
            const double edited=horizontal?35.123456789:-120.123456789;
            typeCoordinate(coordinate,edited);line(v.commandSession_->drawing->candidate,horizontal,edited);key(coordinate,Qt::Key_Return);
            check(!v.commandSession_ && v.undoStack_.size()==2,"numeric Enter did not confirm one operation");line(v.sketchEntities_.back(),horizontal,edited);
            // Zero is a valid numeric offset, even when its field value never changes.
            action->trigger();panel=v.sketchDrawingPanel_;coordinate=horizontal?panel->y:panel->x;key(coordinate,Qt::Key_Return);
            check(!v.commandSession_ && v.undoStack_.size()==3,"zero offset requires an extra point/Enter");line(v.sketchEntities_.back(),horizontal,0);
            const auto axesBefore=v.sketchEntities_.size();v.startSketchAxes(horizontal,!horizontal);check(!v.commandSession_ && v.sketchEntities_.size()==axesBefore,"Construction Axes duplicated the new zero-offset line");
            NativeDocumentSnapshot saved,loaded;check(v.exportDocumentSnapshot(saved),"H/V snapshot export failed");
            QTemporaryDir dir;check(dir.isValid(),"temporary directory failed");const auto path=dir.filePath("orthogonal.odc");
            check(NativeDocumentStorage::save(path,saved,error) && NativeDocumentStorage::load(path,loaded,error),qPrintable(error));
            check(v.importDocumentSnapshot(loaded,error),qPrintable(error));check(v.sketchEntities_==saved.sketchEntities && v.undoStack_.empty(),"H/V Save/Open changed geometry/history");
            check(Construction::equivalent(*v.constructionObject(wp.id),wp) && v.workplaneDisplayRectangle(other)==Sketch::displayOuterRectangle(other,{}),"H/V moved owner or affected other Workplane");
            // Existing free line remains a two-point, arbitrary-angle operation.
            v.activateSketchWorkplane(wp.id);free->trigger();v.acceptSketchPoint({2,3});check(!v.commandSession_->drawing->complete,"free Infinite Line became one-click");
            v.acceptSketchPoint({5,7});check(v.commandSession_->valid,"free Infinite Line no longer works");
            const auto g=std::get<Sketch::InfiniteLine>(v.commandSession_->drawing->candidate.geometry);check(g.origin==Sketch::Point{2,3} && g.direction==Sketch::Point{0.6,0.8},"free line direction constrained");v.cancelCommandSession();
            // Direct API requests cannot turn orthogonal lines into Profile geometry.
            v.startSketchDrawing(horizontal?Sketch::DrawTool::HorizontalInfiniteLine:Sketch::DrawTool::VerticalInfiniteLine,Sketch::Role::Profile);
            v.acceptSketchPoint({9,8});line(v.commandSession_->drawing->candidate,horizontal,horizontal?8:9);v.activateSketchWorkplane(other.id);
            check(!v.commandSession_ && v.sketchEntities_==saved.sketchEntities,"Workplane switch committed preview");
        }
        w.documentDirty_=false;w.close();QApplication::processEvents();
    }
};
void runSketchOrthogonalLines() {SketchOrthogonalLineTest::run();}
