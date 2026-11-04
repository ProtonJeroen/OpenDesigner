#include "CadViewport.h"
#include "MainWindow.h"
#include "CylindricalMoveGeometry.h"
#include "BRepReconstruction.h"
#include "MoveFaceGeometry.h"
#include "BooleanGeometry.h"
#include "CommandPanelPositioner.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMenu>
#include <QStandardItemModel>
#include <QThread>
#include <QTreeWidget>
#include <QTemporaryDir>
#include <QFile>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <gp_Cylinder.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <SelectMgr_Selection.hxx>
#include <SelectMgr_SensitiveEntity.hxx>
#include <TopoDS.hxx>
#include <Image_PixMap.hxx>
#include <Prs3d_Drawer.hxx>
#include <TColStd_ListOfInteger.hxx>
#include <cmath>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace {
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
void near(double actual,double expected,const char* message) {check(std::abs(actual-expected)<1.e-6*std::max(1.,std::abs(expected)),message);}
TopoDS_Face mantle(const TopoDS_Shape& shape) {
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next())
        if(BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType()==GeomAbs_Cylinder) return TopoDS::Face(it.Current());
    return {};
}
std::string serialized(const TopoDS_Shape& s) {std::ostringstream out;BRepTools::Write(s,out);return out.str();}
double volume(const TopoDS_Shape& s) {GProp_GProps p;BRepGProp::VolumeProperties(s,p);return p.Mass();}
TopoDS_Shape fixture(bool boss) {
    const auto block=BRepPrimAPI_MakeBox(100,80,30).Shape();
    if(boss) return BRepAlgoAPI_Fuse(block,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(30,40,30),gp_Dir(0,0,1)),10,20).Shape()).Shape();
    return BRepAlgoAPI_Cut(block,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(30,40,-5),gp_Dir(0,0,1)),10,40).Shape()).Shape();
}
void events() {
    // Native tool-window mapping is asynchronous; allow the existing positioner
    // to observe configure notifications, also with QT_SCALE_FACTOR=2.
    for(int i=0;i<12;++i) {QApplication::processEvents();QThread::msleep(10);}
}
}

