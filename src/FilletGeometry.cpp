#include "FilletGeometry.h"
#include "EdgeBlendValidation.h"
#include <BRepFilletAPI_MakeFillet.hxx>
TopoDS_Shape FilletGeometry::apply(const TopoDS_Shape& source,const TopoDS_Edge& edge,
                                  double distance,QString& error) {
    error.clear();
    try {
        TopTools_IndexedDataMapOfShapeListOfShape adjacent;
        if(!EdgeBlendValidation::input(source,edge,distance,adjacent,error)) return {};
        BRepBuilderAPI_Copy copy(source,Standard_True,Standard_False);
        const auto copiedEdge=TopoDS::Edge(copy.ModifiedShape(edge));
        BRepFilletAPI_MakeFillet builder(copy.Shape());
        builder.Add(distance,copiedEdge);
        builder.Build();
        if(!builder.IsDone()) {error=QStringLiteral("Fillet could not be built. Try a smaller value or another edge.");return {};}
        const auto result=builder.Shape();
        double volume=0;
        if(!BRepReconstruction::validSolid(result,volume)) {
            error=QStringLiteral("Fillet produced invalid or non-solid topology. Try a smaller value.");return {};
        }
        return result;
    } catch(const Standard_Failure& failure) {
        error=QStringLiteral("Fillet: Open CASCADE failed: %1").arg(QString::fromUtf8(failure.GetMessageString()));
        return {};
    }
}
