#pragma once

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <Graphic3d_Camera.hxx>
#include <vector>
#include <algorithm>
#include <BRepLib.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <TopExp_Explorer.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <StdPrs_WFShape.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <SelectMgr_Selection.hxx>
#include <StdSelect_BRepOwner.hxx>

// Face and Part contours live in the depth-tested shaded presentation.
// Keep sensitive owners and picking semantics, suppress their stock wireframes.
class PartSelectionOwner final : public StdSelect_BRepOwner {
public:
    PartSelectionOwner(const Handle(StdSelect_BRepOwner)& original)
        : StdSelect_BRepOwner(original->Shape(),original->Selectable(),original->Priority(),original->ComesFromDecomposition()) {}

    void HilightWithColor(const Handle(PrsMgr_PresentationManager)& manager,
                          const Handle(Prs3d_Drawer)& style,
                          Standard_Integer mode=0) override {
        const auto object=Handle(AIS_InteractiveObject)::DownCast(Selectable());
        const auto context=object.IsNull()?Handle(AIS_InteractiveContext)():object->GetContext();
        if(!context.IsNull() && (style==context->SelectionStyle()
            ||style==context->HighlightStyle(Prs3d_TypeOfHighlight_Dynamic)
            ||style==context->HighlightStyle(Prs3d_TypeOfHighlight_LocalSelected)
            ||style==context->HighlightStyle(Prs3d_TypeOfHighlight_LocalDynamic)))return;
        StdSelect_BRepOwner::HilightWithColor(manager,style,mode);
    }
};

class PartPresentation final : public AIS_Shape {
public:
    explicit PartPresentation(const TopoDS_Shape& shape) : AIS_Shape(shape) {}

    bool setFeatureHighlight(bool enabled,const Quantity_Color& color,double width=2.8) {
        const bool changed=featureHighlight_!=enabled || (enabled && (featureColor_!=color||std::abs(featureWidth_-width)>0.001));
        featureHighlight_=enabled;featureColor_=color;featureWidth_=width;return changed;
    }