struct CylindricalMoveFaceSessionTest {
    static void crashPreview(const QString& path,const QString& displacement) {
        const auto fullBoss=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(158,36,60),gp_Dir(0,0,1)),18,55).Shape();
        const auto hole=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(54,36,-10),gp_Dir(0,0,1)),17,80).Shape();
        const auto oracle=[&](double d) {
            const auto stock=BRepPrimAPI_MakeBox(163+d,80,60).Shape();
            const auto half=BRepPrimAPI_MakeHalfSpace(BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(163+d,0,0),gp_Dir(1,0,0))).Face(),gp_Pnt(0,0,0)).Solid();
            const auto boss=d>=13?fullBoss:BooleanGeometry::combine<BRepAlgoAPI_Common>(fullBoss,half);
            return BooleanGeometry::combine<BRepAlgoAPI_Fuse>(BooleanGeometry::combine<BRepAlgoAPI_Cut>(stock,hole),boss);
        };
        NativeDocumentSnapshot snapshot;QString error;QTemporaryDir temporary;QString document=path;
        if(document.isEmpty()) {
            // Exact analytic material of the private native document. Exercise
            // the native reader without committing or depending on its binary.
            NativePartState p;p.id="slot-4";p.name="Unite 5";
            check(BRepReconstruction::normalize(oracle(0),p.shape,error),qPrintable(error));snapshot.parts={p};snapshot.activePartId=p.id;
            document=temporary.filePath("crash-equivalent.odc");check(NativeDocumentStorage::save(document,snapshot,error),qPrintable(error));
        }
        QFile file(document);check(file.open(QIODevice::ReadOnly),"Cannot read crash document");const auto bytes=file.readAll();file.close();
        check(NativeDocumentStorage::load(document,snapshot,error),qPrintable(error));
        check(snapshot.parts.size()==1,"Crash document Part count changed");const auto source=snapshot.parts[0].shape;
        TopTools_IndexedMapOfShape sourceFaces;TopExp::MapShapes(source,TopAbs_FACE,sourceFaces);check(sourceFaces.Extent()==9,"Crash fixture must have the native merged end-face topology");
        check(BRepReconstruction::equivalent(source,oracle(0)),"Native crash geometry differs from independent oracle");
        const auto rightEnd=[&](const TopoDS_Shape& shape) {
            TopoDS_Face right;double end=-1.e100;
            for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
                const auto f=TopoDS::Face(it.Current());if(BRepAdaptor_Surface(f).GetType()!=GeomAbs_Plane) continue;
                gp_Pnt c;gp_Dir n;check(MoveFaceGeometry::reference(shape,f,c,n,error),qPrintable(error));
                if(n.X()>.99&&c.X()>end) {end=c.X();right=f;}
            }return right;
        };
        const auto target=rightEnd(source);check(!target.IsNull(),"No right-end planar face");
        near(BRepAdaptor_Surface(target).Plane().Location().X(),163,"Wrong crash face");
        double mass=0;check(BRepReconstruction::validSolid(source,mass),"Invalid native crash solid");
        std::cout<<"Native crash Part="<<snapshot.parts[0].id.toStdString()<<" right end x=163; R17 hole and clipped R18 boss at (158,36); volume="<<mass<<std::endl;
        if(displacement=="inspect") return;
        const auto stamp=serialized(source);
        const auto verify=[&](const TopoDS_Shape& shape,double d) {
            double actual=0;check(BRepReconstruction::validSolid(shape,actual),"Preview is not a certified closed solid");
            TopTools_IndexedMapOfShape edges;TopExp::MapShapes(shape,TopAbs_EDGE,edges);
            for(int i=1;i<=edges.Extent();++i) {
                int uses=0;for(TopExp_Explorer f(shape,TopAbs_FACE);f.More();f.Next())
                    for(TopExp_Explorer e(f.Current(),TopAbs_EDGE);e.More();e.Next()) if(e.Current().IsSame(edges(i))) ++uses;
                check(uses==2,"Nonmanifold preview edge");
            }
            const double a=std::min(18.,5+d),pi=std::numbers::pi;
            const double area=18*18*(pi/2+std::asin(a/18))+a*std::sqrt(std::max(0.,18*18-a*a));
            const double expectedVolume=(163+d)*80*60-pi*17*17*60+area*55;
            check(std::abs(actual-expectedVolume)<1.e-6*expectedVolume,qPrintable(QString("Wrong analytic volume at %1: actual=%2 expected=%3").arg(d).arg(actual,0,'g',15).arg(expectedVolume,0,'g',15)));
            check(BRepReconstruction::equivalent(shape,oracle(d)),"Preview changed unrelated material");
            for(double x:{10.37,54.37,140.37,157.37,162.37,165.37,170.37,175.37,180.37})
                for(double y:{10.61,25.61,35.61,50.61,70.61}) for(double z:{-.5,10.,59.5,60.5,90.,114.5,115.5}) {
                    const bool body=x>0&&x<163+d&&y>0&&y<80&&z>0&&z<60&&std::hypot(x-54,y-36)>17;
                    const bool boss=x<163+d&&std::hypot(x-158,y-36)<18&&z>60&&z<115;
                    BRepClass3d_SolidClassifier c(shape,gp_Pnt(x,y,z),1.e-7);
                    check(c.State()==(body||boss?TopAbs_IN:TopAbs_OUT),"Crash preview material occupancy mismatch");
                }
        };
        const auto distances=displacement=="regression"||displacement=="scan"?std::vector<double>{.1,1,2,5,10,12.9,13,13.1,20,40}:std::vector<double>{displacement.toDouble()};
        for(double d:distances) {
            std::cout<<"Isolated Move Face offset="<<d<<std::endl;const auto moved=MoveFaceGeometry::move(source,target,d,error);
            check(!moved.IsNull(),qPrintable(error));verify(moved,d);check(serialized(source)==stamp,"Move mutated original B-Rep");
        }
        for(double d:{-.1,-5.,-100.}) {
            check(MoveFaceGeometry::move(source,target,d,error).IsNull()&&!error.isEmpty(),"Unsupported inward cut was not refused");
            check(serialized(source)==stamp,"Refusal changed original B-Rep");
        }
        MainWindow window;window.resize(1200,900);window.show();events();auto& v=*window.findChild<CadViewport*>();
        check(v.importDocumentSnapshot(snapshot,error),qPrintable(error));v.view_->SetProj(V3d_XposYnegZpos);v.fitSelection({0});events();
        const auto before=v.boxes_[0];const auto history=v.undoStack_.size();QAction* action=nullptr;
        for(auto* a:window.findChildren<QAction*>()) if(a->text()=="Move Face") action=a;
        check(action,"Move Face action missing");
        const auto start=[&] {select(v,0,rightEnd(v.boxes_[0].shape));action->trigger();check(v.commandSession_.has_value(),"Crash face cannot activate Move Face");};
        start();const auto grab=project(v,v.commandSession_->pivot.Translated(gp_Vec(v.commandSession_->direction)*15));
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(grab),QPointF(grab),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);v.mousePressEvent(&press);
        check(v.draggingFace_,"Crash face arrow cannot be dragged");
        const auto end=grab+QPoint(qRound(v.dragScreenX_*5/v.dragMmPerPixel_),qRound(v.dragScreenY_*5/v.dragMmPerPixel_));
        QMouseEvent drag(QEvent::MouseMove,QPointF(end),QPointF(end),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);v.mouseMoveEvent(&drag);
        check(v.commandSession_->valid&&std::abs(v.commandSession_->value-5)<.5,"Interactive crash-range preview failed");verify(v.commandSession_->candidate,v.commandSession_->value);
        check(!v.commandSession_->preview.IsNull()&&v.context_->IsDisplayed(v.commandSession_->preview),"Live preview not displayed");
        v.cancelCommandSession();check(v.boxes_[0].shape.IsSame(before.shape)&&v.undoStack_.size()==history,"Drag Cancel changed document/history");
        start();input(v,"5");check(v.commandSession_->valid,"Exact crash offset preview failed");verify(v.commandSession_->candidate,5);
        input(v,"-5");check(!v.commandSession_->valid,"Invalid preview is confirmable");
        input(v,"13");check(v.commandSession_->valid,"Full-cylinder transition preview failed");verify(v.commandSession_->candidate,13);
        input(v,"5");const auto previewCandidate=v.commandSession_->candidate;v.commandButtons_->button(QDialogButtonBox::Ok)->click();
        const auto committed=v.boxes_[0].shape;check(BRepReconstruction::equivalent(committed,previewCandidate),"Commit differs from crash preview");
        verify(v.boxes_[0].shape,5);currentOwners(v,0);check(v.boxes_[0].persistentId==before.persistentId&&v.boxes_[0].presentation==before.presentation,"Crash fix changed Part identity");
        v.undoPull();check(v.boxes_[0].shape.IsSame(before.shape),"Crash edit Undo failed");v.redoPull();check(v.boxes_[0].shape.IsSame(committed),"Crash edit Redo failed");
        start();input(v,"2");check(v.commandSession_->valid,"Repeated right-end preview failed");verify(v.commandSession_->candidate,7);v.cancelCommandSession();
        check(file.open(QIODevice::ReadOnly)&&file.readAll()==bytes,"Original crash document was modified");
        std::cout<<"Isolated native crash geometry, drag, preview, Cancel and Undo/Redo regression passed\n";
    }
    static void currentOwners(CadViewport& v,size_t index) {
        TopTools_IndexedMapOfShape faces;TopExp::MapShapes(v.boxes_[index].shape,TopAbs_FACE,faces);
        const auto selection=v.boxes_[index].presentation->Selection(AIS_Shape::SelectionMode(TopAbs_FACE));
        int current=0,stale=0;
        for(const auto& entity:selection->Entities()) {
            const auto owner=Handle(StdSelect_BRepOwner)::DownCast(entity->BaseSensitive()->OwnerId());
            if(owner.IsNull()||owner->Shape().ShapeType()!=TopAbs_FACE) continue;
            if(faces.Contains(owner->Shape())) ++current;else ++stale;
        }
        std::cout<<"Post-replacement face owners: current="<<current<<" stale="<<stale<<std::endl;
        check(current>0&&stale==0,"AIS selection contains face owners from replaced geometry");
    }
    static TopoDS_Face radiusFace(const TopoDS_Shape& shape,double radius) {
        for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(face);
            if(s.GetType()==GeomAbs_Cylinder&&std::abs(s.Cylinder().Radius()-radius)<1.e-7) return face;
        }
        return {};
    }
    static void openNotchSession(const QString& path) {
        NativeDocumentSnapshot snapshot;QString error;QByteArray bytes;
        if(!path.isEmpty()) {
            QFile file(path);check(file.open(QIODevice::ReadOnly),"Cannot read native notch replay document");bytes=file.readAll();file.close();
            check(NativeDocumentStorage::load(path,snapshot,error),qPrintable(error));
        } else {
            const auto stock=BRepPrimAPI_MakeBox(163,80,60).Shape();
            const auto hole=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(54,36,0),gp_Dir(0,0,1)),17,60).Shape();
            const auto boss=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(121,36,60),gp_Dir(0,0,1)),18,55).Shape();
            NativePartState p;p.id="open-notch-part";p.name="Notch replay";p.shape=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(BooleanGeometry::combine<BRepAlgoAPI_Cut>(stock,hole),boss);
            p.transform.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),.3);p.transform.SetScaleFactor(1.5);p.transform.SetTranslationPart(gp_Vec(40,-30,25));
            snapshot.parts={p};
        }
        check(!snapshot.parts.empty(),"No Part in notch document");
        const auto initial=snapshot.parts[0].shape;const auto holeFace=radiusFace(initial,17);check(!holeFace.IsNull(),"No native R17 hole");
        const auto cylinder=BRepAdaptor_Surface(holeFace).Cylinder();const double oldX=cylinder.Location().X(),oldY=cylinder.Location().Y();
        check(cylinder.Axis().Direction().IsParallel(gp_Dir(0,0,1),1.e-8),"Unexpected native hole axis");
        double left=1.e100;
        for(TopExp_Explorer it(initial,TopAbs_FACE);it.More();it.Next()) {
            const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);if(s.GetType()!=GeomAbs_Plane) continue;
            auto n=s.Plane().Axis().Direction();if(f.Orientation()==TopAbs_REVERSED) n.Reverse();
            if(n.X()<-.99) left=std::min(left,s.Plane().Location().X());
        }
        check(std::isfinite(left)&&left<oldX,"Cannot identify native exterior side");
        const auto oldTool=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(oldX,oldY,0),gp_Dir(0,0,1)),17,60).Shape();
        TopoDS_Shape body;check(BRepReconstruction::normalize(BooleanGeometry::combine<BRepAlgoAPI_Fuse>(initial,oldTool),body,error,true),qPrintable(error));
        const auto oracle=[&](double x,double y){return BooleanGeometry::combine<BRepAlgoAPI_Cut>(body,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x,y,0),gp_Dir(0,0,1)),17,60).Shape());};
        check(BRepReconstruction::equivalent(initial,oracle(oldX,oldY)),"Native original hole is not the expected complete through-void");
        const auto verify=[&](const TopoDS_Shape& shape,double x,double y) {
            double mass=0;check(BRepReconstruction::validSolid(shape,mass),"Notch preview is not a certified solid");
            check(BRepReconstruction::equivalent(shape,oracle(x,y)),"Notch preview changed native boss or other material");
            const double d=std::clamp(x-left,-17.,17.);const double area=17*17*(std::numbers::pi/2+std::asin(d/17))+d*std::sqrt(std::max(0.,17*17-d*d));
            near(mass,volume(body)-area*60,"Wrong analytic notch volume");
            for(int i=-15;i<185;i+=17) for(int j=-4;j<85;j+=17) for(double z:{-1.,15.,59.,61.,90.,114.,116.}) {
                const gp_Pnt p(i+.27,j+.41,z);BRepClass3d_SolidClassifier b(body,p,1.e-7),result(shape,p,1.e-7);
                const bool expected=b.State()==TopAbs_IN&&!(z>0&&z<60&&std::hypot(p.X()-x,p.Y()-y)<17);
                check(result.State()==(expected?TopAbs_IN:TopAbs_OUT),"Native notch occupancy mismatch");
            }
        };
        MainWindow window;window.resize(1200,900);window.show();events();auto& v=*window.findChild<CadViewport*>();
        check(v.importDocumentSnapshot(snapshot,error),qPrintable(error));v.view_->SetProj(V3d_XnegYnegZpos);v.fitSelection({0});events();
        const auto before=v.boxes_[0];const auto stamp=serialized(before.shape);const auto history=v.undoStack_.size();
        auto* tree=window.findChild<QTreeWidget*>();auto* row=tree->topLevelItem(0)->child(0);const double scale=std::abs(before.orientation.ScaleFactor());
        const auto other=v.boxes_.size()>1?v.boxes_[1].shape:TopoDS_Shape();
        QAction* action=nullptr;for(auto* a:window.findChildren<QAction*>()) if(a->text()=="Move Face") action=a;
        check(action,"Move Face action missing");std::vector<TopoDS_Shape> states{before.shape};double x=oldX,y=oldY;
        const auto start=[&](int direction) {
            select(v,0,radiusFace(v.boxes_[0].shape,17));action->trigger();
            check(v.commandSession_&&v.commandSession_->moveFeature==CadViewport::MoveFaceFeature::ThroughHole&&v.commandSession_->changes.front().partIndex==0,"Current notch cannot activate Move Face");
            check(!v.cylinderBossBoundary_->isVisible(),"Notch incorrectly exposes boss Trim/Extend");v.cylinderMoveDirection_->setCurrentIndex(direction);
        };
        const auto commit=[&](int direction,double displacement) {
            start(direction);input(v,QLocale().toString(displacement*scale,'g',15));
            check(v.commandSession_->valid,qPrintable(v.commandSession_->geometryError));
            if(direction==1) x+=displacement;else y+=displacement;verify(v.commandSession_->candidate,x,y);
            v.commandButtons_->button(QDialogButtonBox::Ok)->click();check(!v.commandSession_,"Notch OK did not close command");
            verify(v.boxes_[0].shape,x,y);states.push_back(v.boxes_[0].shape);currentOwners(v,0);
        };
        commit(1,left-oldX);const auto open=v.boxes_[0].shape;
        // Pick a current sensitive entity in the viewport, then activate from
        // its context menu. Neither an old face nor a fabricated owner is used.
        v.selectParts({});bool picked=false;
        for(TopExp_Explorer it(open,TopAbs_FACE);it.More()&&!picked;it.Next()) {
            const auto face=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(face);
            if(s.GetType()!=GeomAbs_Cylinder||std::abs(s.Cylinder().Radius()-17)>1.e-7) continue;
            for(double fraction:{.98,.85,.5}) for(int u=0;u<40&&!picked;++u) {
                const auto point=s.Value(s.FirstUParameter()+(u+.37)/40*(s.LastUParameter()-s.FirstUParameter()),s.FirstVParameter()+fraction*(s.LastVParameter()-s.FirstVParameter())).Transformed(before.orientation);
                const auto screen=project(v,point);v.context_->MoveTo(screen.x(),screen.y(),v.view_,Standard_False);
                if(!v.context_->HasDetected()) continue;const auto owner=Handle(StdSelect_BRepOwner)::DownCast(v.context_->DetectedOwner());
                if(!owner.IsNull()&&owner->Shape().IsSame(face)) {v.context_->SelectDetected(AIS_SelectionScheme_Replace);picked=true;}
            }
        }
        check(picked&&v.selectionContext().kind==CadViewport::SelectionContext::Kind::Faces,"Current open notch is not selectable in viewport");
        std::unique_ptr<QMenu> menu(v.createSelectionMenu(QPoint()));auto* move=menu->findChild<QAction*>("context.move_face");
        check(move&&move->isEnabled(),"Open-notch context Move Face is unavailable");move->trigger();check(v.commandSession_.has_value(),"Viewport notch activation failed");
        v.cylinderMoveDirection_->setCurrentIndex(1);
        const auto grab=project(v,v.commandSession_->pivot.Translated(gp_Vec(v.commandSession_->direction)*15));
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(grab),QPointF(grab),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);v.mousePressEvent(&press);check(v.draggingFace_,"Notch lateral arrow did not grab");
        const auto end=grab-QPoint(qRound(v.dragScreenX_*5/v.dragMmPerPixel_),qRound(v.dragScreenY_*5/v.dragMmPerPixel_));
        QMouseEvent drag(QEvent::MouseMove,QPointF(end),QPointF(end),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);v.mouseMoveEvent(&drag);
        check(v.commandSession_->valid&&v.commandSession_->value<0&&!v.commandSession_->preview.IsNull(),"Open notch drag has no reconstructed live preview");
        verify(v.commandSession_->candidate,left+v.commandSession_->value/scale,y);
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(v.dragDimension_,&escape);
        check(!v.commandSession_&&v.boxes_[0].shape.IsSame(open)&&v.undoStack_.size()==history+1,"Notch drag/Escape changed document or history");
        commit(1,-5);commit(2,7);commit(1,28-x);commit(1,left-x);commit(2,-4);
        // An unchanged complete boss can still move laterally. Existing partial
        // or overhanging bosses retain their supported planar cap edit instead.
        const auto current=v.boxes_[0].shape;const auto boss=radiusFace(current,18);check(!boss.IsNull(),"Unrelated boss disappeared");
        if(path.isEmpty()||path.endsWith("moveCylinder.odc")) {
            select(v,0,boss);action->trigger();check(v.commandSession_&&v.commandSession_->moveFeature==CadViewport::MoveFaceFeature::Boss,"Unchanged boss lost Move Face");
            v.cylinderMoveDirection_->setCurrentIndex(1);input(v,QLocale().toString(-2*scale,'g',15));check(v.commandSession_->valid,"Unchanged boss preview failed after repeated notch edits");v.cancelCommandSession();
        }
        TopoDS_Face cap;for(TopExp_Explorer it(current,TopAbs_FACE);it.More();it.Next()) {
            const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);
            if(s.GetType()==GeomAbs_Plane&&s.Plane().Axis().Direction().IsParallel(gp_Dir(0,0,1),1.e-8)&&std::abs(s.Plane().Location().Z()-115)<1.e-6) {cap=f;break;}
        }
        select(v,0,cap);action->trigger();check(v.commandSession_.has_value(),"Unchanged boss cap lost Move Face");input(v,QLocale().toString(scale,'g',15));check(v.commandSession_->valid,"Boss cap cannot preview after notch edits");v.cancelCommandSession();
        for(size_t i=states.size()-1;i>0;--i) {v.undoPull();check(v.boxes_[0].shape.IsSame(states[i-1]),"Repeated notch Undo did not restore cached source");}
        check(serialized(before.shape)==stamp,"Notch operations mutated original Part");currentOwners(v,0);
        for(size_t i=1;i<states.size();++i) {v.redoPull();check(v.boxes_[0].shape.IsSame(states[i]),"Repeated notch Redo rebuilt or lost cached geometry");}
        currentOwners(v,0);start(1);v.cancelCommandSession();
        check(v.boxes_[0].persistentId==before.persistentId&&v.boxes_[0].presentation==before.presentation&&v.boxes_[0].name==before.name&&v.boxes_[0].color==before.color&&tree->topLevelItem(0)->child(0)==row,"Notch edits changed Part/browser identity or appearance");
        for(int r=1;r<=3;++r) for(int c=1;c<=4;++c) near(v.boxes_[0].orientation.Value(r,c),before.orientation.Value(r,c),"Notch edit changed placement");
        if(!other.IsNull()) check(v.boxes_[1].shape.IsSame(other),"Notch operation changed another Part");
        if(!path.isEmpty()) {QFile file(path);check(file.open(QIODevice::ReadOnly)&&file.readAll()==bytes,"Original native document was modified");}
        std::cout<<"Repeated open-notch viewport, drag, numeric input, Cancel and cached Undo/Redo passed\n";
    }
    static void afterBoundary(const QString& path) {
        NativeDocumentSnapshot snapshot;QString error;QByteArray bytes;
        double left=-8,right=163,bossX=98,holeY=48;TopoDS_Shape nativeSource;double nativeX=0,nativeY=0;
        if(!path.isEmpty()) {
            QFile file(path);check(file.open(QIODevice::ReadOnly),"Cannot read boundary document");bytes=file.readAll();file.close();
            check(NativeDocumentStorage::load(path,snapshot,error),qPrintable(error));check(!snapshot.parts.empty(),"Empty native boundary document");
            nativeSource=snapshot.parts[0].shape;const auto boss=radiusFace(nativeSource,18),hole=radiusFace(nativeSource,17);
            check(!boss.IsNull()&&!hole.IsNull(),"Native replay requires R18 boss and R17 hole");
            const auto cylinder=BRepAdaptor_Surface(boss).Cylinder();nativeX=cylinder.Location().X();nativeY=cylinder.Location().Y();
            holeY=BRepAdaptor_Surface(hole).Cylinder().Location().Y();bossX=std::min(nativeX,121.);left=1.e100;right=-1.e100;
            for(TopExp_Explorer it(nativeSource,TopAbs_FACE);it.More();it.Next()) {
                const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface surface(f);if(surface.GetType()!=GeomAbs_Plane) continue;
                auto n=surface.Plane().Axis().Direction();if(f.Orientation()==TopAbs_REVERSED) n.Reverse();
                if(n.X()<-.99) left=std::min(left,surface.Plane().Location().X());
                if(n.X()>.99) right=std::max(right,surface.Plane().Location().X());
            }
            check(std::isfinite(left)&&left<bossX,"Cannot identify native left exterior plane");
            if(snapshot.parts.size()==1) {
                NativePartState other;other.id="replay-other";other.name="Replay other Part";other.shape=BRepPrimAPI_MakeBox(100,80,60).Shape();
                other.transform.SetTranslation(gp_Vec(223,0,0));snapshot.parts.push_back(other);
            }
        }
        const auto stock=BRepPrimAPI_MakeBox(gp_Pnt(left,0,0),right-left,80,60).Shape();
        const auto bore=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(54,holeY,0),gp_Dir(0,0,1)),17,60).Shape();
        const auto outside=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(bossX,7,60),gp_Dir(0,0,1)),18,55).Shape();
        const auto originalBoss=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(bossX,36,60),gp_Dir(0,0,1)),18,55).Shape();
        const auto stockWithHole=BooleanGeometry::combine<BRepAlgoAPI_Cut>(stock,bore);
        if(!nativeSource.IsNull()) {
            const auto tool=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(nativeX,nativeY,60),gp_Dir(0,0,1)),18,55).Shape();
            check(BRepReconstruction::equivalent(BooleanGeometry::combine<BRepAlgoAPI_Cut>(nativeSource,tool),stockWithHole),"Native healed support differs from independent box/hole oracle");
            std::cout<<"Native boss replay: x="<<nativeX<<" y="<<nativeY<<" support x=["<<left<<","<<right<<"]"<<std::endl;
        }
        if(path.isEmpty()) {
            NativePartState first;first.id="boundary-part";first.name="Unite 5";
            first.shape=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(stockWithHole,outside);
            NativePartState second;second.id="other-part";second.name="Part 6";second.shape=BRepPrimAPI_MakeBox(100,80,60).Shape();
            second.transform.SetTranslation(gp_Vec(223,0,0));snapshot.parts={first,second};snapshot.activePartId=second.id;
        }
        MainWindow window;window.resize(1200,900);window.show();events();auto& v=*window.findChild<CadViewport*>();
        QAction* action=nullptr;for(auto* a:window.findChildren<QAction*>()) if(a->text()=="Move Face") action=a;
        check(action,"Move Face command missing");
        // Loaded result is valid and all its planes are eligible independently
        // of cylindrical capability. No saved session state is involved.
        for(TopExp_Explorer it(snapshot.parts[0].shape,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());if(BRepAdaptor_Surface(face).GetType()!=GeomAbs_Plane) continue;
            gp_Pnt p;gp_Dir n;check(MoveFaceGeometry::reference(snapshot.parts[0].shape,face,p,n,error),"Native boundary result disables planar recognition");
        }
        const auto saved=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(stockWithHole,outside);
        const auto interior=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(stockWithHole,originalBoss);
        if(!nativeSource.IsNull()) {
            // Exercise the actual loaded mantle first, with no assumed Part
            // count, former axis or saved primitive position. Cancel is read-only.
            check(v.importDocumentSnapshot(snapshot,error),qPrintable(error));
            select(v,0,radiusFace(nativeSource,18));action->trigger();check(v.commandSession_.has_value(),"Actual native boundary boss cannot activate Move Face");
            v.cylinderMoveDirection_->setCurrentIndex(1);input(v,QLocale().toString((bossX-nativeX)*std::abs(v.boxes_[0].orientation.ScaleFactor()),'g',15));
            check(v.commandSession_->valid,qPrintable(v.commandSession_->geometryError));
            auto feature=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(bossX,nativeY,60),gp_Dir(0,0,1)),18,55).Shape();
            check(BRepReconstruction::equivalent(v.commandSession_->candidate,BooleanGeometry::combine<BRepAlgoAPI_Fuse>(stockWithHole,feature)),"Native boss preview differs from independent material oracle");
            v.cancelCommandSession();check(v.boxes_[0].shape.IsSame(nativeSource),"Native boss Cancel changed source");
        }
        for(bool trim:{false,true}) {
            // Replay an actual OK from an interior boss using the exact saved
            // document's supporting stock/hole, preserving its Part ID/placement.
            snapshot.parts[0].shape=interior;
            check(v.importDocumentSnapshot(snapshot,error),qPrintable(error));v.view_->SetProj(V3d_XposYnegZpos);v.fitSelection({0});events();
            const auto before=v.boxes_[0];const auto stamp=serialized(before.shape);
            auto* tree=window.findChild<QTreeWidget*>();auto* row=tree->topLevelItem(0)->child(0);
            select(v,0,radiusFace(before.shape,18));action->trigger();
            check(v.commandSession_.has_value(),"Interior boss session failed");
            v.cylinderMoveDirection_->setCurrentIndex(2);v.cylinderBossBoundary_->setCurrentIndex(trim?1:0);input(v,"-29");
            check(v.commandSession_->valid,"Boundary crossing preview failed");
            const auto candidate=v.commandSession_->candidate;
            if(!trim) check(BRepReconstruction::equivalent(candidate,saved),"Exact saved Extend result was not reproduced");
            v.commandButtons_->button(QDialogButtonBox::Ok)->click();events();
            check(!v.commandSession_&&v.boxes_[0].persistentId==before.persistentId&&v.boxes_[0].presentation==before.presentation,"OK replaced Part identity or left session active");
            check(BRepReconstruction::equivalent(candidate,v.boxes_[0].shape),"Committed boundary result differs from preview");
            check(v.boxes_[0].presentation->Shape().IsSame(v.boxes_[0].shape),"AIS presentation has old B-Rep");
            currentOwners(v,0);
            const auto boundaryStamp=serialized(v.boxes_[0].shape);
            for(TopExp_Explorer it(v.boxes_[0].shape,TopAbs_FACE);it.More();it.Next()) {
                const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);
                if(s.GetType()!=GeomAbs_Plane) continue;
                gp_Pnt center;gp_Dir normal;QString reason;
                check(MoveFaceGeometry::reference(v.boxes_[0].shape,f,center,normal,reason),qPrintable(reason));
                select(v,0,f);std::unique_ptr<QMenu> menu(v.createSelectionMenu(QPoint()));
                auto* move=menu->findChild<QAction*>("context.move_face");
                check(move&&move->isEnabled(),"One boundary boss disabled another plane's context action");
                // These axial faces failed the old global offset + planar-cell
                // paths. Exercise both signed previews, not just recognition.
                if(std::abs(normal.Z())>.99) {
                    move->trigger();check(v.commandSession_.has_value(),"Post-boundary planar context activation failed");
                    for(const auto value:{"2","-2"}) {input(v,value);check(v.commandSession_->valid,qPrintable(QString("Plane z=%1 offset=%2 Trim=%3: %4").arg(s.Plane().Location().Z()).arg(value).arg(trim).arg(v.commandSession_->geometryError)));}
                    v.cancelCommandSession();
                }
            }
            check(serialized(v.boxes_[0].shape)==boundaryStamp,"Per-face preview/Cancel changed boundary geometry");
            // Commit a formerly failing bottom-face edit, then test cached
            // Undo/Redo and repeated activation on the newly reconstructed B-Rep.
            const auto bottom=[&] {
                for(TopExp_Explorer it(v.boxes_[0].shape,TopAbs_FACE);it.More();it.Next()) {
                    const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);
                    if(s.GetType()==GeomAbs_Plane&&s.Plane().Axis().Direction().IsParallel(gp_Dir(0,0,1),1.e-8)&&s.Plane().Location().Z()<1) return f;
                }return TopoDS_Face();
            };
            select(v,0,bottom());action->trigger();input(v,"2");check(v.commandSession_->valid,"Bottom preview failed");
            const auto bottomResult=v.commandSession_->candidate;v.commandButtons_->button(QDialogButtonBox::Ok)->click();currentOwners(v,0);
            select(v,0,bottom());action->trigger();input(v,"1");check(v.commandSession_->valid,"Repeated bottom edit failed");v.cancelCommandSession();
            v.undoPull();check(BRepReconstruction::equivalent(v.boxes_[0].shape,candidate),"Planar Undo lost boss result");
            v.redoPull();check(BRepReconstruction::equivalent(v.boxes_[0].shape,bottomResult),"Planar Redo lost result");
            select(v,0,bottom());action->trigger();check(v.commandSession_.has_value(),"Planar activation failed after Redo");v.cancelCommandSession();
            v.undoPull();
            // Fresh real viewport picking must resolve an unchanged planar face
            // to a member of the new B-Rep, not an obsolete face owner.
            TopoDS_Face planar;
            for(TopExp_Explorer it(v.boxes_[0].shape,TopAbs_FACE);it.More();it.Next()) {
                const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);
                if(s.GetType()==GeomAbs_Plane&&std::abs(s.Plane().Axis().Direction().X())>.99&&s.Plane().Distance(gp_Pnt(right,0,0))<1.e-6) {planar=f;break;}
            }
            check(!planar.IsNull(),"Unchanged side plane disappeared");
            v.selectParts({});bool pickedPlane=false;const BRepAdaptor_Surface planeSurface(planar);
            for(double u:{.5,.2,.8}) for(double w:{.5,.2,.8}) {
                const auto p=planeSurface.Value(planeSurface.FirstUParameter()+u*(planeSurface.LastUParameter()-planeSurface.FirstUParameter()),
                    planeSurface.FirstVParameter()+w*(planeSurface.LastVParameter()-planeSurface.FirstVParameter()));
                const auto point=project(v,p.Transformed(v.boxes_[0].orientation));v.context_->MoveTo(point.x(),point.y(),v.view_,Standard_False);
                if(!v.context_->HasDetected()) continue;const auto owner=Handle(StdSelect_BRepOwner)::DownCast(v.context_->DetectedOwner());
                if(!owner.IsNull()&&owner->Shape().IsSame(planar)) {v.context_->SelectDetected(AIS_SelectionScheme_Replace);pickedPlane=true;break;}
            }
            check(pickedPlane,"Committed planar face is not pickable through current owners");
            check(v.selectionContext().kind==CadViewport::SelectionContext::Kind::Faces,"Fresh planar pick resolves stale topology");
            const auto picked=v.selectionContext().faces.front();
            check(picked.partIndex==0&&picked.face.IsSame(planar),"Planar test ray did not hit the intended unchanged plane");
            action->trigger();check(v.commandSession_&&v.commandSession_->moveFeature==CadViewport::MoveFaceFeature::Planar,"Planar Move Face activation rejected after boss commit");
            input(v,"2");check(v.commandSession_->valid,"Planar Move Face preview failed after boss commit");v.cancelCommandSession();
            select(v,0,radiusFace(v.boxes_[0].shape,18));action->trigger();
            check(v.commandSession_&&v.commandSession_->moveFeature==CadViewport::MoveFaceFeature::Boss,"Boundary boss cannot activate repeated Move Face");
            v.cylinderBossBoundary_->setCurrentIndex(trim?1:0);v.cylinderMoveDirection_->setCurrentIndex(2);
            const auto grab=project(v,v.commandSession_->pivot.Translated(gp_Vec(v.commandSession_->direction)*15));
            QMouseEvent press(QEvent::MouseButtonPress,QPointF(grab),QPointF(grab),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);v.mousePressEvent(&press);
            check(v.draggingFace_,"Repeated boss lateral arrow did not grab");
            const auto end=grab-QPoint(qRound(v.dragScreenX_*3/v.dragMmPerPixel_),qRound(v.dragScreenY_*3/v.dragMmPerPixel_));
            QMouseEvent drag(QEvent::MouseMove,QPointF(end),QPointF(end),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);v.mouseMoveEvent(&drag);
            check(v.commandSession_->valid&&!v.commandSession_->preview.IsNull(),"Repeated boss drag has no certified preview");
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(v.dragDimension_,&escape);
            check(!v.commandSession_&&BRepReconstruction::equivalent(v.boxes_[0].shape,candidate),"Repeated boss Escape changed geometry");
            std::vector<TopoDS_Shape> bossStates{v.boxes_[0].shape};double bossY=7;
            const auto bossOracle=[&](double y) {
                auto boss=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(bossX,y,60),gp_Dir(0,0,1)),18,55).Shape();
                if(trim) boss=BooleanGeometry::combine<BRepAlgoAPI_Common>(boss,BRepPrimAPI_MakeBox(gp_Pnt(left,0,60),right-left,80,55).Shape());
                return BooleanGeometry::combine<BRepAlgoAPI_Fuse>(stockWithHole,boss);
            };
            for(double destination:{3.,-5.,7.,36.,7.}) {
                select(v,0,radiusFace(v.boxes_[0].shape,18));action->trigger();check(v.commandSession_.has_value(),"Repeated boss lost activation");
                v.cylinderBossBoundary_->setCurrentIndex(trim?1:0);v.cylinderMoveDirection_->setCurrentIndex(2);
                input(v,QLocale().toString(destination-bossY,'g',15));
                check(v.commandSession_->valid,qPrintable(v.commandSession_->geometryError));
                check(BRepReconstruction::equivalent(v.commandSession_->candidate,bossOracle(destination)),"Repeated boss preview changed hole or support");
                v.commandButtons_->button(QDialogButtonBox::Ok)->click();currentOwners(v,0);
                bossStates.push_back(v.boxes_[0].shape);bossY=destination;
            }
            for(size_t i=bossStates.size()-1;i>0;--i) {v.undoPull();check(v.boxes_[0].shape.IsSame(bossStates[i-1]),"Repeated boss Undo lost cached source");}
            for(size_t i=1;i<bossStates.size();++i) {v.redoPull();check(v.boxes_[0].shape.IsSame(bossStates[i]),"Repeated boss Redo rebuilt geometry");}
            for(size_t i=bossStates.size()-1;i>0;--i) v.undoPull();
            check(v.boxes_[1].shape.IsSame(snapshot.parts[1].shape),"Repeated boss changed another Part");
            const auto startHole=[&] {
                select(v,0,radiusFace(v.boxes_[0].shape,17));action->trigger();
                check(v.commandSession_&&v.commandSession_->moveFeature==CadViewport::MoveFaceFeature::ThroughHole,"Unaffected hole disabled by boundary boss");
                v.cylinderMoveDirection_->setCurrentIndex(1);input(v,"3");
                check(v.commandSession_->valid,"Unaffected hole cannot preview after boundary crossing");
            };
            startHole();v.cancelCommandSession();check(BRepReconstruction::equivalent(v.boxes_[0].shape,candidate),"Hole Cancel changed boundary result");
            startHole();const auto holeCandidate=v.commandSession_->candidate;
            v.commandButtons_->button(QDialogButtonBox::Ok)->click();currentOwners(v,0);
            check(BRepReconstruction::equivalent(v.boxes_[0].shape,holeCandidate),"Hole commit differs from preview");
            startHole();v.cancelCommandSession(); // repeated supported edits stay available
            v.undoPull();check(BRepReconstruction::equivalent(v.boxes_[0].shape,candidate),"Hole Undo did not preserve boundary boss");
            v.undoPull();check(v.boxes_[0].shape.IsSame(before.shape)&&serialized(before.shape)==stamp,"Undo failed after boundary commit");currentOwners(v,0);
            v.redoPull();check(BRepReconstruction::equivalent(v.boxes_[0].shape,candidate),"Redo lost boundary result");currentOwners(v,0);
            v.redoPull();check(BRepReconstruction::equivalent(v.boxes_[0].shape,holeCandidate),"Hole Redo lost cached geometry");currentOwners(v,0);
            check(v.boxes_[0].persistentId==before.persistentId,"Undo/Redo changed UUID");
            check(v.boxes_[0].presentation==before.presentation&&v.boxes_[0].name==before.name&&
                v.boxes_[0].color==before.color&&v.boxes_[0].visible==before.visible&&tree->topLevelItem(0)->child(0)==row,
                "Boundary/planar edits changed appearance or browser identity");
            for(int r=1;r<=3;++r) for(int c=1;c<=4;++c)
                near(v.boxes_[0].orientation.Value(r,c),before.orientation.Value(r,c),"Boundary edit changed Part placement");
            // Another Part remains editable through the same dispatch.
            TopExp_Explorer other(v.boxes_[1].shape,TopAbs_FACE);select(v,1,TopoDS::Face(other.Current()));action->trigger();
            check(v.commandSession_&&v.commandSession_->moveFeature==CadViewport::MoveFaceFeature::Planar,"Other Part disabled by first Part edit");v.cancelCommandSession();
        }
        if(!path.isEmpty()) {QFile file(path);check(file.open(QIODevice::ReadOnly)&&file.readAll()==bytes,"Boundary bug document was modified");}
        std::cout<<"Committed boundary topology/selection regression passed\n";
    }
    static void documentRegression(const QString& path) {
        NativeDocumentSnapshot snapshot;QString error;QByteArray originalBytes;
        const auto stock=BRepPrimAPI_MakeBox(163,80,60).Shape();
        const auto bore=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(54,36,-10),gp_Dir(0,0,1)),17,80).Shape();
        const auto boss=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(121,36,60),gp_Dir(0,0,1)),18,55).Shape();
        const auto holeOnly=BRepAlgoAPI_Cut(stock,bore).Shape();
        const auto bossOnly=BRepAlgoAPI_Fuse(stock,boss).Shape();
        const auto combined=BRepAlgoAPI_Fuse(holeOnly,boss).Shape();
        if(!path.isEmpty()) {
            QFile file(path);check(file.open(QIODevice::ReadOnly),"Cannot read supplied document");originalBytes=file.readAll();file.close();
            check(NativeDocumentStorage::load(path,snapshot,error),qPrintable(error));
        } else {
            // Portable equivalent of moveCylinder.odc; no private binary fixture.
            NativePartState first;first.id="slot-4";first.name="Unite 5";first.shape=combined;
            NativePartState second;second.id="other-part";second.name="Part 6";
            second.shape=BRepPrimAPI_MakeBox(gp_Pnt(625,0,0),100,80,60).Shape();second.transform.SetTranslation(gp_Vec(-402,0,0));
            snapshot.parts={first,second};snapshot.activePartId=second.id;
        }
        check(snapshot.parts.size()==2,"Unexpected document Part count");
        check(BRepReconstruction::equivalent(snapshot.parts[0].shape,combined),"Document differs from the independent two-feature material oracle");
        near(volume(snapshot.parts[0].shape),163*80*60-std::numbers::pi*17*17*60+std::numbers::pi*18*18*55,"Wrong native combined material volume");
        // The same unchanged single-feature certificates remain the foundation
        // for recognizing each independent feature in the combined Part.
        check(bool(CylindricalMoveGeometry::moveThroughHole(holeOnly,mantle(holeOnly),gp_Vec(1,0,0))),"Isolated document hole is not supported");
        check(bool(CylindricalMoveGeometry::moveBoss(bossOnly,mantle(bossOnly),gp_Vec(1,0,0),CylindricalMoveGeometry::BossBoundaryMode::Extend)),"Isolated document boss is not supported");
        MainWindow window;window.resize(1200,900);window.show();events();auto& v=*window.findChild<CadViewport*>();
        check(v.importDocumentSnapshot(snapshot,error),qPrintable(error));
        check(v.activePartId()==snapshot.parts[1].id,"Native Active Part was not restored");
        const auto source=v.boxes_[0].shape;const auto stamp=serialized(source);
        const auto history=v.undoStack_.size();
        QAction* action=nullptr;
        for(auto* a:window.findChildren<QAction*>()) if(a->text()=="Move Face") action=a;
        check(action&&action->isEnabled(),"3D Modeling Move Face is unavailable");
        QString diagnostic;
        const auto connection=QObject::connect(&v,&CadViewport::statusMessage,&v,[&](const QString& text){diagnostic=text;});
        int count=0;
        v.view_->SetProj(V3d_XposYnegZpos);v.fitSelection({0});events();
        for(TopExp_Explorer it(source,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());const BRepAdaptor_Surface surface(face);
            if(surface.GetType()!=GeomAbs_Cylinder) continue;
            ++count;
            const bool isBoss=face.Orientation()==TopAbs_FORWARD;
            for(bool asBoss:{false,true}) {
                const auto result=asBoss?CylindricalMoveGeometry::moveBoss(source,face,gp_Vec(0,0,0),CylindricalMoveGeometry::BossBoundaryMode::Extend):
                    CylindricalMoveGeometry::moveThroughHole(source,face,gp_Vec(0,0,0));
                check(bool(result)==(asBoss==isBoss),"Independent feature recognition or kind classification failed");
            }
            // Real detected owners, not fabricated selection, for both the
            // through-hole wall and external boss wall.
            QPoint hit;bool found=false;
            for(double fraction:{.1,.5,.85,.98}) for(int u=0;u<48&&!found;++u) {
                const auto p=surface.Value(surface.FirstUParameter()+(u+.37)/48*(surface.LastUParameter()-surface.FirstUParameter()),
                    surface.FirstVParameter()+fraction*(surface.LastVParameter()-surface.FirstVParameter())).Transformed(v.boxes_[0].orientation);
                const auto screen=project(v,p);v.context_->MoveTo(screen.x(),screen.y(),v.view_,Standard_False);
                if(!v.context_->HasDetected()) continue;
                const auto owner=Handle(StdSelect_BRepOwner)::DownCast(v.context_->DetectedOwner());
                if(!owner.IsNull()&&owner->Shape().IsSame(face)) {hit=screen;found=true;}
            }
            check(found,"Document cylinder cannot be selected in viewport");
            v.context_->MoveTo(hit.x(),hit.y(),v.view_,Standard_False);v.context_->SelectDetected(AIS_SelectionScheme_Replace);
            const auto selected=v.selectionContext();
            check(selected.kind==CadViewport::SelectionContext::Kind::Faces&&selected.faces.size()==1&&selected.faces.front().partIndex==0,
                "Typed selection did not resolve document cylinder to its actual Part");
            std::unique_ptr<QMenu> menu(v.createSelectionMenu(hit));
            auto* move=menu?menu->findChild<QAction*>("context.move_face"):nullptr;
            check(move&&move->isEnabled(),"Independent cylinder cannot activate Move Face");
            check(!menu->findChild<QAction*>("context.rotate_face")&&!menu->findChild<QAction*>("context.align_face"),"Unsupported cylinder enabled planar actions");
            diagnostic.clear();action->trigger();
            check(v.commandSession_&&v.commandSession_->changes.front().partIndex==0,"Independent document feature did not start a session on its selected Part");
            v.cylinderMoveDirection_->setCurrentIndex(1);input(v,"5");
            const auto translatedBoss=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(126,36,60),gp_Dir(0,0,1)),18,55).Shape();
            const auto translatedBore=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(59,36,-10),gp_Dir(0,0,1)),17,80).Shape();
            const auto expected=isBoss?BRepAlgoAPI_Fuse(holeOnly,translatedBoss).Shape():
                BRepAlgoAPI_Fuse(BRepAlgoAPI_Cut(stock,translatedBore).Shape(),boss).Shape();
            preview(v,volume(expected));check(BRepReconstruction::equivalent(v.commandSession_->candidate,expected),"Independent native preview changed unrelated material");
            occupancy(v.commandSession_->candidate,gp_Pnt(54,36,30),false);
            occupancy(v.commandSession_->candidate,gp_Pnt(121,36,90),true);
            if(isBoss) {occupancy(v.commandSession_->candidate,gp_Pnt(104,36,90),false);occupancy(v.commandSession_->candidate,gp_Pnt(143,36,90),true);}
            else {occupancy(v.commandSession_->candidate,gp_Pnt(38,36,30),true);occupancy(v.commandSession_->candidate,gp_Pnt(74,36,30),false);}
            if(isBoss) {
                input(v,"42");preview(v,163*80*60-std::numbers::pi*17*17*60+std::numbers::pi*18*18*55);
                v.cylinderBossBoundary_->setCurrentIndex(1);
                preview(v,163*80*60-std::numbers::pi*17*17*60+std::numbers::pi*18*18*55/2);
                occupancy(v.commandSession_->candidate,gp_Pnt(170,36,90),false);
            }
            input(v,isBoss?"-67":"67");
            check(!v.commandSession_->valid&&v.commandSession_->candidate.IsNull()&&diagnostic.contains("unselected feature"),"Interacting native edit was not refused safely");
            v.cancelCommandSession();
            v.selectParts({});action->trigger();check(v.moveFacePicking_,"Command-first picking unavailable");
            diagnostic.clear();
            QMouseEvent press(QEvent::MouseButtonPress,QPointF(hit),QPointF(hit),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);v.mousePressEvent(&press);
            check(v.commandSession_.has_value(),"Viewport command-first path did not recognize independent feature");v.cancelCommandSession();
            check(v.boxes_[0].shape.IsSame(source)&&serialized(source)==stamp&&v.undoStack_.size()==history,"Document previews/cancellation changed geometry/history");
            std::cout<<"Native cylinder radius="<<surface.Cylinder().Radius()<<": selected feature moves independently, preserving unrelated material\n";
        }
        QObject::disconnect(connection);check(count==2,"Unexpected cylindrical face inventory");
        // The complete boss stays locally editable after the hole becomes an
        // open exterior notch: no whole-Part rectangular feature pattern exists.
        select(v,0,radiusFace(v.boxes_[0].shape,17));action->trigger();
        check(v.commandSession_.has_value(),"Cannot start boundary-hole edit");v.cylinderMoveDirection_->setCurrentIndex(1);input(v,"-54");
        check(v.commandSession_->valid,"Cannot preview open hole");v.commandButtons_->button(QDialogButtonBox::Ok)->click();currentOwners(v,0);
        const auto notchedBase=BRepAlgoAPI_Cut(stock,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,36,0),gp_Dir(0,0,1)),17,60).Shape()).Shape();
        const auto persistent=v.boxes_[0].persistentId;const auto presentation=v.boxes_[0].presentation;
        double bossX=121;
        for(double displacement:{5.,-3.}) {
            select(v,0,radiusFace(v.boxes_[0].shape,18));action->trigger();
            check(v.commandSession_&&v.commandSession_->moveFeature==CadViewport::MoveFaceFeature::Boss,"Current boss disabled by unrelated open notch");
            near(v.commandSession_->pivot.Z(),87.5,"Local boss manipulator has incorrect axial anchor");
            v.cylinderMoveDirection_->setCurrentIndex(1);input(v,QString::number(displacement));bossX+=displacement;
            const auto expected=BRepAlgoAPI_Fuse(notchedBase,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(bossX,36,60),gp_Dir(0,0,1)),18,55).Shape()).Shape();
            preview(v,volume(expected));check(BRepReconstruction::equivalent(expected,v.commandSession_->candidate),"Local boss preview changed notch/stock material");
            v.cylinderBossBoundary_->setCurrentIndex(1);preview(v,volume(expected)); // Trim/Extend coincide on supported interior footprint
            const auto candidate=v.commandSession_->candidate;v.commandButtons_->button(QDialogButtonBox::Ok)->click();currentOwners(v,0);
            check(BRepReconstruction::equivalent(candidate,v.boxes_[0].shape)&&v.boxes_[0].persistentId==persistent&&v.boxes_[0].presentation==presentation,
                "Local commit changed geometry/identity unexpectedly");
            occupancy(v.boxes_[0].shape,gp_Pnt(5,36,30),false);occupancy(v.boxes_[0].shape,gp_Pnt(45,36,30),true);
        }
        TopoDS_Face side;
        for(TopExp_Explorer it(v.boxes_[0].shape,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(face);
            if(s.GetType()==GeomAbs_Plane&&std::abs(s.Plane().Axis().Direction().X())>.99&&s.Plane().Distance(gp_Pnt(163,0,0))<1.e-6) {side=face;break;}
        }
        select(v,0,side);action->trigger();check(v.commandSession_&&v.commandSession_->moveFeature==CadViewport::MoveFaceFeature::Planar,"Planar capability was coupled to boss recognition");
        input(v,"2");check(v.commandSession_->valid,"Planar edit after open hole/local boss move failed");v.cancelCommandSession();
        if(!path.isEmpty()) {QFile file(path);check(file.open(QIODevice::ReadOnly)&&file.readAll()==originalBytes,"Supplied document bytes changed");}
        std::cout<<"Two-feature document selection/availability/recognition regression passed\n";
    }
    static QPoint project(CadViewport& v,const gp_Pnt& point) {
        Standard_Integer x,y;v.view_->Convert(point.X(),point.Y(),point.Z(),x,y);return {x,y};
    }
    static void select(CadViewport& v,size_t index,const TopoDS_Face& face) {
        v.selectParts({index});v.context_->ClearSelected(Standard_False);
        Handle(StdSelect_BRepOwner) owner=new StdSelect_BRepOwner(face,v.boxes_[index].presentation,5,Standard_True);
        v.context_->AddOrRemoveSelected(owner,Standard_False);
    }
    static size_t add(CadViewport& v,bool boss,const gp_Trsf& placement=gp_Trsf()) {
        v.addBox();const size_t index=v.boxes_.size()-1;auto state=v.boxes_[index];
        state.shape=fixture(boss);state.geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;
        state.orientation=placement;state.cylinderPrimitive=state.spherePrimitive=state.conePrimitive=false;
        Bnd_Box bounds;BRepBndLib::AddOptimal(state.shape,bounds,false,false);
        bounds.Get(state.min[0],state.min[1],state.min[2],state.max[0],state.max[1],state.max[2]);
        state.baseMin=state.min;state.baseMax=state.max;v.applyFaceState(index,state);
        v.fitSelection({index});return index;
    }
    static void input(CadViewport& v,const QString& text) {
        v.dragDimension_->setText(text);
        QMetaObject::invokeMethod(v.dragDimension_,"textEdited",Qt::DirectConnection,Q_ARG(QString,text));
    }
    static void panelBounds(CadViewport& v) {
        events();v.commandPanelPositioner_->update();events();
        const QRect viewport(v.mapToGlobal(QPoint()),v.size());
        check(viewport.contains(v.commandPanel_->frameGeometry()),"Cylindrical panel escapes viewport bounds");
        check(v.commandPanel_->windowType()==Qt::Tool,"Cylindrical controls replaced the native tool panel");
    }
    static void preview(CadViewport& v,double expectedVolume) {
        const auto& session=*v.commandSession_;
        check(session.valid&&!session.preview.IsNull()&&v.context_->IsDisplayed(session.preview),"Missing cylindrical live preview");
        check(session.preview->Shape().IsSame(session.candidate),"Preview shows a surface or stale candidate");
        near(volume(session.candidate),expectedVolume,"Wrong cylindrical preview volume");
        double vol=0;check(BRepReconstruction::validSolid(session.candidate,vol),"Preview is not a valid solid");
        check(!v.context_->IsDisplayed(session.faceBefore->presentation),"Source obscures candidate");
        Quantity_Color color;session.preview->Color(color);
        check(color.IsEqual(Quantity_Color(.15,.65,1.,Quantity_TOC_RGB)),"Preview lost blue style");
        check(session.preview->Transparency()==.25&&session.preview->DisplayMode()==AIS_Shaded,"Preview shading changed");
        TColStd_ListOfInteger modes;v.context_->ActivatedModes(session.preview,modes);check(modes.IsEmpty(),"Candidate participates in picking");
    }
    static void occupancy(const TopoDS_Shape& shape,const gp_Pnt& p,bool inside) {
        BRepClass3d_SolidClassifier classifier(shape,p,1.e-7);
        check(classifier.State()==(inside?TopAbs_IN:TopAbs_OUT),"Wrong cylindrical preview material occupancy");
    }
    static void run() {
        MainWindow window;window.resize(1200,900);window.show();events();
        auto& v=*window.findChild<CadViewport*>();
        QAction* action=nullptr;
        for(auto* a:window.findChildren<QAction*>()) if(a->text()=="Move Face") action=a;
        check(action,"Existing Move Face command missing");
        const double pi=std::numbers::pi;
        const auto hole=add(v,false);const auto holeSource=v.boxes_[hole].shape;const auto stamp=serialized(holeSource);
        select(v,hole,mantle(holeSource));
        std::unique_ptr<QMenu> menu(v.createSelectionMenu(QPoint()));
        check(menu&&menu->findChild<QAction*>("context.move_face"),"Cylinder absent from typed Move Face menu");
        check(!menu->findChild<QAction*>("context.rotate_face")&&!menu->findChild<QAction*>("context.align_face"),"Cylinder enabled planar Rotate/Align");
        menu->findChild<QAction*>("context.move_face")->trigger();
        check(v.commandSession_&&v.commandSession_->moveFeature==CadViewport::MoveFaceFeature::ThroughHole,"Through-hole was not recognized");
        check(!v.cylinderBossBoundary_->isVisible(),"Hole exposes boss policy");
        check(v.cylinderMoveDirection_->currentIndex()==0&&v.moveFaceArrow_.IsNull(),"Direction was chosen arbitrarily");
        check(!v.commandButtons_->button(QDialogButtonBox::Ok)->isEnabled(),"Missing direction is confirmable");
        input(v,"25");check(!v.commandSession_->valid&&v.commandSession_->candidate.IsNull(),"Distance alone chose an arbitrary direction");
        auto* directions=qobject_cast<QStandardItemModel*>(v.cylinderMoveDirection_->model());
        check(!directions->item(3)->isEnabled(),"Axial reference is selectable");
        v.cylinderMoveDirection_->setCurrentIndex(1);preview(v,240000-3000*pi);
        occupancy(v.commandSession_->candidate,gp_Pnt(30,40,15),true);
        occupancy(v.commandSession_->candidate,gp_Pnt(55,40,15),false);
        panelBounds(v);
        // Changing angle rotates the selected Part X reference toward Part Y.
        v.cylinderMoveAngle_->setValue(90);preview(v,240000-3000*pi);
        near(v.commandSession_->direction.X(),0,"Angle did not rotate lateral direction");near(v.commandSession_->direction.Y(),1,"Positive angle has wrong sense");
        occupancy(v.commandSession_->candidate,gp_Pnt(30,65,15),false);
        auto* angleEditor=v.cylinderMoveAngle_->findChild<QLineEdit*>();
        angleEditor->setText("-");
        QMetaObject::invokeMethod(angleEditor,"textEdited",Qt::DirectConnection,Q_ARG(QString,angleEditor->text()));
        check(!v.commandSession_->valid&&v.commandSession_->candidate.IsNull()&&v.moveFaceArrow_.IsNull(),"Invalid angle retained a preview or direction");
        v.cylinderMoveAngle_->setValue(90);
        QMetaObject::invokeMethod(angleEditor,"textEdited",Qt::DirectConnection,Q_ARG(QString,angleEditor->text()));
        preview(v,240000-3000*pi);
        v.cylinderMoveAngle_->setValue(0);input(v,"70");preview(v,240000-1500*pi);
        input(v,"100");preview(v,240000); // complete hole exit remains previewable
        input(v,"invalid");check(!v.commandSession_->valid&&v.commandSession_->preview.IsNull(),"Invalid input retained stale preview");
        check(v.context_->IsDisplayed(v.boxes_[hole].presentation),"Invalid input failed to restore source display");
        input(v,"25");preview(v,240000-3000*pi);
        check(serialized(holeSource)==stamp&&v.boxes_[hole].shape.IsSame(holeSource),"Preview changed source Part");
        // Rendered preview differs from its erased background, rather than
        // merely carrying a non-null AIS object with no visible triangles.
        Image_PixMap shown,hidden;check(v.view_->ToPixMap(shown,480,360),"Could not render cylinder preview");
        v.context_->Erase(v.commandSession_->preview,Standard_False);check(v.view_->ToPixMap(hidden,480,360),"Could not render preview background");
        int changed=0;
        for(int y=0;y<360;++y) for(int x=0;x<480;++x)
            if(shown.PixelColor(x,y).GetRGB().Distance(hidden.PixelColor(x,y).GetRGB())>.05) ++changed;
        check(changed>100,"Cylindrical candidate is not visibly rendered");
        v.context_->Display(v.commandSession_->preview,Standard_False);v.context_->Deactivate(v.commandSession_->preview);
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(v.cylinderMoveDirection_,&escape);
        check(!v.commandSession_&&v.boxes_[hole].shape.IsSame(holeSource),"Escape changed source geometry");

        v.setPartVisible(hole,false);const auto boss=add(v,true);const auto bossSource=v.boxes_[boss].shape;
        // An unrelated Active Part must not replace the explicitly selected target.
        v.setActivePart(v.partId(hole));select(v,boss,mantle(bossSource));action->trigger();
        check(v.commandSession_&&v.commandSession_->changes.front().partIndex==boss,"Active Part replaced selected boss");
        check(v.cylinderBossBoundary_->isVisible()&&v.cylinderBossBoundary_->currentIndex()==0,"Boss did not default visibly to Extend");
        v.cylinderMoveDirection_->setCurrentIndex(1);input(v,"70");preview(v,240000+2000*pi);
        occupancy(v.commandSession_->candidate,gp_Pnt(105,40,40),true);
        v.cylinderBossBoundary_->setCurrentIndex(1);preview(v,240000+1000*pi);
        occupancy(v.commandSession_->candidate,gp_Pnt(105,40,40),false);
        const auto trimmed=v.commandSession_->candidate;
        v.cylinderMoveAngle_->setValue(10);check(v.commandSession_->bossTrim,"Direction edit silently changed boss policy");
        v.cylinderMoveAngle_->setValue(0);check(BRepReconstruction::equivalent(trimmed,v.commandSession_->candidate),"Mode preview accumulated edits");
        input(v,"100");check(!v.commandSession_->valid&&!v.commandButtons_->button(QDialogButtonBox::Ok)->isEnabled(),"Disconnected boss can be confirmed");
        check(v.cylinderBossBoundary_->currentIndex()==1&&v.commandSession_->bossTrim,"Invalid geometry switched policy");
        input(v,"70");preview(v,240000+1000*pi);panelBounds(v);
        v.commandButtons_->button(QDialogButtonBox::Cancel)->click();
        check(!v.commandSession_&&v.boxes_[boss].shape.IsSame(bossSource),"Cancel changed boss geometry");
        // Command-first picking resolves the visible cylindrical owner through
        // the same viewport route as planar Move Face.
        v.selectParts({});action->trigger();check(v.moveFacePicking_,"Move Face did not arm command-first picking");
        bool picked=false;
        for(int i=0;i<16&&!picked;++i) {
            const double angle=2*pi*i/16;
            const auto point=project(v,gp_Pnt(30+10*std::cos(angle),40+10*std::sin(angle),40));
            v.context_->MoveTo(point.x(),point.y(),v.view_,Standard_False);
            if(!v.context_->HasDetected()) continue;
            const auto owner=Handle(StdSelect_BRepOwner)::DownCast(v.context_->DetectedOwner());
            if(owner.IsNull()||owner->Shape().ShapeType()!=TopAbs_FACE||BRepAdaptor_Surface(TopoDS::Face(owner->Shape())).GetType()!=GeomAbs_Cylinder) continue;
            QMouseEvent press(QEvent::MouseButtonPress,QPointF(point),QPointF(point),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            v.mousePressEvent(&press);picked=v.commandSession_.has_value();
        }
        check(picked&&v.commandSession_->moveFeature==CadViewport::MoveFaceFeature::Boss,"Command-first cylinder picking failed");
        check(v.cylinderBossBoundary_->currentIndex()==0,"New boss session did not reset visibly to Extend");v.cancelCommandSession();

        // Translated/rotated/scaled Part: signed millimeters are world distances,
        // while the reference axes and canonical geometry stay in the Part frame.
        gp_Trsf transform;transform.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),.5);
        transform.SetScaleFactor(2);transform.SetTranslationPart(gp_Vec(210,-100,70));
        v.setPartVisible(boss,false);const auto placed=add(v,false,transform);
        select(v,placed,mantle(v.boxes_[placed].shape));action->trigger();v.cylinderMoveDirection_->setCurrentIndex(1);input(v,"40");
        preview(v,240000-3000*pi);
        occupancy(v.commandSession_->candidate,gp_Pnt(50,40,15),false);
        near(gp_Vec(v.commandSession_->direction).Dot(gp_Vec(gp_Dir(1,0,0).Transformed(transform))),1,"Arrow is not in transformed Part direction");
        const auto expected=CylindricalMoveGeometry::moveThroughHole(v.boxes_[placed].shape,v.commandSession_->face,gp_Vec(20,0,0));
        check(expected&&BRepReconstruction::equivalent(expected.shape,v.commandSession_->candidate),"Part placement applied twice or world mm not converted");
        // Real viewport arrow drag reuses the planar manipulator hit testing.
        const auto grab=project(v,v.commandSession_->pivot.Translated(gp_Vec(v.commandSession_->direction)*55));
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(grab),QPointF(grab),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        v.mousePressEvent(&press);check(v.draggingFace_,"Lateral arrow did not start a drag");
        const auto end=grab+QPoint(qRound(v.dragScreenX_*8/v.dragMmPerPixel_),qRound(v.dragScreenY_*8/v.dragMmPerPixel_));
        QMouseEvent drag(QEvent::MouseMove,QPointF(end),QPointF(end),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);v.mouseMoveEvent(&drag);
        near(v.commandSession_->value,48,"Lateral drag uses wrong mm conversion");
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(end),QPointF(end),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);v.mouseReleaseEvent(&release);
        const auto committed=v.commandSession_->candidate;v.dragDimension_->setText("48");
        v.commandButtons_->button(QDialogButtonBox::Ok)->click();
        check(!v.commandSession_&&BRepReconstruction::equivalent(v.boxes_[placed].shape,committed),"Commit differs from reconstructed preview");
        check(!v.cylinderMoveDirection_->isVisible()&&!v.cylinderBossBoundary_->isVisible(),"Move controls leaked after session");

        // Unsupported cylinders report diagnostics without opening a session.
        v.addCylinder();const auto unsupported=v.boxes_.size()-1;
        QString diagnostic;QObject::connect(&v,&CadViewport::statusMessage,&v,[&](const QString& text){diagnostic=text;});
        select(v,unsupported,mantle(v.boxes_[unsupported].shape));action->trigger();
        check(!v.commandSession_&&diagnostic.contains("Unsupported cylinder"),"Unsupported cylinder opened a session or lost diagnostics");
        std::cout<<"Cylindrical Move Face selection, direction, policy and live previews passed\n";
    }
};
void runCylindricalMoveFaceSessionTests() {CylindricalMoveFaceSessionTest::run();}
void runCylindricalMoveDocumentTest(const QString& path) {CylindricalMoveFaceSessionTest::documentRegression(path);}
void runCylindricalMoveBoundaryTest(const QString& path) {CylindricalMoveFaceSessionTest::afterBoundary(path);}
void runCrashPlanarTest(const QString& path,const QString& displacement) {CylindricalMoveFaceSessionTest::crashPreview(path,displacement);}
void runOpenNotchSessionTest(const QString& path) {CylindricalMoveFaceSessionTest::openNotchSession(path);}
