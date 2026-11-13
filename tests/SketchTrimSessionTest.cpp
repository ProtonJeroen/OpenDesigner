#include "CadViewport.h"
#include "MainWindow.h"
#include "SketchProjection.h"
#include "SketchProfileValidation.h"
#include "SketchProfileBuilder.h"
#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QTemporaryDir>
#include <QMenu>
#include <QToolBar>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <TopExp_Explorer.hxx>
#include <TColStd_ListOfInteger.hxx>
#include <algorithm>
#include <BRepAdaptor_Curve.hxx>
#include <Image_AlienPixMap.hxx>
#include <TopoDS.hxx>
#include <cmath>
#include <stdexcept>

namespace {
void check(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
void key(QWidget* widget,int code) {QKeyEvent event(QEvent::KeyPress,code,Qt::NoModifier);QApplication::sendEvent(widget,&event);}
}
struct SketchTrimSessionTest {
    static NativeDocumentSnapshot fixture(Sketch::Geometry source=Sketch::Segment{{-60,0},{60,0}}) {
        NativeDocumentSnapshot snapshot;Construction::Object wp;wp.id="wp";wp.name="Trim Workplane";wp.kind=Construction::Kind::Workplane;wp.extent=100;
        wp.frame=gp_Ax3(gp_Pnt(10,20,30),gp_Dir(1,2,3),gp_Dir(2,-1,0));snapshot.constructions={wp};
        Sketch::Entity target;target.id="target";target.name="Named source";target.workplaneId=wp.id;target.geometry=source;
        auto left=target;left.id="left";left.role=Sketch::Role::Construction;left.geometry=Sketch::Segment{{-20,-60},{-20,60}};
        auto right=left;right.id="right";right.geometry=Sketch::Segment{{20,-60},{20,60}};
        snapshot.sketchEntities={target,left,right};return snapshot;
    }
    static void import(MainWindow& w,const NativeDocumentSnapshot& snapshot) {
        auto& v=*w.viewport_;QString error;check(v.importDocumentSnapshot(snapshot,error),qPrintable(error));
        check(v.activateSketchWorkplane("wp") && v.lookAtWorkplane("wp"),"transformed Workplane activation failed");
        QApplication::processEvents();w.documentDirty_=false;
    }
    static void start(MainWindow& w) {
        auto* action=w.findChild<QAction*>("sketch.trim");check(action && action->isEnabled(),"Trim menu action unavailable");action->trigger();QApplication::processEvents();
        check(w.viewport_->commandSession_ && w.viewport_->commandSession_->trim && w.viewport_->commandPanel_->isVisible(),"Trim did not use existing visible Sketch command session");
    }
    static void click(CadViewport& v,Sketch::Point point) {
        const auto p=Sketch::worldPoint(point,v.constructionObject("wp")->frame);int x,y;v.view_->Convert(p.X(),p.Y(),p.Z(),x,y);
        const QPoint at(x,y);QMouseEvent move(QEvent::MouseMove,QPointF(at),QPointF(v.mapToGlobal(at)),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&v,&move);
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(at),QPointF(v.mapToGlobal(at)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&v,&press);
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(at),QPointF(v.mapToGlobal(at)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&v,&release);
    }
    static void blue(CadViewport& v) {
        Image_AlienPixMap image;check(v.view_->ToPixMap(image,800,600),"Trim preview cannot render");int blue=0;
        for(int y=0;y<600;++y) for(int x=0;x<800;++x) {
            const auto color=image.PixelColor(x,y).GetRGB();if(color.Blue()>color.Red()+.3 && color.Blue()>color.Green()+.05 && color.Green()>color.Red()+.1) ++blue;
        }
        check(blue>30,"blue Trim preview is not visible");
    }
    static void curveAction(MainWindow& w,const char* id) {
        auto* action=w.findChild<QAction*>(id);check(action && action->isEnabled(),"curve action unavailable");action->trigger();QApplication::processEvents();
    }
    static void available(MainWindow& w,bool enabled) {
        auto* menu=w.findChild<QMenu*>("modeling2DMenu");auto* toolbar=w.findChild<QToolBar*>("sketchToolbar");check(menu && toolbar,"2D menu/toolbar missing");
        for(const auto* id:{"sketch.trim","sketch.extend","sketch.overdraw"}) {
            const auto actions=w.findChildren<QAction*>(id);check(actions.size()==1,"duplicate/missing curve action");
            check(actions[0]->isEnabled()==enabled && menu->actions().count(actions[0])==1 && toolbar->actions().count(actions[0])==1,"curve command state or shared menu/toolbar action incorrect");
        }
    }
    static void clean(CadViewport& v) {
        check(!v.commandSession_ && v.sketchPreviews_.empty() && v.overdrawHover_.IsNull() && v.sketchSnapMarker_.IsNull() && v.sketchAnchorMarker_.IsNull(),"curve command left preview/marker/session state");
        check(v.context_->NbSelected()==0 && !v.context_->HasDetected(),"curve command left selected or detected owners");
        check(!v.commandPanel_->isVisible() && !v.sketchOverdrawPanel_,"curve command left its panel visible");
        check((v.moveFaceArrow_.IsNull() || !v.context_->IsDisplayed(v.moveFaceArrow_)) && (v.faceArrow_.IsNull() || !v.context_->IsDisplayed(v.faceArrow_)),"curve command left a manipulator");
    }
    static void workflowIntegration() {
        MainWindow w;w.resize(1400,1000);w.show();QApplication::processEvents();auto& v=*w.viewport_;QString error;
        if(qgetenv("QT_SCALE_FACTOR")=="2") check(v.devicePixelRatioF()>1.9,"workflow HiDPI scaling inactive");
        available(w,false);import(w,fixture());available(w,true);
        curveAction(w,"sketch.trim");click(v,{0,0});check(v.commandSession_->valid,"workflow Trim preview missing");
        curveAction(w,"sketch.extend");check(v.commandSession_->extend && !v.commandSession_->trim && v.sketchPreviews_.empty() && !v.sketchTrimPrompt_->isVisible(),"Trim to Extend switch retained old preview");
        curveAction(w,"sketch.overdraw");check(v.commandSession_->overdraw && !v.commandSession_->extend && !v.sketchExtendPrompt_->isVisible(),"Extend to Overdraw switch retained old state");
        key(v.sketchOverdrawPanel_,Qt::Key_Escape);clean(v);
        // Both nonmodal panel and viewport keyboard paths must terminate the
        // same transaction, leaving history and confirmed values unchanged.
        for(const auto& geometry:std::vector<Sketch::Geometry>{Sketch::Circle{{0,0},40},Sketch::Ellipse{{0,0},50,30,0},Sketch::Spline{{{-60,0},{0,40},{60,0}}}}) {
            const auto snapshot=fixture(geometry);import(w,snapshot);curveAction(w,"sketch.trim");
            click(v,std::holds_alternative<Sketch::Spline>(geometry)?Sketch::Point{0,40}:std::holds_alternative<Sketch::Ellipse>(geometry)?Sketch::Point{50,0}:Sketch::Point{40,0});
            check(v.commandSession_->valid && v.sketchEntities_==snapshot.sketchEntities,"curve-type Trim changed source or missed preview");
            key(&v,Qt::Key_Escape);clean(v);check(v.sketchEntities_==snapshot.sketchEntities && v.undoStack_.empty(),"curve-type Trim Cancel changed history");
        }
        auto extend=fixture(Sketch::Segment{{-60,0},{0,0}});import(w,extend);
        curveAction(w,"sketch.extend");click(v,{0,0});click(v,{20,30});check(v.commandSession_->valid,"workflow explicit Extend boundary failed");
        const auto expected=v.commandSession_->sketch->after;key(&v,Qt::Key_Return);clean(v);
        check(v.sketchEntities_==expected && v.undoStack_.size()==1,"workflow Extend not one Undo step");v.undoPull();check(v.sketchEntities_==extend.sketchEntities,"workflow Undo failed");v.redoPull();check(v.sketchEntities_==expected,"workflow Redo lost exact values");
        // Duplicate reuses the existing generic Sketch preview and preserves the
        // source curve; Cancel and confirmation remain separate history steps.
        v.selectSketchEntities({"target"});curveAction(w,"sketch.duplicate_curves");check(v.commandSession_ && v.commandSession_->sketch->after.size()==expected.size()+1 && v.sketchEntities_==expected,"duplicate bypassed preview");
        v.cancelCommandSession();v.selectSketchEntities({"target"});curveAction(w,"sketch.duplicate_curves");v.confirmCommandSession();
        check(v.sketchEntities_.size()==expected.size()+1 && v.sketchEntities_.back().id!="target" && v.sketchEntities_.back().geometry==expected[0].geometry,"duplication changed exact source or identity");v.undoPull();check(v.sketchEntities_==expected,"duplicate Undo lost confirmed curve");
        v.endSketchEditing();available(w,false);
        auto owner=*v.constructionObject("wp");owner.selectable=false;check(v.updateConstruction(owner),"Workplane lock failed");check(!v.activateSketchWorkplane("wp"),"locked Workplane activated");available(w,false);
        owner.selectable=true;check(v.updateConstruction(owner),"Workplane unlock failed");check(v.activateSketchWorkplane("wp"),"Workplane activation failed");available(w,true);
        v.setShowWorkplanes(false);available(w,false);check(v.activateSketchWorkplane("wp"),"Workplane view restoration failed");available(w,true);
        owner=*v.constructionObject("wp");owner.visible=false;check(v.updateConstruction(owner),"Workplane hide failed");available(w,false);
        projectedWorkflow(w);splineWorkflow(w);partialReuse(w);pickerRestoration(w);w.documentDirty_=false;
    }
    static void projectedWorkflow(MainWindow& w) {
        auto& v=*w.viewport_;QString error;auto snapshot=fixture();snapshot.sketchEntities.clear();
        gp_Trsf common;common.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),.4);common.SetTranslationPart(gp_Vec(10,20,30));
        snapshot.constructions[0].frame=gp_Ax3(gp_Pnt(0,0,10),gp_Dir(0,-std::sqrt(.75),.5),gp_Dir(1,0,0));snapshot.constructions[0].frame.Transform(common);
        NativePartState part;part.id="cylinder";part.name="Projection source";part.shape=BRepPrimAPI_MakeCylinder(40,10).Shape();part.transform=common;snapshot.parts={part};
        TopoDS_Shape rim;
        for(TopExp_Explorer edge(part.shape,TopAbs_EDGE);edge.More();edge.Next()) {
            BRepAdaptor_Curve curve(TopoDS::Edge(edge.Current()));if(curve.GetType()==GeomAbs_Circle && std::abs(curve.Circle().Location().Z()-10)<1.e-9) {rim=edge.Current();break;}
        }
        check(!rim.IsNull(),"synthetic circular source rim missing");
        const auto projected=SketchProjection::project({{rim,common,"source rim"}},snapshot.constructions[0]);
        check(projected.success && projected.additions.size()==1 && std::holds_alternative<Sketch::Ellipse>(projected.additions[0].geometry),"oblique projection not exact ellipse");
        snapshot.sketchEntities=projected.additions;auto diameter=fixture().sketchEntities[0];diameter.id="diameter";diameter.role=Sketch::Role::Construction;diameter.geometry=Sketch::Segment{{-60,0},{60,0}};snapshot.sketchEntities.push_back(diameter);
        import(w,snapshot);const auto originalPart=v.boxes_[0].shape;const auto originalTransform=v.boxes_[0].orientation;
        curveAction(w,"sketch.overdraw");click(v,{40,0});click(v,{0,20});check(v.commandSession_->overdraw->picks.size()==1,"projected ellipse path not selected");
        const auto selected=v.commandSession_->overdraw->picks[0].profile;
        check(std::holds_alternative<Sketch::EllipticalArc>(selected.geometry) && BRepAdaptor_Curve(TopoDS::Edge(v.sketchPreviews_.at(selected.id)->Shape())).GetType()==GeomAbs_Ellipse,"projected ellipse preview lost analytic support");
        blue(v);NativeDocumentSnapshot pending;check(v.exportDocumentSnapshot(pending) && pending.sketchEntities==snapshot.sketchEntities,"Save captured pending projected Overdraw");
        click(v,{0,-20});key(v.sketchOverdrawPanel_,Qt::Key_Return);clean(v);check(v.undoStack_.size()==1 && v.sketchEntities_.size()==4,"projected Overdraw did not confirm as one change");
        check(v.boxes_[0].shape.IsSame(originalPart),"Sketch operation replaced projected source B-Rep");
        for(int r=1;r<=3;++r)for(int c=1;c<=4;++c)check(v.boxes_[0].orientation.Value(r,c)==originalTransform.Value(r,c),"Sketch operation moved projected source");
        const auto confirmed=v.sketchEntities_;curveAction(w,"sketch.overdraw");click(v,{40,0});click(v,{0,20});click(v,{0,-20});v.confirmCommandSession();clean(v);
        check(v.sketchEntities_==confirmed && v.undoStack_.size()==1,"projected Overdraw reuse was not a genuine no-op");
        // Model notifications must discard an evaluated graph without restoring
        // its old before-snapshot over an external change.
        curveAction(w,"sketch.overdraw");click(v,{40,0});click(v,{0,20});auto changed=v.sketchEntities_;changed[0].name="Changed externally";v.restoreSketchEntities(changed);
        check(!v.commandSession_ && v.sketchEntities_==changed,"stale Overdraw graph survived model notification");clean(v);
        curveAction(w,"sketch.overdraw");click(v,{40,0});click(v,{0,20});v.sketchEntities_[0].name="Changed without notification";const auto unnotified=v.sketchEntities_;
        v.confirmCommandSession();check(v.commandSession_ && v.sketchEntities_==unnotified && v.undoStack_.size()==1,"stale confirmation overwrote canonical data");v.cancelCommandSession();clean(v);
        // Existing import remains authoritative: Open terminates the command,
        // restores confirmed values and disables Sketch actions until activated.
        NativeDocumentSnapshot saved;check(v.exportDocumentSnapshot(saved),"workflow snapshot failed");curveAction(w,"sketch.trim");
        check(v.importDocumentSnapshot(saved,error),qPrintable(error));check(!v.commandSession_ && v.sketchEntities_==saved.sketchEntities,"Open retained pending command");available(w,false);
    }
    static void splineWorkflow(MainWindow& w) {
        auto& v=*w.viewport_;auto snapshot=fixture();snapshot.sketchEntities.resize(2);
        const Sketch::Spline spline{{{-60,0},{0,40},{60,0}}};snapshot.sketchEntities[0].geometry=spline;snapshot.sketchEntities[0].role=Sketch::Role::Construction;
        snapshot.sketchEntities[1].geometry=Sketch::Segment{{60,0},{-60,0}};snapshot.sketchEntities[1].role=Sketch::Role::Construction;
        import(w,snapshot);curveAction(w,"sketch.overdraw");click(v,{60,0});click(v,{0,40});
        check(v.commandSession_->overdraw->picks.size()==1,"spline Overdraw path pick failed");
        const auto picked=v.commandSession_->overdraw->picks[0].profile;
        check(std::holds_alternative<Sketch::BSpline>(picked.geometry) && std::get<Sketch::BSpline>(picked.geometry).reversed,"reverse spline walk did not preserve exact coefficient traversal");
        check(BRepAdaptor_Curve(TopoDS::Edge(v.sketchPreviews_.at(picked.id)->Shape())).GetType()==GeomAbs_BSplineCurve,"spline blue preview lost exact support");
        const auto original=Sketch::splineCurve(spline);const auto exact=Sketch::bsplineCurve(std::get<Sketch::BSpline>(picked.geometry));
        for(int i=1;i<=original->NbPoles();++i)check(original->Pole(i).Distance(exact->Pole(i))==0 && original->Weight(i)==exact->Weight(i),"spline UI re-interpolated its reversed portion");
        blue(v);click(v,{0,0});v.confirmCommandSession();clean(v);
        check(v.undoStack_.size()==1 && v.sketchEntities_.size()==4 && v.sketchEntities_[0]==snapshot.sketchEntities[0] && v.sketchEntities_[1]==snapshot.sketchEntities[1],"spline Overdraw changed source or lifecycle");
        const auto analysis=SketchProfileValidation::validate(v.sketchEntities_,"wp");check(analysis.status==SketchProfileValidation::Status::Valid,"chosen spline/chord Profile is not closed");
        const auto result=SketchProfileBuilder::build(analysis,v.constructionObject("wp")->frame);check(result.success && result.faces.size()==1 && std::abs(result.faces[0].area-3200)<1.e-7,"spline workflow profile lost its independent polynomial area");
    }
    static void partialReuse(MainWindow& w) {
        auto& v=*w.viewport_;auto snapshot=fixture();snapshot.sketchEntities.clear();
        const Sketch::Point corners[]={{-10,0},{10,0},{10,20},{-10,20}};
        for(int i=0;i<4;++i) {auto edge=fixture().sketchEntities[0];edge.id=QString("construction%1").arg(i);edge.role=Sketch::Role::Construction;edge.geometry=Sketch::Segment{corners[i],corners[(i+1)%4]};snapshot.sketchEntities.push_back(edge);}
        auto existing=fixture().sketchEntities[0];existing.id="long-profile";existing.name="Preserve whole user curve";existing.geometry=Sketch::Segment{{-40,0},{40,0}};existing.visible=false;existing.selectable=false;snapshot.sketchEntities.push_back(existing);
        auto unrelated=existing;unrelated.id="unrelated-contour";unrelated.geometry=Sketch::Circle{{80,40},5};unrelated.visible=true;unrelated.selectable=true;snapshot.sketchEntities.push_back(unrelated);
        import(w,snapshot);
        // Keep the small rectangle's nodes separated by the logical-pixel
        // aperture at both desktop scales, despite the distant unrelated loop.
        v.view_->Camera()->SetScale(80);v.view_->Redraw();QApplication::processEvents();
        curveAction(w,"sketch.overdraw");click(v,{-10,0});click(v,{0,0});click(v,{10,10});click(v,{0,20});click(v,{-10,10});
        if(v.commandSession_->overdraw->picks.size()!=4) throw std::runtime_error(QString("partial-reuse path: %1 picks, %2 candidates; %3")
            .arg(v.commandSession_->overdraw->picks.size()).arg(v.commandSession_->overdraw->hits.size()).arg(v.sketchOverdrawPanel_->findChild<QLabel*>("overdrawPrompt")->text()).toStdString());
        const auto prompt=v.sketchOverdrawPanel_->findChild<QLabel*>("overdrawPrompt")->text();check(prompt.contains("reused whole") && prompt.contains("outside") && prompt.contains("prevent profile construction"),"partial Profile-reuse limitation not visible");
        const auto after=v.commandSession_->sketch->after;check(after.size()==snapshot.sketchEntities.size()+3 && after[4]==existing && after[5]==unrelated,"partial reuse damaged original or unrelated contour");
        const auto analysis=SketchProfileValidation::validate(after,"wp");check(analysis.status!=SketchProfileValidation::Status::Valid && !SketchProfileBuilder::build(analysis,v.constructionObject("wp")->frame).success,"retained Profile tails were silently certified as the selected contour");
        v.confirmCommandSession();clean(v);check(v.sketchEntities_==after && v.undoStack_.size()==1,"partial reuse was not atomic");v.undoPull();check(v.sketchEntities_==snapshot.sketchEntities,"partial reuse Undo damaged original contours");
    }
    static void pickerRestoration(MainWindow& w) {
        auto& v=*w.viewport_;
        const auto pointer=[&](gp_Pnt p,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
            int x,y;v.view_->Convert(p.X(),p.Y(),p.Z(),x,y);const QPoint at(x,y);
            QMouseEvent move(QEvent::MouseMove,QPointF(at),QPointF(v.mapToGlobal(at)),Qt::NoButton,Qt::NoButton,modifiers);QApplication::sendEvent(&v,&move);
            QMouseEvent press(QEvent::MouseButtonPress,QPointF(at),QPointF(v.mapToGlobal(at)),Qt::LeftButton,Qt::LeftButton,modifiers);QApplication::sendEvent(&v,&press);
            QMouseEvent release(QEvent::MouseButtonRelease,QPointF(at),QPointF(v.mapToGlobal(at)),Qt::LeftButton,Qt::NoButton,modifiers);QApplication::sendEvent(&v,&release);
        };
        for(const auto* command:{"sketch.trim","sketch.extend","sketch.overdraw"}) {
            auto snapshot=fixture();snapshot.constructions[0].frame=gp_Ax3(gp_Pnt(0,0,80),gp_Dir(0,0,1),gp_Dir(1,0,0));snapshot.sketchEntities[0].role=Sketch::Role::Construction;
            NativePartState box;box.id="box";box.name="Picker target";box.shape=BRepPrimAPI_MakeBox(100,80,60).Shape();snapshot.parts={box};import(w,snapshot);
            if(QString::fromLatin1(command)=="sketch.extend") {
                snapshot.sketchEntities[0].geometry=Sketch::Segment{{-60,0},{0,0}};import(w,snapshot);
            }
            curveAction(w,command);
            if(QString::fromLatin1(command)=="sketch.trim") click(v,{0,0});
            else if(QString::fromLatin1(command)=="sketch.extend") {click(v,{0,0});click(v,{20,30});}
            else {click(v,{-20,0});click(v,{0,0});}
            check(v.commandSession_->valid,"picker-restoration fixture has no active preview");
            v.endSketchEditing();clean(v);available(w,false);v.view_->SetProj(V3d_Zpos);v.fitAll();QApplication::processEvents();
            v.selectParts({});pointer({50,40,60});check(v.selectionContext().kind==CadViewport::SelectionContext::Kind::Faces,"Face picking not restored after Sketch command");
            v.selectParts({});pointer({100,40,60});check(v.selectionContext().kind==CadViewport::SelectionContext::Kind::Edges,"Edge picking not restored after Sketch command");
            pointer({50,40,60},Qt::AltModifier);check(v.selectionContext().kind==CadViewport::SelectionContext::Kind::Parts,"Alt Part picking not restored after Sketch command");
        }
        v.selectParts({});
    }
    static void run() {
        MainWindow w;w.resize(1400,1000);w.show();QApplication::processEvents();auto& v=*w.viewport_;QString error;
        check(!w.findChild<QAction*>("sketch.trim")->isEnabled(),"Trim enabled without Workplane");v.startSketchTrim();check(!v.commandSession_,"Trim implicitly activated Workplane");
        const auto initial=fixture();import(w,initial);const auto history=v.undoStack_.size();
        start(w);check(!v.commandButtons_->button(QDialogButtonBox::Ok)->isEnabled(),"Trim can confirm without segment");
        key(&v,Qt::Key_Return);check(v.commandSession_ && v.sketchEntities_==initial.sketchEntities,"empty Trim confirmation changed model");
        click(v,{20,0});check(v.commandSession_ && !v.commandSession_->valid && v.sketchPreviews_.empty(),"ambiguous intersection click created candidate");
        click(v,{0,0});check(v.commandSession_ && v.commandSession_->valid,"Trim click did not generate candidate");
        check(v.sketchEntities_==initial.sketchEntities && v.undoStack_.size()==history && !w.documentDirty_,"Trim Preview changed document/history");
        const auto candidate=v.commandSession_->sketch->after;check(candidate.size()==4 && candidate[0].id=="target" && candidate[1].id!="target","Trim did not retain first ID and allocate second ID");
        check(candidate[0].name=="Named source" && candidate[1].name=="Named source" && candidate[0].role==Sketch::Role::Profile && candidate[1].role==Sketch::Role::Profile,"Trim lost metadata/role");
        const auto a=std::get<Sketch::Segment>(candidate[0].geometry),b=std::get<Sketch::Segment>(candidate[1].geometry);
        check(std::abs(a.start.x+60)<1.e-9 && std::abs(a.end.x+20)<1.e-9 && std::abs(b.start.x-20)<1.e-9 && std::abs(b.end.x-60)<1.e-9,"Trim retained wrong intervals");
        const auto& frame=v.constructionObject("wp")->frame;
        BRepAdaptor_Curve displayed(TopoDS::Edge(v.sketchPreviews_.at("target")->Shape()));
        check(displayed.Value(displayed.FirstParameter()).Distance(Sketch::worldPoint(a.start,frame))<1.e-7 && displayed.Value(displayed.LastParameter()).Distance(Sketch::worldPoint(a.end,frame))<1.e-7,"Trim preview lost Workplane transform");
        blue(v);click(v,{-40,0});check(v.commandSession_->sketch->after==candidate,"second click chained another Trim before confirmation");
        NativeDocumentSnapshot pending;check(v.exportDocumentSnapshot(pending) && pending.sketchEntities==initial.sketchEntities,"Save captured unconfirmed Trim");
        key(v.commandPanel_,Qt::Key_Escape);check(!v.commandSession_ && v.sketchPreviews_.empty() && v.sketchEntities_==initial.sketchEntities && v.undoStack_.size()==history && !w.documentDirty_,"Escape changed document or left Trim previews");
        start(w);click(v,{0,0});const auto confirmed=v.commandSession_->sketch->after;key(v.commandPanel_,Qt::Key_Return);
        check(!v.commandSession_ && v.sketchEntities_==confirmed && v.undoStack_.size()==history+1 && w.documentDirty_,"Enter did not confirm exactly one SketchChange");
        check(!v.sketchTrimPrompt_->isVisible(),"Trim prompt leaked into next command");
        v.undoPull();check(v.sketchEntities_==initial.sketchEntities,"Trim Undo did not restore exact original");const auto redo=v.redoStack_.size();
        start(w);click(v,{0,0});v.commandButtons_->button(QDialogButtonBox::Cancel)->click();
        check(v.sketchEntities_==initial.sketchEntities && v.redoStack_.size()==redo,"Cancel lost Redo or changed source");
        v.redoPull();check(v.sketchEntities_==confirmed,"Trim Redo changed values or IDs");
        NativeDocumentSnapshot snapshot;check(v.exportDocumentSnapshot(snapshot),"Trim snapshot failed");QTemporaryDir temp;NativeDocumentSnapshot loaded;
        check(NativeDocumentStorage::save(temp.filePath("trim.odc"),snapshot,error) && NativeDocumentStorage::load(temp.filePath("trim.odc"),loaded,error) && loaded.sketchEntities==confirmed,"confirmed Trim Save/Open changed IDs or geometry");
        // One-sided tail removal and role preservation.
        auto construction=fixture();construction.sketchEntities[0].role=Sketch::Role::Construction;import(w,construction);start(w);click(v,{-40,0});v.commandButtons_->button(QDialogButtonBox::Ok)->click();
        check(v.sketchEntities_.size()==3 && v.sketchEntities_[0].id=="target" && v.sketchEntities_[0].role==Sketch::Role::Construction && std::abs(std::get<Sketch::Segment>(v.sketchEntities_[0].geometry).start.x+20)<1.e-9,"tail Trim changed role or ID");
        // Elliptical and exact spline workflows use the same click/session path.
        import(w,fixture(Sketch::Ellipse{{0,0},50,30,.2}));start(w);click(v,Sketch::ellipsePoint({{0,0},50,30,.2},0));
        check(v.commandSession_->valid && std::holds_alternative<Sketch::EllipticalArc>(v.commandSession_->sketch->after[0].geometry),"ellipse Trim session failed");v.cancelCommandSession();
        import(w,fixture(Sketch::Spline{{{-60,0},{0,40},{60,0}}}));start(w);click(v,{0,40});
        check(v.commandSession_->valid && std::holds_alternative<Sketch::BSpline>(v.commandSession_->sketch->after[0].geometry),"spline Trim session did not retain exact coefficients");
        v.confirmCommandSession();snapshot={};check(v.exportDocumentSnapshot(snapshot),"spline Trim snapshot failed");
        check(NativeDocumentStorage::save(temp.filePath("spline-trim.odc"),snapshot,error) && NativeDocumentStorage::load(temp.filePath("spline-trim.odc"),loaded,error) && loaded.sketchEntities==v.sketchEntities_,"spline Trim Save/Open lost coefficients");
        // No bounds, locked target, hidden/locked bounds and another Workplane.
        auto noBounds=fixture();noBounds.sketchEntities.resize(1);import(w,noBounds);start(w);click(v,{0,0});
        check(!v.commandSession_->valid && v.sketchTrimPrompt_->text().contains("intersection") && v.sketchEntities_==noBounds.sketchEntities,"unbounded Trim failed without useful feedback");v.cancelCommandSession();
        auto locked=fixture();locked.sketchEntities[0].selectable=false;import(w,locked);start(w);click(v,{0,0});check(!v.commandSession_->valid,"locked target was trimmed");v.cancelCommandSession();
        auto unavailable=fixture();unavailable.sketchEntities[1].visible=false;unavailable.sketchEntities[2].selectable=false;import(w,unavailable);start(w);click(v,{0,0});check(!v.commandSession_->valid,"hidden/locked boundaries used for Trim");v.cancelCommandSession();
        auto other=fixture();auto wp=other.constructions[0];wp.id="other";other.constructions.push_back(wp);other.sketchEntities[1].workplaneId="other";other.sketchEntities[2].workplaneId="other";
        import(w,other);start(w);click(v,{0,0});check(!v.commandSession_->valid,"other Workplane bounded Trim");v.cancelCommandSession();
        import(w,initial);start(w);click(v,{0,0});v.setShowWorkplanes(false);check(!v.commandSession_ && v.sketchEntities_==initial.sketchEntities,"Hide Workplanes committed pending Trim");
        check(v.activateSketchWorkplane("wp"),"reactivation failed");start(w);click(v,{0,0});v.startSketchDrawing(Sketch::DrawTool::Segment);
        check(v.commandSession_ && !v.commandSession_->trim && v.sketchEntities_==initial.sketchEntities,"tool switch committed Trim or retained its state");v.cancelCommandSession();w.documentDirty_=false;
    }
};
void runSketchTrimSessions() {SketchTrimSessionTest::run();}

void runSketchCurveWorkflowSessions() {SketchTrimSessionTest::workflowIntegration();}
