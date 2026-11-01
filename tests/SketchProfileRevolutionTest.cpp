#include "SketchProfileRevolution.h"
#include "BooleanGeometry.h"
#include <BRepTools.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopExp.hxx>
#include <sstream>
#include <iostream>
#include <numbers>
#include <limits>
#include <stdexcept>

using namespace Sketch;
namespace RV=SketchProfileRevolution;
namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
Entity entity(QString id,Geometry geometry) {Entity e;e.id=id;e.workplaneId="wp";e.geometry=geometry;return e;}
std::vector<Entity> rectangle(double x=2,double y=0,double w=2,double h=3,QString prefix={}) {
    return {entity(prefix+"a",Segment{{x,y},{x+w,y}}),entity(prefix+"b",Segment{{x+w,y},{x+w,y+h}}),
            entity(prefix+"c",Segment{{x+w,y+h},{x,y+h}}),entity(prefix+"d",Segment{{x,y+h},{x,y}})};
}
auto faces(const std::vector<Entity>& input,const gp_Ax3& frame=gp_Ax3()) {
    return SketchProfileBuilder::build(SketchProfileValidation::validate(input,"wp"),frame);
}
void valid(const RV::Result& result,double volume,size_t count=1) {
    if(!result.success)for(const auto& d:result.diagnostics)std::cerr<<d.message.toStdString()<<'\n';
    check(result.success && result.diagnostics.empty() && result.solidCount==count,"revolution failed");
    double measured=0;size_t solids=0;
    check(BooleanGeometry::certifiedSolid(result.shape,measured,solids) && solids==count,"uncertified result");
    check(std::abs(measured-volume)<std::max(1.e-7,volume*1.e-9)
          && std::abs(result.volume-measured)<1.e-9,"wrong revolved volume");
}
void failed(const RV::Result& result,std::optional<RV::Code> code={}) {
    if(result.success)std::cerr<<"Unexpected successful revolution: volume="<<result.volume<<"\n";
    check(!result.success && result.shape.IsNull() && result.solidCount==0 && result.volume==0
          && !result.diagnostics.empty() && !result.diagnostics.front().message.isEmpty(),"partial/invalid success");
    if(code)check(result.diagnostics.front().code==*code,"wrong failure diagnostic");
}
gp_Pnt centre(const TopoDS_Shape& shape) {GProp_GProps mass;BRepGProp::VolumeProperties(shape,mass);return mass.CentreOfMass();}
std::string serialized(const TopoDS_Shape& shape) {std::ostringstream stream;BRepTools::Write(shape,stream);return stream.str();}
}
int main() {try {
    constexpr double pi=std::numbers::pi;
    const gp_Ax1 axis(gp_Pnt(0,0,0),gp_Dir(0,1,0));
    const auto input=rectangle(),saved=input;const auto profile=faces(input);
    check(profile.success,"profile fixture failed");
    const auto original=serialized(profile.faces.front().shape);
    for(double angle:{1.,-1.,45.5,-45.5,90.,-90.,180.,-180.,359.,-359.,360.,-360.})
        valid(RV::revolve(profile,axis,angle),36*pi*std::abs(angle)/360);
    const auto positive=RV::revolve(profile,axis,90),negative=RV::revolve(profile,axis,-90);
    check(centre(positive.shape).Z()<0 && centre(negative.shape).Z()>0,"signed rotation direction lost");
    const auto full=RV::revolve(profile,axis,360);
    TopTools_IndexedMapOfShape fullFaces,partialFaces;
    TopExp::MapShapes(full.shape,TopAbs_FACE,fullFaces);TopExp::MapShapes(positive.shape,TopAbs_FACE,partialFaces);
    check(fullFaces.Extent()==4 && partialFaces.Extent()==6,"full revolution retained caps or partial lost caps");
    valid(RV::revolve(profile,gp_Ax1(gp_Pnt(0,20,0),gp_Dir(0,1,0)),360),36*pi);
    check(centre(RV::revolve(profile,axis.Reversed(),90).shape).Z()>0,"axis reversal ignored");
    valid(RV::revolve(faces(rectangle(-4)),axis,360),36*pi);
    valid(RV::revolve(faces(rectangle(0)),axis,360),12*pi); // Boundary edge on axis: cylinder.
    const auto triangle=faces({entity("slope",Segment{{0,0},{2,3}}),entity("top",Segment{{2,3},{0,3}}),entity("axis",Segment{{0,3},{0,0}})});
    valid(RV::revolve(triangle,axis,360),4*pi); // Cone tip on axis.
    const auto semicircle=faces({entity("arc",Arc{{0,0},2,-pi/2,pi}),entity("axis-edge",Segment{{0,2},{0,-2}})});
    valid(RV::revolve(semicircle,axis,360),32*pi/3); // Valid pole degeneracies: sphere.
    valid(RV::revolve(faces({entity("circle",Circle{{4,0},1})}),axis,360),8*pi*pi);
    const auto annulus=faces({entity("outer",Circle{{4,0},1}),entity("inner",Circle{{4,0},.5})});
    valid(RV::revolve(annulus,axis,360),6*pi*pi);
    valid(RV::revolve(annulus,axis,-90),1.5*pi*pi);
    auto hole=rectangle();hole.push_back(entity("hole",Circle{{3,1.5},.25}));
    valid(RV::revolve(faces(hole),axis,360),36*pi-.375*pi*pi);
    hole.push_back(entity("island",Circle{{3,1.5},.1}));
    valid(RV::revolve(faces(hole),axis,180),(36*pi-.315*pi*pi)/2,2);
    auto multiple=rectangle();const auto second=rectangle(5,4,1,1,"second-");multiple.insert(multiple.end(),second.begin(),second.end());
    valid(RV::revolve(faces(multiple),axis,360),47*pi,2);
    valid(RV::revolve(faces(multiple),axis,-90),47*pi/4,2);
    // Rotate/translate both world-space source and axis once. Compare the
    // centroid too, so an implementation ignoring placement cannot pass by volume.
    gp_Trsf placement;placement.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),.73);
    placement.SetTranslationPart(gp_Vec(35,-20,15));
    const auto moved=RV::revolve(faces(input,gp_Ax3().Transformed(placement)),axis.Transformed(placement),-90);
    valid(moved,9*pi);check(centre(moved.shape).Distance(centre(negative.shape).Transformed(placement))<1.e-7,"world axis/workplane placement ignored");
    const gp_Ax3 arbitrary(gp_Pnt(13,27,-9),gp_Dir(2,-1,3),gp_Dir(1,2,0));
    valid(RV::revolve(faces(input,arbitrary),gp_Ax1(arbitrary.Location(),arbitrary.YDirection()),123.25),36*pi*123.25/360);
    for(double angle:{0.,.999,-.999,360.001,-360.001,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        failed(RV::revolve(profile,axis,angle),RV::Code::InvalidInput);
    failed(RV::revolve({},axis,90),RV::Code::InvalidInput);
    auto open=input;open.pop_back();failed(RV::revolve(faces(open),axis,90),RV::Code::InvalidInput);
    failed(RV::revolve(faces({entity("ellipse",Ellipse{{3,0},1,.5,0})}),axis,90));
    failed(RV::revolve(profile,gp_Ax1(gp_Pnt(),gp_Dir(0,0,1)),90),RV::Code::UnsupportedAxis);
    failed(RV::revolve(profile,gp_Ax1(gp_Pnt(0,0,1),gp_Dir(0,1,0)),90),RV::Code::UnsupportedAxis);
    failed(RV::revolve(faces(rectangle(-1)),axis,90),RV::Code::SelfIntersection);
    failed(RV::revolve(faces({entity("crossing-circle",Circle{{.5,0},1})}),axis,360),RV::Code::SelfIntersection);
    failed(RV::revolve(faces({entity("pinch",Circle{{1,0},1})}),axis,360),RV::Code::SelfIntersection);
    valid(RV::revolve(faces({entity("clearance",Circle{{1.01,0},1})}),axis,360),2.02*pi*pi);
    auto opposite=rectangle();const auto left=rectangle(-4,5,2,1,"left-");opposite.insert(opposite.end(),left.begin(),left.end());
    failed(RV::revolve(faces(opposite),axis,90),RV::Code::UnsupportedAxis);
    auto invalid=profile;invalid.faces.front().shape.Nullify();failed(RV::revolve(invalid,axis,90),RV::Code::InvalidFace);
    invalid=profile;invalid.faces.front().shape=TopoDS::Face(TopExp_Explorer(BRepPrimAPI_MakeSphere(1).Shape(),TopAbs_FACE).Current());
    failed(RV::revolve(invalid,axis,90),RV::Code::InvalidFace);
    invalid=profile;invalid.effectiveVertexTolerance=std::numeric_limits<double>::quiet_NaN();failed(RV::revolve(invalid,axis,90),RV::Code::InvalidInput);
    invalid=profile;invalid.faces.push_back(profile.faces.front());failed(RV::revolve(invalid,axis,360),RV::Code::SelfIntersection);
    invalid=profile;invalid.faces.push_back(faces(input,gp_Ax3(gp_Pnt(0,0,2),gp_Dir(0,0,1))).faces.front());
    failed(RV::revolve(invalid,axis,90),RV::Code::UnsupportedAxis);
    failed(RV::revolve(profile,gp_Ax1(gp_Pnt(0,1.e12,0),gp_Dir(0,1,0)),90),RV::Code::NumericalFailure);
    check(input==saved && serialized(profile.faces.front().shape)==original,"successful or failed sweep modified source geometry");
    std::cout<<"Profile revolution geometry passed\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
