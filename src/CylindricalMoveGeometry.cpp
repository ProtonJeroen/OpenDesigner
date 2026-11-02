#include "CylindricalMoveGeometry.h"
#include "BooleanGeometry.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace {
using namespace CylindricalMoveGeometry;
constexpr double angular = 1.e-8;
constexpr double pi = std::numbers::pi;
Result refuse(Status status, Code code, const char* message) {
    Result result; result.status=status; result.code=code; result.message=message; return result;
}
double coordinate(const gp_Pnt& p, const gp_Dir& d) { return gp_Vec(gp_Pnt(0,0,0),p).Dot(gp_Vec(d)); }
bool sameCylinder(const gp_Cylinder& a,const gp_Cylinder& b,double tolerance) {
    return a.Axis().IsCoaxial(b.Axis(),angular,tolerance) && std::abs(a.Radius()-b.Radius())<=tolerance;
}
// Count edge uses, including both uses of periodic seams. Also certify that
// faces around each vertex form one connected fan (reject pinched vertices).
bool manifold(const TopoDS_Shape& shape) {
    TopTools_IndexedMapOfShape edges,faces,vertices;
    TopExp::MapShapes(shape,TopAbs_EDGE,edges); TopExp::MapShapes(shape,TopAbs_FACE,faces);
    TopExp::MapShapes(shape,TopAbs_VERTEX,vertices);
    std::vector<std::vector<int>> uses(edges.Extent()+1);
    for(int f=1;f<=faces.Extent();++f)
        for(TopExp_Explorer e(faces(f),TopAbs_EDGE);e.More();e.Next())
            uses[edges.FindIndex(e.Current())].push_back(f);
    for(int e=1;e<=edges.Extent();++e)
        if(BRep_Tool::Degenerated(TopoDS::Edge(edges(e))) || uses[e].size()!=2) return false;
    for(int v=1;v<=vertices.Extent();++v) {
        std::vector<int> incident;
        std::vector<std::pair<int,int>> links;
        for(int e=1;e<=edges.Extent();++e) {
            bool touches=false;
            for(TopExp_Explorer it(edges(e),TopAbs_VERTEX);it.More();it.Next())
                touches=touches||it.Current().IsSame(vertices(v));
            if(touches) { links.emplace_back(uses[e][0],uses[e][1]);
                incident.push_back(uses[e][0]); incident.push_back(uses[e][1]); }
        }
        if(incident.empty()) return false;
        std::vector<bool> reached(faces.Extent()+1,false); reached[incident.front()]=true;
        bool changed=true;
        while(changed) { changed=false; for(const auto& [a,b]:links)
            if(reached[a]!=reached[b]) { reached[a]=reached[b]=true; changed=true; } }
        for(int f:incident) if(!reached[f]) return false;
    }
    return true;
}
TopAbs_State classify(const TopoDS_Shape& shape,const gp_Pnt& point,double tolerance) {
    BRepClass3d_SolidClassifier classifier(shape,point,tolerance); return classifier.State();
}
}

