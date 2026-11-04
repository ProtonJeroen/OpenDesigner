#include "CadViewport.h"
#include "MainWindow.h"
#include "MoveFaceGeometry.h"
#include "RotateMoveFixture.h"
#include <QApplication>
#include <QAction>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLineEdit>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTreeWidget>
#include <QLocale>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <Image_PixMap.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <sstream>
#include <iostream>
#include <cmath>
#include <stdexcept>
namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void near(double a,double b,const char* message) { require(std::abs(a-b)<1.e-5*std::max(1.,std::abs(b)),message); }
double volume(const TopoDS_Shape& shape) {
    GProp_GProps props; BRepGProp::VolumeProperties(shape,props); return props.Mass();
}
std::string brep(const TopoDS_Shape& shape) { std::ostringstream out; BRepTools::Write(shape,out); return out.str(); }
int countType(const TopoDS_Shape& shape,TopAbs_ShapeEnum type) {
    int count=0; for(TopExp_Explorer it(shape,type);it.More();it.Next()) ++count; return count;
}
TopoDS_Face cap(const TopoDS_Shape& shape,bool top) {
    TopoDS_Face result; double best=top?-1.e30:1.e30;
    for (TopExp_Explorer f(shape,TopAbs_FACE);f.More();f.Next()) {
        auto face=TopoDS::Face(f.Current()); BRepAdaptor_Surface s(face);
        if (s.GetType()!=GeomAbs_Plane || std::abs(s.Plane().Axis().Direction().Z())<0.999) continue;
        const double z=s.Plane().Location().Z();
        if ((top && z>best) || (!top && z<best)) {best=z;result=face;}
    }
    require(!result.IsNull(),"planar cap missing"); return result;
}
}
struct MoveFaceTest {
    static void geometry() {
        const double pi=std::acos(-1.0);
        const std::vector<TopoDS_Shape> shapes={BRepPrimAPI_MakeBox(100,80,60).Shape(),
            BRepPrimAPI_MakeCylinder(40,80).Shape(),BRepPrimAPI_MakeCone(40,20,80).Shape()};
        for (size_t i=0;i<shapes.size();++i) for (bool top:{false,true}) for (double d:{10.,-10.}) {
            const auto shape=shapes[i]; const auto original=brep(shape);
            QString error; const auto moved=MoveFaceGeometry::move(shape,cap(shape,top),d,error);
            if (moved.IsNull()) throw std::runtime_error("primitive cap: "+error.toStdString());
            require(BRepCheck_Analyzer(moved).IsValid(),"invalid cap result");
            if(i==0) require(countType(moved,TopAbs_FACE)==countType(shape,TopAbs_FACE)
                             &&countType(moved,TopAbs_EDGE)==countType(shape,TopAbs_EDGE)
                             &&countType(moved,TopAbs_VERTEX)==countType(shape,TopAbs_VERTEX),
                             "Move Face added or removed Box topology");
            if(i==0 && top) near(BRepAdaptor_Surface(cap(moved,true)).Plane().Location().Z(),
                                 BRepAdaptor_Surface(cap(shape,true)).Plane().Location().Z()+d,
                                 "Move Face did not translate the existing Box top support exactly");
            double expected=0;
            if (i==0) expected=100*80*(60+d);
            if (i==1) expected=pi*40*40*(80+d);
            if (i==2) {
                const double h=80+d;
                const double r=top?40:40+d/4, t=top?20-d/4:20;
                expected=pi*h*(r*r+r*t+t*t)/3;
            }
            near(volume(moved),expected,"incorrect moved cap volume");
            near(BRepAdaptor_Surface(cap(moved,!top)).Plane().Location().Z(),
                 BRepAdaptor_Surface(cap(shape,!top)).Plane().Location().Z(),"opposite cap moved");
            require(brep(shape)==original,"OCCT modified source B-Rep");
            require(MoveFaceGeometry::move(shape,cap(shape,top),-200,error).IsNull(),"collapse accepted");
            require(brep(shape)==original,"failed offset modified source");
        }
        // A genuinely non-box, concave Boolean solid: move the top of an L extrusion.
        const auto boolean=BRepAlgoAPI_Cut(shapes[0],BRepPrimAPI_MakeBox(gp_Pnt(50,40,-10),60,50,80).Shape()).Shape();
        QString error;
        const auto moved=MoveFaceGeometry::move(boolean,cap(boolean,true),12.25,error);
        if (moved.IsNull()) throw std::runtime_error("Boolean cap: "+error.toStdString());
        near(volume(moved),(8000-2000)*72.25,"incorrect concave Boolean result");
        // Curved selected faces and unsupported neighbours are refused.
        const auto sphere=BRepPrimAPI_MakeSphere(30).Shape();
        TopExp_Explorer sphereFaces(sphere,TopAbs_FACE);
        require(MoveFaceGeometry::move(sphere,TopoDS::Face(sphereFaces.Current()),10,error).IsNull(),"sphere face accepted");
        TopExp_Explorer cylinderFaces(shapes[1],TopAbs_FACE);
        while (cylinderFaces.More() && BRepAdaptor_Surface(TopoDS::Face(cylinderFaces.Current())).GetType()==GeomAbs_Plane) cylinderFaces.Next();
        require(MoveFaceGeometry::move(shapes[1],TopoDS::Face(cylinderFaces.Current()),10,error).IsNull(),"cylinder wall accepted");
        require(MoveFaceGeometry::move(shapes[0],cap(shapes[1],true),10,error).IsNull(),"foreign face accepted");
        const auto open=cap(shapes[0],true);
        require(MoveFaceGeometry::move(open,open,10,error).IsNull(),"open face accepted as solid");
        const auto halfSphere=BRepAlgoAPI_Cut(sphere,BRepPrimAPI_MakeBox(gp_Pnt(-40,-40,-40),80,80,40).Shape()).Shape();
        require(MoveFaceGeometry::move(halfSphere,cap(halfSphere,false),5,error).IsNull(),"planar face with spherical neighbour accepted");
        // OCCT may handle a planar through-hole cap natively; if it cannot, the
        // operation must refuse without mutating the source.
        const auto hole=BRepAlgoAPI_Cut(shapes[0],BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(50,40,-10),gp_Dir(0,0,1)),10,80).Shape()).Shape();
        const auto holeBefore=brep(hole);
        const auto movedHole=MoveFaceGeometry::move(hole,cap(hole,true),10,error);
        if(!movedHole.IsNull()) {
            require(BRepCheck_Analyzer(movedHole).IsValid(),"through-hole cap result is invalid");
            near(volume(movedHole),volume(hole)+10*(8000-std::acos(-1.0)*100),"through-hole cap volume is wrong");
        }
        require(brep(hole)==holeBefore,"through-hole operation mutated source");
        // A shape location is part of the B-Rep, independent of the instance transform.
        gp_Trsf translation; translation.SetTranslation(gp_Vec(31,7,19));
        const auto located=shapes[0].Moved(TopLoc_Location(translation));
        const auto locatedResult=MoveFaceGeometry::move(located,cap(located,true),10,error);
        require(!locatedResult.IsNull(),"located B-Rep rejected");
        near(volume(locatedResult),560000,"located shape was transformed twice");
    }
    static QPoint project(const CadViewport& v,const gp_Pnt& p) {
        Standard_Integer x,y; v.view_->Convert(p.X(),p.Y(),p.Z(),x,y);return QPoint(x,y);
    }
    static void selectFace(CadViewport& v,size_t index,const TopoDS_Face& face) {
        v.selectParts({index});
        v.context_->ClearSelected(Standard_False);
        Handle(StdSelect_BRepOwner) owner=new StdSelect_BRepOwner(face,v.boxes_[index].presentation,5,Standard_True);
        v.context_->AddOrRemoveSelected(owner,Standard_False);
    }
    static void visiblePreview(CadViewport& v) {
        const auto preview=v.commandSession_->preview;
        require(!preview.IsNull() && v.context_->IsDisplayed(preview),"candidate AIS is not displayed");
        require(preview->Shape().IsSame(v.commandSession_->candidate),"preview is not the complete reconstructed candidate");
        Image_PixMap shown,hidden;
        require(v.view_->ToPixMap(shown,640,480),"cannot render preview");
        v.context_->Erase(preview,Standard_False);
        require(v.view_->ToPixMap(hidden,640,480),"cannot render background");
        v.context_->Display(preview,Standard_False); v.context_->Deactivate(preview);
        require(!v.context_->IsDisplayed(v.commandSession_->faceBefore->presentation),"original solid obscures the candidate");
        require(preview->DisplayMode()==AIS_Shaded && preview->Transparency()>0 && preview->Transparency()<0.5,
            "candidate is not distinctly shaded and translucent");
        TColStd_ListOfInteger modes;v.context_->ActivatedModes(preview,modes);
        require(modes.IsEmpty(),"preview participates in operand picking");
        int changed=0,blue=0,triangulated=0,faces=0;
        for(TopExp_Explorer f(preview->Shape(),TopAbs_FACE);f.More();f.Next()) {
            ++faces; TopLoc_Location location;
            if(!BRep_Tool::Triangulation(TopoDS::Face(f.Current()),location).IsNull()) ++triangulated;
        }
        for(int y=0;y<480;++y) for(int x=0;x<640;++x) {
            const auto color=shown.PixelColor(x,y).GetRGB();
            if(color.Distance(hidden.PixelColor(x,y).GetRGB())>0.05) {
                ++changed;
                if(color.Blue()>color.Red()+0.1 && color.Green()>color.Red()+0.05) ++blue;
            }
        }
        require(changed>500 && blue>500,"candidate has no distinctly visible shaded solid");
        require(faces>0 && triangulated==faces,"preview has unmeshed faces");
        v.view_->Invalidate();v.view_->Redraw();
    }
    static void ui() {
        MainWindow window; window.show(); QApplication::processEvents();
        auto& v=*window.findChild<CadViewport*>();
        auto* tree=window.findChild<QTreeWidget*>(); auto* root=tree->topLevelItem(0);
        QAction* action=nullptr;
        for (auto* candidate:window.findChildren<QAction*>()) if (candidate->text()==QStringLiteral("Move Face")) action=candidate;
        require(action,"Move Face menu action missing");
        v.addBox();v.addCylinder();v.addCone();
        v.renamePart(0,QStringLiteral("Keep identity")); v.setPartColor(0,Qt::red);
        gp_Trsf rotation,shift;
        rotation.SetRotation(gp_Ax1(gp_Pnt(3,2,1),gp_Dir(1,2,3)),0.6);
        shift.SetTranslation(gp_Vec(15,-28,33));
        const gp_Trsf transform=shift*rotation;
        v.applyPartState(0,v.boxes_[0].min,v.boxes_[0].max,transform);
        const auto original=v.boxes_[0]; const auto originalBrep=brep(original.shape);
        const auto originalCount=v.boxes_.size(); const auto row=root->child(0);
        for (double distance:{12.25,-9.5}) {
            selectFace(v,0,cap(v.boxes_[0].shape,true));
            v.context_->InitSelected(); const auto owner=v.context_->SelectedOwner();
            const auto undo=v.undoStack_.size(),redo=v.redoStack_.size();
            action->trigger();
            require(v.commandSession_ && v.commandSession_->kind==CadViewport::CommandSession::Kind::MoveFace,"menu did not start face session");
            require(!v.moveFaceArrow_.IsNull(),"missing face normal arrow");
            v.dragDimension_->setText(QLocale().toString(distance,'f',2));
            QMetaObject::invokeMethod(v.dragDimension_,"textEdited",Qt::DirectConnection,Q_ARG(QString,v.dragDimension_->text()));
            require(v.commandSession_->valid && !v.commandSession_->preview.IsNull(),"missing valid geometry preview");
            visiblePreview(v);
            require(v.boxes_[0].shape.IsSame(original.shape) && brep(original.shape)==originalBrep,"preview modified document geometry");
            require(v.boxes_[0].presentation->Shape().IsSame(original.shape),"preview replaced original presentation shape");
            require(v.undoStack_.size()==undo && v.redoStack_.size()==redo,"preview changed history");
            require(row==root->child(0) && row->isSelected(),"preview changed browser identity/selection");
            near(volume(v.commandSession_->candidate),8000*(60+distance),"numeric preview distance wrong");
            v.commandButtons_->button(QDialogButtonBox::Cancel)->click();
            require(!v.commandSession_ && v.boxes_[0].shape.IsSame(original.shape),"Cancel changed canonical geometry");
            require(v.context_->IsDisplayed(original.presentation),"Cancel did not restore original display");
            v.context_->InitSelected();require(v.context_->SelectedOwner()==owner,"Cancel lost original face selection");
            require(v.undoStack_.size()==undo && v.redoStack_.size()==redo,"Cancel changed history");
            action->trigger(); v.dragDimension_->setText(QLocale().toString(distance,'f',2));
            v.commandButtons_->button(QDialogButtonBox::Ok)->click();
            require(!v.commandSession_ && v.undoStack_.size()==undo+1,"OK did not make one undo entry");
            require(v.boxes_.size()==originalCount && v.boxes_[0].presentation==original.presentation,"Move Face replaced part identity");
            require(v.boxes_[0].geometryKind==CadViewport::CadPart::GeometryKind::GeneralBRep,"direct edit remained parametrically rebuildable");
            near(volume(v.boxes_[0].shape),8000*(60+distance),"committed volume wrong");
            for (int r=1;r<=3;++r) for(int c=1;c<=4;++c)
                near(v.boxes_[0].orientation.Value(r,c),transform.Value(r,c),"Move Face moved whole part");
            require(v.partName(0)==QStringLiteral("Keep identity") && v.partColor(0)==QColor(Qt::red) && v.isPartVisible(0),"metadata changed");
            require(row==root->child(0) && row->isSelected(),"OK changed browser references");
            const auto after=v.boxes_[0].shape;
            v.undoPull(); require(v.boxes_[0].shape.IsSame(original.shape) && v.boxes_[0].geometryKind==original.geometryKind,"Undo lost original shape/type");
            require(row==root->child(0) && !row->isSelected() && tree->selectedItems().empty() && v.selectedPartIndices_.empty() && v.context_->NbSelected()==0,"Move Face Undo retained selection or changed browser row");
            v.redoPull(); require(v.boxes_[0].shape.IsSame(after),"Redo recalculated/lost cached result");
            require(tree->selectedItems().empty() && v.selectedPartIndices_.empty() && v.context_->NbSelected()==0,"Move Face Redo retained selection");
            v.undoPull();
        }
        // Real picking must resolve local faces even after a full instance rotation/translation.
        v.setPartVisible(1,false);v.setPartVisible(2,false);v.fitSelection({0});
        v.selectParts({});action->trigger();
        GProp_GProps transformedFace;
        BRepGProp::SurfaceProperties(cap(original.shape,true),transformedFace);
        const auto transformedClick=project(v,transformedFace.CentreOfMass().Transformed(transform));
        QMouseEvent transformedPress(QEvent::MouseButtonPress,QPointF(transformedClick),QPointF(transformedClick),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        v.mousePressEvent(&transformedPress);
        require(v.commandSession_ && v.commandSession_->faceBefore->shape.IsSame(original.shape),"transformed face picking failed");
        near(gp_Vec(v.commandSession_->direction).Dot(gp_Vec(gp_Dir(0,0,1).Transformed(transform))),1,"transformed face normal wrong");
        v.cancelCommandSession();v.setPartVisible(1,true);v.setPartVisible(2,true);
        // A zero confirmation preserves both shape type and the existing Redo branch.
        selectFace(v,0,cap(original.shape,true));action->trigger();
        const auto zeroUndo=v.undoStack_.size(),zeroRedo=v.redoStack_.size();
        v.dragDimension_->setText(QStringLiteral("0"));v.confirmCommandSession();
        require(v.boxes_[0].shape.IsSame(original.shape) && v.boxes_[0].geometryKind==original.geometryKind,"zero offset converted primitive");
        require(v.undoStack_.size()==zeroUndo && v.redoStack_.size()==zeroRedo,"zero offset changed history");
        // Invalid candidate must not allow OK or Enter to commit a previous good preview.
        selectFace(v,0,cap(v.boxes_[0].shape,true)); action->trigger();
        require(v.previewCommand(10),"valid candidate rejected");
        v.dragDimension_->setText(QStringLiteral("-200"));
        require(!v.updateCommandParameter(),"collapse accepted by session");
        require(!v.commandButtons_->button(QDialogButtonBox::Ok)->isEnabled(),"OK enabled for invalid result");
        const auto undo=v.undoStack_.size(); v.confirmCommandSession();
        require(v.commandSession_ && v.undoStack_.size()==undo,"invalid input committed old candidate");
        v.setRotateMode(true);
        require(!v.commandSession_ && v.boxes_[0].shape.IsSame(original.shape),"tool switch retained face preview");
        v.setRotateMode(false);
        // Cylinder/Cone direct edit converts metadata and Undo restores primitive Pull eligibility.
        for (size_t index:{size_t(1),size_t(2)}) {
            const auto before=v.boxes_[index];selectFace(v,index,cap(before.shape,true));action->trigger();
            v.dragDimension_->setText(QStringLiteral("10")); v.confirmCommandSession();
            require(!v.boxes_[index].cylinderPrimitive && !v.boxes_[index].conePrimitive,"direct edit retained stale primitive parameters");
            const auto edited=v.boxes_[index].shape; v.undoPull();
            require(v.boxes_[index].shape.IsSame(before.shape) && v.boxes_[index].cylinderPrimitive==before.cylinderPrimitive && v.boxes_[index].conePrimitive==before.conePrimitive,"Undo lost primitive metadata");
            v.redoPull();require(v.boxes_[index].shape.IsSame(edited),"primitive face Redo failed");
        }
        // Command-first surface picking and real arrow dragging on an untransformed box.
        v.addBox(); const size_t index=v.boxes_.size()-1; v.fitSelection({index});
        v.selectParts({}); action->trigger(); require(v.moveFacePicking_,"command-first selection was not armed");
        GProp_GProps props; BRepGProp::SurfaceProperties(cap(v.boxes_[index].shape,true),props);
        const auto click=project(v,props.CentreOfMass());
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(click),QPointF(click),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        v.mousePressEvent(&press);
        require(v.commandSession_ && v.commandSession_->kind==CadViewport::CommandSession::Kind::MoveFace,"viewport face picking did not start session");
        const auto grab=project(v,v.commandSession_->pivot.Translated(gp_Vec(v.commandSession_->direction)*15));
        QMouseEvent grabEvent(QEvent::MouseButtonPress,QPointF(grab),QPointF(grab),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        v.mousePressEvent(&grabEvent); require(v.draggingFace_,"face arrow did not start drag");
        const auto end=grab+QPoint(qRound(v.dragScreenX_*8/v.dragMmPerPixel_),qRound(v.dragScreenY_*8/v.dragMmPerPixel_));
        QMouseEvent drag(QEvent::MouseMove,QPointF(end),QPointF(end),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        v.mouseMoveEvent(&drag);require(v.commandSession_->value>0,"drag did not update face offset");
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(end),QPointF(end),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        v.mouseReleaseEvent(&release);
        const double firstDistance=v.commandSession_->value;
        const auto nextGrab=project(v,v.commandSession_->pivot.Translated(gp_Vec(v.commandSession_->direction)*(firstDistance+15)));
        QMouseEvent nextPress(QEvent::MouseButtonPress,QPointF(nextGrab),QPointF(nextGrab),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        v.mousePressEvent(&nextPress);require(v.draggingFace_,"preview arrow could not be grabbed again");
        const auto nextEnd=nextGrab-QPoint(qRound(v.dragScreenX_*3/v.dragMmPerPixel_),qRound(v.dragScreenY_*3/v.dragMmPerPixel_));
        QMouseEvent nextDrag(QEvent::MouseMove,QPointF(nextEnd),QPointF(nextEnd),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        v.mouseMoveEvent(&nextDrag);
        near(v.commandSession_->value,firstDistance-3,"repeated drag accumulated from wrong start state");
        QMouseEvent nextRelease(QEvent::MouseButtonRelease,QPointF(nextEnd),QPointF(nextEnd),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        v.mouseReleaseEvent(&nextRelease);
        const auto beforeKey=v.undoStack_.size();
        QApplication::processEvents();
        QKeyEvent esc(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(v.dragDimension_,&esc);
        require(!v.commandSession_ && v.undoStack_.size()==beforeKey,"Esc committed a face drag");
        selectFace(v,index,cap(v.boxes_[index].shape,true));action->trigger();
        v.dragDimension_->setText(QStringLiteral("5"));v.dragDimension_->setFocus();QApplication::processEvents();
        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(v.dragDimension_,&enter);
        require(!v.commandSession_ && v.undoStack_.size()==beforeKey+1,"Enter did not commit exactly once");
        // A repository Boolean result is edited in-place; no operand/result slot is replaced.
        v.addBox();const size_t a=v.boxes_.size()-1;v.selectParts({a});v.duplicateSelectedParts();
        require(v.applyBoolean(CadViewport::BooleanOperation::Subtract,{a,a+1},a),"Boolean fixture failed");
        const size_t result=v.boxes_.size()-1;const auto slotCount=v.boxes_.size();
        const auto resultShape=v.boxes_[result].shape;const double beforeVolume=volume(resultShape);
        selectFace(v,result,cap(resultShape,true));action->trigger();
        v.dragDimension_->setText(QStringLiteral("10"));v.confirmCommandSession();
        require(v.boxes_.size()==slotCount && v.isPartActive(result),"Boolean face edit replaced stable result ID");
        near(volume(v.boxes_[result].shape),beforeVolume*70/60,"Boolean face edit volume wrong");
        v.undoPull();require(v.boxes_[result].shape.IsSame(resultShape),"Boolean face edit undo failed");
        // Every face adapter must preserve the transaction contract when the
        // shared reconstruction merges Boolean faces/edges.
        v.addBox(); const size_t stairIndex=v.boxes_.size()-1;
        auto stairState=v.boxes_[stairIndex];
        BRepAlgoAPI_Fuse united(BRepPrimAPI_MakeBox(100,80,50).Shape(),
            BRepPrimAPI_MakeBox(gp_Pnt(60,0,0),100,80,90).Shape());
        united.Build(); require(united.IsDone(),"UI Unite fixture failed");
        stairState.shape=united.Shape();
        stairState.geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;
        stairState.orientation=transform;
        v.applyFaceState(stairIndex,stairState);
        v.renamePart(stairIndex,"Reconstruction identity");v.setPartColor(stairIndex,Qt::green);
        TopoDS_Face riser;
        for(TopExp_Explorer f(stairState.shape,TopAbs_FACE);f.More();f.Next()) {
            const auto candidate=TopoDS::Face(f.Current());
            GProp_GProps props;BRepGProp::SurfaceProperties(candidate,props);
            if(props.CentreOfMass().Distance(gp_Pnt(60,40,70))<1.e-6) riser=candidate;
        }
        require(!riser.IsNull(),"UI riser missing");
        v.fitSelection({stairIndex});
        const auto stamp=brep(stairState.shape);
        const auto presentation=v.boxes_[stairIndex].presentation;
        const auto stairRow=root->child(static_cast<int>(stairIndex));
        for(int operation=0;operation<3;++operation) for(double sign:{1.0,-1.0}) for(int level:{0,1}) {
            const auto undoCount=v.undoStack_.size();
            AIS_ListOfInteractive baselineObjects;v.context_->ObjectsInside(baselineObjects);
            const double parameter=sign*(operation==1?(level==0?8:75):(level==0?5:60));
            auto start=[&]() {
                selectFace(v,stairIndex,riser);
                if(operation==0) require(v.startMoveFace(stairIndex,riser),"riser Move start failed");
                if(operation==1) {
                    require(v.startRotateFace(stairIndex,riser),"riser Rotate start failed");
                    v.chooseFaceRotationAxis(2);
                    v.commandSession_->faceAxis=gp_Ax1(gp_Pnt(60,0,50),gp_Dir(0,1,0));
                }
                if(operation==2) {
                    require(v.startAlignFace(stairIndex,riser),"riser Align start failed");
                    v.commandSession_->hasReference=true;
                    v.commandSession_->referencePlane=gp_Pln(gp_Pnt(60-parameter,0,0),gp_Dir(1,0,0));
                    v.alignFacePickingReference_=false;
                }
            };
            start();require(v.previewCommand(parameter),"riser preview failed");
            visiblePreview(v);
            require(!v.commandSession_->preview.IsNull() && v.boxes_[stairIndex].shape.IsSame(stairState.shape),"riser preview mutated canonical shape");
            require(brep(stairState.shape)==stamp && v.undoStack_.size()==undoCount,"riser preview changed source/history");
            const auto oldPreview=v.commandSession_->preview;
            v.dragDimension_->setText(QLocale().toString(parameter*0.5));
            QMetaObject::invokeMethod(v.dragDimension_,"textEdited",Qt::DirectConnection,Q_ARG(QString,v.dragDimension_->text()));
            require(v.commandSession_->valid && v.commandSession_->preview!=oldPreview
                && v.context_->DisplayStatus(oldPreview)==PrsMgr_DisplayStatus_None,"new input retained an old preview");
            visiblePreview(v);
            const auto invalidatedPreview=v.commandSession_->preview;
            QString invalidMessage;
            const auto connection=QObject::connect(&v,&CadViewport::statusMessage,&v,[&](const QString& message){invalidMessage=message;});
            v.dragDimension_->setText(QStringLiteral("-"));
            QMetaObject::invokeMethod(v.dragDimension_,"textEdited",Qt::DirectConnection,Q_ARG(QString,v.dragDimension_->text()));
            QObject::disconnect(connection);
            require(!v.commandSession_->valid && v.commandSession_->preview.IsNull()
                && !v.commandButtons_->button(QDialogButtonBox::Ok)->isEnabled()
                && v.context_->IsDisplayed(presentation) && !invalidMessage.isEmpty(),"invalid numeric input retained a misleading preview");
            require(v.context_->DisplayStatus(invalidatedPreview)==PrsMgr_DisplayStatus_None,"invalid input leaked old preview");
            if(operation==1) require(!v.previewCommand(180),"unsupported Rotate reconstruction was accepted");
            require(v.previewCommand(parameter),"valid correction did not restore preview");
            visiblePreview(v);
            const auto cancelledPreview=v.commandSession_->preview;
            v.cancelCommandSession();
            require(v.context_->DisplayStatus(cancelledPreview)==PrsMgr_DisplayStatus_None
                && v.context_->IsDisplayed(presentation),"Cancel retained preview or hid original");
            AIS_ListOfInteractive cancelledObjects;v.context_->ObjectsInside(cancelledObjects);
            require(cancelledObjects.Extent()==baselineObjects.Extent(),"Cancel leaked AIS objects");
            require(!v.commandSession_ && v.undoStack_.size()==undoCount && v.boxes_[stairIndex].shape.IsSame(stairState.shape),"riser Cancel failed");
            start();require(v.previewCommand(parameter),"riser second preview failed");
            const auto committedPreview=v.commandSession_->preview;
            v.dragDimension_->setText(QLocale().toString(parameter));v.confirmCommandSession();
            require(v.context_->DisplayStatus(committedPreview)==PrsMgr_DisplayStatus_None
                && v.context_->IsDisplayed(presentation),"OK retained preview or hid committed solid");
            AIS_ListOfInteractive committedObjects;v.context_->ObjectsInside(committedObjects);
            require(committedObjects.Extent()==baselineObjects.Extent() && v.moveFaceArrow_.IsNull()
                && v.faceRotationManipulator_.IsNull(),"OK leaked AIS objects/manipulators");
            require(!v.commandSession_ && v.undoStack_.size()==undoCount+1,"riser confirm did not terminate with one history entry");
            const auto edited=v.boxes_[stairIndex].shape;
            require(BRepCheck_Analyzer(edited).IsValid() && !edited.IsSame(stairState.shape),"riser commit missing geometry");
            require(v.boxes_[stairIndex].presentation==presentation && root->child(static_cast<int>(stairIndex))==stairRow
                && v.partName(stairIndex)=="Reconstruction identity" && v.partColor(stairIndex)==QColor(Qt::green),"riser lost identity/properties/browser row");
            for(int r=1;r<=3;++r) for(int c=1;c<=4;++c)
                near(v.boxes_[stairIndex].orientation.Value(r,c),transform.Value(r,c),"riser changed part transform");
            v.undoPull();require(v.boxes_[stairIndex].shape.IsSame(stairState.shape),"riser Undo lost original topology");
            v.redoPull();require(v.boxes_[stairIndex].shape.IsSame(edited),"riser Redo did not use cached result");
            v.undoPull();
        }

        // Exact Fedora report: good Rotate +30 B-Rep, then oblique Move +49.
        v.addBox();const size_t exactIndex=v.boxes_.size()-1;
        auto exactState=v.boxes_[exactIndex];exactState.shape=RotateMoveFixture::load("rotated30.brep");
        exactState.geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;
        exactState.orientation=transform;exactState.min={0,0,0};exactState.max={225,80,60};
        exactState.baseMin=exactState.min;exactState.baseMax=exactState.max;
        v.applyFaceState(exactIndex,exactState);v.renamePart(exactIndex,"Fedora Rotate30 Move49");v.setPartColor(exactIndex,QColor(31,129,211));
        const auto exactTarget=RotateMoveFixture::face(exactState.shape,5);
        const auto exactStamp=brep(exactState.shape);const auto exactUndo=v.undoStack_.size();
        const auto exactPresentation=v.boxes_[exactIndex].presentation;const auto exactRow=root->child(static_cast<int>(exactIndex));
        const auto exactSlots=v.boxes_.size();v.fitSelection({exactIndex});
        AIS_ListOfInteractive exactBaseline;v.context_->ObjectsInside(exactBaseline);
        const auto startExact=[&] {
            selectFace(v,exactIndex,exactTarget);
            require(v.startMoveFace(exactIndex,exactTarget),"exact oblique Move session failed to start");
            v.dragDimension_->setText(QStringLiteral("49"));
            require(v.updateCommandParameter(),"exact oblique +49 preview failed");
            RotateMoveFixture::verify(v.commandSession_->candidate);visiblePreview(v);
        };
        startExact();const auto replaced=v.commandSession_->preview;
        require(v.previewCommand(49.5),"oblique preview replacement failed");RotateMoveFixture::verify(v.commandSession_->candidate,49.5);
        require(v.context_->DisplayStatus(replaced)==PrsMgr_DisplayStatus_None,"oblique input replacement retained old preview");
        require(v.previewCommand(49),"oblique +49 preview recovery failed");visiblePreview(v);
        const auto cancelled=v.commandSession_->preview;
        QKeyEvent exactEsc(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(v.dragDimension_,&exactEsc);
        require(!v.commandSession_&&v.undoStack_.size()==exactUndo&&brep(v.boxes_[exactIndex].shape)==exactStamp,"exact oblique Cancel modified document/history");
        require(v.context_->DisplayStatus(cancelled)==PrsMgr_DisplayStatus_None&&v.context_->IsDisplayed(exactPresentation),"exact oblique Cancel leaked preview or hid source");
        startExact();const auto committedExactPreview=v.commandSession_->preview;
        QKeyEvent exactEnter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(v.dragDimension_,&exactEnter);
        require(!v.commandSession_&&v.undoStack_.size()==exactUndo+1,"exact oblique Enter did not commit one history entry");
        const auto exactEdited=v.boxes_[exactIndex].shape;RotateMoveFixture::verify(exactEdited);
        require(v.context_->DisplayStatus(committedExactPreview)==PrsMgr_DisplayStatus_None&&v.moveFaceArrow_.IsNull()
            &&v.faceRotationManipulator_.IsNull(),"exact oblique confirmation retained AIS tools");
        AIS_ListOfInteractive exactFinal;v.context_->ObjectsInside(exactFinal);
        require(exactFinal.Extent()==exactBaseline.Extent(),"exact oblique edit leaked AIS objects");
        require(v.boxes_.size()==exactSlots&&v.boxes_[exactIndex].presentation==exactPresentation&&root->child(static_cast<int>(exactIndex))==exactRow
            &&v.partName(exactIndex)=="Fedora Rotate30 Move49"&&v.partColor(exactIndex)==QColor(31,129,211),"exact oblique edit lost Part identity/properties/browser reference");
        for(int r=1;r<=3;++r) for(int c=1;c<=4;++c) near(v.boxes_[exactIndex].orientation.Value(r,c),transform.Value(r,c),"exact oblique edit changed Part placement");
        v.undoPull();require(v.boxes_[exactIndex].shape.IsSame(exactState.shape)&&brep(v.boxes_[exactIndex].shape)==exactStamp,"exact oblique Undo did not restore supplied B-Rep");
        v.redoPull();require(v.boxes_[exactIndex].shape.IsSame(exactEdited),"exact oblique Redo did not restore cached result");RotateMoveFixture::verify(v.boxes_[exactIndex].shape);
    }
};
void runCylindricalMoveFaceSessionTests();
void runCylindricalMoveDocumentTest(const QString& path);
void runCylindricalMoveBoundaryTest(const QString& path);
void runCrashPlanarTest(const QString& path,const QString& displacement);
void runOpenNotchSessionTest(const QString& path);
void runRotatedHoleTest(const QString& path);
int main(int argc,char** argv) {
#if defined(Q_OS_LINUX)
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && !qEnvironmentVariableIsEmpty("DISPLAY")) qputenv("QT_QPA_PLATFORM","xcb");
#endif
    QLocale::setDefault(QLocale(QLocale::Dutch,QLocale::Netherlands));
    QApplication app(argc,argv);
    try {
        const int document=app.arguments().indexOf("--cylindrical-document");
        const int boundary=app.arguments().indexOf("--cylindrical-boundary");
        const int crash=app.arguments().indexOf("--crash-planar");
        const int notch=app.arguments().indexOf("--open-notch");
        const int rotated=app.arguments().indexOf("--rotated-hole");
        if(rotated>=0) runRotatedHoleTest(app.arguments().value(rotated+1));
        else if(notch>=0) runOpenNotchSessionTest(app.arguments().value(notch+1));
        else if(crash>=0) runCrashPlanarTest(app.arguments().value(crash+1),app.arguments().value(crash+2,"regression"));
        else if(boundary>=0) runCylindricalMoveBoundaryTest(app.arguments().value(boundary+1));
        else if(document>=0) runCylindricalMoveDocumentTest(app.arguments().value(document+1));
        else if(app.arguments().contains("--cylindrical")) runCylindricalMoveFaceSessionTests();
        else {MoveFaceTest::geometry();MoveFaceTest::ui();std::cout<<"Move Face geometry, preview, identity, input and history passed.\n";}
        return 0;
    }
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
