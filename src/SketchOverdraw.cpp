#include "SketchOverdraw.h"
#include "SketchCurveOperations.h"
#include <algorithm>
#include <cmath>
#include <QStringList>
#include <stdexcept>

namespace Sketch {
namespace {
namespace C=SketchCurveOperations;
double distance(Point a,Point b) {return std::hypot(a.x-b.x,a.y-b.y);}
}
bool overdrawCandidates(const std::vector<Entity>& entities,const QString& workplaneId,OverdrawGraph& result,QString& error,double tolerance) {
    result={};error.clear();
    if(workplaneId.isEmpty() || !std::isfinite(tolerance) || tolerance<=0) {error="Invalid Overdraw Workplane or tolerance.";return false;}
    std::vector<const Entity*> sources,profiles;std::vector<std::vector<Point>> cuts;
    QStringList identities;
    for(const auto& e:entities) if(e.workplaneId==workplaneId) {
        if(std::holds_alternative<ConstructionPoint>(e.geometry)) continue;
        if(e.role==Role::Construction && (!e.visible || !e.selectable)) continue;
        if(identities.contains(e.id)) {error="Overdraw: duplicate source identity.";return false;}identities<<e.id;
        if(!valid(e,error)) {error="Overdraw: invalid source. "+error;return false;}
        if(e.role==Role::Profile) profiles.push_back(&e);
        else {sources.push_back(&e);cuts.emplace_back();}
    }
    const auto compare=[&](size_t i,const Entity& other,std::optional<size_t> j) {
        const auto relation=C::supportRelation(sources[i]->geometry,other.geometry,tolerance);
        if(relation.status!=C::Status::Success) {error="Overdraw support comparison: "+relation.message;return false;}
        if(relation.common) {
            cuts[i].insert(cuts[i].end(),relation.boundaries.begin(),relation.boundaries.end());
            if(j) cuts[*j].insert(cuts[*j].end(),relation.boundaries.begin(),relation.boundaries.end());
            return true;
        }
        std::vector<Point> points;
        // Retain the established analytic line/circle solver and its numerical
        // range; all advanced pairs use the shared certified OCCT layer.
        if(supportedSketchToolCurve(sources[i]->geometry) && supportedSketchToolCurve(other.geometry)) {
            const auto hits=intersect(sources[i]->geometry,other.geometry,tolerance);
            if(hits.kind!=IntersectionKind::Finite && hits.kind!=IntersectionKind::None) {
                error="Overdraw: coincident/overlapping or unresolved supports have no certified equivalence.";return false;
            }
            points=hits.points;
        } else {
            const auto hits=C::intersect(sources[i]->geometry,other.geometry,tolerance);
            if(hits.status!=C::Status::Success) {error="Overdraw: ambiguous/overlapping geometry. "+hits.message;return false;}
            for(const auto& hit:hits.hits) points.push_back(hit.point);
        }
        // Profiles are reuse masks, not extra crossing boundaries. Hidden or
        // locked existing Profiles still prevent duplicate persisted geometry.
        if(j) for(auto p:points) {cuts[i].push_back(p);cuts[*j].push_back(p);}
        return true;
    };
    for(size_t i=0;i<sources.size();++i) {
        for(size_t j=i+1;j<sources.size();++j) if(!compare(i,*sources[j],j)) return false;
        for(const auto* profile:profiles) if(!compare(i,*profile,{})) return false;
    }
    OverdrawGraph graph;std::vector<std::vector<Point>> nodeMembers;QStringList notices;int reused=0,duplicates=0;
    const auto node=[&](Point p)->std::optional<size_t> {
        std::optional<size_t> found;
        for(size_t i=0;i<graph.nodes.size();++i) if(distance(p,graph.nodes[i])<=tolerance) {
            if(found) {error="Overdraw endpoint matches multiple distinct nodes; no transitive tolerance bridge is allowed.";return {};}
            found=i;
        }
        if(found) {
            for(auto member:nodeMembers[*found]) if(distance(member,p)>tolerance) {error="Overdraw endpoint cluster exceeds geometric tolerance; no implicit repair was applied.";return {};}
            nodeMembers[*found].push_back(p);return found;
        }
        graph.nodes.push_back(p);nodeMembers.push_back({p});return graph.nodes.size()-1;
    };
    for(size_t i=0;i<sources.size();++i) {
        const auto pieces=C::split(sources[i]->geometry,cuts[i],tolerance);
        if(pieces.status!=C::Status::Success) {error="Overdraw: "+pieces.message;return false;}
        if(!pieces.notice.isEmpty() && !notices.contains(pieces.notice)) notices<<pieces.notice;
        for(const auto& candidate:pieces.pieces) {
            bool duplicate=false;
            for(const auto& piece:graph.pieces) {
                const auto relation=C::supportRelation(piece.geometry,candidate.geometry,tolerance);
                if(relation.status!=C::Status::Success) {error="Overdraw piece comparison: "+relation.message;return false;}
                if(relation.common && relation.firstContainsSecond && relation.secondContainsFirst) {duplicate=true;++duplicates;break;}
            }
            if(duplicate) continue;
            QString existing;
            for(const auto* profile:profiles) {
                const auto relation=C::supportRelation(candidate.geometry,profile->geometry,tolerance);
                if(relation.status!=C::Status::Success) {error="Overdraw Profile reuse: "+relation.message;return false;}
                if(relation.common && relation.secondContainsFirst && (existing.isEmpty() || profile->id<existing)) existing=profile->id;
            }
            const auto first=node(candidate.start),last=node(candidate.end);
            if(!first || !last) return false;
            if(*first==*last) {error="Overdraw piece collapses to one unresolved boundary node.";return false;}
            graph.pieces.push_back({candidate.geometry,*first,*last,sources[i]->id,existing});
            if(!existing.isEmpty()) ++reused;
        }
    }
    if(duplicates) notices<<QString("%1 coincident construction piece(s) reused in the graph.").arg(duplicates);
    if(reused) notices<<QString("%1 piece(s) reuse existing Profile entities unchanged, including any extent outside the selected path. No duplicate Profiles will be added.").arg(reused);
    if(std::any_of(graph.pieces.begin(),graph.pieces.end(),[](const auto& p){const auto* s=std::get_if<BSpline>(&p.geometry);return s && (s->degree>3 || std::any_of(s->weights.begin(),s->weights.end(),[&](double w){return w!=s->weights.front();}));}))
        notices<<"Exact rational or higher-degree B-spline Sketch pieces are available; Profile Builder retains its existing explicit certification limits.";
    graph.notice=notices.join('\n');
    if(graph.pieces.empty()) {error="No finite Overdraw pieces. Use finite endpoints or add intersecting construction geometry.";if(!graph.notice.isEmpty())error+='\n'+graph.notice;return false;}
    result=std::move(graph);return true;
}
Geometry directedPiece(const OverdrawPiece& piece,size_t from) {
    if(from!=piece.start && from!=piece.end) throw std::invalid_argument("Overdraw piece is not incident to the current endpoint.");
    return from==piece.start?piece.geometry:C::reversed(piece.geometry);
}
std::vector<size_t> overdrawHits(const OverdrawGraph& graph,size_t from,const std::vector<size_t>& selected,Point cursor,Point screenCursor,double pixelRadius,const Projector& project) {
    std::vector<size_t> hits;
    if(from>=graph.nodes.size() || !std::isfinite(pixelRadius) || pixelRadius<=0) return hits;
    for(size_t i=0;i<graph.pieces.size();++i) {
        const auto& piece=graph.pieces[i];
        if((piece.start!=from && piece.end!=from) || std::find(selected.begin(),selected.end(),i)!=selected.end()) continue;
        if(const auto point=nearestPoint(directedPiece(piece,from),cursor);point && distance(project(*point),screenCursor)<=pixelRadius) hits.push_back(i);
    }
    return hits;
}
}
