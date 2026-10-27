#include "SketchProfileBuilder.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Wire.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <Precision.hxx>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace PV=SketchProfileValidation;
namespace PB=SketchProfileBuilder;
using namespace Sketch;
namespace {
constexpr double pi=std::numbers::pi;
void check(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
Entity entity(QString id,Geometry geometry) {Entity e;e.id=id;e.workplaneId="wp";e.geometry=std::move(geometry);return e;}
std::vector<Entity> rectangle(double x=0,double y=0,double width=100,double height=50,QString prefix="r") {
    const Point p[]{{x,y},{x+width,y},{x+width,y+height},{x,y+height}};std::vector<Entity> result;
    for(size_t i=0;i<4;++i)result.push_back(entity(prefix+QString::number(i),Segment{p[i],p[(i+1)%4]}));return result;
}
void append(std::vector<Entity>& target,const std::vector<Entity>& other) {target.insert(target.end(),other.begin(),other.end());}
void unchanged(const PV::Result& a,const PV::Result& b) {
    check(a.status==b.status && a.contours.size()==b.contours.size() && a.regions.size()==b.regions.size(),"builder changed validation status/counts");
    for(size_t i=0;i<a.contours.size();++i) {
        const auto& ca=a.contours[i];const auto& cb=b.contours[i];
        check(ca.signedArea==cb.signedArea && ca.parent==cb.parent && ca.depth==cb.depth
            && ca.maximumEndpointGap==cb.maximumEndpointGap && ca.orientation==cb.orientation,"builder changed contour metadata");
        for(size_t j=0;j<ca.curves.size();++j)check(ca.curves[j].entityId==cb.curves[j].entityId
            && ca.curves[j].geometry==cb.curves[j].geometry && ca.curves[j].reversed==cb.curves[j].reversed,"builder changed source curve/ID");
    }
    for(size_t i=0;i<a.regions.size();++i)check(a.regions[i].outer==b.regions[i].outer && a.regions[i].holes==b.regions[i].holes,"builder changed regions");
}
PB::Result constructed(const std::vector<Entity>& entities,const std::vector<double>& areas,const gp_Ax3& frame=gp_Ax3()) {
    const auto original=entities;const auto validation=PV::validate(entities,"wp");const auto saved=validation;
    check(validation.status==PV::Status::Valid,"test input failed Profile validation");
    auto result=PB::build(validation,frame);unchanged(saved,validation);check(original==entities,"source entities changed");
    if(!result.success) {for(const auto& d:result.diagnostics)std::cerr<<d.message.toStdString()<<'\n';throw std::runtime_error("expected successful face construction");}
    check(result.diagnostics.empty() && result.faces.size()==areas.size() && result.wires.size()==validation.contours.size(),"wrong output faces/wires/diagnostics");
    check(result.effectiveVertexTolerance==1.e-7,"effective tolerance not reported");
    std::vector<double> actual;for(const auto& face:result.faces) {
        check(face.region<validation.regions.size() && face.outer==validation.regions[face.region].outer
            && face.holes==validation.regions[face.region].holes,"region references changed");
        check(BRepCheck_Analyzer(face.shape).IsValid(),"invalid face");
        size_t wires=0;for(TopExp_Explorer it(face.shape,TopAbs_WIRE);it.More();it.Next())++wires;
        check(wires==1+face.holes.size(),"hole boundary missing");
        const BRepAdaptor_Surface plane(face.shape);check(plane.GetType()==GeomAbs_Plane,"nonplanar face");
        check(plane.Plane().Distance(frame.Location())<1.e-8,"face on wrong plane");
        GProp_GProps area;BRepGProp::SurfaceProperties(face.shape,area);actual.push_back(area.Mass());
        for(TopExp_Explorer it(face.shape,TopAbs_VERTEX);it.More();it.Next()) {
            const auto vertex=TopoDS::Vertex(it.Current());
            check(BRep_Tool::Tolerance(vertex)<=1.e-7,"excess vertex tolerance");
            check(plane.Plane().Distance(BRep_Tool::Pnt(vertex))<=1.e-7,"world vertex left Workplane");
        }
    }
    auto expected=areas;std::sort(expected.begin(),expected.end());std::sort(actual.begin(),actual.end());
    for(size_t i=0;i<actual.size();++i)check(std::abs(actual[i]-expected[i])<1.e-7*std::max(1.,expected[i]),"wrong face area");
    for(const auto& wire:result.wires) {
        check(wire.contour<validation.contours.size() && BRep_Tool::IsClosed(wire.shape)
            && BRepCheck_Wire(wire.shape).Closed()==BRepCheck_NoError && BRepCheck_Analyzer(wire.shape).IsValid(),"wire not certified closed");
        const auto& contour=validation.contours[wire.contour];check(wire.edges.size()==contour.curves.size(),"edge count/source membership changed");
        for(size_t i=0;i<wire.edges.size();++i) {
            const auto& edge=wire.edges[i];const auto& source=contour.curves[i];
            check(edge.entityId==source.entityId,"edge ID mapping lost");BRepAdaptor_Curve curve(edge.shape);
            const bool segment=std::holds_alternative<Segment>(source.geometry);
            check(curve.GetType()==(segment?GeomAbs_Line:GeomAbs_Circle),"analytic geometry approximated");
            if(const auto* arc=std::get_if<Arc>(&source.geometry)) {
                check(std::abs(curve.FirstParameter()-std::min(arc->startAngle,arc->startAngle+arc->sweepAngle))<1.e-12,"arc start parameter changed");
                check(std::abs(curve.LastParameter()-curve.FirstParameter()-std::abs(arc->sweepAngle))<1.e-12,"arc short/long route changed");
                check((edge.shape.Orientation()==TopAbs_REVERSED)==(arc->sweepAngle<0),"arc traversal changed");
                check(std::abs(curve.Circle().Radius()-arc->radius)<1.e-12,"arc radius changed");
            }
        }
    }
    return result;
}
void refused(const PV::Result& validation,PB::Code code,const gp_Ax3& frame=gp_Ax3(),PB::Policy policy={}) {
    const auto saved=validation;const auto result=PB::build(validation,frame,policy);unchanged(saved,validation);
    if(result.success || result.diagnostics.empty() || result.diagnostics.front().code!=code) {
        for(const auto& d:result.diagnostics)std::cerr<<int(d.code)<<": "<<d.message.toStdString()<<'\n';throw std::runtime_error("wrong construction refusal");
    }
    check(result.faces.empty() && result.wires.empty() && result.effectiveVertexTolerance==0,"failure exposed partial certified output");
    check(!result.diagnostics.front().message.isEmpty(),"no readable failure diagnostic");
}
void basicAndCurved() {
    constructed(rectangle(),{5000});
    auto clockwise=rectangle();clockwise.back().id="a-first";
    check(PV::validate(clockwise,"wp").contours[0].orientation==PV::Orientation::Clockwise,"CW test not clockwise");
    constructed(clockwise,{5000});constructed({entity("circle",Circle{{0,0},10})},{100*pi});
    auto d=std::vector<Entity>{entity("arc",Arc{{0,0},10,0,pi}),entity("line",Segment{{-10,0},{10,0}})};
    constructed(d,{50*pi});
    // All drawing directions and orderings preserve material and source values.
    for(unsigned mask=0;mask<4;++mask) {
        auto reversed=d;
        for(size_t i=0;i<reversed.size();++i)if(mask&(1U<<i)) {
            if(auto* arc=std::get_if<Arc>(&reversed[i].geometry))*arc=reversedArc(*arc);
            else {auto& s=std::get<Segment>(reversed[i].geometry);std::swap(s.start,s.end);}
        }
        constructed(reversed,{50*pi});std::reverse(reversed.begin(),reversed.end());constructed(reversed,{50*pi});
    }
    for(double sweep:{.5*pi,-.5*pi,1.5*pi,-1.5*pi}) {
        const Arc arc{{2,3},10,1.8*pi,sweep};
        const auto p=[&](double t){return Point{arc.center.x+10*std::cos(t),arc.center.y+10*std::sin(t)};};
        constructed({entity("arc",arc),entity("chord",Segment{p(arc.startAngle+arc.sweepAngle),p(arc.startAngle)})},
            {50*std::abs(sweep-std::sin(sweep))});
    }
    const auto rounded=std::vector<Entity>{entity("a",Segment{{0,-2},{6,-2}}),entity("b",Arc{{6,0},2,-pi/2,pi}),
        entity("c",Segment{{6,2},{0,2}}),entity("d",Arc{{0,0},2,pi/2,pi})};
    constructed(rounded,{24+4*pi});
    constructed({entity("a",Arc{{0,0},10,0,pi}),entity("b",Arc{{0,0},10,pi,pi})},{100*pi});
    for(const gp_Ax3 frame:{gp_Ax3(gp_Pnt(12,-34,70),gp_Dir(0,0,1)),gp_Ax3(gp_Pnt(8,-5,20),gp_Dir(1,2,3),gp_Dir(2,-1,0))}) {
        const auto moved=constructed(d,{50*pi},frame);
        const auto& edge=moved.wires[0].edges[0];BRepAdaptor_Curve curve(edge.shape);
        check(curve.Circle().Location().Distance(worldPoint({0,0},frame))<1.e-10,"arc center not transformed correctly");
        const auto box=constructed(rectangle(),{5000},frame);
        const auto expected=worldPoint({0,0},frame);bool found=false;
        for(TopExp_Explorer it(box.faces[0].shape,TopAbs_VERTEX);it.More();it.Next())if(BRep_Tool::Pnt(TopoDS::Vertex(it.Current())).Distance(expected)<1.e-10)found=true;
        check(found,"translated/rotated rectangle vertex missing");
    }
}
void holesAndIslands() {
    auto input=rectangle();input.push_back(entity("hole",Circle{{50,25},10}));auto result=constructed(input,{5000-100*pi});
    BRepClass_FaceClassifier hole(result.faces[0].shape,gp_Pnt(50,25,0),1.e-7);check(hole.State()==TopAbs_OUT,"circular hole filled");
    BRepClass_FaceClassifier material(result.faces[0].shape,gp_Pnt(5,5,0),1.e-7);check(material.State()==TopAbs_IN,"outer material missing");
    constructed({entity("outer",Circle{{0,0},10}),entity("hole",Circle{{0,0},5})},{75*pi});
    const gp_Ax3 rotated(gp_Pnt(10,3,-2),gp_Dir(1,-2,1),gp_Dir(2,1,0));
    const auto transformed=constructed(input,{5000-100*pi},rotated);
    BRepClass_FaceClassifier rotatedHole(transformed.faces[0].shape,worldPoint({50,25},rotated),1.e-7);
    check(rotatedHole.State()==TopAbs_OUT,"rotated circular hole filled");
    input.push_back(entity("island",Circle{{50,25},2}));constructed(input,{5000-100*pi,4*pi});
    input=rectangle();input.push_back(entity("hole1",Circle{{50,25},10}));append(input,rectangle(150,0,100,50,"second"));
    input.push_back(entity("hole2",Circle{{200,25},5}));constructed(input,{5000-100*pi,5000-25*pi});
    std::reverse(input.begin(),input.end());
    for(auto& e:input)if(auto* s=std::get_if<Segment>(&e.geometry))std::swap(s->start,s->end);
    constructed(input,{5000-100*pi,5000-25*pi});
}
void toleranceAndRefusal() {
    auto input=rectangle();std::get<Segment>(input.back().geometry).end.y=.5e-7;
    auto validation=PV::validate(input,"wp");check(validation.status==PV::Status::Valid,"bounded gap test input invalid");
    auto result=PB::build(validation,gp_Ax3());
    check(result.success && result.effectiveVertexTolerance<=validation.connectionTolerance && result.wires[0].maximumEndpointGap>0,"bounded vertex policy did not handle approved gap");
    refused(validation,PB::Code::InvalidPolicy,gp_Ax3(),{2.e-7});refused(validation,PB::Code::InvalidPolicy,gp_Ax3(),{1.e-9});
    refused(validation,PB::Code::InvalidPolicy,gp_Ax3(),{std::numeric_limits<double>::quiet_NaN()});
    auto largerGap=rectangle();std::get<Segment>(largerGap.back().geometry).end.y=5.e-7;
    const auto larger=PV::validate(largerGap,"wp",1.e-6);check(larger.status==PV::Status::Valid,"custom Sketch tolerance input invalid");
    refused(larger,PB::Code::ClosureFailure);
    const auto bounded=PB::build(larger,gp_Ax3(),{1.e-6});
    check(bounded.success && bounded.effectiveVertexTolerance==1.e-6,"approved custom construction tolerance failed");
    input.pop_back();refused(PV::validate(input,"wp"),PB::Code::InvalidValidation);
    refused(PV::validate({entity("ellipse",Ellipse{})},"wp"),PB::Code::InvalidValidation);
    refused(PV::validate({entity("ellipse-arc",EllipticalArc{})},"wp"),PB::Code::InvalidValidation);
    validation=PV::validate(rectangle(),"wp");validation.contours[0].curves[0].geometry=Segment{{0,0},{0,0}};
    refused(validation,PB::Code::InvalidValidation);
    validation=PV::validate(rectangle(),"wp");
    auto& altered=std::get<Segment>(validation.contours[0].curves[0].geometry);altered.start.y+=2.e-7;
    refused(validation,PB::Code::InvalidValidation);
    validation=PV::validate(rectangle(),"wp");std::swap(validation.contours[0].curves[0],validation.contours[0].curves[2]);
    refused(validation,PB::Code::ClosureFailure);
    validation=PV::validate(rectangle(),"wp");validation.regions[0].outer=99;refused(validation,PB::Code::InvalidValidation);
    input=rectangle();input.push_back(entity("hole",Circle{{50,25},10}));validation=PV::validate(input,"wp");validation.regions[0].holes.clear();
    refused(validation,PB::Code::InvalidValidation);
    validation=PV::validate(rectangle(),"wp");refused(validation,PB::Code::NumericalFailure,gp_Ax3(gp_Pnt(1.e12,0,0),gp_Dir(0,0,1)));
    // Profile topology is valid but two distinct junctions have overlapping
    // tolerance balls; fail rather than merge a tiny edge or inflate tolerance.
    input={entity("a",Segment{{0,0},{1.5e-7,0}}),entity("b",Segment{{1.5e-7,0},{10,0}}),
        entity("c",Segment{{10,0},{10,5}}),entity("d",Segment{{10,5},{0,5}}),entity("e",Segment{{0,5},{0,0}})};
    validation=PV::validate(input,"wp");check(validation.status==PV::Status::Valid,"ambiguous construction test topology invalid");
    refused(validation,PB::Code::AmbiguousVertices);
    // A later failed contour cannot leak the first already constructed region.
    append(input,rectangle(30,0,10,5,"0-good"));validation=PV::validate(input,"wp");
    refused(validation,PB::Code::AmbiguousVertices);
}
}
int main() {
    try {basicAndCurved();holesAndIslands();toleranceAndRefusal();std::cout<<"Profile wire/face construction passed\n";return 0;}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