    struct FaceContour { TopoDS_Face face; Quantity_Color color; double width; };
    // Exact HLR generates view-dependent curves in model space. In particular,
    // sewn (CN) edges and isoparameters are deliberately never extracted.
    static TopoDS_Compound faceContour(const TopoDS_Face& face,const HLRAlgo_Projector& projector) {
        Handle(HLRBRep_Algo) hlr=new HLRBRep_Algo;
        const auto displayFace=TopoDS::Face(BRepBuilderAPI_Copy(face,false,false).Shape());
        hlr->Add(displayFace);
        hlr->Projector(projector);hlr->Update();hlr->Hide();
        HLRBRep_HLRToShape result(hlr);
        TopoDS_Compound contour;BRep_Builder builder;builder.MakeCompound(contour);
        const auto outlines=result.CompoundOfEdges(HLRBRep_OutLine,true,true);
        if(!outlines.IsNull())builder.Add(contour,outlines);
        TopoDS_Compound excluded;builder.MakeCompound(excluded);
        bool hasExcluded=!outlines.IsNull();
        if(hasExcluded)builder.Add(excluded,outlines);
        TopTools_IndexedMapOfShape edges;TopExp::MapShapes(displayFace,TopAbs_EDGE,edges);
        for(int i=1;i<=edges.Extent();++i) {
            const auto edge=TopoDS::Edge(edges(i));
            if(BRep_Tool::IsClosed(edge,displayFace)) {builder.Add(excluded,edge);hasExcluded=true;}
        }
        const auto sharp=result.CompoundOfEdges(HLRBRep_Sharp,true,true);
        for(TopExp_Explorer it(sharp,TopAbs_EDGE);it.More();it.Next()) {
            const auto edge=TopoDS::Edge(it.Current());const BRepAdaptor_Curve curve(edge);
            bool redundant=hasExcluded;
            for(double fraction:{0.2,0.5,0.8}) {
                if(!redundant)break;
                const auto point=curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*fraction);
                BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(point).Shape(),excluded);
                redundant=distance.IsDone() && distance.Value()<=std::max(1e-7,10*BRep_Tool::Tolerance(edge));
            }
            if(!redundant)builder.Add(contour,edge);
        }
        return contour;
    }
    bool setFaceContours(const std::vector<FaceContour>& faces,const HLRAlgo_Projector& projector) {
        bool same=faces.size()==faceInputs_.size() && projector.Perspective()==projector_.Perspective()
            && (!projector.Perspective() || std::abs(projector.Focus()-projector_.Focus())<1e-9);
        for(int r=1;r<=3 && same;++r)for(int c=1;c<=4;++c)
            if(std::abs(projector.Transformation().Value(r,c)-projector_.Transformation().Value(r,c))>1e-9)same=false;
        for(size_t i=0;i<faces.size() && same;++i)
            same=faces[i].face.IsEqual(faceInputs_[i].face) && faces[i].color==faceInputs_[i].color && faces[i].width==faceInputs_[i].width;
        if(same || (faces.empty() && faceInputs_.empty()))return false;
        faceInputs_=faces;projector_=projector;faceContours_.clear();
        for(const auto& face:faces)faceContours_.push_back({faceContour(face.face,projector),face.color,face.width});
        return true;
    }
    void setFilletFaces(const std::vector<TopoDS_Face>& faces,bool transitionsOnly) {
        if(classifiedShape_.IsNull() || !classifiedShape_.IsSame(Shape())) {
            classifiedShape_=Shape();geometricFillets_=geometricFilletFaces(Shape());
        }
        filletFaces_=geometricFillets_;
        for(const auto& face:faces)
            if(std::none_of(filletFaces_.begin(),filletFaces_.end(),[&](const auto& f){return f.IsSame(face);}))filletFaces_.push_back(face);
        transitionsOnly_=transitionsOnly;
    }

    // Imported analytic blends have no recipe. Certify tangent support pairs
    // geometrically; an ordinary cylinder and its non-tangent end caps fail.
    static std::vector<TopoDS_Face> geometricFilletFaces(const TopoDS_Shape& shape) {
        TopTools_IndexedDataMapOfShapeListOfShape adjacent;
        TopExp::MapShapesAndAncestors(shape,TopAbs_EDGE,TopAbs_FACE,adjacent);
        TopTools_IndexedMapOfShape all;TopExp::MapShapes(shape,TopAbs_FACE,all);
        std::vector<TopoDS_Face> result;
        for(int i=1;i<=all.Extent();++i) {
            const auto face=TopoDS::Face(all(i));const BRepAdaptor_Surface surface(face);
            const auto type=surface.GetType();
            if(type!=GeomAbs_Cylinder && type!=GeomAbs_Torus)continue;
            TopTools_IndexedMapOfShape supports;
            for(int j=1;j<=adjacent.Extent();++j) {
                const auto edge=TopoDS::Edge(adjacent.FindKey(j));
                if(BRep_Tool::Degenerated(edge)||BRep_Tool::IsClosed(edge,face))continue;
                bool belongs=false;for(const auto& f:adjacent.FindFromIndex(j))if(f.IsSame(face))belongs=true;
                if(!belongs)continue;
                for(const auto& f:adjacent.FindFromIndex(j))if(!f.IsSame(face)) {
                    const auto support=TopoDS::Face(f);const auto supportType=BRepAdaptor_Surface(support).GetType();
                    if(type==GeomAbs_Cylinder && supportType!=GeomAbs_Plane)continue;
                    if(type==GeomAbs_Torus && supportType!=GeomAbs_Plane && supportType!=GeomAbs_Cylinder && supportType!=GeomAbs_Cone)continue;
                    try {if(BRepLib::ContinuityOfFaces(edge,face,support,1e-4)!=GeomAbs_C0)supports.Add(support);}
                    catch(const Standard_Failure&) {}
                }
            }
            if(supports.Extent()!=2)continue;
            if(type==GeomAbs_Cylinder && BRepAdaptor_Surface(TopoDS::Face(supports(1))).Plane().Axis().Direction().IsParallel(
                BRepAdaptor_Surface(TopoDS::Face(supports(2))).Plane().Axis().Direction(),1e-4))continue;
            result.push_back(face);
        }
        return result;
    }

    // Shared BRep references only: no copied geometry or selection primitives.
    // Geometric continuity also handles imported tangent joins without stored
    // regularity flags. 1e-4 radians rejects numerical noise at tangent joins.
    static TopoDS_Compound featureEdges(const TopoDS_Shape& shape,const std::vector<TopoDS_Face>& fillets={},bool transitionsOnly=false) {
        TopoDS_Compound features;BRep_Builder builder;builder.MakeCompound(features);
        TopTools_IndexedDataMapOfShapeListOfShape adjacent;
        TopExp::MapShapesAndAncestors(shape,TopAbs_EDGE,TopAbs_FACE,adjacent);
        for(int i=1;i<=adjacent.Extent();++i) {
            const auto edge=TopoDS::Edge(adjacent.FindKey(i));
            if(BRep_Tool::Degenerated(edge))continue;
            TopTools_IndexedMapOfShape faces;bool seam=false;
            for(const auto& face:adjacent.FindFromIndex(i)) {
                faces.Add(face);
                if(BRep_Tool::IsClosed(edge,TopoDS::Face(face)))seam=true;
            }
            if(seam || faces.IsEmpty() || faces.Extent()>2)continue;
            int blendSides=0;
            for(const auto& face:fillets)if(faces.Contains(face))++blendSides;
            const bool transition=faces.Extent()==2 && blendSides==1;
            if(transitionsOnly && !transition)continue;
            if(faces.Extent()==2 && !transition) {
                try {
                    if(BRepLib::ContinuityOfFaces(edge,TopoDS::Face(faces(1)),TopoDS::Face(faces(2)),1e-4)!=GeomAbs_C0)continue;
                } catch(const Standard_Failure&) {continue;} // Unclassifiable joins are not highlighted.
            }
            builder.Add(features,edge);
        }
        return features;
    }

