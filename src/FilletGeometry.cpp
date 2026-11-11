#include "FilletGeometry.h"
#include "EdgeBlendValidation.h"
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopoDS_Wire.hxx>
#include <vector>
#include <TopExp_Explorer.hxx>
namespace {
bool withinTolerance(const TopoDS_Shape& shape) {
    for(auto type:{TopAbs_FACE,TopAbs_EDGE,TopAbs_VERTEX}) for(TopExp_Explorer it(shape,type);it.More();it.Next()) {
        const double t=type==TopAbs_FACE?BRep_Tool::Tolerance(TopoDS::Face(it.Current())):
            type==TopAbs_EDGE?BRep_Tool::Tolerance(TopoDS::Edge(it.Current())):BRep_Tool::Tolerance(TopoDS::Vertex(it.Current()));
        if(!std::isfinite(t)||t>1.e-6) return false;
    }
    return true;
}
}
TopoDS_Shape FilletGeometry::apply(const TopoDS_Shape& source,const TopoDS_Edge& edge,
                                  double distance,QString& error) {
    error.clear();
    try {
        TopTools_IndexedDataMapOfShapeListOfShape adjacent;
        if(!EdgeBlendValidation::input(source,edge,distance,adjacent,error)) return {};
        BRepAdaptor_Curve selected(edge);
        std::vector<TopoDS_Edge> contour{edge};
        if(selected.GetType()!=GeomAbs_Line&&selected.GetType()!=GeomAbs_Circle) {
            error=QStringLiteral("Fillet supports straight edges and certified circular contours only.");return {};
        }
        if(selected.GetType()==GeomAbs_Circle) {
            if(!withinTolerance(source)) {error=QStringLiteral("Circular Fillet source exceeds the existing 1e-6 tolerance limit.");return {};}
            contour.clear();double sweep=0;const auto circle=selected.Circle();
            for(int i=1;i<=adjacent.Extent();++i) {
                auto candidate=TopoDS::Edge(adjacent.FindKey(i));BRepAdaptor_Curve curve(candidate);
                if(curve.GetType()!=GeomAbs_Circle) continue;
                auto c=curve.Circle();
                if(c.Location().Distance(circle.Location())>1.e-6||std::abs(c.Radius()-circle.Radius())>1.e-6
                   ||!c.Axis().Direction().IsParallel(circle.Axis().Direction(),1.e-8)) continue;
                const auto& faces=adjacent.FindFromIndex(i);
                if(BRep_Tool::Degenerated(candidate)||faces.Extent()!=2) {
                    error=QStringLiteral("Circular contour contains a degenerate or non-manifold edge.");return {};
                }
                for(const auto& face:faces) if(BRep_Tool::IsClosed(candidate,TopoDS::Face(face))) {
                    error=QStringLiteral("Circular contour contains a seam edge.");return {};
                }
                contour.push_back(candidate);sweep+=curve.LastParameter()-curve.FirstParameter();
            }
            TopTools_ListOfShape members;for(const auto& e:contour) members.Append(e);
            BRepBuilderAPI_MakeWire wire;wire.Add(members);
            if(!wire.IsDone()||!BRep_Tool::IsClosed(wire.Wire())||!BRepCheck_Analyzer(wire.Wire()).IsValid()
               ||std::abs(sweep-2*std::acos(-1.))>1.e-8) {
                error=QStringLiteral("Circular Fillet requires one complete unambiguous circular contour.");return {};
            }
        }
        BRepBuilderAPI_Copy copy(source,Standard_True,Standard_False);

        BRepFilletAPI_MakeFillet builder(copy.Shape());
        for(const auto& member:contour) {
            const auto copiedEdge=TopoDS::Edge(copy.ModifiedShape(member));
            if(builder.Contour(copiedEdge)==0) builder.Add(distance,copiedEdge);
        }
        builder.Build();
        if(!builder.IsDone()) {error=QStringLiteral("Fillet could not be built. Try a smaller value or another edge.");return {};}
        const auto result=builder.Shape();
        double volume=0;
        if(!BRepReconstruction::validSolid(result,volume)
           ||(selected.GetType()==GeomAbs_Circle&&!withinTolerance(result))) {
            error=QStringLiteral("Fillet produced invalid or non-solid topology. Try a smaller value.");return {};
        }
        return result;
    } catch(const Standard_Failure& failure) {
        error=QStringLiteral("Fillet: Open CASCADE failed: %1").arg(QString::fromUtf8(failure.GetMessageString()));
        return {};
    }
}
