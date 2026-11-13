#include "CadViewport.h"
#include "MainWindow.h"
#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QTemporaryDir>
#include <BRepAdaptor_Curve.hxx>
#include <Image_AlienPixMap.hxx>
#include <TopoDS.hxx>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace {
void check(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
void key(QWidget* w,int code) {QKeyEvent event(QEvent::KeyPress,code,Qt::NoModifier);QApplication::sendEvent(w,&event);}
}
struct SketchExtendSessionTest {
    static NativeDocumentSnapshot fixture(Sketch::Geometry source=Sketch::Segment{{-60,0},{0,0}}) {
        NativeDocumentSnapshot snapshot;Construction::Object wp;wp.id="wp";wp.name="Extend Workplane";wp.kind=Construction::Kind::Workplane;wp.extent=100;
        wp.frame=gp_Ax3(gp_Pnt(10,20,30),gp_Dir(1,2,3),gp_Dir(2,-1,0));snapshot.constructions={wp};
        Sketch::Entity target;target.id="target";target.name="Named source";target.workplaneId="wp";target.geometry=source;
        auto boundary=target;boundary.id="boundary";boundary.role=Sketch::Role::Construction;boundary.geometry=Sketch::Segment{{40,-60},{40,60}};
        auto decoy=boundary;decoy.id="nearer";decoy.geometry=Sketch::Segment{{20,-60},{20,60}};
        snapshot.sketchEntities={target,boundary,decoy};return snapshot;
    }
    static void import(MainWindow& w,const NativeDocumentSnapshot& snapshot) {
        auto& v=*w.viewport_;QString error;check(v.importDocumentSnapshot(snapshot,error),qPrintable(error));
        check(v.activateSketchWorkplane("wp") && v.lookAtWorkplane("wp"),"transformed Workplane activation failed");QApplication::processEvents();w.documentDirty_=false;
    }
    static void start(MainWindow& w) {
        auto* action=w.findChild<QAction*>("sketch.extend");check(action && action->isEnabled(),"Extend action unavailable");action->trigger();QApplication::processEvents();
        check(w.viewport_->commandSession_ && w.viewport_->commandSession_->extend && w.viewport_->commandPanel_->isVisible(),"Extend did not use the existing Sketch session/panel");
    }
    static void click(CadViewport& v,Sketch::Point p) {
        const auto world=Sketch::worldPoint(p,v.constructionObject("wp")->frame);int x,y;v.view_->Convert(world.X(),world.Y(),world.Z(),x,y);
        const QPoint at(x,y);QMouseEvent move(QEvent::MouseMove,QPointF(at),QPointF(v.mapToGlobal(at)),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&v,&move);
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(at),QPointF(v.mapToGlobal(at)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&v,&press);
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(at),QPointF(v.mapToGlobal(at)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&v,&release);
    }
    static void select(MainWindow& w) {start(w);click(*w.viewport_,{0,0});click(*w.viewport_,{40,30});}
    static void blue(CadViewport& v) {
        Image_AlienPixMap image;check(v.view_->ToPixMap(image,800,600),"Extend preview cannot render");int blue=0;
        for(int y=0;y<600;++y) for(int x=0;x<800;++x) {const auto c=image.PixelColor(x,y).GetRGB();if(c.Blue()>c.Red()+.3 && c.Blue()>c.Green()+.05 && c.Green()>c.Red()+.1) ++blue;}
        check(blue>30,"blue Extend preview is invisible");
    }
    static void run() {
        MainWindow w;w.resize(1400,1000);w.show();QApplication::processEvents();auto& v=*w.viewport_;QString error;
        if(qgetenv("QT_SCALE_FACTOR")=="2") check(v.devicePixelRatioF()>1.9,"HiDPI test did not enable scaling");
        check(!w.findChild<QAction*>("sketch.extend")->isEnabled(),"Extend enabled without Workplane");v.startSketchExtend();check(!v.commandSession_,"Extend implicitly activated Workplane");
        const auto initial=fixture();import(w,initial);const auto history=v.undoStack_.size();
        start(w);check(!v.commandButtons_->button(QDialogButtonBox::Ok)->isEnabled(),"Extend enabled OK without inputs");
        click(v,{-30,0});check(v.commandSession_->extend->sourceId.isEmpty() && !v.commandSession_->valid,"middle pick silently chose an extension side");
        click(v,{0,0});check(v.commandSession_->extend->sourceId=="target" && !v.commandSession_->extend->atStart && v.sketchPreviews_.empty(),"endpoint pick did not arm boundary selection");
        key(v.commandPanel_,Qt::Key_Return);check(v.commandSession_ && v.sketchEntities_==initial.sketchEntities,"missing boundary confirmation changed geometry");
        NativeDocumentSnapshot pending;check(v.exportDocumentSnapshot(pending) && pending.sketchEntities==initial.sketchEntities,"Save captured endpoint selection");
        click(v,{40,30});check(v.commandSession_ && v.commandSession_->valid,"boundary click did not produce Extend preview");
        const auto candidate=v.commandSession_->sketch->after;check(candidate.size()==initial.sketchEntities.size() && candidate[1]==initial.sketchEntities[1] && candidate[2]==initial.sketchEntities[2],"Extend changed a boundary or created duplicate entities");
        check(candidate[0].id=="target" && candidate[0].name=="Named source" && candidate[0].role==Sketch::Role::Profile,"Extend changed source identity or metadata");
        const auto line=std::get<Sketch::Segment>(candidate[0].geometry);check(std::abs(line.start.x+60)<1.e-9 && std::abs(line.end.x-40)<1.e-9,"Extend chose the nearer unselected boundary");
        check(v.sketchEntities_==initial.sketchEntities && v.undoStack_.size()==history && !w.documentDirty_,"Extend preview modified canonical geometry/history");
        check(v.sketchExtendPrompt_->text().contains("open"),"open Sketch result was presented as certified closed Profile");
        const auto& frame=v.constructionObject("wp")->frame;BRepAdaptor_Curve curve(TopoDS::Edge(v.sketchPreviews_.at("target")->Shape()));
        check(curve.Value(curve.LastParameter()).Distance(Sketch::worldPoint(line.end,frame))<1.e-7,"Extend preview lost Workplane transform");blue(v);
        click(v,{20,30});check(v.commandSession_->sketch->after==candidate,"second preview click chained an Extend");
        check(v.exportDocumentSnapshot(pending) && pending.sketchEntities==initial.sketchEntities,"Save captured unconfirmed extension");
        key(v.commandPanel_,Qt::Key_Escape);check(!v.commandSession_ && v.sketchPreviews_.empty() && v.sketchEntities_==initial.sketchEntities && !w.documentDirty_,"Escape failed to restore original geometry");
        select(w);key(v.commandPanel_,Qt::Key_Return);const auto confirmed=v.sketchEntities_;
        check(!v.commandSession_ && v.undoStack_.size()==history+1 && confirmed[0].id=="target" && w.documentDirty_,"Extend was not one existing SketchChange");
        v.undoPull();check(v.sketchEntities_==initial.sketchEntities,"Extend Undo changed original");const auto redo=v.redoStack_.size();
        select(w);v.commandButtons_->button(QDialogButtonBox::Cancel)->click();check(v.redoStack_.size()==redo && v.sketchEntities_==initial.sketchEntities,"Cancel changed geometry or Redo history");
        v.redoPull();check(v.sketchEntities_==confirmed,"Extend Redo changed values/IDs");
        QTemporaryDir temp;NativeDocumentSnapshot saved,loaded;check(v.exportDocumentSnapshot(saved),"snapshot failed");
        check(NativeDocumentStorage::save(temp.filePath("extend.odc"),saved,error) && NativeDocumentStorage::load(temp.filePath("extend.odc"),loaded,error) && loaded.sketchEntities==confirmed,"Extend Save/Open changed exact values");
        auto reversed=fixture(Sketch::Segment{{0,0},{-60,0}});reversed.sketchEntities[0].role=Sketch::Role::Construction;import(w,reversed);select(w);
        check(v.commandSession_->valid && v.commandSession_->extend->atStart,"reversed start endpoint selection failed");v.commandButtons_->button(QDialogButtonBox::Ok)->click();
        check(std::get<Sketch::Segment>(v.sketchEntities_[0].geometry).start.x==40 && v.sketchEntities_[0].role==Sketch::Role::Construction,"reversed Extend lost orientation or role");
        constexpr double pi=std::numbers::pi;
        auto elliptic=fixture(Sketch::EllipticalArc{Sketch::Ellipse{{0,0},50,30,0},0,pi/4});elliptic.sketchEntities.resize(2);elliptic.sketchEntities[1].geometry=Sketch::Segment{{25,10},{25,60}};
        import(w,elliptic);start(w);click(v,Sketch::ellipsePoint({{0,0},50,30,0},pi/4));click(v,{25,50});
        check(v.commandSession_->valid,"elliptical Extend session failed");const auto ea=std::get<Sketch::EllipticalArc>(v.commandSession_->sketch->after[0].geometry);check(std::abs(ea.sweepParameter-pi/3)<1.e-9,"elliptical Extend changed route");v.cancelCommandSession();
        Sketch::BSpline bs;bs.degree=2;bs.poles={{-60,0},{0,60},{60,0}};bs.weights={1,1,1};bs.knots={0,1};bs.multiplicities={3,3};bs.first=.25;bs.last=.5;
        import(w,fixture(bs));start(w);click(v,{0,30});click(v,{40,30});check(v.commandSession_->valid,"coefficient-domain Extend session failed");
        const auto exact=std::get<Sketch::BSpline>(v.commandSession_->sketch->after[0].geometry);check(exact.poles==bs.poles && exact.weights==bs.weights && std::abs(exact.last-5./6)<1.e-9,"coefficient Extend invented support");v.cancelCommandSession();
        auto ambiguous=fixture();ambiguous.sketchEntities.resize(2);ambiguous.sketchEntities[1].geometry=Sketch::Circle{{80,0},10};import(w,ambiguous);start(w);click(v,{0,0});click(v,{80,10});
        check(!v.commandSession_->valid && v.sketchPreviews_.empty() && v.sketchEntities_==ambiguous.sketchEntities && v.sketchExtendPrompt_->text().contains("Several crossings"),"ambiguous boundary silently chose a target");v.cancelCommandSession();
        import(w,fixture(Sketch::Spline{{{-60,0},{0,40},{60,0}}}));start(w);click(v,{60,0});check(v.commandSession_->extend->sourceId.isEmpty() && v.sketchExtendPrompt_->text().contains("no certified continuation"),"interpolating spline was extrapolated");v.cancelCommandSession();
        auto locked=fixture();locked.sketchEntities[0].selectable=false;import(w,locked);start(w);click(v,{0,0});check(v.commandSession_->extend->sourceId.isEmpty(),"locked source selected");v.cancelCommandSession();
        for(bool hide:{false,true}) {
            auto unavailable=fixture();if(hide) unavailable.sketchEntities[1].visible=false;else unavailable.sketchEntities[1].selectable=false;
            import(w,unavailable);select(w);check(!v.commandSession_->valid && v.sketchEntities_==unavailable.sketchEntities,"hidden/locked boundary extended source");v.cancelCommandSession();
        }
        auto other=fixture();auto wp=other.constructions[0];wp.id="other";other.constructions.push_back(wp);other.sketchEntities[1].workplaneId="other";
        import(w,other);select(w);check(!v.commandSession_->valid,"other Workplane boundary accepted");v.cancelCommandSession();
        import(w,initial);start(w);click(v,{0,0});auto stale=initial.sketchEntities[1];stale.visible=false;check(v.updateSketchEntity(stale,error),qPrintable(error));check(!v.commandSession_ && v.sketchEntities_[0]==initial.sketchEntities[0],"stale boundary retained an Extend session or changed source");
        import(w,initial);select(w);v.setShowWorkplanes(false);check(!v.commandSession_ && v.sketchEntities_==initial.sketchEntities,"Hide Workplanes committed pending Extend");
        check(v.activateSketchWorkplane("wp"),"reactivation failed");select(w);v.startSketchDrawing(Sketch::DrawTool::Segment);check(v.commandSession_ && !v.commandSession_->extend && v.sketchEntities_==initial.sketchEntities,"tool switch committed Extend");v.cancelCommandSession();
        check(!v.sketchExtendPrompt_->isVisible(),"Extend prompt leaked into later commands");w.documentDirty_=false;
    }
};
void runSketchExtendSessions() {SketchExtendSessionTest::run();}
