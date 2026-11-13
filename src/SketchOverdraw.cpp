#include "SketchOverdraw.h"
#include <algorithm>
#include <cmath>
#include <QStringList>
#include <stdexcept>

namespace Sketch {
namespace {
constexpr double turn=6.283185307179586476925286766559;
double distance(Point a,Point b) {return std::hypot(a.x-b.x,a.y-b.y);}
double positive(double angle) {angle=std::fmod(angle,turn);return angle<0?angle+turn:angle;}
Point onCircle(Point center,double radius,double angle) {return {center.x+radius*std::cos(angle),center.y+radius*std::sin(angle)};}
}
bool overdrawCandidates(const std::vector<Entity>& entities,const QString& workplaneId,OverdrawGraph& result,QString& error,double tolerance) {
    result={};error.clear();
    if(workplaneId.isEmpty() || !std::isfinite(tolerance) || tolerance<=0) {error="Invalid Overdraw Workplane or tolerance.";return false;}
    std::vector<const Entity*> sources;std::vector<std::vector<Point>> cuts;int unsupportedConics=0;
    for(const auto& e:entities) if(e.workplaneId==workplaneId && e.role==Role::Construction && e.visible && e.selectable) {
        if(std::holds_alternative<ConstructionPoint>(e.geometry)) continue;
        if(!valid(e,error)) {error="Overdraw: invalid construction source. "+error;return false;}
        if(!supportedSketchToolCurve(e.geometry)) {++unsupportedConics;continue;}
        if(std::any_of(sources.begin(),sources.end(),[&](const auto* s){return s->id==e.id;})) {error="Overdraw: duplicate source identity.";return false;}
        sources.push_back(&e);cuts.emplace_back();
        if(const auto* s=std::get_if<Segment>(&e.geometry)) cuts.back()={s->start,s->end};
        if(const auto* a=std::get_if<Arc>(&e.geometry)) cuts.back()={onCircle(a->center,a->radius,a->startAngle),onCircle(a->center,a->radius,a->startAngle+a->sweepAngle)};
    }
    for(size_t i=0;i<sources.size();++i) for(size_t j=i+1;j<sources.size();++j) {
        const auto intersections=intersect(sources[i]->geometry,sources[j]->geometry,tolerance);
        if(intersections.kind==IntersectionKind::Coincident) {
            error="Overdraw cannot resolve coincident/overlapping construction curves. Hide or remove the redundant sources first.";return false;
        }
        for(auto p:intersections.points) {cuts[i].push_back(p);cuts[j].push_back(p);}
    }
    // Build locally so errors never expose a partially usable candidate graph.
    OverdrawGraph graph;int unbounded=0,uncutCircles=0,small=0;
    const auto node=[&](Point p) {
        for(size_t i=0;i<graph.nodes.size();++i) if(distance(p,graph.nodes[i])<=tolerance) return i;
        graph.nodes.push_back(p);return graph.nodes.size()-1;
    };
    const auto append=[&](Geometry geometry,Point start,Point end,const QString& source) {
        if(distance(start,end)<=tolerance) {++small;return;}
        Entity candidate;candidate.workplaneId=workplaneId;candidate.geometry=geometry;
        QString why;if(!valid(candidate,why)) {++small;return;}
        const auto first=node(start),last=node(end);
        if(first==last) {++small;return;}
        graph.pieces.push_back({std::move(geometry),first,last,source});
    };
    for(size_t i=0;i<sources.size();++i) {
        const auto& source=*sources[i];
        if(const auto* circle=std::get_if<Circle>(&source.geometry)) {
            std::vector<double> angles;
            for(auto p:cuts[i]) angles.push_back(positive(std::atan2(p.y-circle->center.y,p.x-circle->center.x)));
            std::sort(angles.begin(),angles.end());
            std::vector<double> unique;
            for(auto a:angles) if(unique.empty() || (a-unique.back())*circle->radius>tolerance) unique.push_back(a);
            if(unique.size()>1 && (turn-unique.back()+unique.front())*circle->radius<=tolerance) unique.pop_back();
            if(unique.size()<2) {++uncutCircles;continue;}
            for(size_t n=0;n<unique.size();++n) {
                const double start=unique[n],end=n+1<unique.size()?unique[n+1]:unique[0]+turn;
                append(Arc{circle->center,circle->radius,start,end-start},onCircle(circle->center,circle->radius,start),onCircle(circle->center,circle->radius,end),source.id);
            }
        } else if(const auto* arc=std::get_if<Arc>(&source.geometry)) {
            const double sign=arc->sweepAngle>0?1:-1,extent=std::abs(arc->sweepAngle);
            std::vector<double> parameters{0,extent};
            for(auto p:cuts[i]) {
                double t=positive(sign*(std::atan2(p.y-arc->center.y,p.x-arc->center.x)-arc->startAngle));
                if((turn-t)*arc->radius<=tolerance) t=0;
                if(t<=extent+tolerance/arc->radius) parameters.push_back(std::clamp(t,0.0,extent));
            }
            std::sort(parameters.begin(),parameters.end());
            std::vector<double> unique;
            for(auto t:parameters) if(unique.empty() || (t-unique.back())*arc->radius>tolerance) unique.push_back(t);
            for(size_t n=1;n<unique.size();++n) {
                const double start=arc->startAngle+sign*unique[n-1],sweep=sign*(unique[n]-unique[n-1]);
                append(Arc{arc->center,arc->radius,start,sweep},onCircle(arc->center,arc->radius,start),onCircle(arc->center,arc->radius,start+sweep),source.id);
            }
        } else {
            Point origin,direction;
            if(const auto* s=std::get_if<Segment>(&source.geometry)) {
                origin=s->start;const double length=distance(s->start,s->end);direction={(s->end.x-origin.x)/length,(s->end.y-origin.y)/length};
            } else {const auto& l=std::get<InfiniteLine>(source.geometry);origin=l.origin;direction=l.direction;unbounded+=2;}
            auto points=cuts[i];const auto parameter=[&](Point p){return (p.x-origin.x)*direction.x+(p.y-origin.y)*direction.y;};
            std::sort(points.begin(),points.end(),[&](auto a,auto b){return parameter(a)<parameter(b);});
            std::vector<Point> unique;
            for(auto p:points) if(unique.empty() || distance(p,unique.back())>tolerance) unique.push_back(p);
            for(size_t n=1;n<unique.size();++n) append(Segment{unique[n-1],unique[n]},unique[n-1],unique[n],source.id);
        }
    }
    QStringList notices;
    if(unsupportedConics) notices<<QString("%1 ellipse/elliptical arc/spline source(s) excluded: elliptic Overdraw and spline Overdraw are not supported.").arg(unsupportedConics);
    if(unbounded) notices<<QString("%1 unbounded line tails excluded; Workplane borders are not geometric limits.").arg(unbounded);
    if(uncutCircles) notices<<QString("%1 circle(s) need at least two distinct intersection points; full circles are not converted to arcs.").arg(uncutCircles);
    if(small) notices<<QString("%1 tolerance-sized piece(s) excluded.").arg(small);
    graph.notice=notices.join('\n');
    if(graph.pieces.empty()) {error="No finite Overdraw pieces. Use finite endpoints or add intersecting construction geometry.";if(!graph.notice.isEmpty()) error+='\n'+graph.notice;return false;}
    for(const auto& piece:graph.pieces) {
        const auto original=std::find_if(sources.begin(),sources.end(),[&](const auto* e){return e->id==piece.sourceId;});
        for(auto index:{piece.start,piece.end}) if(!contains(piece.geometry,graph.nodes[index],tolerance)
            || original==sources.end() || !contains((*original)->geometry,graph.nodes[index],tolerance)) {
            error="Overdraw boundaries cannot be resolved within the geometric tolerance. No pieces were created.";return false;
        }
    }
    result=std::move(graph);return true;
}
Geometry directedPiece(const OverdrawPiece& piece,size_t from) {
    if(!std::holds_alternative<Segment>(piece.geometry) && !std::holds_alternative<Arc>(piece.geometry))
        throw std::invalid_argument("Overdraw requires a bounded segment or arc.");
    if(from!=piece.start && from!=piece.end) throw std::invalid_argument("Overdraw piece is not incident to the current endpoint.");
    if(from==piece.start) return piece.geometry;
    if(const auto* s=std::get_if<Segment>(&piece.geometry)) return Segment{s->end,s->start};
    if(const auto* arc=std::get_if<Arc>(&piece.geometry)) return reversedArc(*arc);
    throw std::invalid_argument("Overdraw requires a bounded segment or arc.");
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
