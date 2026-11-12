#include "ConstructionGeometry.h"
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pln.hxx>
#include <gp_Circ.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Cone.hxx>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <cmath>
using namespace Construction;
namespace {
void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
Reference ref(ReferenceKind kind,gp_Pnt p=gp_Pnt(),gp_Dir n=gp_Dir(0,0,1)) {return {kind,gp_Ax3(p,n),p,false};}
Object compute(Method m,std::vector<Reference> refs={},gp_Vec vector={},double value=0,gp_Vec angles={}) {
    Object result;result.id="result";result.name="Result";QString error;
    check(build(Request{m,refs,vector,angles,value,false},result,error),error.toUtf8().constData());check(valid(result,error),"invalid computed object");return result;
}
void rejects(Method m,std::vector<Reference> refs,gp_Vec vector={},double value=0) {
    Object result;result.id="retained";result.name="Original";const auto original=result;QString error;
    check(!build(Request{m,refs,vector,{},value,false},result,error),"ambiguous construction accepted");
    check(!error.isEmpty() && equivalent(result,original),"failed construction changed the result or lacked explanation");
}
void point(const Object& o,double x,double y,double z) {check(o.frame.Location().IsEqual(gp_Pnt(x,y,z),1.e-7),"unexpected analytic position");}
void presentationChecks() {
    for(const auto direction:{gp_Dir(1,0,0),gp_Dir(1,2,3)}) for(double length:{0.01,10.0,1200.0}) {
        const gp_Pnt a(20,30,40),b=a.Translated(gp_Vec(direction)*length);
        Request request;request.method=Method::AxisTwoPoints;request.references={ref(ReferenceKind::Point,a),ref(ReferenceKind::Point,b)};
        for(bool reverse:{false,true}) {
            request.reverse=reverse;Object axis;axis.id="axis";axis.name="AX1";QString error;
            check(build(request,axis,error),"two-point display construction failed");point(axis,a.X(),a.Y(),a.Z());
            check(displayCenter(axis).IsEqual(a.Translated(gp_Vec(direction)*(length*0.5)),1.e-7),"axis not visually centered between points");
            double low=1.e30,high=-1.e30;
            for(const auto& end:displayBounds(axis)) {const double t=gp_Vec(a,end).Dot(gp_Vec(direction));low=std::min(low,t);high=std::max(high,t);}
            check(low<0 && high>length,"axis did not extend before first and past second point");
            auto moved=axis;request.method=Method::AxisMove;request.references={reference(axis)};request.vector=gp_Vec(3,4,5);request.reverse=false;
            check(build(request,moved,error) && displayCenter(moved).IsEqual(displayCenter(axis).Translated(request.vector),1.e-7),"move lost local display center");
            request.method=Method::AxisTwoPoints;request.references={ref(ReferenceKind::Point,a),ref(ReferenceKind::Point,b)};
        }
    }
    auto plane=compute(Method::PlaneThreePoints,{ref(ReferenceKind::Point,gp_Pnt(100,20,5)),ref(ReferenceKind::Point,gp_Pnt(130,20,5)),ref(ReferenceKind::Point,gp_Pnt(100,50,5))});
    check(displayCenter(plane).IsEqual(gp_Pnt(110,30,5),1.e-7),"three-point plane not centered on selection");point(plane,100,20,5);
    auto offset=compute(Method::PlaneOffset,{reference(plane)},{},15);
    check(displayCenter(offset).IsEqual(gp_Pnt(110,30,20),1.e-7) && offset.extent==plane.extent,"plane offset lost centering or grew extent");
    auto rotated=plane;Request rotate;rotate.method=Method::PlaneRotate;rotate.references={reference(plane)};rotate.vector=gp_Vec(1,0,0);rotate.value=90;QString rotateError;
    gp_Trsf rotation;rotation.SetRotation(gp_Ax1(plane.frame.Location(),gp_Dir(1,0,0)),std::acos(-1.0)*0.5);
    check(build(rotate,rotated,rotateError) && displayCenter(rotated).IsEqual(displayCenter(plane).Transformed(rotation),1.e-7),"rotation lost local plane display center");
    gp_Trsf placement;placement.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),0.7);placement.SetTranslationPart(gp_Vec(1000,2000,3000));
    const auto face=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,5),gp_Dir(0,0,1)),100,140,200,260).Face();
    Reference source;QString error;check(resolve(face,placement,ReferenceKind::Plane,source,error),"off-origin face resolver failed");
    plane=compute(Method::PlaneFace,{source});
    check(displayCenter(plane).IsEqual(gp_Pnt(120,230,5).Transformed(placement),1.e-6),"plane used support origin instead of transformed face center");
    check(gp_Pln(plane.frame).Distance(displayCenter(plane))<1.e-6,"display center left analytic support plane");
    Object bad=plane;bad.displayOffset.SetZ(1);check(!valid(bad,error),"off-plane display offset accepted");
    std::array<int,3> counters{};Object named;named.kind=Kind::Point;named.name="PT42";advanceNameCounters({named},counters);
    check(counters==std::array<int,3>{0,0,42} && namePrefix(Kind::Workplane)=="WP" && namePrefix(Kind::Axis)=="AX","independent name sequences wrong");
}
void midplaneChecks() {
    const auto plane=[](gp_Pnt origin,gp_Dir normal) {return ref(ReferenceKind::Plane,origin,normal);};
    const auto evaluate=[](const Reference& a,const Reference& b,bool alternative=false,bool reverse=false) {
        Request request;request.method=Method::PlaneMidplane;request.references={a,b};request.alternativeBisector=alternative;request.reverse=reverse;
        Object result;result.id="midplane";result.name="Midplane";QString error;
        const bool ok=build(request,result,error);check(ok,error.toUtf8().constData());return result;
    };
    const auto equidistant=[](const Object& middle,const Reference& a,const Reference& b) {
        for(double x:{-50.0,0.0,20.0}) for(double y:{-17.0,0.0,33.0}) {
            const auto p=middle.frame.Location().Translated(gp_Vec(middle.frame.XDirection())*x+gp_Vec(middle.frame.YDirection())*y);
            check(std::abs(gp_Pln(a.frame).Distance(p)-gp_Pln(b.frame).Distance(p))<1.e-6,"midplane is not equidistant from both supports");
        }
    };
    const auto samePlane=[](const Object& a,const Object& b) {
        check(a.frame.Direction().IsParallel(b.frame.Direction(),1.e-9) && gp_Pln(a.frame).Distance(b.frame.Location())<1.e-6,"midplane depends on normal orientation or operand order");
    };
    const auto a=plane(gp_Pnt(17,3,2),gp_Dir(0,0,1)),b=plane(gp_Pnt(-11,20,14),gp_Dir(0,0,-1));
    auto middle=evaluate(a,b);point(middle,3,11.5,8);equidistant(middle,a,b);samePlane(middle,evaluate(b,a));
    auto flipped=b;flipped.frame.ZReverse();flipped.frame.YReverse();samePlane(middle,evaluate(a,flipped));
    samePlane(middle,evaluate(a,b,false,true));
    Request bad;bad.method=Method::PlaneMidplane;bad.references={a,b};bad.alternativeBisector=true;
    Object unchanged;unchanged.id="unchanged";unchanged.name="Keep";const auto original=unchanged;QString error;
    check(!build(bad,unchanged,error) && equivalent(original,unchanged) && !error.isEmpty(),"parallel second solution accepted or changed original");
    rejects(Method::PlaneMidplane,{a,a});
    const double pi=std::acos(-1.0),angle=pi/3;
    const auto flat=plane(gp_Pnt(),gp_Dir(0,0,1)),slope=plane(gp_Pnt(),gp_Dir(-std::sin(angle),0,std::cos(angle)));
    const auto bisector=evaluate(flat,slope),other=evaluate(flat,slope,true);
    check(bisector.frame.Direction().IsParallel(gp_Dir(-0.5,0,std::sqrt(3.0)/2),1.e-9),"60-degree wedge did not produce 30-degree midplane");
    check(std::abs(bisector.frame.Direction().Dot(other.frame.Direction()))<1.e-10,"angle bisectors are not perpendicular");
    equidistant(bisector,flat,slope);equidistant(other,flat,slope);
    auto opposite=slope;opposite.frame.ZReverse();opposite.frame.YReverse();
    samePlane(bisector,evaluate(flat,opposite));samePlane(other,evaluate(flat,opposite,true));samePlane(bisector,evaluate(slope,flat));
    const auto offset=plane(gp_Pnt(0,0,20),slope.frame.Direction());
    equidistant(evaluate(flat,offset),flat,offset);equidistant(evaluate(flat,offset,true),flat,offset);
    const auto right=plane(gp_Pnt(),gp_Dir(1,0,0));equidistant(evaluate(flat,right),flat,right);equidistant(evaluate(flat,right,true),flat,right);
    const auto nearly=plane(gp_Pnt(0,0,20),gp_Dir(1.e-8,0,1));
    const auto nearMiddle=evaluate(flat,nearly);check(nearMiddle.frame.Location().Distance(gp_Pnt(0,0,10))<1.e-5,"nearly parallel midplane origin escaped to remote intersection");
    equidistant(nearMiddle,flat,nearly);
    gp_Trsf transform;transform.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),0.7);transform.SetTranslationPart(gp_Vec(1.e8,-2.e8,3.e8));
    auto ta=flat,tb=offset;ta.frame.Transform(transform);tb.frame.Transform(transform);
    auto expected=evaluate(flat,offset);expected.frame.Transform(transform);const auto transformed=evaluate(ta,tb);
    samePlane(expected,transformed);equidistant(transformed,ta,tb);
}
}
int main() {
 try {
    for(auto method:{Method::PlaneXY,Method::PlaneXZ,Method::PlaneYZ}) {
        const gp_Dir normal=method==Method::PlaneXY?gp_Dir(0,0,1):method==Method::PlaneXZ?gp_Dir(0,-1,0):gp_Dir(1,0,0);
        const gp_Dir x=method==Method::PlaneYZ?gp_Dir(0,1,0):gp_Dir(1,0,0);
        for(double offset:{0.0,12.345678,-9.5}) for(bool reverse:{false,true}) {
            Request q;q.method=method;q.value=offset;q.reverse=reverse;Object result;QString error;
            check(build(q,result,error),"preset failed without geometry or references");
            check(result.frame.Direct() && result.frame.XDirection().Dot(x)>1-1.e-12,"preset frame is not right-handed or has wrong local X");
            check(std::abs(result.frame.Direction().Dot(normal)-(reverse?-1:1))<1.e-12,"preset normal incorrect");
            check(result.frame.Location().IsEqual(gp_Pnt().Translated(gp_Vec(normal)*(reverse?-offset:offset)),1.e-12),"preset offset is not along its displayed normal");
            const gp_Vec cross=gp_Vec(result.frame.XDirection()).Crossed(gp_Vec(result.frame.YDirection()));
            check(cross.Dot(gp_Vec(result.frame.Direction()))>1-1.e-12,"preset local axes left-handed");
        }
    }
    midplaneChecks();
    presentationChecks();
    const auto p=ref(ReferenceKind::Point,gp_Pnt(1,2,3)),x=ref(ReferenceKind::Point,gp_Pnt(5,2,3)),y=ref(ReferenceKind::Point,gp_Pnt(1,7,3));
    const auto xy=ref(ReferenceKind::Plane,gp_Pnt(0,0,3)),yz=ref(ReferenceKind::Plane,gp_Pnt(1,0,0),gp_Dir(1,0,0));
    const auto axis=ref(ReferenceKind::Axis,gp_Pnt(1,2,0)),line=ref(ReferenceKind::Line,gp_Pnt(1,2,3),gp_Dir(1,0,0));
    auto plane=compute(Method::PlaneThreePoints,{p,x,y});point(plane,1,2,3);check(plane.frame.XDirection().IsEqual(gp_Dir(1,0,0),1.e-10),"three-point X direction");
    check(plane.frame.Direction().IsEqual(gp_Dir(0,0,1),1.e-10),"three-point normal");
    point(compute(Method::PlaneNormalAxisPoint,{axis,p}),1,2,3);
    check(compute(Method::PlaneThroughAxisPoint,{axis,x}).frame.Direction().IsEqual(gp_Dir(0,1,0),1.e-10),"axis-containing plane");
    point(compute(Method::PlaneFace,{xy}),0,0,3);point(compute(Method::PlaneOffset,{xy},{},7),0,0,10);
    check(std::abs(compute(Method::PlaneEdgeFace,{line,xy}).frame.Direction().Y())>0.999,"edge-normal plane");
    check(std::abs(compute(Method::PlaneEdgeAngle,{line,xy},{},90).frame.Direction().Y())>0.999,"edge rotation");
    const auto axisX=ref(ReferenceKind::Axis,gp_Pnt(1,2,3),gp_Dir(1,0,0));
    check(std::abs(compute(Method::PlaneAxisAngle,{axisX,xy},{},90).frame.Direction().Y())>0.999,"axis rotation");
    point(compute(Method::PlaneParallelPoint,{xy,p}),1,2,3);
    auto xyz=compute(Method::PlaneXYZ,{},gp_Vec(8,9,10),0,gp_Vec(90,0,0));point(xyz,8,9,10);check(xyz.frame.Direction().Y()<-0.999,"XYZ world rotation convention");
    point(compute(Method::PlaneMove,{xy},gp_Vec(3,4,5)),3,4,8);
    check(compute(Method::PlaneRotate,{xy},gp_Vec(1,0,0),90).frame.Direction().Y()<-0.999,"plane rotate");
    check(compute(Method::AxisTwoPoints,{p,x}).frame.Direction().IsEqual(gp_Dir(1,0,0),1.e-10),"axis two points");
    for(auto method:{Method::AxisPlanes,Method::AxisFaces}) {
        const auto intersection=compute(method,{xy,yz});point(intersection,1,0,3);check(std::abs(intersection.frame.Direction().Y())>0.999,"plane intersection line");
    }
    check(compute(Method::AxisEdge,{line}).frame.Direction().X()>0.999,"edge axis");
    point(compute(Method::AxisCylinder,{ref(ReferenceKind::Cylinder,gp_Pnt(6,7,8))}),6,7,8);
    check(compute(Method::AxisPointDirection,{p},gp_Vec(0,4,0)).frame.Direction().Y()>0.999,"direction normalization");
    point(compute(Method::AxisNormalPlane,{xy}),0,0,3);point(compute(Method::AxisMove,{axis},gp_Vec(4,5,6)),5,7,6);
    check(compute(Method::AxisRotate,{axis},gp_Vec(1,0,0),90).frame.Direction().Y()<-0.999,"axis rotate");
    point(compute(Method::PointXYZ,{},gp_Vec(7,8,9)),7,8,9);point(compute(Method::PointVertex,{p}),1,2,3);
    auto edge=ref(ReferenceKind::Edge);edge.midpoint=gp_Pnt(4,5,6);point(compute(Method::PointEdgeMiddle,{edge}),4,5,6);
    point(compute(Method::PointOnAxis,{axis},{},7),1,2,7);point(compute(Method::PointAxisPlane,{axis,xy}),1,2,3);
    point(compute(Method::PointAxes,{axis,ref(ReferenceKind::Axis,gp_Pnt(0,2,3),gp_Dir(1,0,0))}),1,2,3);
    point(compute(Method::PointCircleCenter,{ref(ReferenceKind::Circle,gp_Pnt(4,5,6))}),4,5,6);
    point(compute(Method::PointOffset,{p},gp_Vec(4,5,6)),5,7,9);
    point(compute(Method::PointProject,{ref(ReferenceKind::Point,gp_Pnt(7,8,20)),xy}),7,8,3);
    rejects(Method::PlaneThreePoints,{p,p,y});rejects(Method::PlaneThreePoints,{p,x,ref(ReferenceKind::Point,gp_Pnt(9,2,3))});
    rejects(Method::PlaneThroughAxisPoint,{axis,ref(ReferenceKind::Point,gp_Pnt(1,2,9))});
    rejects(Method::PlaneEdgeFace,{ref(ReferenceKind::Line),xy});rejects(Method::PlaneAxisAngle,{axis,xy});
    rejects(Method::AxisTwoPoints,{p,p});rejects(Method::AxisPlanes,{xy,xy});
    rejects(Method::AxisFaces,{xy,ref(ReferenceKind::Plane,gp_Pnt(0,0,8))});
    rejects(Method::AxisPointDirection,{p});rejects(Method::AxisRotate,{axis});
    rejects(Method::PointAxisPlane,{ref(ReferenceKind::Axis,{},gp_Dir(1,0,0)),xy});
    rejects(Method::PointAxes,{axis,ref(ReferenceKind::Axis,gp_Pnt(9,3,6),gp_Dir(1,0,0))});
    rejects(Method::PointAxes,{axis,axis});rejects(Method::PointXYZ,{},gp_Vec(std::numeric_limits<double>::infinity(),0,0));
    edge.closed=true;rejects(Method::PointEdgeMiddle,{edge});
    gp_Trsf placement;placement.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(0,1,0)),0.6);placement.SetTranslationPart(gp_Vec(20,30,40));
    Reference resolved;QString error;
    const auto local=gp_Pnt(2,3,4);check(resolve(BRepBuilderAPI_MakeVertex(local).Shape(),placement,ReferenceKind::Point,resolved,error),"vertex resolver");
    check(resolved.frame.Location().IsEqual(local.Transformed(placement),1.e-9),"placement applied incorrectly");
    const auto arc=BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(),gp_Dir(0,0,1)),10),0.0,3.14159265358979323846).Shape();
    check(resolve(arc,placement,ReferenceKind::Edge,resolved,error),"arc-length midpoint resolver");
    check(resolved.midpoint.IsEqual(gp_Pnt(0,10,0).Transformed(placement),1.e-7),"arc-length midpoint wrong");
    check(resolve(arc,placement,ReferenceKind::Circle,resolved,error),"circle center resolver");
    check(resolved.frame.Location().IsEqual(gp_Pnt().Transformed(placement),1.e-9),"circle center placement");
    check(!resolve(arc,placement,ReferenceKind::Line,resolved,error),"curved edge treated as line");
    const auto face=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,5),gp_Dir(0,0,1)),-5,5,-5,5).Face();
    check(resolve(face,placement,ReferenceKind::Plane,resolved,error),"planar resolver");
    check(resolved.frame.Location().IsEqual(gp_Pnt(0,0,5).Transformed(placement),1.e-9),"face placement");
    const auto saved=compute(Method::PlaneFace,{resolved}); // No source handle retained.
    resolved.frame.SetLocation(gp_Pnt(900,900,900));check(saved.frame.Location().Distance(resolved.frame.Location())>100,"persistent construction dependency");
    // v0.5.3: cone axes through the same Cylinder role, with placement.
    {
        const auto cone=BRepBuilderAPI_MakeFace(gp_Cone(gp_Ax3(gp_Pnt(1,2,3),gp_Dir(0,0,1)),0.3,10),0,6.28318530717958647692,0,20).Face();
        check(resolve(cone,placement,ReferenceKind::Cylinder,resolved,error),"conical face axis resolver");
        check(resolved.frame.Location().IsEqual(gp_Pnt(1,2,3).Transformed(placement),1.e-9)
            && resolved.frame.Direction().IsEqual(gp_Dir(0,0,1).Transformed(placement),1.e-12),"cone axis placement");
        const auto axisFromCone=compute(Method::AxisCylinder,{resolved});
        check(axisFromCone.kind==Kind::Axis && axisFromCone.frame.Direction().IsEqual(resolved.frame.Direction(),1.e-12),"Axis from cone");
        check(!resolve(BRepBuilderAPI_MakeFace(gp_Pln(),-1,1,-1,1).Face(),placement,ReferenceKind::Cylinder,resolved,error),"plane accepted as cone/cylinder");
    }
    // v0.5.3: Axis normal to a plane through the projection of a point.
    {
        const auto tilted=ref(ReferenceKind::Plane,gp_Pnt(10,0,0),gp_Dir(1,1,0));
        const auto normal=compute(Method::AxisFaceNormal,{tilted,ref(ReferenceKind::Point,gp_Pnt(0,0,7))});
        check(normal.frame.Direction().IsEqual(gp_Dir(1,1,0),1.e-12),"face normal axis direction");
        point(normal,5,5,7); // projection of (0,0,7) onto x+y=10
        Request reversed{Method::AxisFaceNormal,{tilted,ref(ReferenceKind::Point,gp_Pnt(0,0,7))},{},{},0,true};Object r;r.id="r";r.name="R";
        check(build(reversed,r,error) && r.frame.Direction().IsEqual(gp_Dir(-1,-1,0),1.e-12),"face normal axis Reverse");
        rejects(Method::AxisFaceNormal,{tilted});
    }
    std::cout<<"All construction methods, analytic references and refusal checks passed.\n";return 0;
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