CylindricalMoveGeometry::Result CylindricalMoveGeometry::moveThroughHole(
        const TopoDS_Shape& source,const TopoDS_Face& selected,const gp_Vec& delta) {
    try {
        if(!std::isfinite(delta.X())||!std::isfinite(delta.Y())||!std::isfinite(delta.Z()))
            return refuse(Status::InvalidInput,Code::InvalidDisplacement,"Displacement must be finite.");
        TopTools_IndexedMapOfShape members;
        if(source.IsNull())
            return refuse(Status::InvalidInput,Code::InvalidSource,"Source solid is null.");
        TopExp::MapShapes(source,TopAbs_FACE,members);
        if(selected.IsNull() || !members.Contains(selected) || members.Extent()>64)
            return refuse(Status::Unsupported,Code::UnsupportedFeature,"Select a member mantle on a solid with at most 64 faces.");
        // All further analysis (including kernel certification) uses deep copies.
        BRepBuilderAPI_Copy copy(source,true,false);
        auto isolated=copy.Shape();
        auto target=TopoDS::Face(copy.ModifiedShape(selected));
        for(const auto type:{TopAbs_EDGE,TopAbs_VERTEX}) {
            TopTools_IndexedMapOfShape subshapes;TopExp::MapShapes(isolated,type,subshapes);
            if(subshapes.Extent()>256)
                return refuse(Status::Unsupported,Code::UnsupportedFeature,"Source exceeds the supported edge/vertex complexity budget.");
        }
        double mass=0;
        if(!BRepReconstruction::validSolid(isolated,mass)||!manifold(isolated))
            return refuse(Status::InvalidInput,Code::InvalidSource,"Source must be one closed manifold solid with one shell.");
        const BRepAdaptor_Surface surface(target);
        if(surface.GetType()!=GeomAbs_Cylinder)
            return refuse(Status::Unsupported,Code::UnsupportedFeature,"Selected face is not an analytic cylinder.");
        const auto cylinder=surface.Cylinder();
        const double radius=cylinder.Radius();
        // Fixed, conservative tolerances; never enlarge fuzzy Boolean tolerance.
        const double tolerance=10*Precision::Confusion();
        for(auto type:{TopAbs_VERTEX,TopAbs_EDGE,TopAbs_FACE})
            for(TopExp_Explorer it(isolated,type);it.More();it.Next()) {
                const double t=type==TopAbs_VERTEX?BRep_Tool::Tolerance(TopoDS::Vertex(it.Current())):
                    type==TopAbs_EDGE?BRep_Tool::Tolerance(TopoDS::Edge(it.Current())):
                    BRep_Tool::Tolerance(TopoDS::Face(it.Current()));
                if(!std::isfinite(t)||t>tolerance)
                    return refuse(Status::Unsupported,Code::UnsupportedFeature,"Source subshape tolerance exceeds the supported 1e-6 limit.");
            }
        if(std::abs(delta.Dot(gp_Vec(cylinder.Axis().Direction())))>tolerance)
            return refuse(Status::InvalidInput,Code::InvalidDisplacement,"Displacement must be perpendicular to the hole axis.");

        // Recover a rectangular stock from physical oriented plane supports.
        // Never use the source's axis-aligned bounding box.
        std::vector<TopoDS_Face> planes,mantles;
        for(TopExp_Explorer it(isolated,TopAbs_FACE);it.More();it.Next()) {
            auto face=TopoDS::Face(it.Current()); const BRepAdaptor_Surface s(face);
            if(s.GetType()==GeomAbs_Plane) planes.push_back(face);
            else if(s.GetType()==GeomAbs_Cylinder && sameCylinder(cylinder,s.Cylinder(),tolerance)) mantles.push_back(face);
            else return refuse(Status::Unsupported,Code::UnsupportedStock,"Only rectangular stock with one coaxial through-hole is supported.");
        }
        gp_Dir z=cylinder.Axis().Direction(),x;
        bool haveX=false;
        for(const auto& face:planes) {
            auto n=BRepAdaptor_Surface(face).Plane().Axis().Direction();
            if(std::abs(n.Dot(z))<angular) { x=n; haveX=true; break; }
        }
        if(!haveX) return refuse(Status::Unsupported,Code::UnsupportedStock,"No rectangular side support was found.");
        const gp_Dir y=gp_Dir(gp_Vec(z).Crossed(gp_Vec(x)));
        const std::array<gp_Dir,3> basis{x,y,z};
        std::array<double,3> lo,hi; lo.fill(-std::numeric_limits<double>::infinity());hi.fill(std::numeric_limits<double>::infinity());
        std::array<bool,6> seen{};
        for(const auto& face:planes) {
            const auto p=BRepAdaptor_Surface(face).Plane(); auto n=p.Axis().Direction();
            if(face.Orientation()==TopAbs_REVERSED) n.Reverse();
            int axis=-1;
            for(int i=0;i<3;++i) if(n.IsParallel(basis[i],angular)) axis=i;
            if(axis<0) return refuse(Status::Unsupported,Code::UnsupportedStock,"Stock planes do not form a rectangular block normal to the hole.");
            const bool upper=n.Dot(basis[axis])>0; const int slot=2*axis+int(upper);
            const double d=coordinate(p.Location(),basis[axis]); auto& bound=upper?hi[axis]:lo[axis];
            if(seen[slot]&&std::abs(bound-d)>tolerance)
                return refuse(Status::Unsupported,Code::UnsupportedStock,"Stock has additional or ambiguous plane supports.");
            seen[slot]=true; bound=d;
        }
        if(!std::all_of(seen.begin(),seen.end(),[](bool v){return v;}))
            return refuse(Status::Unsupported,Code::UnsupportedStock,"Six oriented stock supports are required.");
        double scale=radius;
        for(int i=0;i<3;++i) {
            if(hi[i]-lo[i]<=100*tolerance)
                return refuse(Status::Unsupported,Code::UnsupportedStock,"Stock thickness is below the supported tolerance.");
            scale=std::max(scale,hi[i]-lo[i]);
        }
        if(radius<=100*tolerance || scale>1.e7 || std::max(std::abs(lo[0]),std::abs(hi[0]))>1.e9 ||
           std::max(std::abs(lo[1]),std::abs(hi[1]))>1.e9 || std::max(std::abs(lo[2]),std::abs(hi[2]))>1.e9)
            return refuse(Status::Unsupported,Code::UnsupportedFeature,"Feature size or coordinates exceed supported numerical limits.");
        for(int i=0;i<2;++i) {
            const double c=coordinate(cylinder.Location(),basis[i]);
            if(c-radius-lo[i]<=100*tolerance || hi[i]-c-radius<=100*tolerance)
                return refuse(Status::Unsupported,Code::UnsupportedFeature,"Source hole must have a complete circular mantle clear of stock sides.");
        }
        const double height=hi[2]-lo[2];
        const auto base=cylinder.Location().Translated(gp_Vec(z)*(lo[2]-coordinate(cylinder.Location(),z)));

        // Certify mantle boundary topology: seams/internal split edges are not
        // rims. Every exterior arc borders exactly one axial planar stock face.
        TopTools_IndexedMapOfShape mantleMap; for(const auto& f:mantles) mantleMap.Add(f);
        TopTools_IndexedDataMapOfShapeListOfShape adjacency;
        TopExp::MapShapesAndAncestors(isolated,TopAbs_EDGE,TopAbs_FACE,adjacency);
        std::vector<bool> reached(mantleMap.Extent()+1,false);
        reached[mantleMap.FindIndex(target)]=true;
        bool changed=true;
        while(changed) {
            changed=false;
            for(int i=1;i<=adjacency.Extent();++i) {
                std::vector<int> neighbors;bool connected=false;
                for(TopTools_ListIteratorOfListOfShape it(adjacency.FindFromIndex(i));it.More();it.Next()) {
                    const int f=mantleMap.FindIndex(it.Value());
                    if(f) {neighbors.push_back(f);connected=connected||reached[f];}
                }
                if(connected) for(int f:neighbors) if(!reached[f]) {reached[f]=true;changed=true;}
            }
        }
        for(int f=1;f<=mantleMap.Extent();++f) if(!reached[f])
            return refuse(Status::Unsupported,Code::UnsupportedFeature,"Cylindrical patches do not form one connected mantle.");
        std::array<double,2> rimLength{};
        for(int i=1;i<=adjacency.Extent();++i) {
            TopTools_IndexedMapOfShape inside,outside;
            for(TopTools_ListIteratorOfListOfShape it(adjacency.FindFromIndex(i));it.More();it.Next())
                (mantleMap.Contains(it.Value())?inside:outside).Add(it.Value());
            if(inside.IsEmpty()||outside.IsEmpty()) continue;
            if(inside.Extent()!=1||outside.Extent()!=1)
                return refuse(Status::Unsupported,Code::UnsupportedFeature,"Ambiguous mantle rim adjacency.");
            const BRepAdaptor_Surface neighbor(TopoDS::Face(outside(1)));
            const BRepAdaptor_Curve edge(TopoDS::Edge(adjacency.FindKey(i)));
            if(neighbor.GetType()!=GeomAbs_Plane||!neighbor.Plane().Axis().Direction().IsParallel(z,angular)||edge.GetType()!=GeomAbs_Circle)
                return refuse(Status::Unsupported,Code::UnsupportedFeature,"Mantle must terminate in circular rims on axial stock planes.");
            const auto circle=edge.Circle(); const double d=coordinate(circle.Location(),z);
            const int end=std::abs(d-lo[2])<=tolerance?0:std::abs(d-hi[2])<=tolerance?1:-1;
            if(end<0||std::abs(circle.Radius()-radius)>tolerance||gp_Lin(cylinder.Axis()).Distance(circle.Location())>tolerance)
                return refuse(Status::Unsupported,Code::UnsupportedFeature,"Incomplete or inconsistent circular rim.");
            GProp_GProps length; BRepGProp::LinearProperties(adjacency.FindKey(i),length);rimLength[end]+=length.Mass();
        }
        for(double length:rimLength) if(std::abs(length-2*pi*radius)>tolerance*std::max(1.,radius))
            return refuse(Status::Unsupported,Code::UnsupportedFeature,"Both complete circular rims are required.");
        double area=0;
        for(const auto& f:mantles) {
            GProp_GProps p;BRepGProp::SurfaceProperties(f,p);area+=p.Mass();
            BRepAdaptor_Surface s(f);
            const double u=(s.FirstUParameter()+s.LastUParameter())/2;
            const double v=(s.FirstVParameter()+s.LastVParameter())/2;
            BRepLProp_SLProps props(s,u,v,1,tolerance);
            if(!props.IsNormalDefined())
                return refuse(Status::Unsupported,Code::AmbiguousMaterial,"Mantle normal is undefined.");
            auto normal=props.Normal();if(f.Orientation()==TopAbs_REVERSED) normal.Reverse();
            const auto point=s.Value(u,v);
            auto radial=gp_Vec(base,point);radial-=gp_Vec(z)*radial.Dot(gp_Vec(z));
            if(normal.Dot(gp_Dir(radial))>-.99)
                return refuse(Status::Unsupported,Code::UnsupportedFeature,"Mantle is not oriented toward a cylindrical void.");
        }
        if(std::abs(area-2*pi*radius*height)>tolerance*std::max(1.,radius*height))
            return refuse(Status::Unsupported,Code::UnsupportedFeature,"Mantle does not cover the complete cylindrical span.");
        const double probe=std::min({radius,height,scale})*1.e-4;
        if(probe<10*tolerance) return refuse(Status::Unsupported,Code::UnsupportedFeature,"Feature is too small for reliable material classification.");
        for(int a=0;a<12;++a) for(double h:{0.2,0.5,0.8}) {
            const gp_Vec radial=gp_Vec(cylinder.XAxis().Direction())*std::cos(2*pi*(a+.37)/12)+
                                gp_Vec(cylinder.YAxis().Direction())*std::sin(2*pi*(a+.37)/12);
            const auto center=base.Translated(gp_Vec(z)*(h*height));
            if(classify(isolated,center.Translated(radial*(radius-probe)),tolerance)!=TopAbs_OUT ||
               classify(isolated,center.Translated(radial*(radius+probe)),tolerance)!=TopAbs_IN)
                return refuse(Status::Unsupported,Code::AmbiguousMaterial,"Cylinder does not have unambiguous through-hole material sides.");
        }
        const auto corner=gp_Pnt(0,0,0).Translated(gp_Vec(x)*lo[0]+gp_Vec(y)*lo[1]+gp_Vec(z)*lo[2]);
        const auto stockOracle=BRepPrimAPI_MakeBox(gp_Ax2(corner,z,x),hi[0]-lo[0],hi[1]-lo[1],height).Shape();
        const auto originalTool=BRepPrimAPI_MakeCylinder(gp_Ax2(base,z),radius,height).Shape();
        const auto originalOracle=BooleanGeometry::combine<BRepAlgoAPI_Cut>(stockOracle,originalTool);
        if(!BRepReconstruction::equivalent(isolated,originalOracle))
            return refuse(Status::Unsupported,Code::AmbiguousMaterial,"Source differs from rectangular stock minus one complete through-hole.");
        QString error;
        TopoDS_Shape stock;
        if(!BRepReconstruction::normalize(BooleanGeometry::combine<BRepAlgoAPI_Fuse>(isolated,originalTool),stock,error)||
           !BRepReconstruction::equivalent(stock,stockOracle))
            return refuse(Status::Failure,Code::AmbiguousMaterial,"Closing the old hole did not recover the certified stock.");
        // Extend the tool axially only. The source cylinder radius/direction and
        // the stock's exterior surfaces stay fixed while their trims may change.
        const double margin=std::max(1.,height);
        const auto translatedBase=base.Translated(delta);
        const auto tool=BRepPrimAPI_MakeCylinder(gp_Ax2(translatedBase.Translated(gp_Vec(z)*-margin),z),radius,height+2*margin).Shape();
        const auto candidate=BooleanGeometry::combine<BRepAlgoAPI_Cut>(stock,tool);
        TopoDS_Shape result;
        if(!BRepReconstruction::normalize(candidate,result,error)||!manifold(result))
            return refuse(Status::Failure,Code::InvalidResult,"Translated hole does not leave one closed manifold connected solid.");
        // Validate final supports without requiring the cylinder to survive.
        auto translatedCylinder=cylinder;translatedCylinder.Translate(delta);
        bool hasMantle=false;
        for(TopExp_Explorer it(result,TopAbs_FACE);it.More();it.Next()) {
            const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);bool allowed=false;
            if(s.GetType()==GeomAbs_Cylinder) {
                hasMantle=true;allowed=sameCylinder(translatedCylinder,s.Cylinder(),tolerance);
            }
            if(s.GetType()==GeomAbs_Plane) for(const auto& p:planes)
                allowed=allowed||BRepReconstruction::sameAnalyticSupport(p,f);
            if(!allowed) return refuse(Status::Failure,Code::InvalidResult,"Result contains an unexpected exterior support.");
        }
        const auto finiteTool=BRepPrimAPI_MakeCylinder(gp_Ax2(translatedBase,z),radius,height).Shape();
        const auto expected=BooleanGeometry::combine<BRepAlgoAPI_Cut>(stockOracle,finiteTool);
        if(!BRepReconstruction::equivalent(result,expected))
            return refuse(Status::Failure,Code::InvalidResult,"Result material differs from the requested translated hole.");
        Result answer;answer.status=Status::Success;answer.shape=result;
        answer.axis=gp_Ax1(translatedBase,z);answer.radius=radius;answer.height=height;
        const bool exited=!hasMantle;
        if(exited&&!BRepReconstruction::equivalent(result,stockOracle))
            return refuse(Status::Failure,Code::InvalidResult,"A vanished hole did not leave the complete certified stock.");
        answer.code=delta.Magnitude()<=tolerance?Code::Unchanged:exited?Code::HoleExited:Code::Moved;
        answer.message=exited?"Hole moved outside the stock.":"Through-hole translated successfully.";
        return answer;
    } catch(const Standard_Failure& failure) {
        Result r=refuse(Status::Failure,Code::KernelFailure,"OCCT could not certify the through-hole move.");
        if(failure.GetMessageString()) r.message+=" "+std::string(failure.GetMessageString());return r;
    } catch(const std::exception& failure) {
        Result r=refuse(Status::Failure,Code::KernelFailure,"Through-hole reconstruction failed: ");r.message+=failure.what();return r;
    }
}
