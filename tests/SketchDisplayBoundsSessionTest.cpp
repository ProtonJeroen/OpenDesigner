#include "CadViewport.h"
#include "MainWindow.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopExp_Explorer.hxx>
#include <stdexcept>
#include <algorithm>
#include <cmath>

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
    static void clipped(CadViewport& v,const Construction::Object& wp,const Sketch::Entity& entity,
                        const Handle(AIS_Shape)& presentation,const Sketch::DisplayRectangle& r) {
        check(std::holds_alternative<Sketch::InfiniteLine>(entity.geometry),"offset lost infinite model geometry");
        check(!presentation->Shape().IsNull() && presentation->Shape().ShapeType()==TopAbs_EDGE,"offset Infinite Line has no visible preview/edge");
        const auto edge=TopoDS::Edge(presentation->Shape());
        for(const auto& vertex:{TopExp::FirstVertex(edge),TopExp::LastVertex(edge)}) {
            const gp_Vec delta(wp.frame.Location(),BRep_Tool::Pnt(vertex));
            const Sketch::Point p{delta.Dot(gp_Vec(wp.frame.XDirection())),delta.Dot(gp_Vec(wp.frame.YDirection()))};
            check(Sketch::contains(entity.geometry,p),"display endpoint left the analytic line");
            check(p.x>=r.minX-1.e-7 && p.x<=r.maxX+1.e-7 && p.y>=r.minY-1.e-7 && p.y<=r.maxY+1.e-7
                && std::min({std::abs(p.x-r.minX),std::abs(p.x-r.maxX),std::abs(p.y-r.minY),std::abs(p.y-r.maxY)})<1.e-7,
                "offset line not clipped to the outer frame edge");
        }
    }
    static void offsetCommands(MainWindow& w) {
        auto& v=*w.viewport_;QString error;const double q=std::sqrt(0.5);
        for(bool finite:{false,true}) for(const auto direction:std::vector<Sketch::Point>{{0,1},{1,0},{q,q}}) {
            NativeDocumentSnapshot snapshot;Construction::Object wp;wp.id="offset-wp";wp.name="WP1";wp.kind=Construction::Kind::Workplane;
            wp.frame=gp_Ax3(gp_Pnt(15,-30,70),gp_Dir(1,2,3),gp_Dir(2,-1,0));
            auto other=wp;other.id="other-offset-wp";other.name="WP2";snapshot.constructions={wp,other};
            Sketch::Entity source;source.workplaneId=wp.id;source.role=Sketch::Role::Construction;
            source.geometry=Sketch::InfiniteLine{{direction.x*1.e6,direction.y*1.e6},direction};snapshot.sketchEntities={source};
            if(finite) {auto circle=source;circle.id="circle";circle.geometry=Sketch::Circle{{0,0},10};snapshot.sketchEntities.push_back(circle);}
            check(v.importDocumentSnapshot(snapshot,error),qPrintable(error));check(v.activateSketchWorkplane(wp.id),"activate offset Workplane failed");
            w.documentDirty_=false;const auto base=Sketch::displayOuterRectangle(wp,snapshot.sketchEntities);
            const auto start=[&] {
                v.selectSketchEntities({source.id});v.startSketchConstruction(Sketch::ConstructionTool::OffsetLine);
                check(v.sketchConstructionPanel_,"Offset command did not start");
                v.sketchConstructionPanel_->output->setCurrentIndex(0);v.sketchConstructionPanel_->distance->setValue(200);
                check(v.commandSession_->valid,"Offset preview invalid");
                return v.commandSession_->lineConstruction->candidate;
            };
            auto offset=start();auto shown=snapshot.sketchEntities;shown.push_back(offset);
            const auto expanded=Sketch::displayOuterRectangle(wp,shown);
            check(expanded!=base,"offset preview did not enlarge the frame");frame(v,wp,expanded);frame(v,other,Sketch::displayOuterRectangle(other,{}));
            clipped(v,wp,offset,v.sketchPreviews_.at(offset.id),expanded);
            NativeDocumentSnapshot pending;check(v.exportDocumentSnapshot(pending) && pending.sketchEntities==snapshot.sketchEntities
                && v.undoStack_.empty() && !w.documentDirty_,"offset bounds preview changed saved data/history/dirty state");
            v.cancelCommandSession();frame(v,wp,base);check(!w.documentDirty_ && v.undoStack_.empty(),"offset Cancel changed document/history");
            offset=start();shown=snapshot.sketchEntities;shown.push_back(offset);v.confirmCommandSession();
            frame(v,wp,expanded);clipped(v,wp,offset,v.sketchPresentations_.at(offset.id),expanded);
            check(v.sketchEntities_==shown && v.undoStack_.size()==1,"Offset commit was not one exact SketchChange");
            const auto far=Sketch::nearestPoint(offset.geometry,{100000,200000});
            check(far && Sketch::contains(offset.geometry,*far),"line snapping was clipped with the presentation");
            v.undoPull();frame(v,wp,base);check(v.sketchEntities_==snapshot.sketchEntities && v.sketchSelection().empty(),"offset Undo failed");
            v.redoPull();frame(v,wp,expanded);check(v.sketchEntities_==shown && v.sketchSelection().empty(),"offset Redo changed geometry/selection");
            auto moved=wp;moved.frame.SetLocation(gp_Pnt(-40,80,120));moved.frame.SetDirection(gp_Dir(-1,3,2));
            check(v.updateConstruction(moved),"offset owner transform failed");frame(v,moved,expanded);
            clipped(v,moved,offset,v.sketchPresentations_.at(offset.id),expanded);
            NativeDocumentSnapshot saved,loaded;check(v.exportDocumentSnapshot(saved),"offset snapshot export failed");
            QTemporaryDir dir;check(dir.isValid(),"offset round-trip directory failed");const auto path=dir.filePath("offset.odc");
            check(NativeDocumentStorage::save(path,saved,error) && NativeDocumentStorage::load(path,loaded,error),qPrintable(error));
            check(v.importDocumentSnapshot(loaded,error),qPrintable(error));frame(v,moved,expanded);frame(v,other,Sketch::displayOuterRectangle(other,{}));
            check(v.sketchEntities_==shown && Construction::equivalent(*v.constructionObject(wp.id),moved),"offset Save/Open rewrote source/model values");
            clipped(v,moved,offset,v.sketchPresentations_.at(offset.id),expanded);
        }
    }
    static void run() {
        MainWindow w;w.show();QApplication::processEvents();auto& v=*w.viewport_;QString error;
        v.startConstruction(Construction::Method::PlaneXYZ);v.confirmCommandSession();
        auto wp=v.constructions_.front();
        auto other=wp;other.id="other";other.name="WP2";v.commitConstructions({wp,other});
        frame(v,wp,Sketch::displayOuterRectangle(wp,{}));
        Sketch::Entity axis;axis.workplaneId=wp.id;axis.role=Sketch::Role::Construction;axis.geometry=Sketch::InfiniteLine{{1.e6,3000},{1,0}};
        const auto originalWP=wp;
        check(v.previewSketchEntities({axis},error),qPrintable(error));v.confirmCommandSession();
        check(TopExp_Explorer(v.sketchPresentations_.at(axis.id)->Shape(),TopAbs_EDGE).More(),"outside line did not enlarge its infinite-only Workplane");
        Sketch::Entity segment;segment.workplaneId=wp.id;segment.geometry=Sketch::Segment{{1000,3000},{1100,3020}};
        const auto expanded=Sketch::displayOuterRectangle(wp,{axis,segment});
        const auto history=v.undoStack_.size();w.documentDirty_=false;
        check(v.previewSketchEntities({axis,segment},error),qPrintable(error));
        frame(v,wp,expanded);frame(v,other,Sketch::displayOuterRectangle(other,{}));
        check(v.undoStack_.size()==history && !w.documentDirty_ && v.sketchEntities_==std::vector{axis},"bounds preview changed document/history");
        v.cancelCommandSession();frame(v,wp,Sketch::displayOuterRectangle(wp,{axis}));
        check(!w.documentDirty_,"bounds Cancel dirtied document");
        check(v.previewSketchEntities({axis,segment},error),qPrintable(error));v.confirmCommandSession();frame(v,wp,expanded);
        const auto line=v.sketchPresentations_.at(axis.id)->Shape();
        check(BRep_Tool::Pnt(TopExp::FirstVertex(TopoDS::Edge(line))).IsEqual(Sketch::worldPoint({990-Sketch::WorkplaneBorderWidth,3000},wp.frame),1.e-8),"line origin-based clipping survived resize");
        v.selectConstructions({wp.id});const auto selected=v.constructionPresentations_.at(wp.id);
        check(v.context_->NbSelected()>0,"Workplane selection setup failed");
        v.workplaneDisplayRectangles_.clear();v.updateConstructionLabels();
        bool selectedOwner=false;
        for(v.context_->InitSelected();v.context_->MoreSelected();v.context_->NextSelected()) if(v.context_->SelectedInteractive()==selected) selectedOwner=true;
        check(v.constructionPresentations_.at(wp.id)==selected && selectedOwner && v.constructionSelection()==QStringList{wp.id},"derived resize replaced selected Workplane");
        v.selectSketchEntities({segment.id});
        check(v.previewSketchEntities({axis},error),qPrintable(error));frame(v,wp,Sketch::displayOuterRectangle(wp,{axis}));
        v.cancelCommandSession();frame(v,wp,expanded);
        check(v.sketchSelection()==QStringList{segment.id},"Cancel lost prior sketch selection");
        check(v.deleteSketchEntities({segment.id},error),qPrintable(error));frame(v,wp,Sketch::displayOuterRectangle(wp,{axis}));
        v.undoPull();frame(v,wp,expanded);check(v.sketchSelection().empty(),"Undo kept selection");
        v.redoPull();frame(v,wp,Sketch::displayOuterRectangle(wp,{axis}));v.undoPull();
        // Unconfirmed drawing must resize too, not only complete transactions.
        v.activateSketchWorkplane(wp.id);v.startSketchDrawing(Sketch::DrawTool::Circle);
        auto& d=*v.commandSession_->drawing;d.points={{-3000,-4000}};d.cursor=Sketch::Point{-2800,-4000};v.refreshSketchDrawing();
        auto shown=v.sketchEntities_;shown.push_back(d.candidate);frame(v,wp,Sketch::displayOuterRectangle(wp,shown));
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
        offsetCommands(w);
        w.documentDirty_=false;w.close();QApplication::processEvents();
    }
};
void runSketchDisplayBoundsSessions() {SketchDisplayBoundsSessionTest::run();}
