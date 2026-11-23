#include "BRepTiming.h"
#include "BRepTopology.h"
#include <BRepTools.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <utility>

namespace BRepReconstruction {
bool TopologyIndex::build(const TopoDS_Shape& shape, TopologyIndex& result, QString& error) {
    BRepTiming::Scope timingScope("BRepTopology.TopologyIndex::build");
    result={};error.clear();
    if(shape.IsNull()) {error=QStringLiteral("Cannot index a null B-Rep.");return false;}
    try {
        TopologyIndex index;
        TopExp::MapShapes(shape,TopAbs_FACE,index.faceMap_);
        TopExp::MapShapes(shape,TopAbs_EDGE,index.edgeMap_);
        for(int i=1;i<=index.edgeMap_.Extent();++i)
            index.edges_.push_back({TopoDS::Edge(index.edgeMap_(i)),{}, {}});
        for(int i=1;i<=index.faceMap_.Extent();++i) {
            const auto owner=TopoDS::Face(index.faceMap_(i));
            Face face(owner);
            const auto outer=BRepTools::OuterWire(owner);
            for(TopExp_Explorer w(owner,TopAbs_WIRE);w.More();w.Next()) {
                const auto wire=TopoDS::Wire(w.Current());
                Wire record{wire,!outer.IsNull()&&wire.IsSame(outer),{}};
                // Do not deduplicate edge occurrences: continuity and seam
                // analysis need each oriented face/wire use and its pcurve.
                for(TopExp_Explorer e(wire,TopAbs_EDGE);e.More();e.Next()) {
                    const auto use=TopoDS::Edge(e.Current());
                    auto& edge=index.edges_.at(index.edgeMap_.FindIndex(use)-1);
                    edge.uses.push_back({i-1,int(face.wires.size()),use});
                    if(std::none_of(edge.faces.begin(),edge.faces.end(),[&](const auto& f){return f.IsSame(owner);}))
                        edge.faces.push_back(owner);
                    record.edges.push_back(use);
                }
                face.wires.push_back(std::move(record));
            }
            index.faces_.push_back(std::move(face));
        }
        result=std::move(index);return true;
    } catch(const Standard_Failure&) {
        error=QStringLiteral("OCCT could not index the current B-Rep supports and trims.");return false;
    }
}
const TopologyIndex::Face* TopologyIndex::face(const TopoDS_Face& owner) const {
    const int i=faceMap_.FindIndex(owner);return i?&faces_[i-1]:nullptr;
}
const TopologyIndex::Edge* TopologyIndex::edge(const TopoDS_Edge& owner) const {
    const int i=edgeMap_.FindIndex(owner);return i?&edges_[i-1]:nullptr;
}
std::vector<TopoDS_Face> TopologyIndex::neighbors(const TopoDS_Face& owner) const {
    std::vector<TopoDS_Face> result;
    const auto* record=face(owner);if(!record) return result;
    for(const auto& wire:record->wires) for(const auto& use:wire.edges)
        for(const auto& other:edge(use)->faces)
            if(!other.IsSame(owner)&&std::none_of(result.begin(),result.end(),[&](const auto& f){return f.IsSame(other);}))
                result.push_back(other);
    return result;
}
}
