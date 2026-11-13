#include "SketchProfileBuilder.h"
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Wire.hxx>
#include <BRepCheck_Face.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Geom_Line.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Ellipse.hxx>
#include <GeomAPI.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace SketchProfileBuilder {
namespace {
namespace PV=SketchProfileValidation;
using namespace Sketch;
struct Failure {Diagnostic diagnostic;};
void require(bool ok,Code code,const QString& message,QStringList ids={}) {
    if(!ok) throw Failure{{code,message,std::move(ids),{}, {}}};
}
Point arcPoint(const Arc& a,double angle) {return {a.center.x+a.radius*std::cos(angle),a.center.y+a.radius*std::sin(angle)};}
std::pair<Point,Point> endpoints(const PV::Curve& curve) {
    if(const auto* s=std::get_if<Segment>(&curve.geometry)) return {s->start,s->end};
    if(const auto* a=std::get_if<Arc>(&curve.geometry)) return {arcPoint(*a,a->startAngle),arcPoint(*a,a->startAngle+a->sweepAngle)};
    if(const auto* a=std::get_if<EllipticalArc>(&curve.geometry)) {const auto ends=ellipticalArcEndpoints(*a);return {ends[0],ends[1]};}
    if(const auto* e=std::get_if<Ellipse>(&curve.geometry)) {const auto p=ellipsePoint(*e,0);return {p,p};}
    if(const auto* s=std::get_if<Spline>(&curve.geometry)) return {s->points.front(),s->closed?s->points.front():s->points.back()};
    const auto& c=std::get<Circle>(curve.geometry);const Point p{c.center.x+c.radius,c.center.y};return {p,p};
}
double distance(Point a,Point b) {return std::hypot(a.x-b.x,a.y-b.y);}
QStringList ids(const PV::Contour& contour) {QStringList result;for(const auto& c:contour.curves)result<<c.entityId;return result;}
QStringList key(const PV::Contour& contour) {auto result=ids(contour);result.sort();return result;}
bool resolvable(double value,double tolerance) {
    const double a=std::abs(value);
    return std::isfinite(value) && std::nextafter(a,std::numeric_limits<double>::infinity())-a<=tolerance/16;
}
bool resolvable(const gp_Pnt& p,double tolerance) {return resolvable(p.X(),tolerance)&&resolvable(p.Y(),tolerance)&&resolvable(p.Z(),tolerance);}

// Reuse the existing validator: its public Result can have been edited since
// analysis. Recheck geometry and region membership instead of trusting a flag
// or copying intersection/containment logic into the construction adapter.
PV::Result recheck(const PV::Result& source,std::vector<size_t>& mapping) {
    std::vector<Entity> entities;
    for(const auto& contour:source.contours) for(const auto& curve:contour.curves) {
        Entity e;e.id=curve.entityId;e.workplaneId="profile-builder";
        e.geometry=std::visit([](const auto& g)->Geometry{return g;},curve.geometry);entities.push_back(std::move(e));
    }
    auto checked=PV::validate(entities,"profile-builder",source.connectionTolerance);
    require(checked.status==PV::Status::Valid,Code::InvalidValidation,
        "Profile geometry no longer passes validation: "+(checked.diagnostics.empty()?QString():checked.diagnostics.front().message));
    require(source.contours.size()==checked.contours.size(),Code::InvalidValidation,"Validated contour membership changed.");
    for(const auto& contour:source.contours) {
        const auto keys=key(contour);size_t index=0;
        while(index<checked.contours.size() && key(checked.contours[index])!=keys) ++index;
        require(index<checked.contours.size(),Code::InvalidValidation,"Contour source membership is inconsistent.",keys);
        mapping.push_back(index);
    }
    using RegionKey=std::pair<size_t,std::vector<size_t>>;
    const auto regions=[&](const auto& input,bool remap) {
        std::vector<RegionKey> keys;
        for(const auto& region:input.regions) {
            require(region.outer<input.contours.size(),Code::InvalidValidation,"Region outer contour reference is invalid.");
            RegionKey value{remap?mapping[region.outer]:region.outer,{}};
            for(size_t hole:region.holes) {
                require(hole<input.contours.size(),Code::InvalidValidation,"Region hole reference is invalid.");
                value.second.push_back(remap?mapping[hole]:hole);
            }
            std::sort(value.second.begin(),value.second.end());keys.push_back(std::move(value));
        }
        std::sort(keys.begin(),keys.end());return keys;
    };
    require(regions(source,true)==regions(checked,false),Code::InvalidValidation,"Material regions or direct holes disagree with validated geometry.");
    return checked;
}
void certifyWire(const Wire& wire,double tolerance) {
    BRepCheck_Wire check(wire.shape);
    require(BRep_Tool::IsClosed(wire.shape) && check.Closed()==BRepCheck_NoError
        && check.Orientation(TopoDS_Face())==BRepCheck_NoError && BRepCheck_Analyzer(wire.shape).IsValid(),
        Code::WireFailure,"OCCT wire connectivity, closure, orientation or B-Rep validity failed.");
    size_t count=0;for(TopExp_Explorer it(wire.shape,TopAbs_EDGE);it.More();it.Next())++count;
    require(count==wire.edges.size(),Code::WireFailure,"OCCT wire changed the source edge count.");
    for(size_t i=0;i<wire.edges.size();++i) {
        const auto& edge=wire.edges[i].shape;TopoDS_Vertex a,b,nextA,nextB;
        TopExp::Vertices(edge,a,b,true);TopExp::Vertices(wire.edges[(i+1)%wire.edges.size()].shape,nextA,nextB,true);
        require(!a.IsNull() && !b.IsNull() && b.IsSame(nextA),Code::ClosureFailure,"Wire edges do not share oriented endpoint vertices.",{wire.edges[i].entityId});
        require(BRep_Tool::Tolerance(edge)<=tolerance,Code::ToleranceExceeded,"OCCT increased an edge tolerance beyond the construction limit.",{wire.edges[i].entityId});
        BRepAdaptor_Curve curve(edge);
        require(!BRep_Tool::Degenerated(edge) && (curve.GetType()==GeomAbs_Line || curve.GetType()==GeomAbs_Circle || curve.GetType()==GeomAbs_Ellipse || curve.GetType()==GeomAbs_BSplineCurve),
            Code::EdgeFailure,"An edge is degenerate or lost its analytic line/circle/ellipse support.",{wire.edges[i].entityId});
        const bool reverse=edge.Orientation()==TopAbs_REVERSED;
        require(BRep_Tool::Pnt(a).Distance(curve.Value(reverse?curve.LastParameter():curve.FirstParameter()))<=tolerance
            && BRep_Tool::Pnt(b).Distance(curve.Value(reverse?curve.FirstParameter():curve.LastParameter()))<=tolerance,
            Code::ToleranceExceeded,"An edge endpoint exceeds the approved construction tolerance.",{wire.edges[i].entityId});
    }
    for(TopExp_Explorer it(wire.shape,TopAbs_VERTEX);it.More();it.Next())
        require(BRep_Tool::Tolerance(TopoDS::Vertex(it.Current()))<=tolerance,Code::ToleranceExceeded,"OCCT increased a vertex tolerance beyond the construction limit.");
}
}
Result build(const SketchProfileValidation::Result& profile,const gp_Ax3& frame,Policy policy) {
    Result result;std::optional<size_t> activeContour,activeRegion;
    try {
        const double tolerance=policy.maximumVertexTolerance;
        require(profile.status==PV::Status::Valid && profile.diagnostics.empty() && !profile.contours.empty() && !profile.regions.empty(),
            Code::InvalidValidation,"Construction requires a successful, complete Profile validation result.");
        require(std::isfinite(tolerance) && tolerance>=Precision::Confusion()
            && std::isfinite(profile.connectionTolerance) && profile.connectionTolerance>0 && tolerance<=profile.connectionTolerance,
            Code::InvalidPolicy,"Construction tolerance must be at least OCCT Confusion and may not exceed the recorded Sketch tolerance.");
        require(resolvable(frame.Location(),tolerance),Code::NumericalFailure,"Workplane world coordinates cannot resolve the construction tolerance.");
        const gp_Dir normal(gp_Vec(frame.XDirection()).Crossed(gp_Vec(frame.YDirection())));
        const gp_Ax3 planeFrame(frame.Location(),normal,frame.XDirection());
        const auto world=[&](Point p) {
            auto q=Sketch::worldPoint(p,frame);
            require(resolvable(q,tolerance),Code::NumericalFailure,"World geometry cannot resolve the construction tolerance.");return q;
        };
        std::vector<size_t> mapping;const auto checked=recheck(profile,mapping);
        BRep_Builder builder;
        for(size_t ci=0;ci<profile.contours.size();++ci) {
            activeContour=ci;const auto& contour=profile.contours[ci];const auto sourceIds=ids(contour);Wire wire{ci,{}, {},0};
            std::vector<TopoDS_Vertex> vertices;std::vector<gp_Pnt> starts,ends;
            for(const auto& curve:contour.curves) {
                const auto [a,b]=endpoints(curve);starts.push_back(world(a));ends.push_back(world(b));
                auto vertex=BRepBuilderAPI_MakeVertex(starts.back()).Vertex();builder.UpdateVertex(vertex,tolerance);vertices.push_back(vertex);
                if(const auto* circle=std::get_if<Circle>(&curve.geometry))
                    require(circle->radius>2*tolerance,Code::AmbiguousVertices,"Circle radius is not separated from the vertex tolerance.",sourceIds);
            }
            for(size_t i=0;i<vertices.size();++i) {
                const size_t next=(i+1)%vertices.size();
                const double localGap=distance(endpoints(contour.curves[i]).second,endpoints(contour.curves[next]).first);
                const double worldGap=ends[i].Distance(starts[next]);
                require(localGap<=tolerance && worldGap<=tolerance,Code::ClosureFailure,"Ordered source endpoints exceed the approved closure tolerance.",sourceIds);
                wire.maximumEndpointGap=std::max(wire.maximumEndpointGap,worldGap);
                for(size_t j=0;j<i;++j) require(starts[i].Distance(starts[j])>2*tolerance,
                    Code::AmbiguousVertices,"Distinct junction vertices are not separated by twice the construction tolerance.",sourceIds);
            }
            builder.MakeWire(wire.shape);
            for(size_t i=0;i<contour.curves.size();++i) {
                const auto& g=contour.curves[i].geometry;Handle(Geom_Curve) curve;double first=0,last=0;bool reverse=false;
                if(std::holds_alternative<Segment>(g)) {
                    last=starts[i].Distance(ends[i]);curve=new Geom_Line(starts[i],gp_Dir(gp_Vec(starts[i],ends[i])));
                } else if(const auto* s=std::get_if<Spline>(&g)) {
                    const auto c=splineCurve(*s);curve=GeomAPI::To3d(c,gp_Pln(planeFrame));first=c->FirstParameter();last=c->LastParameter();
                } else if(std::holds_alternative<Ellipse>(g) || std::holds_alternative<EllipticalArc>(g)) {
                    Ellipse e;
                    if(const auto* arc=std::get_if<EllipticalArc>(&g)) {
                        e=arc->ellipse;reverse=arc->sweepParameter<0;
                        first=std::min(arc->startParameter,arc->startParameter+arc->sweepParameter);
                        last=std::max(arc->startParameter,arc->startParameter+arc->sweepParameter);
                    } else {e=std::get<Ellipse>(g);last=2*std::numbers::pi;}
                    require(e.minorRadius>2*tolerance,Code::AmbiguousVertices,"Ellipse minor radius is not separated from the vertex tolerance.",sourceIds);
                    const gp_Vec axis=gp_Vec(frame.XDirection())*std::cos(e.axisAngle)+gp_Vec(frame.YDirection())*std::sin(e.axisAngle);
                    curve=new Geom_Ellipse(gp_Elips(gp_Ax2(world(e.center),normal,gp_Dir(axis)),e.majorRadius,e.minorRadius));
                } else {
                    Point center;double radius;
                    if(const auto* arc=std::get_if<Arc>(&g)) {
                        center=arc->center;radius=arc->radius;reverse=arc->sweepAngle<0;
                        first=std::min(arc->startAngle,arc->startAngle+arc->sweepAngle);
                        last=std::max(arc->startAngle,arc->startAngle+arc->sweepAngle);
                    } else {const auto& circle=std::get<Circle>(g);center=circle.center;radius=circle.radius;last=2*std::numbers::pi;}
                    curve=new Geom_Circle(gp_Circ(gp_Ax2(world(center),normal,frame.XDirection()),radius));
                }
                auto a=vertices[i],b=vertices[(i+1)%vertices.size()];if(reverse)std::swap(a,b);
                a.Orientation(TopAbs_FORWARD);b.Orientation(TopAbs_REVERSED);
                TopoDS_Edge edge;builder.MakeEdge(edge,curve,Precision::Confusion());builder.Range(edge,first,last);
                builder.Add(edge,a);builder.Add(edge,b);builder.UpdateVertex(a,first,edge,tolerance);builder.UpdateVertex(b,last,edge,tolerance);
                if(reverse)edge.Reverse();builder.Add(wire.shape,edge);wire.edges.push_back({contour.curves[i].entityId,edge});
            }
            certifyWire(wire,tolerance);result.wires.push_back(std::move(wire));
        }
        activeContour.reset();
        for(size_t ri=0;ri<profile.regions.size();++ri) {
            activeRegion=ri;const auto& region=profile.regions[ri];
            const auto oriented=[&](size_t ci,bool outer) {
                auto wire=result.wires[ci].shape;
                const bool ccw=checked.contours[mapping[ci]].signedArea>0;
                if(ccw!=outer)wire.Reverse();return wire;
            };
            BRepBuilderAPI_MakeFace make(gp_Pln(planeFrame),oriented(region.outer,true),true);
            for(size_t hole:region.holes) make.Add(oriented(hole,false));
            require(make.IsDone(),Code::FaceFailure,"OCCT could not build the planar region face.");
            Face face{ri,region.outer,region.holes,{},make.Face(),0};
            long double expected=std::abs(checked.contours[mapping[region.outer]].signedArea);double perimeter=0;
            std::vector<size_t> members{region.outer};members.insert(members.end(),region.holes.begin(),region.holes.end());
            for(size_t ci:members) {
                face.entityIds.append(ids(profile.contours[ci]));
                if(ci!=region.outer)expected-=std::abs(checked.contours[mapping[ci]].signedArea);
                GProp_GProps length;BRepGProp::LinearProperties(result.wires[ci].shape,length);perimeter+=length.Mass();
                certifyWire(result.wires[ci],tolerance);
            }
            BRepCheck_Face check(face.shape);
            require(BRepCheck_Analyzer(face.shape).IsValid() && check.IntersectWires()==BRepCheck_NoError
                && check.ClassifyWires()==BRepCheck_NoError && check.OrientationOfWires()==BRepCheck_NoError,
                Code::FaceFailure,"Planar face validity, wire nesting or hole orientation failed.",face.entityIds);
            require(BRep_Tool::Tolerance(face.shape)<=tolerance,Code::ToleranceExceeded,"OCCT face tolerance exceeds the construction limit.");
            require(BRepTools::OuterWire(face.shape).IsSame(result.wires[region.outer].shape),Code::FaceFailure,"OCCT changed the outer boundary identity.");
            size_t wireCount=0;
            for(TopExp_Explorer it(face.shape,TopAbs_WIRE);it.More();it.Next()) {
                ++wireCount;
                require(std::any_of(members.begin(),members.end(),[&](size_t ci){return it.Current().IsSame(result.wires[ci].shape);}),
                    Code::FaceFailure,"OCCT replaced a source boundary during face construction.",face.entityIds);
                BRepCheck_Wire wc(TopoDS::Wire(it.Current()));
                require(wc.Closed()==BRepCheck_NoError && wc.Closed2d(face.shape)==BRepCheck_NoError
                    && wc.Orientation(face.shape)==BRepCheck_NoError,Code::WireFailure,"Face boundary is not closed and correctly oriented in UV.",face.entityIds);
            }
            require(wireCount==members.size(),Code::FaceFailure,"Face boundary count changed; a hole or region was lost.",face.entityIds);
            const BRepAdaptor_Surface surface(face.shape);
            require(surface.GetType()==GeomAbs_Plane && surface.Plane().Distance(frame.Location())<=tolerance
                && surface.Plane().Axis().Direction().IsParallel(normal,1.e-12),Code::FaceFailure,"Constructed face is not on the requested Workplane.");
            GProp_GProps area;BRepGProp::SurfaceProperties(face.shape,area);face.area=area.Mass();
            const long double areaTolerance=8*tolerance*perimeter+256*std::numeric_limits<double>::epsilon()*std::abs(expected);
            require(std::isfinite(face.area) && face.area>0 && expected>0 && std::abs(face.area-expected)<=areaTolerance,
                Code::AreaMismatch,"Constructed face area disagrees with the validated material region.",face.entityIds);
            result.faces.push_back(std::move(face));
        }
        result.effectiveVertexTolerance=tolerance;result.success=true;return result;
    } catch(const Failure& failure) {
        auto diagnostic=failure.diagnostic;diagnostic.contour=activeContour;diagnostic.region=activeRegion;
        Result failed;failed.diagnostics.push_back(std::move(diagnostic));return failed;
    } catch(const Standard_Failure& failure) {
        Result failed;failed.diagnostics.push_back({Code::NumericalFailure,
            "OCCT construction failed: "+QString::fromUtf8(failure.GetMessageString()),{},activeContour,activeRegion});return failed;
    } catch(const std::exception& failure) {
        Result failed;failed.diagnostics.push_back({Code::NumericalFailure,
            "Profile construction failed: "+QString::fromUtf8(failure.what()),{},activeContour,activeRegion});return failed;
    }
}
}
