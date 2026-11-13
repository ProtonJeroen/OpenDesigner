// v0.5.3 Workplane and reference sessions: camera decoupling, Look at
// Workplane, frame presentation during Move/Rotate previews, reference picking
// and snapping, Move Face direction sources, Undo/Redo, Save/Open, view modes.
// v0.5.4: view-level Show/Hide Workplanes (--workplane-visibility) and Move
// Face by Points (--move-by-points).
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
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <Image_AlienPixMap.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <cmath>
#include <map>
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
        // Behavioural check (no ViewAffinity() accessor before OCCT 7.7): the new
        // Part is neither drawn nor pickable while 3D geometry is hidden.
        {
            Bnd_Box bounds;BRepBndLib::Add(v.boxes_[added].shape,bounds);double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
            Bnd_Box world;world.Add(gp_Pnt(x0,y0,z0).Transformed(v.boxes_[added].orientation));world.Add(gp_Pnt(x1,y1,z1).Transformed(v.boxes_[added].orientation));
            v.view_->FitAll(world,0.5,Standard_False); // frame the (invisible) new Part
            const auto p=pixel(v,gp_Pnt((x0+x1)/2,(y0+y1)/2,z1).Transformed(v.boxes_[added].orientation));
            Image_AlienPixMap image;v.view_->Redraw();check(v.view_->ToPixMap(image,v.width(),v.height()),"capture failed");
            check(p.x()>=0 && p.y()>=0 && p.x()<int(image.SizeX()) && p.y()<int(image.SizeY()),"new Part sample outside capture");
            check(image.PixelColor(p.x(),p.y()).GetRGB().Distance(image.PixelColor(3,3).GetRGB())<0.05,"new Part drawn while 3D geometry is hidden");
            v.context_->MoveTo(p.x(),p.y(),v.view_,Standard_False);
            check(!v.context_->HasDetected(),"new Part pickable while 3D geometry is hidden");v.context_->ClearDetected(Standard_False);
        }
        show3D->trigger();v.view_->SetProj(V3d_XposYnegZpos);v.fitAll();QApplication::processEvents();
        check(v.show3DGeometry() && detectsTarget() && rendered(),"Show 3D Geometry did not restore");
        auto after=flags();after.pop_back();check(after==before && v.isPartVisible(added),"Show 3D Geometry changed Part visibility");
        check(!v.isPartVisible(2) && !v.context_->IsDisplayed(v.boxes_[2].presentation),"individually hidden Part reappeared");
        NativeDocumentSnapshot snapshot;check(v.exportDocumentSnapshot(snapshot) && !snapshot.parts[2].visible && snapshot.parts[0].visible,"view mode leaked into saved visibility");
        check(v.undoStack_.size()==history,"view modes created history"); // Add Box is not an Undo step either
        std::cout<<"view modes passed\n";
    }

    // ======================= v0.5.4 Show/Hide Workplanes =======================
    static NativeDocumentSnapshot visibilityFixture() {
        auto doc=referenceFixture();
        // Individually hidden Workplane (with a Sketch) and an individually hidden Sketch entity.
        auto hidden=workplane("wp2","WP2",gp_Ax3(gp_Pnt(0,0,-80),gp_Dir(0,0,1),gp_Dir(1,0,0)));hidden.visible=false;
        doc.constructions.push_back(hidden);
        Sketch::Entity onHidden;onHidden.id="onHidden";onHidden.name="onHidden";onHidden.workplaneId="wp2";onHidden.geometry=Sketch::Segment{{0,0},{50,0}};
        Sketch::Entity hiddenSeg;hiddenSeg.id="hiddenSeg";hiddenSeg.name="hiddenSeg";hiddenSeg.workplaneId="wp";hiddenSeg.visible=false;hiddenSeg.geometry=Sketch::Segment{{-40,5},{-10,5}};
        doc.sketchEntities.push_back(onHidden);doc.sketchEntities.push_back(hiddenSeg);
        return doc;
    }
    struct VisibilityFlags {
        std::vector<std::pair<QString,bool>> constructions,sketches;
        bool operator==(const VisibilityFlags&) const = default;
    };
    static VisibilityFlags visibilityFlags(const CadViewport& v) {
        VisibilityFlags f;
        for(const auto& o:v.constructions_) f.constructions.push_back({o.id,o.visible});
        for(const auto& e:v.sketchEntities_) f.sketches.push_back({e.id,e.visible});
        return f;
    }
    // Displayed state of every Workplane/Sketch presentation, keyed by id.
    static std::map<QString,bool> displayed(const CadViewport& v) {
        std::map<QString,bool> d;
        for(const auto& [id,p]:v.constructionPresentations_) d[id]=v.context_->IsDisplayed(p);
        for(const auto& [id,l]:v.constructionLabels_) d["label:"+id]=v.context_->IsDisplayed(l);
        for(const auto& [id,p]:v.sketchPresentations_) d["sketch:"+id]=v.context_->IsDisplayed(p);
        return d;
    }
    static Quantity_Color pixelColor(CadViewport& v,const gp_Pnt& p) {
        Image_AlienPixMap image;v.context_->ClearDetected(Standard_False);v.view_->Redraw();
        check(v.view_->ToPixMap(image,v.width(),v.height()),"capture failed");
        const auto q=pixel(v,p);check(q.x()>=0 && q.y()>=0 && q.x()<int(image.SizeX()) && q.y()<int(image.SizeY()),"sample outside capture");
        return image.PixelColor(q.x(),q.y()).GetRGB();
    }
    static Quantity_Color background(CadViewport& v) {
        Image_AlienPixMap image;v.view_->Redraw();check(v.view_->ToPixMap(image,v.width(),v.height()),"capture failed");return image.PixelColor(3,3).GetRGB();
    }
    static bool detects(CadViewport& v,const gp_Pnt& p,const Handle(AIS_InteractiveObject)& object) {
        const auto q=pixel(v,p);v.context_->MoveTo(q.x(),q.y(),v.view_,Standard_False);
        const bool hit=v.context_->HasDetected() && v.context_->DetectedInteractive()==object;
        v.context_->ClearDetected(Standard_False);return hit;
    }
    // 1-6: Show/Hide Workplanes.
    static void workplaneVisibility() {
        MainWindow w;w.resize(1200,900);w.show();QApplication::processEvents();auto& v=*w.viewport_;QString error;
        check(v.importDocumentSnapshot(visibilityFixture(),error),qPrintable(error));w.documentDirty_=false;
        v.view_->SetProj(V3d_XposYnegZpos);v.fitAll();QApplication::processEvents();
        auto* toggle=action(w,"view.show_workplanes");auto* show3D=action(w,"view.show_3d");
        check(toggle->isCheckable() && toggle->isChecked() && v.showWorkplanes(),"Show Workplanes not on by default");
        const auto flags=visibilityFlags(v);const auto history=v.undoStack_.size();
        const auto shownState=displayed(v);
        check(shownState.at("wp") && shownState.at("label:wp") && shownState.at("sketch:seg") && shownState.at("sketch:circ"),"visible Workplane/Sketch not displayed initially");
        check(!shownState.at("wp2") && !shownState.at("sketch:onHidden") && !shownState.at("sketch:hiddenSeg"),"individually hidden objects displayed initially");
        check(shownState.at("ax") && shownState.at("pt"),"Axis/Point not displayed initially");
        const gp_Pnt segMid=sketchWorld(v,{15,20}),circleCenter=sketchWorld(v,{60,20}),circlePoint=sketchWorld(v,{70,20});
        const auto outer=v.workplaneDisplayRectangle(*v.constructionObject("wp"));
        const gp_Pnt ring=sketchWorld(v,{outer.minX+5,(outer.minY+outer.maxY)/2});
        // Shown: drawn, pickable, reference/snap source.
        const auto drawnSegment=pixelColor(v,segMid),drawnRing=pixelColor(v,ring);
        check(detects(v,circlePoint,v.sketchPresentations_.at("circ")),"Sketch not pickable while shown");
        auto picked=v.pickReference(pixel(v,circleCenter),{References::Kind::SketchPoint},error);
        check(picked && picked->ownerId=="circ","Sketch center not a reference while shown");
        // 1. Hide: not drawn, not pickable, not snap or reference targets.
        v.selectConstructions({"wp","pt"});
        toggle->trigger();check(!v.showWorkplanes() && !toggle->isChecked(),"Hide Workplanes not applied");
        const auto hiddenState=displayed(v);
        check(!hiddenState.at("wp") && !hiddenState.at("label:wp") && !hiddenState.at("sketch:seg") && !hiddenState.at("sketch:circ"),"Workplane/Sketch still displayed while hidden");
        check(!hiddenState.at("wp2") && !hiddenState.at("sketch:onHidden"),"individually hidden Workplane displayed while hidden");
        check(v.selectedConstructionIds_==QStringList{"pt"},"hidden Workplane stayed selected / Point selection lost");
        check(pixelColor(v,segMid).Distance(drawnSegment)>0.05 || pixelColor(v,ring).Distance(drawnRing)>0.05,"hidden Workplane/Sketch still rendered");
        check(pixelColor(v,ring).Distance(drawnRing)>0.05,"hidden Workplane ring still rendered");
        check(!detects(v,circlePoint,v.sketchPresentations_.at("circ")) && !detects(v,ring,v.constructionPresentations_.at("wp")),"hidden Workplane/Sketch pickable");
        for(const auto& p:{segMid,circleCenter,ring}) {
            const auto q=pixel(v,p);v.context_->MoveTo(q.x(),q.y(),v.view_,Standard_False);
            if(v.context_->HasDetected()) {
                const auto o=v.context_->DetectedInteractive();
                for(const auto& [id,sp]:v.sketchPresentations_) check(sp!=o,"Sketch detected while Workplanes hidden");
                check(o!=v.constructionPresentations_.at("wp"),"Workplane detected while hidden");
            }
            v.context_->ClearDetected(Standard_False);
        }
        check(!v.pickReference(pixel(v,circleCenter),{References::Kind::SketchPoint},error),"hidden Sketch point supplied a picked reference");
        check(!v.pickReference(pixel(v,segMid),{References::Kind::SketchLine},error),"hidden Sketch line supplied a picked reference");
        check(!v.pickReference(pixel(v,ring),{References::Kind::Workplane},error),"hidden Workplane supplied a picked reference");
        References::Resolved r;References::Target center{References::Kind::SketchPoint,"circ"};center.point={"circ",{},Sketch::PointFeature::Center,0};
        check(!v.resolveReference(center,r,error) && error.contains("hidden"),"hidden Sketch point resolved / wrong diagnostic");
        check(!v.resolveReference({References::Kind::Workplane,"wp"},r,error) && error.contains("Hidden"),"hidden Workplane resolved / wrong diagnostic");
        // Construction role picking (Point from Sketch center) uses the same snap targets: refused.
        v.startConstruction(Construction::Method::PointVertex);check(!v.showWorkplanes(),"Point construction showed Workplanes");
        v.chooseConstructionRole(0);v.captureConstructionReference(pixel(v,circleCenter));
        check(!v.commandSession_ || !v.commandSession_->valid,"hidden Sketch center accepted as construction operand");
        v.cancelCommandSession();
        // A construction preview (Point XYZ) redisplays constructions: hidden Workplanes stay hidden.
        v.startConstruction(Construction::Method::PointXYZ);check(v.commandSession_ && v.commandSession_->construction,"Point XYZ session missing");
        v.previewConstruction();
        check(!displayed(v).at("wp") && !displayed(v).at("label:wp") && !displayed(v).at("sketch:seg") && displayed(v).at("ax"),"construction preview redisplayed hidden Workplanes");
        v.cancelCommandSession();check(!displayed(v).at("wp") && !displayed(v).at("sketch:seg"),"construction Cancel redisplayed hidden Workplanes");
        // 6. Axes and Points stay visible and available.
        check(hiddenState.at("ax") && hiddenState.at("pt") && hiddenState.at("label:ax"),"Axis/Point hidden with Workplanes");
        check(v.pickReference(pixel(v,gp_Pnt(150,0,0)),{References::Kind::Axis},error).has_value(),"Axis not pickable while Workplanes hidden");
        check(v.pickReference(pixel(v,gp_Pnt(150,50,0)),{References::Kind::Point},error).has_value(),"Point not pickable while Workplanes hidden");
        check(v.resolveReference({References::Kind::Axis,"ax"},r,error) && v.resolveReference({References::Kind::Point,"pt"},r,error),"Axis/Point refused while Workplanes hidden");
        check(v.isPartVisible(0) && v.context_->IsDisplayed(v.boxes_[0].presentation),"solids hidden with Workplanes");
        // 3. Flags unchanged, nothing saved, no Undo step, document not dirty.
        check(visibilityFlags(v)==flags,"Hide Workplanes changed visibility flags");
        NativeDocumentSnapshot snapshot;check(v.exportDocumentSnapshot(snapshot),"export failed");
        for(const auto& o:snapshot.constructions) check(o.visible==(o.id!="wp2"),"view setting leaked into saved Workplane visibility");
        for(const auto& e:snapshot.sketchEntities) check(e.visible==(e.id!="hiddenSeg"),"view setting leaked into saved Sketch visibility");
        check(v.undoStack_.size()==history && !w.documentDirty_,"Hide Workplanes created history or dirtied the document");
        // New constructions/Sketches added while hidden follow the view setting.
        check(v.importDocumentSnapshot(snapshot,error),qPrintable(error));w.documentDirty_=false;check(!v.showWorkplanes() && !displayed(v).at("wp") && !displayed(v).at("sketch:seg"),"Open did not keep the hidden view setting");
        // 2. Show restores exactly the previous state, including individually hidden objects.
        toggle->trigger();check(v.showWorkplanes() && toggle->isChecked(),"Show Workplanes not applied");
        check(displayed(v)==shownState,"Show Workplanes did not restore the exact previous display state");
        check(visibilityFlags(v)==flags,"Show Workplanes changed visibility flags");
        check(pixelColor(v,segMid).Distance(drawnSegment)<0.05 && pixelColor(v,ring).Distance(drawnRing)<0.05,"Show Workplanes did not redraw the same frame/Sketch");
        check(detects(v,circlePoint,v.sketchPresentations_.at("circ")),"Sketch not pickable after Show Workplanes");
        check(v.pickReference(pixel(v,circleCenter),{References::Kind::SketchPoint},error).has_value(),"Sketch center not a reference after Show Workplanes");
        check(v.undoStack_.size()==history && !w.documentDirty_,"Show Workplanes created history or dirtied the document");
        std::cout<<"show/hide Workplanes passed\n";
        // 4. Combinations with Show/Hide 3D Geometry and Wireframe/Shaded.
        const gp_Pnt onTarget(50,40,60);
        for(int wire=0;wire<2;++wire) for(int solids=0;solids<2;++solids) for(int planes=0;planes<2;++planes) {
            action(w,wire?"view.wireframe":"view.shaded")->trigger();
            if(v.show3DGeometry()!=bool(solids)) show3D->trigger();
            if(v.showWorkplanes()!=bool(planes)) toggle->trigger();
            check(v.wireframe()==bool(wire) && v.show3DGeometry()==bool(solids) && v.showWorkplanes()==bool(planes),"view mode combination not applied");
            check(toggle->isChecked()==bool(planes) && show3D->isChecked()==bool(solids),"view actions out of sync");
            const auto d=displayed(v);
            check(d.at("wp")==bool(planes) && d.at("sketch:seg")==bool(planes) && d.at("ax") && d.at("pt") && !d.at("wp2") && !d.at("sketch:hiddenSeg"),"combination changed Workplane/Sketch/Axis display");
            for(const auto& part:v.boxes_) check(part.presentation->DisplayMode()==(wire?AIS_WireFrame:AIS_Shaded),"combination changed Part display mode");
            check(detects(v,onTarget,v.boxes_[0].presentation)==bool(solids),"combination changed solid picking");
            check(v.pickReference(pixel(v,circleCenter),{References::Kind::SketchPoint},error).has_value()==bool(planes),"combination changed Sketch reference availability");
            if(!solids) {
                // Only Workplane geometry can render here: hidden means background.
                const auto bg=background(v);
                check((pixelColor(v,ring).Distance(bg)>0.05)==bool(planes),"Workplane ring rendering wrong with 3D geometry hidden");
            }
            check(visibilityFlags(v)==flags,"combination changed visibility flags");
        }
        action(w,"view.shaded")->trigger();if(!v.show3DGeometry()) show3D->trigger();if(!v.showWorkplanes()) toggle->trigger();
        check(v.undoStack_.size()==history && !w.documentDirty_,"view combinations created history");
        std::cout<<"view mode combinations passed\n";
        // 5. Activating a Workplane, Edit Sketch and new/edited Workplanes show them again.
        toggle->trigger();check(!v.showWorkplanes(),"hide");
        check(v.activateSketchWorkplane("wp") && v.showWorkplanes() && toggle->isChecked(),"activating a Workplane did not show Workplanes");
        check(displayed(v).at("wp") && displayed(v).at("sketch:seg"),"activated Workplane not displayed");
        toggle->trigger();check(!v.showWorkplanes() && v.activeSketchWorkplaneId_.isEmpty(),"hiding did not end Sketch editing");
        v.selectConstructions({"wp"});action(w,"sketch.edit")->trigger();
        check(v.activeSketchWorkplaneId_=="wp" && v.showWorkplanes() && toggle->isChecked(),"Edit Sketch did not show Workplanes");
        v.endSketchEditing();toggle->trigger();
        auto* combo=w.findChild<QComboBox*>("sketchWorkplane");combo->setCurrentIndex(combo->findData("wp"));
        check(v.activeSketchWorkplaneId_=="wp" && v.showWorkplanes(),"Workplane combo activation did not show Workplanes");
        v.endSketchEditing();toggle->trigger();
        chooseFace(v,0,planar(v.boxes_[0].shape,gp_Dir(0,0,1)));v.startConstruction(Construction::Method::PlaneFace);
        check(v.showWorkplanes() && toggle->isChecked() && v.commandSession_ && v.commandSession_->kind==CadViewport::CommandSession::Kind::Construction,"new Workplane did not show Workplanes");
        v.cancelCommandSession();toggle->trigger();
        v.startConstruction(Construction::Method::PlaneMove,"wp");check(v.showWorkplanes(),"Move Workplane did not show Workplanes");
        v.cancelCommandSession();
        check(visibilityFlags(v)==flags && v.undoStack_.size()==history && !w.documentDirty_,"auto-show changed flags or history");
        std::cout<<"auto-show on Workplane activation/creation passed\n";
    }
    // ======================= v0.5.4 Move Face by Points =======================
    static TopoDS_Vertex vertex(const TopoDS_Shape& shape,const gp_Pnt& p) {
        for(TopExp_Explorer it(shape,TopAbs_VERTEX);it.More();it.Next()) if(BRep_Tool::Pnt(TopoDS::Vertex(it.Current())).Distance(p)<1.e-9) return TopoDS::Vertex(it.Current());
        throw std::runtime_error("vertex missing");
    }
    static TopoDS_Edge circularEdge(const TopoDS_Shape& shape,double z) {
        for(TopExp_Explorer it(shape,TopAbs_EDGE);it.More();it.Next()) {
            BRepAdaptor_Curve c(TopoDS::Edge(it.Current()));
            if(c.GetType()==GeomAbs_Circle && std::abs(c.Circle().Location().Z()-z)<1.e-9) return TopoDS::Edge(it.Current());
        }
        throw std::runtime_error("circular edge missing");
    }
    static void acceptPopups() {QTimer::singleShot(0,[]{for(auto* w:QApplication::topLevelWidgets()) if(auto* box=qobject_cast<QMessageBox*>(w)) box->accept();});}
    static void moveByPoints() {
        using MDS=CadViewport::MoveDirectionSource;using References::Kind;
        CadViewport v;v.resize(1200,900);v.show();QApplication::processEvents();QString error;
        auto doc=referenceFixture();
        // Part with a certified Fillet on a top edge (11).
        NativePartState filleted;filleted.id="filleted";filleted.name="Filleted";
        {
            const auto sharp=BRepPrimAPI_MakeBox(gp_Pnt(0,-300,0),100,80,60).Shape();EdgeBlendFeatures::State state;
            check(EdgeBlendFeatures::create(sharp,{},edge(sharp,gp_Pnt(0,-300,60),gp_Pnt(100,-300,60)),EdgeBlendFeatures::Kind::Fillet,5,state,filleted.shape,error),qPrintable(error));
            filleted.edgeBlendRecipe=EdgeBlendFeatures::encode(state);
        }
        doc.parts.push_back(filleted);
        check(v.importDocumentSnapshot(doc,error),qPrintable(error));
        v.view_->SetProj(V3d_XposYnegZpos);v.fitAll();QApplication::processEvents();
        const auto original=v.boxes_[0].shape;const auto top=planar(original,gp_Dir(0,0,1));
        const auto sourceShape=v.boxes_[1].shape,holed=v.boxes_[2].shape;
        const References::Target topCorner{Kind::Vertex,"target",vertex(original,gp_Pnt(100,80,60))};
        const References::Target topCorner2{Kind::Vertex,"target",vertex(original,gp_Pnt(0,0,60))};
        const References::Target sourceCorner{Kind::Vertex,"source",vertex(sourceShape,gp_Pnt(220,20,40))};
        const References::Target point{Kind::Point,"pt"};
        const auto sketchPoint=[](const QString& id,Sketch::PointFeature feature,const QString& partner={},int index=0) {
            References::Target t{Kind::SketchPoint,id};t.point={id,partner,feature,index};return t;
        };
        const auto center=sketchPoint("circ",Sketch::PointFeature::Center);
        const auto segEnd=sketchPoint("seg",Sketch::PointFeature::End);
        // seg (0,0)-(30,40) crosses cross (0,40)-(40,0) at (120/7,160/7).
        Sketch::PointReference crossing;
        for(const auto& t:Sketch::snapTargets(v.sketchEntities_,"wp",{},{},true))
            if(t.kind==Sketch::SnapKind::Intersection && t.reference) crossing=*t.reference;
        check(!crossing.entity.isEmpty(),"intersection reference missing");
        References::Target intersection{Kind::SketchPoint,crossing.entity};intersection.point=crossing;
        const References::Target holeCenter{Kind::Edge,"hole",circularEdge(holed,30)};
        const auto world=[&](const References::Target& t){References::Resolved r;QString e;check(v.resolveReference(t,r,e) && r.point,qPrintable(e));return *r.point;};
        check(world(intersection).Distance(sketchWorld(v,{120./7,160./7}))<1.e-9 && world(holeCenter).Distance(gp_Pnt(30,230,30))<1.e-9,"point source oracle");
        // Runs By Points on the target top and compares with the certified normal-offset oracle.
        const auto expectByPoints=[&](const References::Target& a,const References::Target& b,const char* label,bool commit) {
            const auto history=v.undoStack_.size();chooseFace(v,0,top);v.moveFace();
            check(v.commandSession_ && v.commandSession_->kind==CadViewport::CommandSession::Kind::MoveFace,"Move Face session missing");
            check(v.setMoveFaceDirection(MDS::ByPoints,{a,b},false),qPrintable(QString(label)+": "+v.commandSession_->geometryError));
            const gp_Vec ab(world(a),world(b));const double offset=ab.Dot(gp_Vec(0,0,1));
            check(std::abs(v.commandSession_->value-ab.Magnitude())<1.e-9,"session value is not |AB|");
            check(v.dragDimension_->isReadOnly() && !v.moveDirectionReverse_->isVisibleTo(v.commandPanel_) && v.moveDirectionRestart_->isVisibleTo(v.commandPanel_),"By Points panel controls");
            check(v.moveDirectionLabel_->text().contains(QLocale().toString(ab.Magnitude(),'f',6)) && v.moveDirectionLabel_->text().contains(QLocale().toString(offset,'f',6)),"panel does not show |AB| and the normal offset");
            QString oracleError;const auto expected=MoveFaceGeometry::move(original,top,offset,oracleError);
            check(!expected.IsNull() && BRepReconstruction::equivalent(v.commandSession_->candidate,expected),label);
            check(BRepCheck_Analyzer(v.commandSession_->candidate).IsValid(),"invalid By Points result");
            check(v.boxes_[0].shape.IsSame(original),"preview mutated the source");
            if(!commit) {v.cancelCommandSession();check(v.boxes_[0].shape.IsSame(original) && v.undoStack_.size()==history,"Cancel changed geometry/history");return expected;}
            v.confirmCommandSession();check(v.undoStack_.size()==history+1 && BRepReconstruction::equivalent(v.boxes_[0].shape,expected),"By Points commit is not one Undo step");
            check(v.partId(0)=="target" && v.boxes_[0].name=="Target","By Points changed Part identity");
            return expected;
        };
        // 7. A on the Face: the Face lands in the plane through B (z = 20).
        auto moved=expectByPoints(topCorner,center,"A on Face, B Sketch center",true);
        {Bnd_Box b;BRepBndLib::Add(v.boxes_[0].shape,b,false);double x0,y0,z0,x1,y1,z1;b.Get(x0,y0,z0,x1,y1,z1);
         check(std::abs(x1-x0-100)<1.e-6 && std::abs(y1-y0-80)<1.e-6 && std::abs(z0)<1.e-6 && std::abs(z1-world(center).Z())<1.e-6,"Face did not land in the plane through B");}
        // 12. Undo/Redo and Save/Open of the result.
        v.undoPull();check(v.boxes_[0].shape.IsSame(original),"By Points Undo");
        v.redoPull();check(BRepReconstruction::equivalent(v.boxes_[0].shape,moved),"By Points Redo");
        {
            NativeDocumentSnapshot saved,reopened;QTemporaryDir dir;
            check(v.exportDocumentSnapshot(saved) && NativeDocumentStorage::save(dir.filePath("bypoints.odc"),saved,error)
                && NativeDocumentStorage::load(dir.filePath("bypoints.odc"),reopened,error),qPrintable(error));
            CadViewport other;other.resize(600,400);other.show();QApplication::processEvents();
            check(other.importDocumentSnapshot(reopened,error),qPrintable(error));
            check(other.partId(0)=="target" && BRepReconstruction::equivalent(other.boxes_[0].shape,moved),"By Points result changed on Save/Open");
        }
        v.undoPull();check(v.boxes_[0].shape.IsSame(original) && v.undoStack_.empty(),"By Points final Undo");
        // 8. Oblique vector: only (B - A) . n moves the Face.
        expectByPoints(point,sourceCorner,"oblique Point -> Vertex",false);
        // 9. Every supported point source as A and as B.
        const std::vector<std::pair<const char*,References::Target>> sources{
            {"Vertex",sourceCorner},{"Point",point},{"Sketch end point",segEnd},{"Sketch center",center},{"Sketch intersection",intersection},{"circular Edge center",holeCenter}};
        const std::vector<References::Target> partners{topCorner,point,center,sourceCorner};
        for(const auto& [name,s]:sources) for(int role=0;role<2;++role) {
            bool done=false;
            for(const auto& partner:partners) {
                const auto a=role==0?s:partner,b=role==0?partner:s;
                const double offset=world(b).Z()-world(a).Z();
                if(std::abs(offset)<1 || offset<-50) continue; // keep the target a valid, non-degenerate solid
                expectByPoints(a,b,qPrintable(QString("%1 as %2").arg(name,role==0?"A":"B")),false);done=true;break;
            }
            check(done,"no partner for a point source");
        }
        // UI: Direction combo arms picking; clicks pick A then B; another click replaces B; restart picks A again.
        chooseFace(v,0,top);v.moveFace();
        v.moveDirectionSource_->setCurrentIndex(int(MDS::ByPoints));
        check(v.moveDirectionPicking_ && v.commandSession_->directionSource==MDS::ByPoints && v.dragDimension_->isReadOnly(),"By Points combo did not arm picking");
        check(!v.commandSession_->valid && v.commandSession_->candidate.IsNull(),"preview before the second pick");
        click(v,pixel(v,gp_Pnt(150,50,0)));
        check(v.commandSession_->directionReferences.size()==1 && v.commandSession_->directionReferences[0].kind==Kind::Point && v.commandSession_->candidate.IsNull(),"start point A not picked");
        click(v,pixel(v,sketchWorld(v,{60,20})));
        check(v.commandSession_->directionReferences.size()==2 && v.commandSession_->directionReferences[1].kind==Kind::SketchPoint && v.commandSession_->valid,"end point B not picked / no preview");
        check(v.moveDirectionPicking_,"picking not armed for replacing B");
        click(v,pixel(v,gp_Pnt(220,20,40)));
        check(v.commandSession_->directionReferences.size()==2 && v.commandSession_->directionReferences[0].kind==Kind::Point
            && v.commandSession_->directionReferences[1].kind==Kind::Vertex && v.commandSession_->valid,"another pick did not replace B");
        QString oracleError;check(BRepReconstruction::equivalent(v.commandSession_->candidate,MoveFaceGeometry::move(original,top,40,oracleError)),"picked By Points result");
        // Arrow dragging is not a distance input in By Points.
        check(!v.beginMoveFaceDrag(pixel(v,v.commandSession_->pivot)),"By Points accepted an arrow drag");
        v.moveDirectionRestart_->click();
        check(v.commandSession_->directionReferences.empty() && v.moveDirectionPicking_ && !v.commandSession_->valid,"Pick start point again did not reset A");
        click(v,pixel(v,gp_Pnt(150,0,0))); // an Axis is not a point source
        check(v.commandSession_->directionReferences.empty(),"Axis accepted as a point");
        v.cancelCommandSession();check(v.boxes_[0].shape.IsSame(original) && v.undoStack_.empty(),"UI Cancel changed geometry/history");
        // 10. Refusals are atomic: no candidate, no change, no history.
        const auto refuse=[&](std::vector<References::Target> refs,const char* expected,const char* label) {
            const auto history=v.undoStack_.size();chooseFace(v,0,top);v.moveFace();v.setMoveFaceDirection(MDS::ByPoints,refs,false);
            check(!v.updateCommandParameter(false) && v.commandSession_->candidate.IsNull() && !v.commandSession_->valid,label);
            check(v.commandSession_->geometryError.contains(expected),qPrintable(QString(label)+": "+v.commandSession_->geometryError));
            acceptPopups();v.confirmCommandSession();
            check(v.boxes_[0].shape.IsSame(original) && v.undoStack_.size()==history,"refused By Points mutated geometry/history");
            v.cancelCommandSession();
        };
        refuse({sourceCorner,sourceCorner},"coincide","coincident points accepted");
        refuse({topCorner,topCorner2},"parallel","parallel vector accepted");
        refuse({{Kind::Edge,"source",edge(sourceShape,gp_Pnt(200,0,0),gp_Pnt(220,0,0))},point},"circular","straight Edge accepted as a point");
        refuse({point},"end point B","single point accepted");
        // Hidden: individually hidden Point and Sketch points while Workplanes are hidden.
        {auto objects=v.constructions_;for(auto& o:objects) if(o.id=="pt") o.visible=false;v.commitConstructions(objects);}
        refuse({point,sourceCorner},"Hidden","hidden Point accepted");
        v.undoPull();check(v.undoStack_.empty(),"visibility Undo");
        v.setShowWorkplanes(false);
        refuse({center,sourceCorner},"hidden","Sketch point on hidden Workplanes accepted");
        v.setShowWorkplanes(true);
        // Stale: a Vertex rebuilt by a later edit of its Part; deleted: a deleted Point.
        chooseFace(v,1,planar(sourceShape,gp_Dir(0,0,1)));v.moveFace();v.dragDimension_->setText("5");v.confirmCommandSession();
        check(v.undoStack_.size()==1 && !v.boxes_[1].shape.IsSame(sourceShape),"Source edit failed");
        {
            chooseFace(v,0,top);v.moveFace();v.setMoveFaceDirection(MDS::ByPoints,{point,sourceCorner},false);
            check(!v.updateCommandParameter(false) && v.commandSession_->geometryError.contains("no longer exists") && v.commandSession_->candidate.IsNull(),"stale Vertex accepted");
            acceptPopups();v.confirmCommandSession();check(v.boxes_[0].shape.IsSame(original) && v.undoStack_.size()==1,"stale reference mutated geometry/history");
            v.cancelCommandSession();
        }
        v.undoPull();check(v.boxes_[1].shape.IsSame(sourceShape) && v.undoStack_.empty(),"Source edit Undo");
        v.deleteConstructions({"pt"});
        {
            chooseFace(v,0,top);v.moveFace();v.setMoveFaceDirection(MDS::ByPoints,{point,sourceCorner},false);
            check(!v.updateCommandParameter(false) && v.commandSession_->geometryError.contains("no longer exists"),"deleted Point accepted");
            v.cancelCommandSession();check(v.boxes_[0].shape.IsSame(original),"deleted reference mutated geometry");
        }
        v.undoPull();check(v.constructionObject("pt") && v.undoStack_.empty(),"Point delete Undo");
        // Unsupported Face type: a cylindrical hole wall keeps its lateral controls.
        TopoDS_Face wall;for(TopExp_Explorer it(holed,TopAbs_FACE);it.More();it.Next()) if(BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType()==GeomAbs_Cylinder) wall=TopoDS::Face(it.Current());
        chooseFace(v,2,wall);v.moveFace();
        check(v.commandSession_ && v.commandSession_->moveFeature!=CadViewport::MoveFaceFeature::Planar,"hole wall session missing");
        check(!v.setMoveFaceDirection(MDS::ByPoints,{point,sourceCorner},false) && !v.moveDirectionSource_->isVisible() && v.cylinderMoveDirection_->isVisible(),"By Points accepted for a cylindrical wall");
        v.cancelCommandSession();check(v.boxes_[2].shape.IsSame(holed) && v.undoStack_.empty(),"cylindrical refusal mutated geometry");
        std::cout<<"Move Face by Points sources, refusals, Undo/Redo and Save/Open passed\n";
        // 11. Keep ON/OFF on a Face adjacent to a certified Fillet is unchanged.
        const size_t f=3;const auto filletSource=v.boxes_[f].shape;auto filletRecipe=v.boxes_[f].blends;
        check(EdgeBlendFeatures::resolve(filletSource,filletRecipe,error) && filletRecipe.features.size()==1,qPrintable(error));
        TopoDS_Face filletTop;double highest=-1e30;
        for(TopExp_Explorer it(filletSource,TopAbs_FACE);it.More();it.Next()) {
            BRepAdaptor_Surface a(TopoDS::Face(it.Current()));
            if(a.GetType()==GeomAbs_Plane && a.Plane().Axis().Direction().IsParallel(gp_Dir(0,0,1),1.e-9) && a.Plane().Location().Z()>highest) {highest=a.Plane().Location().Z();filletTop=TopoDS::Face(it.Current());}
        }
        check(std::abs(highest-60)<1.e-9,"filleted top missing");
        const References::Target filletCorner{Kind::Vertex,"filleted",vertex(filletSource,gp_Pnt(100,-220,60))};
        for(const bool keep:{true,false}) {
            chooseFace(v,f,filletTop);v.moveFace();
            check(v.commandSession_ && v.commandSession_->blendSupport && v.keepBlends_->isChecked(),"filleted Move Face session / Keep default");
            v.keepBlends_->setChecked(keep);
            check(v.setMoveFaceDirection(MDS::ByPoints,{filletCorner,center},false),qPrintable(v.commandSession_->geometryError));
            const double offset=world(center).Z()-60;
            TopoDS_Shape expected;EdgeBlendFeatures::State expectedBlends;QString e;
            check(EdgeBlendFeatures::move(filletSource,filletRecipe,filletTop,offset,keep,expectedBlends,expected,e),qPrintable(e));
            check(BRepReconstruction::equivalent(v.commandSession_->candidate,expected),"Keep ON/OFF By Points differs from the certified blend move");
            check(v.commandSession_->changes.front().faceChange->after.blends.features.size()==(keep?1u:0u),"Keep ON/OFF blend metadata");
            v.confirmCommandSession();check(v.undoStack_.size()==1 && BRepReconstruction::equivalent(v.boxes_[f].shape,expected),"Keep By Points commit");
            v.undoPull();check(v.boxes_[f].shape.IsSame(filletSource) && v.boxes_[f].blends.features.size()==1,"Keep By Points Undo");
            v.redoPull();check(BRepReconstruction::equivalent(v.boxes_[f].shape,expected),"Keep By Points Redo");
            v.undoPull();v.redoStack_.clear();v.updateHistoryActions();
        }
        std::cout<<"Move Face by Points Keep ON/OFF passed\n";
    }
    static void run() {cameraDecoupling();framePresentation();references();viewModes();}
};
void runWorkplaneReferenceSessions() {WorkplaneReferenceSessionTest::run();}
void runWorkplaneVisibilitySessions() {WorkplaneReferenceSessionTest::workplaneVisibility();}
void runMoveFaceByPointsSessions() {WorkplaneReferenceSessionTest::moveByPoints();}
