// v0.5.3 Workplane and reference sessions: camera decoupling, Look at
// Workplane, frame presentation during Move/Rotate previews, reference picking
// and snapping, Move Face direction sources, Undo/Redo, Save/Open, view modes.
#include "CadViewport.h"
#include "MainWindow.h"
#include "ConstructionPanel.h"
#include "MoveFaceGeometry.h"
#include "BRepReconstruction.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QMessageBox>
#include <QLocale>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <QMouseEvent>
#include <BRep_Tool.hxx>
#include <Image_AlienPixMap.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
}
struct WorkplaneReferenceSessionTest {
    static Construction::Object workplane(const QString& id,const QString& name,const gp_Ax3& frame) {
        Construction::Object wp;wp.id=id;wp.name=name;wp.kind=Construction::Kind::Workplane;wp.frame=frame;return wp;
    }
    struct CameraState {gp_Dir direction,up;gp_Pnt eye,center;double scale;};
    static CameraState camera(CadViewport& v) {
        const auto c=v.view_->Camera();return {c->Direction(),c->Up(),c->Eye(),c->Center(),c->Scale()};
    }
    static void sameCamera(const CameraState& a,const CameraState& b,const char* message) {
        check(a.direction.IsEqual(b.direction,1.e-12) && a.up.IsEqual(b.up,1.e-12) && a.eye.Distance(b.eye)<1.e-9
            && a.center.Distance(b.center)<1.e-9 && std::abs(a.scale-b.scale)<1.e-9,message);
    }
    static QAction* action(MainWindow& w,const char* name) {
        auto* a=w.findChild<QAction*>(name);check(a,"action missing");return a;
    }
    // 1-2: activation leaves the camera unchanged; Look at Workplane aligns it.
    static void cameraDecoupling() {
        MainWindow w;w.resize(1300,950);w.show();QApplication::processEvents();auto& v=*w.viewport_;QString error;
        NativeDocumentSnapshot doc;
        const gp_Ax3 tilted(gp_Pnt(20,-30,40),gp_Dir(1,2,3),gp_Dir(2,-1,0));
        doc.constructions={workplane("wp1","WP1",tilted),workplane("wp2","WP2",gp_Ax3(gp_Pnt(0,0,50),gp_Dir(0,0,1),gp_Dir(1,0,0)))};
        check(v.importDocumentSnapshot(doc,error),qPrintable(error));w.documentDirty_=false;
        v.view_->SetProj(V3d_XposYnegZpos);v.fitAll();QApplication::processEvents();
        auto* look=action(w,"view.look_at_workplane");check(!look->isEnabled(),"Look at Workplane enabled without a Workplane");
        // Every activation route keeps the camera.
        auto before=camera(v);
        v.selectConstructions({"wp1"});check(look->isEnabled(),"Look at Workplane disabled for a selected Workplane");
        action(w,"sketch.edit")->trigger();check(v.activeSketchWorkplaneId_=="wp1","Edit Sketch did not activate");
        sameCamera(before,camera(v),"Edit Sketch moved the camera");
        auto* combo=w.findChild<QComboBox*>("sketchWorkplane");combo->setCurrentIndex(combo->findData("wp2"));
        check(v.activeSketchWorkplaneId_=="wp2","Workplane combo did not activate");sameCamera(before,camera(v),"Workplane combo moved the camera");
        v.endSketchEditing();v.selectConstructions({"wp1"});
        std::unique_ptr<QMenu> menu(v.createSelectionMenu(QPoint(-10,-10)));
        auto* edit=menu->findChild<QAction*>("context.edit_sketch");check(edit,"context Edit Sketch missing");edit->trigger();
        check(v.activeSketchWorkplaneId_=="wp1","context Edit Sketch did not activate");sameCamera(before,camera(v),"context Edit Sketch moved the camera");
        check(v.activateSketchWorkplane("wp2"),"API activation failed");sameCamera(before,camera(v),"API activation moved the camera");
        check(!w.documentDirty_ && v.undoStack_.empty(),"activation dirtied document or history");
        // Explicit alignment: view along -normal, +Y up, active Workplane first.
        const auto expect=[&](const gp_Ax3& frame) {
            const auto c=v.view_->Camera();
            check(c->Direction().IsEqual(frame.Direction().Reversed(),1.e-9),"Look at Workplane view direction is not -normal");
            check(c->Up().IsEqual(frame.YDirection(),1.e-9),"Look at Workplane up vector is not Workplane +Y");
        };
        look->trigger();expect(doc.constructions[1].frame);
        v.endSketchEditing();v.selectConstructions({"wp1"});look->trigger();expect(tilted);
        check(v.activeSketchWorkplaneId_.isEmpty(),"Look at Workplane activated the Workplane");
        v.view_->SetProj(V3d_XposYnegZpos);menu.reset(v.createSelectionMenu(QPoint(-10,-10)));
        auto* contextLook=menu->findChild<QAction*>("context.look_at_workplane");check(contextLook,"context Look at Workplane missing");
        contextLook->trigger();expect(tilted);check(v.activeSketchWorkplaneId_.isEmpty(),"context Look at Workplane activated the Workplane");
        check(!w.documentDirty_ && v.undoStack_.empty(),"Look at Workplane dirtied document or history");
        v.selectConstructions({"wp1","wp2"});check(!look->isEnabled() && !v.lookAtWorkplane(),"ambiguous Look at Workplane accepted");
        std::cout<<"camera decoupling passed\n";
    }
    // 3: minimum frame, 10 mm ring, frame visible during Move/Rotate previews.
    static void framePresentation() {
        CadViewport v;v.resize(1100,850);v.show();QApplication::processEvents();QString error;
        NativeDocumentSnapshot doc;auto wp=workplane("wp","WP1",gp_Ax3(gp_Pnt(0,0,0),gp_Dir(0,0,1),gp_Dir(1,0,0)));wp.extent=10;
        doc.constructions={wp};check(v.importDocumentSnapshot(doc,error),qPrintable(error));
        const auto outer=v.workplaneDisplayRectangle(wp);
        check(std::abs(outer.maxX-outer.minX-(100+2*10))<1.e-9 && std::abs(outer.maxY-outer.minY-(100+2*10))<1.e-9,"small Workplane frame is not 100 mm + 2 x 10 mm ring");
        check(v.activateSketchWorkplane("wp") && v.lookAtWorkplane("wp"),"activation failed");
        v.view_->SetScale(v.view_->Scale()/3);QApplication::processEvents();
        // Renders the current scene and samples Workplane-local points.
        const auto sample=[&](const gp_Ax3& frame,std::vector<Sketch::Point> points) {
            Image_AlienPixMap image;v.context_->ClearDetected(Standard_False);v.view_->Redraw();
            check(v.view_->ToPixMap(image,v.width(),v.height()),"capture failed");std::vector<Quantity_Color> colors;
            for(const auto p:points) {
                const auto world=Sketch::worldPoint(p,frame);int x,y;v.view_->Convert(world.X(),world.Y(),world.Z(),x,y);
                check(x>=0 && y>=0 && x<int(image.SizeX()) && y<int(image.SizeY()),"frame sample outside capture");
                colors.push_back(image.PixelColor(x,y).GetRGB());
            }
            return colors;
        };
        const double ring=outer.maxX-5; // middle of the 10 mm ring on +X
        auto colors=sample(wp.frame,{{ring+150,0},{ring,0}});const auto background=colors[0];
        check(colors[1].Distance(background)>0.05,"active frame ring not rendered");
        // Translation: the blue candidate frame is visible at the moved location.
        v.startConstruction(Construction::Method::PlaneMove,"wp");v.constructionPanel_->setVector(gp_Vec(150,0,0));
        check(v.commandSession_ && v.commandSession_->valid && !v.commandSession_->preview.IsNull(),"Move Workplane preview missing");
        const auto moved=sample(wp.frame,{{ring+150,0}})[0];
        check(moved.Distance(background)>0.05,"Workplane frame invisible during Move preview");
        check(moved.Blue()>moved.Red()+0.05,"Move preview frame is not blue");
        v.cancelCommandSession();check(v.constructionObject("wp")->frame.Location().Distance(gp_Pnt(0,0,0))<1.e-12,"Cancel moved the Workplane");
        colors=sample(wp.frame,{{ring,0},{ring+150,0}});
        check(colors[0].Distance(background)>0.05,"Cancel did not restore the original frame");
        check(colors[1].Distance(background)<0.05,"Cancel left the preview frame");
        // Rotation about world Y by 30 degrees: the frame stays visible.
        v.startConstruction(Construction::Method::PlaneRotate,"wp");v.constructionPanel_->setVector(gp_Vec(0,1,0));v.constructionPanel_->setValue(30);
        check(v.commandSession_ && v.commandSession_->valid,"Rotate Workplane preview missing");
        const auto rotated=v.commandSession_->construction->candidate.frame;
        // Ring corners: away from the orange rotation manipulator at every scale.
        const auto turned=sample(rotated,{{ring,ring},{-ring,-ring},{ring,-ring}});
        for(const auto& c:turned) check(c.Distance(background)>0.05 && c.Blue()>c.Red()+0.05,"Workplane frame invisible during Rotate preview");
        v.confirmCommandSession();check(v.undoStack_.size()==1,"Rotate Workplane not one Undo step");
        v.undoPull();check(v.constructionObject("wp")->frame.Direction().IsEqual(gp_Dir(0,0,1),1.e-12),"Rotate Undo failed");
        std::cout<<"frame presentation passed\n";
    }
    // ---- references, picking, snapping and Move Face direction ----
    static TopoDS_Face planar(const TopoDS_Shape& shape,const gp_Dir& normal) {
        for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());BRepAdaptor_Surface s(face);if(s.GetType()!=GeomAbs_Plane) continue;
            gp_Dir n=s.Plane().Axis().Direction();if(face.Orientation()==TopAbs_REVERSED) n.Reverse();
            if(n.IsEqual(normal,1.e-9)) return face;
        }
        throw std::runtime_error("planar face missing");
    }
    static TopoDS_Edge edge(const TopoDS_Shape& shape,const gp_Pnt& a,const gp_Pnt& b) {
        for(TopExp_Explorer it(shape,TopAbs_EDGE);it.More();it.Next()) {
            const auto e=TopoDS::Edge(it.Current());
            const auto p=BRep_Tool::Pnt(TopExp::FirstVertex(e)),q=BRep_Tool::Pnt(TopExp::LastVertex(e));
            if((p.Distance(a)<1.e-9&&q.Distance(b)<1.e-9)||(p.Distance(b)<1.e-9&&q.Distance(a)<1.e-9)) return e;
        }
        throw std::runtime_error("edge missing");
    }
    static QPoint pixel(CadViewport& v,const gp_Pnt& p) {int x,y;v.view_->Convert(p.X(),p.Y(),p.Z(),x,y);return {x,y};}
    static void click(CadViewport& v,const QPoint& p) {
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(p),QPointF(v.mapToGlobal(p)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(p),QPointF(v.mapToGlobal(p)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        v.mousePressEvent(&press);v.mouseReleaseEvent(&release);
    }
    static void chooseFace(CadViewport& v,size_t index,const TopoDS_Face& face) {
        v.selectParts({});v.context_->ClearSelected(Standard_False);v.selectedPartIndices_.clear();
        Handle(StdSelect_BRepOwner) owner=new StdSelect_BRepOwner(face,v.boxes_[index].presentation,5,true);
        v.context_->AddOrRemoveSelected(owner,Standard_False);
        check(v.selectionContext().kind==CadViewport::SelectionContext::Kind::Faces,"Face selection failed");
    }
    static NativeDocumentSnapshot referenceFixture() {
        NativeDocumentSnapshot doc;
        NativePartState target;target.id="target";target.name="Target";target.shape=BRepPrimAPI_MakeBox(100,80,60).Shape();
        NativePartState source;source.id="source";source.name="Source";source.shape=BRepPrimAPI_MakeBox(gp_Pnt(200,0,0),20,20,40).Shape();
        NativePartState hole;hole.id="hole";hole.name="Holed";
        hole.shape=BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(gp_Pnt(0,200,0),60,60,30).Shape(),BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(30,230,-5),gp_Dir(0,0,1)),8,40).Shape()).Shape();
        doc.parts={target,source,hole};
        // Vertical Workplane: local X = world X, local Y = world Z.
        auto wp=workplane("wp","WP1",gp_Ax3(gp_Pnt(0,-100,0),gp_Dir(0,-1,0),gp_Dir(1,0,0)));
        Construction::Object axis;axis.id="ax";axis.name="AX1";axis.kind=Construction::Kind::Axis;axis.frame=gp_Ax3(gp_Pnt(150,0,0),gp_Dir(1,0,1));axis.extent=40;
        Construction::Object point;point.id="pt";point.name="PT1";point.kind=Construction::Kind::Point;point.frame=gp_Ax3(gp_Pnt(150,50,0),gp_Dir(0,0,1));
        doc.constructions={wp,axis,point};
        const auto entity=[](const QString& id,Sketch::Geometry g){Sketch::Entity e;e.id=id;e.name=id;e.workplaneId="wp";e.geometry=g;return e;};
        doc.sketchEntities={entity("seg",Sketch::Segment{{0,0},{30,40}}),entity("circ",Sketch::Circle{{60,20},10}),entity("cross",Sketch::Segment{{0,40},{40,0}})};
        return doc;
    }
    static gp_Pnt sketchWorld(CadViewport& v,Sketch::Point p) {return Sketch::worldPoint(p,v.constructionObject("wp")->frame);}
    // 4, 7: Axis/Point from Sketch references; picking == snapping.
    static void pickingAndSnapping(CadViewport& v) {
        // Snap targets with references are picked as exactly the same reference.
        const auto targets=Sketch::snapTargets(v.sketchEntities_,"wp",{},{},true);
        size_t compared=0;
        for(const auto& t:targets) if(t.reference && (t.kind==Sketch::SnapKind::Center || t.kind==Sketch::SnapKind::Intersection)) {
            QString error;const auto picked=v.pickReference(pixel(v,sketchWorld(v,t.point)),{References::Kind::SketchPoint},error);
            check(picked && picked->kind==References::Kind::SketchPoint && picked->point==*t.reference,"pick and snap chose different Sketch references");
            References::Resolved r;check(v.resolveReference(*picked,r,error) && r.point->Distance(sketchWorld(v,t.point))<1.e-9,"picked Sketch point differs from snap point");
            // The Sketch drawing snap at the same pixel carries the same reference.
            check(v.activateSketchWorkplane("wp"),"activate");v.startSketchDrawing(Sketch::DrawTool::Segment);
            v.sketchDrawingMove(pixel(v,sketchWorld(v,t.point)),Qt::NoModifier);
            const auto& drawing=*v.commandSession_->drawing;
            check(drawing.snap && drawing.snap->reference && *drawing.snap->reference==*t.reference,"Sketch snap did not resolve to the picked reference");
            v.cancelCommandSession();v.endSketchEditing();++compared;
        }
        check(compared==2,"expected the circle center and the segment intersection");
        // Axis from a Sketch line and Point from a Sketch circle center through construction role picking.
        const auto history=v.undoStack_.size();
        v.startConstruction(Construction::Method::AxisEdge);v.chooseConstructionRole(0);
        v.captureConstructionReference(pixel(v,sketchWorld(v,{15,20})));
        check(v.commandSession_ && v.commandSession_->valid,"Axis from Sketch line not accepted");
        check(v.commandSession_->construction->candidate.frame.Direction().IsEqual(gp_Dir(0.6,0,0.8),1.e-12),"Axis from Sketch line direction");
        v.confirmCommandSession();
        v.startConstruction(Construction::Method::PointVertex);v.chooseConstructionRole(0);
        v.captureConstructionReference(pixel(v,sketchWorld(v,{60,20})));
        check(v.commandSession_ && v.commandSession_->valid && v.commandSession_->construction->candidate.frame.Location().Distance(sketchWorld(v,{60,20}))<1.e-9,"Point from Sketch center");
        v.confirmCommandSession();
        // Face normal at a point (Axis) from a selected planar Face and an existing Point.
        chooseFace(v,0,planar(v.boxes_[0].shape,gp_Dir(0,0,1)));
        v.startConstruction(Construction::Method::AxisFaceNormal);auto* panel=v.constructionPanel_;
        panel->roles[1]->setCurrentIndex(panel->roles[1]->findData("pt"));
        check(v.commandSession_ && v.commandSession_->valid,"Axis normal at point not valid");
        const auto normalAxis=v.commandSession_->construction->candidate;
        check(normalAxis.frame.Direction().IsEqual(gp_Dir(0,0,1),1.e-12) && normalAxis.frame.Location().Distance(gp_Pnt(150,50,60))<1.e-9,"Axis normal at point geometry");
        v.confirmCommandSession();check(v.undoStack_.size()==history+3,"each construction is one Undo step");
        const auto created=v.constructions_;
        v.undoPull();v.undoPull();v.undoPull();check(v.constructions_.size()==created.size()-3,"construction Undo");
        v.redoPull();v.redoPull();v.redoPull();check(v.constructions_.size()==created.size(),"construction Redo");
        // Save/Open round trip of the new Axes and Point.
        NativeDocumentSnapshot saved,reopened;QTemporaryDir dir;QString error;
        check(v.exportDocumentSnapshot(saved) && NativeDocumentStorage::save(dir.filePath("refs.odc"),saved,error)
            && NativeDocumentStorage::load(dir.filePath("refs.odc"),reopened,error),qPrintable(error));
        check(reopened.constructions.size()==created.size(),"constructions lost on Save/Open");
        for(size_t i=0;i<created.size();++i) check(Construction::equivalent(reopened.constructions[i],created[i]),"construction changed on Save/Open");
        std::cout<<"reference picking, snapping and Sketch-based constructions passed\n";
    }
    // 5, 8, 9, 6, 10: Move Face direction sources.
    static void moveFaceDirections(CadViewport& v) {
        using MDS=CadViewport::MoveDirectionSource;QString error;
        const auto source=v.boxes_[1].shape;
        // A real pick of the Source Part's vertical Edge; it follows Part transforms.
        const auto vertical=edge(source,gp_Pnt(200,0,0),gp_Pnt(200,0,40));
        const auto edgeTarget=v.pickReference(pixel(v,gp_Pnt(200,0,20)),{References::Kind::Edge},error);
        check(edgeTarget && edgeTarget->kind==References::Kind::Edge && edgeTarget->ownerId=="source" && edgeTarget->subshape.IsSame(vertical),"Edge pick failed");
        References::Resolved r;check(v.resolveReference(*edgeTarget,r,error),qPrintable(error));
        const gp_Dir edgeDirection=r.line->Direction();
        v.beginCommandSession(CadViewport::CommandSession::Kind::Move,{1},gp_Dir(1,0,0));v.dragDimension_->setText("10");v.confirmCommandSession();
        check(v.resolveReference(*edgeTarget,r,error) && r.point->Distance(gp_Pnt(210,0,20))<1.e-9 && r.line->Direction().IsEqual(edgeDirection,1.e-12),"Edge reference did not follow the moved Part");
        v.undoPull();
        const auto top=planar(v.boxes_[0].shape,gp_Dir(0,0,1));const auto original=v.boxes_[0].shape;
        // Oracle: certified planar normal offset by distance * (d . n).
        const auto expectMove=[&](MDS kind,std::vector<References::Target> refs,bool reverse,double distance,double factor,const char* label) {
            const auto history=v.undoStack_.size();chooseFace(v,0,top);v.moveFace();
            check(v.commandSession_ && v.commandSession_->kind==CadViewport::CommandSession::Kind::MoveFace,"Move Face session missing");
            v.setMoveFaceDirection(kind,refs,reverse);v.dragDimension_->setText(QLocale().toString(distance));
            check(v.updateCommandParameter(false),qPrintable(QString(label)+": "+v.commandSession_->geometryError));
            QString oracleError;const auto expected=MoveFaceGeometry::move(original,top,distance*factor,oracleError);
            check(!expected.IsNull() && BRepReconstruction::equivalent(v.commandSession_->candidate,expected),label);
            check(BRepCheck_Analyzer(v.commandSession_->candidate).IsValid(),"invalid Move Face result");
            v.confirmCommandSession();check(v.undoStack_.size()==history+1 && BRepReconstruction::equivalent(v.boxes_[0].shape,expected),"Move Face direction commit");
            check(v.partId(0)=="target" && v.boxes_[0].name=="Target","Move Face changed Part identity");
            v.undoPull();check(v.boxes_[0].shape.IsSame(original),"Move Face direction Undo");
            v.redoPull();check(BRepReconstruction::equivalent(v.boxes_[0].shape,expected),"Move Face direction Redo");
            v.undoPull();
        };
        expectMove(MDS::FaceNormal,{},false,10,1,"Face normal default");
        expectMove(MDS::Reference,{*edgeTarget},false,10,edgeDirection.Z(),"Edge direction");
        expectMove(MDS::Reference,{{References::Kind::Axis,"ax"}},false,10,1/std::sqrt(2.),"Axis direction");
        expectMove(MDS::Reference,{{References::Kind::Axis,"ax"}},true,10,-1/std::sqrt(2.),"reversed Axis direction");
        expectMove(MDS::Reference,{{References::Kind::SketchLine,"seg"}},false,12.5,0.8,"Sketch line direction");
        References::Target center{References::Kind::SketchPoint,"circ"};center.point={"circ",{},Sketch::PointFeature::Center,0};
        const gp_Vec p2p(gp_Pnt(150,50,0),sketchWorld(v,{60,20}));
        expectMove(MDS::PointToPoint,{{References::Kind::Point,"pt"},center},false,7,p2p.Z()/p2p.Magnitude(),"Point to Point direction");
        // Real UI: the Direction combo arms picking; clicks resolve through the shared picker.
        chooseFace(v,0,top);v.moveFace();
        v.moveDirectionSource_->setCurrentIndex(1);check(v.moveDirectionPicking_,"Direction combo did not arm picking");
        click(v,pixel(v,gp_Pnt(150,0,0)));
        check(!v.moveDirectionPicking_ && v.commandSession_->directionReferences.size()==1 && v.commandSession_->directionReferences[0].kind==References::Kind::Axis,"Axis not picked from the viewport");
        v.moveDirectionSource_->setCurrentIndex(2);click(v,pixel(v,gp_Pnt(150,50,0)));click(v,pixel(v,sketchWorld(v,{60,20})));
        check(v.commandSession_->directionReferences.size()==2 && v.commandSession_->directionReferences[0].kind==References::Kind::Point
            && v.commandSession_->directionReferences[1].kind==References::Kind::SketchPoint,"Point to Point not picked from the viewport");
        v.moveDirectionReverse_->setChecked(true);check(v.commandSession_->directionReverse,"Reverse checkbox ignored");
        v.cancelCommandSession();check(v.boxes_[0].shape.IsSame(original) && v.undoStack_.empty(),"Cancel changed geometry/history");
        // 9: a direction parallel to the Face is refused atomically.
        const auto horizontal=edge(source,gp_Pnt(200,0,0),gp_Pnt(220,0,0));
        chooseFace(v,0,top);v.moveFace();v.setMoveFaceDirection(MDS::Reference,{{References::Kind::Edge,"source",horizontal}},false);
        v.dragDimension_->setText("10");check(!v.updateCommandParameter(false) && v.commandSession_->geometryError.contains("parallel") && v.commandSession_->candidate.IsNull(),"parallel direction accepted");
        QTimer::singleShot(0,[]{for(auto* w:QApplication::topLevelWidgets()) if(auto* box=qobject_cast<QMessageBox*>(w)) box->accept();});
        v.confirmCommandSession();check(v.boxes_[0].shape.IsSame(original) && v.undoStack_.empty(),"refused direction mutated geometry/history");v.cancelCommandSession();
        // 9: direction references are not certified for cylindrical walls.
        TopoDS_Face wall;for(TopExp_Explorer it(v.boxes_[2].shape,TopAbs_FACE);it.More();it.Next()) if(BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType()==GeomAbs_Cylinder) wall=TopoDS::Face(it.Current());
        const auto holed=v.boxes_[2].shape;chooseFace(v,2,wall);v.moveFace();
        check(v.commandSession_ && v.commandSession_->moveFeature!=CadViewport::MoveFaceFeature::Planar,"hole wall session missing");
        check(!v.setMoveFaceDirection(MDS::Reference,{{References::Kind::Axis,"ax"}},false) && !v.moveDirectionSource_->isVisible(),"direction reference accepted for a cylindrical wall");
        v.cancelCommandSession();check(v.boxes_[2].shape.IsSame(holed) && v.undoStack_.empty(),"cylindrical refusal mutated geometry");
        // 6: an Edge rebuilt by a later edit is stale and refused; never rebound.
        chooseFace(v,1,planar(source,gp_Dir(0,0,1)));v.moveFace();v.dragDimension_->setText("5");v.confirmCommandSession();
        check(v.undoStack_.size()==1 && !v.boxes_[1].shape.IsSame(source),"Source edit failed");
        chooseFace(v,0,top);v.moveFace();v.setMoveFaceDirection(MDS::Reference,{*edgeTarget},false);v.dragDimension_->setText("10");
        check(!v.updateCommandParameter(false) && v.commandSession_->geometryError.contains("no longer exists") && v.commandSession_->candidate.IsNull(),"stale Edge reference not refused");
        QTimer::singleShot(0,[]{for(auto* w:QApplication::topLevelWidgets()) if(auto* box=qobject_cast<QMessageBox*>(w)) box->accept();});
        v.confirmCommandSession();check(v.boxes_[0].shape.IsSame(original) && v.undoStack_.size()==1,"stale reference mutated geometry/history");
        v.cancelCommandSession();v.undoPull();check(v.boxes_[1].shape.IsSame(source),"Source edit Undo");
        std::cout<<"Move Face direction sources passed\n";
    }
    // 11: older documents (schema v2 constructions only, v3 Sketch lines) open and resolve.
    static void olderDocuments(CadViewport& v) {
        QTemporaryDir dir;QString error;
        for(int schema:{2,3}) {
            auto doc=referenceFixture();doc.schemaVersion=schema;doc.activePartId.clear();
            if(schema==2) doc.sketchEntities.clear();
            else doc.sketchEntities={doc.sketchEntities[0],doc.sketchEntities[2]};
            NativeDocumentSnapshot loaded;const auto path=dir.filePath(QStringLiteral("v%1.odc").arg(schema));
            check(NativeDocumentStorage::save(path,doc,error) && NativeDocumentStorage::load(path,loaded,error) && v.importDocumentSnapshot(loaded,error),qPrintable(error));
            References::Resolved r;
            check(v.resolveReference({References::Kind::Axis,"ax"},r,error) && r.line->Direction().IsEqual(gp_Dir(1,0,1),1.e-12),"older document Axis reference");
            check(v.lookAtWorkplane("wp") && v.view_->Camera()->Direction().IsEqual(gp_Dir(0,1,0),1.e-9),"older document Look at Workplane");
            if(schema==3) check(v.resolveReference({References::Kind::SketchLine,"seg"},r,error),"older document Sketch line reference");
            check(v.undoStack_.empty(),"Open created history");
        }
        std::cout<<"older documents passed\n";
    }
    static void references() {
        CadViewport v;v.resize(1200,900);v.show();QApplication::processEvents();QString error;
        check(v.importDocumentSnapshot(referenceFixture(),error),qPrintable(error));
        v.view_->SetProj(V3d_XposYnegZpos);v.fitAll();QApplication::processEvents();
        pickingAndSnapping(v);
        check(v.importDocumentSnapshot(referenceFixture(),error),qPrintable(error));
        v.view_->SetProj(V3d_XposYnegZpos);v.fitAll();QApplication::processEvents();
        moveFaceDirections(v);
        olderDocuments(v);
    }
    // 12: view modes never change Part visibility flags; toggling back restores exactly.
    static void viewModes() {
        MainWindow w;w.resize(1200,900);w.show();QApplication::processEvents();auto& v=*w.viewport_;QString error;
        check(v.importDocumentSnapshot(referenceFixture(),error),qPrintable(error));w.documentDirty_=false;
        v.view_->SetProj(V3d_XposYnegZpos);v.fitAll();QApplication::processEvents();
        v.setPartVisible(2,false);w.documentDirty_=false;const auto history=v.undoStack_.size();
        const auto flags=[&]{std::vector<bool> f;for(size_t i=0;i<v.boxes_.size();++i) f.push_back(v.isPartVisible(i));return f;};
        const auto before=flags();
        const gp_Pnt onTarget(50,40,60);
        const auto detectsTarget=[&] {
            const auto p=pixel(v,onTarget);v.context_->MoveTo(p.x(),p.y(),v.view_,Standard_False);
            const bool hit=v.context_->HasDetected() && v.context_->DetectedInteractive()==v.boxes_[0].presentation;
            v.context_->ClearDetected(Standard_False);return hit;
        };
        const auto rendered=[&] {
            Image_AlienPixMap image;v.view_->Redraw();check(v.view_->ToPixMap(image,v.width(),v.height()),"capture failed");
            // Compare with an empty background pixel in the same capture.
            const auto p=pixel(v,onTarget);return image.PixelColor(p.x(),p.y()).GetRGB().Distance(image.PixelColor(3,3).GetRGB())>0.05;
        };
        check(detectsTarget() && rendered(),"target not visible/pickable initially");
        action(w,"view.wireframe")->trigger();check(v.wireframe(),"Wireframe not applied");
        for(size_t i=0;i<v.boxes_.size();++i) check(v.boxes_[i].presentation->DisplayMode()==AIS_WireFrame,"Part not in wireframe");
        check(flags()==before,"Wireframe changed Part visibility");
        action(w,"view.shaded")->trigger();check(!v.wireframe(),"Shaded not applied");
        for(size_t i=0;i<v.boxes_.size();++i) check(v.boxes_[i].presentation->DisplayMode()==AIS_Shaded,"Part not shaded again");
        auto* show3D=action(w,"view.show_3d");show3D->trigger();check(!v.show3DGeometry(),"Hide 3D Geometry not applied");
        check(flags()==before,"Hide 3D Geometry changed Part visibility flags");
        check(!detectsTarget(),"hidden solid still pickable");check(!rendered(),"hidden solid still drawn");
        check(!v.pickReference(pixel(v,onTarget),{References::Kind::Face},error),"hidden solid supplied a reference");
        check(v.pickReference(pixel(v,gp_Pnt(150,0,0)),{References::Kind::Axis},error).has_value(),"construction Axis not available while solids are hidden");
        v.addBox();const size_t added=v.boxes_.size()-1;
        const auto c=v.boxes_[added].presentation;check(!c->ViewAffinity()->IsVisible(v.view_->View()->Identification()),"new Part shown while 3D geometry is hidden");
        show3D->trigger();check(v.show3DGeometry() && detectsTarget() && rendered(),"Show 3D Geometry did not restore");
        auto after=flags();after.pop_back();check(after==before && v.isPartVisible(added),"Show 3D Geometry changed Part visibility");
        check(!v.isPartVisible(2) && !v.context_->IsDisplayed(v.boxes_[2].presentation),"individually hidden Part reappeared");
        NativeDocumentSnapshot snapshot;check(v.exportDocumentSnapshot(snapshot) && !snapshot.parts[2].visible && snapshot.parts[0].visible,"view mode leaked into saved visibility");
        check(v.undoStack_.size()==history,"view modes created history"); // Add Box is not an Undo step either
        std::cout<<"view modes passed\n";
    }
    static void run() {cameraDecoupling();framePresentation();references();viewModes();}
};
void runWorkplaneReferenceSessions() {WorkplaneReferenceSessionTest::run();}