protected:
    void Compute(const Handle(PrsMgr_PresentationManager)& manager,
                 const Handle(Prs3d_Presentation)& presentation,Standard_Integer mode) override {
        AIS_Shape::Compute(manager,presentation,mode);
        if(mode!=AIS_Shaded)return;
        for(const auto& contour:faceContours_)drawLines(presentation,contour.shape,contour.color,contour.width);
        if(!featureHighlight_)return;
        drawLines(presentation,featureEdges(Shape(),filletFaces_,transitionsOnly_),featureColor_,featureWidth_);
    }
    void drawLines(const Handle(Prs3d_Presentation)& presentation,const TopoDS_Shape& features,
                   const Quantity_Color& color,double width) {
        if(!TopExp_Explorer(features,TopAbs_EDGE).More())return;
        Handle(Prs3d_Drawer) drawer=new Prs3d_Drawer;drawer->SetLink(Attributes());
        const Handle(Prs3d_LineAspect) lines=new Prs3d_LineAspect(color,Aspect_TOL_SOLID,width);
        drawer->SetWireAspect(lines);drawer->SetLineAspect(lines);
        drawer->SetFreeBoundaryAspect(lines);drawer->SetUnFreeBoundaryAspect(lines);
        // Scene-depth-tested lines in the same presentation as the shaded
        // faces. No Topmost layer, triangulation edges, or selectable overlay.
        // OCCT 7.6's wireframe builder writes Polygon3D caches into edges.
        // Detach only display-edge topology/caches; share the exact curves.
        // Document and Undo snapshots must not change when highlighting draws.
        const auto displayEdges=BRepBuilderAPI_Copy(features,false,false).Shape();
        StdPrs_WFShape::Add(presentation,displayEdges,drawer);
    }

    void ComputeSelection(const Handle(SelectMgr_Selection)& selection,Standard_Integer mode) override {
        AIS_Shape::ComputeSelection(selection,mode);
        if(mode!=0 && mode!=AIS_Shape::SelectionMode(TopAbs_FACE))return;
        Handle(PartSelectionOwner) partOwner;
        NCollection_DataMap<TopoDS_Shape,Handle(PartSelectionOwner),TopTools_ShapeMapHasher> faceOwners;
        for(const auto& entity:selection->Entities()) {
            const auto& sensitive=entity->BaseSensitive();
            const auto original=Handle(StdSelect_BRepOwner)::DownCast(sensitive->OwnerId());
            if(original.IsNull())continue;
            if(mode!=0) {
                if(!faceOwners.IsBound(original->Shape()))faceOwners.Bind(original->Shape(),new PartSelectionOwner(original));
                sensitive->Set(faceOwners.Find(original->Shape()));continue;
            }
            if(original->ComesFromDecomposition())continue;
            if(partOwner.IsNull())partOwner=new PartSelectionOwner(original);
            // Keep OCCT's sensitive geometry, priority, placement and BVH.
            // Only its highlight callback changes.
            sensitive->Set(partOwner);
        }
    }
private:
    struct ContourLines { TopoDS_Shape shape; Quantity_Color color; double width; };
    std::vector<ContourLines> faceContours_;
    std::vector<FaceContour> faceInputs_;
    HLRAlgo_Projector projector_;
    std::vector<TopoDS_Face> filletFaces_,geometricFillets_;
    TopoDS_Shape classifiedShape_;
    bool transitionsOnly_=false;
    bool featureHighlight_=false;
    double featureWidth_=2.8;
    Quantity_Color featureColor_=Quantity_NOC_WHITE;
};
