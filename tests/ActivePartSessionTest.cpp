#include "CadViewport.h"
#include "MainWindow.h"
#include "PartPresentation.h"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepTools.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <sstream>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <Graphic3d_Group.hxx>
#include <PrsMgr_Presentation.hxx>
#include <QApplication>
#include <QTreeWidget>
#include <QMouseEvent>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QContextMenuEvent>
#include <Prs3d_Drawer.hxx>
#include <AIS_ListOfInteractive.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <TopExp_Explorer.hxx>
#include <QTemporaryDir>
#include <stdexcept>
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Aspect>
bool outlined(const Aspect& aspect,const Handle(Prs3d_Drawer)& drawer) {
    if constexpr(requires { aspect.ToDrawSilhouette(); })return aspect.ToDrawSilhouette();
    return drawer->FaceBoundaryDraw();
}
bool outlined(const Handle(AIS_Shape)& shape) {
    return outlined(*shape->Attributes()->ShadingAspect()->Aspect(),shape->Attributes());
}
template<class Aspect>
Quantity_Color outlineColor(const Aspect& aspect,const Handle(Prs3d_Drawer)& drawer) {
    if constexpr(requires { aspect.ToDrawSilhouette(); })return aspect.EdgeColor();
    return drawer->FaceBoundaryAspect()->Aspect()->Color();
}
}
struct ActivePartSessionTest {
static void run() {
    MainWindow window;window.resize(1200,900);window.show();QApplication::processEvents();
    auto& v=*window.viewport_;v.initializeViewer();v.addBox();v.addBox();v.fitAll();
    AIS_ListOfInteractive displayedBefore;v.context_->DisplayedObjects(displayedBefore);
    const auto selectionColor=v.context_->SelectionStyle()->Color();
    const auto hoverColor=v.context_->HighlightStyle(Prs3d_TypeOfHighlight_LocalDynamic)->Color();
    const auto featureLines=[&](const Handle(AIS_Shape)& part,const Quantity_Color& color,double width=2.8) {
        const auto presentation=v.context_->MainPrsMgr()->Presentation(part,AIS_Shaded);
        if(presentation.IsNull())return false;
        for(const auto& group:presentation->Groups()) {
            const auto aspect=group->Aspects();
            if(!group->IsClosed() && !aspect.IsNull() && aspect->Color().IsEqual(color)
                && std::abs(aspect->LineWidth()-width*v.devicePixelRatioF())<0.001)return true;
        }
        return false;
    };
    const auto a=v.partId(0),b=v.partId(1);int changes=0;
    QObject::connect(&v,&CadViewport::activePartChanged,&v,[&]{++changes;});
    check(v.setActivePart(a) && changes==1,"activation failed");
    check(!window.root_->child(0)->icon(0).isNull(),"browser active icon absent");
    const auto shape=v.boxes_[0].shape;const auto color=v.partColor(0);
    window.documentDirty_=false;
    check(v.setActivePart(a) && changes==1 && !window.documentDirty_,"idempotent activation modified document state");
    check(!v.setActivePart("missing") && v.activePartId()==a,"invalid activation accepted");
    check(outlined(v.boxes_[0].presentation),"active outline absent");
    AIS_ListOfInteractive displayedAfter;v.context_->DisplayedObjects(displayedAfter);
    check(displayedBefore.Size()==displayedAfter.Size(),"activation created duplicate visual objects");
    check(v.context_->SelectionStyle()->Color()==selectionColor && v.context_->HighlightStyle(Prs3d_TypeOfHighlight_LocalDynamic)->Color()==hoverColor,"activation changed hover/selection appearance");
    const auto green=outlineColor(*v.boxes_[0].presentation->Attributes()->ShadingAspect()->Aspect(),v.boxes_[0].presentation->Attributes());
    const Quantity_Color expectedGreen(167.0/255,212.0/255,0.0,Quantity_TOC_sRGB);
    check(green.IsEqual(expectedGreen),"active outline is not #A7D400");
    check(featureLines(v.boxes_[0].presentation,expectedGreen),"active sharp feature edges absent from shaded presentation");
    const auto outlineWidth=[]<class Aspect>(const Aspect& aspect,const auto& drawer) {
        if constexpr(requires { aspect.ToDrawSilhouette(); })return double(aspect.EdgeWidth());
        return double(drawer->FaceBoundaryAspect()->Aspect()->Width());
    };
    auto drawer=v.boxes_[0].presentation->Attributes();
    check(std::abs(outlineWidth(*drawer->ShadingAspect()->Aspect(),drawer)-2.8*v.devicePixelRatioF())<0.001,"active outline width wrong");
    check(v.context_->SelectionStyle()->ZLayer()==Graphic3d_ZLayerId_Top
        && v.viewer_->ZLayerSettings(Graphic3d_ZLayerId_Top).ToEnableDepthTest()
        && !v.viewer_->ZLayerSettings(Graphic3d_ZLayerId_Top).ToClearDepth(),"whole Part selection ignores scene depth");
    const auto faceStyle=v.context_->HighlightStyle(Prs3d_TypeOfHighlight_LocalSelected);
    const auto faceMode=faceStyle->DisplayMode();const auto faceColor=faceStyle->Color();
    v.selectParts({0});
    check(outlineColor(*drawer->ShadingAspect()->Aspect(),drawer).IsEqual(selectionColor)
        && !drawer->FaceBoundaryDraw() && !drawer->ShadingAspect()->Aspect()->ToDrawEdges()
        && std::abs(outlineWidth(*drawer->ShadingAspect()->Aspect(),drawer)-2*v.devicePixelRatioF())<0.001,"selected silhouette did not override Active Part in cyan");
    check(v.context_->IsSelected(v.boxes_[0].presentation->GlobalSelOwner())
        && !v.context_->MainPrsMgr()->IsHighlighted(v.boxes_[0].presentation,AIS_WireFrame),"whole Part still draws selection wireframe edges");
    check(!featureLines(v.boxes_[0].presentation,selectionColor,2),"selected plain Part retained unrelated sharp edges");
    check(v.boxes_[0].shape.IsSame(shape) && v.partColor(0)==color,"selected contour changed geometry/material");
    v.selectParts({1});
    check(outlineColor(*drawer->ShadingAspect()->Aspect(),drawer).IsEqual(expectedGreen)
        && featureLines(v.boxes_[0].presentation,expectedGreen),"deselect did not restore yellow-green silhouette/features");
    v.selectParts({});
    check(!outlined(v.boxes_[1].presentation) && !v.boxes_[1].presentation->Attributes()->FaceBoundaryDraw(),"deselected Part retained cyan contours");
    check(faceStyle->DisplayMode()==faceMode && faceStyle->Color()==faceColor
        && v.context_->HighlightStyle(Prs3d_TypeOfHighlight_LocalDynamic)->Color()==hoverColor,"contours changed face/hover styles");
    AIS_ListOfInteractive contourObjects;v.context_->DisplayedObjects(contourObjects);
    check(contourObjects.Size()==displayedBefore.Size(),"selected contours created duplicate AIS objects");
    v.selectParts({1});check(v.activePartId()==a,"part selection changed active part");
    for(const auto type:{TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX}) {
        TopExp_Explorer it(v.boxes_[1].shape,type);
        auto owner=new StdSelect_BRepOwner(it.Current(),v.boxes_[1].presentation,5,true);
        v.context_->ClearSelected(false);v.context_->AddOrRemoveSelected(owner,false);
        check(v.activePartId()==a,"subshape selection changed active part");
        check(v.context_->IsHilighted(owner),"local subshape highlight was suppressed with whole-part wireframe");
    }
    v.setActivePart(b);
    check(window.root_->child(0)->icon(0).isNull() && !window.root_->child(1)->icon(0).isNull(),"browser switch stale");
    check(!outlined(v.boxes_[0].presentation) && outlined(v.boxes_[1].presentation),"outline switch stale");
    v.setPartVisible(1,false);
    check(v.activePartId()==b && !v.context_->IsDisplayed(v.boxes_[1].presentation),"hidden active part displayed");
    QString error;auto eligible=[&](size_t i){return v.solidTargetError(i);};
    check(!v.initialTargetPart(eligible,error) && !error.isEmpty(),"hidden initial target accepted");
    v.setPartVisible(1,true);check(v.initialTargetPart(eligible,error)==std::optional<size_t>(1) && error.isEmpty(),"eligible active target not supplied");
    check(v.context_->IsDisplayed(v.boxes_[1].presentation),"show did not restore presentation");
    v.setActivePart({});check(window.root_->child(1)->icon(0).isNull() && !outlined(v.boxes_[1].presentation),"deactivation stale");
    check(!v.initialTargetPart(eligible,error) && error.isEmpty(),"absent active target resolved");
    // Use the same detected owner for part, face, edge and vertex picks.
    v.selectParts({});v.view_->SetProj(0,0,1);v.fitAll();
    QString picked;v.beginTargetPicking(eligible,[&](const QString& id){picked=id;});
    Standard_Integer x,y;const auto& part=v.boxes_[0];
    v.view_->Convert((part.min[0]+part.max[0])/2,(part.min[1]+part.max[1])/2,part.max[2],x,y);
    const QPoint point(x,y);check(v.detectedPart(point)==std::optional<size_t>(0),"face owner did not resolve to target");
    check(v.detectedPart(QPoint(-100,-100))==std::nullopt,"empty space resolved a target");
    QMouseEvent emptyClick(QEvent::MouseButtonPress,QPointF(-100,-100),QPointF(-100,-100),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    v.mousePressEvent(&emptyClick);check(v.targetPicking() && picked.isEmpty(),"empty click ended picking or accepted a target");
    QMouseEvent click(QEvent::MouseButtonPress,QPointF(point),QPointF(point),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    v.mousePressEvent(&click);check(picked==a && !v.targetPicking() && v.activePartId().isEmpty(),"viewport pick changed active or failed");
    v.setActivePart(b);v.beginTargetPicking(eligible,[&](const QString& id){picked=id;});
    v.mousePressEvent(&click);check(picked==a && v.activePartId()==b,"temporary target replaced active part");
    // Frontmost detected face owns a pick, even when another Part is selected.
    gp_Trsf overlap;overlap.SetTranslation(gp_Vec(-125,0,30));
    v.boxes_[1].orientation=overlap;v.applyFaceState(1,v.boxes_[1]);v.view_->ZFitAll();v.view_->Redraw();
    v.beginTargetPicking(eligible,[&](const QString& id){picked=id;});
    check(v.detectedPart(point)==std::optional<size_t>(1),"overlapping pick ignored frontmost part");
    v.mousePressEvent(&click);check(picked==b,"overlapping target was wrong");
    v.setPartVisible(1,false);v.beginTargetPicking(eligible,[&](const QString& id){picked=id;});
    v.mousePressEvent(&click);check(picked==a,"hidden overlapping target intercepted pick");
    v.setPartVisible(1,true);v.boxes_[1].orientation=gp_Trsf();v.applyFaceState(1,v.boxes_[1]);
    v.setActivePart(b);v.selectParts({1});
    QTimer::singleShot(0,&v,[&]{
        auto* menu=v.findChild<QMenu*>("viewportContextMenu");check(menu,"viewport menu absent");
        for(auto* action:menu->actions())if(action->text()=="Set Active Part") {action->trigger();menu->close();return;}
        menu->close();throw std::runtime_error("Set Active Part absent over geometry");
    });
    QContextMenuEvent context(QContextMenuEvent::Mouse,point,v.mapToGlobal(point));v.contextMenuEvent(&context);
    check(v.activePartId()==a && v.selectedPartIndices_==std::vector<size_t>{1},"context activation used selection instead of clicked geometry");
    v.setActivePart(a);gp_Trsf moved;moved.SetTranslation(gp_Vec(10,20,30));v.boxes_[0].orientation=moved;v.applyFaceState(0,v.boxes_[0]);
    check(v.activePartId()==a && v.boxes_[0].presentation->LocalTransformation().TranslationPart().IsEqual(moved.TranslationPart(),1e-8),"transformed active part lost identity");
    check(v.boxes_[0].shape.IsSame(shape) && v.partColor(0)==color,"activation changed shape/material");
    NativeDocumentSnapshot snapshot,loaded;check(v.exportDocumentSnapshot(snapshot),"snapshot failed");
    QTemporaryDir directory;const auto path=directory.filePath("active.odc");
    check(NativeDocumentStorage::save(path,snapshot,error) && NativeDocumentStorage::load(path,loaded,error) && loaded.activePartId==a,"active persistence failed");
    check(v.importDocumentSnapshot(loaded,error) && v.activePartId()==a && outlined(v.boxes_[0].presentation),"open lost active presentation");
    loaded.activePartId="invalid";check(!v.importDocumentSnapshot(loaded,error) && v.activePartId()==a,"invalid import replaced document");
    // Existing Boolean lifecycle removes source UUIDs and must clear active state.
    v.boxes_[0].active=false;v.context_->Remove(v.boxes_[0].presentation,false);emit v.partsChanged();
    check(v.activePartId().isEmpty() && !v.context_->IsDisplayed(v.boxes_[0].presentation),"removed active reference/presentation stale");
    v.boxes_[0].active=true;emit v.partsChanged();check(v.activePartId().isEmpty(),"restoration silently reactivated part");
    v.setActivePart(b);v.resetDocument();check(v.activePartId().isEmpty() && !v.partIndexForId(b),"reset left active reference");
    // Sharp rims are features; seams and smooth geometric joins are not.
    v.addCylinder();const auto cylinder=v.boxes_[0].presentation;
    const auto edgeCount=[](const TopoDS_Shape& shape) {
        TopTools_IndexedMapOfShape edges;TopExp::MapShapes(shape,TopAbs_EDGE,edges);return edges.Extent();
    };
    check(edgeCount(PartPresentation::featureEdges(cylinder->Shape()))==2,"cylinder feature filter kept its seam or lost its rims");
    const auto body=BRepPrimAPI_MakeBox(100,100,20).Shape();
    const auto boss=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(70,50,20),gp_Dir(0,0,1)),15,20).Shape();
    const auto hole=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(30,50,-10),gp_Dir(0,0,1)),10,40).Shape();
    const auto detailed=BRepAlgoAPI_Cut(BRepAlgoAPI_Fuse(body,boss).Shape(),hole).Shape();
    const auto detailEdges=PartPresentation::featureEdges(detailed);int circles=0;
    for(TopExp_Explorer it(detailEdges,TopAbs_EDGE);it.More();it.Next())
        if(BRepAdaptor_Curve(TopoDS::Edge(it.Current())).GetType()==GeomAbs_Circle)++circles;
    check(circles==4,"hole rims or boss transitions missing from feature highlight");
    const auto panel=[](double x) {
        BRepBuilderAPI_MakePolygon wire;wire.Add(gp_Pnt(x,0,0));wire.Add(gp_Pnt(x+10,0,0));
        wire.Add(gp_Pnt(x+10,10,0));wire.Add(gp_Pnt(x,10,0));wire.Close();
        return BRepBuilderAPI_MakeFace(wire.Wire()).Shape();
    };
    BRepBuilderAPI_Sewing sewing;sewing.Add(panel(0));sewing.Add(panel(10));sewing.Perform();
    check(edgeCount(sewing.SewedShape())==7 && edgeCount(PartPresentation::featureEdges(sewing.SewedShape()))==6,
        "geometrically tangent join was highlighted");
    v.setActivePart(v.partId(0));v.selectParts({0});
    auto cylinderDrawer=cylinder->Attributes();
    check(outlined(cylinder) && !cylinderDrawer->FaceBoundaryDraw()
        && !cylinderDrawer->ShadingAspect()->Aspect()->ToDrawEdges()
        && !v.context_->MainPrsMgr()->IsHighlighted(cylinder,AIS_WireFrame),"cylinder seam/boundaries entered part highlighting");
    v.view_->SetProj(1,-1,1);
    gp_Trsf rotation;rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,0,0)),0.4);
    v.boxes_[0].orientation=rotation;v.applyFaceState(0,v.boxes_[0]);v.selectParts({0});
    v.view_->ZFitAll();v.view_->Redraw();
    check(outlined(cylinder) && !cylinderDrawer->FaceBoundaryDraw()
        && outlineColor(*cylinderDrawer->ShadingAspect()->Aspect(),cylinderDrawer).IsEqual(selectionColor)
        && !v.context_->MainPrsMgr()->IsHighlighted(cylinder,AIS_WireFrame),"camera/part rotation restored cylinder wireframe");
    v.selectParts({});v.setActivePart({});check(!outlined(cylinder),"cylinder silhouette remained after deselect/deactivate");
    // OCCT 7.6 used to attach wireframe Polygon3D caches to the canonical
    // BRep. Preserve full serialized evidence, including its existing mesh.
    const auto evidence=BRepPrimAPI_MakeBox(30,40,50).Shape();
    Handle(PartPresentation) display=new PartPresentation(evidence);
    v.context_->Display(display,AIS_Shaded,-1,false);
    const auto serialized=[&] {std::ostringstream out;BRepTools::Write(evidence,out);return out.str();};
    const auto beforeHighlight=serialized();
    for(const auto& highlightColor:{expectedGreen,selectionColor}) {
        display->setFeatureHighlight(true,highlightColor);
        display->SetToUpdate(AIS_Shaded);v.context_->MainPrsMgr()->Update(display,AIS_Shaded);
        check(serialized()==beforeHighlight,"feature rendering mutated canonical BRep/mesh evidence");
    }
    display->setFeatureHighlight(false,selectionColor);
    display->SetToUpdate(AIS_Shaded);v.context_->MainPrsMgr()->Update(display,AIS_Shaded);
    check(serialized()==beforeHighlight,"clearing features mutated canonical BRep/mesh evidence");
    v.context_->Remove(display,false);
    window.documentDirty_=false;window.close();
}
};
void runActivePartSessions(){ActivePartSessionTest::run();}

