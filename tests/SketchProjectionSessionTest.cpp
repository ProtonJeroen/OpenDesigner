#include "CadViewport.h"
#include "MainWindow.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDockWidget>
#include <QEventLoop>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QWheelEvent>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Image_AlienPixMap.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_PointAspect.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Elips.hxx>
#include <Geom_Ellipse.hxx>
#include <Geom_BSplineCurve.hxx>
#include <GeomConvert.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void settle() {for(int i=0;i<3;++i) {QApplication::processEvents();QGuiApplication::sync();QEventLoop loop;QTimer::singleShot(10,&loop,&QEventLoop::quit);loop.exec();}}
void key(QWidget& widget,int code) {QKeyEvent event(QEvent::KeyPress,code,Qt::NoModifier);QApplication::sendEvent(&widget,&event);settle();}
// Compare exact curves/surfaces/locations/topology without AIS mesh caches.
// XCAF assembly insertion during Save can clear TShape::Free; that first
// bookkeeping flag is not geometry. Keep all other flags and topology verbatim.
std::string brep(const TopoDS_Shape& shape) {
    std::ostringstream raw;BRepTools::Write(shape,raw,false,false,TopTools_FormatVersion_CURRENT);
    std::istringstream input(raw.str());std::ostringstream normalized;std::string line;
    while(std::getline(input,line)) {if(line.size()==7 && std::all_of(line.begin(),line.end(),[](char c){return c=='0' || c=='1';})) line[0]='0';normalized<<line<<'\n';}
    return normalized.str();
}
void chooseFile(const QString& path) {
    QTimer::singleShot(50,[path] {
        for(auto* widget:QApplication::topLevelWidgets()) if(auto* dialog=qobject_cast<QFileDialog*>(widget);dialog && dialog->isVisible()) {
            dialog->setDirectory(QFileInfo(path).absolutePath());dialog->selectFile(QFileInfo(path).fileName());
            if(auto* edit=dialog->findChild<QLineEdit*>("fileNameEdit")) edit->setText(QFileInfo(path).fileName());
            QTimer::singleShot(50,dialog,[dialog]{QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);});return;
        }
    });
}
}
struct SketchProjectionSessionTest {
    using Mode=SketchProjectionPanel::Mode;
    static NativeDocumentSnapshot fixture() {
        NativeDocumentSnapshot doc;Construction::Object wp;wp.id="projection-wp";wp.name="WP1";wp.kind=Construction::Kind::Workplane;wp.frame.SetLocation(gp_Pnt(0,0,-5));doc.constructions={wp};
        Sketch::Entity existing;existing.workplaneId=wp.id;existing.name="Existing profile";existing.geometry=Sketch::Segment{{-35,-20},{-15,-20}};doc.sketchEntities={existing};
        auto cut=BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(40,30,10).Shape(),BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(20,15,-5),gp_Dir(0,0,1)),4,20).Shape()).Shape();
        const auto ellipse=BRepBuilderAPI_MakeFace(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Elips(gp_Ax2(),9,5)).Edge()).Wire()).Face();
        const auto splineFace=BRepBuilderAPI_MakeFace(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(
            GeomConvert::CurveToBSplineCurve(new Geom_Ellipse(gp_Elips(gp_Ax2(),9,5)))).Edge()).Wire()).Face();
        const std::vector shapes{cut,BRepPrimAPI_MakeBox(15,12,8).Shape(),BRepPrimAPI_MakeCylinder(6,12).Shape(),BRepPrimAPI_MakeCylinder(7,12,std::numbers::pi).Shape(),BRepPrimAPI_MakePrism(ellipse,gp_Vec(0,0,10)).Shape(),BRepPrimAPI_MakePrism(splineFace,gp_Vec(0,0,10)).Shape()};
        const std::array translations{gp_Vec(),gp_Vec(65,0,5),gp_Vec(95,35,0),gp_Vec(95,-30,0),gp_Vec(-50,35,0),gp_Vec(-90,35,0)};
        for(size_t i=0;i<shapes.size();++i) {NativePartState part;part.id=QString("projection-part-%1").arg(i);part.name=QString("Part %1").arg(i+1);part.shape=shapes[i];part.transform.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(0,0,1)),i==1?0.4:0);part.transform.SetTranslationPart(translations[i]);doc.parts.push_back(part);}
        doc.camera.eye=gp_Pnt(35,10,300);doc.camera.center=gp_Pnt(35,10,0);doc.camera.scale=180;return doc;
    }
    static void setup(MainWindow& w) {
        w.resize(1250,950);w.show();settle();QString error;check(w.viewport_->importDocumentSnapshot(fixture(),error),qPrintable(error));
        check(w.viewport_->activateSketchWorkplane("projection-wp",false),"cannot activate projection target");
        w.viewport_->selectSketchEntities({w.viewport_->sketchEntities_.front().id});w.documentDirty_=false;settle();
    }
    static TopoDS_Face top(CadViewport& v,size_t part=0) {
        TopoDS_Face face;double highest=-1.e100;
        for(TopExp_Explorer it(v.boxes_[part].shape,TopAbs_FACE);it.More();it.Next()) {const BRepAdaptor_Surface s(TopoDS::Face(it.Current()));
            if(s.GetType()==GeomAbs_Plane && s.Plane().Axis().Direction().Z()>0.9 && s.Plane().Location().Z()>highest) {face=TopoDS::Face(it.Current());highest=s.Plane().Location().Z();}}
        check(!face.IsNull(),"top face missing in projection fixture");return face;
    }
    static TopoDS_Shape circular(CadViewport& v,size_t part) {
        for(TopExp_Explorer it(v.boxes_[part].shape,TopAbs_EDGE);it.More();it.Next()) {BRepAdaptor_Curve c(TopoDS::Edge(it.Current()));if(c.GetType()==GeomAbs_Circle) return it.Current();}
        throw std::runtime_error("fixture circular edge missing");
    }
    static void mode(CadViewport& v,Mode mode) {v.sketchProjectionPanel_->mode->setCurrentIndex(v.sketchProjectionPanel_->mode->findData(int(mode)));}
    static void pick(CadViewport& v,const std::vector<std::pair<size_t,TopoDS_Shape>>& shapes) {
        v.context_->ClearSelected(Standard_False);
        for(const auto& [index,shape]:shapes) v.context_->AddOrRemoveSelected(new StdSelect_BRepOwner(shape,v.boxes_[index].presentation,5,Standard_True),Standard_False);
        check(v.captureSketchProjectionSelection(),"projection selection capture failed");
    }
    static QPoint pixel(CadViewport& v,gp_Pnt p,size_t part=0) {p.Transform(v.boxes_[part].orientation);int x,y;v.view_->Convert(p.X(),p.Y(),p.Z(),x,y);return {x,y};}
    static void click(CadViewport& v,QPoint p,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(p),QPointF(v.mapToGlobal(p)),Qt::LeftButton,Qt::LeftButton,modifiers);
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(p),QPointF(v.mapToGlobal(p)),Qt::LeftButton,Qt::NoButton,modifiers);
        QApplication::sendEvent(&v,&press);QApplication::sendEvent(&v,&release);
    }
    static bool activeMode(CadViewport& v,const Handle(AIS_Shape)& p,int mode) {
        TColStd_ListOfInteger modes;v.context_->ActivatedModes(p,modes);return std::find(modes.begin(),modes.end(),mode)!=modes.end();
    }
    static void normalPicking(CadViewport& v) {
        for(const auto& part:v.boxes_) if(part.visible && part.active) for(auto type:{TopAbs_VERTEX,TopAbs_EDGE,TopAbs_FACE})
            check(!activeMode(v,part.presentation,AIS_Shape::SelectionMode(type)),"3D projection picker leaked into normal Sketch editing");
        for(const auto& e:v.sketchEntities_) if(v.sketchPickable(e)) check(activeMode(v,v.sketchPresentations_.at(e.id),0),"normal Sketch picker was not restored");
    }
    static void preview(CadViewport& v,size_t count) {
        check(v.commandSession_ && v.commandSession_->projection && v.commandSession_->valid && v.sketchPreviews_.size()==count,"missing or invalid projection preview");
        for(const auto& [id,p]:v.sketchPreviews_) {
            Quantity_Color color;p->Color(color);check(v.context_->IsDisplayed(p) && color.Blue()>0.9 && color.Green()>0.5 && color.Red()<0.2,"projection preview is not visible blue geometry");
            check(!activeMode(v,p,0),"projection preview is selectable as a source");
        }
        for(const auto& ref:v.commandSession_->projection->sources) check(v.context_->IsSelected(ref.owner),"3D source highlight is missing");
    }
    static void availabilityAndPicking() {
        MainWindow w;w.show();settle();auto& v=*w.viewport_;auto* action=w.findChild<QAction*>("sketch.project_3d");
        check(action && !action->isEnabled(),"projection action executable without a Workplane");QString status;
        QObject::connect(&v,&CadViewport::statusMessage,[&](const QString& message){status=message;});
        v.startSketchProjection();check(!v.commandSession_ && !v.sketchProjectionPanel_ && status.contains("Workplane"),"missing-target invocation did not fail clearly");
        setup(w);check(action->isEnabled(),"active Workplane did not enable projection");const auto baseline=v.sketchEntities_;const auto prior=v.selectedSketchIds_;const auto history=v.undoStack_.size();
        action->trigger();settle();check(v.sketchProjectionPanel_->isVisible() && v.activeSketchWorkplaneId_=="projection-wp","projection left Sketch editing or did not open panel");
        mode(v,Mode::Vertex);click(v,pixel(v,{0,0,10}));preview(v,1);
        check(v.commandSession_->projection->sources.size()==1 && std::holds_alternative<Sketch::ConstructionPoint>(v.commandSession_->projection->result.additions.front().geometry),"viewport vertex pick not a Construction Point");
        check(v.sketchPreviews_.begin()->second->Shape().ShapeType()==TopAbs_VERTEX && v.sketchPreviews_.begin()->second->Attributes()->PointAspect()->Aspect()->Scale()>=5,"projected point preview has no screen marker");
        click(v,pixel(v,{0,0,8},1));preview(v,1);
        const auto placed=std::get<Sketch::ConstructionPoint>(v.commandSession_->projection->result.additions.front().geometry).position;
        check(std::hypot(placed.x-65,placed.y)<1.e-9,"actual OCCT pick of a placed Part was transformed twice or rejected");
        mode(v,Mode::Edge);check(v.commandSession_->projection->sources.empty() && v.sketchPreviews_.empty(),"mode change retained incompatible vertex");
        click(v,pixel(v,{20,0,10}));preview(v,1);check(std::holds_alternative<Sketch::Segment>(v.commandSession_->projection->result.additions[0].geometry),"viewport Edge mode failed");
        mode(v,Mode::Mixed);check(v.commandSession_->projection->sources.size()==1,"Mixed discarded eligible selection");
        click(v,pixel(v,{0,0,10}),Qt::ControlModifier);check(v.commandSession_->projection->sources.size()==2,"Ctrl vertex additive pick failed");preview(v,2);
        click(v,pixel(v,{5,5,10}),Qt::ControlModifier);check(v.commandSession_->projection->sources.size()==3,"Mixed face pick failed");preview(v,6);
        click(v,pixel(v,{0,0,10}),Qt::ControlModifier);check(v.commandSession_->projection->sources.size()==2,"Ctrl removal did not update selection");preview(v,5);
        check(v.sketchEntities_==baseline && v.undoStack_.size()==history && !w.documentDirty_,"live preview dirtied/mutated document/history");
        v.selectSketchEntities({baseline.front().id});check(v.commandSession_ && v.commandSession_->projection->sources.size()==2,"browser 2D selection became a source or closed command");
        mode(v,Mode::Face);click(v,pixel(v,{5,5,10}));preview(v,5);check(v.commandSession_->projection->sources.size()==1,"Face mode did not replace/filter selection");
        v.sketchProjectionPanel_->clear->click();check(v.sketchPreviews_.empty() && v.context_->NbSelected()==0 && !v.sketchProjectionPanel_->buttons->button(QDialogButtonBox::Ok)->isEnabled(),"Clear left preview/highlights or enabled OK");
        key(*v.sketchProjectionPanel_->mode,Qt::Key_Escape);check(!v.commandSession_ && v.sketchPreviews_.empty() && v.sketchEntities_==baseline && v.selectedSketchIds_==prior,"Escape failed to cancel and restore original Sketch selection");normalPicking(v);
        w.documentDirty_=false;
    }
    static void commitHistoryStorage() {
        MainWindow w;setup(w);auto& v=*w.viewport_;const auto baseline=v.sketchEntities_;const auto history=v.undoStack_.size();
        std::vector<std::string> before;for(const auto& part:v.boxes_) before.push_back(brep(part.shape));
        v.startSketchProjection();pick(v,{{0,top(v)},{1,top(v,1)},{2,circular(v,2)},{3,circular(v,3)}});preview(v,11);
        const auto additions=v.commandSession_->projection->result.additions;const auto owners=v.commandSession_->projection->sources;
        check(std::count_if(additions.begin(),additions.end(),[](const auto& e){return std::holds_alternative<Sketch::Circle>(e.geometry);})==2
            && std::count_if(additions.begin(),additions.end(),[](const auto& e){return std::holds_alternative<Sketch::Arc>(e.geometry);})==1,"Circle/Arc/hole preview types incorrect");
        // Recompute and confirm without replacing the preview's stable IDs.
        v.refreshSketchProjection();check(v.commandSession_->projection->result.additions==additions,"unchanged preview recreated stable IDs");
        NativeDocumentSnapshot pending;check(v.exportDocumentSnapshot(pending) && pending.sketchEntities==baseline,"Save snapshot included unconfirmed projection");
        QTemporaryDir dir;QString error;check(NativeDocumentStorage::save(dir.filePath("pending.odc"),pending,error),qPrintable(error));
        check(v.commandSession_ && v.context_->NbSelected()==int(owners.size()),"Save snapshot/storage closed projection or lost selection");
        chooseFile(dir.filePath("live-preview.odc"));check(w.saveDocument(),"File Save during projection failed");
        NativeDocumentSnapshot stillPending;check(NativeDocumentStorage::load(w.documentPath_,stillPending,error) && stillPending.sketchEntities==baseline && v.commandSession_
            && v.context_->NbSelected()==int(owners.size()) && v.undoStack_.size()==history,"File Save committed preview or lost session/selection/history");
        key(*v.sketchProjectionPanel_->mode,Qt::Key_Return);check(!v.commandSession_ && !v.sketchProjectionPanel_ && v.sketchPreviews_.empty(),"Enter did not complete projection lifecycle");
        auto expected=baseline;expected.insert(expected.end(),additions.begin(),additions.end());
        check(v.sketchEntities_==expected && v.undoStack_.size()==history+1 && w.documentDirty_,"projection was not exactly one SketchChange");
        check(v.context_->NbSelected()==0 && v.selectedSketchIds_.empty(),"committed projection retained source selection");normalPicking(v);
        check(w.constructionRoot_->child(0)->childCount()==int(expected.size()),"confirmed projection not rebuilt in Structure Browser");
        for(size_t i=0;i<v.boxes_.size();++i) {
            const auto after=brep(v.boxes_[i].shape);
            if(after!=before[i]) {size_t at=0;while(at<std::min(after.size(),before[i].size()) && after[at]==before[i][at]) ++at;
                std::cerr<<"Source "<<i<<" differs at "<<at<<"; before: "<<before[i].substr(at,200)<<"; after: "<<after.substr(at,200)<<'\n';}
            check(after==before[i],"projection modified exact 3D source Part geometry/topology");
        }
        std::set<QString> ids;for(const auto& e:expected) check(ids.insert(e.id).second && e.workplaneId==v.activeSketchWorkplaneId_,"projection identity/ownership invalid");
        v.undoPull();check(v.sketchEntities_==baseline && v.context_->NbSelected()==0,"Undo did not remove entire projection");
        v.redoPull();check(v.sketchEntities_==expected && v.context_->NbSelected()==0,"Redo did not restore exact stable projection");
        // Re-selecting the face/edge set is a no-op, even with reversed owners.
        v.startSketchProjection();std::vector<std::pair<size_t,TopoDS_Shape>> repeat;
        for(const auto& ref:owners) repeat.push_back({ref.partIndex,ref.shape.Reversed()});pick(v,repeat);preview(v,0);
        const auto savedHistory=v.undoStack_.size();w.documentDirty_=false;v.confirmCommandSession();
        check(!v.commandSession_ && v.undoStack_.size()==savedHistory && !w.documentDirty_ && v.sketchEntities_==expected,"fully deduplicated confirmation dirtied history/model");
        NativeDocumentSnapshot doc,loaded;check(v.exportDocumentSnapshot(doc),"cannot capture projected document");
        check(NativeDocumentStorage::save(dir.filePath("curves.odc"),doc,error) && NativeDocumentStorage::load(dir.filePath("curves.odc"),loaded,error),qPrintable(error));
        check(loaded.schemaVersion==NativeDocumentStorage::CurrentSchemaVersion && loaded.sketchEntities==expected && !Sketch::hasConstructionPoints(loaded.sketchEntities),"projected curves lost migrated native values");
        chooseFile(dir.filePath("saved-as.odc"));check(w.saveDocumentAs() && v.undoStack_.size()==savedHistory && v.sketchEntities_==expected,"Save As lost projected values/history");
        // A vertex transaction produces v4 without changing existing curves.
        v.startSketchProjection();mode(v,Mode::Vertex);const auto placedVertex=TopExp_Explorer(v.boxes_[1].shape,TopAbs_VERTEX).Current();
        pick(v,{{1,placedVertex}});preview(v,1);
        const auto world=BRep_Tool::Pnt(TopoDS::Vertex(placedVertex)).Transformed(v.boxes_[1].orientation);
        const auto point=std::get<Sketch::ConstructionPoint>(v.commandSession_->projection->result.additions.front().geometry).position;
        check(std::hypot(point.x-world.X(),point.y-world.Y())<1.e-9,"UI applied Part placement twice or ignored it");
        v.confirmCommandSession();check(v.undoStack_.size()==savedHistory+1,"single vertex did not make one history step");
        check(v.exportDocumentSnapshot(doc) && NativeDocumentStorage::save(dir.filePath("points.odc"),doc,error) && NativeDocumentStorage::load(dir.filePath("points.odc"),loaded,error),qPrintable(error));
        check(loaded.schemaVersion==NativeDocumentStorage::CurrentSchemaVersion && loaded.sketchEntities==v.sketchEntities_,"projected point failed native v4 roundtrip");
        check(w.saveDocument(),"confirmed point Save failed");v.startSketchProjection();chooseFile(dir.filePath("points.odc"));w.openDocument();
        check(!v.commandSession_ && v.sketchPreviews_.empty() && v.undoStack_.empty() && v.redoStack_.empty() && v.context_->NbSelected()==0 && v.activeSketchWorkplaneId_.isEmpty(),"Open retained projection session/history/pickers");
        w.documentDirty_=false;
    }
    static void existingConstructionOverlap() {
        MainWindow w;w.resize(1250,950);w.show();settle();auto doc=fixture();
        doc.constructions[0].frame.SetLocation(gp_Pnt(0,0,70));
        doc.sketchEntities[0].role=Sketch::Role::Construction;
        doc.sketchEntities[0].geometry=Sketch::Segment{{-11,-59},{89,-59}};
        doc.parts[0].shape=BRepPrimAPI_MakeBox(gp_Pnt(-17.224,-59,30),96.593,20,10).Shape();
        QString error;auto& v=*w.viewport_;check(v.importDocumentSnapshot(doc,error),qPrintable(error));
        check(v.activateSketchWorkplane("projection-wp",false),"cannot activate reported WP1");w.documentDirty_=false;
        const auto baseline=v.sketchEntities_;const auto history=v.undoStack_.size();const auto face=top(v);
        v.startSketchProjection();pick(v,{{0,face}});preview(v,4);
        const auto checkBoundary=[](const auto& entities) {
            return std::any_of(entities.begin(),entities.end(),[](const auto& e) {
                const auto* s=std::get_if<Sketch::Segment>(&e.geometry);if(!s) return false;
                const auto near=[](Sketch::Point a,Sketch::Point b){return std::hypot(a.x-b.x,a.y-b.y)<1.e-9;};
                return (near(s->start,{-17.224,-59}) && near(s->end,{79.369,-59}))
                    || (near(s->end,{-17.224,-59}) && near(s->start,{79.369,-59}));
            });
        };
        check(checkBoundary(v.commandSession_->projection->result.additions),"reported face preview was truncated");
        QTemporaryDir dir;chooseFile(dir.filePath("overlap-pending.odc"));check(w.saveDocument(),"Save during overlapping preview failed");
        NativeDocumentSnapshot loaded;check(NativeDocumentStorage::load(w.documentPath_,loaded,error) && loaded.sketchEntities==baseline
            && v.commandSession_ && v.undoStack_.size()==history,"Save committed partial preview or lost session/history");
        v.cancelCommandSession();check(v.sketchEntities_==baseline && v.undoStack_.size()==history && v.sketchPreviews_.empty(),"Cancel changed overlapping document");
        v.startSketchProjection();pick(v,{{0,face}});preview(v,4);const auto additions=v.commandSession_->projection->result.additions;
        v.confirmCommandSession();auto expected=baseline;expected.insert(expected.end(),additions.begin(),additions.end());
        check(v.sketchEntities_==expected && v.undoStack_.size()==history+1 && checkBoundary(additions),"overlapping face did not commit its entire exact contour once");
        v.undoPull();check(v.sketchEntities_==baseline && v.context_->NbSelected()==0,"overlapping face Undo failed");
        v.redoPull();check(v.sketchEntities_==expected && v.selectedSketchIds_.empty() && v.context_->NbSelected()==0,"overlapping face Redo lost stable values/selection");
        v.startSketchProjection();pick(v,{{0,face.Reversed()}});preview(v,0);w.documentDirty_=false;v.confirmCommandSession();
        check(v.undoStack_.size()==history+1 && !w.documentDirty_,"repeating overlapping face was not a no-op");
        chooseFile(dir.filePath("overlap-confirmed.odc"));check(w.saveDocumentAs() && v.undoStack_.size()==history+1,"Save As lost overlapping geometry/history");
        chooseFile(w.documentPath_);w.openDocument();
        check(v.sketchEntities_==expected && v.undoStack_.empty() && v.redoStack_.empty() && !v.commandSession_
            && v.context_->NbSelected()==0 && checkBoundary(v.sketchEntities_),"Open lost exact overlapping face or left history/selection");
        w.documentDirty_=false;
    }
    static void conicHistory() {
        MainWindow w;setup(w);auto& v=*w.viewport_;const auto baseline=v.sketchEntities_;const auto history=v.undoStack_.size();
        // Tilt the existing analytic elliptical prism: both edge and face modes
        // use the same exact kernel and one existing SketchChange transaction.
        gp_Trsf transform;transform.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,0,0)),0.4);transform.SetTranslationPart(gp_Vec(-50,35,0));
        v.boxes_[4].orientation=transform;
        const auto face=top(v,4);v.startSketchProjection();pick(v,{{4,face}});preview(v,1);
        const auto additions=v.commandSession_->projection->result.additions;
        check(std::holds_alternative<Sketch::Ellipse>(additions[0].geometry),"elliptical face lost analytic output in session");
        NativeDocumentSnapshot snapshot;check(v.exportDocumentSnapshot(snapshot) && snapshot.sketchEntities==baseline,"Save included elliptic preview");
        v.cancelCommandSession();check(v.sketchEntities_==baseline && v.undoStack_.size()==history,"Cancel left conic geometry/history");
        v.startSketchProjection();pick(v,{{4,face}});preview(v,1);const auto committed=v.commandSession_->projection->result.additions;
        v.confirmCommandSession();auto expected=baseline;expected.insert(expected.end(),committed.begin(),committed.end());
        check(v.sketchEntities_==expected && v.undoStack_.size()==history+1,"elliptical projection did not commit one transaction");
        v.undoPull();check(v.sketchEntities_==baseline && v.selectedSketchIds_.empty() && v.context_->NbSelected()==0,"elliptical Undo left geometry/selection");
        v.redoPull();check(v.sketchEntities_==expected && v.selectedSketchIds_.empty() && v.context_->NbSelected()==0,"elliptical Redo lost stable geometry");
        v.startSketchProjection();pick(v,{{4,face.Reversed()}});preview(v,0);const auto count=v.undoStack_.size();w.documentDirty_=false;v.confirmCommandSession();
        check(v.undoStack_.size()==count && !w.documentDirty_,"duplicate elliptic face added an Undo step");
        w.documentDirty_=false;
    }
    static void failuresAndLifecycle() {
        MainWindow w;setup(w);auto& v=*w.viewport_;const auto baseline=v.sketchEntities_;const auto history=v.undoStack_.size();
        const auto face=top(v);v.startSketchProjection();pick(v,{{0,face}});preview(v,5);
        // Same source and reversed occurrence remain one selected source.
        pick(v,{{0,face},{0,face.Reversed()}});preview(v,5);check(v.commandSession_->projection->sources.size()==1 && v.context_->NbSelected()==1,"duplicate/reversed owners not canonicalized");
        v.sketchProjectionPanel_->sources->item(0)->setSelected(true);v.sketchProjectionPanel_->remove->click();check(v.context_->NbSelected()==0 && v.sketchPreviews_.empty(),"Remove sources left selected owners/preview");
        // A vertical edge plus a good face makes the entire preview invalid.
        TopoDS_Shape vertical;for(TopExp_Explorer it(v.boxes_[0].shape,TopAbs_EDGE);it.More();it.Next()) {BRepAdaptor_Curve c(TopoDS::Edge(it.Current()));if(c.GetType()==GeomAbs_Line && std::abs(c.Line().Direction().Z())>0.9) {vertical=it.Current();break;}}
        check(!vertical.IsNull(),"vertical fixture edge missing");pick(v,{{0,face},{0,vertical}});
        check(!v.commandSession_->valid && v.sketchPreviews_.empty() && v.commandSession_->sketch->after==baseline,"bad mixed batch left partial preview");
        check(v.sketchProjectionPanel_->findChild<QPlainTextEdit*>("projectionDiagnostics")->toPlainText().contains("projects to a point"),"kernel diagnostic not shown in panel");
        v.confirmCommandSession();check(v.commandSession_ && v.sketchEntities_==baseline && v.undoStack_.size()==history,"invalid OK committed or destroyed correction workflow");
        TopoDS_Shape unsupported;for(TopExp_Explorer it(v.boxes_[5].shape,TopAbs_EDGE);it.More();it.Next()) if(BRepAdaptor_Curve(TopoDS::Edge(it.Current())).GetType()==GeomAbs_BSplineCurve) {unsupported=it.Current();break;}
        check(!unsupported.IsNull(),"unsupported-curve fixture is missing its spline");pick(v,{{0,face},{5,unsupported}});
        check(!v.commandSession_->valid && v.sketchPreviews_.empty() && v.sketchProjectionPanel_->findChild<QPlainTextEdit*>("projectionDiagnostics")->toPlainText().contains("not approximated"),"unsupported curve diagnostic/atomic preview failed");
        pick(v,{{0,face}});preview(v,5);v.sketchProjectionPanel_->buttons->button(QDialogButtonBox::Cancel)->click();normalPicking(v);
        for(int route=0;route<3;++route) {
            v.startSketchProjection();pick(v,{{0,face}});preview(v,5);
            if(route==0) v.sketchProjectionPanel_->close();
            else if(route==1) v.startSketchDrawing(Sketch::DrawTool::Segment);
            else v.endSketchEditing();
            check(v.sketchPreviews_.empty() && !v.sketchProjectionPanel_ && v.sketchEntities_==baseline && v.undoStack_.size()==history,"close/switch/Finish 2D leaked projection transaction");
            v.cancelCommandSession();if(v.activeSketchWorkplaneId_.isEmpty()) check(v.activateSketchWorkplane("projection-wp",false),"reactivation failed");
        }
        v.startSketchProjection();pick(v,{{0,face}});
        auto moved=v.boxes_[0].orientation;moved.SetTranslationPart(gp_Vec(5,0,0));v.boxes_[0].orientation=moved;
        v.confirmCommandSession();check(!v.commandSession_ && v.sketchPreviews_.empty() && v.context_->NbSelected()==0,"stale Part placement committed projection");v.boxes_[0].orientation=gp_Trsf();
        v.startSketchProjection();pick(v,{{0,face}});v.boxes_[0].active=false;v.refreshSketchProjection();check(!v.commandSession_ && v.sketchPreviews_.empty(),"deleted source left dangling command");v.boxes_[0].active=true;
        v.startSketchProjection();pick(v,{{0,face}});v.setPartVisible(0,false);
        check(!v.commandSession_ && !v.activeSketchWorkplaneId_.isEmpty() && v.sketchPreviews_.empty(),"hiding a source leaked projection or lost valid Sketch context");v.setPartVisible(0,true);v.activateSketchWorkplane("projection-wp",false);
        v.startSketchProjection();pick(v,{{0,face}});auto target=v.constructions_.front();target.visible=false;check(v.updateConstruction(target),"hide target failed");
        check(!v.commandSession_ && !v.sketchProjectionPanel_ && v.sketchPreviews_.empty() && v.activeSketchWorkplaneId_.isEmpty(),"hidden target retained projection context");
        v.undoPull();check(v.activateSketchWorkplane("projection-wp",false),"target restore failed");
        v.startSketchProjection();mode(v,Mode::Vertex);const auto vertex=TopoDS::Vertex(TopExp_Explorer(v.boxes_[0].shape,TopAbs_VERTEX).Current());pick(v,{{0,vertex}});preview(v,1);
        BRep_Builder builder;builder.UpdateVertex(vertex,BRep_Tool::Pnt(vertex).Translated(gp_Vec(1,2,3)),1.e-7);
        v.confirmCommandSession();check(!v.commandSession_ && v.sketchPreviews_.empty() && v.context_->NbSelected()==0,"in-place source geometry changed after preview but still committed");
        v.startSketchProjection();v.resetDocument();check(!v.commandSession_ && v.sketchPreviews_.empty() && v.context_->NbSelected()==0 && v.sketchEntities_.empty(),"New document retained preview/selection");
        setup(w);v.startSketchProjection();pick(v,{{0,top(v)}});w.documentDirty_=false;check(w.close(),"clean window close was refused");
        check(!v.commandSession_ && v.sketchPreviews_.empty() && !v.sketchProjectionPanel_,"closing the document left an active projection");w.documentDirty_=false;
    }
    static void panelAndNavigation() {
        MainWindow w;setup(w);auto& v=*w.viewport_;v.startSketchProjection();settle();auto* panel=v.sketchProjectionPanel_;
        const auto bounds=[&]{return QRect(v.mapToGlobal(QPoint()),v.size());};
        const auto frame=panel->frameGeometry();check(bounds().contains(frame) && std::abs(bounds().right()-frame.right()-12)<=1 && std::abs(frame.top()-bounds().top()-12)<=1,"projection panel not anchored to viewport +12");
        auto* scroll=panel->findChild<QScrollArea*>();check(scroll,"projection panel bypassed central positioner");
        check(scroll->horizontalScrollBar()->maximum()==0 && scroll->verticalScrollBar()->maximum()==0,"projection panel unexpectedly scrolls on a fitting viewport");
        const auto at=bounds().topLeft()+QPoint(25,25);panel->move(panel->pos()+at-panel->frameGeometry().topLeft());settle();
        pick(v,{{0,top(v)}});settle();check(panel->frameGeometry().topLeft()==at,"selection/preview reset user panel position");
        QWheelEvent wheel(QPointF(100,120),QPointF(v.mapToGlobal(QPoint(100,120))),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(&v,&wheel);check(v.commandSession_ && !w.documentDirty_,"viewport navigation closed or committed projection");
        auto* dock=w.findChild<QDockWidget*>();dock->hide();settle();check(bounds().contains(panel->frameGeometry()),"docking left projection panel outside viewport");dock->show();
        v.setFixedSize(340,280);settle();check(bounds().contains(panel->frameGeometry()) && scroll->verticalScrollBar()->maximum()>0,"small viewport did not clamp/scroll projection panel");
        scroll->horizontalScrollBar()->setValue(scroll->horizontalScrollBar()->maximum());scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());settle();
        for(auto role:{QDialogButtonBox::Ok,QDialogButtonBox::Cancel}) {const auto* button=panel->buttons->button(role);check(scroll->viewport()->rect().intersects(QRect(button->mapTo(scroll->viewport(),QPoint()),button->size())),"projection OK/Cancel unreachable after scrolling");}
        v.setMinimumSize(0,0);v.setMaximumSize(QWIDGETSIZE_MAX,QWIDGETSIZE_MAX);w.resize(1250,950);settle();
        check(bounds().contains(panel->frameGeometry()),"viewport restore lost projection panel");
        key(*panel->mode,Qt::Key_Escape);check(!v.commandSession_ && v.sketchPreviews_.empty(),"panel-focused Escape did not cancel");w.documentDirty_=false;
    }
    static void run() {availabilityAndPicking();commitHistoryStorage();existingConstructionOverlap();conicHistory();failuresAndLifecycle();panelAndNavigation();}
};
void runSketchProjectionSessions() {SketchProjectionSessionTest::run();}
