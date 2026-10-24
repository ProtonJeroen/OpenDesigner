#include "SketchProjection.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRep_Tool.hxx>
#include <Precision.hxx>
#include <QSet>
#include <Standard_Failure.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace SketchProjection {
namespace {
using Sketch::Point;
using Sketch::Segment;
using Sketch::ConstructionPoint;
constexpr double turn=2*std::numbers::pi;
constexpr double angularRoundoff=64*std::numeric_limits<double>::epsilon();
struct Failure {Code code;QString message;};
void require(bool ok,Code code,const char* message) {if(!ok) throw Failure{code,QString::fromLatin1(message)};}
bool finite(const gp_Pnt& p) {return std::isfinite(p.X()) && std::isfinite(p.Y()) && std::isfinite(p.Z());}
bool finite(const gp_Trsf& transform) {
    for(int r=1;r<=3;++r) for(int c=1;c<=4;++c) if(!std::isfinite(transform.Value(r,c))) return false;
    return std::isfinite(transform.ScaleFactor()) && transform.ScaleFactor()!=0;
}
Point local(const gp_Pnt& p,const gp_Trsf& placement,const gp_Ax3& frame,double tolerance) {
    require(finite(p),Code::NumericalFailure,"Source coordinates are not finite.");
    std::array<long double,3> relative{};
    for(int r=1;r<=3;++r) {
        // Cancel common translations BEFORE adding small local coordinates.
        // An intermediate gp_Pnt in absolute world space would lose them.
        relative[size_t(r-1)]=static_cast<long double>(placement.Value(r,4))-frame.Location().Coord(r);
        for(int c=1;c<=3;++c) relative[size_t(r-1)]+=static_cast<long double>(placement.Value(r,c))*p.Coord(c);
    }
    const auto coordinate=[&](const gp_Dir& axis) {
        long double value=0;
        for(int i=1;i<=3;++i) value+=relative[size_t(i-1)]*axis.Coord(i);
        const double stored=double(value);
        require(std::isfinite(stored) && std::abs(value-static_cast<long double>(stored))<=tolerance,
                Code::NumericalFailure,"Projected coordinates cannot be stored within the geometric tolerance.");
        return stored;
    };
    // Dropping the normal component is orthogonal projection onto frame XY.
    // extent/displayOffset are presentation data, not part of this calculation.
    return {coordinate(frame.XDirection()),coordinate(frame.YDirection())};
}
// A direction has no translation. Normalize only the external uniform scale;
// the adaptor has already applied the edge/curve locations to the circle frame.
std::array<long double,3> localDirection(const gp_Dir& direction,const gp_Trsf& placement,const gp_Ax3& frame) {
    std::array<long double,3> world{},result{};
    for(int r=1;r<=3;++r) for(int c=1;c<=3;++c)
        world[size_t(r-1)]+=static_cast<long double>(placement.Value(r,c))*direction.Coord(c)/std::abs(placement.ScaleFactor());
    const std::array axes{frame.XDirection(),frame.YDirection(),frame.Direction()};
    for(size_t i=0;i<3;++i) for(int r=1;r<=3;++r) result[i]+=world[size_t(r-1)]*axes[i].Coord(r);
    return result;
}
long double normalized(long double angle) {angle=std::fmod(angle,static_cast<long double>(turn));return angle<0?angle+turn:angle;}
Sketch::Geometry circularGeometry(const Source& source,const BRepAdaptor_Curve& curve,const gp_Ax3& frame,double tolerance) {
    const auto circle=curve.Circle();const auto center=local(circle.Location(),source.partPlacement,frame,tolerance);
    const long double exactRadius=static_cast<long double>(circle.Radius())*std::abs(source.partPlacement.ScaleFactor());
    const double radius=double(exactRadius);
    require(std::isfinite(radius) && std::abs(exactRadius-radius)<=tolerance,Code::NumericalFailure,"Projected circle radius cannot be stored within tolerance.");
    require(radius>tolerance,Code::DegenerateEdge,"The placed circle has zero or tolerance-sized radius.");
    const auto u=localDirection(circle.XAxis().Direction(),source.partPlacement,frame);
    const auto v=localDirection(circle.YAxis().Direction(),source.partPlacement,frame);
    // Only floating-point angular roundoff is allowed here. A user-supplied
    // linear tolerance must never turn an oblique ellipse into a circle.
    require(std::abs(u[2])<=angularRoundoff && std::abs(v[2])<=angularRoundoff,Code::NonCircularProjection,
            "The circular edge is not parallel to the Workplane; its projection is an ellipse or a collapsed curve, which is not supported.");
    require((u[2]*u[2]+v[2]*v[2])*exactRadius<=tolerance
         && std::abs(std::hypot(u[0],u[1])-1)*exactRadius<=tolerance
         && std::abs(std::hypot(v[0],v[1])-1)*exactRadius<=tolerance
         && std::abs(u[0]*v[0]+u[1]*v[1])*exactRadius<=tolerance,Code::NumericalFailure,
            "The circular projection cannot be represented within the geometric tolerance.");
    const double first=curve.FirstParameter(),last=curve.LastParameter(),span=last-first;
    // A nearly full arc stays an arc. Do not use the linear model tolerance
    // (or merely coincident B-Rep endpoints) to classify a complete period.
    if(last==first+curve.Period()) return Sketch::Circle{center,radius};
    require(span<turn,Code::UnsupportedCircularSpan,"The circular edge is not a single full circle or a bounded arc shorter than one turn.");
    require(span>1.e-12 && exactRadius*span>tolerance,Code::DegenerateEdge,"The circular arc has zero or tolerance-sized span.");
    const bool reversed=source.shape.Orientation()==TopAbs_REVERSED;
    const double parameter=reversed?last:first;
    const long double cosine=std::cos(static_cast<long double>(parameter)),sine=std::sin(static_cast<long double>(parameter));
    const auto start=double(normalized(std::atan2(u[1]*cosine+v[1]*sine,u[0]*cosine+v[0]*sine)));
    const int handedness=u[0]*v[1]-u[1]*v[0]<0?-1:1;
    Sketch::Arc arc{center,radius,start,span*handedness*(reversed?-1:1)};
    // Guard finite-precision angle/radius conversion without changing either
    // endpoint or replacing an unrepresentable arc with some other geometry.
    const auto endpoint=[&](double angle) {return Point{center.x+radius*std::cos(angle),center.y+radius*std::sin(angle)};};
    const auto a=local(curve.Value(parameter),source.partPlacement,frame,tolerance);
    const auto b=local(curve.Value(reversed?first:last),source.partPlacement,frame,tolerance);
    const auto p=endpoint(arc.startAngle),q=endpoint(arc.startAngle+arc.sweepAngle);
    require(std::hypot(a.x-p.x,a.y-p.y)<=tolerance && std::hypot(b.x-q.x,b.y-q.y)<=tolerance,Code::NumericalFailure,
            "Projected arc endpoints cannot be represented within the geometric tolerance.");
    return arc;
}
Sketch::Geometry geometry(const Source& source,const gp_Ax3& frame,double tolerance) {
    require(!source.shape.IsNull(),Code::NullSource,"The source shape is null.");
    require(source.shape.ShapeType()==TopAbs_VERTEX || source.shape.ShapeType()==TopAbs_EDGE,
            Code::UnsupportedShape,"Only vertices and analytic straight or circular edges are supported.");
    require(finite(source.partPlacement) && finite(source.shape.Location().Transformation()),
            Code::InvalidPlacement,"Source Part or subshape placement is invalid.");
    require(source.shape.Orientation()==TopAbs_FORWARD || source.shape.Orientation()==TopAbs_REVERSED,
            Code::InvalidOrientation,"Internal/external source orientations are not supported.");
    if(source.shape.ShapeType()==TopAbs_VERTEX) {
        require(BRepCheck_Analyzer(source.shape).IsValid(),Code::InvalidBRep,"The source vertex is not valid B-Rep geometry.");
        return ConstructionPoint{local(BRep_Tool::Pnt(TopoDS::Vertex(source.shape)),source.partPlacement,frame,tolerance)};
    }
    const auto edge=TopoDS::Edge(source.shape);
    require(!BRep_Tool::Degenerated(edge),Code::DegenerateEdge,"A degenerated edge cannot supply Construction geometry.");
    TopLoc_Location curveLocation;double first,last;
    require(!BRep_Tool::Curve(edge,curveLocation,first,last).IsNull(),Code::MissingCurve,"The edge has no exact 3D curve.");
    require(finite(curveLocation.Transformation()),Code::InvalidPlacement,"The edge curve placement is invalid.");
    BRepAdaptor_Curve curve(edge);first=curve.FirstParameter();last=curve.LastParameter();
    require(std::isfinite(first) && std::isfinite(last) && !Precision::IsInfinite(first) && !Precision::IsInfinite(last),
            Code::UnboundedEdge,"An unbounded edge cannot supply bounded Construction geometry.");
    require(last>first,Code::DegenerateEdge,"The edge has an empty or reversed parameter interval.");
    require(curve.Is3DCurve() && (curve.GetType()==GeomAbs_Line || curve.GetType()==GeomAbs_Circle),Code::NonlinearEdge,
            "Only exact analytic lines and circles/arcs are supported; other curves are not approximated.");
    require(BRepCheck_Analyzer(edge).IsValid(),Code::InvalidBRep,"The source edge is not valid B-Rep geometry.");
    if(curve.GetType()==GeomAbs_Circle) return circularGeometry(source,curve,frame,tolerance);
    auto a=curve.Value(first),b=curve.Value(last);
    require(finite(a) && finite(b),Code::NumericalFailure,"Edge coordinates are not finite.");
    std::array<long double,3> direction{};
    for(int r=1;r<=3;++r) for(int c=1;c<=3;++c)
        direction[size_t(r-1)]+=source.partPlacement.Value(r,c)*(static_cast<long double>(b.Coord(c))-a.Coord(c));
    require(std::hypot(direction[0],direction[1],direction[2])>tolerance,
            Code::DegenerateEdge,"The placed source edge has zero or tolerance-sized length.");
    if(source.shape.Orientation()==TopAbs_REVERSED) std::swap(a,b);
    Segment result{local(a,source.partPlacement,frame,tolerance),local(b,source.partPlacement,frame,tolerance)};
    require(std::hypot(result.end.x-result.start.x,result.end.y-result.start.y)>tolerance,
            Code::CollapsedProjection,"The edge projects to a point or tolerance-sized span; no Construction line was created.");
    return result;
}
bool near(Point a,Point b,double tolerance) {return std::hypot(a.x-b.x,a.y-b.y)<=tolerance;}
std::optional<Sketch::Circle> support(const Sketch::Geometry& geometry) {
    if(const auto* circle=std::get_if<Sketch::Circle>(&geometry)) return *circle;
    if(const auto* arc=std::get_if<Sketch::Arc>(&geometry)) return Sketch::Circle{arc->center,arc->radius};
    return {};
}
bool sameSupport(const Sketch::Circle& a,const Sketch::Circle& b,double tolerance) {return near(a.center,b.center,tolerance) && std::abs(a.radius-b.radius)<=tolerance;}
long double intervalStart(const Sketch::Arc& arc) {return normalized(static_cast<long double>(arc.startAngle)+(arc.sweepAngle<0?arc.sweepAngle:0));}
bool duplicate(const Sketch::Geometry& a,const Sketch::Geometry& b,double tolerance) {
    if(const auto* p=std::get_if<ConstructionPoint>(&a)) {
        const auto* q=std::get_if<ConstructionPoint>(&b);return q && near(p->position,q->position,tolerance);
    }
    if(const auto ca=support(a),cb=support(b);ca && cb && sameSupport(*ca,*cb,tolerance)) {
        const auto* aa=std::get_if<Sketch::Arc>(&a);const auto* ab=std::get_if<Sketch::Arc>(&b);
        if(!aa || !ab) return !aa && !ab; // A bounded arc never duplicates a full circle.
        auto delta=std::abs(intervalStart(*aa)-intervalStart(*ab));delta=std::min(delta,turn-delta);
        return delta*std::max(ca->radius,cb->radius)<=tolerance
            && std::abs(std::abs(aa->sweepAngle)-std::abs(ab->sweepAngle))*std::max(ca->radius,cb->radius)<=tolerance;
    }
    const auto* p=std::get_if<Segment>(&a);const auto* q=std::get_if<Segment>(&b);
    return p && q && ((near(p->start,q->start,tolerance) && near(p->end,q->end,tolerance))
                  || (near(p->start,q->end,tolerance) && near(p->end,q->start,tolerance)));
}
struct Line {
    long double x,y,dx,dy,length;
    explicit Line(const Segment& s):x(s.start.x),y(s.start.y),dx(static_cast<long double>(s.end.x)-x),dy(static_cast<long double>(s.end.y)-y),length(std::hypot(dx,dy)) {dx/=length;dy/=length;}
    long double along(Point p) const {return (static_cast<long double>(p.x)-x)*dx+(static_cast<long double>(p.y)-y)*dy;}
    long double distance(Point p) const {return std::abs((static_cast<long double>(p.x)-x)*dy-(static_cast<long double>(p.y)-y)*dx);}
};
bool overlap(const Sketch::Geometry& a,const Sketch::Geometry& b,double tolerance) {
    if(const auto ca=support(a),cb=support(b);ca && cb && sameSupport(*ca,*cb,tolerance)) {
        if(!std::holds_alternative<Sketch::Arc>(a) || !std::holds_alternative<Sketch::Arc>(b)) return true;
        const auto intervals=[](const Sketch::Arc& arc) {
            const auto start=intervalStart(arc),end=start+std::abs(arc.sweepAngle);
            std::vector<std::pair<long double,long double>> result{{start,std::min(end,static_cast<long double>(turn))}};
            if(end>turn) result.emplace_back(0,end-turn);
            return result;
        };
        const auto noise=std::min(static_cast<long double>(angularRoundoff*turn),
                                 static_cast<long double>(tolerance)/std::max(ca->radius,cb->radius));
        for(const auto& i:intervals(std::get<Sketch::Arc>(a))) for(const auto& j:intervals(std::get<Sketch::Arc>(b)))
            if(std::min(i.second,j.second)-std::max(i.first,j.first)>noise) return true;
        return false;
    }
    const auto* s=std::get_if<Segment>(&a);if(!s) return false;
    const Line line(*s);
    if(const auto* infinite=std::get_if<Sketch::InfiniteLine>(&b)) {
        const auto distance=[&](Point p) {return std::abs((static_cast<long double>(p.x)-infinite->origin.x)*infinite->direction.y
                                                     -(static_cast<long double>(p.y)-infinite->origin.y)*infinite->direction.x);};
        return distance(s->start)<=tolerance && distance(s->end)<=tolerance;
    }
    const auto* other=std::get_if<Segment>(&b);if(!other) return false;
    const Line second(*other);
    if(line.distance(other->start)>tolerance || line.distance(other->end)>tolerance
       || second.distance(s->start)>tolerance || second.distance(s->end)>tolerance) return false;
    auto p=line.along(other->start),q=line.along(other->end);if(q<p) std::swap(p,q);
    const auto shared=std::min(line.length,q)-std::max(0.L,p);
    // Do not swallow short real overlaps using the model's linear tolerance.
    // Only arithmetic noise at interval boundaries counts as endpoint contact.
    const auto noise=8*std::numeric_limits<long double>::epsilon()*std::max({1.L,line.length,std::abs(p),std::abs(q)});
    return shared>noise;
}
}
Result project(const std::vector<Source>& sources,const Construction::Object& workplane,
               const std::vector<Sketch::Entity>& existing,double tolerance) {
    Result result;
    const auto report=[&](Code code,const QString& message,std::optional<size_t> source={},
                          std::optional<size_t> related={},const QString& existingId={}) {
        result.diagnostics.push_back({code,message,source,source?sources[*source].label:QString{},related,existingId});
    };
    QString error;
    if(workplane.kind!=Construction::Kind::Workplane || !Construction::valid(workplane,error)) {
        report(Code::InvalidWorkplane,"Choose a valid target Workplane. "+error);return result;
    }
    if(!std::isfinite(tolerance) || tolerance<1.e-9) {
        report(Code::InvalidTolerance,"Projection tolerance must be finite and at least 1e-9 mm.");return result;
    }
    QSet<QString> ids{workplane.id};
    for(const auto& e:existing) {
        if(!Sketch::valid(e,error) || ids.contains(e.id)) {
            report(Code::InvalidExistingGeometry,"Invalid existing 2D collection: "+(error.isEmpty()?QString("duplicate identity."):error),{},{},e.id);return result;
        }
        ids.insert(e.id);
    }
    struct Candidate {size_t source;Sketch::Geometry geometry;};
    std::vector<Candidate> candidates;
    for(size_t i=0;i<sources.size();++i) {
        try {candidates.push_back({i,geometry(sources[i],workplane.frame,tolerance)});}
        catch(const Failure& f) {report(f.code,f.message,i);}
        catch(const Standard_Failure& f) {report(Code::KernelFailure,QString("OCCT could not evaluate this source: %1").arg(QString::fromUtf8(f.GetMessageString())),i);}
        catch(const std::exception& f) {report(Code::KernelFailure,QString("Could not evaluate this source: %1").arg(QString::fromUtf8(f.what())),i);}
    }
    // Deduplication precedes overlap checking, so an unchanged duplicate cannot
    // cause a new overlap error against already existing overlapping references.
    std::vector<Candidate> unique;
    for(const auto& c:candidates) {
        const auto old=std::find_if(existing.begin(),existing.end(),[&](const auto& e) {
            return e.workplaneId==workplane.id && e.role==Sketch::Role::Construction && duplicate(c.geometry,e.geometry,tolerance);
        });
        if(old!=existing.end()) {report(Code::Duplicate,"Duplicate Construction geometry already exists; source skipped.",c.source,{},old->id);continue;}
        const auto earlier=std::find_if(unique.begin(),unique.end(),[&](const auto& p){return duplicate(c.geometry,p.geometry,tolerance);});
        if(earlier!=unique.end()) {report(Code::Duplicate,"Duplicate projected geometry in this batch; source skipped.",c.source,earlier->source);continue;}
        unique.push_back(c);
    }
    for(size_t i=0;i<unique.size();++i) {
        const auto& c=unique[i];
        for(const auto& e:existing) if(e.workplaneId==workplane.id && e.role==Sketch::Role::Construction && overlap(c.geometry,e.geometry,tolerance))
            report(Code::PartialOverlap,"The projected geometry overlaps existing Construction geometry; no automatic trimming is performed.",c.source,{},e.id);
        for(size_t j=0;j<i;++j) if(overlap(c.geometry,unique[j].geometry,tolerance))
            report(Code::PartialOverlap,"Projected curves overlap over a nonzero interval; no automatic trimming is performed.",c.source,unique[j].source);
    }
    if(std::any_of(result.diagnostics.begin(),result.diagnostics.end(),[](const auto& d){return d.isError();})) return result;
    for(const auto& c:unique) {
        Sketch::Entity e;while(ids.contains(e.id)) e.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        e.workplaneId=workplane.id;e.role=Sketch::Role::Construction;e.geometry=c.geometry;
        if(!Sketch::valid(e,error)) {report(Code::NumericalFailure,error,c.source);result.additions.clear();return result;}
        ids.insert(e.id);result.additions.push_back(std::move(e));
    }
    result.success=true;return result;
}
}
