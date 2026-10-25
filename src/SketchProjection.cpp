#include "SketchProjection.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Wire.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <GeomAdaptor_Surface.hxx>
#include <Precision.hxx>
#include <QSet>
#include <Standard_Failure.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Elips.hxx>
#include <gp_Lin2d.hxx>
#include <gp_Pln.hxx>
#include <gp_Sphere.hxx>
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
// Project the analytic cosine/sine coefficients, not sampled curve points.
// A = [p q] = B Q, where B is a direct principal-axis frame and Q is
// orthogonal. Thus tLocal = handedness*tSource + phase for the whole curve.
Sketch::Geometry ellipticalGeometry(const Source& source,const BRepAdaptor_Curve& curve,const gp_Ax3& frame,double tolerance) {
    const bool circular=curve.GetType()==GeomAbs_Circle;
    const auto axes=circular?curve.Circle().Position():curve.Ellipse().Position();
    const auto center=local(axes.Location(),source.partPlacement,frame,tolerance);
    const auto u=localDirection(axes.XDirection(),source.partPlacement,frame);
    const auto v=localDirection(axes.YDirection(),source.partPlacement,frame);
    const long double scale=std::abs(source.partPlacement.ScaleFactor());
    const long double r1=scale*(circular?curve.Circle().Radius():curve.Ellipse().MajorRadius());
    const long double r2=scale*(circular?curve.Circle().Radius():curve.Ellipse().MinorRadius());
    require(std::isfinite(r1) && std::isfinite(r2),Code::NumericalFailure,"Placed conic radii are not finite.");
    require(r2>tolerance,Code::DegenerateEdge,"The placed conic has a zero or tolerance-sized radius.");
    const long double px=r1*u[0],py=r1*u[1],qx=r2*v[0],qy=r2*v[1];
    const long double magnitude=std::max({std::abs(px),std::abs(py),std::abs(qx),std::abs(qy)});
    require(std::isfinite(magnitude),Code::NumericalFailure,"Projected conic coefficients are not finite.");
    require(magnitude>tolerance,Code::CollapsedProjection,"The conic projection collapses within tolerance.");
    const long double x=px/magnitude,y=py/magnitude,z=qx/magnitude,w=qy/magnitude;
    const long double xx=x*x+z*z,yy=y*y+w*w,xy=x*y+z*w,det=x*w-y*z;
    const long double largest=std::sqrt((xx+yy+std::hypot(xx-yy,2*xy))/2);
    const long double a=magnitude*largest,b=magnitude*std::abs(det)/largest;
    require(b>tolerance,Code::CollapsedProjection,"The conic projects edge-on or has a tolerance-sized minor radius; no line approximation was created.");
    require(a/b<=1/std::sqrt(std::numeric_limits<double>::epsilon()),Code::NumericalFailure,
            "The projected conic is too ill-conditioned for reliable ellipse parameters.");
    require(angularRoundoff*std::max({r1,r2,std::abs(static_cast<long double>(center.x)),std::abs(static_cast<long double>(center.y))})<=tolerance,Code::NumericalFailure,
            "Conic coefficient roundoff exceeds the requested geometric tolerance.");
    Sketch::Ellipse ellipse{center,double(a),double(b),double(normalized(std::atan2(2*xy,xx-yy)/2))};
    const long double ca=std::cos(static_cast<long double>(ellipse.axisAngle)),sa=std::sin(static_cast<long double>(ellipse.axisAngle));
    const long double phase=std::atan2((-sa*px+ca*py)/ellipse.minorRadius,(ca*px+sa*py)/ellipse.majorRadius);
    const int handedness=det<0?-1:1;
    // Certify the stored double basis and phase over the entire parameter
    // domain: ||error(p)|| + ||error(q)|| bounds every curve position error.
    const auto residual=[&](double storedPhase) {
        const long double c=std::cos(static_cast<long double>(storedPhase)),s=std::sin(static_cast<long double>(storedPhase));
        const long double ax=ellipse.majorRadius*ca,ay=ellipse.majorRadius*sa,bx=-ellipse.minorRadius*sa,by=ellipse.minorRadius*ca;
        return std::hypot(px-(ax*c+bx*s),py-(ay*c+by*s))
             + std::hypot(qx-handedness*(-ax*s+bx*c),qy-handedness*(-ay*s+by*c));
    };
    require(std::isfinite(ellipse.majorRadius) && std::isfinite(ellipse.minorRadius)
            && residual(double(phase))<=tolerance,Code::NumericalFailure,"Stored ellipse coefficients cannot reproduce the projection within tolerance.");
    const double first=curve.FirstParameter(),last=curve.LastParameter(),span=last-first;
    if(last==first+curve.Period()) return ellipse;
    require(span<turn,Code::UnsupportedCircularSpan,"The conic edge must span less than one turn or exactly one full period.");
    require(span>1.e-12 && b*span>tolerance,Code::DegenerateEdge,"The projected conic arc has a zero or tolerance-sized span.");
    const bool reversed=source.shape.Orientation()==TopAbs_REVERSED;
    const double parameter=reversed?last:first;
    const double start=double(normalized(handedness*static_cast<long double>(parameter)+phase));
    // Recheck the phase after storing the normalized start. This additionally
    // catches large source parameters whose phase reduction loses precision.
    require(residual(double(static_cast<long double>(start)-handedness*static_cast<long double>(parameter)))<=tolerance,
            Code::NumericalFailure,"The stored elliptical arc start loses the source parameter phase.");
    Sketch::EllipticalArc arc{ellipse,start,span*handedness*(reversed?-1:1)};
    const auto ends=Sketch::ellipticalArcEndpoints(arc);
    const auto sourceStart=local(curve.Value(parameter),source.partPlacement,frame,tolerance);
    const auto sourceEnd=local(curve.Value(reversed?first:last),source.partPlacement,frame,tolerance);
    require(std::hypot(ends[0].x-sourceStart.x,ends[0].y-sourceStart.y)<=tolerance
            && std::hypot(ends[1].x-sourceEnd.x,ends[1].y-sourceEnd.y)<=tolerance,Code::NumericalFailure,
            "Stored elliptical arc endpoints cannot reproduce the source traversal within tolerance.");
    return arc;
}
void validateSource(const Source& source) {
    require(!source.shape.IsNull(),Code::NullSource,"The source shape is null.");
    require(source.shape.ShapeType()==TopAbs_VERTEX || source.shape.ShapeType()==TopAbs_EDGE || source.shape.ShapeType()==TopAbs_FACE,
            Code::UnsupportedShape,"Only vertices, analytic line/conic edges and supported face boundaries are supported.");
    require(finite(source.partPlacement) && finite(source.shape.Location().Transformation()),
            Code::InvalidPlacement,"Source Part or subshape placement is invalid.");
    require(source.shape.Orientation()==TopAbs_FORWARD || source.shape.Orientation()==TopAbs_REVERSED,
            Code::InvalidOrientation,"Internal/external source orientations are not supported.");
}
Sketch::Geometry geometry(const Source& source,const gp_Ax3& frame,double tolerance) {
    validateSource(source);
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
    require(curve.Is3DCurve() && (curve.GetType()==GeomAbs_Line || curve.GetType()==GeomAbs_Circle || curve.GetType()==GeomAbs_Ellipse),Code::NonlinearEdge,
            "Only exact analytic lines, circles and ellipses are supported; other curves are not approximated.");
    require(BRepCheck_Analyzer(edge).IsValid(),Code::InvalidBRep,"The source edge is not valid B-Rep geometry.");
    if(curve.GetType()==GeomAbs_Circle) {
        const auto axes=curve.Circle().Position();
        if(std::abs(localDirection(axes.XDirection(),source.partPlacement,frame)[2])<=angularRoundoff
           && std::abs(localDirection(axes.YDirection(),source.partPlacement,frame)[2])<=angularRoundoff)
            return circularGeometry(source,curve,frame,tolerance);
        return ellipticalGeometry(source,curve,frame,tolerance);
    }
    if(curve.GetType()==GeomAbs_Ellipse) return ellipticalGeometry(source,curve,frame,tolerance);
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
struct Piece {Sketch::Geometry geometry;QString context;};

// A degenerated flag or coincident endpoint vertices alone cannot prove that
// an edge has no extent (a full circle also has coincident endpoints). Certify
// the entire p-curve's image on a supported analytic surface, without sampling.
Point collapsedPole(const Source& source,const TopoDS_Face& face,const gp_Ax3& frame,double tolerance) {
    const auto edge=TopoDS::Edge(source.shape);
    TopLoc_Location curveLocation,surfaceLocation;double first,last;
    require(BRep_Tool::Curve(edge,curveLocation,first,last).IsNull(),Code::DegenerateEdge,
            "The degenerated edge has a 3D curve; it cannot safely be omitted.");
    const auto pcurve=BRep_Tool::CurveOnSurface(edge,face,first,last);
    const auto rawSurface=BRep_Tool::Surface(face,surfaceLocation);
    require(!pcurve.IsNull() && !rawSurface.IsNull() && finite(surfaceLocation.Transformation())
            && std::isfinite(first) && std::isfinite(last) && !Precision::IsInfinite(first) && !Precision::IsInfinite(last) && last>first,
            Code::DegenerateEdge,"The degenerated edge lacks a finite, verifiable surface p-curve.");
    Geom2dAdaptor_Curve uv(pcurve,first,last);GeomAdaptor_Surface surface(rawSurface);
    require(uv.GetType()==GeomAbs_Line && uv.Line().Direction().Y()==0,Code::DegenerateEdge,
            "Only constant-V spherical/conical pole p-curves can safely be omitted.");
    const auto start=uv.Value(first);const long double v=start.Y();
    long double residual=0,reference=1;
    if(surface.GetType()==GeomAbs_Sphere) {
        residual=std::abs(std::cos(v))*surface.Sphere().Radius();reference=surface.Sphere().Radius();
    } else if(surface.GetType()==GeomAbs_Cone) {
        const auto cone=surface.Cone();const long double r=cone.RefRadius(),offset=v*std::sin(static_cast<long double>(cone.SemiAngle()));
        residual=std::abs(r+offset);reference=std::max({1.L,std::abs(r),std::abs(offset)});
    } else require(false,Code::DegenerateEdge,"The degenerated edge's surface cannot be certified as a collapsed analytic pole.");
    const long double scale=std::abs(static_cast<long double>(surfaceLocation.Transformation().ScaleFactor())*source.partPlacement.ScaleFactor());
    require(std::isfinite(residual) && residual<=angularRoundoff*reference && 2*residual*scale<=tolerance,
            Code::DegenerateEdge,"The degenerated edge's p-curve does not collapse within the projection tolerance.");
    const auto pole=surface.Value(start.X(),start.Y()).Transformed(surfaceLocation.Transformation());
    require(finite(pole),Code::NumericalFailure,"The degenerated edge's pole is not finite.");
    size_t vertices=0;
    for(TopoDS_Iterator it(edge);it.More();it.Next()) {
        require(it.Value().ShapeType()==TopAbs_VERTEX,Code::DegenerateEdge,"Unexpected topology on a degenerated edge.");
        const auto p=BRep_Tool::Pnt(TopoDS::Vertex(it.Value()));
        require(finite(p) && static_cast<long double>(p.Distance(pole))*std::abs(source.partPlacement.ScaleFactor())<=tolerance,
                Code::DegenerateEdge,"The degenerated edge's vertices do not agree with its analytic pole.");++vertices;
    }
    require(vertices==2,Code::DegenerateEdge,"The degenerated edge does not have two verifiable endpoint occurrences.");
    return local(pole,source.partPlacement,frame,tolerance);
}
// Only certify complete rectangular UV trims. A UV bounding box alone cannot
// prove that a generator stays inside a face with holes or oblique/curved caps.
std::vector<Piece> cylinderSilhouettes(const Source& source,const TopoDS_Face& face,
        const GeomAdaptor_Surface& surface,const TopLoc_Location& location,const gp_Ax3& frame,double tolerance) {
    const auto cylinder=surface.Cylinder();
    const auto surfacePlacement=location.Transformation();
    const auto direction=[&](const gp_Dir& d) {
        std::array<long double,3> result{};
        for(int r=1;r<=3;++r) for(int c=1;c<=3;++c) result[r-1]+=static_cast<long double>(surfacePlacement.Value(r,c))*d.Coord(c);
        return result;
    };
    const auto sx=direction(cylinder.XAxis().Direction()),sy=direction(cylinder.YAxis().Direction()),sz=direction(cylinder.Axis().Direction());
    const auto residual=[&](const gp_Pnt& point,double v,long double c,long double s) {
        std::array<long double,3> result{};
        for(int r=1;r<=3;++r) {
            result[r-1]=static_cast<long double>(surfacePlacement.Value(r,4))-point.Coord(r);
            for(int j=1;j<=3;++j) result[r-1]+=static_cast<long double>(surfacePlacement.Value(r,j))*cylinder.Location().Coord(j);
            result[r-1]+=cylinder.Radius()*(c*sx[r-1]+s*sy[r-1])+v*sz[r-1];
        }
        return result;
    };
    const auto norm=[](const auto& a){return std::hypot(a[0],a[1],a[2]);};
    struct Side {gp_Pnt2d a,b;bool horizontal,reversed;};
    std::vector<Side> edges;size_t wires=0;
    for(TopoDS_Iterator it(face);it.More();it.Next()) {
        require(it.Value().ShapeType()==TopAbs_WIRE && ++wires==1,Code::UnsupportedSilhouetteTrim,
                "Cylinder silhouettes require one full rectangular UV boundary without holes.");
        for(TopoDS_Iterator edge(it.Value());edge.More();edge.Next()) {
            require(edge.Value().ShapeType()==TopAbs_EDGE,Code::UnsupportedSilhouetteTrim,"Invalid cylindrical trim topology.");
            double first,last;const auto uv=BRep_Tool::CurveOnSurface(TopoDS::Edge(edge.Value()),face,first,last);
            require(!uv.IsNull() && std::isfinite(first) && std::isfinite(last) && !Precision::IsInfinite(first)
                    && !Precision::IsInfinite(last) && last>first,Code::UnsupportedSilhouetteTrim,
                    "Cylinder silhouettes require finite verifiable boundary p-curves.");
            const Geom2dAdaptor_Curve curve(uv,first,last);
            require(curve.GetType()==GeomAbs_Line,Code::UnsupportedSilhouetteTrim,
                    "Curved cylinder trims are not supported for exact finite silhouettes.");
            const auto direction=curve.Line().Direction();
            require(direction.X()==0 || direction.Y()==0,Code::UnsupportedSilhouetteTrim,
                    "Oblique cylinder trims are not supported for exact finite silhouettes.");
            const auto a=curve.Value(first),b=curve.Value(last);
            require(std::isfinite(a.X()) && std::isfinite(a.Y()) && std::isfinite(b.X()) && std::isfinite(b.Y()),
                    Code::NumericalFailure,"Cylinder trim coordinates are not finite.");
            // Certify the entire exact 3D boundary against its cylinder p-curve.
            // BRepCheck alone may accept a larger stored edge/face tolerance.
            const BRepAdaptor_Curve boundary(TopoDS::Edge(edge.Value()));
            require(boundary.Is3DCurve() && boundary.FirstParameter()==first && boundary.LastParameter()==last,
                    Code::UnsupportedSilhouetteTrim,"Cylinder boundary parameterizations do not match their p-curves.");
            long double error=0;
            if(direction.Y()==0) {
                require(boundary.GetType()==GeomAbs_Circle,Code::UnsupportedSilhouetteTrim,
                        "A rectangular cylinder cap must be an analytic circular edge.");
                const auto cap=boundary.Circle();const long double phase=curve.Line().Location().X();
                const auto c=std::cos(phase),s=std::sin(phase);
                auto p=sx,q=sy;
                for(int i=0;i<3;++i) {
                    p[i]=cylinder.Radius()*(c*sx[i]+s*sy[i])-cap.Radius()*cap.XAxis().Direction().Coord(i+1);
                    q[i]=direction.X()*cylinder.Radius()*(-s*sx[i]+c*sy[i])-cap.Radius()*cap.YAxis().Direction().Coord(i+1);
                }
                error=norm(residual(cap.Location(),a.Y(),0,0))+norm(p)+norm(q);
            } else {
                require(boundary.GetType()==GeomAbs_Line,Code::UnsupportedSilhouetteTrim,
                        "A rectangular cylinder seam must be an analytic straight edge.");
                gp_Pnt at;gp_Vec tangent;boundary.D1(first,at,tangent);auto delta=sz;
                for(int i=0;i<3;++i) delta[i]=direction.Y()*sz[i]-tangent.Coord(i+1);
                error=norm(residual(at,a.Y(),std::cos(static_cast<long double>(a.X())),std::sin(static_cast<long double>(a.X()))))
                    +(static_cast<long double>(last)-first)*norm(delta);
            }
            require(std::isfinite(error) && error*std::abs(source.partPlacement.ScaleFactor())<=tolerance,
                    Code::NumericalFailure,"Cylinder boundary curves do not agree with the trimmed surface within projection tolerance.");
            edges.push_back({a,b,direction.Y()==0,edge.Value().Orientation()==TopAbs_REVERSED});
        }
    }
    require(!edges.empty(),Code::UnsupportedSilhouetteTrim,"Cylinder silhouettes require a finite boundary.");
    double u0=edges.front().a.X(),u1=u0,v0=edges.front().a.Y(),v1=v0;
    for(const auto& e:edges) for(const auto p:{e.a,e.b}) {
        u0=std::min(u0,p.X());u1=std::max(u1,p.X());v0=std::min(v0,p.Y());v1=std::max(v1,p.Y());
    }
    const auto placement=source.partPlacement*location.Transformation();
    require(finite(placement),Code::InvalidPlacement,"The combined cylinder placement is invalid.");
    const long double scale=std::abs(placement.ScaleFactor());
    const long double radius=scale*cylinder.Radius();
    const long double uNoise=angularRoundoff*std::max({1.,std::abs(u0),std::abs(u1)});
    const long double vNoise=angularRoundoff*std::max({1.,std::abs(v0),std::abs(v1)});
    require(radius*uNoise<=tolerance && scale*vNoise<=tolerance,Code::NumericalFailure,
            "Cylinder trim roundoff exceeds the projection tolerance.");
    require(std::abs((static_cast<long double>(u1)-u0)-turn)<=uNoise,Code::UnsupportedSilhouetteTrim,
            "Cylinder silhouettes currently require a complete 360-degree mantle.");
    require(radius>tolerance && scale*(static_cast<long double>(v1)-v0)>tolerance,Code::CollapsedProjection,
            "The cylinder radius or finite height is tolerance-sized.");
    std::array<std::vector<std::pair<double,double>>,4> sides;
    for(const auto& e:edges) {
        if(e.horizontal) {
            const auto side=std::abs(static_cast<long double>(e.a.Y())-v0)<=vNoise?0:1;
            require(std::abs(static_cast<long double>(e.a.Y())-(side==0?v0:v1))<=vNoise,Code::UnsupportedSilhouetteTrim,
                    "Cylinder caps do not form a rectangular UV trim.");
            sides[side].emplace_back(std::min(e.a.X(),e.b.X()),std::max(e.a.X(),e.b.X()));
        } else {
            const auto side=std::abs(static_cast<long double>(e.a.X())-u0)<=uNoise?2:3;
            require(std::abs(static_cast<long double>(e.a.X())-(side==2?u0:u1))<=uNoise,Code::UnsupportedSilhouetteTrim,
                    "Cylinder seams do not form a rectangular UV trim.");
            sides[side].emplace_back(std::min(e.a.Y(),e.b.Y()),std::max(e.a.Y(),e.b.Y()));
        }
    }
    for(size_t i=0;i<sides.size();++i) {
        auto& intervals=sides[i];std::sort(intervals.begin(),intervals.end());
        long double cursor=i<2?u0:v0;const auto noise=i<2?uNoise:vNoise;
        for(const auto [a,b]:intervals) {
            require(b>a && std::abs(static_cast<long double>(a)-cursor)<=noise,Code::UnsupportedSilhouetteTrim,
                    "Cylinder trim intervals contain gaps or overlaps.");cursor=b;
        }
        require(!intervals.empty() && std::abs(cursor-(i<2?u1:v1))<=noise,Code::UnsupportedSilhouetteTrim,
                "Cylinder trim sides do not cover one complete bounded mantle.");
    }
    long double area=0;
    for(const auto& e:edges) {
        const long double ax=static_cast<long double>(e.a.X())-u0,ay=static_cast<long double>(e.a.Y())-v0;
        const long double bx=static_cast<long double>(e.b.X())-u0,by=static_cast<long double>(e.b.Y())-v0;
        area+=(e.reversed?-1:1)*(ax*by-bx*ay);
    }
    require(area>0,Code::UnsupportedSilhouetteTrim,
            "The cylindrical trim does not bound the finite rectangular interior.");
    const auto x=localDirection(cylinder.XAxis().Direction(),placement,frame);
    const auto y=localDirection(cylinder.YAxis().Direction(),placement,frame);
    const auto axis=localDirection(cylinder.Axis().Direction(),placement,frame);
    const long double radial=std::hypot(x[2],y[2]);
    require(radial>angularRoundoff,Code::CollapsedProjection,
            "A cylinder viewed along its axis has no two distinct finite silhouette lines.");
    require(angularRoundoff*radius/radial<=tolerance,Code::NumericalFailure,
            "The cylinder axis is too nearly parallel to the projection direction for reliable silhouettes.");
    // Surface normal dot view direction = Nx*cos(u)+Ny*sin(u)=0.
    // The two exact radial directions are +/- (Ny,-Nx)/hypot(Nx,Ny).
    const long double c=y[2]/radial,s=-x[2]/radial;
    const auto center=local(cylinder.Location(),placement,frame,tolerance);
    require(angularRoundoff*std::max({radius,scale*std::abs(static_cast<long double>(v0)),scale*std::abs(static_cast<long double>(v1)),
                std::abs(static_cast<long double>(center.x)),std::abs(static_cast<long double>(center.y))})<=tolerance,
            Code::NumericalFailure,"Cylinder silhouette coordinate roundoff exceeds tolerance.");
    const auto endpoint=[&](double v,int side) {
        const auto coord=[&](double origin,long double dx,long double dy,long double dz) {
            const long double value=origin+side*radius*(c*dx+s*dy)+scale*v*dz;const double stored=double(value);
            require(std::isfinite(stored) && std::abs(value-stored)<=tolerance,Code::NumericalFailure,
                    "A finite cylinder silhouette endpoint cannot be stored within tolerance.");return stored;
        };
        return Point{coord(center.x,x[0],y[0],axis[0]),coord(center.y,x[1],y[1],axis[1])};
    };
    std::vector<Piece> result;
    for(int side:{1,-1}) {
        const Segment line{endpoint(v0,side),endpoint(v1,side)};
        require(std::hypot(line.end.x-line.start.x,line.end.y-line.start.y)>tolerance,Code::CollapsedProjection,
                "The cylinder silhouette collapses to a point or tolerance-sized span.");
        result.push_back({line,QString("Cylinder silhouette %1: ").arg(result.size()+1)});
    }
    return result;
}
std::vector<Piece> faceGeometry(const Source& source,const gp_Ax3& frame,double tolerance,std::vector<QString>& skipped) {
    // Face reversal changes surface orientation, not the boundary's geometric
    // set. Keep each wire's own traversal, with all cumulative locations.
    const auto face=TopoDS::Face(source.shape.Oriented(TopAbs_FORWARD));
    require(BRepCheck_Analyzer(face).IsValid(),Code::InvalidBRep,"The source face is not valid B-Rep geometry.");
    require(!BRepTools::OuterWire(face).IsNull(),Code::InvalidFaceBoundary,"The face has no bounded outer wire.");
    TopLoc_Location surfaceLocation;const auto surface=BRep_Tool::Surface(face,surfaceLocation);
    require(!surface.IsNull() && finite(surfaceLocation.Transformation()),Code::InvalidPlacement,"The face surface placement is invalid.");
    GeomAdaptor_Surface adaptor(surface);
    const auto silhouettes=adaptor.GetType()==GeomAbs_Cylinder
        ?cylinderSilhouettes(source,face,adaptor,surfaceLocation,frame,tolerance):std::vector<Piece>{};
    if(adaptor.GetType()==GeomAbs_Plane) {
        auto normal=adaptor.Plane().Axis().Direction();normal.Transform(surfaceLocation.Transformation());
        require(std::abs(localDirection(normal,source.partPlacement,frame)[2])>angularRoundoff,
                Code::CollapsedProjection,"The planar face projects edge-on to a line; no contour was created.");
    }
    std::vector<Piece> result;size_t wireIndex=0;
    for(TopoDS_Iterator wires(face);wires.More();wires.Next()) {
        ++wireIndex;const auto wireContext=QString("Face wire %1: ").arg(wireIndex);
        require(wires.Value().ShapeType()==TopAbs_WIRE,Code::InvalidFaceBoundary,"A face boundary contains a non-wire subshape.");
        const auto wire=TopoDS::Wire(wires.Value());
        require(wire.Orientation()==TopAbs_FORWARD || wire.Orientation()==TopAbs_REVERSED,Code::InvalidOrientation,"Internal/external face wires are not supported.");
        BRepCheck_Wire check(wire);
        require(check.Closed()==BRepCheck_NoError,Code::InvalidFaceBoundary,"A face boundary wire is not topologically closed.");
        std::vector<TopoDS_Shape> remaining;
        for(TopoDS_Iterator edges(wire);edges.More();edges.Next()) {
            require(edges.Value().ShapeType()==TopAbs_EDGE,Code::InvalidFaceBoundary,"A boundary wire contains a non-edge subshape.");
            remaining.push_back(edges.Value());
        }
        std::optional<Point> beginning,previous;size_t edgeIndex=0,curves=0;
        for(BRepTools_WireExplorer edges(wire,face);edges.More();edges.Next()) {
            ++edgeIndex;const auto context=QString("Face wire %1, edge %2: ").arg(wireIndex).arg(edgeIndex);
            try {
                const auto edge=edges.Current();
                const auto occurrence=std::find_if(remaining.begin(),remaining.end(),[&](const auto& s){return s.IsEqual(edge);});
                require(occurrence!=remaining.end(),Code::InvalidFaceBoundary,"Wire traversal repeated or lost an edge occurrence.");
                remaining.erase(occurrence);
                Source child{edge,source.partPlacement,source.label};validateSource(child);
                Point a,b;
                if(BRep_Tool::Degenerated(edge)) {
                    a=b=collapsedPole(child,face,frame,tolerance);skipped.push_back(context+"Certified collapsed pole edge omitted; no endpoint point was added.");
                } else {
                    BRepAdaptor_Curve curve(edge);
                    auto g=geometry(child,frame,tolerance);
                    a=local(curve.Value(edge.Orientation()==TopAbs_REVERSED?curve.LastParameter():curve.FirstParameter()),source.partPlacement,frame,tolerance);
                    b=local(curve.Value(edge.Orientation()==TopAbs_REVERSED?curve.FirstParameter():curve.LastParameter()),source.partPlacement,frame,tolerance);
                    // Check the persisted Arc endpoints too, not just the B-Rep
                    // walk, so precision loss cannot open a previously closed wire.
                    if(const auto* arc=std::get_if<Sketch::Arc>(&g)) {
                        a={arc->center.x+arc->radius*std::cos(arc->startAngle),arc->center.y+arc->radius*std::sin(arc->startAngle)};
                        b={arc->center.x+arc->radius*std::cos(arc->startAngle+arc->sweepAngle),arc->center.y+arc->radius*std::sin(arc->startAngle+arc->sweepAngle)};
                    }
                    if(const auto* arc=std::get_if<Sketch::EllipticalArc>(&g)) {
                        const auto ends=Sketch::ellipticalArcEndpoints(*arc);
                        require(near(a,ends[0],tolerance) && near(b,ends[1],tolerance),Code::NumericalFailure,
                                "Stored elliptical boundary endpoints do not match the oriented B-Rep edge.");
                        a=ends[0];b=ends[1];
                    }
                    result.push_back({std::move(g),context});++curves;
                }
                require(!previous || near(*previous,a,tolerance),Code::InvalidFaceBoundary,"Projected neighboring edges do not meet within tolerance.");
                if(!beginning) beginning=a;previous=b;
            } catch(const Failure& f) {throw Failure{f.code,context+f.message};}
            catch(const Standard_Failure& f) {throw Failure{Code::KernelFailure,context+QString::fromUtf8(f.GetMessageString())};}
        }
        if(!remaining.empty() || !beginning || !curves) throw Failure{Code::InvalidFaceBoundary,wireContext+"No complete, nonempty boundary traversal is available."};
        if(!near(*previous,*beginning,tolerance)) throw Failure{Code::InvalidFaceBoundary,wireContext+"The projected wire is not closed within tolerance."};
    }
    require(wireIndex>0,Code::InvalidFaceBoundary,"The face has no boundary wires.");
    result.insert(result.end(),silhouettes.begin(),silhouettes.end());
    return result;
}
std::optional<Sketch::Circle> support(const Sketch::Geometry& geometry) {
    if(const auto* circle=std::get_if<Sketch::Circle>(&geometry)) return *circle;
    if(const auto* arc=std::get_if<Sketch::Arc>(&geometry)) return Sketch::Circle{arc->center,arc->radius};
    return {};
}
bool sameSupport(const Sketch::Circle& a,const Sketch::Circle& b,double tolerance) {return near(a.center,b.center,tolerance) && std::abs(a.radius-b.radius)<=tolerance;}
long double intervalStart(const Sketch::Arc& arc) {return normalized(static_cast<long double>(arc.startAngle)+(arc.sweepAngle<0?arc.sweepAngle:0));}
// All supported conics have direct local frames. Write B = S R(angle),
// S = R diag(a,b) R^T, the symmetric positive square root of B B^T.
// Comparing S by its exact 2x2 spectral norm bounds the whole support, handles
// axis sign changes and avoids ill-defined principal directions near circles.
struct Conic {
    Sketch::Ellipse ellipse;
    bool full=true;
    double start=0,sweep=turn;
};
std::optional<Conic> conic(const Sketch::Geometry& g) {
    if(const auto* e=std::get_if<Sketch::Ellipse>(&g)) return Conic{*e};
    if(const auto* e=std::get_if<Sketch::EllipticalArc>(&g)) return Conic{e->ellipse,false,e->startParameter,e->sweepParameter};
    if(const auto* e=std::get_if<Sketch::Circle>(&g)) return Conic{{e->center,e->radius,e->radius,0}};
    if(const auto* e=std::get_if<Sketch::Arc>(&g)) return Conic{{e->center,e->radius,e->radius,0},false,e->startAngle,e->sweepAngle};
    return {};
}
std::array<long double,3> symmetricSupport(const Sketch::Ellipse& e) {
    const long double c=std::cos(static_cast<long double>(e.axisAngle)),s=std::sin(static_cast<long double>(e.axisAngle));
    const long double gap=static_cast<long double>(e.majorRadius)-e.minorRadius;
    return {e.minorRadius+gap*c*c,e.minorRadius+gap*s*s,gap*c*s};
}
bool sameConic(const Conic& a,const Conic& b,double tolerance) {
    const auto x=symmetricSupport(a.ellipse),y=symmetricSupport(b.ellipse);
    const long double xx=x[0]-y[0],yy=x[1]-y[1],xy=x[2]-y[2];
    const long double norm=std::abs((xx+yy)/2)+std::hypot((xx-yy)/2,xy);
    const long double centers=std::hypot(static_cast<long double>(a.ellipse.center.x)-b.ellipse.center.x,
                                         static_cast<long double>(a.ellipse.center.y)-b.ellipse.center.y);
    // Under the common parameter w=t+axisAngle, every point differs by at
    // most center distance + ||Sa-Sb||. No curve sampling or conic fitting.
    return centers+norm<=tolerance;
}
long double conicStart(const Conic& c) {
    const auto reduced=[](double t) {return std::atan2(std::sin(static_cast<long double>(t)),std::cos(static_cast<long double>(t)));};
    return normalized(reduced(c.start)+reduced(c.ellipse.axisAngle)+(c.sweep<0?c.sweep:0));
}
bool duplicateConics(const Conic& a,const Conic& b,double tolerance) {
    if(!sameConic(a,b,tolerance)) return false;
    if(a.full || b.full) return a.full && b.full;
    auto delta=std::abs(conicStart(a)-conicStart(b));delta=std::min(delta,turn-delta);
    const auto radius=std::max(a.ellipse.majorRadius,b.ellipse.majorRadius);
    return delta*radius<=tolerance && std::abs(std::abs(a.sweep)-std::abs(b.sweep))*radius<=tolerance;
}
bool overlapConics(const Conic& a,const Conic& b,double tolerance) {
    if(!sameConic(a,b,tolerance)) return false;
    if(a.full || b.full) return true;
    const auto intervals=[](const Conic& c) {
        const auto start=conicStart(c),end=start+std::abs(c.sweep);
        std::vector<std::pair<long double,long double>> result{{start,std::min(end,static_cast<long double>(turn))}};
        if(end>turn) result.emplace_back(0,end-turn);
        return result;
    };
    // As with circular arcs, model tolerance must not swallow a short but
    // positive common interval. Only phase arithmetic noise permits contact.
    const auto noise=std::min(static_cast<long double>(angularRoundoff*turn),
                             static_cast<long double>(tolerance)/std::max(a.ellipse.majorRadius,b.ellipse.majorRadius));
    for(const auto& i:intervals(a)) for(const auto& j:intervals(b))
        if(std::min(i.second,j.second)-std::max(i.first,j.first)>noise) return true;
    return false;
}
bool duplicate(const Sketch::Geometry& a,const Sketch::Geometry& b,double tolerance) {
    if(Sketch::isElliptical(a) || Sketch::isElliptical(b)) {
        const auto ca=conic(a),cb=conic(b);return ca && cb && duplicateConics(*ca,*cb,tolerance);
    }
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
    if(Sketch::isElliptical(a) || Sketch::isElliptical(b)) {
        const auto ca=conic(a),cb=conic(b);return ca && cb && overlapConics(*ca,*cb,tolerance);
    }
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
    struct Candidate {size_t source;Sketch::Geometry geometry;QString context;bool faceBoundary=false;};
    std::vector<Candidate> candidates;
    for(size_t i=0;i<sources.size();++i) {
        try {
            validateSource(sources[i]);
            if(sources[i].shape.ShapeType()==TopAbs_FACE) {
                std::vector<QString> skipped;
                const auto pieces=faceGeometry(sources[i],workplane.frame,tolerance,skipped);
                for(const auto& message:skipped) report(Code::DegenerateSkipped,message,i);
                for(const auto& p:pieces) candidates.push_back({i,p.geometry,p.context,true});
            } else candidates.push_back({i,geometry(sources[i],workplane.frame,tolerance),{}});
        }
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
        if(old!=existing.end()) {report(Code::Duplicate,c.context+"Duplicate Construction geometry already exists; boundary piece skipped.",c.source,{},old->id);continue;}
        const auto earlier=std::find_if(unique.begin(),unique.end(),[&](const auto& p){return duplicate(c.geometry,p.geometry,tolerance);});
        if(earlier!=unique.end()) {
            // Selecting a face plus its edge in either order must preserve the
            // validated boundary's policy even when the edge was seen first.
            earlier->faceBoundary=earlier->faceBoundary || c.faceBoundary;
            report(Code::Duplicate,c.context+"Duplicate projected geometry in this batch; boundary piece skipped.",c.source,earlier->source);continue;
        }
        unique.push_back(c);
    }
    for(size_t i=0;i<unique.size();++i) {
        const auto& c=unique[i];
        // A valid face contributes its complete analytic boundary. Existing
        // references may overlap it, but must never trim or block that contour.
        // Exact duplicates were already reused above; standalone edge and
        // within-batch overlap checks retain their existing strict behavior.
        for(const auto& e:existing) if(!c.faceBoundary && e.workplaneId==workplane.id && e.role==Sketch::Role::Construction && overlap(c.geometry,e.geometry,tolerance))
            report(Code::PartialOverlap,c.context+"The projected geometry overlaps existing Construction geometry; no automatic trimming is performed.",c.source,{},e.id);
        for(size_t j=0;j<i;++j) if(overlap(c.geometry,unique[j].geometry,tolerance))
            report(Code::PartialOverlap,c.context+"Projected curves overlap over a nonzero interval; no automatic trimming is performed. "+unique[j].context,c.source,unique[j].source);
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
