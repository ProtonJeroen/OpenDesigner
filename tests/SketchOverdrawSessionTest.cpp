#include "CadViewport.h"
#include "MainWindow.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTimer>
#include <QFileDialog>
#include <QLineEdit>
#include <QFileInfo>
#include <Image_AlienPixMap.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <TopoDS.hxx>
#include <cmath>
#include <stdexcept>

namespace {
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void key(QWidget* w,int code) {QKeyEvent e(QEvent::KeyPress,code,Qt::NoModifier);QApplication::sendEvent(w,&e);}
void chooseFile(const QString& path) {
    QTimer::singleShot(50,[path] {
        for(auto* w:QApplication::topLevelWidgets()) if(auto* d=qobject_cast<QFileDialog*>(w);d && d->isVisible()) {
            d->setDirectory(QFileInfo(path).absolutePath());d->selectFile(QFileInfo(path).fileName());
            if(auto* edit=d->findChild<QLineEdit*>("fileNameEdit")) edit->setText(QFileInfo(path).fileName());
            QTimer::singleShot(50,d,[d]{QMetaObject::invokeMethod(d,"accept",Qt::DirectConnection);});return;
        }
    });
}
}
struct SketchOverdrawSessionTest {
    static Sketch::Point arcPoint(const Sketch::Arc& arc,double t) {
        const auto a=arc.startAngle+t*arc.sweepAngle;return {arc.center.x+arc.radius*std::cos(a),arc.center.y+arc.radius*std::sin(a)};
    }
    static void checkOrientedPreview(CadViewport& v,const Handle(AIS_Shape)& p,const Sketch::Geometry& geometry) {
        check(!p.IsNull(),"directed preview missing");const auto edge=TopoDS::Edge(p->Shape());BRepAdaptor_Curve curve(edge);
        const auto& frame=v.constructionObject(v.activeSketchWorkplaneId_)->frame;
        for(double t:{0.0,0.25,0.5,0.75,1.0}) {
            const auto at=edge.Orientation()==TopAbs_REVERSED?1-t:t;const auto actual=curve.Value(curve.FirstParameter()+at*(curve.LastParameter()-curve.FirstParameter()));
            Sketch::Point expected;
            if(const auto* a=std::get_if<Sketch::Arc>(&geometry)) expected=arcPoint(*a,t);
            else {const auto& s=std::get<Sketch::Segment>(geometry);expected={s.start.x+(s.end.x-s.start.x)*t,s.start.y+(s.end.y-s.start.y)*t};}
            check(actual.Distance(Sketch::worldPoint(expected,frame))<1.e-7,"green/blue preview orientation differs from selected route");
        }
    }
    static void arcTraversal() {
        constexpr double pi=3.14159265358979323846;
        MainWindow w;w.resize(1400,1000);w.show();QApplication::processEvents();auto& v=*w.viewport_;QString error;
        auto base=fixture();base.sketchEntities.clear();
        const Sketch::Arc a{{0,0},50,350*pi/180,40*pi/180},b{{100,0},40,90*pi/180,-60*pi/180};
        const auto make=[&](const char* id,Sketch::Geometry geometry) {Sketch::Entity e;e.id=id;e.name=id;e.workplaneId="wp";e.role=Sketch::Role::Construction;e.geometry=geometry;return e;};
        base.sketchEntities={make("A",a),make("line",Sketch::Segment{arcPoint(b,0),arcPoint(a,1)}),make("B",b)};
        for(bool reverse:{false,true}) {
            check(v.importDocumentSnapshot(base,error),qPrintable(error));(v.activateSketchWorkplane("wp")&&v.lookAtWorkplane("wp"));w.documentDirty_=false;
            const auto begin=reverse?arcPoint(b,1):arcPoint(a,0);
            const auto middle=Sketch::Point{(arcPoint(b,0).x+arcPoint(a,1).x)/2,(arcPoint(b,0).y+arcPoint(a,1).y)/2};
            const auto line=std::get<Sketch::Segment>(base.sketchEntities[1].geometry);
            std::vector<std::pair<Sketch::Point,Sketch::Geometry>> route;
            if(reverse) route={{arcPoint(b,0.5),Sketch::reversedArc(b)},{middle,line},{arcPoint(a,0.5),Sketch::reversedArc(a)}};
            else route={{arcPoint(a,0.5),a},{middle,Sketch::Segment{line.end,line.start}},{arcPoint(b,0.5),b}};
            const auto chooseRoute=[&] {
                start(w);mouse(v,begin,true);
                check(!v.commandPanel_->isVisible(),"Overdraw start opened legacy 3D controls");
                check(v.commandSession_->overdraw->currentNode.has_value(),"arc route start not acquired");
                for(const auto& [cursor,expected]:route) {
                    mouse(v,cursor);check(v.commandSession_->overdraw->hits.size()==1,"arc/line route unexpectedly ambiguous");
                    checkOrientedPreview(v,v.overdrawHover_,expected);
                    const auto index=v.commandSession_->overdraw->hits.front();
                    if(const auto* arc=std::get_if<Sketch::Arc>(&expected)) {
                        const auto label=v.sketchOverdrawPanel_->candidates->itemText(1);
                        check(label.contains(QString::number(arc->sweepAngle*180/pi,'g',9)),"candidate label shows source direction instead of route direction");
                    }
                    mouse(v,cursor,true);const auto& chosen=v.commandSession_->overdraw->picks.back();
                    check(chosen.piece==index,"click chose a different arc piece");checkOrientedPreview(v,v.sketchPreviews_.at(chosen.profile.id),expected);
                    check(!v.commandPanel_->isVisible() && v.sketchOverdrawPanel_->isVisible(),"Overdraw click stole focus into 3D controls");
                }
            };
            chooseRoute();const auto history=v.undoStack_.size();
            key(v.sketchOverdrawPanel_->candidates,Qt::Key_Backspace);check(v.commandSession_->overdraw->picks.size()==2,"reverse arc Backspace did not remove one piece");
            mouse(v,route.back().first);checkOrientedPreview(v,v.overdrawHover_,route.back().second);mouse(v,route.back().first,true);
            v.sketchOverdrawPanel_->newStartButton->click();check(v.commandSession_->overdraw->picks.size()==3 && !v.commandSession_->overdraw->currentNode,"reverse New start lost chosen pieces");
            key(v.sketchOverdrawPanel_,Qt::Key_Escape);check(v.sketchEntities_==base.sketchEntities && v.undoStack_.size()==history && !w.documentDirty_,"reverse arc Cancel changed document/history");
            chooseRoute();v.sketchOverdrawPanel_->buttons->button(QDialogButtonBox::Ok)->click();const auto confirmed=v.sketchEntities_;
            check(confirmed.size()==6 && v.undoStack_.size()==history+1,"arc chain not one confirmed transaction");
            for(size_t i=0;i<3;++i) {
                check(confirmed[i]==base.sketchEntities[i],"arc chain modified construction sources");
                check(confirmed[i+3].role==Sketch::Role::Profile,"arc traversal result not Profile");
                if(const auto* expected=std::get_if<Sketch::Arc>(&route[i].second)) {
                    const auto result=std::get<Sketch::Arc>(confirmed[i+3].geometry);check(std::abs(result.sweepAngle-expected->sweepAngle)<1.e-12,"reverse arc changed short/long extent");
                }
            }
            const auto endpoints=[](const Sketch::Geometry& g) {
                if(const auto* arc=std::get_if<Sketch::Arc>(&g)) return std::pair{arcPoint(*arc,0),arcPoint(*arc,1)};
                const auto& s=std::get<Sketch::Segment>(g);return std::pair{s.start,s.end};
            };
            for(size_t i=3;i<5;++i) {const auto end=endpoints(confirmed[i].geometry).second,next=endpoints(confirmed[i+1].geometry).first;check(std::hypot(end.x-next.x,end.y-next.y)<1.e-7,"reverse line/arc chain disconnected");}
            v.selectSketchEntities({confirmed.back().id});v.undoPull();check(v.sketchEntities_==base.sketchEntities && v.sketchSelection().empty() && w.structure_->selectedItems().empty(),"arc-chain Undo/selection wrong");
            v.redoPull();check(v.sketchEntities_==confirmed && v.context_->NbSelected()==0,"arc-chain Redo changed profiles/IDs");
            QTemporaryDir dir;NativeDocumentSnapshot saved,loaded;v.exportDocumentSnapshot(saved);
            check(NativeDocumentStorage::save(dir.filePath("reverse-arcs.odc"),saved,error) && NativeDocumentStorage::load(dir.filePath("reverse-arcs.odc"),loaded,error)
                && loaded.sketchEntities==saved.sketchEntities,"reverse arc-chain roundtrip failed");
        }
        // An isolated large arc traversed backwards must remain large.
        const Sketch::Arc large{{0,0},60,350*pi/180,300*pi/180};base.sketchEntities={make("large",large)};
        check(v.importDocumentSnapshot(base,error),qPrintable(error));(v.activateSketchWorkplane("wp")&&v.lookAtWorkplane("wp"));start(w);mouse(v,arcPoint(large,1),true);mouse(v,arcPoint(large,0.5));
        checkOrientedPreview(v,v.overdrawHover_,Sketch::reversedArc(large));mouse(v,arcPoint(large,0.5),true);v.confirmCommandSession();
        check(std::abs(std::get<Sketch::Arc>(v.sketchEntities_.back().geometry).sweepAngle+300*pi/180)<1.e-12,"reverse large arc became short complement");w.documentDirty_=false;
    }
    static NativeDocumentSnapshot fixture() {
        NativeDocumentSnapshot s;Construction::Object wp;wp.id="wp";wp.name="WP1";wp.kind=Construction::Kind::Workplane;wp.extent=110;
        wp.frame=gp_Ax3(gp_Pnt(10,20,30),gp_Dir(1,2,3),gp_Dir(2,-1,0));
        auto wp2=wp;wp2.id="wp2";wp2.name="WP2";wp2.frame.SetLocation(gp_Pnt(-200,0,0));s.constructions={wp,wp2};
        Sketch::Entity line;line.id="line";line.name="Construction line";line.workplaneId="wp";line.role=Sketch::Role::Construction;line.geometry=Sketch::Segment{{-80,0},{80,0}};
        auto circle=line;circle.id="circle";circle.name="Construction circle";circle.geometry=Sketch::Circle{{0,0},40};s.sketchEntities={line,circle};return s;
    }
    static void action(MainWindow& w,const char* name) {auto* a=w.findChild<QAction*>(name);check(a && a->isEnabled(),name);a->trigger();QApplication::processEvents();}
    static QPoint pixel(CadViewport& v,Sketch::Point point) {
        auto p=Sketch::worldPoint(point,v.constructionObject(v.activeSketchWorkplaneId_)->frame);int x,y;v.view_->Convert(p.X(),p.Y(),p.Z(),x,y);return {x,y};
    }
    static void mouse(CadViewport& v,Sketch::Point p,bool click=false) {
        const auto at=pixel(v,p);QMouseEvent move(QEvent::MouseMove,QPointF(at),QPointF(v.mapToGlobal(at)),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&v,&move);
        if(click) {
            QMouseEvent press(QEvent::MouseButtonPress,QPointF(at),QPointF(v.mapToGlobal(at)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&v,&press);
            QMouseEvent release(QEvent::MouseButtonRelease,QPointF(at),QPointF(v.mapToGlobal(at)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&v,&release);
        }
    }
    static void start(MainWindow& w) {action(w,"sketch.overdraw");check(w.viewport_->sketchOverdrawPanel_ && w.viewport_->commandSession_->overdraw,"Overdraw session not visible");}
    static void render(CadViewport& v) {
        Image_AlienPixMap image;check(v.view_->ToPixMap(image,800,600),"Overdraw preview cannot render");int green=0,blue=0;
        for(int y=0;y<600;++y) for(int x=0;x<800;++x) {
            const auto color=image.PixelColor(x,y).GetRGB();
            if(color.Green()>0.7 && color.Red()<0.3 && color.Blue()<0.3) ++green;
            if(color.Blue()>color.Red()+0.3 && color.Blue()>color.Green()+0.05 && color.Green()>color.Red()+0.1) ++blue;
        }
        const auto capture=qgetenv("OPENDESIGNER_OVERDRAW_CAPTURE");if(!capture.isEmpty()) check(image.Save(capture.constData()),"capture failed");
        if(green<=20 || blue<=20) throw std::runtime_error(QString("Preview pixels: green %1, blue %2").arg(green).arg(blue).toStdString());
    }
    static void run() {
        MainWindow w;w.resize(1400,1000);w.show();QApplication::processEvents();auto& v=*w.viewport_;QString error;
        const auto initial=fixture();check(v.importDocumentSnapshot(initial,error),"fixture import failed");w.documentDirty_=false;
        check(!w.findChild<QAction*>("sketch.overdraw")->isEnabled(),"Overdraw enabled outside active 2D context");
        v.startSketchOverdraw();check(!v.commandSession_,"Overdraw implicitly activated a Workplane");(v.activateSketchWorkplane("wp")&&v.lookAtWorkplane("wp"));
        w.findChild<QAction*>("sketch.construction")->setChecked(true); // Geometry output remains Profile.
        start(w);auto* panel=v.sketchOverdrawPanel_;
        check(!panel->buttons->button(QDialogButtonBox::Ok)->isEnabled() && !w.documentDirty_,"empty Overdraw dirtied document or enabled OK");
        v.confirmCommandSession();check(v.commandSession_.has_value(),"empty Overdraw committed");
        mouse(v,{40,0},true);check(v.commandSession_->overdraw->currentNode.has_value(),"meeting point not acquired on placed WP");
        mouse(v,{0,40});check(!v.overdrawHover_.IsNull() && v.commandSession_->overdraw->hover,"candidate not green");
        const auto green=v.overdrawHover_->Attributes()->WireAspect()->Aspect()->Color();check(green.Green()>0.9 && green.Red()<0.1,"candidate has incorrect color");
        check(v.context_->NbSelected()==0 && v.sketchEntities_==initial.sketchEntities && v.undoStack_.empty(),"hover changed canonical geometry or selected AIS owners");
        // At the junction both line directions and both circle portions are possible.
        mouse(v,{40,0});check(v.commandSession_->overdraw->hits.size()==4 && !v.commandSession_->overdraw->hover && v.overdrawHover_.IsNull(),"ambiguous junction auto-chose a portion");
        mouse(v,{40,0},true);check(v.commandSession_->overdraw->picks.empty(),"ambiguous click selected geometry");
        int upper=-1;
        const auto& state=*v.commandSession_->overdraw;
        for(size_t i=0;i<state.hits.size();++i) if(const auto* a=std::get_if<Sketch::Arc>(&state.graph.pieces[state.hits[i]].geometry);a && std::sin(a->startAngle+a->sweepAngle/2)>0.5) upper=int(i);
        check(upper>=0,"upper arc missing from choices");panel->candidates->setCurrentIndex(upper+1);
        check(!v.overdrawHover_.IsNull(),"explicit ambiguous choice not previewed");panel->chooseButton->click();
        check(v.commandSession_->overdraw->picks.size()==1 && v.sketchPreviews_.size()==1 && v.overdrawHover_.IsNull(),"chosen arc not blue or route was automatically completed");
        mouse(v,{0,-40},true);check(v.commandSession_->overdraw->picks.size()==2,"second arc could not be chosen");
        key(panel->candidates,Qt::Key_Backspace);check(v.commandSession_->overdraw->picks.size()==1 && v.sketchPreviews_.size()==1,"Backspace did not correct last piece");
        mouse(v,{0,0});render(v);mouse(v,{0,0},true);
        check(v.commandSession_->overdraw->picks.size()==2 && v.sketchEntities_==initial.sketchEntities && !w.documentDirty_,"pending route mutated document");
        for(const auto& e:initial.sketchEntities) check(v.context_->IsDisplayed(v.sketchPresentations_.at(e.id)),"source construction hidden or removed");
        NativeDocumentSnapshot pending;v.exportDocumentSnapshot(pending);check(pending.sketchEntities==initial.sketchEntities,"Save captured temporary Overdraw pieces");
        panel->buttons->button(QDialogButtonBox::Ok)->click();
        const auto confirmed=v.sketchEntities_;
        check(confirmed.size()==4 && v.undoStack_.size()==1 && w.documentDirty_ && !v.sketchOverdrawPanel_ && v.overdrawHover_.IsNull()
            && v.sketchPreviews_.empty() && v.sketchSnapMarker_.IsNull() && v.sketchAnchorMarker_.IsNull(),"Overdraw commit not atomic or left transient objects");
        check(confirmed[0]==initial.sketchEntities[0] && confirmed[1]==initial.sketchEntities[1] && confirmed[2].role==Sketch::Role::Profile
            && confirmed[3].role==Sketch::Role::Profile && confirmed[2].id!=confirmed[3].id,"sources/roles/identity not retained");
        check(w.constructionRoot_->child(0)->childCount()==4,"profile browser rows missing");
        v.selectSketchEntities({confirmed[2].id});v.undoPull();check(v.sketchEntities_==initial.sketchEntities && v.sketchSelection().empty()
            && w.structure_->selectedItems().empty(),"Undo did not restore sources/clear selection");
        v.redoPull();check(v.sketchEntities_==confirmed && v.context_->NbSelected()==0,"Redo changed identities or restored selection");
        // Confirmed profiles are independent of later changes and deletion of sources.
        auto changed=initial.sketchEntities[1];changed.geometry=Sketch::Circle{{0,0},45};check(v.updateSketchEntity(changed,error),"source edit failed");
        check(v.sketchEntities_[2]==confirmed[2] && v.sketchEntities_[3]==confirmed[3],"source edit changed profile geometry");
        check(v.deleteSketchEntities({"line","circle"},error) && v.sketchEntities_.size()==2,"source deletion removed profiles");v.undoPull();v.undoPull();
        check(v.sketchEntities_==confirmed,"source Undo changed profile values");
        // Save and failed Open preserve a pending session, history, and Redo.
        QTemporaryDir dir;w.documentPath_=dir.filePath("overdraw.odc");const auto history=v.undoStack_.size(),redo=v.redoStack_.size();
        start(w);mouse(v,{80,0},true);mouse(v,{60,0},true);check(v.commandSession_->overdraw->picks.size()==1,"pending second command failed");
        v.sketchOverdrawPanel_->newStartButton->click();check(!v.commandSession_->overdraw->currentNode && v.commandSession_->overdraw->picks.size()==1,"New start lost chosen geometry");
        mouse(v,{-80,0},true);mouse(v,{-60,0},true);check(v.commandSession_->overdraw->picks.size()==2,"second chain not selectable");
        v.sketchOverdrawPanel_->backButton->click();check(v.commandSession_->overdraw->picks.size()==1,"Back button did not correct a second chain");
        mouse(v,{-60,0},true);
        check(w.saveDocument() && v.commandSession_ && v.undoStack_.size()==history && v.redoStack_.size()==redo,"Save terminated Overdraw or cleared history");
        NativeDocumentSnapshot loaded;check(NativeDocumentStorage::load(w.documentPath_,loaded,error) && loaded.sketchEntities==confirmed,".odc roundtrip changed profiles or saved pending pieces");
        auto bad=loaded;bad.sketchEntities[0].workplaneId="missing";
        check(!v.importDocumentSnapshot(bad,error) && v.commandSession_ && v.commandSession_->overdraw->picks.size()==2 && v.sketchEntities_==confirmed,"failed Open damaged pending command");
        key(v.sketchOverdrawPanel_->candidates,Qt::Key_Escape);check(!v.commandSession_ && !v.sketchOverdrawPanel_ && v.sketchEntities_==confirmed
            && v.redoStack_.size()==redo && !w.documentDirty_,"Cancel changed document/history or dirtied Save state");
        // Successful Open during Overdraw resets editing, selection and previews.
        start(w);mouse(v,{80,0},true);mouse(v,{60,0},true);chooseFile(w.documentPath_);w.openDocument();
        check(v.sketchEntities_==confirmed && v.undoStack_.empty() && v.redoStack_.empty() && !v.commandSession_
            && v.activeSketchWorkplaneId_.isEmpty() && v.overdrawHover_.IsNull() && !v.sketchOverdrawPanel_,"Open retained Overdraw state");
        // Shared chronological history still interleaves with a 3D Move.
        check(v.importDocumentSnapshot(initial,error),"reset fixture failed");v.addBox();(v.activateSketchWorkplane("wp")&&v.lookAtWorkplane("wp"));
        start(w);mouse(v,{80,0},true);mouse(v,{60,0},true);v.confirmCommandSession();
        const auto profileSet=v.sketchEntities_;const auto originalTransform=v.boxes_[0].orientation;
        v.selectParts({0});v.beginCommandSession(CadViewport::CommandSession::Kind::Move,{0},gp_Dir(1,0,0),gp_Pnt());check(v.previewCommand(12),"3D Move failed");v.closeCommandSession(true);
        v.undoPull();check(v.boxes_[0].orientation.TranslationPart().IsEqual(originalTransform.TranslationPart(),1.e-9) && v.sketchEntities_==profileSet,"mixed history did not undo 3D Move");
        v.undoPull();check(v.sketchEntities_==initial.sketchEntities,"mixed history did not undo whole Overdraw");v.redoPull();v.redoPull();
        check(v.sketchEntities_==profileSet && v.context_->NbSelected()==0,"mixed Redo failed");
        // Unsupported cases are explicit, noncommittable, and cancellable.
        auto single=initial;single.sketchEntities.erase(single.sketchEntities.begin());
        check(v.importDocumentSnapshot(single,error),"single circle fixture failed");(v.activateSketchWorkplane("wp")&&v.lookAtWorkplane("wp"));w.documentDirty_=false;start(w);
        check(v.sketchOverdrawPanel_->findChild<QLabel*>("overdrawPrompt")->text().contains("two distinct") && !v.commandSession_->valid,"unsupported circle has no useful explanation");
        key(v.sketchOverdrawPanel_,Qt::Key_Escape);check(v.sketchEntities_==single.sketchEntities && v.undoStack_.empty() && !w.documentDirty_,"unsupported Cancel changed document");
        check(v.importDocumentSnapshot(initial,error),"node ambiguity fixture failed");(v.activateSketchWorkplane("wp")&&v.lookAtWorkplane("wp"));
        v.view_->Camera()->SetScale(10000); // World-space height; V3d_View::SetScale is a relative zoom factor.
        start(w);mouse(v,{0,0});
        check(v.commandSession_->overdraw->hits.size()>1 && !v.commandSession_->overdraw->nodeHover,"ambiguous start point was guessed");
        mouse(v,{0,0},true);check(!v.commandSession_->overdraw->currentNode,"ambiguous start click was accepted");
        v.sketchOverdrawPanel_->candidates->setCurrentIndex(1);v.sketchOverdrawPanel_->chooseButton->click();
        check(v.commandSession_->overdraw->currentNode.has_value(),"explicit start-point choice ignored");v.cancelCommandSession();
        // A Workplane switch cancels all chosen/green geometry; New also cleans it.
        check(v.importDocumentSnapshot(initial,error),"switch fixture failed");(v.activateSketchWorkplane("wp")&&v.lookAtWorkplane("wp"));start(w);mouse(v,{80,0},true);mouse(v,{60,0},true);mouse(v,{0,0});
        auto* chooser=w.findChild<QComboBox*>("sketchWorkplane");chooser->setCurrentIndex(chooser->findData("wp2"));
        check(v.activeSketchWorkplaneId_=="wp2" && !v.commandSession_ && v.sketchEntities_==initial.sketchEntities && v.overdrawHover_.IsNull(),"WP switch committed or leaked Overdraw");
        (v.activateSketchWorkplane("wp")&&v.lookAtWorkplane("wp"));start(w);mouse(v,{80,0},true);mouse(v,{60,0},true);w.documentDirty_=false;w.newDocument();
        check(v.sketchEntities_.empty() && !v.commandSession_ && v.sketchPreviews_.empty() && !v.sketchOverdrawPanel_
            && v.overdrawHover_.IsNull() && !w.findChild<QAction*>("sketch.overdraw")->isEnabled(),"New retained Overdraw state");
        w.documentDirty_=false;
    }
};
void runSketchOverdrawSessions() {SketchOverdrawSessionTest::run();}
void runSketchOverdrawTraversal() {SketchOverdrawSessionTest::arcTraversal();}
