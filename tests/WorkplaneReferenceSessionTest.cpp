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
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
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
    static void run() {cameraDecoupling();framePresentation();}
};
void runWorkplaneReferenceSessions() {WorkplaneReferenceSessionTest::run();}
