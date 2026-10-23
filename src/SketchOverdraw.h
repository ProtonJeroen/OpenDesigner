#pragma once
#include "SketchSnapping.h"

namespace Sketch {
// Evaluated command data only. Source IDs never enter the resulting profiles.
struct OverdrawPiece {
    Geometry geometry;
    size_t start=0,end=0;
    QString sourceId;
};
struct OverdrawGraph {
    std::vector<Point> nodes;
    std::vector<OverdrawPiece> pieces;
    QString notice;
};
bool overdrawCandidates(const std::vector<Entity>& entities,const QString& workplaneId,
                        OverdrawGraph& result,QString& error,double tolerance=1.e-7);
Geometry directedPiece(const OverdrawPiece& piece,size_t from);
// Multiple entries mean ambiguity: the UI must explicitly choose, not take front().
std::vector<size_t> overdrawHits(const OverdrawGraph& graph,size_t from,
    const std::vector<size_t>& selected,Point cursor,Point screenCursor,double pixelRadius,const Projector& project);
}
