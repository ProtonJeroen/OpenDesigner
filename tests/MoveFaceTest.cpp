#include "CadViewport.h"
#include "BRepTiming.h"
#ifdef OPENDESIGNER_ENABLE_BOX_SHADOW
#include "BoxShadowObservation.h"
#endif
#include <QMenu>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QMessageBox>
#include <iomanip>
#include "MainWindow.h"
#include "MainWindowTestHelpers.h"
#include "MoveFaceGeometry.h"
#include "RotateMoveFixture.h"
#include "EllipticalExtrusionFixture.h"
#include "ObroundExtrusionFixture.h"
#include "SplineExtrusionFixture.h"
#include "BRepCertification.h"
#include "BRepProvisionalProviders.h"
#include "experimental/BoxProvisionalPreview.h"
#include "experimental/BoxProvisionalSession.h"
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <QTemporaryDir>
#include <QApplication>
#include <QAction>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QCheckBox>
#include <QLineEdit>
#include <QMouseEvent>
#include <QInputMethodEvent>
#include <BRepClass_FaceClassifier.hxx>
#include <QKeyEvent>
#include <QTreeWidget>
#include <QLocale>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <Standard_Version.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <gp_Lin.hxx>
#include <numbers>
#include "SketchProfileBuilder.h"
#include "SketchProfileExtrusion.h"
#include "SketchProfileValidation.h"
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
    static void pump(int ms) {
        QElapsedTimer t;t.start();while(t.elapsed()<ms){QApplication::processEvents();QThread::msleep(5);}
    }
    static void awaitNumeric(CadViewport& v) {
        QElapsedTimer t;t.start();
        while(v.pendingMoveNumeric_&&t.elapsed()<60000){QApplication::processEvents();QThread::msleep(5);}
        require(!v.pendingMoveNumeric_&&v.commandSession_&&v.commandSession_->valid,"delayed numeric preview failed");
    }
    static void key(CadViewport& v,int code,const QString& text={}) {
        QKeyEvent press(QEvent::KeyPress,code,Qt::NoModifier,text);QApplication::sendEvent(v.dragDimension_,&press);
        QKeyEvent release(QEvent::KeyRelease,code,Qt::NoModifier,text);QApplication::sendEvent(v.dragDimension_,&release);
    }
    static void numericInput() {
        MainWindow window;window.show();QApplication::processEvents();
        auto& v=*window.findChild<CadViewport*>();v.addBox();auto state=v.boxes_[0];
        state.shape=BRepPrimAPI_MakeBox(100,80,20).Shape();state.geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;
        state.min={0,0,0};state.max={100,80,20};state.baseMin=state.min;state.baseMax=state.max;
        v.applyFaceState(0,state);const auto original=v.boxes_[0];const auto stamp=brep(original.shape);
        const auto top=cap(original.shape,true);
        const auto start=[&]{v.cancelCommandSession();v.applyFaceState(0,original);selectFace(v,0,top);require(v.startMoveFace(0,top),"numeric start");v.dragDimension_->selectAll();BRepTiming::reset();BRepTiming::enabled=true;};
        const auto calls=[] {return BRepTiming::stats["MoveFaceGeometry.MoveFaceGeometry::move"].calls;};
        const auto rapid=[&]{key(v,Qt::Key_1,"1");key(v,Qt::Key_5,"5");};
        start();rapid();require(calls()==0&&v.pendingMoveNumeric_&&!v.commandSession_->valid&&!v.commandSession_->movePreviewCertificate,"transient value evaluated/receipt retained");
        awaitNumeric(v);require(calls()==1&&v.commandSession_->value==15,"rapid typing not coalesced");
        const auto candidate=v.commandSession_->candidate;const auto history=v.undoStack_.size();v.confirmCommandSession();
        require(!v.commandSession_&&calls()==1&&v.undoStack_.size()==history+1,"P2 confirmation repeated evaluation");
        near(volume(v.boxes_[0].shape),280000,"numeric final volume");require(v.boxes_[0].persistentId==original.persistentId,"identity");
        v.undoPull();require(v.boxes_[0].shape.IsSame(original.shape),"numeric Undo");v.redoPull();require(v.boxes_[0].shape.IsSame(candidate),"numeric Redo");
        start();key(v,Qt::Key_1,"1");awaitNumeric(v);key(v,Qt::Key_5,"5");require(!v.commandSession_->movePreviewCertificate&&!v.commandSession_->valid,"new input kept receipt");awaitNumeric(v);require(calls()==2,"paused typing count");
        start();key(v,Qt::Key_Minus,"-");rapid();awaitNumeric(v);require(calls()==1&&v.commandSession_->value==-15,"negative input");near(volume(v.commandSession_->candidate),40000,"negative volume");
        start();rapid();awaitNumeric(v);v.dragDimension_->selectAll();key(v,Qt::Key_2,"2");require(!v.commandSession_->movePreviewCertificate,"correction receipt");awaitNumeric(v);require(calls()==2&&v.commandSession_->value==2,"correction");
        for(bool enter:{true,false}) {start();rapid();require(calls()==0,"early commit already evaluated");if(enter)key(v,Qt::Key_Return);else v.commandButtons_->button(QDialogButtonBox::Ok)->click();require(!v.commandSession_&&calls()==1,"early commit failed");pump(350);require(calls()==1,"late duplicate after commit");}
        start();rapid();v.cancelCommandSession();pump(350);require(!v.commandSession_&&calls()==0,"Cancel pending");
        start();rapid();key(v,Qt::Key_Escape);pump(350);require(!v.commandSession_&&calls()==0,"Escape pending");
        start();rapid();v.context_->ClearSelected(Standard_False);v.context_->AddOrRemoveSelected(new StdSelect_BRepOwner(cap(original.shape,false),v.boxes_[0].presentation),Standard_False);pump(350);require(calls()==0&&!v.commandSession_->valid,"changed Face evaluated");v.confirmCommandSession();require(v.commandSession_.has_value(),"stale selection committed");
        start();rapid();auto edited=original;edited.shape=BRepPrimAPI_MakeBox(100,80,30).Shape();v.applyFaceState(0,edited);pump(350);v.confirmCommandSession();require(calls()==0&&v.commandSession_&&v.boxes_[0].shape.IsSame(edited.shape),"edited source committed stale preview");
        start();rapid();require(v.previewCommand(2),"explicit manipulator preview");v.dragDimension_->setText("2");pump(350);require(calls()==1&&!v.pendingMoveNumeric_,"explicit input delayed/duplicated");v.confirmCommandSession();require(!v.commandSession_&&calls()==1,"explicit preview P2");
        start();rapid();v.boxes_[0].orientation.SetTranslation(gp_Vec(4,0,0));pump(350);require(calls()==0&&!v.commandSession_->valid,"placement change evaluated stale input");
        start();rapid();selectFace(v,0,cap(original.shape,false));require(!v.commandSession_,"Face selection failed to cancel");require(v.startMoveFace(0,cap(original.shape,false)),"restart failed");pump(350);require(calls()==0&&!v.pendingMoveNumeric_,"restart retained timer");
        start();rapid();require(v.setMoveFaceDirection(CadViewport::MoveDirectionSource::Reference,{{References::Kind::Face,original.persistentId,top}},false),"reference update failed");require(!v.pendingMoveNumeric_&&calls()==1,"reference update delayed");pump(350);require(calls()==1,"reference duplicated");
        start();rapid();v.addBox();v.selectParts({1});pump(350);require(!v.commandSession_&&calls()==0,"source Part change retained timer");
        BRepTiming::enabled=false;require(brep(original.shape)==stamp,"source modified");std::cout<<"Numeric scheduling, invalidation, Enter/OK, Cancel, selection/source, explicit input and Undo/Redo passed\n";
    }
    // Optional read-only acceptance/benchmark. No private document/geometry is
    // embedded in the suite or required by CI. Caller supplies path and Part.
    static void splineDocument(const QString& path,int partNumber,int trials=3,bool lifecycle=false,double expectedHeight=std::numeric_limits<double>::quiet_NaN()) {
        require(trials>=1&&trials<=3,"diagnostic trial count outside 1..3");
        const auto strictNear=[](double a,double b,const char* message){require(std::abs(a-b)<=1.e-8*std::max(1.,std::abs(b)),message);};
        NativeDocumentSnapshot document;QString error;
        require(NativeDocumentStorage::load(path,document,error),"diagnostic load failed");
        require(partNumber>0&&size_t(partNumber)<=document.parts.size(),"diagnostic Part missing");
        MainWindow window;window.show();QApplication::processEvents();
        auto& v=*window.findChild<CadViewport*>();
        require(v.importDocumentSnapshot(document,error),"diagnostic import failed");
        const size_t partIndex=partNumber-1;
        if(lifecycle)require(v.setActivePart(v.boxes_[partIndex].persistentId),"cannot activate diagnostic Part");
        const auto original=v.boxes_[partIndex];const auto source=brep(original.shape);
        const auto top=cap(original.shape,true),bottom=cap(original.shape,false);
        const double initialHeight=BRepAdaptor_Surface(top).Plane().Location().Z()-BRepAdaptor_Surface(bottom).Plane().Location().Z();
        std::cout<<"SOURCE initial_height="<<initialHeight<<" displacement=15 result_height="<<initialHeight+15<<'\n';
        if(std::isfinite(expectedHeight))require(std::abs(initialHeight-expectedHeight)<=1.e-6,"diagnostic starting-height mismatch; no operation attempted");
        const double sectionArea=BRepCertification::area(top);
        gp_Pnt pick;
        if(lifecycle) {
            bool found=false;double u0,u1,v0,v1;BRepTools::UVBounds(top,u0,u1,v0,v1);
            const BRepAdaptor_Surface support(top);
            // This search chooses a valid click target only; it is not geometry
            // reconstruction or a sampled modeling/certification acceptance.
            for(int a=0;a<19&&!found;++a)for(int b=0;b<19&&!found;++b) {
                const int i=10+(a%2?-(a+1)/2:a/2),j=10+(b%2?-(b+1)/2:b/2);
                auto point=support.Value(u0+(u1-u0)*i/20,v0+(v1-v0)*j/20);
                if(BRepClass_FaceClassifier(top,point,1.e-7).State()==TopAbs_IN) {pick=point.Transformed(original.orientation);found=true;}
            }
            require(found,"diagnostic click target not found");
            const auto normal=gp_Dir(0,0,1).Transformed(original.orientation);
            v.view_->SetProj(normal.X(),normal.Y(),normal.Z());v.fitAll();QApplication::processEvents();
        }
        for(int trial=0;trial<trials;++trial) {
            v.resetModelingTools();
            if(!lifecycle)selectFace(v,partIndex,top);
            else {v.context_->ClearSelected(Standard_False);v.setSelectionMode(CadViewport::SelectionMode::Face);}
            QApplication::processEvents();
            const auto phase=[&](const char* label,auto action){
                BRepTiming::reset();BRepTiming::enabled=true;BRepTiming::detailed=true;
                {BRepTiming::Scope scope("phase.total");action();}
                BRepTiming::enabled=false;BRepTiming::report(std::cout,std::string(lifecycle?"document-spline-profile/":"document-spline/")+std::to_string(trial)+"/"+label);
                BRepTiming::detailed=false;
            };
            if(lifecycle) {
                const auto pixel=project(v,pick);
                phase("face-selection",[&]{
                    QMouseEvent press(QEvent::MouseButtonPress,QPointF(pixel),QPointF(v.mapToGlobal(pixel)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                    QApplication::sendEvent(&v,&press);
                    QMouseEvent release(QEvent::MouseButtonRelease,QPointF(pixel),QPointF(v.mapToGlobal(pixel)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(&v,&release);QApplication::processEvents();
                    const auto selection=v.selectionContext();
                    require(selection.faces.size()==1&&selection.faces[0].partIndex==partIndex&&selection.faces[0].face.IsSame(top),"actual click did not select target cap");
                });
                std::unique_ptr<QMenu> menu;
                phase("context-menu",[&]{menu.reset(v.createSelectionMenu(pixel));});
            }
            phase("face-eligibility",[&]{gp_Pnt p;gp_Dir n;QString e;require(MoveFaceGeometry::referenceForMove(original.shape,top,p,n,e),"diagnostic eligibility failed");});
            phase("command-initialization",[&]{
                if(lifecycle)v.moveFace();else require(v.startMoveFace(partIndex,top),"diagnostic start failed");
                require(v.commandSession_&&v.commandSession_->face.IsSame(top),"diagnostic session target changed");
                QApplication::processEvents();
            });
            phase("preview-15",[&]{
                if(lifecycle) {
                    // One real Qt input-method commit, not a direct call to the
                    // reconstruction function and not a clipboard mutation.
                    v.dragDimension_->selectAll();QInputMethodEvent input;input.setCommitString("15");
                    QApplication::sendEvent(v.dragDimension_,&input);
                    awaitNumeric(v);require(v.commandSession_->value==15,"input-method request failed");
                } else {
                    QSignalBlocker block(v.dragDimension_);v.dragDimension_->setText("15");require(v.updateCommandParameter(false),"diagnostic preview failed");
                }
                {BRepTiming::Event delivery("Preview.EventFlush");QApplication::processEvents();delivery.finish();}
            });
            require(v.commandSession_->valid&&!v.commandSession_->candidate.IsNull(),"uncertified diagnostic preview");
            const auto candidate=v.commandSession_->candidate;
            const auto undo=v.undoStack_.size();
            phase("confirmation",[&]{v.confirmCommandSession();QApplication::processEvents();});
            require(!v.commandSession_&&v.undoStack_.size()==undo+1,"diagnostic confirmation not atomic");
            require(BRepTiming::stats["MoveFace.certifiedPreviewReused"].calls==1,"diagnostic confirmation missed P2 receipt");
            const auto committed=v.boxes_[partIndex];
            require(committed.persistentId==original.persistentId,"diagnostic identity/placement changed");
            for(int r=1;r<=3;++r)for(int c=1;c<=4;++c)require(committed.orientation.Value(r,c)==original.orientation.Value(r,c),"diagnostic placement changed");
            if(trial==0) {
                strictNear(BRepCertification::volume(candidate),sectionArea*(initialHeight+15),"independent section/height volume");
                strictNear(BRepAdaptor_Surface(cap(candidate,true)).Plane().Location().Z(),BRepAdaptor_Surface(top).Plane().Location().Z()+15,"diagnostic height");
                require(BRepCertification::validBRep(candidate)&&BRepCertification::closedShells(candidate)
                    &&BRepCertification::manifold(candidate)&&BRepCertification::withinTolerance(candidate,1.e-6),"diagnostic topology");
                require(BRepCertification::sameTrimmedRegion(bottom,cap(candidate,false)),"diagnostic bottom changed");
                const auto region=BRepPrimAPI_MakePrism(top,gp_Vec(0,0,15),true,true).Shape();
                require(BRepCertification::preservedOutside(original.shape,candidate,region),"diagnostic protected material changed");
                BRepCertification::MaterialDifference diff;
                require(BRepCertification::difference(original.shape,candidate,diff,error)
                    &&BRepCertification::equivalent(diff.added,region),"diagnostic directional material");
                strictNear(BRepCertification::volume(diff.removed),0,"diagnostic removed material");
                for(TopExp_Explorer f(original.shape,TopAbs_FACE);f.More();f.Next()) {
                    const auto old=TopoDS::Face(f.Current());
                    if(BRepAdaptor_Surface(old).GetType()!=GeomAbs_SurfaceOfExtrusion)continue;
                    BRepReconstruction::InvariantSplineSupport a;
                    require(BRepReconstruction::SupportDescriptor(old).invariantSpline(a,error),"diagnostic source support");
                    int matches=0;
                    for(TopExp_Explorer g(candidate,TopAbs_FACE);g.More();g.Next()) {
                        const auto next=TopoDS::Face(g.Current());if(BRepAdaptor_Surface(next).GetType()!=GeomAbs_SurfaceOfExtrusion)continue;
                        BRepCertification::Correspondence mapping;
                        if(!BRepCertification::correspondInvariantSpline(old,next,mapping,error)||mapping.faces[0].regions.overlaps.empty())continue;
                        BRepReconstruction::InvariantSplineSupport b;
                        require(BRepReconstruction::SupportDescriptor(next).invariantSpline(b,error),"diagnostic result support");
                        require(a.basis->Degree()==b.basis->Degree()&&a.basis->NbPoles()==b.basis->NbPoles()&&a.basis->NbKnots()==b.basis->NbKnots(),"diagnostic spline representation");
                        for(int k=1;k<=a.basis->NbKnots();++k) {
                            require(a.basis->Knot(k)==b.basis->Knot(k)&&a.basis->Multiplicity(k)==b.basis->Multiplicity(k),"diagnostic knots/multiplicities changed");
                        }
                        const gp_Vec shift(a.basis->Pole(1),b.basis->Pole(1));
                        require(shift.Crossed(gp_Vec(a.axis)).Magnitude()<1.e-10,"diagnostic carrier shifted laterally");
                        for(int k=1;k<=a.basis->NbPoles();++k)require(a.basis->Pole(k).Translated(shift).Distance(b.basis->Pole(k))<1.e-10,"diagnostic coefficients changed");
                        ++matches;
                    }
                    require(matches==1,"diagnostic ambiguous spline correspondence");
                }
                std::cout<<"ACCEPTANCE height="<<initialHeight+15<<" volume="<<BRepCertification::volume(candidate)<<" topology/material/coefficients/correspondence passed\n";
            }
            v.undoPull();require(v.boxes_[partIndex].shape.IsSame(original.shape),"diagnostic Undo lost source");
            v.redoPull();require(v.boxes_[partIndex].shape.IsSame(committed.shape),"diagnostic Redo rebuilt geometry");
            v.undoPull();require(brep(original.shape)==source,"diagnostic source mutated");
            if(lifecycle) {
                selectFace(v,partIndex,top);require(v.startMoveFace(partIndex,top),"typing setup failed");
                v.dragDimension_->selectAll();const auto history=v.undoStack_.size();
                phase("typing-1-then-15",[&]{
                    const auto key=[&](int code,const QString& text){
                        QKeyEvent press(QEvent::KeyPress,code,Qt::NoModifier,text);
                        QApplication::sendEvent(v.dragDimension_,&press);
                        QKeyEvent release(QEvent::KeyRelease,code,Qt::NoModifier,text);
                        QApplication::sendEvent(v.dragDimension_,&release);QApplication::processEvents();
                    };
                    {BRepTiming::Scope first("Input.key1");key(Qt::Key_1,"1");}
                    require(!v.commandSession_->valid&&v.pendingMoveNumeric_,"first typed value was evaluated early");
                    {BRepTiming::Scope second("Input.key5");key(Qt::Key_5,"5");}
                    awaitNumeric(v);require(v.commandSession_->value==15,"second typed value was not certified");
                });
                require(BRepTiming::stats["MoveFaceGeometry.MoveFaceGeometry::move"].calls==1,"typing request count differs");
                v.cancelCommandSession();
                require(v.undoStack_.size()==history&&v.boxes_[partIndex].shape.IsSame(original.shape)&&brep(original.shape)==source,"typing/cancel changed source or history");
            }
        }
    }

    static void splineHeadless(const QString& path,int partNumber,double expectedHeight=std::numeric_limits<double>::quiet_NaN()) {
        NativeDocumentSnapshot document;QString error;
        require(NativeDocumentStorage::load(path,document,error),"headless document load failed");
        require(partNumber>0&&size_t(partNumber)<=document.parts.size(),"headless Part missing");
        const auto shape=document.parts[partNumber-1].shape;const auto before=brep(shape);const auto top=cap(shape,true);
        const double startingHeight=BRepAdaptor_Surface(top).Plane().Location().Z()-BRepAdaptor_Surface(cap(shape,false)).Plane().Location().Z();
        std::cout<<"SOURCE initial_height="<<startingHeight<<" displacement=15 result_height="<<startingHeight+15<<'\n';
        if(std::isfinite(expectedHeight))require(std::abs(startingHeight-expectedHeight)<=1.e-6,"diagnostic starting-height mismatch; no operation attempted");
        const double height=BRepAdaptor_Surface(top).Plane().Location().Z()+15;
        for(int trial=0;trial<3;++trial) {
            BRepTiming::reset();BRepTiming::enabled=true;BRepTiming::detailed=true;TopoDS_Shape candidate;
            {BRepTiming::Scope phase("phase.total");candidate=MoveFaceGeometry::move(shape,top,15,error,MoveFaceGeometry::ReconstructionPolicy::AxialInvariantCap);}
            BRepTiming::enabled=false;BRepTiming::report(std::cout,"document-spline-headless/"+std::to_string(trial)+"/geometry-15");BRepTiming::detailed=false;
            require(!candidate.IsNull(),error.toStdString().c_str());
            near(BRepAdaptor_Surface(cap(candidate,true)).Plane().Location().Z(),height,"headless height changed");
            require(brep(shape)==before,"headless source mutated");
        }
    }

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
        gp_Pnt planarCenter;gp_Dir planarNormal;
        require(MoveFaceGeometry::reference(halfSphere,cap(halfSphere,false),planarCenter,planarNormal,error),"planar recognition depended on a neighboring surface type");
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

#ifdef OPENDESIGNER_ENABLE_BOX_SHADOW
    static void boxShadowObservation() {
        MainWindow window;window.show();QApplication::processEvents();
        auto& v=*window.findChild<CadViewport*>();v.addBox();
        auto state=v.boxes_[0];state.shape=BRepPrimAPI_MakeBox(40,30,20).Shape();
        state.geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;state.blends={};state.orientation=gp_Trsf();
        state.min={0,0,0};state.max={40,30,20};state.baseMin=state.min;state.baseMax=state.max;v.applyFaceState(0,state);
        const auto original=v.boxes_[0];const auto stamp=brep(original.shape);const auto top=cap(original.shape,true);
        auto start=[&] {v.cancelCommandSession();v.applyFaceState(0,original);selectFace(v,0,top);require(v.startMoveFace(0,top),"shadow session start");};
        auto parameter=[&](double value) {const QSignalBlocker block(v.dragDimension_);v.dragDimension_->setText(QLocale().toString(value));};
        auto evaluate=[&](double value,bool on) {
            qputenv("OPENDESIGNER_BOX_SHADOW",on?"1":"0");parameter(value);QElapsedTimer timer;timer.start();
            const bool result=v.updateCommandParameter(false);
            const double ms=timer.nsecsElapsed()/1.e6;
            std::cout<<"BOX_SESSION enabled="<<on<<" value="<<value<<" valid="<<result<<" complete_preview_ms="<<ms<<'\n';return result;
        };
        qunsetenv("OPENDESIGNER_BOX_SHADOW_TEST_FAULT");
        for(double d:{15.,-5.}) for(int trial=0;trial<3;++trial) {
            start();require(evaluate(d,false),"disabled Box preview");auto baseline=v.commandSession_->candidate;
            // Both measurements are the first nonzero preview of a fresh command.
            start();require(evaluate(d,true),"observed Box preview");auto candidate=v.commandSession_->candidate;auto record=BoxShadowObservation::last();
            require(record.eligible&&record.productionAccepted&&record.shadow=="Certified"&&!record.disagreement,"shadow normal verdict");
            require(BRepCertification::equivalent(baseline,candidate),"observer changed candidate material");
            require(brep(baseline)==brep(candidate),"observer changed exact Box geometry");
            require(v.commandSession_->movePreviewCertificate.has_value(),"observer lost P2 receipt");
            const auto count=BoxShadowObservation::count();const auto undo=v.undoStack_.size();
            BRepTiming::reset();BRepTiming::enabled=true;QElapsedTimer timer;timer.start();v.confirmCommandSession();
            const auto ms=timer.nsecsElapsed()/1.e6;BRepTiming::enabled=false;
            require(!v.commandSession_&&v.boxes_[0].shape.IsSame(candidate)&&v.undoStack_.size()==undo+1,"observer changed confirmation");
            require(BoxShadowObservation::count()==count&&BRepTiming::stats["MoveFace.certifiedPreviewReused"].calls==1
                &&BRepTiming::stats["MoveFaceGeometry.MoveFaceGeometry::move"].calls==0,"observer affected receipt reuse");
            std::cout<<"BOX_CONFIRM value="<<d<<" ms="<<ms<<" receipt_reuse_ms="<<BRepTiming::stats["MoveFace.reuseMovePreview"].inclusive<<'\n';
            require(v.boxes_[0].persistentId==original.persistentId&&v.boxes_[0].presentation==original.presentation,"observer changed Part identity");
            v.undoPull();require(v.boxes_[0].shape.IsSame(original.shape),"observer Undo");v.redoPull();require(v.boxes_[0].shape.IsSame(candidate),"observer Redo");v.undoPull();
        }
        start();require(evaluate(15,true),"repeat preview setup");auto one=v.commandSession_->candidate;
        require(evaluate(15,true)&&BRepCertification::equivalent(one,v.commandSession_->candidate),"repeat unchanged preview");
        require(evaluate(10,true),"changed displacement");near(volume(v.commandSession_->candidate),40*30*30,"changed value ignored");
        require(!evaluate(-25,true),"invalid height accepted");require(!v.commandSession_->valid,"failed preview committable");
        for(const char* fault:{"throw","unresolved"}) {
            start();qputenv("OPENDESIGNER_BOX_SHADOW_TEST_FAULT",fault);require(evaluate(15,true),"shadow failure blocked production");
            auto record=BoxShadowObservation::last();require(record.shadow=="Unresolved"&&record.productionAccepted,"injected failure not observed");
            auto candidate=v.commandSession_->candidate;parameter(15);v.confirmCommandSession();
            require(!v.commandSession_&&v.boxes_[0].shape.IsSame(candidate),"shadow failure blocked confirmation");v.undoPull();
        }
        qunsetenv("OPENDESIGNER_BOX_SHADOW_TEST_FAULT");
        start();require(evaluate(15,true),"invalidation setup");auto replacement=original;replacement.shape=BRepPrimAPI_MakeBox(40,30,25).Shape();
        v.applyFaceState(0,replacement);const auto count=BoxShadowObservation::count();parameter(15);
        {QSignalBlocker block(&v);v.confirmCommandSession();}
        require(v.boxes_[0].shape.IsSame(replacement.shape)&&BoxShadowObservation::count()==count,"stale source observed/committed");
        v.cancelCommandSession();v.applyFaceState(0,original);
        // Unsupported exact ellipse bypasses all Box shadow validation.
        auto ellipse=original;ellipse.shape=EllipticalFixture::prism();v.applyFaceState(0,ellipse);auto ecap=cap(ellipse.shape,true);selectFace(v,0,ecap);
        require(v.startMoveFace(0,ecap),"ellipse bypass setup");const auto bypass=BoxShadowObservation::count();
        require(evaluate(15,true)&&BoxShadowObservation::count()==bypass,"unsupported geometry entered Box observer");v.cancelCommandSession();
        require(brep(original.shape)==stamp,"observer changed source geometry");
        qunsetenv("OPENDESIGNER_BOX_SHADOW");
        std::cout<<"Box production shadow enable/disable, repeated/changed/invalid input, bypass, failure, receipt, source invalidation and history passed\n";
    }
#endif


    // F2.9D.3: QApplication-delivered press/drag/release on geometry created
    // through the real Box/Extrude actions, before and after Save/Open, flag OFF/ON.
    // A real hand drag usually returns through 0 mm after the first published
    // provisional preview; that certified zero fallback must not invalidate the
    // renderer-caused (Erase) empty owner binding.
    static void sendMouse(CadViewport& v,QEvent::Type type,QPoint pixel,Qt::MouseButton button,Qt::MouseButtons buttons) {
        QMouseEvent event(type,QPointF(pixel),QPointF(v.mapToGlobal(pixel)),button,buttons,Qt::NoModifier);
        QApplication::sendEvent(&v,&event);QApplication::processEvents();
    }
    static void realClick(CadViewport& v,QPoint pixel) {
        sendMouse(v,QEvent::MouseMove,pixel,Qt::NoButton,Qt::NoButton);
        sendMouse(v,QEvent::MouseButtonPress,pixel,Qt::LeftButton,Qt::LeftButton);
        sendMouse(v,QEvent::MouseButtonRelease,pixel,Qt::LeftButton,Qt::NoButton);
    }
    static std::vector<double> jitterDrag(CadViewport& v,double startMm,const std::vector<int>& path) {
        const auto& s=*v.commandSession_;
        const auto grab=project(v,s.pivot.Translated(gp_Vec(s.direction)*(startMm+15)));
        const auto base=project(v,s.pivot),tip=project(v,s.pivot.Translated(gp_Vec(s.direction)*31));
        const double length=std::max(1.,std::hypot(double(tip.x()-base.x()),double(tip.y()-base.y())));
        const QPointF axis=QPointF(tip-base)/length;
        sendMouse(v,QEvent::MouseMove,grab,Qt::NoButton,Qt::NoButton);
        sendMouse(v,QEvent::MouseButtonPress,grab,Qt::LeftButton,Qt::LeftButton);
        require(v.draggingFace_,"manipulator press did not start drag");near(v.moveFaceDragStart_,startMm,"drag started from wrong value");
        QPoint last=grab;std::vector<double> ms;
        for(int px:path) {
            last=grab+QPoint(qRound(axis.x()*px),qRound(axis.y()*px));
            QMouseEvent event(QEvent::MouseMove,QPointF(last),QPointF(v.mapToGlobal(last)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
            QElapsedTimer timer;timer.start();QApplication::sendEvent(&v,&event);ms.push_back(timer.nsecsElapsed()/1.e6);
            QApplication::processEvents();
        }
        sendMouse(v,QEvent::MouseButtonRelease,last,Qt::LeftButton,Qt::NoButton);
        return ms;
    }
    static void dragSession(MainWindow& window,CadViewport& v,bool flag,bool expectProvisional,const char* label) {
        using Phase=CadViewport::CommandSession::MovePreviewPhase;
        qputenv("OPENDESIGNER_ENABLE_PROVISIONAL_BOX_PREVIEW",flag?"1":"0");
        QAction* action=nullptr;
        for(auto* candidate:window.findChildren<QAction*>()) if(candidate->text()==QStringLiteral("Move Face")) action=candidate;
        const auto original=v.boxes_[0];const auto top=cap(original.shape,true);
        GProp_GProps props;BRepGProp::SurfaceProperties(top,props);
        const auto faceCenter=project(v,props.CentreOfMass().Transformed(original.orientation));
        auto start=[&] {
            v.selectParts({});realClick(v,faceCenter);action->trigger();QApplication::processEvents();
            require(v.commandSession_&&v.commandSession_->kind==CadViewport::CommandSession::Kind::MoveFace,label);
        };
        auto displayed=[&] {
            const auto& c=*v.commandSession_;
            return !c.moveNumericContextInvalidated&&(expectProvisional
                ? c.movePreviewPhase==Phase::Provisional&&!c.valid&&!c.provisionalCandidate().IsNull()
                : c.valid&&c.provisionalCandidate().IsNull());
        };
        start();
        // ~1 mm out, back through 0 mm, then the real drag; then resume once more.
        const std::vector<int> path{1,2,3,4,5,6,5,4,3,2,1,0,4,8,12,16,20,24,28,32,36,40};
        BRepTiming::reset();BRepTiming::enabled=true;
        auto ms=jitterDrag(v,0,path);
        const auto reconstructions=BRepTiming::stats["MoveFaceGeometry.MoveFaceGeometry::move"].calls;BRepTiming::enabled=false;
        // Updates after the zero jitter (12 events): nonzero drag steps only.
        std::vector<double> steps(ms.begin()+12,ms.end());std::sort(steps.begin(),steps.end());
        std::cout<<"DRAG_TIMING "<<label<<" update_median_ms="<<steps[steps.size()/2]<<" update_max_ms="<<steps.back()
                 <<" events="<<ms.size()<<" full_reconstructions="<<reconstructions<<'\n';
        require(std::abs(v.commandSession_->value)>1&&displayed(),label);
        const double first=v.commandSession_->value;pump(350);
        require(displayed()&&v.commandSession_->value==first,"debounce after drag changed/invalidated preview");
        jitterDrag(v,first,{4,8,12,16,20});
        require(v.commandSession_->value>first&&displayed(),"resumed drag failed");
        QKeyEvent esc(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
        const auto undo=v.undoStack_.size();QApplication::sendEvent(v.dragDimension_,&esc);
        require(!v.commandSession_&&v.undoStack_.size()==undo&&v.boxes_[0].shape.IsSame(original.shape),"Escape changed source/history");
        // Numeric input through 0 mm uses the same binding.
        start();
        for(const char* text:{"5","0","5"}) {v.dragDimension_->setText(text);v.updateCommandParameter(false);}
        require(displayed()&&v.commandSession_->value==5,"numeric 5/0/5 invalidated preview");
        jitterDrag(v,5,path);const double committed=v.commandSession_->value;
        require(displayed(),"drag after numeric zero failed");
        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(v.dragDimension_,&enter);
        require(!v.commandSession_&&v.undoStack_.size()==undo+1,"Enter did not commit exactly once");
        require(v.boxes_[0].persistentId==original.persistentId&&v.boxes_[0].presentation==original.presentation,"commit changed Part identity");
        Bnd_Box before,after;BRepBndLib::Add(original.shape,before,false);BRepBndLib::Add(v.boxes_[0].shape,after,false);
        near(after.CornerMax().Z()-before.CornerMax().Z(),committed,"committed height differs from dragged value");
        near(volume(v.boxes_[0].shape)/volume(original.shape),
             (after.CornerMax().Z()-after.CornerMin().Z())/(before.CornerMax().Z()-before.CornerMin().Z()),"committed material wrong");
        const auto edited=v.boxes_[0].shape;
        v.undoPull();require(v.boxes_[0].shape.IsSame(original.shape),"Undo");
        v.redoPull();require(v.boxes_[0].shape.IsSame(edited),"Redo");
        v.undoPull();
        std::cout<<"DRAG "<<label<<" first="<<first<<" committed="<<committed<<'\n';
    }
    // F2.9E.6 timing fixtures: the same real-window Box (0) / sketch Extrude (1)
    // setup as dragInitialization, which is kept verbatim as last verified.
    static void createDragFixture(MainWindow& window,CadViewport& v,int fixture) {
        if(fixture==0) {
            QAction* box=nullptr;for(auto* candidate:window.findChildren<QAction*>()) if(candidate->text()==QStringLiteral("Create &Box")) box=candidate;
            require(box,"Create Box action missing");box->trigger();
        } else {
            Construction::Object wp;wp.id="drag-extrude-wp";wp.name="WP1";wp.kind=Construction::Kind::Workplane;v.restoreConstructions({wp});
            const Sketch::Point p[]{{0,0},{100,0},{100,50},{0,50}};std::vector<Sketch::Entity> sketch;
            for(size_t i=0;i<4;++i){Sketch::Entity e;e.id="drag-profile-"+QString::number(i);e.workplaneId=wp.id;e.geometry=Sketch::Segment{p[i],p[(i+1)%4]};sketch.push_back(e);}
            v.restoreSketchEntities(sketch);v.activateSketchWorkplane(wp.id);v.selectSketchEntities({sketch.front().id});
            auto* extrude=window.findChild<QAction*>("sketch.extrude");require(extrude&&extrude->isEnabled(),"Extrude unavailable");extrude->trigger();
            QApplication::processEvents();
            QKeyEvent ret(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(v.sweepControls_->findChild<QLineEdit*>("extrusionDistance"),&ret);
            QApplication::processEvents();require(v.boxes_.size()==1&&!v.commandSession_,"Extrude did not create one Part");
            v.endSketchEditing();v.view_->SetProj(1,-1,1);v.fitAll();
        }
        pump(100);
    }
    static void reopenDocument(CadViewport& v) {
        NativeDocumentSnapshot snapshot,loaded;QTemporaryDir directory;QString error;
        require(v.exportDocumentSnapshot(snapshot)&&NativeDocumentStorage::save(directory.filePath("drag.odc"),snapshot,error)
            &&NativeDocumentStorage::load(directory.filePath("drag.odc"),loaded,error)&&v.importDocumentSnapshot(loaded,error),"Save/Open failed");
        pump(100);
    }
    static void dragInitialization() {
        for(int fixture=0;fixture<2;++fixture) for(bool flag:{false,true}) {
            MainWindow window;window.resize(1400,1000);window.show();QApplication::processEvents();pump(200);
            auto& v=*window.findChild<CadViewport*>();v.initializeViewer();
            if(fixture==0) {
                QAction* box=nullptr;for(auto* candidate:window.findChildren<QAction*>()) if(candidate->text()==QStringLiteral("Create &Box")) box=candidate;
                require(box,"Create Box action missing");box->trigger();
            } else {
                Construction::Object wp;wp.id="drag-extrude-wp";wp.name="WP1";wp.kind=Construction::Kind::Workplane;v.restoreConstructions({wp});
                const Sketch::Point p[]{{0,0},{100,0},{100,50},{0,50}};std::vector<Sketch::Entity> sketch;
                for(size_t i=0;i<4;++i){Sketch::Entity e;e.id="drag-profile-"+QString::number(i);e.workplaneId=wp.id;e.geometry=Sketch::Segment{p[i],p[(i+1)%4]};sketch.push_back(e);}
                v.restoreSketchEntities(sketch);v.activateSketchWorkplane(wp.id);v.selectSketchEntities({sketch.front().id});
                auto* extrude=window.findChild<QAction*>("sketch.extrude");require(extrude&&extrude->isEnabled(),"Extrude unavailable");extrude->trigger();
                QApplication::processEvents();
                QKeyEvent ret(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(v.sweepControls_->findChild<QLineEdit*>("extrusionDistance"),&ret);
                QApplication::processEvents();require(v.boxes_.size()==1&&!v.commandSession_,"Extrude did not create one Part");
                v.endSketchEditing();v.view_->SetProj(1,-1,1);v.fitAll();
            }
            pump(100);
            const char* names[2][2][2]={{{"OFF new Box","OFF reopened Box"},{"ON new Box","ON reopened Box"}},
                                        {{"OFF new Extrude","OFF reopened Extrude"},{"ON new Extrude","ON reopened Extrude"}}};
            require(v.boxes_[0].orientation.Form()==gp_Identity,"new Part placement not identity form");
            dragSession(window,v,flag,flag,names[fixture][flag][0]);
            NativeDocumentSnapshot snapshot,loaded;QTemporaryDir directory;QString error;
            require(v.exportDocumentSnapshot(snapshot)&&NativeDocumentStorage::save(directory.filePath("drag.odc"),snapshot,error)
                &&NativeDocumentStorage::load(directory.filePath("drag.odc"),loaded,error)&&v.importDocumentSnapshot(loaded,error),"Save/Open failed");
            pump(100);
            // F2.9D.4: the reloaded exact identity matrix carries gp_CompoundTrsf
            // form; eligibility now judges the stored matrix, so ON stays provisional.
            require(v.boxes_[0].orientation.Form()==gp_CompoundTrsf,"Save/Open placement representation changed; revisit F2.9D.4 note");
            dragSession(window,v,flag,flag,names[fixture][flag][1]);
            // The document is untitled, so Discard cannot destroy a file.
            MainWindowTest::PromptReport prompts;
            { MainWindowTest::UnsavedPromptGuard guard(window,QString(),prompts); window.close(); }
            require(prompts.answered==1,"closing the dirty window raised exactly one unsaved-changes prompt");
        }
        qunsetenv("OPENDESIGNER_ENABLE_PROVISIONAL_BOX_PREVIEW");
        std::cout<<"Move Face drag initialization: Box/Extrude, new/reopened, flag OFF/ON, zero jitter, numeric, Escape, Enter, Undo/Redo passed\n";
    }
    // F2.9E.6 acceptance timings: unregistered measurement mode
    // (--provisional-acceptance-timings). Primary latencies run with BRepTiming
    // disabled; a separate instrumented pass (stats only, never detailed
    // tracing) gives the stage breakdown and per-update call counts. Flag order
    // alternates per trial so slow drift affects ON and OFF equally.
    static void provisionalAcceptanceTimings() {
        using Phase=CadViewport::CommandSession::MovePreviewPhase;
        qunsetenv("OPENDESIGNER_BOX_SHADOW_TEST_FAULT");qputenv("OPENDESIGNER_BOX_SHADOW","0");
        const int trials=qEnvironmentVariableIsSet("OPENDESIGNER_ACCEPTANCE_TRIALS")?qEnvironmentVariableIntValue("OPENDESIGNER_ACCEPTANCE_TRIALS"):9;
        auto stats=[](std::vector<double> x) {
            std::sort(x.begin(),x.end());std::ostringstream out;out<<std::fixed<<std::setprecision(2);
            if(x.empty())return std::string("n=0");
            const double median=x.size()%2?x[x.size()/2]:(x[x.size()/2-1]+x[x.size()/2])/2;
            out<<"median="<<median<<" min="<<x.front()<<" max="<<x.back()<<" n="<<x.size();return out.str();
        };
        auto inclusive=[](const char* name){auto it=BRepTiming::stats.find(name);return it==BRepTiming::stats.end()?0.:it->second.inclusive;};
        auto calls=[](const char* name){auto it=BRepTiming::stats.find(name);return it==BRepTiming::stats.end()?0u:it->second.calls;};
        std::cout<<std::unitbuf;
        const QByteArray only=qgetenv("OPENDESIGNER_ACCEPTANCE_FIXTURE"); // "0" Box, "1" Extrude, unset both
        for(int fixture=0;fixture<2;++fixture) {
            if(!only.isEmpty()&&only.toInt()!=fixture)continue;
            MainWindow window;window.resize(1400,1000);window.show();QApplication::processEvents();pump(200);
            auto& v=*window.findChild<CadViewport*>();v.initializeViewer();createDragFixture(window,v,fixture);
            QAction* action=nullptr;
            for(auto* candidate:window.findChildren<QAction*>()) if(candidate->text()==QStringLiteral("Move Face")) action=candidate;
            require(action,"Move Face action missing");
            for(int reopened=0;reopened<2;++reopened) {
                if(reopened)reopenDocument(v);
                const std::string label=std::string(reopened?"reopened ":"new ")+(fixture?"Extrude":"Box");
                const auto original=v.boxes_[0];const auto top=cap(original.shape,true);
                GProp_GProps props;BRepGProp::SurfaceProperties(top,props);
                const auto faceCenter=project(v,props.CentreOfMass().Transformed(original.orientation));
                auto start=[&](bool flag) {
                    qputenv("OPENDESIGNER_ENABLE_PROVISIONAL_BOX_PREVIEW",flag?"1":"0");
                    v.selectParts({});realClick(v,faceCenter);action->trigger();QApplication::processEvents();
                    require(v.commandSession_&&v.commandSession_->kind==CadViewport::CommandSession::Kind::MoveFace
                        &&v.commandSession_->provisionalPreviewEnabled==flag,"acceptance session start failed");
                    v.stopMoveNumericPreview();pump(50);
                };
                auto routed=[&](bool flag) {
                    const auto& c=*v.commandSession_;
                    return flag ? c.movePreviewPhase==Phase::Provisional&&!c.valid&&!c.provisionalCandidate().IsNull()&&!c.movePreviewCertificate
                                : c.valid&&c.movePreviewCertificate&&c.provisionalCandidate().IsNull();
                };
                // evaluation = updateCommandParameter including its synchronous
                // Redraw; frame = evaluation plus the following event-loop turn.
                auto evaluate=[&](double d,double& evaluation,double& frame) {
                    {const QSignalBlocker block(v.dragDimension_);v.dragDimension_->setText(QLocale().toString(d,'g',17));}
                    QElapsedTimer timer;timer.start();require(v.updateCommandParameter(false),"acceptance preview failed");
                    evaluation=timer.nsecsElapsed()/1.e6;QApplication::processEvents();frame=timer.nsecsElapsed()/1.e6;
                };
                auto commit=[&](double d) {
                    const auto undo=v.undoStack_.size();QElapsedTimer timer;timer.start();
                    v.commandButtons_->button(QDialogButtonBox::Ok)->click();const double ms=timer.nsecsElapsed()/1.e6;
                    require(!v.commandSession_&&v.undoStack_.size()==undo+1,"acceptance OK did not commit once");
                    Bnd_Box before,after;BRepBndLib::Add(original.shape,before,false);BRepBndLib::Add(v.boxes_[0].shape,after,false);
                    near(after.CornerMax().Z()-before.CornerMax().Z(),d,"acceptance committed height");
                    v.undoPull();require(v.boxes_[0].shape.IsSame(original.shape),"acceptance Undo");pump(20);return ms;
                };
                std::map<std::pair<bool,double>,std::vector<double>> evaluation,frame,ok;
                for(int trial=0;trial<trials;++trial)
                    for(bool flag:trial%2?std::vector<bool>{true,false}:std::vector<bool>{false,true})
                        for(double d:{15.,-5.}) {
                            start(flag);double e=0,f=0;evaluate(d,e,f);require(routed(flag),"acceptance route changed");
                            evaluation[{flag,d}].push_back(e);frame[{flag,d}].push_back(f);ok[{flag,d}].push_back(commit(d));
                        }
                for(bool flag:{false,true})for(double d:{15.,-5.})
                    std::cout<<"ACCEPT_NUMERIC "<<label<<" flag="<<(flag?"ON":"OFF")<<" d="<<d
                             <<" | preview_ms "<<stats(evaluation[{flag,d}])<<" | frame_ms "<<stats(frame[{flag,d}])
                             <<" | ok_ms "<<stats(ok[{flag,d}])<<'\n';
                // Instrumented pass: stage breakdown and per-update call counts.
                for(bool flag:{false,true})for(double d:{15.,-5.}) {
                    std::map<std::string,std::vector<double>> stage;std::map<std::string,unsigned> count;
                    const char* stages[]={"BRepReconstruction.SourceAdmission","MoveFace.BoxProvisional.Eligibility",
                        "BRepReconstruction.planPlanarSweep","MoveFace.BoxProvisional.Construction","MoveFace.BoxProvisional.DisplayChecks",
                        "MoveFace.BoxProvisional.Publication","MoveFace.BoxProvisional.Total","MoveFace.movePartSnapshot",
                        "MoveFace.CadViewport::redrawFacePreview","MoveFaceGeometry.MoveFaceGeometry::move","MoveFace.capturePreviewCertificate",
                        "MoveFace.CadViewport::previewMoveFace"};
                    const char* counted[]={"BRepCertification.singleSolid","OCCT.selfInterference.copy-and-perform","BRepCertification.validBRep",
                        "BRepCertification.withinTolerance","BRepTopology.TopologyIndex::build","MoveFace.movePartSnapshot",
                        "BRepReconstruction.BRepReconstruction::neighborhood","BRepReconstruction.planPlanarSweep","BRepReconstruction.SourceAdmission",
                        "MoveFaceGeometry.MoveFaceGeometry::move","BRepCertification.equivalent"};
                    std::vector<double> okFull;std::vector<unsigned> okFullCalls,okReuse;
                    for(int trial=0;trial<3;++trial) {
                        start(flag);BRepTiming::reset();BRepTiming::enabled=true;double e=0,f=0;evaluate(d,e,f);BRepTiming::enabled=false;
                        require(routed(flag),"instrumented route changed");
                        for(const char* name:stages)stage[name].push_back(inclusive(name));
                        if(trial==0)for(const char* name:counted)count[name]=calls(name);
                        BRepTiming::reset();BRepTiming::enabled=true;commit(d);BRepTiming::enabled=false;
                        okFull.push_back(inclusive("MoveFaceGeometry.MoveFaceGeometry::move"));
                        okFullCalls.push_back(calls("MoveFaceGeometry.MoveFaceGeometry::move"));okReuse.push_back(calls("MoveFace.certifiedPreviewReused"));
                    }
                    for(size_t i=0;i<okFullCalls.size();++i)
                        require(flag?(okFullCalls[i]==1&&okReuse[i]==0):(okFullCalls[i]==0&&okReuse[i]==1),"OK certification/P2 policy changed");
                    std::cout<<"ACCEPT_STAGES "<<label<<" flag="<<(flag?"ON":"OFF")<<" d="<<d;
                    for(const char* name:stages){std::vector<double> x=stage[name];std::sort(x.begin(),x.end());std::cout<<" "<<name<<"="<<x[1];}
                    std::sort(okFull.begin(),okFull.end());std::cout<<" ok_full_move_ms="<<okFull[1]<<" ok_full_calls="<<okFullCalls[0]<<" ok_p2_reuse="<<okReuse[0]<<'\n';
                    std::cout<<"ACCEPT_CALLS "<<label<<" flag="<<(flag?"ON":"OFF")<<" d="<<d;
                    for(const char* name:counted)std::cout<<" "<<name<<"="<<count[name];
                    std::cout<<'\n';
                }
                // Manipulator drag: zero jitter (12 events through 0 mm), then a ramp.
                const std::vector<int> path{1,2,3,4,5,6,5,4,3,2,1,0,4,8,12,16,20,24,28,32,36,40};
                for(bool flag:{false,true}) {
                    std::vector<double> zeroHandle,rampHandle,zeroFrame,rampFrame;
                    auto drag=[&](bool record) {
                        start(flag);const auto& s=*v.commandSession_;
                        const auto grab=project(v,s.pivot.Translated(gp_Vec(s.direction)*15));
                        const auto base=project(v,s.pivot),tip=project(v,s.pivot.Translated(gp_Vec(s.direction)*31));
                        const double length=std::max(1.,std::hypot(double(tip.x()-base.x()),double(tip.y()-base.y())));
                        const QPointF axis=QPointF(tip-base)/length;
                        sendMouse(v,QEvent::MouseMove,grab,Qt::NoButton,Qt::NoButton);
                        sendMouse(v,QEvent::MouseButtonPress,grab,Qt::LeftButton,Qt::LeftButton);
                        require(v.draggingFace_,"acceptance drag did not start");QPoint last=grab;
                        for(size_t i=0;i<path.size();++i) {
                            last=grab+QPoint(qRound(axis.x()*path[i]),qRound(axis.y()*path[i]));
                            QMouseEvent event(QEvent::MouseMove,QPointF(last),QPointF(v.mapToGlobal(last)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
                            QElapsedTimer timer;timer.start();QApplication::sendEvent(&v,&event);const double handle=timer.nsecsElapsed()/1.e6;
                            QApplication::processEvents();const double whole=timer.nsecsElapsed()/1.e6;
                            if(record){(i<12?zeroHandle:rampHandle).push_back(handle);(i<12?zeroFrame:rampFrame).push_back(whole);}
                        }
                        sendMouse(v,QEvent::MouseButtonRelease,last,Qt::LeftButton,Qt::NoButton);
                        require(routed(flag)&&std::abs(v.commandSession_->value)>1,"acceptance drag route changed");
                        QKeyEvent esc(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(v.dragDimension_,&esc);
                        require(!v.commandSession_&&v.boxes_[0].shape.IsSame(original.shape),"acceptance Escape");pump(20);
                    };
                    for(int rep=0;rep<5;++rep)drag(true);
                    std::cout<<"ACCEPT_DRAG "<<label<<" flag="<<(flag?"ON":"OFF")<<" | zero_handle_ms "<<stats(zeroHandle)
                             <<" | zero_frame_ms "<<stats(zeroFrame)<<" | ramp_handle_ms "<<stats(rampHandle)<<" | ramp_frame_ms "<<stats(rampFrame)<<'\n';
                    BRepTiming::reset();BRepTiming::enabled=true;drag(false);BRepTiming::enabled=false;
                    std::cout<<"ACCEPT_DRAG_CALLS "<<label<<" flag="<<(flag?"ON":"OFF")<<" events="<<path.size()
                             <<" mouseMove="<<calls("CadViewport.CadViewport::mouseMoveEvent")<<" previewMoveFace="<<calls("MoveFace.CadViewport::previewMoveFace")
                             <<" provisional_total="<<calls("MoveFace.BoxProvisional.Total")<<" source_admission="<<calls("BRepReconstruction.SourceAdmission")
                             <<" singleSolid="<<calls("BRepCertification.singleSolid")<<" full_move="<<calls("MoveFaceGeometry.MoveFaceGeometry::move")
                             <<" plan="<<calls("BRepReconstruction.planPlanarSweep")<<" snapshot="<<calls("MoveFace.movePartSnapshot")
                             <<" redraw="<<calls("MoveFace.CadViewport::redrawFacePreview")<<" paint="<<calls("Viewport.paintEvent")<<'\n';
                }
            }
            // The measured edits leave the document dirty, so MainWindow::maybeSave
            // opens a modal "Unsaved changes" prompt on close. The shared guard
            // answers it with Discard for the duration of the close only.
            MainWindowTest::PromptReport prompts;
            QElapsedTimer closing;closing.start();
            { MainWindowTest::UnsavedPromptGuard guard(window,QString(),prompts); window.close(); }
            std::cout<<"ACCEPT_CLOSE fixture="<<fixture<<" unsaved_prompts_discarded="<<prompts.answered<<" close_ms="<<closing.nsecsElapsed()/1.e6<<'\n';
        }
        qunsetenv("OPENDESIGNER_ENABLE_PROVISIONAL_BOX_PREVIEW");qunsetenv("OPENDESIGNER_BOX_SHADOW");
        std::cout<<"F2.9E.6 acceptance timings complete\n";
    }
    static void interactiveProvisional() {
        using Phase=CadViewport::CommandSession::MovePreviewPhase;
        qunsetenv("OPENDESIGNER_BOX_SHADOW_TEST_FAULT");qputenv("OPENDESIGNER_BOX_SHADOW","0");
        MainWindow window;window.show();QApplication::processEvents();
        auto& v=*window.findChild<CadViewport*>();v.addBox();auto original=v.boxes_[0];
        original.shape=BRepPrimAPI_MakeBox(40,30,20).Shape();original.geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;
        original.blends={};original.orientation=gp_Trsf();original.cylinderPrimitive=original.spherePrimitive=original.conePrimitive=false;
        original.min={0,0,0};original.max={40,30,20};original.baseMin=original.min;original.baseMax=original.max;
        auto start=[&](bool enabled,const TopoDS_Shape& shape,gp_Trsf placement=gp_Trsf()) {
            v.cancelCommandSession();qputenv("OPENDESIGNER_ENABLE_PROVISIONAL_BOX_PREVIEW",enabled?"1":"0");
            auto state=original;state.shape=shape;state.orientation=placement;
            Bnd_Box bounds;BRepBndLib::AddOptimal(shape,bounds,false,false);
            bounds.Get(state.min[0],state.min[1],state.min[2],state.max[0],state.max[1],state.max[2]);state.baseMin=state.min;state.baseMax=state.max;
            v.applyFaceState(0,state);selectFace(v,0,cap(shape,true));require(v.startMoveFace(0,cap(shape,true)),"interactive provisional start failed");
            v.stopMoveNumericPreview();require(v.commandSession_->provisionalPreviewEnabled==enabled,"featureflag not captured at session start");
        };
        auto number=[&](double value){const QSignalBlocker block(v.dragDimension_);v.dragDimension_->setText(QLocale().toString(value,'g',17));};
        auto evaluate=[&](double value) {
            number(value);BRepTiming::reset();BRepTiming::enabled=BRepTiming::detailed=true;
            const auto begin=std::chrono::steady_clock::now();const bool ready=v.updateCommandParameter(false);
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
            BRepTiming::enabled=BRepTiming::detailed=false;require(ready,"interactive preview failed");return ms;
        };
        auto raw=[&] {
            require(v.commandSession_&&v.commandSession_->movePreviewPhase==Phase::Provisional
                &&!v.commandSession_->provisionalCandidate().IsNull()&&!v.commandSession_->valid
                &&v.commandSession_->candidate.IsNull()&&!v.commandSession_->changes.front().faceChange
                &&!v.commandSession_->movePreviewCertificate&&!v.commandSession_->preview.IsNull()
                &&v.commandSession_->preview->Shape().IsEqual(v.commandSession_->provisionalCandidate()),"raw candidate crossed certification boundary");
        };
        for(bool enabled:{true,false})for(double d:{15.,-5.})for(int trial=0;trial<3;++trial) {
            start(enabled,original.shape);const auto before=v.boxes_[0];const auto stamp=v.movePartSnapshot(before);
            const auto undo=v.undoStack_.size(),redo=v.redoStack_.size();const auto previewMs=evaluate(d);
            const auto stat=[](const char* name){return BRepTiming::stats[name].inclusive;};
            const double construction=stat("MoveFace.BoxProvisional.Construction"),checks=stat("MoveFace.BoxProvisional.DisplayChecks");
            const double publication=stat("MoveFace.BoxProvisional.Publication"),eligibility=stat("MoveFace.BoxProvisional.Eligibility");
            const double planMs=stat("BRepReconstruction.planPlanarSweep");
            if(enabled) {
                raw();require(BRepTiming::stats["MoveFaceGeometry.MoveFaceGeometry::move"].calls==0,"provisional path ran full reconstruction");
                for(const auto& e:BRepTiming::details)require(e.kind!="Boolean.MaterialCommon","provisional path ran material Common");
                // Even direct internal commit misuse cannot approve raw geometry.
                v.closeCommandSession(true);raw();
            } else require(v.commandSession_->valid&&v.commandSession_->movePreviewCertificate
                &&v.commandSession_->provisionalCandidate().IsNull(),"flag OFF changed certified route");
            require(v.movePartSnapshot(v.boxes_[0])==stamp&&v.undoStack_.size()==undo&&v.redoStack_.size()==redo,"preview mutated document/history");
            const auto displayed=enabled?v.commandSession_->provisionalCandidate():v.commandSession_->candidate;
            require(BRepCertification::equivalent(displayed,BRepPrimAPI_MakeBox(40,30,20+d).Shape()),"preview differs from independent Box oracle");
            if(enabled&&trial==0) {
                NativeDocumentSnapshot snapshot,loaded;QTemporaryDir directory;QString error;
                require(v.exportDocumentSnapshot(snapshot)&&snapshot.parts[0].shape.IsEqual(before.shape)
                    &&NativeDocumentStorage::save(directory.filePath("provisional.odc"),snapshot,error)
                    &&NativeDocumentStorage::load(directory.filePath("provisional.odc"),loaded,error)
                    &&BRepCertification::equivalent(loaded.parts[0].shape,before.shape)&&v.movePartSnapshot(v.boxes_[0])==stamp,
                    "Save serialized provisional geometry or mutated binding");raw();
            }
            BRepTiming::reset();BRepTiming::enabled=BRepTiming::detailed=true;
            const auto begin=std::chrono::steady_clock::now();v.commandButtons_->button(QDialogButtonBox::Ok)->click();
            const double okMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
            const double fullMs=stat("MoveFaceGeometry.MoveFaceGeometry::move");
            const auto fullCalls=BRepTiming::stats["MoveFaceGeometry.MoveFaceGeometry::move"].calls;
            const auto reuse=BRepTiming::stats["MoveFace.certifiedPreviewReused"].calls;
            unsigned material=0;for(const auto& e:BRepTiming::details)material+=e.kind=="Boolean.MaterialCommon";
            BRepTiming::enabled=BRepTiming::detailed=false;
            require(!v.commandSession_&&v.undoStack_.size()==undo+1&&v.boxes_[0].persistentId==before.persistentId,"OK/history/Part identity failed");
            require(enabled?(fullCalls==1&&material>0&&reuse==0):(fullCalls==0&&reuse==1),"OK certification/P2 policy changed");
            const auto accepted=v.boxes_[0].shape;require(BRepCertification::equivalent(accepted,BRepPrimAPI_MakeBox(40,30,20+d).Shape()),"certified result differs from baseline");
            v.undoPull();require(v.boxes_[0].shape.IsEqual(before.shape),"Undo did not restore source");v.redoPull();
            require(v.boxes_[0].shape.IsEqual(accepted),"Redo did not restore certified shape");v.undoPull();
            std::cout<<"INTERACTIVE_BOX enabled="<<enabled<<" delta="<<d<<" trial="<<trial<<" preview_ms="<<previewMs
                <<" plan_ms="<<planMs<<" eligibility_ms="<<eligibility<<" construction_ms="<<construction<<" checks_ms="<<checks
                <<" publication_ms="<<publication<<" full_ms="<<fullMs<<" ok_ms="<<okMs<<'\n';
        }
        start(true,original.shape);qputenv("OPENDESIGNER_ENABLE_PROVISIONAL_BOX_PREVIEW","0");evaluate(15);raw(); // read once
        auto first=v.commandSession_->provisionalCandidate();evaluate(-5);raw();
        require(!first.IsEqual(v.commandSession_->provisionalCandidate())&&(v.commandSession_->provisional&&v.commandSession_->provisional->value==-5),"repeated preview retained old candidate");
        const auto u=v.undoStack_.size();auto overlay=v.commandSession_->preview;v.commandButtons_->button(QDialogButtonBox::Cancel)->click();
        require(!v.commandSession_&&!v.context_->IsDisplayed(overlay)&&v.undoStack_.size()==u,"Cancel retained overlay/history");
        start(true,original.shape);evaluate(15);key(v,Qt::Key_Escape);require(!v.commandSession_&&v.boxes_[0].shape.IsEqual(original.shape),"Escape failed");
        start(true,original.shape);evaluate(15);{
            const QSignalBlocker block(v.dragDimension_);v.dragDimension_->setText("invalid");}
        require(!v.updateCommandParameter(false)&&v.commandSession_->provisionalCandidate().IsNull()&&!v.commandSession_->valid,"invalid input retained raw candidate");
        start(true,original.shape);v.dragDimension_->selectAll();BRepTiming::reset();BRepTiming::enabled=true;
        key(v,Qt::Key_1,"1");key(v,Qt::Key_5,"5");
        require(v.pendingMoveNumeric_&&BRepTiming::stats["MoveFaceGeometry.MoveFaceGeometry::move"].calls==0,"rapid input evaluated transient full geometry");
        pump(350);BRepTiming::enabled=false;raw();require((v.commandSession_->provisional&&v.commandSession_->provisional->value==15),"debounce published wrong input");
        v.dragDimension_->selectAll();key(v,Qt::Key_Minus,"-");key(v,Qt::Key_5,"5");require(v.pendingMoveNumeric_.has_value(),"pending OK scenario missing");
        BRepTiming::reset();BRepTiming::enabled=true;key(v,Qt::Key_Return);BRepTiming::enabled=false;
        require(!v.commandSession_&&BRepTiming::stats["MoveFaceGeometry.MoveFaceGeometry::move"].calls==1
            &&BRepCertification::equivalent(v.boxes_[0].shape,BRepPrimAPI_MakeBox(40,30,15).Shape()),"pending Enter committed obsolete input");v.undoPull();
        start(true,original.shape);evaluate(15);const auto staleUndo=v.undoStack_.size();
        v.context_->ClearSelected(Standard_False);
        Handle(StdSelect_BRepOwner) changedOwner=new StdSelect_BRepOwner(cap(original.shape,false),v.boxes_[0].presentation,5,Standard_True);
        v.context_->AddOrRemoveSelected(changedOwner,Standard_False);
        {const QSignalBlocker block(&v);v.confirmCommandSession();}
        require(v.commandSession_&&!v.commandSession_->valid&&v.commandSession_->provisionalCandidate().IsNull()
            &&v.undoStack_.size()==staleUndo&&v.boxes_[0].shape.IsEqual(original.shape),"changed selection committed stale preview");
        start(true,original.shape);evaluate(15);auto edited=v.boxes_[0];edited.shape=BRepPrimAPI_MakeBox(40,30,25).Shape();edited.max[2]=edited.baseMax[2]=25;
        v.applyFaceState(0,edited);require(v.commandSession_->provisionalCandidate().IsNull(),"source edit retained provisional overlay");
        {const QSignalBlocker block(&v);v.confirmCommandSession();}
        require(v.boxes_[0].shape.IsEqual(edited.shape)&&v.undoStack_.size()==staleUndo,"stale source committed/rolled back");
        for(const auto& shape:std::vector<TopoDS_Shape>{EllipticalFixture::prism(),SplineFixture::solid()}) {
            start(true,shape);evaluate(15);require(v.commandSession_->valid&&v.commandSession_->provisionalCandidate().IsNull()
                &&BRepTiming::stats["MoveFaceGeometry.MoveFaceGeometry::move"].calls==1,"unsupported geometry failed certified fallback");
        }
        // F2.9G.3: an analytic cylinder cap is now provable by the clear-sweep capability.
        start(true,BRepPrimAPI_MakeCylinder(10,20).Shape());evaluate(15);raw();
        require(BRepTiming::stats["MoveFaceGeometry.MoveFaceGeometry::move"].calls==0,"clear-sweep provisional ran full reconstruction");
        gp_Trsf rotated;rotated.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,0,0)),.5);
        start(true,original.shape,rotated);evaluate(15);require(v.commandSession_->valid&&v.commandSession_->provisionalCandidate().IsNull(),"unsupported placement entered provisional path");
        start(true,original.shape);v.commandSession_->directionSource=CadViewport::MoveDirectionSource::Reference;
        v.commandSession_->directionReferences={{References::Kind::Face,original.persistentId,cap(original.shape,true)}};
        evaluate(15);require(v.commandSession_->valid&&v.commandSession_->provisionalCandidate().IsNull(),"reference direction entered provisional path");
        start(true,original.shape);evaluate(15);v.fitAll();const auto pivot=v.commandSession_->pivot;
        const auto press=project(v,pivot.Translated(gp_Vec(0,0,25)));
        require(v.beginMoveFaceDrag(press),"provisional manipulator could not resume");near(v.moveFaceDragStart_,15,"drag restarted at source position");
        const QPoint moved=press+QPoint(int(10*v.dragScreenX_),int(10*v.dragScreenY_));
        QMouseEvent drag(QEvent::MouseMove,QPointF(moved),QPointF(v.mapToGlobal(moved)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&v,&drag);raw();v.draggingFace_=false;v.cancelCommandSession();
        start(true,original.shape);number(-25);{const QSignalBlocker block(&v);require(!v.updateCommandParameter(false),"invalid height accepted");}
        require(v.commandSession_->provisionalCandidate().IsNull()&&v.boxes_[0].shape.IsEqual(original.shape),"failed construction changed source");
        v.cancelCommandSession();qunsetenv("OPENDESIGNER_ENABLE_PROVISIONAL_BOX_PREVIEW");qunsetenv("OPENDESIGNER_BOX_SHADOW");
        std::cout<<"Interactive provisional Box: flag, certification, debounce, drag, cancellation, stale bindings, Save, fallback and history passed\n";
    }
    // F2.9G.3: geometry-independent clear planar sweep capability in the real
    // Move Face session. Fixtures are synthetic B-Reps (no construction history).
    static void clearSweepSession() {
        using Phase=CadViewport::CommandSession::MovePreviewPhase;
        qunsetenv("OPENDESIGNER_BOX_SHADOW_TEST_FAULT");qputenv("OPENDESIGNER_BOX_SHADOW","0");
        MainWindow window;window.resize(1400,1000);window.show();QApplication::processEvents();
        auto& v=*window.findChild<CadViewport*>();v.initializeViewer();v.addBox();auto templatePart=v.boxes_[0];
        templatePart.geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;templatePart.blends={};
        templatePart.cylinderPrimitive=templatePart.spherePrimitive=templatePart.conePrimitive=false;
        auto prism=[](const std::vector<gp_Pnt>& profile,double height) {
            BRepBuilderAPI_MakePolygon polygon;for(const auto& p:profile)polygon.Add(p);polygon.Close();
            return BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(polygon.Wire()).Face(),gp_Vec(0,0,height),true,true).Shape();
        };
        auto start=[&](bool enabled,const TopoDS_Shape& shape,gp_Trsf placement=gp_Trsf()) {
            v.cancelCommandSession();qputenv("OPENDESIGNER_ENABLE_PROVISIONAL_BOX_PREVIEW",enabled?"1":"0");
            auto state=templatePart;state.shape=shape;state.orientation=placement;
            Bnd_Box bounds;BRepBndLib::AddOptimal(shape,bounds,false,false);
            bounds.Get(state.min[0],state.min[1],state.min[2],state.max[0],state.max[1],state.max[2]);state.baseMin=state.min;state.baseMax=state.max;
            v.applyFaceState(0,state);selectFace(v,0,cap(shape,true));require(v.startMoveFace(0,cap(shape,true)),"clear-sweep session start failed");
            v.stopMoveNumericPreview();
        };
        auto evaluate=[&](double value,bool detailed=false) {
            {const QSignalBlocker block(v.dragDimension_);v.dragDimension_->setText(QLocale().toString(value,'g',17));}
            BRepTiming::reset();BRepTiming::enabled=true;BRepTiming::detailed=detailed;
            QElapsedTimer timer;timer.start();const bool ready=v.updateCommandParameter(false);const double ms=timer.nsecsElapsed()/1.e6;
            BRepTiming::enabled=BRepTiming::detailed=false;require(ready,"clear-sweep preview failed");return ms;
        };
        auto provisional=[&] {
            const auto& c=*v.commandSession_;
            return c.movePreviewPhase==Phase::Provisional&&!c.provisionalCandidate().IsNull()&&!c.valid&&c.candidate.IsNull()
                &&!c.changes.front().faceChange&&!c.movePreviewCertificate&&!c.preview.IsNull()&&c.preview->Shape().IsEqual(c.provisionalCandidate())
                &&!c.moveNumericContextInvalidated;
        };
        auto stat=[](const char* name){return BRepTiming::stats[name];};
        const auto box=BRepPrimAPI_MakeBox(40,30,20).Shape();
        std::vector<std::pair<std::string,TopoDS_Shape>> eligible;
        {   std::vector<gp_Pnt> r;const double c=std::cos(M_PI/6),sn=std::sin(M_PI/6);
            for(auto [x,y]:std::vector<std::pair<double,double>>{{0,0},{40,0},{40,30},{0,30}})r.push_back({x*c-y*sn,x*sn+y*c,0});
            eligible.push_back({"rectangular extrusion (not axis-aligned)",prism(r,20)});}
        {std::vector<gp_Pnt> hex;for(int i=0;i<6;++i)hex.push_back({30*std::cos(i*M_PI/3),30*std::sin(i*M_PI/3),0});eligible.push_back({"convex hexagon extrusion",prism(hex,20)});}
        eligible.push_back({"non-convex L extrusion",prism({{0,0,0},{40,0,0},{40,10,0},{10,10,0},{10,30,0},{0,30,0}},20)});
        eligible.push_back({"obround with hole extrusion",BRepPrimAPI_MakePrism(ObroundFixture::localProfile(0),gp_Vec(0,0,20),true,true).Shape()});
        eligible.push_back({"box with through-slot",BRepAlgoAPI_Cut(box,BRepPrimAPI_MakeBox(gp_Pnt(15,10,-1),gp_Pnt(25,20,30)).Shape()).Shape()});
        for(const auto& [label,shape]:eligible) {
            const auto top=cap(shape,true);const double area=BRepCertification::area(top),volume0=BRepCertification::volume(shape);
            for(double d:{15.,-5.}) {
                // Independent certified expectation (first route) for material comparison.
                QString error;const auto expected=MoveFaceGeometry::move(shape,top,d,error,MoveFaceGeometry::ReconstructionPolicy::AxialInvariantCap);
                require(!expected.IsNull(),"certified expectation failed");
                std::vector<double> preview,ok;double eligibility=0,construction=0,checks=0,publication=0,full=0;
                for(int trial=0;trial<3;++trial) {
                    start(true,shape);const auto before=v.boxes_[0];const auto stamp=v.movePartSnapshot(before);
                    const auto undo=v.undoStack_.size(),redo=v.redoStack_.size();preview.push_back(evaluate(d));
                    require(provisional(),(label+": not provisional").c_str());
                    require(stat("MoveFaceGeometry.MoveFaceGeometry::move").calls==0&&stat("BRepReconstruction.SourceAdmission").calls==1,(label+": certified work during provisional preview").c_str());
                    eligibility=stat("MoveFace.BoxProvisional.Eligibility").inclusive;construction=stat("MoveFace.BoxProvisional.Construction").inclusive;
                    checks=stat("MoveFace.BoxProvisional.DisplayChecks").inclusive;publication=stat("MoveFace.BoxProvisional.Publication").inclusive;
                    const auto shown=v.commandSession_->provisionalCandidate();
                    require(BRepCertification::equivalent(shown,expected),(label+": provisional material differs from certified").c_str());
                    require(std::abs(BRepCertification::volume(shown)-(volume0+area*d))<=1.e-6*std::max(1.,volume0),(label+": provisional volume").c_str());
                    require(v.movePartSnapshot(v.boxes_[0])==stamp&&v.undoStack_.size()==undo&&v.redoStack_.size()==redo,(label+": preview mutated document").c_str());
                    if(trial==0) {   // Save during preview writes only the source.
                        NativeDocumentSnapshot snapshot;require(v.exportDocumentSnapshot(snapshot)&&snapshot.parts[0].shape.IsEqual(before.shape),(label+": Save took provisional geometry").c_str());
                    }
                    BRepTiming::reset();BRepTiming::enabled=true;QElapsedTimer timer;timer.start();
                    v.commandButtons_->button(QDialogButtonBox::Ok)->click();ok.push_back(timer.nsecsElapsed()/1.e6);
                    full=stat("MoveFaceGeometry.MoveFaceGeometry::move").inclusive;
                    const auto fullCalls=stat("MoveFaceGeometry.MoveFaceGeometry::move").calls,reuse=stat("MoveFace.certifiedPreviewReused").calls;BRepTiming::enabled=false;
                    require(!v.commandSession_&&v.undoStack_.size()==undo+1&&fullCalls==1&&reuse==0,(label+": OK did not run full certification once").c_str());
                    const auto accepted=v.boxes_[0].shape;require(BRepCertification::equivalent(accepted,expected),(label+": certified result differs").c_str());
                    v.undoPull();require(v.boxes_[0].shape.IsEqual(before.shape),(label+": Undo").c_str());
                    v.redoPull();require(v.boxes_[0].shape.IsEqual(accepted),(label+": Redo").c_str());v.undoPull();
                }
                std::sort(preview.begin(),preview.end());std::sort(ok.begin(),ok.end());
                std::cout<<"CLEAR_SWEEP "<<label<<" d="<<d<<" preview_ms_median="<<preview[1]<<" eligibility_ms="<<eligibility<<" construction_ms="<<construction
                         <<" checks_ms="<<checks<<" publication_ms="<<publication<<" ok_ms_median="<<ok[1]<<" full_ms="<<full<<'\n';
            }
            // Numeric debounce, pending Enter with the latest value, Cancel and Escape.
            start(true,shape);v.dragDimension_->selectAll();key(v,Qt::Key_1,"1");key(v,Qt::Key_5,"5");
            require(v.pendingMoveNumeric_.has_value(),"clear-sweep debounce not pending");pump(350);
            require(provisional()&&v.commandSession_->provisional->value==15,(label+": debounce").c_str());
            v.dragDimension_->selectAll();key(v,Qt::Key_Minus,"-");key(v,Qt::Key_3,"3");require(v.pendingMoveNumeric_.has_value(),"pending Enter scenario missing");
            const auto undoBefore=v.undoStack_.size();QString error;
            const auto minus3=MoveFaceGeometry::move(shape,top,-3,error,MoveFaceGeometry::ReconstructionPolicy::AxialInvariantCap);
            key(v,Qt::Key_Return);require(!v.commandSession_&&v.undoStack_.size()==undoBefore+1&&BRepCertification::equivalent(v.boxes_[0].shape,minus3),(label+": pending Enter").c_str());
            v.undoPull();
            start(true,shape);evaluate(15);{auto overlay=v.commandSession_->preview;const auto u=v.undoStack_.size();
                v.commandButtons_->button(QDialogButtonBox::Cancel)->click();
                require(!v.commandSession_&&!v.context_->IsDisplayed(overlay)&&v.undoStack_.size()==u&&v.boxes_[0].shape.IsEqual(shape),(label+": Cancel").c_str());}
            start(true,shape);evaluate(-5);key(v,Qt::Key_Escape);require(!v.commandSession_&&v.boxes_[0].shape.IsEqual(shape),(label+": Escape").c_str());
            // Manipulator drag 15 -> through 0 -> -5 -> 10, resumed from the provisional state.
            start(true,shape);evaluate(15);v.fitAll();
            {   const auto& c=*v.commandSession_;const auto grab=project(v,c.pivot.Translated(gp_Vec(c.direction)*(15+15)));
                QMouseEvent press(QEvent::MouseButtonPress,QPointF(grab),QPointF(v.mapToGlobal(grab)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                v.mousePressEvent(&press);require(v.draggingFace_,(label+": drag did not start").c_str());near(v.moveFaceDragStart_,15,"drag restarted at source position");
                std::vector<double> dragMs;bool sawZero=false;
                for(double mm:{-5.,-10.,-15.,-20.,-10.,0.,-5.}) {
                    const auto at=grab+QPoint(qRound(v.dragScreenX_*mm/v.dragMmPerPixel_),qRound(v.dragScreenY_*mm/v.dragMmPerPixel_));
                    QMouseEvent move(QEvent::MouseMove,QPointF(at),QPointF(v.mapToGlobal(at)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
                    QElapsedTimer timer;timer.start();v.mouseMoveEvent(&move);dragMs.push_back(timer.nsecsElapsed()/1.e6);
                    const double value=v.commandSession_->value;
                    require(!v.commandSession_->moveNumericContextInvalidated,(label+": drag invalidated the binding").c_str());
                    if(std::abs(value)<1.e-7)sawZero=true;else require(provisional(),(label+": drag left the provisional route").c_str());
                }
                QMouseEvent release(QEvent::MouseButtonRelease,QPointF(grab),QPointF(v.mapToGlobal(grab)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                v.mouseReleaseEvent(&release);std::sort(dragMs.begin(),dragMs.end());
                std::cout<<"CLEAR_SWEEP_DRAG "<<label<<" events="<<dragMs.size()<<" passed_zero="<<sawZero<<" update_median_ms="<<dragMs[dragMs.size()/2]<<'\n';
                require(provisional(),(label+": drag ended outside the provisional route").c_str());
                key(v,Qt::Key_Escape);require(!v.commandSession_&&v.boxes_[0].shape.IsEqual(shape),(label+": Escape after drag").c_str());
            }
            // Selected Face and source invalidation.
            start(true,shape);evaluate(15);
            {   const auto u=v.undoStack_.size();v.context_->ClearSelected(Standard_False);
                Handle(StdSelect_BRepOwner) other=new StdSelect_BRepOwner(cap(shape,false),v.boxes_[0].presentation,5,Standard_True);
                v.context_->AddOrRemoveSelected(other,Standard_False);
                {const QSignalBlocker block(&v);v.confirmCommandSession();}
                require(v.commandSession_&&!v.commandSession_->valid&&v.commandSession_->provisionalCandidate().IsNull()&&v.undoStack_.size()==u
                    &&v.boxes_[0].shape.IsEqual(shape),(label+": changed Face committed").c_str());}
            start(true,shape);evaluate(15);
            {   auto edited=v.boxes_[0];edited.shape=BRepAlgoAPI_Cut(shape,BRepPrimAPI_MakeBox(gp_Pnt(-100,-100,-1),gp_Pnt(100,100,1)).Shape()).Shape();
                const auto u=v.undoStack_.size();v.applyFaceState(0,edited);
                require(v.commandSession_->provisionalCandidate().IsNull(),(label+": source edit kept overlay").c_str());
                {const QSignalBlocker block(&v);v.confirmCommandSession();}
                require(v.boxes_[0].shape.IsEqual(edited.shape)&&v.undoStack_.size()==u,(label+": stale source committed").c_str());}
            // Save/Open: the reopened identity placement stays provisional.
            start(true,shape);v.cancelCommandSession();
            {   NativeDocumentSnapshot snapshot,loaded;QTemporaryDir directory;QString e;
                require(v.exportDocumentSnapshot(snapshot)&&NativeDocumentStorage::save(directory.filePath("clear.odc"),snapshot,e)
                    &&NativeDocumentStorage::load(directory.filePath("clear.odc"),loaded,e)&&v.importDocumentSnapshot(loaded,e),"Save/Open failed");
                auto reopened=v.boxes_[0];require(reopened.orientation.Form()==gp_CompoundTrsf,"reopened placement form changed");
                qputenv("OPENDESIGNER_ENABLE_PROVISIONAL_BOX_PREVIEW","1");selectFace(v,0,cap(reopened.shape,true));
                require(v.startMoveFace(0,cap(reopened.shape,true)),"reopened start failed");v.stopMoveNumericPreview();evaluate(-5);
                require(provisional(),(label+": reopened geometry not provisional").c_str());v.cancelCommandSession();}
            std::cout<<"CLEAR_SWEEP_SESSION "<<label<<" passed\n";
        }
        // Unsupported or unproven geometry keeps the certified preview (fallback).
        std::vector<std::pair<std::string,TopoDS_Shape>> fallback{
            {"analytic cap over NURBS-converted body",BRepAlgoAPI_Fuse(BRepPrimAPI_MakeBox(gp_Pnt(0,0,10),gp_Pnt(40,30,20)).Shape(),
                BRepBuilderAPI_NurbsConvert(BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),gp_Pnt(40,30,10)).Shape(),true).Shape()).Shape()},
            {"near-coincident analytic Faces (5e-6 wall step)",BRepAlgoAPI_Fuse(BRepPrimAPI_MakeBox(gp_Pnt(0,0,0),gp_Pnt(40,30,10)).Shape(),BRepPrimAPI_MakeBox(gp_Pnt(0,0,10),gp_Pnt(40.000005,30,20)).Shape()).Shape()},
            {"elliptical wall extrusion",EllipticalFixture::prism()}};
        for(const auto& [label,shape]:fallback) {
            {   // Start manually: Move Face itself may refuse a B-spline source (existing behavior).
                v.cancelCommandSession();qputenv("OPENDESIGNER_ENABLE_PROVISIONAL_BOX_PREVIEW","1");
                auto state=templatePart;state.shape=shape;state.orientation=gp_Trsf();
                Bnd_Box bounds;BRepBndLib::AddOptimal(shape,bounds,false,false);
                bounds.Get(state.min[0],state.min[1],state.min[2],state.max[0],state.max[1],state.max[2]);state.baseMin=state.min;state.baseMax=state.max;
                v.applyFaceState(0,state);selectFace(v,0,cap(shape,true));
                if(!v.startMoveFace(0,cap(shape,true))) {
                    require(!v.commandSession_&&v.boxes_[0].shape.IsEqual(shape),(label+": refused start changed state").c_str());
                    std::cout<<"CLEAR_SWEEP_FALLBACK "<<label<<" move_face_start_refused=1 (existing source policy; provisional not reached)\n";continue;
                }
                v.stopMoveNumericPreview();
            }
            {const QSignalBlocker block(v.dragDimension_);v.dragDimension_->setText("15");}
            BRepTiming::reset();BRepTiming::enabled=true;v.updateCommandParameter(false);BRepTiming::enabled=false;
            require(v.commandSession_->provisionalCandidate().IsNull()&&v.commandSession_->movePreviewPhase!=Phase::Provisional
                &&stat("MoveFaceGeometry.MoveFaceGeometry::move").calls==1,(label+": unsupported geometry did not fall back").c_str());
            std::cout<<"CLEAR_SWEEP_FALLBACK "<<label<<" certified_valid="<<v.commandSession_->valid
                     <<" source_admissions="<<stat("BRepReconstruction.SourceAdmission").calls<<'\n';
            v.cancelCommandSession();
        }
        {   // Unsupported placement on otherwise eligible geometry.
            gp_Trsf rotated;rotated.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,0,0)),.5);start(true,eligible[1].second,rotated);evaluate(15);
            require(v.commandSession_->valid&&v.commandSession_->provisionalCandidate().IsNull()&&stat("BRepReconstruction.SourceAdmission").calls==0,
                "unsupported placement entered the provisional route");v.cancelCommandSession();
        }
        {   // Flag OFF: the same eligible geometry keeps the certified preview (P2 receipt).
            start(false,eligible[1].second);const double off=evaluate(15);
            require(v.commandSession_->valid&&v.commandSession_->movePreviewCertificate&&v.commandSession_->provisionalCandidate().IsNull(),"flag OFF changed route");
            std::cout<<"CLEAR_SWEEP_FLAG_OFF convex hexagon extrusion d=15 certified_preview_ms="<<off<<'\n';v.cancelCommandSession();
        }
        qunsetenv("OPENDESIGNER_ENABLE_PROVISIONAL_BOX_PREVIEW");qunsetenv("OPENDESIGNER_BOX_SHADOW");
        std::cout<<"Clear planar sweep session: provisional +/-, numeric/debounce, pending Enter, drag through 0, OK certification, Cancel/Escape, Undo/Redo, Save/Open, invalidation, fallbacks passed\n";
        // No window.close(): the edited document would block on the modal Unsaved
        // changes prompt (hang under xvfb); the window is destroyed at scope exit.
    }
    // F2.9G.5: stacked cylindrical wall bands created by an outward planar Move
    // Face are unified; unrelated splits and different-extent bands stay pinned.
    static TopoDS_Face obroundTwoHolesProfile(double z=0) {
        const gp_Pnt a(-30,-20,z),b(30,-20,z),c(30,20,z),d(-30,20,z);
        BRepBuilderAPI_MakeWire outer;outer.Add(BRepBuilderAPI_MakeEdge(a,b).Edge());
        outer.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(b,gp_Pnt(50,0,z),c).Value()).Edge());
        outer.Add(BRepBuilderAPI_MakeEdge(c,d).Edge());outer.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(d,gp_Pnt(-50,0,z),a).Value()).Edge());
        BRepBuilderAPI_MakeFace face(outer.Wire());
        for(double x:{-30.,30.}){auto w=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(x,0,z),gp_Dir(0,0,1)),6)).Edge()).Wire();w.Reverse();face.Add(w);}
        return face.Face();
    }
    static int cylinderFaces(const TopoDS_Shape& s){int n=0;for(TopExp_Explorer f(s,TopAbs_FACE);f.More();f.Next())n+=BRepAdaptor_Surface(TopoDS::Face(f.Current())).GetType()==GeomAbs_Cylinder;return n;}
    // Same-carrier cylinder seams; `plane` restricts to seams whose circle lies in it.
    static int cylinderSeams(const TopoDS_Shape& s,const gp_Pln* plane=nullptr) {
        TopTools_IndexedDataMapOfShapeListOfShape owners;TopExp::MapShapesAndAncestors(s,TopAbs_EDGE,TopAbs_FACE,owners);int n=0;
        for(int i=1;i<=owners.Extent();++i){const auto& L=owners(i);if(L.Extent()!=2)continue;const auto a=TopoDS::Face(L.First()),b=TopoDS::Face(L.Last());
            if(a.IsSame(b)||BRepAdaptor_Surface(a).GetType()!=GeomAbs_Cylinder||BRepAdaptor_Surface(b).GetType()!=GeomAbs_Cylinder)continue;
            if(!BRepCertification::sameCarrier(a,b)&&!BRepCertification::sameCarrier(b,a))continue;
            const BRepAdaptor_Curve c(TopoDS::Edge(owners.FindKey(i)));
            if(plane&&(c.GetType()!=GeomAbs_Circle||plane->Distance(c.Circle().Location())>1.e-6))continue;++n;}
        return n;
    }
    static void stackedCylinderUnification() {
        using Policy=MoveFaceGeometry::ReconstructionPolicy;
        // F2.9G.5 merging is enabled only on validated OCCT (>= 7.9); older
        // kernels must keep the original guarded result (split retained).
        const bool merges=OCC_VERSION_HEX>=0x070900;
        const int unifiedCylinders=merges?4:8,unifiedSeams=merges?0:4;
        std::cout<<"STACKED OCCT "<<OCC_VERSION_COMPLETE<<" merge_enabled="<<merges<<'\n';
        const auto profile=obroundTwoHolesProfile();
        auto prism=[](const TopoDS_Face& f,double h){return BRepPrimAPI_MakePrism(f,gp_Vec(0,0,h),true,true).Shape();};
        const auto source=prism(profile,20);const double area=BRepCertification::area(profile);
        auto capOf=[&](const TopoDS_Shape& s){return cap(s,true);};
        auto planeOf=[](const TopoDS_Face& f){return BRepAdaptor_Surface(f).Plane();};
        QString error;
        // 1. Obround with two holes, outward +10.
        require(cylinderFaces(source)==4,"fixture cylinder count");
        const auto moved=MoveFaceGeometry::move(source,capOf(source),10,error,Policy::AxialInvariantCap);
        require(!moved.IsNull(),"outward planar Move Face refused");double mass=0;
        require(BRepCertification::singleSolid(moved,mass)&&BRepCertification::withinTolerance(moved,1.e-6),"result invalid or out of tolerance");
        require(cylinderFaces(moved)==unifiedCylinders&&cylinderSeams(moved)==unifiedSeams&&countType(moved,TopAbs_FACE)==(merges?8:12),"stacked cylindrical band result wrong for this OCCT");
        require(BRepCertification::equivalent(moved,prism(profile,30))&&std::abs(mass-area*30)<=1.e-8*area*30,"material/volume changed");
        {int holes=0;for(TopExp_Explorer f(moved,TopAbs_FACE);f.More();f.Next()){const BRepAdaptor_Surface a(TopoDS::Face(f.Current()));
            holes+=a.GetType()==GeomAbs_Cylinder&&std::abs(a.Cylinder().Radius()-6)<1.e-9;}
         TopTools_IndexedMapOfShape wires;TopExp::MapShapes(capOf(moved),TopAbs_WIRE,wires);
         require(holes==(merges?2:4)&&wires.Extent()==3,"holes not preserved");}  // unmerged: each hole wall stays split in two
        const auto again=MoveFaceGeometry::move(moved,capOf(moved),3,error,Policy::AxialInvariantCap);
        require(!again.IsNull()&&(!merges||cylinderFaces(again)==4)&&BRepCertification::equivalent(again,prism(profile,33)),"second Move Face on the result");
        // Inward moves create no seams; unchanged.
        const auto inward=MoveFaceGeometry::move(source,capOf(source),-5,error,Policy::AxialInvariantCap);
        require(!inward.IsNull()&&cylinderFaces(inward)==4&&BRepCertification::equivalent(inward,prism(profile,15)),"inward move");
        // 2. Rotated planar Face (arbitrary orientation, not a Z plane).
        {   gp_Trsf r;r.SetRotation(gp_Ax1(gp_Pnt(3,-2,1),gp_Dir(1,2,3)),0.4);
            BRepBuilderAPI_Transform rotate(source,r,true);const auto rotated=rotate.Shape();
            const auto rotatedCap=TopoDS::Face(rotate.ModifiedShape(capOf(source)));
            const auto result=MoveFaceGeometry::move(rotated,rotatedCap,10,error,Policy::AxialInvariantCap);
            require(!result.IsNull()&&cylinderFaces(result)==unifiedCylinders&&cylinderSeams(result)==unifiedSeams,"rotated planar Face band result wrong");
            require(BRepCertification::equivalent(result,BRepBuilderAPI_Transform(prism(profile,30),r,true).Shape()),"rotated material");}
        // 3. Pre-existing unrelated splits (all walls split at z=10) must stay.
        {   const auto split=BRepAlgoAPI_Fuse(prism(profile,10),BRepPrimAPI_MakePrism(obroundTwoHolesProfile(10),gp_Vec(0,0,10),true,true).Shape()).Shape();
            const gp_Pln old10(gp_Pnt(0,0,10),gp_Dir(0,0,1)),old20(gp_Pnt(0,0,20),gp_Dir(0,0,1));
            require(cylinderSeams(split,&old10)==4,"split fixture");
            const auto result=MoveFaceGeometry::move(split,capOf(split),10,error,Policy::AxialInvariantCap);
            require(!result.IsNull()&&BRepCertification::equivalent(result,prism(profile,30)),"pre-split move");
            std::cout<<"STACKED pre-split: seams_at_z10="<<cylinderSeams(result,&old10)<<" seams_at_z20="<<cylinderSeams(result,&old20)<<" cylinders="<<cylinderFaces(result)<<'\n';
            require(cylinderSeams(result,&old10)==4&&cylinderSeams(result,&old20)==unifiedSeams,"unrelated pre-existing split merged, or new seam wrong");}
        // 4. Different axial extents: the wall's top is partly at the cap (z=20)
        //    and partly at a notch (z=15); its seam spans only part of the wall.
        {   const auto stepped=BRepAlgoAPI_Cut(BRepPrimAPI_MakeCylinder(20,20).Shape(),BRepPrimAPI_MakeBox(gp_Pnt(10,-30,15),gp_Pnt(30,30,25)).Shape()).Shape();
            const auto top=capOf(stepped);const gp_Pln old20(gp_Pnt(0,0,20),gp_Dir(0,0,1));
            const auto result=MoveFaceGeometry::move(stepped,top,5,error,Policy::AxialInvariantCap);
            std::cout<<"STACKED different-extent: certified="<<!result.IsNull()<<" cylinders="<<(result.IsNull()?0:cylinderFaces(result))
                     <<" seams_in_cap_plane="<<(result.IsNull()?0:cylinderSeams(result,&old20))<<" error="<<error.left(100).toStdString()<<'\n';
            require(!result.IsNull(),"different-extent planar move refused");
            require(cylinderSeams(result,&old20)>=1,"different-extent cylindrical band was merged (protection lost)");
            double v=0;require(BRepCertification::singleSolid(result,v),"different-extent result invalid");}
        // 5. Session: certified OK, Undo/Redo, Save/Open, second Move Face (flag OFF).
        qunsetenv("OPENDESIGNER_ENABLE_PROVISIONAL_BOX_PREVIEW");qputenv("OPENDESIGNER_BOX_SHADOW","0");
        MainWindow window;window.resize(1200,900);window.show();QApplication::processEvents();
        auto& v=*window.findChild<CadViewport*>();v.initializeViewer();v.addBox();auto part=v.boxes_[0];
        part.shape=source;part.geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;part.blends={};part.orientation=gp_Trsf();
        part.cylinderPrimitive=part.spherePrimitive=part.conePrimitive=false;
        {Bnd_Box b;BRepBndLib::AddOptimal(source,b,false,false);b.Get(part.min[0],part.min[1],part.min[2],part.max[0],part.max[1],part.max[2]);part.baseMin=part.min;part.baseMax=part.max;}
        v.applyFaceState(0,part);
        auto commit=[&](double d) {
            const auto shape=v.boxes_[0].shape;selectFace(v,0,cap(shape,true));require(v.startMoveFace(0,cap(shape,true)),"session start");
            v.stopMoveNumericPreview();{const QSignalBlocker block(v.dragDimension_);v.dragDimension_->setText(QLocale().toString(d,'g',17));}
            require(v.updateCommandParameter(false)&&v.commandSession_->valid,"certified preview");
            const auto undo=v.undoStack_.size();v.commandButtons_->button(QDialogButtonBox::Ok)->click();
            require(!v.commandSession_&&v.undoStack_.size()==undo+1,"OK did not commit once");
        };
        commit(10);const auto committed=v.boxes_[0].shape;
        require(cylinderFaces(committed)==unifiedCylinders&&cylinderSeams(committed)==unifiedSeams&&BRepCertification::equivalent(committed,prism(profile,30)),"session result wrong");
        v.undoPull();require(v.boxes_[0].shape.IsEqual(source),"Undo");v.redoPull();require(v.boxes_[0].shape.IsEqual(committed),"Redo");
        {   NativeDocumentSnapshot snapshot,loaded;QTemporaryDir directory;QString e;
            require(v.exportDocumentSnapshot(snapshot)&&NativeDocumentStorage::save(directory.filePath("stacked.odc"),snapshot,e)
                &&NativeDocumentStorage::load(directory.filePath("stacked.odc"),loaded,e)&&v.importDocumentSnapshot(loaded,e),"Save/Open");
            require(cylinderFaces(v.boxes_[0].shape)==unifiedCylinders&&cylinderSeams(v.boxes_[0].shape)==unifiedSeams&&BRepCertification::equivalent(v.boxes_[0].shape,committed),"Save/Open changed the solid");}
        commit(3);require((!merges||cylinderFaces(v.boxes_[0].shape)==4)&&BRepCertification::equivalent(v.boxes_[0].shape,prism(profile,33)),"second session Move Face");
        // No window.close(): the edited document would open the modal Unsaved
        // changes prompt; the window is destroyed at scope exit without it.
        qunsetenv("OPENDESIGNER_BOX_SHADOW");
        std::cout<<"Stacked cylindrical band unification: obround, rotated Face, unrelated splits, different extents, OK, Undo/Redo, Save/Open, second move passed\n";
    }
    // F2.9G.5A: the same obround built through the production Sketch pipeline,
    // with the holes either in the extruded profile or added afterwards by a
    // separate Sketch Cut. The Cut leaves hole walls on carriers of opposite
    // parametrization handedness to the swept extension (opposite raw Face
    // orientation, same material side); both must unify identically.
    static void stackedCylinderConstructionMethods() {
        using namespace Sketch;
        const bool merges=OCC_VERSION_HEX>=0x070900;
        const int faces=merges?8:12,cylinders=merges?4:8,seams=merges?0:4,holeFaces=merges?2:4;
        auto entity=[](QString id,Geometry g){Entity x;x.id=id;x.workplaneId="wp";x.geometry=g;return x;};
        const double pi=std::numbers::pi;
        const std::vector<Entity> outline{entity("a",Segment{{-30,-20},{30,-20}}),entity("r",Arc{{30,0},20,-pi/2,pi}),
                                          entity("c",Segment{{30,20},{-30,20}}),entity("l",Arc{{-30,0},20,pi/2,pi})};
        const std::vector<Entity> holes{entity("h1",Circle{{-30,0},6}),entity("h2",Circle{{30,0},6})};
        const auto expected=BRepPrimAPI_MakePrism(obroundTwoHolesProfile(),gp_Vec(0,0,30),true,true).Shape();
        auto build=[](const std::vector<Entity>& input){
            const auto built=SketchProfileBuilder::build(SketchProfileValidation::validate(input,"wp"),gp_Ax3());
            require(built.success,"Sketch profile construction failed");return built;};
        auto faceCount=[](const TopoDS_Shape& x){int n=0;for(TopExp_Explorer f(x,TopAbs_FACE);f.More();f.Next())++n;return n;};
        // Hole walls: radius 6 on the axes x=+-30 (parallel to Z), covering z in [0,30] exactly.
        auto holesUnchanged=[&](const TopoDS_Shape& x) {
            int count=0;double area=0;
            for(TopExp_Explorer f(x,TopAbs_FACE);f.More();f.Next()) {
                const auto face=TopoDS::Face(f.Current());const BRepAdaptor_Surface s(face);
                if(s.GetType()!=GeomAbs_Cylinder||std::abs(s.Cylinder().Radius()-6)>1.e-9) continue;
                const auto c=s.Cylinder();const gp_Lin axis(c.Axis());
                if(!c.Axis().Direction().IsParallel(gp_Dir(0,0,1),1.e-12)) return false;
                if(axis.Distance(gp_Pnt(-30,0,0))>1.e-9&&axis.Distance(gp_Pnt(30,0,0))>1.e-9) return false;
                ++count;area+=BRepCertification::area(face);
            }
            return count==holeFaces&&std::abs(area-2*2*pi*6*30)<1.e-6;
        };
        qunsetenv("OPENDESIGNER_ENABLE_PROVISIONAL_BOX_PREVIEW");qputenv("OPENDESIGNER_BOX_SHADOW","0");
        for(const bool separateCut:{false,true}) {
            const char* method=separateCut?"separate Cut":"sketch profile";
            TopoDS_Shape source;
            if(separateCut) {
                const auto prism=SketchProfileExtrusion::extrude(build(outline),gp_Dir(0,0,1),20);require(prism.success,"outline extrusion");
                const auto cut=SketchProfileExtrusion::cut(build(holes),gp_Ax3(),prism.shape,gp_Trsf(),20,false,true);
                require(cut.success,"separate hole Cut");source=cut.shape;
            } else {
                auto all=outline;all.insert(all.end(),holes.begin(),holes.end());
                const auto prism=SketchProfileExtrusion::extrude(build(all),gp_Dir(0,0,1),20);require(prism.success,"profile extrusion");source=prism.shape;
            }
            require(faceCount(source)==8&&cylinderFaces(source)==4&&BRepCertification::equivalent(source,BRepPrimAPI_MakePrism(obroundTwoHolesProfile(),gp_Vec(0,0,20),true,true).Shape()),"construction fixture");
            MainWindow window;window.resize(1200,900);window.show();QApplication::processEvents();
            auto& v=*window.findChild<CadViewport*>();v.initializeViewer();v.addBox();auto part=v.boxes_[0];
            part.shape=source;part.geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;part.blends={};part.orientation=gp_Trsf();
            part.cylinderPrimitive=part.spherePrimitive=part.conePrimitive=false;
            {Bnd_Box b;BRepBndLib::AddOptimal(source,b,false,false);b.Get(part.min[0],part.min[1],part.min[2],part.max[0],part.max[1],part.max[2]);part.baseMin=part.min;part.baseMax=part.max;}
            v.applyFaceState(0,part);
            const auto before=v.boxes_[0].shape;const auto top=cap(before,true);
            selectFace(v,0,top);require(v.startMoveFace(0,top),"session start");
            v.stopMoveNumericPreview();{const QSignalBlocker block(v.dragDimension_);v.dragDimension_->setText(QLocale().toString(10.,'g',17));}
            require(v.updateCommandParameter(false)&&v.commandSession_->valid,"certified preview");
            const auto undo=v.undoStack_.size();v.commandButtons_->button(QDialogButtonBox::Ok)->click();
            require(!v.commandSession_&&v.undoStack_.size()==undo+1,"OK did not commit once");
            const auto committed=v.boxes_[0].shape;double volume=0;
            std::cout<<"STACKED "<<method<<": faces="<<faceCount(committed)<<" cylinders="<<cylinderFaces(committed)<<" seams="<<cylinderSeams(committed)<<'\n';
            auto verify=[&](const TopoDS_Shape& x,const char* stage) {
                const std::string at=std::string(method)+" "+stage;
                require(faceCount(x)==faces&&cylinderFaces(x)==cylinders&&cylinderSeams(x)==seams,(at+": Face/cylinder/seam count").c_str());
                require(BRepCertification::singleSolid(x,volume)&&BRepCheck_Analyzer(x).IsValid(),(at+": invalid solid").c_str());
                require(BRepCertification::equivalent(x,expected),(at+": material not preserved").c_str());
                require(holesUnchanged(x),(at+": hole geometry changed").c_str());
            };
            verify(committed,"OK");
            v.undoPull();require(v.boxes_[0].shape.IsEqual(before),"Undo");
            v.redoPull();require(v.boxes_[0].shape.IsEqual(committed),"Redo");verify(v.boxes_[0].shape,"Redo");
            // No window.close(): the edited document would open the modal prompt.
        }
        qunsetenv("OPENDESIGNER_BOX_SHADOW");
        std::cout<<"Stacked cylindrical unification for sketch-profile and separately Cut holes passed\n";
    }
    // F2.9D.4: identity placement eligibility after Save/Open.
    static void provisionalPlacement() {
        using Phase=CadViewport::CommandSession::MovePreviewPhase;
        namespace B=ProvisionalBoxPreview;
        qunsetenv("OPENDESIGNER_BOX_SHADOW_TEST_FAULT");qputenv("OPENDESIGNER_BOX_SHADOW","0");
        auto describe=[](const char* label,const CadViewport::CadPart& part) {
            const auto& t=part.orientation;const auto shown=part.presentation->LocalTransformation();
            std::cout<<std::setprecision(17)<<"PLACEMENT "<<label<<" form="<<int(t.Form())<<" display_form="<<int(shown.Form())
                     <<" scale="<<t.ScaleFactor()<<" negative="<<t.IsNegative()<<" translation=("<<t.TranslationPart().X()<<','
                     <<t.TranslationPart().Y()<<','<<t.TranslationPart().Z()<<") matrix=[";
            for(int r=1;r<=3;++r)for(int c=1;c<=4;++c)std::cout<<t.Value(r,c)<<(r==3&&c==4?"]":",");
            B::Point lo,hi;const bool shapeEligible=B::eligible(part.shape,cap(part.shape,true),lo,hi);
            std::cout<<" shape_location_identity="<<part.shape.Location().IsIdentity()<<" shape_eligible="<<shapeEligible
                     <<" old_form_gate="<<(t.Form()==gp_Identity&&shown.Form()==gp_Identity)
                     <<" new_matrix_gate="<<(B::identityPlacement(t)&&B::identityPlacement(shown))<<'\n';
            return shapeEligible;
        };
        // 1. Matrix-level contract, including representations built through public OCCT APIs only.
        auto values=[](double a11,double a12,double a13,double a14,double a21,double a22,double a23,double a24,
                       double a31,double a32,double a33,double a34){gp_Trsf t;t.SetValues(a11,a12,a13,a14,a21,a22,a23,a24,a31,a32,a33,a34);return t;};
        const gp_Trsf compoundIdentity=values(1,0,0,0,0,1,0,0,0,0,1,0);
        require(compoundIdentity.Form()==gp_CompoundTrsf,"public SetValues identity no longer labelled gp_CompoundTrsf");
        require(B::identityPlacement(gp_Trsf())&&B::identityPlacement(compoundIdentity),"exact identity placement rejected");
        std::vector<std::pair<const char*,gp_Trsf>> refused;
        {gp_Trsf t;t.SetTranslation(gp_Vec(5,0,0));refused.push_back({"translation 5 mm",t});}
        {gp_Trsf t;t.SetTranslation(gp_Vec(0,0,1.e-12));refused.push_back({"translation 1e-12 mm",t});}
        refused.push_back({"compound translation 1e-12 mm",values(1,0,0,0,0,1,0,0,0,0,1,1.e-12)});
        {gp_Trsf t;t.SetTranslation(gp_Vec(0,0,0));refused.push_back({"zero gp_Translation label",t});}
        {gp_Trsf t;t.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(0,0,1)),M_PI/6);refused.push_back({"rotation 30 deg",t});}
        {gp_Trsf t;t.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(0,0,1)),1.e-12);refused.push_back({"rotation 1e-12 rad",t});}
        refused.push_back({"compound rotation 30 deg",values(std::cos(M_PI/6),-std::sin(M_PI/6),0,0,std::sin(M_PI/6),std::cos(M_PI/6),0,0,0,0,1,0)});
        {gp_Trsf t;t.SetScale(gp_Pnt(),2);refused.push_back({"scale 2",t});}
        refused.push_back({"compound scale 2",values(2,0,0,0,0,2,0,0,0,0,2,0)});
        refused.push_back({"compound near-unit scale",values(1,0,0,0,0,1,0,0,0,0,1+1.e-12,0)});
        {gp_Trsf t;t.SetMirror(gp_Ax2(gp_Pnt(),gp_Dir(0,0,1)));refused.push_back({"plane mirror",t});}
        {gp_Trsf t;t.SetMirror(gp_Pnt());refused.push_back({"point mirror",t});}
        refused.push_back({"compound negative determinant",values(-1,0,0,0,0,1,0,0,0,0,1,0)});
        {   // gp_Trsf cannot hold shear: SetValues orthogonalizes it. Judge what is stored.
            const auto sheared=values(1,1.e-3,0,0,0,1,0,0,0,0,1,0);bool exact=true;
            for(int r=1;r<=3;++r)for(int c=1;c<=4;++c)exact=exact&&sheared.Value(r,c)==(r==c?1.:0.);
            std::cout<<"PLACEMENT_CONTRACT shear_input_stored_as_exact_identity="<<exact<<'\n';
            require(B::identityPlacement(sheared)==exact,"shear representation judged by label instead of stored matrix");
        }
        {gp_Trsf t;t.SetTranslation(gp_Vec(5,0,0));t.SetForm(gp_Identity);refused.push_back({"mislabelled translation",t});}
        for(const auto& [name,t]:refused) {
            std::cout<<"PLACEMENT_CONTRACT refused="<<name<<" form="<<int(t.Form())<<'\n';
            require(!B::identityPlacement(t),name);
        }
        {   // Incremental gate cost: two constant-time matrix checks per evaluation.
            volatile bool sink=false;const int n=2000000;QElapsedTimer timer;timer.start();
            for(int i=0;i<n;++i)sink=sink^B::identityPlacement((i&1)?compoundIdentity:refused[size_t(i)%refused.size()].second);
            std::cout<<"PLACEMENT_GATE_COST ns_per_check="<<double(timer.nsecsElapsed())/n<<'\n';
        }

        {   // F2.9E.5: evaluation-local source admission evidence.
            using Admission=BRepReconstruction::SourceAdmission;
            static_assert(!std::is_copy_constructible_v<Admission>&&!std::is_move_constructible_v<Admission>
                &&!std::is_copy_assignable_v<Admission>,"source evidence must stay evaluation-local");
            const auto box=BRepPrimAPI_MakeBox(40,30,20).Shape();const auto top=cap(box,true),bottom=cap(box,false);
            const Admission admitted(box,top);
            gp_Trsf shift;shift.SetTranslation(gp_Vec(1,0,0));
            const auto copy=BRepBuilderAPI_Copy(box,true,false).Shape();
            require(admitted.valid()&&admitted.binds(box,top),"valid source evidence rejected");
            require(!admitted.binds(box,bottom)&&!admitted.binds(copy,cap(copy,true))&&!admitted.binds(box.Moved(TopLoc_Location(shift)),top)
                &&!admitted.binds(box.Reversed(),top)&&!admitted.binds(box,TopoDS::Face(top.Reversed())),"evidence bound to another source/Face/placement");
            auto planCalls=[&](const TopoDS_Shape& s,const TopoDS_Face& f,const Admission* evidence,bool& ok,double& volume) {
                BRepReconstruction::PlanarSweepPlan plan;QString e;BRepTiming::reset();BRepTiming::enabled=true;
                ok=BRepReconstruction::planPlanarSweep(s,f,false,plan,e,evidence);BRepTiming::enabled=false;volume=plan.local.certifiedVolume;
                return BRepTiming::stats["BRepCertification.singleSolid"].calls;
            };
            bool ok=false;double planVolume=0;
            require(planCalls(box,top,&admitted,ok,planVolume)==0&&ok&&planVolume==admitted.volume(),"binding evidence not reused");
            require(planCalls(box,top,nullptr,ok,planVolume)==1&&ok,"certified plan lost its source certificate");
            require(planCalls(copy,cap(copy,true),&admitted,ok,planVolume)==1&&ok,"foreign evidence skipped certification");
            require(planCalls(box,bottom,&admitted,ok,planVolume)==1&&ok,"other-Face evidence skipped certification");
            // Invalid sources yield invalid evidence, which never skips certification.
            BRep_Builder builder;TopoDS_Compound twoSolids;builder.MakeCompound(twoSolids);
            builder.Add(twoSolids,box);builder.Add(twoSolids,BRepPrimAPI_MakeBox(gp_Pnt(100,0,0),10,10,10).Shape());
            const TopoDS_Shape shellOnly=BRepPrimAPI_MakeBox(10,10,10).Shell(); // no solid
            for(const auto& invalid:std::vector<TopoDS_Shape>{twoSolids,shellOnly}) {
                const auto face=cap(invalid,true);const Admission rejected(invalid,face);
                require(!rejected.valid()&&!rejected.binds(invalid,face),"invalid source produced evidence");
                require(planCalls(invalid,face,&rejected,ok,planVolume)==1&&!ok,"invalid source admitted by plan");
            }
            std::cout<<"SOURCE_ADMISSION binding, reuse, foreign/other-Face/placement/orientation refusal and invalid sources passed\n";
        }
        {   // F2.9F.1: generic negotiation contract. Same decisions as the former
            // direct Box dispatch; one source certificate per call; no broadening.
            namespace P=BRepReconstruction::Provisional;
            const auto box=BRepPrimAPI_MakeBox(40,30,20).Shape();
            auto request=[&](const TopoDS_Shape& s,const TopoDS_Face& f) {
                P::PlanarMoveRequest r;r.source=s;r.face=f;r.planarFaceMove=r.alongOutwardNormal=r.invariantCapPolicy=true;return r;
            };
            const auto base=request(box,cap(box,true));
            require(P::admitOperation(base)==P::Admission::CertifiedOnly,"supported request refused before geometry");
            std::vector<std::pair<const char*,P::PlanarMoveRequest>> unsupported;
            {auto r=base;r.planarFaceMove=false;unsupported.push_back({"non-planar route",r});}
            {auto r=base;r.blendFeatures=true;unsupported.push_back({"blend features",r});}
            {auto r=base;r.alongOutwardNormal=false;unsupported.push_back({"reference/reversed direction",r});}
            {auto r=base;r.invariantCapPolicy=false;unsupported.push_back({"other certified policy",r});}
            {auto r=base;r.placement.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,0,0)),.5);unsupported.push_back({"rotated placement",r});}
            {auto r=base;r.displayed.SetTranslation(gp_Vec(5,0,0));unsupported.push_back({"moved presentation",r});}
            {auto r=base;r.placement=values(1,0,0,0,0,1,0,0,0,0,1,0);r.displayed=r.placement;
             require(P::admitOperation(r)==P::Admission::CertifiedOnly,"reopened identity placement refused");}
            for(const auto& [name,r]:unsupported) {
                BRepTiming::reset();BRepTiming::enabled=true;const auto c=P::negotiatePlanarMove(r,15);BRepTiming::enabled=false;
                require(P::admitOperation(r)==P::Admission::Unsupported&&c.admission==P::Admission::Unsupported
                    &&c.outcome==P::Outcome::CertifiedOnly&&c.shape.IsNull()&&BRepTiming::stats.empty(),name);
            }
            gp_Trsf rotation;rotation.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,0,0)),.5);
            BRepBuilderAPI_Transform rotate(box,rotation,true);const auto rotated=rotate.Shape();
            const auto rotatedTop=TopoDS::Face(rotate.ModifiedShape(cap(box,true))); // the former +Z Face
            for(const auto& [s,f]:std::vector<std::pair<TopoDS_Shape,TopoDS_Face>>{{box,cap(box,false)},{rotated,rotatedTop},
                    {BRepPrimAPI_MakeCylinder(10,20).Shape(),cap(BRepPrimAPI_MakeCylinder(10,20).Shape(),true)},
                    {EllipticalFixture::prism(),cap(EllipticalFixture::prism(),true)}}) {
                // Box-provider semantics are unchanged: the Box proof refuses these.
                const auto c=P::negotiatePlanarMove(request(s,f),&ProvisionalBoxPreview::provider,15);
                require(c.admission==P::Admission::CertifiedOnly&&c.outcome==P::Outcome::CertifiedOnly&&c.shape.IsNull(),"unproven source admitted");
            }
            {   // F2.9G.3 selection: Box first; the clear-sweep provider only when the
                // Box source proof does not apply; one source certificate either way.
                namespace Clear=ProvisionalPlanarClearance;
                auto selectedFor=[&](const TopoDS_Shape& s,const TopoDS_Face& f,double d,P::Candidate& c) {
                    const P::PlanarMoveProvider* selected=nullptr;BRepTiming::reset();BRepTiming::enabled=true;
                    c=P::constructPlanarMove(request(s,f),d,&selected);BRepTiming::enabled=false;
                    require(BRepTiming::stats["BRepReconstruction.SourceAdmission"].calls==(selected?1u:0u),"source certified more than once");
                    return selected;
                };
                P::Candidate c;
                require(selectedFor(box,cap(box,true),15,c)==&ProvisionalBoxPreview::provider&&c.outcome==P::Outcome::Ready,"Box no longer selected first");
                require(selectedFor(box,cap(box,true),0,c)==&ProvisionalBoxPreview::provider&&c.outcome==P::Outcome::CertifiedOnly,"Box value refusal fell through");
                require(selectedFor(box,cap(box,true),-25,c)==&ProvisionalBoxPreview::provider&&c.outcome==P::Outcome::CertifiedOnly,"Box collapse fell through to another provider");
                const auto cylinder=BRepPrimAPI_MakeCylinder(10,20).Shape();
                require(selectedFor(cylinder,cap(cylinder,true),15,c)==&Clear::provider&&c.outcome==P::Outcome::Ready,"clear sweep not selected for a provable cylinder cap");
                require(selectedFor(box,cap(box,false),5,c)==&Clear::provider&&c.outcome==P::Outcome::Ready,"clear sweep not selected for a provable bottom Face");
                const auto ellipse=EllipticalFixture::prism();
                require(selectedFor(ellipse,cap(ellipse,true),15,c)==nullptr&&c.outcome==P::Outcome::CertifiedOnly,"non-analytic source selected");
                require(selectedFor(rotated,rotatedTop,15,c)==&Clear::provider,"rotated analytic Box not offered to clear sweep");
                const auto spline=SplineFixture::solid();
                require(selectedFor(spline,SplineFixture::cap(spline,20),15,c)==nullptr,"spline source selected");
                require(!Clear::provider.display.planarFaces&&Clear::provider.display.fallBackOnFailure&&!ProvisionalBoxPreview::provider.display.fallBackOnFailure
                    &&Clear::provider.admitsSweep&&!ProvisionalBoxPreview::provider.admitsSweep,"provider policies changed");
                // Display failure policy: CertifiedOnly for the clear sweep, Failed for the Box.
                const auto ready=P::constructPlanarMove(request(cylinder,cap(cylinder,true)),&Clear::provider,15);
                P::DisplayExpectation tiny=Clear::provider.display;tiny.maxFaces=1;
                const auto fallback=P::validateCandidate(request(cylinder,cap(cylinder,true)),15,ready,tiny);
                P::DisplayExpectation strict=tiny;strict.fallBackOnFailure=false;
                const auto failed=P::validateCandidate(request(cylinder,cap(cylinder,true)),15,ready,strict);
                require(ready.outcome==P::Outcome::Ready&&fallback.outcome==P::Outcome::CertifiedOnly&&fallback.shape.IsNull()
                    &&failed.outcome==P::Outcome::Failed&&failed.shape.IsNull(),"display-failure policy wrong");
                auto inconsistent=ready;inconsistent.admission=P::Admission::CertifiedOnly;
                require(P::validateCandidate(request(cylinder,cap(cylinder,true)),15,inconsistent,Clear::provider.display).outcome==P::Outcome::Failed,
                    "consistency failure converted to fallback");
                std::cout<<"NEGOTIATION F2.9G.3 selection, single certificate and display policy passed\n";
            }
            // F2.9F.3: expected outcomes/reasons are the F2.9F.2 baseline (20-case
            // probe); the unvalidated shared construction agrees with negotiation.
            const std::vector<std::tuple<double,P::Outcome,QString>> expected{
                {15.,P::Outcome::Ready,{}},{-5.,P::Outcome::Ready,{}},{0.,P::Outcome::CertifiedOnly,{}},
                {-25.,P::Outcome::CertifiedOnly,{}},{1.e300,P::Outcome::Failed,QStringLiteral("gp_Dir() - input vector has zero norm")}};
            for(const auto& [d,outcome,reason]:expected) {
                BRepTiming::reset();BRepTiming::enabled=true;const auto c=P::negotiatePlanarMove(base,d);BRepTiming::enabled=false;
                const auto unvalidated=P::constructPlanarMove(base,d);
                require(c.admission==P::Admission::Provisional&&c.outcome==outcome&&c.reason==reason
                    &&unvalidated.outcome==outcome&&unvalidated.reason==reason
                    &&BRepTiming::stats["BRepCertification.singleSolid"].calls==1&&BRepTiming::stats["BRepReconstruction.SourceAdmission"].calls==1,
                    "negotiation differs from the F2.9F.2 baseline");
                if(c.outcome==P::Outcome::Ready)
                    require(BRepCertification::equivalent(c.shape,unvalidated.shape)&&BRepCertification::equivalent(c.shape,BRepPrimAPI_MakeBox(40,30,20+d).Shape()),"negotiated geometry differs");
                std::cout<<"NEGOTIATION d="<<d<<" admission="<<int(c.admission)<<" outcome="<<int(c.outcome)<<'\n';
            }
            std::cout<<"NEGOTIATION operation contract, placement, unproven sources, provider equivalence and single certificate passed\n";
            // F2.9F.2: shared candidate validation. Malformed or inconsistent
            // candidates become Failed without a shape; nothing is ever upgraded.
            {   // F2.9F.3/F2.9F.4: the Box provider only proves, through the
                // geometry-neutral contract; selection is the single explicit point.
                const auto& box3=ProvisionalBoxPreview::provider;P::ProviderProof proof;
                {const P::PlanarMoveProvider* selected=nullptr;P::constructPlanarMove(base,15,&selected);require(selected==&box3,"provider selection changed");}
                require(box3.proveSource(box,base.face,proof)&&ProvisionalBoxPreview::lo(proof)==ProvisionalBoxPreview::Point{0,0,0}
                    &&ProvisionalBoxPreview::hi(proof)==ProvisionalBoxPreview::Point{40,30,20}
                    &&!box3.proveSource(box,cap(box,false),proof)&&!box3.proveSource(BRepPrimAPI_MakeCylinder(10,20).Shape(),cap(BRepPrimAPI_MakeCylinder(10,20).Shape(),true),proof),
                    "Box source proof changed");
                box3.proveSource(box,base.face,proof);
                require(box3.admitsValue(proof,-19.99)&&!box3.admitsValue(proof,-20.)&&!box3.admitsValue(proof,-25.)&&box3.admitsValue(proof,1.e-6),"Box collapse rule changed");
                BRepReconstruction::PlanarSweepPlan plan;QString e;
                require(BRepReconstruction::planPlanarSweep(box,base.face,false,plan,e)&&box3.admitsPlan(plan),"Box plan binding refused its own Face");
                auto sideways=plan;sideways.normal=gp_Dir(1,0,0);require(!box3.admitsPlan(sideways),"Box plan binding accepted a non +Z normal");
                require(QString::fromUtf8(box3.planRefusal)==QStringLiteral("Shared plan differs from the provisional Box admission.")
                    &&QString::fromUtf8(box3.display.failure)==QStringLiteral("Provisional Box failed display checks."),"Box refusal messages changed");
                // Shared stages with no provider: nothing is admitted or constructed.
                BRepTiming::reset();BRepTiming::enabled=true;const auto none=P::negotiatePlanarMove(base,nullptr,15);BRepTiming::enabled=false;
                require(none.admission==P::Admission::CertifiedOnly&&none.outcome==P::Outcome::CertifiedOnly&&none.shape.IsNull()
                    &&BRepTiming::stats.empty(),"provider-less negotiation admitted or examined geometry");
            }
            const auto ready=P::constructPlanarMove(base,15);
            require(ready.outcome==P::Outcome::Ready&&!ready.shape.IsNull(),"provider Ready missing");
            auto validated=[&](P::Candidate c,double d,const P::DisplayExpectation& e=ProvisionalBoxPreview::display) {
                BRepTiming::reset();BRepTiming::enabled=true;const auto r=P::validateCandidate(base,d,c,e);BRepTiming::enabled=false;return r;
            };
            const QString inconsistent=QStringLiteral("Provisional candidate is inconsistent with its request.");
            {auto r=validated(ready,15);require(r.outcome==P::Outcome::Ready&&r.shape.IsEqual(ready.shape)&&r.reason.isEmpty()
                &&BRepTiming::stats["BRepCertification.singleSolid"].calls==0&&BRepTiming::stats["BRepCertification.equivalent"].calls==0,
                "valid candidate changed, copied or re-certified");}
            std::vector<std::pair<const char*,std::pair<P::Candidate,double>>> malformed;
            {auto c=ready;c.shape.Nullify();malformed.push_back({"Ready without shape",{c,15}});}
            {auto c=ready;c.shape=box;malformed.push_back({"Ready aliasing the source",{c,15}});}
            {auto c=ready;c.admission=P::Admission::CertifiedOnly;malformed.push_back({"Ready without provider admission",{c,15}});}
            {auto c=ready;c.admission=P::Admission::Unsupported;malformed.push_back({"Ready for unsupported request",{c,15}});}
            malformed.push_back({"Ready for zero displacement",{ready,0.}});
            malformed.push_back({"Ready for NaN displacement",{ready,std::nan("")}});
            for(const auto& [name,input]:malformed) {
                const auto r=validated(input.first,input.second);
                require(r.outcome==P::Outcome::Failed&&r.shape.IsNull()&&r.reason==inconsistent,name);
            }
            const auto cylinder=BRepPrimAPI_MakeCylinder(10,20).Shape();
            {auto c=ready;c.shape=cylinder;const auto r=validated(c,15);
             require(r.outcome==P::Outcome::Failed&&r.shape.IsNull()&&r.reason==QStringLiteral("Provisional Box failed display checks."),
                 "candidate outside the provider's declared form displayed");}
            {auto c=ready;c.shape=box.Reversed();require(validated(c,15).outcome==P::Outcome::Failed,"inside-out candidate displayed");}
            // Generic gate imposes no provider form: a closed cylinder is displayable
            // under the default expectation, never under the Box expectation.
            require(P::displayable(cylinder,P::DisplayExpectation{})&&!P::displayable(cylinder,ProvisionalBoxPreview::display),
                "provider form leaked into, or missing from, the generic display gate");
            for(auto outcome:{P::Outcome::CertifiedOnly,P::Outcome::Failed}) {
                auto c=ready;c.outcome=outcome;c.reason=QStringLiteral("kept");const auto r=validated(c,15);
                require(r.outcome==outcome&&r.shape.IsNull()&&r.reason==QStringLiteral("kept")&&BRepTiming::stats.empty(),"non-Ready result changed or validated");
            }
            std::cout<<"NEGOTIATION candidate validation: consistency, malformed candidates, provider form, no upgrade passed\n";
        }
        // 2. Production Box, fresh versus Save/Open.
        MainWindow window;window.show();QApplication::processEvents();
        auto& v=*window.findChild<CadViewport*>();v.initializeViewer();
        QAction* create=nullptr;for(auto* a:window.findChildren<QAction*>())if(a->text()==QStringLiteral("Create &Box"))create=a;
        require(create,"Create Box action missing");create->trigger();QApplication::processEvents();
        require(v.boxes_.size()==1,"Create Box failed");
        const auto fresh=v.boxes_[0];
        require(describe("fresh",fresh)&&fresh.orientation.Form()==gp_Identity,"fresh Box not eligible/identity form");
        QTemporaryDir directory;QString error;NativeDocumentSnapshot saved,loaded;
        require(v.exportDocumentSnapshot(saved)&&NativeDocumentStorage::save(directory.filePath("placement.odc"),saved,error)
            &&NativeDocumentStorage::load(directory.filePath("placement.odc"),loaded,error)&&v.importDocumentSnapshot(loaded,error),"Save/Open failed");
        QApplication::processEvents();
        const auto reopened=v.boxes_[0];
        const bool reopenedShapeEligible=describe("reopened",reopened);
        require(reopened.orientation.Form()==gp_CompoundTrsf,"Save/Open no longer yields gp_CompoundTrsf; representation case lost");
        require(reopenedShapeEligible,"reopened Box shape rejected for a reason other than placement");
        std::cout<<"PLACEMENT_EQUIVALENCE id="<<(reopened.persistentId==fresh.persistentId)<<" brep_text="<<(brep(reopened.shape)==brep(fresh.shape))
                 <<" equivalent="<<BRepCertification::equivalent(reopened.shape,fresh.shape)<<'\n';
        require(reopened.persistentId==fresh.persistentId&&BRepCertification::equivalent(reopened.shape,fresh.shape),"reopened Box geometry/identity differs from fresh Box");

        auto evaluate=[&](double value) {
            {const QSignalBlocker block(v.dragDimension_);v.dragDimension_->setText(QLocale().toString(value,'g',17));}
            BRepTiming::reset();BRepTiming::enabled=BRepTiming::detailed=true;const bool ready=v.updateCommandParameter(false);
            BRepTiming::enabled=BRepTiming::detailed=false;return ready;
        };
        auto start=[&](bool enabled) {
            v.cancelCommandSession();qputenv("OPENDESIGNER_ENABLE_PROVISIONAL_BOX_PREVIEW",enabled?"1":"0");
            const auto top=cap(v.boxes_[0].shape,true);selectFace(v,0,top);
            require(v.startMoveFace(0,top),"placement session start failed");v.stopMoveNumericPreview();
        };
        auto provisional=[&] {
            const auto& s=*v.commandSession_;
            return s.movePreviewPhase==Phase::Provisional&&!s.provisionalCandidate().IsNull()&&!s.valid&&s.candidate.IsNull()
                &&!s.changes.front().faceChange&&!s.movePreviewCertificate
                &&BRepTiming::stats["MoveFaceGeometry.MoveFaceGeometry::move"].calls==0;
        };
        auto certified=[&] {
            const auto& s=*v.commandSession_;
            return s.valid&&s.provisionalCandidate().IsNull()&&s.movePreviewPhase!=Phase::Provisional
                &&BRepTiming::stats["MoveFaceGeometry.MoveFaceGeometry::move"].calls==1;
        };
        const auto stamp=v.movePartSnapshot(v.boxes_[0]);const double height=reopened.max[2]-reopened.min[2];
        auto expected=[&](double d){return BRepPrimAPI_MakeBox(gp_Pnt(reopened.min[0],reopened.min[1],reopened.min[2]),
            gp_Pnt(reopened.max[0],reopened.max[1],reopened.max[2]+d)).Shape();};
        // Reopened Box: provisional, raw geometry outside document/history, Save snapshot owned.
        start(true);const auto undo=v.undoStack_.size(),redo=v.redoStack_.size();
        require(evaluate(15)&&provisional(),"reopened identity Box did not select provisional preview");
        require(BRepCertification::equivalent(v.commandSession_->provisionalCandidate(),expected(15)),"reopened provisional differs from Box oracle");
        require(v.movePartSnapshot(v.boxes_[0])==stamp&&v.undoStack_.size()==undo&&v.redoStack_.size()==redo,"preview mutated source/history");
        {   NativeDocumentSnapshot snapshot,again;QString e;
            require(v.exportDocumentSnapshot(snapshot)&&snapshot.parts[0].shape.IsEqual(v.boxes_[0].shape)
                &&NativeDocumentStorage::save(directory.filePath("during.odc"),snapshot,e)&&NativeDocumentStorage::load(directory.filePath("during.odc"),again,e)
                &&BRepCertification::equivalent(again.parts[0].shape,reopened.shape)&&v.movePartSnapshot(v.boxes_[0])==stamp
                &&provisional(),"Save during provisional serialized raw geometry or mutated source");}
        // Through 0 mm and back (numeric), then OK with complete certification.
        require(evaluate(0)&&!v.commandSession_->moveNumericContextInvalidated,"0 mm evaluation invalidated reopened session");
        require(evaluate(-5)&&provisional(),"reopened provisional after 0 mm failed");
        BRepTiming::reset();BRepTiming::enabled=BRepTiming::detailed=true;v.commandButtons_->button(QDialogButtonBox::Ok)->click();
        unsigned material=0;for(const auto& e:BRepTiming::details)material+=e.kind=="Boolean.MaterialCommon";
        const auto fullCalls=BRepTiming::stats["MoveFaceGeometry.MoveFaceGeometry::move"].calls;BRepTiming::enabled=BRepTiming::detailed=false;
        require(!v.commandSession_&&v.undoStack_.size()==undo+1&&fullCalls==1&&material>0,"OK did not run complete certification exactly once");
        require(v.boxes_[0].persistentId==reopened.persistentId&&v.boxes_[0].presentation==reopened.presentation
            &&BRepCertification::equivalent(v.boxes_[0].shape,expected(-5)),"OK result/identity wrong");
        const auto accepted=v.boxes_[0].shape;
        v.undoPull();require(v.boxes_[0].shape.IsEqual(reopened.shape)&&v.movePartSnapshot(v.boxes_[0])==stamp,"Undo");
        v.redoPull();require(v.boxes_[0].shape.IsEqual(accepted),"Redo");v.undoPull();
        // Cancel and Escape commit nothing.
        start(true);require(evaluate(15)&&provisional(),"cancel setup");v.commandButtons_->button(QDialogButtonBox::Cancel)->click();
        require(!v.commandSession_&&v.undoStack_.size()==undo&&v.movePartSnapshot(v.boxes_[0])==stamp,"Cancel changed source/history");
        start(true);require(evaluate(15)&&provisional(),"escape setup");key(v,Qt::Key_Escape);
        require(!v.commandSession_&&v.undoStack_.size()==undo&&v.movePartSnapshot(v.boxes_[0])==stamp,"Escape changed source/history");
        // P2 receipt reuse (flag OFF) unchanged on the reopened Part.
        start(false);require(evaluate(15)&&certified()&&v.commandSession_->movePreviewCertificate,"flag OFF certified preview failed");
        BRepTiming::reset();BRepTiming::enabled=true;v.commandButtons_->button(QDialogButtonBox::Ok)->click();BRepTiming::enabled=false;
        require(!v.commandSession_&&BRepTiming::stats["MoveFace.certifiedPreviewReused"].calls==1
            &&BRepTiming::stats["MoveFaceGeometry.MoveFaceGeometry::move"].calls==0,"P2 receipt reuse changed");v.undoPull();
        require(v.movePartSnapshot(v.boxes_[0])==stamp,"P2 undo did not restore source");
        // Changed selected Face: refusal.
        start(true);require(evaluate(15)&&provisional(),"changed-face setup");
        v.context_->ClearSelected(Standard_False);
        Handle(StdSelect_BRepOwner) other=new StdSelect_BRepOwner(cap(v.boxes_[0].shape,false),v.boxes_[0].presentation,5,Standard_True);
        v.context_->AddOrRemoveSelected(other,Standard_False);
        {const QSignalBlocker block(&v);v.confirmCommandSession();}
        require(v.commandSession_&&!v.commandSession_->valid&&v.commandSession_->provisionalCandidate().IsNull()
            &&v.undoStack_.size()==undo&&v.movePartSnapshot(v.boxes_[0])==stamp,"changed Face committed stale provisional");
        // Stale Part revision: refusal.
        start(true);require(evaluate(15)&&provisional(),"stale setup");
        auto edited=v.boxes_[0];edited.shape=BRepBuilderAPI_Copy(reopened.shape,true,false).Shape();v.applyFaceState(0,edited);
        require(v.commandSession_->provisionalCandidate().IsNull(),"source edit retained provisional");
        {const QSignalBlocker block(&v);v.confirmCommandSession();}
        require(v.undoStack_.size()==undo&&v.boxes_[0].shape.IsEqual(edited.shape),"stale revision committed");
        v.cancelCommandSession();

        // 3. Synthetic placements on the reopened Part (public OCCT constructors only).
        auto withPlacement=[&](const TopoDS_Shape& shape,const gp_Trsf& placement) {
            v.cancelCommandSession();auto state=reopened;state.shape=shape;state.orientation=placement;
            Bnd_Box bounds;BRepBndLib::AddOptimal(shape,bounds,false,false);
            bounds.Get(state.min[0],state.min[1],state.min[2],state.max[0],state.max[1],state.max[2]);state.baseMin=state.min;state.baseMax=state.max;
            v.applyFaceState(0,state);start(true);return evaluate(15);
        };
        require(withPlacement(reopened.shape,compoundIdentity)&&provisional(),"SetValues identity placement not provisional");
        require(withPlacement(reopened.shape,gp_Trsf())&&provisional(),"gp_Identity placement not provisional");
        for(const auto& [name,t]:refused) {
            if(t.Form()==gp_Identity)continue; // SetForm misuse is covered at matrix level only.
            withPlacement(reopened.shape,t);
            std::cout<<"PLACEMENT_SESSION refused="<<name<<" certified_valid="<<v.commandSession_->valid<<'\n';
            require(v.commandSession_->provisionalCandidate().IsNull()&&v.commandSession_->movePreviewPhase!=Phase::Provisional
                &&BRepTiming::stats["MoveFaceGeometry.MoveFaceGeometry::move"].calls==1,name);
        }
        // F2.9G.3: a cylinder cap is now provable by the clear-sweep capability;
        // an elliptical (extrusion-carried) wall remains unsupported.
        require(withPlacement(EllipticalFixture::prism(),compoundIdentity)&&certified(),"unsupported geometry did not fall back");
        v.cancelCommandSession();qunsetenv("OPENDESIGNER_ENABLE_PROVISIONAL_BOX_PREVIEW");qunsetenv("OPENDESIGNER_BOX_SHADOW");
        std::cout<<"Provisional identity placement: contract, fresh/reopened Box, Save, 0 mm, OK, Cancel/Escape, Undo/Redo, P2, stale bindings and fallbacks passed\n";
    }
    static void provisionalBox() {
        namespace P=BoxProvisionalExperiment;
        qunsetenv("OPENDESIGNER_BOX_SHADOW_TEST_FAULT");qputenv("OPENDESIGNER_BOX_SHADOW","0");
        MainWindow window;window.show();QApplication::processEvents();
        auto& v=*window.findChild<CadViewport*>();v.addBox();auto original=v.boxes_[0];
        original.shape=BRepPrimAPI_MakeBox(40,30,20).Shape();original.geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;
        original.blends={};original.orientation=gp_Trsf();original.cylinderPrimitive=original.spherePrimitive=original.conePrimitive=false;
        original.min={0,0,0};original.max={40,30,20};original.baseMin=original.min;original.baseMax=original.max;v.applyFaceState(0,original);
        const auto top=cap(original.shape,true);const auto originalStamp=v.movePartSnapshot(original);
        // F2.9C.1 diagnostic changes only a disposable topology copy.
        {
            auto probe=original;probe.shape=BRepBuilderAPI_Copy(original.shape,true,false).Shape();
            const auto before=v.movePartSnapshot(probe);const auto ascii=brep(probe.shape);const auto identity=probe.shape;
            auto flags=[](const TopoDS_Shape& shape){return QString("%1%2%3%4%5%6%7")
                .arg(shape.Free()).arg(shape.Modified()).arg(shape.Checked()).arg(shape.Orientable())
                .arg(shape.Closed()).arg(shape.Infinite()).arg(shape.Convex());};
            TopTools_IndexedMapOfShape shapes;TopExp::MapShapes(probe.shape,shapes);
            std::vector<QString> beforeFlags;for(int i=1;i<=shapes.Extent();++i)beforeFlags.push_back(flags(shapes(i)));
            TopoDS_Compound assembly;BRep_Builder builder;builder.MakeCompound(assembly);
            builder.Add(assembly,probe.shape.Moved(TopLoc_Location(probe.orientation)));
            std::cout<<"SNAPSHOT_BINDING add_binary_equal="<<(v.movePartSnapshot(probe)==before)<<std::endl;
            require(!probe.shape.Free()&&v.movePartSnapshot(probe)!=before,"compound attachment mechanism changed");
            probe.shape.Free(true); // disposable diagnostic graph only
            require(v.movePartSnapshot(probe)==before&&brep(probe.shape)==ascii,"Free flag explanation changed");
            NativeDocumentSnapshot snapshot;require(v.exportDocumentSnapshot(snapshot),"diagnostic export failed");
            snapshot.parts[0].shape=probe.shape;QString error;QTemporaryDir temp;
            require(temp.isValid()&&NativeDocumentStorage::save(temp.filePath("binding.odc"),snapshot,error),"diagnostic Save failed");
            require(probe.shape.IsEqual(identity)&&v.movePartSnapshot(probe)==before&&brep(probe.shape)==ascii,
                "production Save mutated caller identity/signature");
            for(int i=1;i<=shapes.Extent();++i)require(flags(shapes(i))==beforeFlags[size_t(i-1)],"production Save changed topology flags");
        }
        struct Pending : P::State {
            std::shared_ptr<CadViewport::CadPart> before;
            QByteArray stamp;
            std::vector<size_t> selection;
            std::vector<Handle(SelectMgr_EntityOwner)> owners;
            size_t undo=0,redo=0;
            bool keepBlends=true;
            P::SessionAdapter adapter;
            quint64 generation=0;
            double publishMs=0;
        };
        auto owners=[&] {std::vector<Handle(SelectMgr_EntityOwner)> result;
            for(v.context_->InitSelected();v.context_->MoreSelected();v.context_->NextSelected())result.push_back(v.context_->SelectedOwner());return result;};
        auto cleanup=[&](Pending* p) {
            try {
                p->adapter.cancel();
                if(v.commandSession_&&v.commandSession_->faceBefore==p->before)v.cancelCommandSession();
                if(!p->overlay.IsNull()&&v.context_->DisplayStatus(p->overlay)!=PrsMgr_DisplayStatus_None)v.context_->Remove(p->overlay,Standard_False);
            } catch(...) {}
            delete p;
        };
        using Owned=std::unique_ptr<Pending,decltype(cleanup)>;
        auto text=[&](const QString& value){const QSignalBlocker block(v.dragDimension_);v.dragDimension_->setText(value);};
        auto originalUnchanged=[&](const Pending& p) {
            if(v.movePartSnapshot(v.boxes_[0])!=originalStamp)std::cerr<<"PROVISIONAL_SOURCE_DIAGNOSTIC sameShape="<<v.boxes_[0].shape.IsEqual(original.shape)
                <<" geometryEqual="<<(brep(v.boxes_[0].shape)==brep(original.shape))<<" id="<<(v.boxes_[0].persistentId==original.persistentId)
                <<" maxZ="<<v.boxes_[0].max[2]<<" kind="<<int(v.boxes_[0].geometryKind)<<" sourceForm="<<int(v.boxes_[0].orientation.Form())
                <<" displayForm="<<int(v.boxes_[0].presentation->LocalTransformation().Form())<<'\n';
            require(v.movePartSnapshot(v.boxes_[0])==originalStamp,"provisional experiment changed source Part");
            require(v.undoStack_.size()==p.undo&&v.redoStack_.size()==p.redo,"uncommitted preview changed history");
            NativeDocumentSnapshot snapshot;require(v.exportDocumentSnapshot(snapshot)&&snapshot.parts.size()==1
                &&snapshot.parts[0].shape.IsSame(original.shape)&&snapshot.parts[0].id==original.persistentId,"provisional entered Save snapshot");
        };
        unsigned preparedCases=0;
        auto prepare=[&](QString input,bool constructionFailure=false) {
            std::cout<<"PROVISIONAL_CASE prepare="<<++preparedCases<<" input="<<input.toStdString()<<" construction_fault="<<constructionFailure<<std::endl;
            v.cancelCommandSession();v.applyFaceState(0,original);selectFace(v,0,top);require(v.startMoveFace(0,top),"provisional source setup");
            // The experiment delivers this stable request directly, as if its
            // timer already fired. Pending-input tests explicitly queue later.
            v.stopMoveNumericPreview();
            const auto started=P::Clock::now();text(input);Owned p(new Pending,cleanup);
            p->generation=preparedCases;
            require(p->adapter.phase()==P::SessionAdapter::Phase::Idle,"adapter did not start Idle");
            p->before=v.commandSession_->faceBefore;p->source=original.shape;p->selected=v.commandSession_->face;
            p->partId=original.persistentId;p->stamp=v.movePartSnapshot(v.boxes_[0]);p->revision=v.moveNumericRevision_;
            p->selection=v.selectedPartIndices_;p->undo=v.undoStack_.size();p->redo=v.redoStack_.size();p->keepBlends=v.keepBlends_->isChecked();
            // Displayability is never installed as production certification.
            v.commandSession_->valid=false;v.commandSession_->candidate.Nullify();v.commandSession_->movePreviewCertificate.reset();
            v.commandSession_->changes.front().faceChange.reset();
            bool ok=false;p->displacement=QLocale().toDouble(input,&ok);
            const double bindingMs=P::elapsed(started);
            BRepTiming::reset();BRepTiming::enabled=BRepTiming::detailed=true;
            if(ok&&v.dragDimension_->hasAcceptableInput())P::construct(*p,constructionFailure);
            else p->error="invalid numeric input";
            p->preparationMs+=bindingMs;
            if(p->displayable) {
                const auto render=P::Clock::now();QString error;
                p->overlay=v.createFacePreview(*p->before,p->candidate,error);require(!p->overlay.IsNull(),"provisional rendering failed");
                for(TopExp_Explorer f(p->candidate,TopAbs_FACE);f.More();f.Next()) {
                    TopLoc_Location location;auto mesh=BRep_Tool::Triangulation(TopoDS::Face(f.Current()),location);
                    require(!mesh.IsNull()&&mesh->NbNodes()>0&&mesh->NbTriangles()>0,"missing provisional mesh");
                    for(int i=1;i<=mesh->NbNodes();++i){auto point=mesh->Node(i).Transformed(location.Transformation());
                        require(std::isfinite(point.X())&&std::isfinite(point.Y())&&std::isfinite(point.Z()),"nonfinite mesh node");}
                    for(int i=1;i<=mesh->NbTriangles();++i){int a,b,c;mesh->Triangle(i).Get(a,b,c);
                        require(a>0&&b>0&&c>0&&a<=mesh->NbNodes()&&b<=mesh->NbNodes()&&c<=mesh->NbNodes(),"invalid mesh index");}
                }
                // Only AIS ownership is borrowed by the current session. Raw B-Rep
                // stays in test state; no FaceChange/candidate/P2 is populated.
                v.commandSession_->preview=p->overlay;v.context_->Erase(original.presentation,Standard_False);
                v.commandButtons_->button(QDialogButtonBox::Ok)->setEnabled(true);v.redrawFacePreview();p->renderMs=P::elapsed(render);
            }
            BRepTiming::enabled=BRepTiming::detailed=false;
            for(const auto& e:BRepTiming::details)require(e.kind!="Boolean.MaterialCommon","material certification ran during provisional preview");
            p->owners=owners();
            const auto publishStart=P::Clock::now();
            if(p->displayable)require(p->adapter.publish(*p,p->generation),"adapter failed provisional publication");
            else p->adapter.fail();
            p->publishMs=P::elapsed(publishStart);p->totalMs=P::elapsed(started);
            require(p->adapter.phase()==(p->displayable?P::SessionAdapter::Phase::Provisional:P::SessionAdapter::Phase::Failed),"preview phase mismatch");
            require(!v.commandSession_->valid&&v.commandSession_->candidate.IsNull()&&!v.commandSession_->movePreviewCertificate
                &&!v.commandSession_->changes.front().faceChange,"provisional became committable");
            originalUnchanged(*p);return p;
        };
        std::vector<double> identityTimes;
        auto keyMatchesRaw=[&](const Pending& p) {
            if(!v.commandSession_||v.commandSession_->faceBefore!=p.before||v.commandSession_->moveSourceInvalidated
               ||v.moveNumericRevision_!=p.revision||!v.commandSession_->face.IsEqual(p.selected)
               ||v.commandSession_->directionSource!=CadViewport::MoveDirectionSource::FaceNormal||v.commandSession_->directionReverse
               ||!v.commandSession_->allowEllipticalCapMove||v.keepBlends_->isChecked()!=p.keepBlends
               ||v.boxes_[0].persistentId!=p.partId||!v.boxes_[0].shape.IsEqual(p.source)
               ||v.movePartSnapshot(v.boxes_[0])!=p.stamp||v.selectedPartIndices_!=p.selection||owners()!=p.owners)return false;
            bool ok=false;const double d=QLocale().toDouble(v.dragDimension_->text(),&ok);
            return ok&&v.dragDimension_->hasAcceptableInput()&&std::isfinite(d)&&d==p.displacement;
        };
        auto keyMatches=[&](const Pending& p) {
            const auto begin=P::Clock::now();const bool matches=keyMatchesRaw(p);
            identityTimes.push_back(P::elapsed(begin));return matches;
        };
        auto reject=[&](Pending& p) {
            p.adapter.fail();
            if(v.commandSession_&&v.commandSession_->faceBefore==p.before) {
                auto& session=*v.commandSession_;session.valid=false;session.state=CadViewport::CommandSession::State::Parameters;
                session.movePreviewCertificate.reset();session.candidate.Nullify();session.changes.front().faceChange.reset();
                QString error;v.presentFacePreview(*session.faceBefore,false,error);v.redrawFacePreview();
            }
            p.candidate.Nullify();p.displayable=false;
        };
        auto confirm=[&](Pending& p,bool injectFailure=false,bool cancelCertified=false,bool loseReceipt=false) {
            using Adapter=P::SessionAdapter;
            if(p.adapter.phase()!=Adapter::Phase::Provisional||p.adapter.committed())return false;
            const double frozen=p.displacement;const auto beforePart=p.before;const auto stamp=p.stamp;
            const bool certifiedOK=p.adapter.certify(p,p.generation,v.pendingMoveNumeric_.has_value()||v.moveNumericTimer_->isActive(),
                [&]{return keyMatches(p);},[&]() -> std::optional<Adapter::CertifiedResult> {
            require(p.adapter.phase()==Adapter::Phase::Certifying&&!p.adapter.cancel(),"in-flight certification falsely cancellable");
            BRepTiming::reset();BRepTiming::enabled=BRepTiming::detailed=true;
            // This is simulated OK: the unchanged FULL production parameter path
            // runs now, not when the raw overlay was drawn.
            // The adapter already parsed/froze input and refuses pending input.
            // Enter the unchanged full production evaluator directly; calling
            // previewCommand would also advance the numeric timer token.
            const bool ready=v.previewMoveFace(frozen);
            p.fullOnOkMs=BRepTiming::stats["MoveFaceGeometry.MoveFaceGeometry::move"].inclusive;
            const auto evaluations=BRepTiming::stats["MoveFaceGeometry.MoveFaceGeometry::move"].calls;
            unsigned materialChecks=0;for(const auto& event:BRepTiming::details)materialChecks+=event.kind=="Boolean.MaterialCommon";
            BRepTiming::enabled=BRepTiming::detailed=false;
            require(evaluations==1,"confirmation skipped or duplicated full Move evaluation");
            if(!ready||injectFailure)return std::nullopt;
            require(materialChecks>0&&v.commandSession_->movePreviewCertificate.has_value(),"full material certification/P2 missing on OK");
            require(v.commandSession_->faceBefore==beforePart&&v.commandSession_->value==frozen&&v.commandSession_->face.IsEqual(p.selected)
                &&v.movePartSnapshot(v.boxes_[0])==stamp&&v.moveSourceUnchanged()
                &&v.selectedPartIndices_==p.selection&&owners()==p.owners,"confirmation binding changed");
            const auto certified=v.commandSession_->candidate;
            require(!certified.IsEqual(p.candidate),"provisional graph was committed without full regeneration");
            // Confirmation-only independent oracle checks. MakeBox is test data,
            // never the candidate constructor.
            const auto expected=BRepPrimAPI_MakeBox(40,30,20+frozen).Shape();
            require(BRepCertification::equivalent(certified,expected)&&BRepCertification::equivalent(certified,p.candidate),"full/raw/expected material disagreement");
            originalUnchanged(p);
            return Adapter::CertifiedResult{certified,v.commandSession_->changes.front().faceChange};
            });
            if(!certifiedOK){reject(p);return false;}
            require(p.adapter.phase()==Adapter::Phase::Certified,"full evaluation did not enter Certified");
            require(!p.adapter.publish(p,p.generation),"Certified candidate replaced without invalidation");
            bool repeated=false;
            require(!p.adapter.certify(p,p.generation,false,[&]{repeated=true;return true;},[&]() -> std::optional<Adapter::CertifiedResult>{
                repeated=true;return std::nullopt;})&&!repeated,"Certified state repeated full evaluation");
            if(cancelCertified){require(p.adapter.cancel(),"Certified cancellation failed");reject(p);return false;}
            if(loseReceipt)v.commandSession_->movePreviewCertificate.reset(); // adversarial bridge test
            const bool committed=p.adapter.commit(p,p.generation,[&](const Adapter::CertifiedResult& result) {
                return keyMatches(p)&&v.commandSession_->movePreviewCertificate.has_value()&&v.commandSession_->valid
                    &&v.commandSession_->candidate.IsEqual(result.shape)
                    &&v.commandSession_->movePreviewCertificate->candidate.IsEqual(result.shape)
                    &&v.commandSession_->changes.front().faceChange==result.transaction;
            },[&](const Adapter::CertifiedResult& result) {
            const auto certified=result.shape;
            BRepTiming::reset();BRepTiming::enabled=true;v.confirmCommandSession();BRepTiming::enabled=false;
            require(BRepTiming::stats["MoveFace.certifiedPreviewReused"].calls==1
                &&BRepTiming::stats["MoveFaceGeometry.MoveFaceGeometry::move"].calls==0,"real certified receipt not reused");
            require(!v.commandSession_&&v.undoStack_.size()==p.undo+1&&v.redoStack_.empty()
                &&v.boxes_[0].shape.IsSame(certified),"certified transaction/history mismatch");
            require(v.boxes_[0].persistentId==original.persistentId&&v.boxes_[0].presentation==original.presentation
                &&v.boxes_[0].orientation.Form()==original.orientation.Form(),"Part identity/placement lost");
            return true;
            });
            if(!committed){reject(p);return false;}
            require(p.adapter.committed(),"commit outcome not terminal");
            p.displayable=false;return true;
        };
        // Explicit state guards, independent of the existing production booleans.
        {
            P::SessionAdapter idle;bool called=false;P::State empty;
            require(!idle.commit(empty,0,[&](const auto&){called=true;return true;},[&](const auto&){called=true;return true;})
                &&!idle.certify(empty,0,false,[&]{called=true;return true;},[&]() -> std::optional<P::SessionAdapter::CertifiedResult>{called=true;return std::nullopt;})
                &&!called&&idle.phase()==P::SessionAdapter::Phase::Idle,"Idle operation invoked a commit/certification callback");
            require(!idle.publish(empty,0)&&idle.phase()==P::SessionAdapter::Phase::Failed,"missing candidate did not fail");
            require(idle.cancel()&&!idle.publish(empty,0)&&!idle.committed(),"Cancelled state was revived");
        }
        {auto p=prepare("15");bool called=false;
            require(!p->adapter.commit(*p,p->generation,[&](const auto&){called=true;return true;},[&](const auto&){called=true;return true;})
                &&!called&&p->adapter.phase()==P::SessionAdapter::Phase::Provisional,"raw candidate could commit");originalUnchanged(*p);}
        {auto p=prepare("15");p->adapter.fail();
            require(p->adapter.phase()==P::SessionAdapter::Phase::Failed&&p->adapter.publish(*p,p->generation),"Failed state could not recover with a fresh publication");
            const auto previous=p->candidate;p->candidate=BRepBuilderAPI_Copy(previous,true,true).Shape();
            ++p->generation;require(p->adapter.publish(*p,p->generation)&&!p->candidate.IsEqual(previous),"repeated publication failed");
            require(confirm(*p),"latest candidate publication not confirmable");v.undoPull();}
        {auto p=prepare("15");++p->generation;require(!confirm(*p),"stale session generation accepted");originalUnchanged(*p);}
        {auto p=prepare("15");p->candidate=p->source;require(!confirm(*p),"changed provisional candidate identity accepted");originalUnchanged(*p);}
        {auto p=prepare("15");v.dragDimension_->selectAll();key(v,Qt::Key_Minus,"-");key(v,Qt::Key_5,"5");
            require(v.pendingMoveNumeric_.has_value()&&v.moveNumericTimer_->isActive(),"pending debounce not exercised");
            require(!confirm(*p)&&p->adapter.phase()==P::SessionAdapter::Phase::Failed,"OK accepted obsolete preview while input pending");
            originalUnchanged(*p);v.cancelCommandSession();
            require(!v.pendingMoveNumeric_&&!v.moveNumericTimer_->isActive(),"Cancel retained delayed input");}
        {auto p=prepare("15");require(!confirm(*p,false,true)&&p->adapter.phase()==P::SessionAdapter::Phase::Cancelled,
            "Certified cancellation failed");originalUnchanged(*p);}
        {auto p=prepare("15");require(!confirm(*p,false,false,true)&&p->adapter.phase()==P::SessionAdapter::Phase::Failed,
            "lost P2 receipt allowed Certified-to-commit");originalUnchanged(*p);}
        {auto p=prepare("15");try {
            p->adapter.certify(*p,p->generation,false,[&]{return keyMatches(*p);},[]() -> std::optional<P::SessionAdapter::CertifiedResult>{
                throw std::runtime_error("injected bridge exception");});
            require(false,"bridge exception disappeared");
            } catch(const std::runtime_error&) {require(p->adapter.phase()==P::SessionAdapter::Phase::Failed,"exception retained usable adapter state");}
            originalUnchanged(*p);}
        for(double delta:{15.,-5.})for(int trial=0;trial<3;++trial) {
            auto p=prepare(QLocale().toString(delta));require(p->displayable,"provisional positive failed");
            require(confirm(*p),"certified OK failed");const auto accepted=v.boxes_[0].shape;
            require(p->certification==P::Certification::Uncertified,"raw state falsely certified");
            std::cout<<"PROVISIONAL delta="<<delta<<" trial="<<trial<<" prepare_ms="<<p->preparationMs<<" raw_ms="<<p->rawMs
                <<" checks_ms="<<p->checksMs<<" render_ms="<<p->renderMs<<" total_ms="<<p->totalMs<<" full_on_OK_ms="<<p->fullOnOkMs
                <<" raw_faces="<<ExperimentalBoxShadow::map(p->candidate,TopAbs_FACE).Extent()<<" publish_ms="<<p->publishMs<<'\n';
            require(!confirm(*p)&&p->adapter.committed()&&v.undoStack_.size()==p->undo+1,"repeated OK was not inert");
            v.undoPull();require(v.boxes_[0].shape.IsSame(original.shape),"provisional Undo did not restore exact source");
            v.redoPull();require(v.boxes_[0].shape.IsSame(accepted),"provisional Redo did not restore certified shape");v.undoPull();
        }
        {auto p=prepare("15");require(p->adapter.cancel()&&!p->adapter.cancel(),"Cancel transition incorrect");v.cancelCommandSession();originalUnchanged(*p);require(!v.context_->IsDisplayed(p->overlay),"Cancel retained raw overlay");}
        {auto p=prepare("15");require(p->adapter.cancel(),"Escape adapter cancellation failed");key(v,Qt::Key_Escape);require(!v.commandSession_,"Escape failed");originalUnchanged(*p);}
        {auto p=prepare("15");auto overlay=p->overlay;const auto u=p->undo,r=p->redo;p.reset();
            require(!v.commandSession_&&v.context_->DisplayStatus(overlay)==PrsMgr_DisplayStatus_None
                &&v.boxes_[0].shape.IsSame(original.shape)&&v.undoStack_.size()==u&&v.redoStack_.size()==r,"destruction leaked or committed preview");}
        for(const auto& input:QStringList{"-","7","-25"}){auto p=prepare(input);require(!p->displayable&&!confirm(*p),"invalid/unsupported provisional input accepted");originalUnchanged(*p);}
        {auto p=prepare("15",true);require(!p->displayable&&!p->error.isEmpty()&&!confirm(*p),"construction failure became committable");originalUnchanged(*p);}
        {auto p=prepare("15");text("-5");require(!confirm(*p),"stale displacement accepted");originalUnchanged(*p);}
        {auto p=prepare("15");v.commandSession_->face=cap(original.shape,false);require(!confirm(*p),"changed Face accepted");originalUnchanged(*p);}
        {auto p=prepare("15");v.boxes_[0].persistentId+="-changed";require(!confirm(*p),"changed Part identity accepted");
            v.boxes_[0].persistentId=original.persistentId;originalUnchanged(*p);}
        {auto p=prepare("15");gp_Trsf translation;translation.SetTranslation(gp_Vec(1,0,0));
            v.boxes_[0].shape.Location(TopLoc_Location(translation));require(!confirm(*p),"changed source location accepted");
            v.boxes_[0].shape=original.shape;originalUnchanged(*p);}
        {auto p=prepare("15");v.boxes_[0].shape.Reverse();require(!confirm(*p),"changed source orientation accepted");
            v.boxes_[0].shape=original.shape;originalUnchanged(*p);}
        {auto p=prepare("15");gp_Trsf rotation;rotation.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(0,1,0)),.5);
            v.boxes_[0].presentation->SetLocalTransformation(rotation);require(!confirm(*p),"changed presentation placement accepted");
            v.boxes_[0].presentation->SetLocalTransformation(original.orientation);originalUnchanged(*p);}
        {auto p=prepare("15");++v.moveNumericRevision_;require(!confirm(*p),"stale generation accepted");originalUnchanged(*p);}
        {auto p=prepare("15");v.applyFaceState(0,original);require(!confirm(*p),"same-geometry revision accepted");originalUnchanged(*p);}
        {auto p=prepare("15");auto edited=original;edited.shape=BRepPrimAPI_MakeBox(40,30,25).Shape();edited.max[2]=edited.baseMax[2]=25;
            v.applyFaceState(0,edited);const auto current=v.movePartSnapshot(v.boxes_[0]);require(!confirm(*p),"changed source accepted");p.reset();
            require(v.movePartSnapshot(v.boxes_[0])==current,"reject/Cancel rolled back external source edit");}
        {auto stale=prepare("15");auto current=prepare("-5");require(!confirm(*stale),"old command state accepted");stale.reset();
            require(v.commandSession_&&current->displayable,"old state destroyed newer session");originalUnchanged(*current);}
        {auto p=prepare("15");require(!confirm(*p,true),"injected confirmation failure committed");originalUnchanged(*p);
            require(!v.commandSession_->movePreviewCertificate&&!v.commandSession_->changes.front().faceChange,"failed OK retained committable state");}
        {auto p=prepare("15");text("-25");require(!confirm(*p),"unsupported input kept old provisional");
            BRepTiming::reset();BRepTiming::enabled=true;const bool accepted=v.updateCommandParameter(false);BRepTiming::enabled=false;
            require(!accepted&&BRepTiming::stats["MoveFaceGeometry.MoveFaceGeometry::move"].calls==1,"full production failed-input path not exercised");originalUnchanged(*p);}
        auto ownedSnapshot=[](const NativeDocumentSnapshot& shallow) {
            auto owned=shallow;
            for(auto& part:owned.parts)part.shape=BRepBuilderAPI_Copy(part.shape,true,false).Shape();
            return owned;
        };
        {auto p=prepare("15");NativeDocumentSnapshot snapshot,loaded;QString error;QTemporaryDir temp;
            require(temp.isValid()&&v.exportDocumentSnapshot(snapshot),"synthetic snapshot export failed");
            require(snapshot.parts.size()==1&&snapshot.parts[0].shape.IsSame(original.shape),"snapshot contains provisional geometry");
            // Retain the F2.9C.1 ownership-cost comparison, but submit the
            // shallow snapshot below to exercise production storage isolation.
            std::vector<double> copyTimes;
            const auto liveSignature=v.movePartSnapshot(v.boxes_[0]);
            for(int trial=0;trial<3;++trial) {
                const auto begin=P::Clock::now();auto owned=ownedSnapshot(snapshot);copyTimes.push_back(P::elapsed(begin));
                require(!owned.parts[0].shape.IsPartner(snapshot.parts[0].shape)
                    &&v.movePartSnapshot(v.boxes_[0])==liveSignature&&keyMatches(*p),"owned snapshot changed source binding");
            }
            std::sort(copyTimes.begin(),copyTimes.end());
            std::cout<<"SNAPSHOT_COPY median_ms="<<copyTimes[1]<<" min_ms="<<copyTimes.front()<<" max_ms="<<copyTimes.back()<<std::endl;
            // F2.9C.2: pass the real shallow export; storage owns isolation now.
            require(NativeDocumentStorage::save(temp.filePath("provisional-source.odc"),snapshot,error)
                &&NativeDocumentStorage::load(temp.filePath("provisional-source.odc"),loaded,error),"synthetic preview Save/Open failed");
            require(loaded.parts.size()==1&&loaded.parts[0].id==original.persistentId
                &&BRepCertification::equivalent(loaded.parts[0].shape,original.shape),"Save included raw provisional result");originalUnchanged(*p);
            require(keyMatches(*p)&&!v.commandSession_->movePreviewCertificate,"production Save invalidated or certified provisional state");
            require(confirm(*p),"full confirmation after owned Save failed");v.undoPull();
            require(v.boxes_[0].shape.IsEqual(original.shape),"Save/confirm/Undo lost original identity");}
        std::sort(identityTimes.begin(),identityTimes.end());
        std::cout<<"SESSION_IDENTITY calls="<<identityTimes.size()<<" median_ms="<<identityTimes[identityTimes.size()/2]
            <<" max_ms="<<identityTimes.back()<<std::endl;
        require(v.movePartSnapshot(v.boxes_[0])==originalStamp,"final source modified");
        qunsetenv("OPENDESIGNER_BOX_SHADOW");
        std::cout<<"Provisional Box: real confirmation, P2, history, persistence, cancellation, exceptions and stale bindings passed\n";
    }
    static void previewReuse() {
        const auto measured=[&](auto action) {
            BRepTiming::reset();BRepTiming::enabled=true;action();BRepTiming::enabled=false;
        };
        const auto calls=[](const char* name){return BRepTiming::stats[name].calls;};
        for(int kind=0;kind<4;++kind) {
            MainWindow window;window.show();QApplication::processEvents();
            auto& v=*window.findChild<CadViewport*>();v.addBox();auto state=v.boxes_[0];
            state.shape=kind==0?BRepPrimAPI_MakeBox(100,80,20).Shape():kind==1?EllipticalFixture::prism():kind==2?ObroundFixture::prism():SplineFixture::solid();
            state.geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;
            Bnd_Box bounds;BRepBndLib::AddOptimal(state.shape,bounds,false,false);
            bounds.Get(state.min[0],state.min[1],state.min[2],state.max[0],state.max[1],state.max[2]);state.baseMin=state.min;state.baseMax=state.max;
            state.orientation.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),.4);state.orientation.SetTranslationPart(gp_Vec(17,-9,23));
            v.applyFaceState(0,state);v.fitAll();QApplication::processEvents();
            const auto original=v.boxes_[0];const auto source=brep(original.shape);
            const auto axis=kind==2?ObroundFixture::axis():gp_Dir(0,0,1);
            const auto point=kind==2?ObroundFixture::world(20,0,20):gp_Pnt(0,0,20);TopoDS_Face top;
            for(TopExp_Explorer f(original.shape,TopAbs_FACE);f.More();f.Next()) {
                BRepAdaptor_Surface a(TopoDS::Face(f.Current()));
                if(a.GetType()==GeomAbs_Plane&&a.Plane().Axis().Direction().IsParallel(axis,1.e-8)&&a.Plane().Distance(point)<1.e-7) top=TopoDS::Face(f.Current());
            }
            require(!top.IsNull(),"reuse cap missing");QString error;
            const auto uncached=MoveFaceGeometry::move(original.shape,top,15,error,MoveFaceGeometry::ReconstructionPolicy::AxialInvariantCap);
            require(!uncached.IsNull(),"uncached oracle failed");
            selectFace(v,0,top);require(v.startMoveFace(0,top),"reuse session start failed");
            v.dragDimension_->setText("15");require(v.updateCommandParameter(false),"reuse preview failed");
            require(v.commandSession_->movePreviewCertificate.has_value(),"certified preview proof not established");
            const auto candidate=v.commandSession_->candidate;const auto undo=v.undoStack_.size();
            NativeDocumentSnapshot during;require(v.exportDocumentSnapshot(during)&&during.parts[0].shape.IsSame(original.shape),"preview leaked into persistence");
            if(kind==0) {
                const auto stamp=v.movePartSnapshot(v.boxes_[0]);const auto revision=v.moveNumericRevision_;
                const auto selected=v.commandSession_->face;const auto change=v.commandSession_->changes.front().faceChange;
                QTemporaryDir temp;NativeDocumentSnapshot snapshot,loaded;
                for(int trial=0;trial<3;++trial) {
                    require(temp.isValid()&&v.exportDocumentSnapshot(snapshot)
                        &&NativeDocumentStorage::save(temp.filePath("live-certified.odc"),snapshot,error)
                        &&NativeDocumentStorage::load(temp.filePath("live-certified.odc"),loaded,error),"live certified Save failed");
                    require(v.movePartSnapshot(v.boxes_[0])==stamp&&v.moveNumericRevision_==revision&&v.moveSourceUnchanged()
                        &&v.commandSession_->face.IsEqual(selected)&&v.commandSession_->movePreviewCertificate.has_value()
                        &&v.commandSession_->changes.front().faceChange==change&&v.undoStack_.size()==undo
                        &&BRepCertification::equivalent(loaded.parts[0].shape,original.shape),"Save changed source/selection/receipt/history");
                }
            }
            measured([&]{v.confirmCommandSession();});
            require(calls("MoveFace.certifiedPreviewReused")==1&&calls("MoveFace.CadViewport::previewMoveFace")==0
                &&calls("BRepCertification.singleSolid")==0&&calls("OCCT.materialCommon.Build")==0,"OK repeated reconstruction/certification");
            require(!v.commandSession_&&v.undoStack_.size()==undo+1&&v.boxes_[0].shape.IsSame(candidate),"reuse did not commit exact candidate once");
            const auto edited=v.boxes_[0];require(BRepCertification::equivalent(edited.shape,uncached),"reused result differs from full certified route");
            require(edited.persistentId==original.persistentId&&edited.name==original.name&&edited.color==original.color,"reuse lost identity/metadata");
            for(int r=1;r<=3;++r) for(int c=1;c<=4;++c) require(edited.orientation.Value(r,c)==original.orientation.Value(r,c),"reuse changed placement");
            v.undoPull();require(v.boxes_[0].shape.IsSame(original.shape),"reuse Undo lost source");
            v.redoPull();require(v.boxes_[0].shape.IsSame(candidate),"reuse Redo lost candidate");
            NativeDocumentSnapshot saved,loaded;QTemporaryDir temp;
            const auto savedStamp=v.movePartSnapshot(v.boxes_[0]);
            require(temp.isValid()&&v.exportDocumentSnapshot(saved)&&NativeDocumentStorage::save(temp.filePath("reuse.odc"),saved,error)
                &&NativeDocumentStorage::load(temp.filePath("reuse.odc"),loaded,error)&&loaded.parts[0].id==original.persistentId
                &&BRepCertification::equivalent(loaded.parts[0].shape,candidate),"reused native Save/Open failed");
            require(v.movePartSnapshot(v.boxes_[0])==savedStamp,"post-commit Save changed source signature");
            v.undoPull();selectFace(v,0,top);require(v.startMoveFace(0,top),"cancel setup failed");
            v.dragDimension_->setText("15");require(v.updateCommandParameter(false),"cancel preview failed");v.cancelCommandSession();
            require(!v.commandSession_&&v.boxes_[0].shape.IsSame(original.shape)&&brep(original.shape)==source,"Cancel retained candidate or changed source");
            const auto cancelledStamp=v.movePartSnapshot(v.boxes_[0]);const auto cancelUndo=v.undoStack_.size();
            require(v.exportDocumentSnapshot(saved)&&NativeDocumentStorage::save(temp.filePath("cancelled.odc"),saved,error)
                &&v.movePartSnapshot(v.boxes_[0])==cancelledStamp&&v.undoStack_.size()==cancelUndo,"Save after Cancel changed source/history");
        }
        // Mutation guards: use a fresh source in the same Part for each case.
        MainWindow window;window.show();QApplication::processEvents();auto& v=*window.findChild<CadViewport*>();v.addBox();
        const auto setup=[&] {
            v.cancelCommandSession();auto state=v.boxes_[0];state.shape=BRepPrimAPI_MakeBox(100,80,20).Shape();
            state.blends={};state.geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;
            state.min={0,0,0};state.max={100,80,20};state.baseMin=state.min;state.baseMax=state.max;state.orientation=gp_Trsf();
            v.applyFaceState(0,state);const auto top=cap(state.shape,true);selectFace(v,0,top);
            require(v.startMoveFace(0,top),"invalidation session failed");v.dragDimension_->setText("15");
            require(v.updateCommandParameter(false)&&v.commandSession_->movePreviewCertificate.has_value(),"invalidation proof missing");return top;
        };
        const auto fullConfirm=[&] {
            measured([&]{v.confirmCommandSession();});
            require(calls("MoveFace.certifiedPreviewReused")==0&&calls("MoveFace.CadViewport::previewMoveFace")==1,"changed operation reused stale candidate");
            require(!v.commandSession_,"changed operation did not use full confirmation");
        };
        setup();v.dragDimension_->setText("20");fullConfirm();near(BRepCertification::volume(v.boxes_[0].shape),100*80*40,"changed displacement not applied");
        auto top=setup();v.commandSession_->directionSource=CadViewport::MoveDirectionSource::Reference;
        v.commandSession_->directionReferences={{References::Kind::Face,v.boxes_[0].persistentId,top}};fullConfirm();
        setup();v.commandSession_->face=cap(v.boxes_[0].shape,false);fullConfirm();near(v.boxes_[0].min[2],-15,"changed Face not independently rebuilt");
        top=setup();v.context_->ClearSelected(Standard_False);
        Handle(StdSelect_BRepOwner) owner=new StdSelect_BRepOwner(cap(v.boxes_[0].shape,false),v.boxes_[0].presentation,5,Standard_True);
        v.context_->AddOrRemoveSelected(owner,Standard_False);fullConfirm();
        setup();{const QSignalBlocker block(v.keepBlends_);v.keepBlends_->setChecked(false);}fullConfirm();
        setup();v.commandSession_->allowEllipticalCapMove=false;fullConfirm();
        const auto refuseConfirm=[&] {
            const auto source=brep(v.boxes_[0].shape);const auto undo=v.undoStack_.size();
            measured([&]{const QSignalBlocker block(&v);v.confirmCommandSession();});
            require(calls("MoveFace.certifiedPreviewReused")==0&&v.commandSession_&&!v.commandSession_->valid
                &&v.commandSession_->candidate.IsNull()&&!v.commandSession_->movePreviewCertificate
                &&v.undoStack_.size()==undo&&brep(v.boxes_[0].shape)==source,"uncertified/stale operation committed");v.cancelCommandSession();
        };
        setup();auto replacement=v.boxes_[0];replacement.shape=BRepPrimAPI_MakeBox(100,80,25).Shape();v.applyFaceState(0,replacement);refuseConfirm();
        setup();auto same=v.boxes_[0];v.applyFaceState(0,same);refuseConfirm(); // revision event, identical geometry
        setup();v.boxes_[0].orientation.SetTranslation(gp_Vec(1,2,3));refuseConfirm();
        setup();auto displayTransform=v.boxes_[0].orientation;displayTransform.SetTranslation(gp_Vec(5,0,0));
        v.boxes_[0].presentation->SetLocalTransformation(displayTransform);refuseConfirm();
        setup();TopExp_Explorer vertex(v.boxes_[0].shape,TopAbs_VERTEX);BRep_Builder builder;
        builder.UpdateVertex(TopoDS::Vertex(vertex.Current()),1.e-4);refuseConfirm(); // same root handle, changed exact tolerance
        setup();v.dragDimension_->setText("-100");refuseConfirm();
        setup();v.dragDimension_->setText("invalid");refuseConfirm();
        setup();v.commandSession_->movePreviewCertificate.reset();fullConfirm(); // uncertain proof takes full path
        setup();v.undoPull();require(!v.commandSession_,"Undo retained live preview certificate");
        v.redoPull();require(!v.commandSession_,"Redo revived preview certificate");
        std::cout<<"Certified preview reuse, exact content/frame guards, fallback, invalidation, history and persistence passed\n";
    }
    static void startupValidation() {
        MainWindow window;window.show();QApplication::processEvents();
        auto& v=*window.findChild<CadViewport*>();v.addBox();
        const auto identity=v.boxes_[0].persistentId;
        const auto checkMenu=[&](const TopoDS_Shape& shape,const TopoDS_Face& face,bool valid) {
            v.cancelCommandSession();
            auto state=v.boxes_[0];state.shape=shape;state.blends={};
            state.geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;
            state.cylinderPrimitive=state.spherePrimitive=state.conePrimitive=false;
            Bnd_Box bounds;BRepBndLib::AddOptimal(shape,bounds,false,false);
            bounds.Get(state.min[0],state.min[1],state.min[2],state.max[0],state.max[1],state.max[2]);
            state.baseMin=state.min;state.baseMax=state.max;v.applyFaceState(0,state);
            selectFace(v,0,face);const auto selection=v.selectionContext();
            require(selection.faces.size()==1,"eligibility selection lost current Face");
            const auto snapshot=brep(shape);const auto undo=v.undoStack_.size();
            // Compare with the unchanged standalone policy, including its
            // primitive fallback, rather than asserting only action presence.
            const bool expectedResize=v.supportsResize(selection);
            std::unique_ptr<QMenu> menu(v.createSelectionMenu(QPoint()));
            const auto move=menu->findChild<QAction*>("context.move_face");
            const auto resize=menu->findChild<QAction*>("context.resize");
            require((move&&move->isEnabled())==valid,"Move menu eligibility changed");
            require(bool(resize&&resize->isEnabled())==expectedResize,"Pull menu eligibility changed");
            require(v.boxes_[0].persistentId==identity&&brep(shape)==snapshot&&v.undoStack_.size()==undo,"menu changed source, metadata or history");
            menu.reset();
            // No QAction/menu result becomes a source certificate. Spline
            // startup repeats refusal-only checks; preview must certify source.
            // Other operation/reference policies retain full startup validation.
            BRepTiming::reset();BRepTiming::enabled=true;
            const bool started=v.startMoveFace(0,face);
            BRepTiming::enabled=false;
            require(started==valid,"command accepted stale or invalid source");
            if(valid) {
                bool spline=false;
                for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
                    const BRepAdaptor_Surface support(TopoDS::Face(it.Current()));
                    if(support.GetType()==GeomAbs_SurfaceOfExtrusion&&support.BasisCurve()->GetType()==GeomAbs_BSplineCurve)spline=true;
                }
                require(BRepTiming::stats["OCCT.selfInterference.Perform"].calls==(spline?0u:1u),"startup validation order changed unexpectedly");
            }
            v.cancelCommandSession();
            require(brep(shape)==snapshot,"startup/cancel changed source");
        };
        const auto box=BRepPrimAPI_MakeBox(100,80,20).Shape();
        checkMenu(box,cap(box,true),true);
        const auto ellipse=EllipticalFixture::prism();checkMenu(ellipse,cap(ellipse,true),true);
        const auto obround=ObroundFixture::prism();TopoDS_Face obroundTop;
        for(TopExp_Explorer it(obround,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());BRepAdaptor_Surface s(face);
            if(s.GetType()==GeomAbs_Plane&&s.Plane().Axis().Direction().IsParallel(ObroundFixture::axis(),1.e-8)
               &&s.Plane().Distance(ObroundFixture::world(20,0,20))<1.e-7) obroundTop=face;
        }
        checkMenu(obround,obroundTop,true);
        const auto spline=SplineFixture::solid();checkMenu(spline,SplineFixture::cap(spline,20),true);
        // Same Part, edited revision: a valid Face alone is not a valid solid.
        const auto open=cap(box,true);checkMenu(open,open,false);
        const auto edited=BRepPrimAPI_MakeBox(100,80,30).Shape();checkMenu(edited,cap(edited,true),true);
        selectFace(v,0,cap(edited,true));
        std::unique_ptr<QMenu> staleMenu(v.createSelectionMenu(QPoint()));
        const auto staleAction=staleMenu->findChild<QAction*>("context.move_face");
        require(staleAction&&staleAction->isEnabled(),"stale-action setup missing Move action");
        auto changed=v.boxes_[0];changed.shape=cap(edited,true);
        v.applyFaceState(0,changed);selectFace(v,0,TopoDS::Face(changed.shape));
        staleAction->trigger();
        require(!v.commandSession_&&v.boxes_[0].shape.IsSame(changed.shape)
            &&v.boxes_[0].persistentId==identity,"old menu action accepted an edited invalid source");
        std::cout<<"Synchronous menu eligibility, independent startup validation, edited-source refusal and metadata preservation passed\n";
    }
    // Diagnostic benchmark: no time limits, cache changes or reduced certificates.
    static void startupTimings(bool detailed=false) {
        std::cout<<std::fixed<<std::setprecision(3);
        const std::vector<std::string> labels={"Box","Ellipse","Obround-hole","Spline"};
        for(size_t kind=0;kind<labels.size();++kind) {
            MainWindow window;window.show();QApplication::processEvents();
            auto& v=*window.findChild<CadViewport*>();v.addBox();
            auto state=v.boxes_[0];
            if(kind==0) state.shape=BRepPrimAPI_MakeBox(100,80,20).Shape();
            if(kind==1) state.shape=EllipticalFixture::prism(20);
            if(kind==2) state.shape=ObroundFixture::prism(20);
            if(kind==3) state.shape=SplineFixture::solid();
            if(kind!=0) state.geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;
            Bnd_Box bounds;BRepBndLib::AddOptimal(state.shape,bounds,false,false);
            bounds.Get(state.min[0],state.min[1],state.min[2],state.max[0],state.max[1],state.max[2]);
            state.baseMin=state.min;state.baseMax=state.max;v.applyFaceState(0,state);
            const auto axis=kind==2?ObroundFixture::axis():gp_Dir(0,0,1);
            const auto point=kind==0?gp_Pnt(50,40,20):kind==1?gp_Pnt(0,0,20):kind==2?ObroundFixture::world(20,0,20):gp_Pnt(30,20,20);
            v.view_->SetProj(axis.X(),axis.Y(),axis.Z());v.fitAll();QApplication::processEvents();
            TopoDS_Face top;
            for(TopExp_Explorer it(state.shape,TopAbs_FACE);it.More();it.Next()) {
                const auto face=TopoDS::Face(it.Current());BRepAdaptor_Surface surface(face);
                if(surface.GetType()==GeomAbs_Plane&&surface.Plane().Axis().Direction().IsParallel(axis,1.e-8)
                   &&surface.Plane().Distance(point)<1.e-7) top=face;
            }
            require(!top.IsNull(),"timing target missing");
            const auto snapshot=brep(state.shape);
            for(int trial=1;trial<=3;++trial) {
                v.resetModelingTools();v.context_->ClearSelected(Standard_False);
                v.fitAll();QApplication::processEvents();const auto pixel=project(v,point);
                const auto phase=[&](const std::string& name,auto action) {
                    BRepTiming::reset();BRepTiming::detailed=detailed;BRepTiming::enabled=true;
                    {BRepTiming::Scope phaseScope("phase.total");action();}
                    BRepTiming::enabled=false;
                    BRepTiming::report(std::cout,labels[kind]+"/"+std::to_string(trial)+"/"+name);
                    BRepTiming::detailed=false;
                };
                phase("selection",[&]{
                    v.setSelectionMode(CadViewport::SelectionMode::Face);
                    QMouseEvent press(QEvent::MouseButtonPress,QPointF(pixel),QPointF(v.mapToGlobal(pixel)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                    QApplication::sendEvent(&v,&press);
                    QMouseEvent release(QEvent::MouseButtonRelease,QPointF(pixel),QPointF(v.mapToGlobal(pixel)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(&v,&release);QApplication::processEvents();
                    const auto selected=v.selectionContext();
                    require(selected.faces.size()==1&&selected.faces[0].face.IsSame(top),"timing click did not select correct cap");
                });
                std::unique_ptr<QMenu> menu;
                phase("context-menu",[&]{menu.reset(v.createSelectionMenu(pixel));});
                phase("startup",[&]{v.moveFace();require(v.commandSession_&&v.commandSession_->face.IsSame(top),"timing command start failed");QApplication::processEvents();});
                const auto undoBefore=v.undoStack_.size();
                phase("preview-15",[&]{
                    v.dragDimension_->setText("15");require(v.updateCommandParameter(false),"timing preview failed");
                    BRepTiming::Event delivery("Preview.EventFlush");QApplication::processEvents();delivery.finish();
                });
                phase("confirmation",[&]{
                    v.confirmCommandSession();QApplication::processEvents();
                    require(!v.commandSession_&&v.undoStack_.size()==undoBefore+1,"timing confirmation did not commit one transaction");
                });
                v.undoPull();QApplication::processEvents();
                require(v.boxes_[0].shape.IsSame(state.shape),"timing Undo did not restore exact source");
                require(brep(state.shape)==snapshot,"timing run mutated source");
            }
        }
    }
    static void obroundSession() {
        MainWindow window;window.show();QApplication::processEvents();
        auto& v=*window.findChild<CadViewport*>();v.addBox();
        auto state=v.boxes_[0];state.shape=ObroundFixture::prism();
        state.geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;
        Bnd_Box bounds;BRepBndLib::AddOptimal(state.shape,bounds,false,false);
        bounds.Get(state.min[0],state.min[1],state.min[2],state.max[0],state.max[1],state.max[2]);
        state.baseMin=state.min;state.baseMax=state.max;
        state.orientation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1)),.3);
        state.orientation.SetTranslationPart(gp_Vec(11,19,-7));
        v.applyFaceState(0,state);v.renamePart(0,"Obround identity");v.setPartColor(0,QColor(31,73,109));
        v.fitAll();QApplication::processEvents();
        const auto original=v.boxes_[0];const auto source=brep(original.shape);
        const auto undo=v.undoStack_.size(),redo=v.redoStack_.size(),count=v.boxes_.size();
        TopoDS_Face top;
        BRepReconstruction::TopologyIndex index;QString error;
        require(BRepReconstruction::TopologyIndex::build(original.shape,index,error),"obround session topology failed");
        for(const auto& record:index.faces()) {
            const auto& surface=record.support.surface;
            if(surface.GetType()==GeomAbs_Plane&&surface.Plane().Axis().Direction().IsParallel(ObroundFixture::axis(),1.e-8)
               &&surface.Plane().Distance(ObroundFixture::world(0,0,20))<1.e-7) top=record.support.face;
        }
        require(!top.IsNull()&&index.face(top)->wires.size()==2,"correct obround top Face not selected");
        for(bool confirm:{false,true}) {
            selectFace(v,0,top);v.moveFace();
            require(v.commandSession_&&v.commandSession_->kind==CadViewport::CommandSession::Kind::MoveFace
                &&v.commandSession_->face.IsSame(top),"existing Move command did not adopt the top Face");
            v.dragDimension_->setText("15");require(v.updateCommandParameter(false),"obround Move preview failed");
            require(v.commandSession_->valid&&!v.commandSession_->preview.IsNull(),"obround blue preview missing");
            visiblePreview(v);near(volume(v.commandSession_->candidate),35*ObroundFixture::area(),"obround preview volume wrong");
            require(BRepCertification::equivalent(v.commandSession_->candidate,ObroundFixture::booleanSolid(35)),"obround preview material wrong");
            require(v.boxes_[0].shape.IsSame(original.shape)&&brep(original.shape)==source
                &&v.undoStack_.size()==undo&&v.redoStack_.size()==redo,"preview changed source or history");
            v.commandButtons_->button(confirm?QDialogButtonBox::Ok:QDialogButtonBox::Cancel)->click();
            require(!v.commandSession_,"obround session did not finish");
            if(!confirm) require(v.boxes_[0].shape.IsSame(original.shape)&&v.undoStack_.size()==undo
                &&v.redoStack_.size()==redo&&v.context_->IsDisplayed(original.presentation),"obround Cancel changed source/history/display");
        }
        const auto edited=v.boxes_[0];
        require(v.undoStack_.size()==undo+1&&v.boxes_.size()==count,"obround OK did not produce one in-place transaction");
        near(volume(edited.shape),35*ObroundFixture::area(),"obround committed height/volume wrong");
        require(edited.persistentId==original.persistentId&&edited.presentation==original.presentation
            &&edited.name==original.name&&edited.color==original.color&&edited.visible==original.visible,"obround Part identity/appearance changed");
        for(int r=1;r<=3;++r) for(int c=1;c<=4;++c)
            require(edited.orientation.Value(r,c)==original.orientation.Value(r,c),"obround Part placement changed");
        v.undoPull();require(v.boxes_[0].shape.IsSame(original.shape),"obround Undo lost original geometry");
        v.redoPull();require(v.boxes_[0].shape.IsSame(edited.shape),"obround Redo did not restore cached result");
        require(brep(original.shape)==source,"confirmed obround edit mutated source snapshot");
        std::cout<<"Obround Move Face selection, blue preview, OK/Cancel, identity/placement and Undo/Redo passed\n";
    }
    static void ellipticalSession() {
        for(bool hole:{false,true}) {
            MainWindow window;window.show();QApplication::processEvents();
            auto& v=*window.findChild<CadViewport*>();v.addBox();
            auto state=v.boxes_[0];state.shape=EllipticalFixture::prism(20,hole);
            state.geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;
            state.min={-50,-30,0};state.max={50,30,20};state.baseMin=state.min;state.baseMax=state.max;
            state.orientation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),.4);
            state.orientation.SetTranslationPart(gp_Vec(17,-9,23));
            v.applyFaceState(0,state);v.renamePart(0,"Elliptical identity");v.setPartColor(0,Qt::red);
            v.fitAll();QApplication::processEvents();
            const auto original=v.boxes_[0];const auto source=brep(original.shape);
            const auto top=cap(original.shape,true);const auto undo=v.undoStack_.size();
            const double expected=(hole?1400:1500)*std::numbers::pi*35;
            selectFace(v,0,top);v.startPrimitivePull();
            require(v.commandSession_&&!v.commandSession_->allowEllipticalCapMove,"Pull inherited elliptical Move capability");
            v.dragDimension_->setText("15");require(!v.updateCommandParameter(false),"unsupported elliptical Pull accepted");
            v.cancelCommandSession();
            require(v.startMoveFace(0,top),"elliptical Move session start failed");
            require(v.setMoveFaceDirection(CadViewport::MoveDirectionSource::Reference,
                {{References::Kind::Face,original.persistentId,top}},false),"existing Face-normal reference failed");
            v.dragDimension_->setText("15");require(v.updateCommandParameter(false),"elliptical preview failed");
            require(v.commandSession_->valid&&!v.commandSession_->preview.IsNull(),"elliptical blue preview missing");
            visiblePreview(v);near(volume(v.commandSession_->candidate),expected,"elliptical preview volume wrong");
            require(v.boxes_[0].shape.IsSame(original.shape)&&brep(original.shape)==source&&v.undoStack_.size()==undo,"preview mutated source/history");
            NativeDocumentSnapshot preview;require(v.exportDocumentSnapshot(preview),"capture during preview failed");
            near(volume(preview.parts[0].shape),(hole?28000:30000)*std::numbers::pi,"preview entered saved state");
            v.cancelCommandSession();require(!v.commandSession_&&v.boxes_[0].shape.IsSame(original.shape)&&v.undoStack_.size()==undo,"Cancel changed source/history");
            require(v.startMoveFace(0,top),"elliptical restart failed");
            v.dragDimension_->setText("15");v.confirmCommandSession();
            require(!v.commandSession_&&v.undoStack_.size()==undo+1,"elliptical commit was not one transaction");
            const auto edited=v.boxes_[0];near(volume(edited.shape),expected,"elliptical committed volume wrong");
            require(edited.persistentId==original.persistentId&&edited.name==original.name&&edited.color==original.color
                &&edited.visible==original.visible&&edited.presentation==original.presentation,"Part identity/appearance changed");
            for(int r=1;r<=3;++r) for(int c=1;c<=4;++c)
                near(edited.orientation.Value(r,c),original.orientation.Value(r,c),"Part placement changed");
            v.undoPull();require(v.boxes_[0].shape.IsSame(original.shape),"elliptical Undo lost source");
            v.redoPull();require(v.boxes_[0].shape.IsSame(edited.shape),"elliptical Redo recalculated result");
            NativeDocumentSnapshot saved,loaded;QString error;QTemporaryDir temp;require(temp.isValid(),"temporary directory failed");
            require(v.exportDocumentSnapshot(saved)&&NativeDocumentStorage::save(temp.filePath("elliptical.odc"),saved,error)
                &&NativeDocumentStorage::load(temp.filePath("elliptical.odc"),loaded,error),"elliptical native roundtrip failed");
            require(loaded.parts[0].id==edited.persistentId&&loaded.parts[0].name==edited.name
                &&BRepCertification::equivalent(loaded.parts[0].shape,edited.shape),"native roundtrip lost identity/material");
            require(v.importDocumentSnapshot(loaded,error),"elliptical Open failed");
            require(v.startMoveFace(0,cap(v.boxes_[0].shape,true)),"reopened cap lost eligibility");
            v.dragDimension_->setText("-5");require(v.updateCommandParameter(false),"reopened cap shortening failed");
            near(volume(v.commandSession_->candidate),(hole?1400:1500)*std::numbers::pi*30,"reopened shortening volume wrong");
            v.cancelCommandSession();
        }
        std::cout<<"Elliptical Move Face preview, references, history and native persistence passed\n";
    }
    static void splineSession() {
        for(bool hole:{false}) {
            MainWindow window;window.show();QApplication::processEvents();
            auto& v=*window.findChild<CadViewport*>();v.addBox();
            auto state=v.boxes_[0];state.shape=SplineFixture::solid();
            state.geometryKind=CadViewport::CadPart::GeometryKind::GeneralBRep;
            state.min={0,10,0};state.max={60,30,20};state.baseMin=state.min;state.baseMax=state.max;
            state.orientation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),.4);
            state.orientation.SetTranslationPart(gp_Vec(17,-9,23));
            v.applyFaceState(0,state);v.renamePart(0,"Spline identity");v.setPartColor(0,Qt::red);
            v.fitAll();QApplication::processEvents();
            const auto original=v.boxes_[0];const auto source=brep(original.shape);
            const auto top=cap(original.shape,true);const auto undo=v.undoStack_.size();
            const double expected=SplineFixture::area*35;
            selectFace(v,0,top);v.startPrimitivePull();
            require(v.commandSession_&&!v.commandSession_->allowEllipticalCapMove,"Pull inherited spline Move capability");
            v.dragDimension_->setText("15");require(!v.updateCommandParameter(false),"unsupported spline Pull accepted");
            v.cancelCommandSession();
            require(v.startMoveFace(0,top),"spline Move session start failed");
            TopoDS_Shape low,high;
            for(TopExp_Explorer vertex(original.shape,TopAbs_VERTEX);vertex.More();vertex.Next()) {
                const auto p=BRep_Tool::Pnt(TopoDS::Vertex(vertex.Current()));
                if(p.X()<1.e-7&&p.Z()<1.e-7) low=vertex.Current();
                if(p.X()>59.99&&p.Z()>19.99) high=vertex.Current();
            }
            require(!low.IsNull()&&!high.IsNull(),"spline diagonal references missing");
            v.dragDimension_->setText("15");
            require(!v.setMoveFaceDirection(CadViewport::MoveDirectionSource::PointToPoint,
                {{References::Kind::Vertex,original.persistentId,low},{References::Kind::Vertex,original.persistentId,high}},false)
                &&!v.commandSession_->valid,"oblique spline reference was projected into an accepted edit");
            require(v.setMoveFaceDirection(CadViewport::MoveDirectionSource::Reference,
                {{References::Kind::Face,original.persistentId,top}},false),"existing Face-normal reference failed");
            v.dragDimension_->setText("15");require(v.updateCommandParameter(false),"spline preview failed");
            require(v.commandSession_->valid&&!v.commandSession_->preview.IsNull(),"spline blue preview missing");
            visiblePreview(v);near(BRepCertification::volume(v.commandSession_->candidate),expected,"spline preview volume wrong");
            require(v.boxes_[0].shape.IsSame(original.shape)&&brep(original.shape)==source&&v.undoStack_.size()==undo,"preview mutated source/history");
            NativeDocumentSnapshot preview;require(v.exportDocumentSnapshot(preview),"capture during preview failed");
            near(BRepCertification::volume(preview.parts[0].shape),SplineFixture::area*20,"preview entered saved state");
            v.cancelCommandSession();require(!v.commandSession_&&v.boxes_[0].shape.IsSame(original.shape)&&v.undoStack_.size()==undo,"Cancel changed source/history");
            require(v.startMoveFace(0,top),"spline restart failed");
            v.dragDimension_->setText("15");v.confirmCommandSession();
            require(!v.commandSession_&&v.undoStack_.size()==undo+1,"spline commit was not one transaction");
            const auto edited=v.boxes_[0];near(BRepCertification::volume(edited.shape),expected,"spline committed volume wrong");
            require(edited.persistentId==original.persistentId&&edited.name==original.name&&edited.color==original.color
                &&edited.visible==original.visible&&edited.presentation==original.presentation,"Part identity/appearance changed");
            for(int r=1;r<=3;++r) for(int c=1;c<=4;++c)
                near(edited.orientation.Value(r,c),original.orientation.Value(r,c),"Part placement changed");
            v.undoPull();require(v.boxes_[0].shape.IsSame(original.shape),"spline Undo lost source");
            v.redoPull();require(v.boxes_[0].shape.IsSame(edited.shape),"spline Redo recalculated result");
            NativeDocumentSnapshot saved,loaded;QString error;QTemporaryDir temp;require(temp.isValid(),"temporary directory failed");
            require(v.exportDocumentSnapshot(saved)&&NativeDocumentStorage::save(temp.filePath("spline.odc"),saved,error)
                &&NativeDocumentStorage::load(temp.filePath("spline.odc"),loaded,error),"spline native roundtrip failed");
            require(loaded.parts[0].id==edited.persistentId&&loaded.parts[0].name==edited.name
                &&BRepCertification::equivalent(loaded.parts[0].shape,edited.shape),"native roundtrip lost identity/material");
            require(v.importDocumentSnapshot(loaded,error),"spline Open failed");
            require(v.startMoveFace(0,cap(v.boxes_[0].shape,true)),"reopened cap lost eligibility");
            v.dragDimension_->setText("-5");require(v.updateCommandParameter(false),"reopened cap shortening failed");
            near(BRepCertification::volume(v.commandSession_->candidate),SplineFixture::area*30,"reopened shortening volume wrong");
            v.cancelCommandSession();
        }
        std::cout<<"Spline Move Face preview, references, history and native persistence passed\n";
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
            if(v.pendingMoveNumeric_)awaitNumeric(v);
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
            if(v.pendingMoveNumeric_)awaitNumeric(v);
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
void runSlopedPlaneTest(const QString& path);
void runFacePersistenceHistoryTest(const QString& directory);
int main(int argc,char** argv) {
    if(argc>3&&std::string(argv[1])=="--spline-headless") {
        QCoreApplication app(argc,argv);
        try {
            bool expectedGiven=false;const double expected=QString::fromLocal8Bit(argc>4?argv[4]:"").toDouble(&expectedGiven);
            require(argc<=4||expectedGiven,"invalid expected source height");
            MoveFaceTest::splineHeadless(QString::fromLocal8Bit(argv[2]),QString::fromLocal8Bit(argv[3]).toInt(),expectedGiven?expected:std::numeric_limits<double>::quiet_NaN());return 0;
        }
        catch(const Standard_Failure& e){std::cerr<<e.GetMessageString()<<'\n';return 1;}
        catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    }
    if(argc>1&&std::string(argv[1])=="--geometry") {
        try {MoveFaceTest::geometry();std::cout<<"Move Face geometry passed\n";return 0;}
        catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
    }
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
        const int persistence=app.arguments().indexOf("--history-persistence");
        const int plane=app.arguments().indexOf("--sloped-plane");
        const int splineDocument=app.arguments().indexOf("--spline-document");
        const int splineProfile=app.arguments().indexOf("--spline-profile");
        if(splineProfile>=0) {
            bool expectedGiven=false;const auto text=app.arguments().value(splineProfile+4);const double expected=text.toDouble(&expectedGiven);
            require(text.isEmpty()||expectedGiven,"invalid expected source height");
            MoveFaceTest::splineDocument(app.arguments().value(splineProfile+1),app.arguments().value(splineProfile+2).toInt(),app.arguments().value(splineProfile+3,"3").toInt(),true,expectedGiven?expected:std::numeric_limits<double>::quiet_NaN());
        }
        else if(splineDocument>=0) MoveFaceTest::splineDocument(app.arguments().value(splineDocument+1),app.arguments().value(splineDocument+2).toInt(),app.arguments().value(splineDocument+3,"3").toInt());
        else if(app.arguments().contains("--preview-certification-timings")) MoveFaceTest::startupTimings(true);
        else if(app.arguments().contains("--numeric-input")) MoveFaceTest::numericInput();
        #ifdef OPENDESIGNER_ENABLE_BOX_SHADOW
        else if(app.arguments().contains("--box-shadow-observation")) MoveFaceTest::boxShadowObservation();
#endif
        else if(app.arguments().contains("--interactive-provisional")) MoveFaceTest::interactiveProvisional();
        else if(app.arguments().contains("--drag-initialization")) MoveFaceTest::dragInitialization();
        else if(app.arguments().contains("--provisional-acceptance-timings")) MoveFaceTest::provisionalAcceptanceTimings();
        else if(app.arguments().contains("--box-provisional")) MoveFaceTest::provisionalBox();
        else if(app.arguments().contains("--provisional-placement")) MoveFaceTest::provisionalPlacement();
        else if(app.arguments().contains("--clear-sweep-provisional")) MoveFaceTest::clearSweepSession();
        else if(app.arguments().contains("--stacked-cylinder-unification")) {MoveFaceTest::stackedCylinderUnification();MoveFaceTest::stackedCylinderConstructionMethods();}
        else if(app.arguments().contains("--preview-reuse")) MoveFaceTest::previewReuse();
        else if(app.arguments().contains("--startup-validation")) MoveFaceTest::startupValidation();
        else if(app.arguments().contains("--startup-timings")) MoveFaceTest::startupTimings();
        else if(app.arguments().contains("--spline")) MoveFaceTest::splineSession();
        else if(app.arguments().contains("--obround")) MoveFaceTest::obroundSession();
        else if(app.arguments().contains("--elliptical")) MoveFaceTest::ellipticalSession();
        else if(plane>=0) runSlopedPlaneTest(app.arguments().value(plane+1));
        else if(persistence>=0) runFacePersistenceHistoryTest(app.arguments().value(persistence+1));
        else if(rotated>=0) runRotatedHoleTest(app.arguments().value(rotated+1));
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
