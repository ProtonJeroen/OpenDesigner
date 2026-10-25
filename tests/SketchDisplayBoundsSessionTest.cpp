#include "CadViewport.h"
#include "MainWindow.h"
#include <QApplication>
#include <QTemporaryDir>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopExp_Explorer.hxx>
#include <stdexcept>

namespace {
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
}
struct SketchDisplayBoundsSessionTest {
    static void frame(CadViewport& v,const Construction::Object& wp,const Sketch::DisplayRectangle& r) {
        const auto presentation=v.constructionPresentations_.at(wp.id);
        TopExp_Explorer faces(presentation->Shape(),TopAbs_FACE);check(faces.More(),"Workplane face missing");
        double u0,u1,v0,v1;BRepTools::UVBounds(TopoDS::Face(faces.Current()),u0,u1,v0,v1);
        check(std::abs(u0-r.minX)<1.e-8 && std::abs(u1-r.maxX)<1.e-8
            && std::abs(v0-r.minY)<1.e-8 && std::abs(v1-r.maxY)<1.e-8,"displayed frame is not current rectangular bounds");
        const auto label=v.constructionLabels_.at(wp.id)->Position();
        double distance=1.e100;for(const auto& corner:Sketch::displayCorners(r,wp.frame)) distance=std::min(distance,corner.Distance(label));
        check(distance<2*v.view_->Convert(10),"label did not follow rectangle corner");
    }
    static void run() {
        MainWindow w;w.show();QApplication::processEvents();auto& v=*w.viewport_;QString error;
        v.startConstruction(Construction::Method::PlaneXYZ);v.confirmCommandSession();
        auto wp=v.constructions_.front();
        auto other=wp;other.id="other";other.name="WP2";v.commitConstructions({wp,other});
        frame(v,wp,Sketch::displayRectangle(wp,{}));
        Sketch::Entity axis;axis.workplaneId=wp.id;axis.role=Sketch::Role::Construction;axis.geometry=Sketch::InfiniteLine{{1.e6,3000},{1,0}};
        const auto originalWP=wp;
        check(v.previewSketchEntities({axis},error),qPrintable(error));v.confirmCommandSession();
        check(!TopExp_Explorer(v.sketchPresentations_.at(axis.id)->Shape(),TopAbs_EDGE).More(),"outside line not clipped away");
        Sketch::Entity segment;segment.workplaneId=wp.id;segment.geometry=Sketch::Segment{{1000,3000},{1100,3020}};
        const auto expanded=Sketch::displayRectangle(wp,{axis,segment});
        const auto history=v.undoStack_.size();w.documentDirty_=false;
        check(v.previewSketchEntities({axis,segment},error),qPrintable(error));
        frame(v,wp,expanded);frame(v,other,Sketch::displayRectangle(other,{}));
        check(v.undoStack_.size()==history && !w.documentDirty_ && v.sketchEntities_==std::vector{axis},"bounds preview changed document/history");
        v.cancelCommandSession();frame(v,wp,Sketch::displayRectangle(wp,{axis}));
        check(!w.documentDirty_,"bounds Cancel dirtied document");
        check(v.previewSketchEntities({axis,segment},error),qPrintable(error));v.confirmCommandSession();frame(v,wp,expanded);
        const auto line=v.sketchPresentations_.at(axis.id)->Shape();
        check(BRep_Tool::Pnt(TopExp::FirstVertex(TopoDS::Edge(line))).IsEqual(Sketch::worldPoint({990,3000},wp.frame),1.e-8),"line origin-based clipping survived resize");
        v.selectConstructions({wp.id});const auto selected=v.constructionPresentations_.at(wp.id);
        check(v.context_->NbSelected()>0,"Workplane selection setup failed");
        v.workplaneDisplayRectangles_.clear();v.updateConstructionLabels();
        bool selectedOwner=false;
        for(v.context_->InitSelected();v.context_->MoreSelected();v.context_->NextSelected()) if(v.context_->SelectedInteractive()==selected) selectedOwner=true;
        check(v.constructionPresentations_.at(wp.id)==selected && selectedOwner && v.constructionSelection()==QStringList{wp.id},"derived resize replaced selected Workplane");
        v.selectSketchEntities({segment.id});
        check(v.previewSketchEntities({axis},error),qPrintable(error));frame(v,wp,Sketch::displayRectangle(wp,{axis}));
        v.cancelCommandSession();frame(v,wp,expanded);
        check(v.sketchSelection()==QStringList{segment.id},"Cancel lost prior sketch selection");
        check(v.deleteSketchEntities({segment.id},error),qPrintable(error));frame(v,wp,Sketch::displayRectangle(wp,{axis}));
        v.undoPull();frame(v,wp,expanded);check(v.sketchSelection().empty(),"Undo kept selection");
        v.redoPull();frame(v,wp,Sketch::displayRectangle(wp,{axis}));v.undoPull();
        // Unconfirmed drawing must resize too, not only complete transactions.
        v.activateSketchWorkplane(wp.id,false);v.startSketchDrawing(Sketch::DrawTool::Circle);
        auto& d=*v.commandSession_->drawing;d.points={{-3000,-4000}};d.cursor=Sketch::Point{-2800,-4000};v.refreshSketchDrawing();
        auto shown=v.sketchEntities_;shown.push_back(d.candidate);frame(v,wp,Sketch::displayRectangle(wp,shown));
        v.cancelCommandSession();frame(v,wp,expanded);
        auto rotated=wp;rotated.frame=gp_Ax3(gp_Pnt(70,20,-30),gp_Dir(1,2,3),gp_Dir(2,-1,0));
        check(v.updateConstruction(rotated),"Workplane move/rotation failed");frame(v,rotated,expanded);
        check(v.sketchEntities_==std::vector({axis,segment}),"rotation rewrote sketch coordinates");
        check(Construction::equivalent(originalWP,*v.constructionObject(wp.id))==false,"rotation not applied");
        v.undoPull();check(Construction::equivalent(originalWP,*v.constructionObject(wp.id)),"auto bounds leaked into document Workplane values");frame(v,wp,expanded);
        NativeDocumentSnapshot snapshot;check(v.exportDocumentSnapshot(snapshot),"snapshot export failed");
        QTemporaryDir dir;check(dir.isValid(),"temporary directory failed");const auto path=dir.filePath("bounds.odc");
        check(NativeDocumentStorage::save(path,snapshot,error),qPrintable(error));NativeDocumentSnapshot loaded;
        check(NativeDocumentStorage::load(path,loaded,error),qPrintable(error));
        check(v.importDocumentSnapshot(loaded,error),qPrintable(error));frame(v,wp,expanded);
        check(v.sketchEntities_==snapshot.sketchEntities && Construction::equivalent(*v.constructionObject(wp.id),originalWP),"bounds persisted or changed source values");
        w.documentDirty_=false;w.close();QApplication::processEvents();
    }
};
void runSketchDisplayBoundsSessions() {SketchDisplayBoundsSessionTest::run();}