void runSelectionContours() {
    const auto count=[](const TopoDS_Shape& shape) {
        TopTools_IndexedMapOfShape edges;TopExp::MapShapes(shape,TopAbs_EDGE,edges);return edges.Extent();
    };
    const auto cylinder=BRepPrimAPI_MakeCylinder(10,30).Shape();
    std::ostringstream beforeContour;BRepTools::Write(cylinder,beforeContour);
    TopoDS_Face side,cap;
    for(TopExp_Explorer it(cylinder,TopAbs_FACE);it.More();it.Next()) {
        auto f=TopoDS::Face(it.Current());
        if(BRepAdaptor_Surface(f).GetType()==GeomAbs_Cylinder)side=f;else cap=f;
    }
    for(const auto direction:{gp_Dir(1,0,0),gp_Dir(0,1,0),gp_Dir(1,1,1)}) {
        const HLRAlgo_Projector projector(gp_Ax2(gp_Pnt(),direction));
        const auto contour=PartPresentation::faceContour(side,projector);
        int generators=0;
        for(TopExp_Explorer it(contour,TopAbs_EDGE);it.More();it.Next()) {
            BRepAdaptor_Curve curve(TopoDS::Edge(it.Current()));
            if(curve.GetType()!=GeomAbs_Line)continue;
            const auto mid=curve.Value((curve.FirstParameter()+curve.LastParameter())/2);
            // Silhouette generators lie in the axial plane perpendicular to view.
            check(std::abs(mid.X()*direction.X()+mid.Y()*direction.Y())<1e-5,
                  "cylinder contour retained its front seam instead of silhouette");
            ++generators;
        }
        check(count(contour)>2,"curved Face lost visible trimmed rims");
        check(generators==2,"curved Face lost view-dependent silhouette generators");
    }
    PartPresentation cached(cylinder);
    const HLRAlgo_Projector orthographic(gp_Ax2(gp_Pnt(),gp_Dir(1,0,0)));
    const std::vector<PartPresentation::FaceContour> selectedFace{{side,Quantity_NOC_LIGHTBLUE,2.}};
    check(cached.setFaceContours(selectedFace,orthographic),"initial Face contour not generated");
    check(!cached.setFaceContours(selectedFace,orthographic),"unchanged Face contour cache missed");
    check(cached.setFaceContours(selectedFace,HLRAlgo_Projector(gp_Ax2(gp_Pnt(),gp_Dir(0,1,0)))),"camera rotation did not invalidate Face contour");
    std::ostringstream afterContour;BRepTools::Write(cylinder,afterContour);
    check(beforeContour.str()==afterContour.str(),"Face contour rendering mutated canonical BRep");
    check(count(PartPresentation::faceContour(cap,HLRAlgo_Projector(gp_Ax2(gp_Pnt(),gp_Dir(0,0,1)))))==1,
          "planar disk contour lost its outline");
    TopExp_Explorer rim(cylinder,TopAbs_EDGE);
    BRepFilletAPI_MakeFillet fillet(cylinder);fillet.Add(2,TopoDS::Edge(rim.Current()));fillet.Build();
    check(fillet.IsDone(),"contour fillet fixture failed");
    std::vector<TopoDS_Face> blends;
    for(TopExp_Explorer it(fillet.Shape(),TopAbs_FACE);it.More();it.Next())
        if(BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType()==GeomAbs_Torus)blends.push_back(TopoDS::Face(it.Current()));
    check(blends.size()==1,"missing toroidal Fillet fixture");
    check(PartPresentation::geometricFilletFaces(fillet.Shape()).size()==1,"imported toroidal Fillet not classified geometrically");
    check(PartPresentation::geometricFilletFaces(cylinder).empty(),"ordinary cylinder misclassified as Fillet");
    for(const auto direction:{gp_Dir(1,0,0),gp_Dir(1,1,1)})
        check(count(PartPresentation::faceContour(blends.front(),HLRAlgo_Projector(gp_Ax2(gp_Pnt(),direction))))>0,
              "toroidal Face lost visible contour under camera rotation");
    check(count(PartPresentation::featureEdges(fillet.Shape(),blends,true))==2,
          "Part contour lost tangent Fillet transitions or kept unrelated edges");
    check(count(PartPresentation::featureEdges(cylinder,{},true))==0,"plain Part retained unrelated rims/seams");
    gp_Trsf transform;transform.SetTranslation(gp_Vec(100,20,40));
    const auto shifted=TopoDS::Face(side.Moved(TopLoc_Location(transform)));
    check(count(PartPresentation::faceContour(shifted,HLRAlgo_Projector(gp_Ax2(gp_Pnt(100,20,40),gp_Dir(1,0,0)))))==
          count(PartPresentation::faceContour(side,HLRAlgo_Projector(gp_Ax2(gp_Pnt(),gp_Dir(1,0,0))))),"transformed Face changed contour topology");
}
