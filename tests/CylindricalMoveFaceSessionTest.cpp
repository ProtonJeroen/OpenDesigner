#include "CadViewport.h"
#include "MainWindow.h"
#include "CylindricalMoveGeometry.h"
#include "BRepReconstruction.h"
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
#include <QFile>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
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
