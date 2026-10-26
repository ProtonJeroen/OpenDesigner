#include "CadViewport.h"
#include "MainWindow.h"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QTemporaryDir>
#include <BRepAdaptor_Curve.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Tool.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Image_AlienPixMap.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}}
struct WorkplaneRenderingTest {
    static NativeDocumentSnapshot fixture(bool rotated,bool parts=false) {
        NativeDocumentSnapshot doc;Construction::Object wp;wp.id="wp";wp.name="WP1";wp.kind=Construction::Kind::Workplane;
        if(rotated) wp.frame=gp_Ax3(gp_Pnt(20,30,70),gp_Dir(1,2,3),gp_Dir(2,-1,0));
        auto other=wp;other.id="other";other.name="WP2";other.frame.SetLocation(Sketch::worldPoint({-700,-700},wp.frame));doc.constructions={wp,other};
        if(parts) {
            gp_Trsf placement;placement.SetTransformation(wp.frame);placement.Invert();
            NativePartState box;box.id="box";box.name="Occluder";box.shape=BRepPrimAPI_MakeBox(gp_Pnt(-600,-600,5),1200,1200,10).Shape();box.transform=placement;
            NativePartState cylinder; cylinder.id="cylinder";cylinder.name="Projection source";
            cylinder.shape=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(400,100,20),gp_Dir(0,0,1)),30,20).Shape();cylinder.transform=placement;
            doc.parts={box,cylinder};
        }
        return doc;
    }
    static void styles(CadViewport& v) {
        int green=0;
        for(const auto& wp:v.constructions_) {
            const auto p=v.constructionPresentations_.at(wp.id);Quantity_Color color;p->Color(color);
            check(std::abs(p->Transparency()-0.5)<1.e-12,"Workplane border opacity is not exactly 50%");
            if(wp.id==v.activeSketchWorkplaneId_) {check(color.Green()>color.Red()+0.3 && color.Green()>color.Blue()+0.3,"active Workplane not green");++green;}
            else check(color.Red()>0.6 && color.Distance(Quantity_Color(color.Red(),color.Red(),color.Red(),Quantity_TOC_RGB))<1.e-9,"inactive Workplane not light gray");
            check(p->Shape().ShapeType()==TopAbs_FACE,"Workplane still contains arrows/extra shapes");
            check(BRepClass_FaceClassifier(TopoDS::Face(p->Shape()),Sketch::worldPoint({0,0},wp.frame),1.e-7).State()!=TopAbs_IN,"Workplane has a filled center");
        }
        check(green==(v.activeSketchWorkplaneId_.isEmpty()?0:1),"multiple active Workplanes");
    }
    static void filledBorder(CadViewport& v,const QString& id) {
        const auto* wp=v.constructionObject(id);const auto p=v.constructionPresentations_.at(id);
        const auto r=Sketch::displayRectangle(*wp,v.sketchEntities_);
        const double midX=(r.minX+r.maxX)/2,midY=(r.minY+r.maxY)/2,h=Sketch::WorkplaneBorderWidth/2;
        v.activateSketchWorkplane(id);v.fitConstructions({id});
        for(bool active:{true,false}) {
            if(!active) v.endSketchEditing();
            v.context_->ClearDetected(Standard_False);v.context_->ClearSelected(Standard_False);v.view_->Redraw();
            Image_AlienPixMap image;check(v.view_->ToPixMap(image,v.width(),v.height()),"border raster capture failed");
            const auto pixel=[&](const Image_AlienPixMap& raster,Sketch::Point local) {
                const auto world=Sketch::worldPoint(local,wp->frame);int x,y;v.view_->Convert(world.X(),world.Y(),world.Z(),x,y);
                check(x>=0 && y>=0 && x<int(raster.SizeX()) && y<int(raster.SizeY()),"border pixel outside capture");
                return raster.PixelColor(x,y).GetRGB();
            };
            const auto background=pixel(image,{midX,midY});
            std::optional<Quantity_Color> strip;
            for(const auto sample:std::vector<Sketch::Point>{{r.minX-h,midY},{r.maxX+h,midY},{midX,r.minY-h},{midX,r.maxY+h},
                    {r.minX-h,r.minY-h},{r.maxX+h,r.minY-h},{r.minX-h,r.maxY+h},{r.maxX+h,r.maxY+h}}) {
                const auto color=pixel(image,sample);
                check(color.Distance(background)>0.05,"Workplane strip interior is not filled (only contour lines are visible)");
                if(active) check(color.Green()>color.Red()+0.05 && color.Green()>color.Blue()+0.05,"filled active border not green");
                else check(std::abs(color.Red()-color.Green())<0.03 && std::abs(color.Green()-color.Blue())<0.03,"filled inactive border not gray");
                if(strip) check(color.Distance(*strip)<0.03,"Workplane corner has overlapping/darker transparent surfaces");
                else strip=color;
            }
            // Distinguish a translucent surface from an opaque fill, not only
            // from wireframe. Restore the presentation immediately afterwards.
            p->SetTransparency(0);v.context_->Redisplay(p,Standard_False);v.view_->Redraw();Image_AlienPixMap opaque;
            check(v.view_->ToPixMap(opaque,v.width(),v.height()),"opaque comparison capture failed");
            const auto solid=pixel(opaque,{r.minX-h,midY});
            p->SetTransparency(0.5);v.context_->Redisplay(p,Standard_False);
            check(solid.Distance(*strip)>0.05,"border transparency has no visible effect");
            check(p->DisplayMode()==AIS_Shaded && !p->IsInfinite(),"finite ring is not using the shaded presentation path");
        }
    }
    static void visiblePreview(CadViewport& v,const Sketch::Entity& entity) {
        const auto* wp=v.constructionObject(entity.workplaneId);const auto rectangle=v.workplaneDisplayRectangle(*wp);
        check(v.workplaneDisplayRectangles_.at(wp->id)==rectangle,"frame and blue preview use different bounds");
        const auto p=v.sketchPreviews_.at(entity.id);check(v.context_->IsDisplayed(p) && p->ZLayer()==Graphic3d_ZLayerId_Topmost,"preview hidden or wrong layer");
        std::vector<Sketch::Point> samples;
        if(const auto* line=std::get_if<Sketch::InfiniteLine>(&entity.geometry)) {
            const auto clipped=Sketch::clipLine(*line,rectangle);check(clipped.has_value(),"preview line outside current frame");
            check(p->Shape().ShapeType()==TopAbs_EDGE,"first AIS preview was built empty");
            const auto edge=TopoDS::Edge(p->Shape());
            check(BRep_Tool::Pnt(TopExp::FirstVertex(edge)).IsEqual(Sketch::worldPoint(clipped->start,wp->frame),1.e-7)
                && BRep_Tool::Pnt(TopExp::LastVertex(edge)).IsEqual(Sketch::worldPoint(clipped->end,wp->frame),1.e-7),"preview not clipped to outer border");
            for(double t:{0.25,0.5,0.75}) samples.push_back({clipped->start.x+t*(clipped->end.x-clipped->start.x),clipped->start.y+t*(clipped->end.y-clipped->start.y)});
        } else if(const auto* c=std::get_if<Sketch::Circle>(&entity.geometry)) {
            samples={{c->center.x+c->radius,c->center.y},{c->center.x,c->center.y+c->radius},{c->center.x-c->radius,c->center.y}};
        } else if(const auto* e=std::get_if<Sketch::Ellipse>(&entity.geometry)) {
            for(double t:{0.,1.,2.}) samples.push_back(Sketch::ellipsePoint(*e,t));
        } else throw std::runtime_error("add a visual sample for this preview type");
        v.fitConstructions({wp->id});v.view_->Redraw();Image_AlienPixMap image;
        check(v.view_->ToPixMap(image,v.width(),v.height()),"preview raster capture failed");
        for(const auto sample:samples) {
            const auto world=Sketch::worldPoint(sample,wp->frame);int x,y;v.view_->Convert(world.X(),world.Y(),world.Z(),x,y);bool blue=false;
            for(int py=std::max(0,y-7);py<std::min(int(image.SizeY()),y+8);++py)
                for(int px=std::max(0,x-7);px<std::min(int(image.SizeX()),x+8);++px) {
                    const auto c=image.PixelColor(px,py).GetRGB();if(c.Blue()>0.65 && c.Green()>0.25 && c.Red()<0.45) blue=true;
                }
            check(blue,"blue preview has topology but is not visible in the viewport before OK");
        }
    }
    static void run() {
        MainWindow w;w.resize(1300,1000);w.show();QApplication::processEvents();auto& v=*w.viewport_;QString error;
        for(bool rotated:{false,true}) {
            const auto doc=fixture(rotated);check(v.importDocumentSnapshot(doc,error),qPrintable(error));styles(v);
            filledBorder(v,"wp");filledBorder(v,"other");
            check(v.activateSketchWorkplane("wp"),"activation failed");styles(v);v.activateSketchWorkplane("other");styles(v);v.endSketchEditing();styles(v);v.activateSketchWorkplane("wp");
            const auto wp=doc.constructions.front();const auto baseline=v.workplaneDisplayRectangle(wp);w.documentDirty_=false;
            for(bool vertical:{true,false}) {
                v.startSketchDrawing(vertical?Sketch::DrawTool::VerticalInfiniteLine:Sketch::DrawTool::HorizontalInfiniteLine);
                auto* spin=vertical?v.sketchDrawingPanel_->x:v.sketchDrawingPanel_->y;spin->setValue(vertical?200:-150);
                auto candidate=v.commandSession_->drawing->candidate;visiblePreview(v,candidate);
                spin->setValue(vertical?350:-300);candidate=v.commandSession_->drawing->candidate;visiblePreview(v,candidate);
                check(v.sketchEntities_.empty() && v.undoStack_.empty() && !w.documentDirty_,"preview dirtied document/history");
                NativeDocumentSnapshot pending;check(v.exportDocumentSnapshot(pending) && pending.sketchEntities.empty(),"Save included preview");
                v.cancelCommandSession();check(v.workplaneDisplayRectangles_.at(wp.id)==baseline && v.sketchPreviews_.empty(),"Cancel retained frame/preview");
            }
            v.startSketchDrawing(Sketch::DrawTool::InfiniteLine);v.acceptSketchPoint({200,0});v.acceptSketchPoint({230,40});
            auto shown=v.commandSession_->drawing->candidate;visiblePreview(v,shown);v.confirmCommandSession();
            check(v.sketchEntities_==std::vector{shown} && v.undoStack_.size()==1,"OK differs from shown geometry");
            v.undoPull();check(v.workplaneDisplayRectangles_.at(wp.id)==baseline && v.sketchSelection().empty(),"Undo retained preview bounds/selection");v.redoPull();
            check(v.sketchEntities_==std::vector{shown},"Redo changed shown geometry");
            NativeDocumentSnapshot saved,loaded;QTemporaryDir dir;check(v.exportDocumentSnapshot(saved),"snapshot failed");
            check(NativeDocumentStorage::save(dir.filePath("ring.odc"),saved,error) && NativeDocumentStorage::load(dir.filePath("ring.odc"),loaded,error),qPrintable(error));
            check(v.importDocumentSnapshot(loaded,error) && v.sketchEntities_==saved.sketchEntities && v.undoStack_.empty(),"ring changed storage/history");styles(v);
            check(v.activateSketchWorkplane("wp"),"reactivation failed");auto changed=wp;changed.selectable=false;v.updateConstruction(changed);styles(v);
            check(v.activeSketchWorkplaneId_.isEmpty(),"locked Workplane stayed active");v.undoPull();v.activateSketchWorkplane("wp");changed=wp;changed.visible=false;v.updateConstruction(changed);styles(v);
            check(!v.context_->IsDisplayed(v.constructionPresentations_.at(wp.id)),"hidden border remains displayed");v.undoPull();v.activateSketchWorkplane("wp");styles(v);
            // Ellipse hover support uses the same complete temporary display set.
            v.startSketchDrawing(Sketch::DrawTool::Ellipse,Sketch::Role::Construction);v.acceptSketchEllipsePoint({500,200});v.acceptSketchEllipsePoint({580,200});
            visiblePreview(v,v.commandSession_->drawing->candidate);v.cancelCommandSession();
            auto offsetDoc=doc;Sketch::Entity source;source.workplaneId=wp.id;source.role=Sketch::Role::Construction;source.geometry=Sketch::InfiniteLine{{0,0},{1,0}};offsetDoc.sketchEntities={source};
            check(v.importDocumentSnapshot(offsetDoc,error),qPrintable(error));v.activateSketchWorkplane("wp");v.selectSketchEntities({source.id});w.documentDirty_=false;
            v.startSketchConstruction(Sketch::ConstructionTool::OffsetLine);v.sketchConstructionPanel_->distance->setValue(250);visiblePreview(v,v.commandSession_->lineConstruction->candidate);v.cancelCommandSession();
            check(v.sketchEntities_==offsetDoc.sketchEntities && v.undoStack_.empty() && !w.documentDirty_,"Offset preview mutated source");
            // Occluding Parts must not hide blue geometry; the source cylinder's
            // circular edge is also projected outside the previous empty frame.
            const auto withParts=fixture(rotated,true);check(v.importDocumentSnapshot(withParts,error),qPrintable(error));v.activateSketchWorkplane("wp");
            v.startSketchDrawing(Sketch::DrawTool::VerticalInfiniteLine);v.sketchDrawingPanel_->x->setValue(200);visiblePreview(v,v.commandSession_->drawing->candidate);v.cancelCommandSession();
            v.startSketchProjection();TopoDS_Shape circular;
            for(TopExp_Explorer it(v.boxes_[1].shape,TopAbs_EDGE);it.More();it.Next()) if(BRepAdaptor_Curve(TopoDS::Edge(it.Current())).GetType()==GeomAbs_Circle) {circular=it.Current();break;}
            check(!circular.IsNull(),"cylinder circular edge missing");v.context_->ClearSelected(Standard_False);
            v.context_->AddOrRemoveSelected(new StdSelect_BRepOwner(circular,v.boxes_[1].presentation,5,Standard_True),Standard_False);
            check(v.captureSketchProjectionSelection() && v.commandSession_->valid,"circle projection preview invalid");
            v.context_->ClearSelected(Standard_False);check(v.commandSession_->sketch->after.size()==1,"projection candidate set incomplete");
            visiblePreview(v,v.commandSession_->sketch->after.front());v.cancelCommandSession();
        }
        // A border with a true hole must allow ordinary 3D picking through it.
        auto doc=fixture(false);NativePartState part;part.id="behind";part.name="Behind";part.shape=BRepPrimAPI_MakeBox(gp_Pnt(-50,-50,-20),100,100,10).Shape();doc.parts={part};
        check(v.importDocumentSnapshot(doc,error),qPrintable(error));v.activateSketchWorkplane("wp");v.endSketchEditing();
        int x,y;v.view_->Convert(0,0,0,x,y);v.context_->MoveTo(x,y,v.view_,Standard_False);
        check(v.context_->HasDetected() && v.context_->DetectedInteractive()==v.boxes_[0].presentation,"Workplane middle blocks Part selection");
        w.documentDirty_=false;w.close();QApplication::processEvents();
    }
};
void runWorkplaneRendering() {WorkplaneRenderingTest::run();}
