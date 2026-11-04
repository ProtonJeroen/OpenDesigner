#include "CylindricalMoveGeometry.h"
#include "BooleanGeometry.h"
#include "MoveFaceGeometry.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Defeaturing.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
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
#include <gp_Circ.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
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
    return a.Axis().Direction().IsParallel(b.Axis().Direction(),angular)
        && gp_Lin(a.Axis()).Distance(b.Location())<=tolerance && std::abs(a.Radius()-b.Radius())<=tolerance;
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

namespace {
// The source isolation, mantle/rim recognition, stock certificate and final
// acceptance are shared; only the operation's explicit material expression differs.
Result moveFeature(const TopoDS_Shape& source,const TopoDS_Face& selected,
                   const gp_Vec& delta,std::optional<BossBoundaryMode> policy) {
    const bool boss=policy.has_value();
    try {
        if(boss&&*policy!=BossBoundaryMode::Trim&&*policy!=BossBoundaryMode::Extend)
            return refuse(Status::InvalidInput,Code::InvalidPolicy,"Choose Trim or Extend explicitly.");
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
            return refuse(Status::InvalidInput,Code::InvalidDisplacement,"Displacement must be perpendicular to the cylinder axis.");

        // Recover a rectangular stock from physical oriented plane supports.
        // Never use the source's axis-aligned bounding box.
        std::vector<TopoDS_Face> planes,mantles;
        for(TopExp_Explorer it(isolated,TopAbs_FACE);it.More();it.Next()) {
            auto face=TopoDS::Face(it.Current()); const BRepAdaptor_Surface s(face);
            if(s.GetType()==GeomAbs_Plane) planes.push_back(face);
            else if(s.GetType()==GeomAbs_Cylinder && sameCylinder(cylinder,s.Cylinder(),tolerance)) mantles.push_back(face);
            else if(s.GetType()==GeomAbs_Cylinder)
                return refuse(Status::Unsupported,Code::UnsupportedStock,
                    "Cylindrical surfaces outside the selected mantle require independent-feature certification.");
            else return refuse(Status::Unsupported,Code::UnsupportedStock,"Only rectangular stock with one complete cylindrical feature is supported.");
        }
        TopTools_IndexedMapOfShape mantleMap; for(const auto& f:mantles) mantleMap.Add(f);
        TopTools_IndexedDataMapOfShapeListOfShape adjacency;
        TopExp::MapShapesAndAncestors(isolated,TopAbs_EDGE,TopAbs_FACE,adjacency);
        TopoDS_Face cap;
        if(boss) {
            // A cap must be a disk adjacent to the mantle, not merely a plane
            // at an axial extremum. Its circular boundary is certified below.
            for(const auto& f:planes) {
                const BRepAdaptor_Surface s(f);
                if(!s.Plane().Axis().Direction().IsParallel(cylinder.Axis().Direction(),angular)) continue;
                TopTools_IndexedMapOfShape wires;TopExp::MapShapes(f,TopAbs_WIRE,wires);
                GProp_GProps p;BRepGProp::SurfaceProperties(f,p);
                if(wires.Extent()!=1 || std::abs(p.Mass()-pi*radius*radius)>tolerance*std::max(1.,radius*radius) ||
                   gp_Lin(cylinder.Axis()).Distance(p.CentreOfMass())>tolerance) continue;
                bool adjacent=false;
                for(TopExp_Explorer edge(f,TopAbs_EDGE);edge.More();edge.Next())
                    for(TopTools_ListIteratorOfListOfShape it(adjacency.FindFromKey(edge.Current()));it.More();it.Next())
                        adjacent=adjacent||mantleMap.Contains(it.Value());
                if(!adjacent) continue;
                if(!cap.IsNull()) return refuse(Status::Unsupported,Code::UnsupportedFeature,"Boss cap is ambiguous; a standalone cylinder is not an attached boss.");
                cap=f;
            }
            if(cap.IsNull()) return refuse(Status::Unsupported,Code::UnsupportedFeature,"Boss needs one complete planar disk cap adjacent to its mantle.");
            planes.erase(std::remove_if(planes.begin(),planes.end(),[&](const auto& f){return f.IsSame(cap);}),planes.end());
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
            if(axis<0) return refuse(Status::Unsupported,Code::UnsupportedStock,"Stock planes do not form a rectangular block normal to the cylinder.");
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
                return refuse(Status::Unsupported,Code::UnsupportedFeature,"Source feature must have a complete circular footprint clear of stock sides.");
        }
        const double stockHeight=hi[2]-lo[2];
        double featureLo=lo[2],featureHi=hi[2],support=0;
        if(boss) {
            const double end=coordinate(BRepAdaptor_Surface(cap).Plane().Location(),z);
            if(end>hi[2]+100*tolerance) {support=hi[2];featureLo=support;featureHi=end;}
            else if(end<lo[2]-100*tolerance) {support=lo[2];featureLo=end;featureHi=support;}
            else return refuse(Status::Unsupported,Code::UnsupportedFeature,"Boss cap must lie beyond its stock supporting plane.");
        }
        const double height=featureHi-featureLo;
        if(height>1.e7) return refuse(Status::Unsupported,Code::UnsupportedFeature,"Feature height exceeds supported numerical limits.");
        const auto base=cylinder.Location().Translated(gp_Vec(z)*(featureLo-coordinate(cylinder.Location(),z)));

        // Certify mantle boundary topology: seams/internal split edges are not
        // rims. Every exterior arc borders an axial planar support or cap face.
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
        TopTools_IndexedMapOfShape supportFaces;
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
            const int end=std::abs(d-featureLo)<=tolerance?0:std::abs(d-featureHi)<=tolerance?1:-1;
            if(end<0||std::abs(circle.Radius()-radius)>tolerance||gp_Lin(cylinder.Axis()).Distance(circle.Location())>tolerance)
                return refuse(Status::Unsupported,Code::UnsupportedFeature,"Incomplete or inconsistent circular rim.");
            if(boss) {
                const bool atSupport=std::abs(d-support)<=tolerance;
                if(atSupport) supportFaces.Add(outside(1));
                else if(!outside(1).IsSame(cap))
                    return refuse(Status::Unsupported,Code::UnsupportedFeature,"Boss top rim must border its unique disk cap.");
                if(atSupport&&outside(1).IsSame(cap))
                    return refuse(Status::Unsupported,Code::UnsupportedFeature,"Boss footprint and cap must be distinct.");
            }
            GProp_GProps length; BRepGProp::LinearProperties(adjacency.FindKey(i),length);rimLength[end]+=length.Mass();
        }
        for(double length:rimLength) if(std::abs(length-2*pi*radius)>tolerance*std::max(1.,radius))
            return refuse(Status::Unsupported,Code::UnsupportedFeature,"Both complete circular rims are required.");
        if(boss&&supportFaces.Extent()!=1)
            return refuse(Status::Unsupported,Code::UnsupportedFeature,"Boss footprint must border one unambiguous planar support face.");
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
            if((boss?1.:-1.)*normal.Dot(gp_Dir(radial))<.99)
                return refuse(Status::Unsupported,Code::UnsupportedFeature,"Mantle orientation does not match the requested feature kind.");
        }
        if(std::abs(area-2*pi*radius*height)>tolerance*std::max(1.,radius*height))
            return refuse(Status::Unsupported,Code::UnsupportedFeature,"Mantle does not cover the complete cylindrical span.");
        const double probe=std::min({radius,height,scale})*1.e-4;
        if(probe<10*tolerance) return refuse(Status::Unsupported,Code::UnsupportedFeature,"Feature is too small for reliable material classification.");
        for(int a=0;a<12;++a) for(double h:{0.2,0.5,0.8}) {
            const gp_Vec radial=gp_Vec(cylinder.XAxis().Direction())*std::cos(2*pi*(a+.37)/12)+
                                gp_Vec(cylinder.YAxis().Direction())*std::sin(2*pi*(a+.37)/12);
            const auto center=base.Translated(gp_Vec(z)*(h*height));
            if(classify(isolated,center.Translated(radial*(radius-probe)),tolerance)!=(boss?TopAbs_IN:TopAbs_OUT) ||
               classify(isolated,center.Translated(radial*(radius+probe)),tolerance)!=(boss?TopAbs_OUT:TopAbs_IN))
                return refuse(Status::Unsupported,Code::AmbiguousMaterial,"Cylinder does not have unambiguous feature material sides.");
        }
        const auto corner=gp_Pnt(0,0,0).Translated(gp_Vec(x)*lo[0]+gp_Vec(y)*lo[1]+gp_Vec(z)*lo[2]);
        const auto stockOracle=BRepPrimAPI_MakeBox(gp_Ax2(corner,z,x),hi[0]-lo[0],hi[1]-lo[1],stockHeight).Shape();
        const auto originalTool=BRepPrimAPI_MakeCylinder(gp_Ax2(base,z),radius,height).Shape();
        const auto originalOracle=boss?BooleanGeometry::combine<BRepAlgoAPI_Fuse>(stockOracle,originalTool):
                                       BooleanGeometry::combine<BRepAlgoAPI_Cut>(stockOracle,originalTool);
        if(!BRepReconstruction::equivalent(isolated,originalOracle))
            return refuse(Status::Unsupported,Code::AmbiguousMaterial,"Source differs from rectangular stock with exactly one complete requested feature.");
        QString error;
        TopoDS_Shape stock;
        const auto restored=boss?BooleanGeometry::combine<BRepAlgoAPI_Cut>(isolated,originalTool):
                                 BooleanGeometry::combine<BRepAlgoAPI_Fuse>(isolated,originalTool);
        if(!BRepReconstruction::normalize(restored,stock,error)||
           !BRepReconstruction::equivalent(stock,stockOracle))
            return refuse(Status::Failure,Code::AmbiguousMaterial,"Removing the old feature did not recover the certified stock.");
        // Only the through-hole cutting tool extends axially. Cylinder radius/
        // direction and stock supports stay fixed while their trims may change.
        const double margin=std::max(1.,height);
        const auto translatedBase=base.Translated(delta);
        const auto finiteTool=BRepPrimAPI_MakeCylinder(gp_Ax2(translatedBase,z),radius,height).Shape();
        TopoDS_Shape candidate,expected;
        if(boss) {
            // A prism bounded by the certified lateral stock planes, with its
            // axial ends exactly on the boss rims. These are physical support
            // extensions, never an axis-aligned bounding-box approximation.
            const auto trimCorner=corner.Translated(gp_Vec(z)*(featureLo-lo[2]));
            const auto lateralRegion=BRepPrimAPI_MakeBox(gp_Ax2(trimCorner,z,x),hi[0]-lo[0],hi[1]-lo[1],height).Shape();
            const double cx=coordinate(translatedBase,x),cy=coordinate(translatedBase,y);
            const double dx=std::max({lo[0]-cx,0.,cx-hi[0]}),dy=std::max({lo[1]-cy,0.,cy-hi[1]});
            if(radius-std::hypot(dx,dy)<=100*tolerance)
                return refuse(Status::Failure,Code::DisconnectedBoss,"Boss footprint has no reliable positive-area connection to the original support.");
            const auto clipped=BooleanGeometry::combine<BRepAlgoAPI_Common>(finiteTool,lateralRegion);
            double clippedVolume=0;
            if(!BRepReconstruction::validSolid(clipped,clippedVolume)||clippedVolume/height<=10000*tolerance*tolerance)
                return refuse(Status::Failure,Code::DisconnectedBoss,"Boss support overlap is empty, degenerate or ambiguous.");
            const auto material=*policy==BossBoundaryMode::Trim?clipped:finiteTool;
            candidate=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(stock,material);
            expected=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(stockOracle,material);
        } else {
            const auto tool=BRepPrimAPI_MakeCylinder(gp_Ax2(translatedBase.Translated(gp_Vec(z)*-margin),z),radius,height+2*margin).Shape();
            candidate=BooleanGeometry::combine<BRepAlgoAPI_Cut>(stock,tool);
            expected=BooleanGeometry::combine<BRepAlgoAPI_Cut>(stockOracle,finiteTool);
        }
        TopoDS_Shape result;
        if(!BRepReconstruction::normalize(candidate,result,error)||!manifold(result))
            return refuse(Status::Failure,Code::InvalidResult,"Translated feature does not leave one closed manifold connected solid.");
        // Validate final supports without requiring the cylinder to survive.
        auto translatedCylinder=cylinder;translatedCylinder.Translate(delta);
        bool hasMantle=false;
        for(TopExp_Explorer it(result,TopAbs_FACE);it.More();it.Next()) {
            const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);bool allowed=false;
            if(s.GetType()==GeomAbs_Cylinder) {
                hasMantle=true;allowed=sameCylinder(translatedCylinder,s.Cylinder(),tolerance);
            }
            if(s.GetType()==GeomAbs_Plane) for(const auto& p:planes)
                allowed=allowed||BRepReconstruction::sameAnalyticSupport(p,f)||
                    (boss&&BRepReconstruction::sameAnalyticSupport(TopoDS::Face(p.Reversed()),f));
            if(boss&&s.GetType()==GeomAbs_Plane)
                allowed=allowed||BRepReconstruction::sameAnalyticSupport(cap,f);
            if(!allowed) return refuse(Status::Failure,Code::InvalidResult,"Result contains an unexpected exterior support.");
        }
        if(!BRepReconstruction::equivalent(result,expected))
            return refuse(Status::Failure,Code::InvalidResult,"Result material differs from the requested translated feature policy.");
        if(boss&&(!hasMantle||!BRepReconstruction::equivalent(BooleanGeometry::combine<BRepAlgoAPI_Common>(result,stockOracle),stockOracle)))
            return refuse(Status::Failure,Code::InvalidResult,"Boss reconstruction lost its curved wall or changed the supporting stock.");
        if(boss&&*policy==BossBoundaryMode::Extend&&
           !BRepReconstruction::equivalent(BooleanGeometry::combine<BRepAlgoAPI_Common>(result,finiteTool),finiteTool))
            return refuse(Status::Failure,Code::InvalidResult,"Extend did not preserve the complete translated boss.");
        Result answer;answer.status=Status::Success;answer.shape=result;
        answer.axis=gp_Ax1(translatedBase,z);answer.radius=radius;answer.height=height;
        const bool exited=!boss&&!hasMantle;
        if(exited&&!BRepReconstruction::equivalent(result,stockOracle))
            return refuse(Status::Failure,Code::InvalidResult,"A vanished hole did not leave the complete certified stock.");
        answer.code=delta.Magnitude()<=tolerance?Code::Unchanged:exited?Code::HoleExited:Code::Moved;
        answer.message=boss?(*policy==BossBoundaryMode::Trim?"Boss translated with Trim.":"Boss translated with Extend."):
                           exited?"Hole moved outside the stock.":"Through-hole translated successfully.";
        return answer;
    } catch(const Standard_Failure& failure) {
        Result r=refuse(Status::Failure,Code::KernelFailure,"OCCT could not certify the cylindrical feature move.");
        if(failure.GetMessageString()) r.message+=" "+std::string(failure.GetMessageString());return r;
    } catch(const std::exception& failure) {
        Result r=refuse(Status::Failure,Code::KernelFailure,"Cylindrical reconstruction failed: ");r.message+=failure.what();return r;
    }
}
}

namespace {
struct IndependentFeature {
    gp_Cylinder cylinder;
    std::vector<TopoDS_Face> mantles;
    TopoDS_Face cap;
    TopoDS_Shape volume;
    bool boss=false;
};
double faceArea(const TopoDS_Shape& shape) {
    GProp_GProps p;BRepGProp::SurfaceProperties(shape,p);return p.Mass();
}
// Compare trimmed surface regions, not face identities/counts. Normalization
// may split/merge faces; every original unselected mantle/cap region must remain.
bool retainedFeature(const IndependentFeature& feature,const TopoDS_Shape& result) {
    auto originals=feature.mantles;if(feature.boss) originals.push_back(feature.cap);
    for(const auto& original:originals) {
        double covered=0;
        for(TopExp_Explorer it(result,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());
            if(BRepReconstruction::sameAnalyticSupport(original,face))
                covered+=faceArea(BooleanGeometry::combine<BRepAlgoAPI_Common>(original,face));
        }
        if(std::abs(covered-faceArea(original))>1.e-8*std::max(1.,faceArea(original))) return false;
    }
    double before=0,after=0;
    for(const auto& f:feature.mantles) before+=faceArea(f);
    for(TopExp_Explorer it(result,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());
        if(BRepReconstruction::sameAnalyticSupport(feature.mantles.front(),face)) after+=faceArea(face);
    }
    return std::abs(before-after)<=1.e-8*std::max(1.,before);
}

Result moveSelected(const TopoDS_Shape& source,const TopoDS_Face& selected,
                    const gp_Vec& delta,std::optional<BossBoundaryMode> policy) {
    // Preserve the existing fast path and its input, tolerance and solid checks.
    const auto direct=moveFeature(source,selected,delta,policy);
    if(direct||direct.code!=Code::UnsupportedStock) return direct;
    try {
        const double tolerance=10*Precision::Confusion();
        BRepBuilderAPI_Copy copy(source,true,false);const auto isolated=copy.Shape();
        const auto target=TopoDS::Face(copy.ModifiedShape(selected));
        TopTools_IndexedMapOfShape cylinders;
        for(TopExp_Explorer it(isolated,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());const auto type=BRepAdaptor_Surface(face).GetType();
            if(type==GeomAbs_Cylinder) cylinders.Add(face);
            else if(type!=GeomAbs_Plane) return direct;
        }
        if(!cylinders.Contains(target)) return direct;
        TopTools_IndexedDataMapOfShapeListOfShape adjacency;
        TopExp::MapShapesAndAncestors(isolated,TopAbs_EDGE,TopAbs_FACE,adjacency);
        std::vector<IndependentFeature> features;
        std::vector<bool> visited(cylinders.Extent()+1,false);
        size_t chosen=0;
        for(int seed=1;seed<=cylinders.Extent();++seed) {
            if(visited[seed]) continue;
            IndependentFeature feature;feature.cylinder=BRepAdaptor_Surface(TopoDS::Face(cylinders(seed))).Cylinder();
            std::vector<int> pending{seed};visited[seed]=true;
            TopTools_IndexedMapOfShape members;
            for(size_t next=0;next<pending.size();++next) {
                const auto face=TopoDS::Face(cylinders(pending[next]));members.Add(face);feature.mantles.push_back(face);
                for(TopExp_Explorer edge(face,TopAbs_EDGE);edge.More();edge.Next())
                    for(TopTools_ListIteratorOfListOfShape it(adjacency.FindFromKey(edge.Current()));it.More();it.Next()) {
                        const int index=cylinders.FindIndex(it.Value());
                        if(index&&!visited[index]&&sameCylinder(feature.cylinder,BRepAdaptor_Surface(TopoDS::Face(it.Value())).Cylinder(),tolerance)) {
                            visited[index]=true;pending.push_back(index);
                        }
                    }
            }
            if(members.Contains(target)) chosen=features.size();
            if(!feature.cylinder.Axis().Direction().IsParallel(BRepAdaptor_Surface(target).Cylinder().Axis().Direction(),angular))
                return refuse(Status::Unsupported,Code::UnsupportedStock,"Independent features currently require parallel cylinder axes normal to rectangular stock.");
            const auto first=feature.mantles.front();BRepAdaptor_Surface surface(first);
            const double u=(surface.FirstUParameter()+surface.LastUParameter())/2,v=(surface.FirstVParameter()+surface.LastVParameter())/2;
            BRepLProp_SLProps props(surface,u,v,1,tolerance);
            if(!props.IsNormalDefined()) return direct;
            auto normal=props.Normal();if(first.Orientation()==TopAbs_REVERSED) normal.Reverse();
            const auto z=feature.cylinder.Axis().Direction();
            auto radial=gp_Vec(feature.cylinder.Location(),surface.Value(u,v));radial-=gp_Vec(z)*radial.Dot(gp_Vec(z));
            feature.boss=normal.Dot(gp_Dir(radial))>0;
            std::vector<double> ends;std::array<double,2> lengths{};
            for(int edge=1;edge<=adjacency.Extent();++edge) {
                TopTools_IndexedMapOfShape inside,outside;
                for(TopTools_ListIteratorOfListOfShape it(adjacency.FindFromIndex(edge));it.More();it.Next())
                    (members.Contains(it.Value())?inside:outside).Add(it.Value());
                if(inside.IsEmpty()||outside.IsEmpty()) continue;
                if(inside.Extent()!=1||outside.Extent()!=1)
                    return refuse(Status::Unsupported,Code::UnsupportedFeature,"Feature rim adjacency is ambiguous or interacts with another feature.");
                const BRepAdaptor_Surface neighbor(TopoDS::Face(outside(1)));
                const BRepAdaptor_Curve curve(TopoDS::Edge(adjacency.FindKey(edge)));
                if(neighbor.GetType()!=GeomAbs_Plane||!neighbor.Plane().Axis().Direction().IsParallel(z,angular)||curve.GetType()!=GeomAbs_Circle)
                    return refuse(Status::Unsupported,Code::UnsupportedFeature,"Independent features need complete circular rims on perpendicular planar neighbors.");
                const auto circle=curve.Circle();
                if(std::abs(circle.Radius()-feature.cylinder.Radius())>tolerance||gp_Lin(feature.cylinder.Axis()).Distance(circle.Location())>tolerance)
                    return refuse(Status::Unsupported,Code::UnsupportedFeature,"Feature rim is not concentric with its mantle.");
                const double end=coordinate(circle.Location(),z);
                size_t index=0;while(index<ends.size()&&std::abs(ends[index]-end)>tolerance) ++index;
                if(index==ends.size()) ends.push_back(end);
                if(index>=2) return refuse(Status::Unsupported,Code::UnsupportedFeature,"Feature must have exactly two axial rims.");
                GProp_GProps p;BRepGProp::LinearProperties(adjacency.FindKey(edge),p);lengths[index]+=p.Mass();
                if(feature.boss&&std::abs(faceArea(outside(1))-pi*circle.Radius()*circle.Radius())<=tolerance*std::max(1.,circle.Radius()*circle.Radius())) {
                    if(!feature.cap.IsNull()&&!feature.cap.IsSame(outside(1))) return direct;
                    feature.cap=TopoDS::Face(outside(1));
                }
            }
            const double radius=feature.cylinder.Radius();
            if(ends.size()!=2||std::abs(lengths[0]-2*pi*radius)>tolerance*std::max(1.,radius)||
               std::abs(lengths[1]-2*pi*radius)>tolerance*std::max(1.,radius))
                return refuse(Status::Unsupported,Code::UnsupportedFeature,"Independent feature rims must each form a complete circle.");
            const double height=std::abs(ends[1]-ends[0]);
            double area=0;for(const auto& face:feature.mantles) area+=faceArea(face);
            if(height<=100*tolerance||std::abs(area-2*pi*radius*height)>tolerance*std::max(1.,radius*height)||
               (feature.boss&&feature.cap.IsNull()))
                return refuse(Status::Unsupported,Code::UnsupportedFeature,"Independent feature mantle or boss cap is incomplete.");
            const auto base=feature.cylinder.Location().Translated(gp_Vec(z)*(std::min(ends[0],ends[1])-coordinate(feature.cylinder.Location(),z)));
            const double probe=std::min(radius,height)*1.e-4;
            if(probe<10*tolerance)
                return refuse(Status::Unsupported,Code::AmbiguousMaterial,"Feature is too small for reliable material-side classification.");
            for(int sample=0;sample<12;++sample) for(double fraction:{.2,.5,.8}) {
                const double angle=2*pi*(sample+.37)/12;
                const auto radial=gp_Vec(feature.cylinder.XAxis().Direction())*std::cos(angle)+
                    gp_Vec(feature.cylinder.YAxis().Direction())*std::sin(angle);
                const auto center=base.Translated(gp_Vec(z)*(fraction*height));
                if(classify(isolated,center.Translated(radial*(radius-probe)),tolerance)!=(feature.boss?TopAbs_IN:TopAbs_OUT)||
                   classify(isolated,center.Translated(radial*(radius+probe)),tolerance)!=(feature.boss?TopAbs_OUT:TopAbs_IN))
                    return refuse(Status::Unsupported,Code::AmbiguousMaterial,"Original feature material sides are ambiguous or interact with other geometry.");
            }
            feature.volume=BRepPrimAPI_MakeCylinder(gp_Ax2(base,z),radius,height).Shape();
            features.push_back(feature);
            if(features.size()>8) return refuse(Status::Unsupported,Code::UnsupportedStock,"At most eight independent cylindrical features are supported.");
        }
        if(features.size()<2) return direct;
        if(features[chosen].boss!=policy.has_value())
            return refuse(Status::Unsupported,Code::UnsupportedFeature,"Selected mantle does not match the requested hole or boss operation.");
        // Clearance is measured on footprints, including hole/boss pairs that
        // would meet only on the support plane. Tangency is not independence.
        for(size_t i=0;i<features.size();++i) for(size_t j=0;j<i;++j)
            if(gp_Lin(features[i].cylinder.Axis()).Distance(features[j].cylinder.Location())<=
               features[i].cylinder.Radius()+features[j].cylinder.Radius()+100*tolerance)
                return refuse(Status::Unsupported,Code::InteractingFeatures,"Source feature footprints overlap, touch or cannot be certified as independent.");
        auto movedCylinder=features[chosen].cylinder;movedCylinder.Translate(delta);
        for(size_t i=0;i<features.size();++i) if(i!=chosen&&gp_Lin(movedCylinder.Axis()).Distance(features[i].cylinder.Location())<=
            movedCylinder.Radius()+features[i].cylinder.Radius()+100*tolerance)
            return refuse(Status::Failure,Code::InteractingFeatures,"Translated footprint overlaps or touches an unselected feature; original Part was kept.");
        QString error;TopoDS_Shape stock=isolated;
        for(const auto& feature:features) stock=feature.boss?BooleanGeometry::combine<BRepAlgoAPI_Cut>(stock,feature.volume):
            BooleanGeometry::combine<BRepAlgoAPI_Fuse>(stock,feature.volume);
        TopoDS_Shape normalized;
        if(!BRepReconstruction::normalize(stock,normalized,error))
            return refuse(Status::Unsupported,Code::AmbiguousMaterial,"Independent feature removal did not recover one valid stock solid.");
        stock=normalized;
        TopoDS_Shape sourceOracle=stock,selectedOnly;TopoDS_Face selectedFace;
        for(size_t i=0;i<features.size();++i) {
            const auto& feature=features[i];
            const auto single=feature.boss?BooleanGeometry::combine<BRepAlgoAPI_Fuse>(stock,feature.volume):
                BooleanGeometry::combine<BRepAlgoAPI_Cut>(stock,feature.volume);
            TopoDS_Face face;
            for(TopExp_Explorer it(single,TopAbs_FACE);it.More();it.Next())
                if(BRepReconstruction::sameAnalyticSupport(feature.mantles.front(),TopoDS::Face(it.Current()))) {face=TopoDS::Face(it.Current());break;}
            // Reuse all existing rectangular stock, cap/support, material-side,
            // mantle/rim and exact source material certificates for EACH feature.
            const auto certified=moveFeature(single,face,gp_Vec(0,0,0),feature.boss?
                std::optional<BossBoundaryMode>(BossBoundaryMode::Extend):std::nullopt);
            if(!certified||!retainedFeature(feature,certified.shape))
                return refuse(Status::Unsupported,Code::AmbiguousMaterial,"An independent feature does not satisfy the complete-feature/rectangular-stock certificates.");
            if(i==chosen) {selectedOnly=single;selectedFace=face;}
            sourceOracle=feature.boss?BooleanGeometry::combine<BRepAlgoAPI_Fuse>(sourceOracle,feature.volume):
                BooleanGeometry::combine<BRepAlgoAPI_Cut>(sourceOracle,feature.volume);
        }
        if(!BRepReconstruction::equivalent(sourceOracle,isolated))
            return refuse(Status::Unsupported,Code::AmbiguousMaterial,"Stock and certified independent features do not reproduce the source material.");
        auto answer=moveFeature(selectedOnly,selectedFace,delta,policy);
        if(!answer) return answer;
        // Edit the original locally. Recomposition is an independent oracle,
        // not a replacement of unrelated features with newly generated geometry.
        TopoDS_Shape local=policy?BooleanGeometry::combine<BRepAlgoAPI_Cut>(isolated,features[chosen].volume):
            BooleanGeometry::combine<BRepAlgoAPI_Fuse>(isolated,features[chosen].volume);
        if(policy) {
            const auto added=BooleanGeometry::combine<BRepAlgoAPI_Cut>(answer.shape,stock);
            local=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(local,added);
        } else {
            const auto& axis=answer.axis;const double margin=std::max(1.,answer.height);
            const auto tool=BRepPrimAPI_MakeCylinder(gp_Ax2(axis.Location().Translated(gp_Vec(axis.Direction())*-margin),axis.Direction()),answer.radius,answer.height+2*margin).Shape();
            local=BooleanGeometry::combine<BRepAlgoAPI_Cut>(local,tool);
        }
        TopoDS_Shape expected=answer.shape;
        for(size_t i=0;i<features.size();++i) if(i!=chosen)
            expected=features[i].boss?BooleanGeometry::combine<BRepAlgoAPI_Fuse>(expected,features[i].volume):
                BooleanGeometry::combine<BRepAlgoAPI_Cut>(expected,features[i].volume);
        if(!BRepReconstruction::normalize(local,normalized,error)||!manifold(normalized)||!BRepReconstruction::equivalent(normalized,expected))
            return refuse(Status::Failure,Code::InvalidResult,"Local reconstruction failed the independent multi-feature material/solid certificate.");
        for(size_t i=0;i<features.size();++i) if(i!=chosen&&!retainedFeature(features[i],normalized))
            return refuse(Status::Failure,Code::InvalidResult,"An unselected feature's trimmed surface geometry changed.");
        answer.shape=normalized;return answer;
    } catch(const Standard_Failure&) {
        return refuse(Status::Failure,Code::KernelFailure,"OCCT could not certify independent-feature reconstruction; original Part was kept.");
    } catch(const std::exception& failure) {
        Result r=refuse(Status::Failure,Code::KernelFailure,"Independent-feature reconstruction failed: ");r.message+=failure.what();return r;
    }
}

// A boundary-crossing boss need not be recognizable as a NEW selected boss to
// remain protected while editing a complete through-hole or unaffected stock
// side plane below it.
// Isolate only at the selected hole's physical rim planes; the existing engine
// must still certify all material and stock inside that slab.
Result moveWithExterior(const TopoDS_Shape& source,const TopoDS_Face& selected,
                        const gp_Vec& delta,const Result& refused,
                        const TopoDS_Face& plane={},double distance=0) {
    if(refused.status!=Status::Unsupported||refused.code==Code::InteractingFeatures) return refused;
    try {
        const double tolerance=10*Precision::Confusion();
        TopTools_IndexedMapOfShape sourceFaces;TopExp::MapShapes(source,TopAbs_FACE,sourceFaces);
        if(selected.IsNull()||!sourceFaces.Contains(selected)||sourceFaces.Extent()>64||BRepAdaptor_Surface(selected).GetType()!=GeomAbs_Cylinder) return refused;
        if(!plane.IsNull()&&(!sourceFaces.Contains(plane)||BRepAdaptor_Surface(plane).GetType()!=GeomAbs_Plane||!std::isfinite(distance))) return refused;
        BRepBuilderAPI_Copy copy(source,true,false);const auto isolated=copy.Shape();
        double sourceVolume=0;if(!BRepReconstruction::validSolid(isolated,sourceVolume)||!manifold(isolated)) return refused;
        for(const auto type:{TopAbs_VERTEX,TopAbs_EDGE,TopAbs_FACE}) {
            TopTools_IndexedMapOfShape shapes;TopExp::MapShapes(isolated,type,shapes);
            if(type!=TopAbs_FACE&&shapes.Extent()>256) return refused;
            for(int i=1;i<=shapes.Extent();++i) {
                const double t=type==TopAbs_VERTEX?BRep_Tool::Tolerance(TopoDS::Vertex(shapes(i))):
                    type==TopAbs_EDGE?BRep_Tool::Tolerance(TopoDS::Edge(shapes(i))):BRep_Tool::Tolerance(TopoDS::Face(shapes(i)));
                if(!std::isfinite(t)||t>tolerance) return refused;
            }
        }
        auto target=TopoDS::Face(copy.ModifiedShape(selected));target.Orientation(selected.Orientation());
        const auto cylinder=BRepAdaptor_Surface(target).Cylinder();
        const auto axis=cylinder.Axis().Direction();const double radius=cylinder.Radius();
        TopTools_IndexedDataMapOfShapeListOfShape adjacency;
        TopExp::MapShapesAndAncestors(isolated,TopAbs_EDGE,TopAbs_FACE,adjacency);
        TopTools_IndexedMapOfShape mantle;mantle.Add(target);
        for(int i=1;i<=mantle.Extent();++i) {
            const auto face=TopoDS::Face(mantle(i));BRepAdaptor_Surface s(face);
            BRepLProp_SLProps p(s,(s.FirstUParameter()+s.LastUParameter())/2,(s.FirstVParameter()+s.LastVParameter())/2,1,tolerance);
            if(!p.IsNormalDefined()) return refused;
            auto normal=p.Normal();if(face.Orientation()==TopAbs_REVERSED) normal.Reverse();
            auto radial=gp_Vec(cylinder.Location(),s.Value((s.FirstUParameter()+s.LastUParameter())/2,(s.FirstVParameter()+s.LastVParameter())/2));
            radial-=gp_Vec(axis)*radial.Dot(gp_Vec(axis));
            if(normal.Dot(gp_Dir(radial))>-.99) return refused;
            for(TopExp_Explorer edge(face,TopAbs_EDGE);edge.More();edge.Next())
                for(TopTools_ListIteratorOfListOfShape it(adjacency.FindFromKey(edge.Current()));it.More();it.Next()) {
                    const auto next=TopoDS::Face(it.Value());const BRepAdaptor_Surface n(next);
                    if(n.GetType()==GeomAbs_Cylinder&&sameCylinder(cylinder,n.Cylinder(),tolerance)) mantle.Add(next);
                }
        }
        std::vector<double> ends;std::array<double,2> lengths{};
        for(int i=1;i<=adjacency.Extent();++i) {
            TopTools_IndexedMapOfShape inside,outside;
            for(TopTools_ListIteratorOfListOfShape it(adjacency.FindFromIndex(i));it.More();it.Next())
                (mantle.Contains(it.Value())?inside:outside).Add(it.Value());
            if(inside.IsEmpty()||outside.IsEmpty()) continue;
            if(inside.Extent()!=1||outside.Extent()!=1) return refused;
            const BRepAdaptor_Surface neighbor(TopoDS::Face(outside(1)));
            const BRepAdaptor_Curve curve(TopoDS::Edge(adjacency.FindKey(i)));
            if(neighbor.GetType()!=GeomAbs_Plane||!neighbor.Plane().Axis().Direction().IsParallel(axis,angular)||curve.GetType()!=GeomAbs_Circle) return refused;
            const auto circle=curve.Circle();
            if(std::abs(circle.Radius()-radius)>tolerance||gp_Lin(cylinder.Axis()).Distance(circle.Location())>tolerance) return refused;
            const double end=coordinate(circle.Location(),axis);
            size_t n=0;while(n<ends.size()&&std::abs(ends[n]-end)>tolerance) ++n;
            if(n==ends.size()) ends.push_back(end);
            if(n>=2) return refused;
            GProp_GProps p;BRepGProp::LinearProperties(adjacency.FindKey(i),p);lengths[n]+=p.Mass();
        }
        if(ends.size()!=2) return refused;
        const double low=std::min(ends[0],ends[1]),high=std::max(ends[0],ends[1]),height=high-low;
        double area=0;for(int i=1;i<=mantle.Extent();++i) area+=faceArea(mantle(i));
        if(height<=100*tolerance||std::abs(area-2*pi*radius*height)>tolerance*std::max(1.,radius*height)) return refused;
        for(double length:lengths) if(std::abs(length-2*pi*radius)>tolerance*std::max(1.,radius)) return refused;
        for(TopExp_Explorer it(isolated,TopAbs_FACE);it.More();it.Next()) {
            const auto type=BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType();
            if(type!=GeomAbs_Plane&&type!=GeomAbs_Cylinder) return refused;
        }
        const auto base=cylinder.Location().Translated(gp_Vec(axis)*(low-coordinate(cylinder.Location(),axis)));
        const auto top=base.Translated(gp_Vec(axis)*height);
        const auto lower=BRepPrimAPI_MakeHalfSpace(BRepBuilderAPI_MakeFace(gp_Pln(base,axis)).Face(),base.Translated(gp_Vec(axis)*-height)).Solid();
        const auto upper=BRepPrimAPI_MakeHalfSpace(BRepBuilderAPI_MakeFace(gp_Pln(top,axis)).Face(),top.Translated(gp_Vec(axis)*height)).Solid();
        const auto slab=BooleanGeometry::combine<BRepAlgoAPI_Cut>(BooleanGeometry::combine<BRepAlgoAPI_Cut>(isolated,lower),upper);
        const auto exterior=BooleanGeometry::combine<BRepAlgoAPI_Cut>(isolated,slab);
        double volume=0;size_t count=0;bool exteriorCylinder=false;
        for(TopExp_Explorer it(exterior,TopAbs_FACE);it.More();it.Next())
            exteriorCylinder=exteriorCylinder||BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType()==GeomAbs_Cylinder;
        if(!exteriorCylinder||!BRepReconstruction::validSolid(slab,volume)||!BooleanGeometry::certifiedSolid(exterior,volume,count)||
           !BRepReconstruction::equivalent(BooleanGeometry::combine<BRepAlgoAPI_Fuse>(slab,exterior),isolated)) return refused;
        const double margin=std::max(1.,height);
        const auto tool=[&](const gp_Vec& shift) {return BRepPrimAPI_MakeCylinder(gp_Ax2(base.Translated(shift-gp_Vec(axis)*margin),axis),radius,height+2*margin).Shape();};
        const auto oldTool=tool(gp_Vec(0,0,0)),newTool=tool(delta);
        const BRepExtrema_DistShapeShape oldClearance(oldTool,exterior),newClearance(newTool,exterior);
        if(!oldClearance.IsDone()||oldClearance.Value()<=100*tolerance) return refused;
        if(!newClearance.IsDone()||newClearance.Value()<=100*tolerance)
            return refuse(Status::Failure,Code::InteractingFeatures,"Translated hole touches or intersects preserved exterior feature material.");
        auto editFace=plane.IsNull()?target:TopoDS::Face(copy.ModifiedShape(plane));
        if(!plane.IsNull()) editFace.Orientation(plane.Orientation());
        if(!plane.IsNull()) {
            const auto support=BRepAdaptor_Surface(editFace).Plane();auto outward=support.Axis().Direction();
            if(editFace.Orientation()==TopAbs_REVERSED) outward.Reverse();
            if(std::abs(outward.Dot(axis))>angular) return refused;
            for(TopExp_Explorer it(editFace,TopAbs_VERTEX);it.More();it.Next()) {
                const double d=coordinate(BRep_Tool::Pnt(TopoDS::Vertex(it.Current())),axis);
                if(d<low-tolerance||d>high+tolerance) return refused;
            }
            // Clearance only: conservative analytic bounds include cylinder
            // extrema absent from the seam vertices. These never crop geometry.
            Bnd_Box bounds;BRepBndLib::AddOptimal(exterior,bounds,false,false);
            double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
            const auto origin=support.Location();
            const double d=std::max((x0-origin.X())*outward.X(),(x1-origin.X())*outward.X())+
                std::max((y0-origin.Y())*outward.Y(),(y1-origin.Y())*outward.Y())+
                std::max((z0-origin.Z())*outward.Z(),(z1-origin.Z())*outward.Z());
            if(d>=-100*tolerance||d-distance>=-100*tolerance) return refused;
        }
        TopoDS_Face slabFace;
        for(TopExp_Explorer it(slab,TopAbs_FACE);it.More();it.Next()) {
            const auto f=TopoDS::Face(it.Current());
            if(BRepReconstruction::sameAnalyticSupport(editFace,f)) {slabFace=f;break;}
        }
        Result answer;
        if(plane.IsNull()) answer=moveSelected(slab,slabFace,delta,std::nullopt);
        else {
            // The original plane patch must survive isolation unchanged.
            if(slabFace.IsNull()||std::abs(faceArea(slabFace)-faceArea(editFace))>1.e-8*std::max(1.,faceArea(editFace))||
               std::abs(faceArea(BooleanGeometry::combine<BRepAlgoAPI_Common>(slabFace,editFace))-faceArea(editFace))>1.e-8*std::max(1.,faceArea(editFace))) return refused;
            QString reason;answer.shape=MoveFaceGeometry::move(slab,slabFace,distance,reason);
            if(answer.shape.IsNull()) return refused;
            answer.status=Status::Success;answer.code=std::abs(distance)<tolerance?Code::Unchanged:Code::Moved;
            answer.message="Stock plane moved with exterior features preserved.";
        }
        if(!answer) return answer;
        TopoDS_Shape local;
        if(plane.IsNull()) {
            const auto originalVolume=BRepPrimAPI_MakeCylinder(gp_Ax2(base,axis),radius,height).Shape();
            local=BooleanGeometry::combine<BRepAlgoAPI_Cut>(BooleanGeometry::combine<BRepAlgoAPI_Fuse>(isolated,originalVolume),newTool);
        } else if(std::abs(distance)<tolerance) local=isolated;
        else if(distance>0) local=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(isolated,BooleanGeometry::combine<BRepAlgoAPI_Cut>(answer.shape,slab));
        else local=BooleanGeometry::combine<BRepAlgoAPI_Cut>(isolated,BooleanGeometry::combine<BRepAlgoAPI_Cut>(slab,answer.shape));
        const auto expected=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(answer.shape,exterior);
        TopoDS_Shape result;QString error;
        if(!BRepReconstruction::normalize(local,result,error,true)||!manifold(result)||!BRepReconstruction::equivalent(result,expected)||
           !BRepReconstruction::equivalent(BooleanGeometry::combine<BRepAlgoAPI_Common>(result,exterior),exterior))
            return refuse(Status::Failure,Code::InvalidResult,"Reconstruction changed preserved exterior material or invalidated the solid.");
        for(TopExp_Explorer it(exterior,TopAbs_FACE);it.More();it.Next()) {
            const auto f=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(f);
            if(s.GetType()==GeomAbs_Plane&&s.Plane().Axis().Direction().IsParallel(axis,angular)) {
                const double d=coordinate(s.Plane().Location(),axis);
                if(std::abs(d-low)<=tolerance||std::abs(d-high)<=tolerance) continue; // partition interface, not exterior support
            }
            double covered=0;
            for(TopExp_Explorer next(result,TopAbs_FACE);next.More();next.Next()) {
                const auto candidate=TopoDS::Face(next.Current());
                if(BRepReconstruction::sameAnalyticSupport(f,candidate)) covered+=faceArea(BooleanGeometry::combine<BRepAlgoAPI_Common>(f,candidate));
            }
            if(std::abs(covered-faceArea(f))>1.e-8*std::max(1.,faceArea(f)))
                return refuse(Status::Failure,Code::InvalidResult,"A preserved exterior feature's trimmed surface changed.");
        }
        answer.shape=result;return answer;
    } catch(const Standard_Failure&) {return refused;}
      catch(const std::exception&) {return refused;}
}

Result moveLocalHole(const TopoDS_Shape& source,const TopoDS_Face& selected,
                     const gp_Vec& delta,const Result& refused) {
    if(refused.status!=Status::Unsupported||!std::isfinite(delta.X())||!std::isfinite(delta.Y())||!std::isfinite(delta.Z())) return refused;
    try {
        BRepReconstruction::Neighborhood local;QString error;
        if(!BRepReconstruction::neighborhood(source,selected,local,error)||!manifold(local.solid)||
           BRepAdaptor_Surface(local.target).GetType()!=GeomAbs_Cylinder||local.neighbors.size()!=2) return refused;
        const double tolerance=10*Precision::Confusion();
        const auto cylinder=BRepAdaptor_Surface(local.target).Cylinder();const auto axis=cylinder.Axis().Direction();
        const double radius=cylinder.Radius();
        if(radius<=100*tolerance||radius>1.e7||std::abs(delta.Dot(gp_Vec(axis)))>tolerance) return refused;
        std::array<double,2> ends{},lengths{};
        for(size_t i=0;i<2;++i) {
            const BRepAdaptor_Surface s(local.neighbors[i]);
            if(s.GetType()!=GeomAbs_Plane||!s.Plane().Axis().Direction().IsParallel(axis,angular)) return refused;
            ends[i]=coordinate(s.Plane().Location(),axis);
        }
        const double low=std::min(ends[0],ends[1]),height=std::abs(ends[1]-ends[0]);
        if(height<=100*tolerance||height>1.e7) return refused;
        for(const auto& edge:local.boundary) {
            const BRepAdaptor_Curve curve(edge);if(curve.GetType()!=GeomAbs_Circle) return refused;
            const auto circle=curve.Circle();const double z=coordinate(circle.Location(),axis);
            const int end=std::abs(z-ends[0])<=tolerance?0:std::abs(z-ends[1])<=tolerance?1:-1;
            if(end<0||std::abs(circle.Radius()-radius)>tolerance||gp_Lin(cylinder.Axis()).Distance(circle.Location())>tolerance) return refused;
            GProp_GProps p;BRepGProp::LinearProperties(edge,p);lengths[end]+=p.Mass();
        }
        for(double length:lengths) if(std::abs(length-2*pi*radius)>tolerance*std::max(1.,radius)) return refused;
        double area=0;
        for(const auto& face:local.patch) {
            area+=faceArea(face);const BRepAdaptor_Surface s(face);
            const double u=(s.FirstUParameter()+s.LastUParameter())/2,v=(s.FirstVParameter()+s.LastVParameter())/2;
            BRepLProp_SLProps p(s,u,v,1,tolerance);if(!p.IsNormalDefined()) return refused;
            auto normal=p.Normal();if(face.Orientation()==TopAbs_REVERSED) normal.Reverse();
            auto radial=gp_Vec(cylinder.Location(),s.Value(u,v));radial-=gp_Vec(axis)*radial.Dot(gp_Vec(axis));
            if(normal.Dot(gp_Dir(radial))>-.99) return refused;
        }
        if(std::abs(area-2*pi*radius*height)>tolerance*std::max(1.,radius*height)) return refused;
        const auto base=cylinder.Location().Translated(gp_Vec(axis)*(low-coordinate(cylinder.Location(),axis)));
        const double probe=std::min(radius,height)*1.e-4;if(probe<10*tolerance) return refused;
        for(int i=0;i<12;++i) for(double fraction:{.2,.5,.8}) {
            const double angle=2*pi*(i+.37)/12;
            const auto radial=gp_Vec(cylinder.XAxis().Direction())*std::cos(angle)+gp_Vec(cylinder.YAxis().Direction())*std::sin(angle);
            const auto center=base.Translated(gp_Vec(axis)*(height*fraction));
            if(classify(local.solid,center.Translated(radial*(radius-probe)),tolerance)!=TopAbs_OUT||
               classify(local.solid,center.Translated(radial*(radius+probe)),tolerance)!=TopAbs_IN) return refused;
        }
        const auto oldTool=BRepPrimAPI_MakeCylinder(gp_Ax2(base,axis),radius,height).Shape();
        GProp_GProps overlap;BRepGProp::VolumeProperties(BooleanGeometry::combine<BRepAlgoAPI_Common>(local.solid,oldTool),overlap);
        if(std::abs(overlap.Mass())>1.e-8*std::max(1.,pi*radius*radius*height)) return refused;
        const auto filled=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(local.solid,oldTool);
        TopoDS_Shape healed;if(!BRepReconstruction::normalize(filled,healed,error,true)||
            !BRepReconstruction::equivalent(BooleanGeometry::combine<BRepAlgoAPI_Cut>(healed,local.solid),oldTool)) return refused;
        // Both missing disks must close on the actual rim-plane boundaries.
        // No stock box or original creator is involved in the fill certificate.
        for(TopExp_Explorer it(oldTool,TopAbs_FACE);it.More();it.Next()) {
            const auto cap=TopoDS::Face(it.Current());if(BRepAdaptor_Surface(cap).GetType()!=GeomAbs_Plane) continue;
            double covered=0;
            for(TopExp_Explorer next(healed,TopAbs_FACE);next.More();next.Next()) {
                const auto f=TopoDS::Face(next.Current());
                if(BRepReconstruction::sameAnalyticSupport(cap,f)) covered+=faceArea(BooleanGeometry::combine<BRepAlgoAPI_Common>(cap,f));
            }
            if(std::abs(covered-pi*radius*radius)>1.e-8*std::max(1.,pi*radius*radius)) return refused;
        }
        const auto tool=BRepPrimAPI_MakeCylinder(gp_Ax2(base.Translated(delta),axis),radius,height).Shape();
        const auto candidate=BooleanGeometry::combine<BRepAlgoAPI_Cut>(healed,tool);
        auto affected=local.patch;affected.insert(affected.end(),local.neighbors.begin(),local.neighbors.end());
        gp_Trsf shift;shift.SetTranslation(delta);BRepReconstruction::Result finished;
        const auto region=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(oldTool,tool);
        if(!BRepReconstruction::finishLocal(local,candidate,region,affected,local.patch,shift,finished,error)||!manifold(finished.solid)) {
            auto failure=refuse(Status::Unsupported,Code::InteractingFeatures,"Local hole reconstruction cannot preserve unrelated material and trimmed surfaces: ");
            failure.message+=error.toStdString();return failure;
        }
        auto translated=cylinder;translated.Translate(delta);bool remains=false;
        for(TopExp_Explorer it(finished.solid,TopAbs_FACE);it.More();it.Next()) {
            const auto f=TopoDS::Face(it.Current());bool allowed=false;
            if(BRepAdaptor_Surface(f).GetType()==GeomAbs_Cylinder&&sameCylinder(translated,BRepAdaptor_Surface(f).Cylinder(),tolerance)) {allowed=true;remains=true;}
            for(TopExp_Explorer old(local.solid,TopAbs_FACE);old.More()&&!allowed;old.Next()) {
                const auto face=TopoDS::Face(old.Current());
                if(std::none_of(local.patch.begin(),local.patch.end(),[&](const auto& p){return p.IsSame(face);})) allowed=BRepReconstruction::sameAnalyticSupport(face,f);
            }
            if(!allowed) return refuse(Status::Failure,Code::InvalidResult,"Local hole reconstruction introduced an unintended boundary support.");
        }
        Result result;result.status=Status::Success;result.code=delta.Magnitude()<=tolerance?Code::Unchanged:remains?Code::Moved:Code::HoleExited;
        result.shape=finished.solid;result.radius=radius;result.height=height;result.axis=gp_Ax1(base.Translated(delta),axis);
        result.message="Through-hole translated using its current local rims and material neighborhood.";return result;
    } catch(const Standard_Failure&) {return refused;}
      catch(const std::exception&) {return refused;}
}

// Planar rim supports can intersect a cylinder in circles, ellipses or trimmed
// curves. Infer the missing material from those actual planes, not edge types
// or a stock pattern. The same certificate serves lateral Move and radial Pull.
Result reconstructPlanarRimHole(const TopoDS_Shape& source,const TopoDS_Face& selected,
                               const gp_Vec& delta,double newRadius,const Result& refused) {
    if(refused.status!=Status::Unsupported) return refused;
    const auto unsupported=[](const char* message){return refuse(Status::Unsupported,Code::UnsupportedFeature,message);};
    try {
        BRepReconstruction::Neighborhood local;QString error;
        if(!BRepReconstruction::neighborhood(source,selected,local,error)) {
            auto failure=unsupported("Cylinder recognized, but its current neighborhood cannot be certified: ");failure.message+=error.toStdString();return failure;
        }
        const BRepAdaptor_Surface surface(local.target);if(surface.GetType()!=GeomAbs_Cylinder||!manifold(local.solid)) return refused;
        const auto cylinder=surface.Cylinder();const auto axis=cylinder.Axis().Direction();const double radius=cylinder.Radius(),tolerance=10*Precision::Confusion();
        if(!std::isfinite(delta.X())||!std::isfinite(delta.Y())||!std::isfinite(delta.Z())||std::abs(delta.Dot(gp_Vec(axis)))>tolerance)
            return refuse(Status::InvalidInput,Code::InvalidDisplacement,"Hole movement must be finite and perpendicular to its cylinder axis.");
        if(!std::isfinite(newRadius)||newRadius<=100*tolerance||newRadius>1.e7||radius<=100*tolerance||radius>1.e7) return refused;
        std::vector<gp_Pln> planes;double wall=0;
        for(const auto& face:local.patch) {
            const BRepAdaptor_Surface s(face);const double u=(s.FirstUParameter()+s.LastUParameter())/2,v=(s.FirstVParameter()+s.LastVParameter())/2;
            BRepLProp_SLProps p(s,u,v,1,tolerance);if(!p.IsNormalDefined()) return unsupported("Cylindrical wall material normal is undefined.");
            auto normal=p.Normal();if(face.Orientation()==TopAbs_REVERSED) normal.Reverse();
            auto radial=gp_Vec(cylinder.Location(),s.Value(u,v));radial-=gp_Vec(axis)*radial.Dot(gp_Vec(axis));
            const double sign=normal.Dot(gp_Dir(radial));if(sign>.99) return refused; // Keep the boss certificate's diagnostic.
            if(sign>-.99) return unsupported("The current cylindrical patch has an ambiguous material normal.");
            wall+=faceArea(face);
        }
        for(const auto& face:local.neighbors) {
            const BRepAdaptor_Surface s(face);if(s.GetType()!=GeomAbs_Plane) return unsupported("Cylindrical hole reconstruction needs planar rim supports; a curved intersection needs another material certificate.");
            auto n=s.Plane().Axis().Direction();if(face.Orientation()==TopAbs_REVERSED) n.Reverse();
            const gp_Pln plane(s.Plane().Location(),n);
            if(std::none_of(planes.begin(),planes.end(),[&](const auto& p){return p.Axis().Direction().IsEqual(n,angular)&&p.Distance(plane.Location())<=tolerance;})) planes.push_back(plane);
        }
        if(!std::any_of(planes.begin(),planes.end(),[&](const auto& p){return p.Axis().Direction().Dot(axis)>angular;})||
           !std::any_of(planes.begin(),planes.end(),[&](const auto& p){return p.Axis().Direction().Dot(axis)<-angular;}))
            return unsupported("Current rim supports do not bound the cylindrical void in both axial directions.");
        const auto tool=[&](const gp_Pnt& origin,double r,double& low,double& high) {
            low=1.e100;high=-1.e100;
            for(const auto& plane:planes) {
                const auto n=plane.Axis().Direction();const double dot=n.Dot(axis);if(std::abs(dot)<=angular) continue;
                const double center=gp_Vec(origin,plane.Location()).Dot(gp_Vec(n))/dot;
                const double amplitude=r*std::sqrt(std::max(0.,1-dot*dot))/std::abs(dot);
                low=std::min(low,center-amplitude);high=std::max(high,center+amplitude);
            }
            const double margin=std::max(1.,r);const double height=high-low+2*margin;
            if(!std::isfinite(height)||height<=100*tolerance||height>1.e7) throw std::runtime_error("Unreliable planar rim span");
            TopoDS_Shape material=BRepPrimAPI_MakeCylinder(gp_Ax2(origin.Translated(gp_Vec(axis)*(low-margin)),axis),r,height).Shape();
            for(const auto& plane:planes) material=BooleanGeometry::combine<BRepAlgoAPI_Common>(material,
                BRepPrimAPI_MakeHalfSpace(BRepBuilderAPI_MakeFace(plane).Face(),plane.Location().Translated(gp_Vec(plane.Axis().Direction())*-margin)).Solid());
            for(TopExp_Explorer it(material,TopAbs_FACE);it.More();it.Next()) {
                const BRepAdaptor_Surface s(TopoDS::Face(it.Current()));
                if(s.GetType()==GeomAbs_Plane&&std::none_of(planes.begin(),planes.end(),[&](const auto& p){return p.Axis().Direction().IsParallel(s.Plane().Axis().Direction(),angular)&&p.Distance(s.Plane().Location())<=tolerance;}))
                    throw std::runtime_error("Cylinder clipping retained a computational end cap rather than an actual rim support.");
            }
            return material;
        };
        double oldLow=0,oldHigh=0;const auto missing=tool(cylinder.Location(),radius,oldLow,oldHigh);double mass=0;
        if(!BRepReconstruction::validSolid(missing,mass)) return unsupported("The rim planes do not bound one finite cylindrical void.");
        GProp_GProps occupied;BRepGProp::VolumeProperties(BooleanGeometry::combine<BRepAlgoAPI_Common>(local.solid,missing),occupied);
        if(std::abs(occupied.Mass())>1.e-8*std::max(1.,mass)) return unsupported("The inferred cylindrical void crosses existing material.");
        double coveredWall=0;for(TopExp_Explorer it(missing,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());if(BRepAdaptor_Surface(face).GetType()!=GeomAbs_Cylinder) continue;
            coveredWall+=faceArea(face);
            double covered=0;for(const auto& patch:local.patch) covered+=faceArea(BooleanGeometry::combine<BRepAlgoAPI_Common>(face,patch));
            if(std::abs(covered-faceArea(face))>1.e-8*std::max(1.,faceArea(face))) return unsupported("Current hole wall does not cover the cylinder bounded by its actual rim planes.");
        }
        if(std::abs(wall-coveredWall)>1.e-8*std::max(1.,wall)) return unsupported("The hole has additional cylindrical trims outside its rim supports.");
        TopoDS_Shape healed;
        if(!BRepReconstruction::normalize(BooleanGeometry::combine<BRepAlgoAPI_Fuse>(local.solid,missing),healed,error,true)||
           !BRepReconstruction::equivalent(BooleanGeometry::combine<BRepAlgoAPI_Cut>(healed,local.solid),missing)||
           !BRepReconstruction::equivalent(BooleanGeometry::combine<BRepAlgoAPI_Cut>(healed,missing),local.solid))
            return unsupported("Filling the planar-rim cylindrical void failed independent material reconstruction.");
        const auto exteriorEnds=[&](const TopoDS_Shape& material) {
            for(TopExp_Explorer it(material,TopAbs_FACE);it.More();it.Next()) {
                const auto face=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(face);if(s.GetType()!=GeomAbs_Plane) continue;
                if(std::none_of(planes.begin(),planes.end(),[&](const auto& p){return p.Axis().Direction().IsParallel(s.Plane().Axis().Direction(),angular)&&p.Distance(s.Plane().Location())<=tolerance;})) continue;
                double covered=0;for(TopExp_Explorer support(healed,TopAbs_FACE);support.More();support.Next()) {
                    const auto f=TopoDS::Face(support.Current());if(BRepReconstruction::sameAnalyticSupport(face,f)) covered+=faceArea(BooleanGeometry::combine<BRepAlgoAPI_Common>(face,f));
                }
                if(std::abs(covered-faceArea(face))>1.e-8*std::max(1.,faceArea(face))) return false;
            }return true;
        };
        if(!exteriorEnds(missing)) return unsupported("The current cylindrical void does not terminate on exterior rim surfaces; blind/interacting material is preserved.");
        double low=0,high=0;const auto destination=tool(cylinder.Location().Translated(delta),newRadius,low,high);
        if(!BRepReconstruction::validSolid(destination,mass)) return unsupported("Requested cylinder and rim planes have no reliable finite intersection.");
        const auto removed=BooleanGeometry::combine<BRepAlgoAPI_Common>(healed,destination);
        if(!exteriorEnds(removed)) return unsupported("Requested hole would end inside material beyond its current rim supports.");
        const auto candidate=BooleanGeometry::combine<BRepAlgoAPI_Cut>(healed,destination);
        auto affected=local.patch;affected.insert(affected.end(),local.neighbors.begin(),local.neighbors.end());
        for(TopExp_Explorer it(local.solid,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());
            if(BRepAdaptor_Surface(face).GetType()==GeomAbs_Plane&&faceArea(BooleanGeometry::combine<BRepAlgoAPI_Common>(face,destination))>tolerance*tolerance&&
               std::none_of(affected.begin(),affected.end(),[&](const auto& f){return f.IsSame(face);})) affected.push_back(face);
        }
        gp_Trsf identity;BRepReconstruction::Result finished;
        const auto region=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(missing,destination);
        if(!BRepReconstruction::finishLocal(local,candidate,region,affected,{},identity,finished,error)||!manifold(finished.solid)) {
            auto failure=unsupported("Cylindrical reconstruction cannot preserve unrelated material or trimmed faces: ");failure.message+=error.toStdString();return failure;
        }
        if(!BRepReconstruction::equivalent(BooleanGeometry::combine<BRepAlgoAPI_Cut>(healed,finished.solid),removed)) return unsupported("Reconstructed hole failed independent removed-material verification.");
        auto translated=cylinder;translated.Translate(delta);translated.SetRadius(newRadius);bool remains=false;
        for(TopExp_Explorer it(finished.solid,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(face);
            bool allowed=s.GetType()==GeomAbs_Cylinder&&sameCylinder(s.Cylinder(),translated,tolerance);remains=remains||allowed;
            for(TopExp_Explorer old(local.solid,TopAbs_FACE);old.More()&&!allowed;old.Next()) {
                const auto f=TopoDS::Face(old.Current());if(std::none_of(local.patch.begin(),local.patch.end(),[&](const auto& p){return p.IsSame(f);})) allowed=BRepReconstruction::sameAnalyticSupport(f,face);
            }
            if(!allowed) return unsupported("Requested hole introduced an unintended analytic boundary surface.");
        }
        Result result;result.status=Status::Success;result.code=delta.Magnitude()<=tolerance&&std::abs(newRadius-radius)<=tolerance?Code::Unchanged:remains?Code::Moved:Code::HoleExited;
        result.shape=finished.solid;result.radius=newRadius;result.height=high-low;result.axis=gp_Ax1(cylinder.Location().Translated(delta+gp_Vec(axis)*low),axis);
        result.message="Cylindrical hole reconstructed against its current planar rim supports.";return result;
    } catch(const Standard_Failure& exception) {auto failure=unsupported("OCCT could not certify the requested cylinder/plane intersection: ");failure.message+=exception.GetMessageString();return failure;}
      catch(const std::exception& exception) {auto failure=unsupported("Current rim planes could not certify the cylindrical material reconstruction: ");failure.message+=exception.what();return failure;}
}

// Shared acceptance for the already certified fill/healed body. The selected
// cylindrical wall resolves the whole feature, including cap/underside patches.
Result finishCylindricalRemoval(const BRepReconstruction::Neighborhood& local,
                               const TopoDS_Shape& healed,const TopoDS_Shape& region,
                               BRepReconstruction::Result& removal) {
    auto affected=local.patch;affected.insert(affected.end(),local.neighbors.begin(),local.neighbors.end());
    QString error;gp_Trsf identity;BRepReconstruction::Result finished;
    if(!BRepReconstruction::finishLocal(local,healed,region,affected,{},identity,finished,error)||!manifold(finished.solid)) {
        auto failure=refuse(Status::Unsupported,Code::InvalidResult,"Delete Face cannot preserve the surrounding material and manifold topology: ");failure.message+=error.toStdString();return failure;
    }
    for(const auto& patch:local.patch) for(TopExp_Explorer it(finished.solid,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());
        if(BRepReconstruction::sameAnalyticSupport(patch,face)&&faceArea(BooleanGeometry::combine<BRepAlgoAPI_Common>(patch,face))>1.e-8)
            return refuse(Status::Unsupported,Code::InvalidResult,"Delete Face retained part of the selected cylindrical feature.");
    }
    for(TopExp_Explorer it(finished.solid,TopAbs_FACE);it.More();it.Next()) {
        bool mapped=false;for(const auto& mapping:finished.history) for(const auto& face:mapping.modified) if(face.IsSame(it.Current())) mapped=true;
        if(!mapped) return refuse(Status::Unsupported,Code::InvalidResult,"Delete Face introduced an uncertified closing surface.");
    }
    removal=std::move(finished);Result result;result.status=Status::Success;result.shape=removal.solid;
    result.message="Cylindrical feature removed from its certified current material neighborhood.";return result;
}

// Recover only the cylinder material on the actual exterior plane's material
// side. The current notch rims define everything; no former stock is retained.
Result moveLocalNotch(const TopoDS_Shape& source,const TopoDS_Face& selected,
                      const gp_Vec& delta,const Result& refused,BRepReconstruction::Result* removal=nullptr) {
    if(refused.status!=Status::Unsupported) return refused;
    const auto unsupported=[](const char* message){return refuse(Status::Unsupported,Code::UnsupportedFeature,message);};
    try {
        BRepReconstruction::Neighborhood local;QString error;
        if(!BRepReconstruction::neighborhood(source,selected,local,error)||!manifold(local.solid)||BRepAdaptor_Surface(local.target).GetType()!=GeomAbs_Cylinder) return refused;
        const auto cylinder=BRepAdaptor_Surface(local.target).Cylinder();const auto axis=cylinder.Axis().Direction();
        const double radius=cylinder.Radius(),tolerance=10*Precision::Confusion();
        if(!std::isfinite(delta.X())||!std::isfinite(delta.Y())||!std::isfinite(delta.Z())||std::abs(delta.Dot(gp_Vec(axis)))>tolerance)
            return refuse(Status::InvalidInput,Code::InvalidDisplacement,"Notch displacement must be finite and perpendicular to its cylinder axis.");
        if(radius<=100*tolerance||radius>1.e7) return refused;
        double wallArea=0;
        for(const auto& face:local.patch) {
            const BRepAdaptor_Surface s(face);const double u=(s.FirstUParameter()+s.LastUParameter())/2,v=(s.FirstVParameter()+s.LastVParameter())/2;
            BRepLProp_SLProps p(s,u,v,1,tolerance);if(!p.IsNormalDefined()) return refused;
            auto n=p.Normal();if(face.Orientation()==TopAbs_REVERSED) n.Reverse();
            auto radial=gp_Vec(cylinder.Location(),s.Value(u,v));radial-=gp_Vec(axis)*radial.Dot(gp_Vec(axis));
            if(n.Dot(gp_Dir(radial))>-.99) return refused;
            const double probe=radius*1.e-4;
            if(classify(local.solid,s.Value(u,v).Translated(gp_Vec(gp_Dir(radial))*-probe),tolerance)!=TopAbs_OUT||
               classify(local.solid,s.Value(u,v).Translated(gp_Vec(gp_Dir(radial))*probe),tolerance)!=TopAbs_IN) return refused;
            wallArea+=faceArea(face);
        }
        std::vector<double> ends;std::optional<gp_Pln> side;
        for(const auto& face:local.neighbors) {
            const BRepAdaptor_Surface s(face);if(s.GetType()!=GeomAbs_Plane) return unsupported("Open notch movement requires planar axial rims and a planar exterior side; blends are unsupported.");
            auto normal=s.Plane().Axis().Direction();if(face.Orientation()==TopAbs_REVERSED) normal.Reverse();
            if(normal.IsParallel(axis,angular)) {
                const double end=coordinate(s.Plane().Location(),axis);
                if(std::none_of(ends.begin(),ends.end(),[&](double z){return std::abs(z-end)<=tolerance;})) ends.push_back(end);
            } else {
                if(std::abs(normal.Dot(axis))>angular) return unsupported("Sloped notch boundaries cannot be reconstructed safely.");
                if(side&&(!side->Axis().Direction().IsEqual(normal,angular)||side->Distance(s.Plane().Location())>tolerance))
                    return unsupported("Open notches crossing multiple exterior sides or corners are not yet supported.");
                side=gp_Pln(s.Plane().Location(),normal);
            }
        }
        if(ends.size()!=2) return unsupported("Open notch movement needs exactly two unambiguous axial end planes.");
        const double low=std::min(ends[0],ends[1]),height=std::abs(ends[0]-ends[1]);
        if(height<=100*tolerance||height>1.e7) return unsupported("The notch has no reliable finite through-span.");
        const auto base=cylinder.Location().Translated(gp_Vec(axis)*(low-coordinate(cylinder.Location(),axis)));
        std::array<double,2> arcs{};double lines=0;
        for(const auto& edge:local.boundary) {
            const BRepAdaptor_Curve curve(edge);GProp_GProps p;BRepGProp::LinearProperties(edge,p);
            if(curve.GetType()==GeomAbs_Circle) {
                const auto circle=curve.Circle();const double z=coordinate(circle.Location(),axis);
                const int end=std::abs(z-ends[0])<=tolerance?0:std::abs(z-ends[1])<=tolerance?1:-1;
                if(end<0||std::abs(circle.Radius()-radius)>tolerance||gp_Lin(cylinder.Axis()).Distance(circle.Location())>tolerance) return unsupported("Notch rims do not share the selected cylinder's axis and radius.");
                arcs[end]+=p.Mass();
            } else if(side&&curve.GetType()==GeomAbs_Line&&curve.Line().Direction().IsParallel(axis,angular)&&side->Distance(curve.Value(curve.FirstParameter()))<=tolerance) lines+=p.Mass();
            else return unsupported("Notch opening edges must follow one exterior plane parallel to the cylinder axis.");
        }
        const bool rim=side ? arcs[0]>100*tolerance&&arcs[0]<2*pi*radius-100*tolerance
                            : std::abs(arcs[0]-2*pi*radius)<=tolerance*std::max(1.,radius);
        if(!rim||std::abs(arcs[0]-arcs[1])>tolerance*std::max(1.,radius)||
           std::abs(lines-(side?2*height:0))>tolerance*std::max(1.,height)||std::abs(wallArea-height*arcs[0])>1.e-8*std::max(1.,wallArea))
            return unsupported("Notch wall and opening do not form one constant cylindrical through-span.");
        const auto oldTool=BRepPrimAPI_MakeCylinder(gp_Ax2(base,axis),radius,height).Shape();
        TopoDS_Shape missing=oldTool;
        if(side) {
            const auto half=BRepPrimAPI_MakeHalfSpace(BRepBuilderAPI_MakeFace(*side).Face(),side->Location().Translated(gp_Vec(side->Axis().Direction())*-std::max(1.,radius))).Solid();
            missing=BooleanGeometry::combine<BRepAlgoAPI_Common>(oldTool,half);
        }
        double mass=0;if(!BRepReconstruction::validSolid(missing,mass)) return unsupported("The inferred notch fill is degenerate or ambiguous.");
        GProp_GProps occupied;BRepGProp::VolumeProperties(BooleanGeometry::combine<BRepAlgoAPI_Common>(local.solid,oldTool),occupied);
        if(std::abs(occupied.Mass())>1.e-8*std::max(1.,mass)) return unsupported("The selected notch cylinder contains ambiguous existing material.");
        TopoDS_Shape healed;
        if(!BRepReconstruction::normalize(BooleanGeometry::combine<BRepAlgoAPI_Fuse>(local.solid,missing),healed,error,true)||
           !BRepReconstruction::equivalent(BooleanGeometry::combine<BRepAlgoAPI_Cut>(healed,local.solid),missing)||
           !BRepReconstruction::equivalent(BooleanGeometry::combine<BRepAlgoAPI_Cut>(healed,oldTool),local.solid))
            return unsupported("Filling the current notch did not recover a certified local material boundary.");
        // All fill planes must close on actual exterior surfaces. Destination
        // axial ends must also be exterior, refusing blind cuts under a boss.
        const auto closedOnExterior=[&](const TopoDS_Shape& volume,bool axialOnly) {
            for(TopExp_Explorer it(volume,TopAbs_FACE);it.More();it.Next()) {
                const auto face=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(face);
                if(s.GetType()!=GeomAbs_Plane||(axialOnly&&!s.Plane().Axis().Direction().IsParallel(axis,angular))) continue;
                if(axialOnly) {
                    const double z=coordinate(s.Plane().Location(),axis);
                    if(std::abs(z-ends[0])>tolerance&&std::abs(z-ends[1])>tolerance) return false;
                }
                double covered=0;for(TopExp_Explorer f(healed,TopAbs_FACE);f.More();f.Next()) {
                    const auto support=TopoDS::Face(f.Current());
                    if(BRepReconstruction::sameAnalyticSupport(face,support)) covered+=faceArea(BooleanGeometry::combine<BRepAlgoAPI_Common>(face,support));
                }
                if(std::abs(covered-faceArea(face))>1.e-8*std::max(1.,faceArea(face))) return false;
            }return true;
        };
        if(!closedOnExterior(missing,false)) return unsupported("The inferred notch fill does not close its actual exterior side and axial rims.");
        double expectedWall=0;for(TopExp_Explorer it(missing,TopAbs_FACE);it.More();it.Next())
            if(BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType()==GeomAbs_Cylinder) expectedWall+=faceArea(it.Current());
        if(std::abs(expectedWall-wallArea)>1.e-8*std::max(1.,wallArea)) return unsupported("The partial mantle differs from the inferred exterior-boundary trim.");
        if(removal) return finishCylindricalRemoval(local,healed,missing,*removal);
        const auto tool=BRepPrimAPI_MakeCylinder(gp_Ax2(base.Translated(delta),axis),radius,height).Shape();
        const auto removed=BooleanGeometry::combine<BRepAlgoAPI_Common>(healed,tool);
        if(!closedOnExterior(removed,true)) return refuse(Status::Unsupported,Code::InteractingFeatures,"Translated notch would enter a blind region or intersect another feature's support.");
        const auto candidate=BooleanGeometry::combine<BRepAlgoAPI_Cut>(healed,tool);
        auto affected=local.patch;affected.insert(affected.end(),local.neighbors.begin(),local.neighbors.end());
        // A restored complete hole can open against an exterior side on its next
        // move. Only planar side regions actually cut by the new finite tool
        // may change; other planes and every unrelated cylindrical patch remain
        // protected by the local material/surface certificate.
        for(TopExp_Explorer it(local.solid,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(face);
            if(s.GetType()==GeomAbs_Plane&&std::abs(s.Plane().Axis().Direction().Dot(axis))<=angular&&
               faceArea(BooleanGeometry::combine<BRepAlgoAPI_Common>(face,tool))>tolerance*tolerance&&
               std::none_of(affected.begin(),affected.end(),[&](const auto& f){return f.IsSame(face);})) affected.push_back(face);
        }
        gp_Trsf shift;shift.SetTranslation(delta);BRepReconstruction::Result finished;
        const auto region=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(oldTool,tool);
        if(!BRepReconstruction::finishLocal(local,candidate,region,affected,local.patch,shift,finished,error)||!manifold(finished.solid)) {
            auto failure=refuse(Status::Unsupported,Code::InteractingFeatures,"Notch translation cannot preserve an unrelated feature or exterior surface: ");failure.message+=error.toStdString();return failure;
        }
        GProp_GProps removedMass;BRepGProp::VolumeProperties(removed,removedMass);
        if(removedMass.Mass()>1.e-10&&!BRepReconstruction::equivalent(BooleanGeometry::combine<BRepAlgoAPI_Cut>(healed,finished.solid),removed))
            return refuse(Status::Failure,Code::InvalidResult,"Notch reconstruction changed material beyond the requested cylinder cut.");
        auto translated=cylinder;translated.Translate(delta);bool remains=false;TopoDS_Face movedWall;
        for(TopExp_Explorer it(finished.solid,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(face);
            bool allowed=s.GetType()==GeomAbs_Cylinder&&sameCylinder(s.Cylinder(),translated,tolerance);
            if(allowed&&faceArea(BooleanGeometry::combine<BRepAlgoAPI_Common>(face,tool))>tolerance*tolerance) {remains=true;movedWall=face;}
            for(TopExp_Explorer old(local.solid,TopAbs_FACE);old.More()&&!allowed;old.Next()) {
                const auto f=TopoDS::Face(old.Current());if(std::none_of(local.patch.begin(),local.patch.end(),[&](const auto& p){return p.IsSame(f);})) allowed=BRepReconstruction::sameAnalyticSupport(f,face);
            }
            if(!allowed) return refuse(Status::Failure,Code::InvalidResult,"Notch translation introduced an unintended boundary support.");
        }
        if(remains) {
            BRepReconstruction::Neighborhood next;if(!BRepReconstruction::neighborhood(finished.solid,movedWall,next,error)) return unsupported("The moved notch neighborhood could not be certified.");
            std::optional<gp_Pln> boundary;
            for(const auto& face:next.neighbors) {
                const BRepAdaptor_Surface s(face);if(s.GetType()!=GeomAbs_Plane) return unsupported("The destination notch meets an unsupported curved feature.");
                auto n=s.Plane().Axis().Direction();if(face.Orientation()==TopAbs_REVERSED) n.Reverse();
                if(n.IsParallel(axis,angular)) continue;
                if(std::abs(n.Dot(axis))>angular||(boundary&&(!boundary->Axis().Direction().IsEqual(n,angular)||boundary->Distance(s.Plane().Location())>tolerance)))
                    return unsupported("Destination notch crosses multiple exterior sides or a corner; original geometry was kept.");
                boundary=gp_Pln(s.Plane().Location(),n);
            }
        }
        Result result;result.status=Status::Success;result.code=delta.Magnitude()<=tolerance?Code::Unchanged:remains?Code::Moved:Code::HoleExited;
        result.shape=finished.solid;result.radius=radius;result.height=height;result.axis=gp_Ax1(base.Translated(delta),axis);
        result.message="Cylindrical notch translated from its current rims and certified exterior support.";return result;
    } catch(const Standard_Failure&) {return unsupported("OCCT could not certify the current notch reconstruction; original geometry was kept.");}
      catch(const std::exception&) {return unsupported("Notch material reconstruction failed; original geometry was kept.");}
}

// Boundary follow-up uses only the current cylinder and the healed support.
// Extruding an actual trimmed support face retains its holes and side boundaries.
struct BossRadiusEdit {std::optional<double> radius;gp_Ax1* attachment=nullptr;};
Result moveBoundaryBoss(const TopoDS_Shape& source,const TopoDS_Face& selected,
                        const gp_Vec& delta,BossBoundaryMode policy,const Result& refused,BRepReconstruction::Result* removal=nullptr,
                        const BossRadiusEdit* edit=nullptr) {
    if(refused.status!=Status::Unsupported) return refused;
    const auto unsupported=[](const char* message){return refuse(Status::Unsupported,Code::UnsupportedFeature,message);};
    try {
        BRepReconstruction::Neighborhood local;QString error;
        if(!BRepReconstruction::neighborhood(source,selected,local,error)||!manifold(local.solid)||
           BRepAdaptor_Surface(local.target).GetType()!=GeomAbs_Cylinder) return refused;
        const auto cylinder=BRepAdaptor_Surface(local.target).Cylinder();const auto axis=cylinder.Axis().Direction();
        const double radius=cylinder.Radius(),newRadius=edit&&edit->radius?*edit->radius:radius,tolerance=10*Precision::Confusion();
        if(!std::isfinite(delta.X())||!std::isfinite(delta.Y())||!std::isfinite(delta.Z())||std::abs(delta.Dot(gp_Vec(axis)))>tolerance)
            return refuse(Status::InvalidInput,Code::InvalidDisplacement,"Boss movement must be finite and perpendicular to its axis.");
        if(policy!=BossBoundaryMode::Trim&&policy!=BossBoundaryMode::Extend) return refused;
        std::vector<double> ends;double wall=0;
        for(const auto& face:local.neighbors) {
            const BRepAdaptor_Surface s(face);if(s.GetType()!=GeomAbs_Plane) return unsupported("Boundary boss needs planar cap, support and side intersections; blends are unsupported.");
            if(s.Plane().Axis().Direction().IsParallel(axis,angular)) {
                const double z=coordinate(s.Plane().Location(),axis);
                if(std::none_of(ends.begin(),ends.end(),[&](double e){return std::abs(e-z)<=tolerance;})) ends.push_back(z);
            } else if(std::abs(s.Plane().Axis().Direction().Dot(axis))>angular) return unsupported("Sloped boss trims are unsupported.");
        }
        if(ends.size()!=2||radius<=100*tolerance||radius>1.e7) return unsupported("Boss needs two distinct axial rim planes and a reliable radius.");
        const double height=std::abs(ends[1]-ends[0]);if(height<=100*tolerance||height>1.e7) return refused;
        for(const auto& face:local.patch) {
            const BRepAdaptor_Surface s(face);const double u=(s.FirstUParameter()+s.LastUParameter())/2,v=(s.FirstVParameter()+s.LastVParameter())/2;
            BRepLProp_SLProps p(s,u,v,1,tolerance);if(!p.IsNormalDefined()) return refused;
            auto n=p.Normal();if(face.Orientation()==TopAbs_REVERSED) n.Reverse();
            auto radial=gp_Vec(cylinder.Location(),s.Value(u,v));radial-=gp_Vec(axis)*radial.Dot(gp_Vec(axis));
            if(n.Dot(gp_Dir(radial))<.99) return unsupported("Selected wall is not an outward boss material boundary.");
            wall+=faceArea(face);
        }
        const auto low=cylinder.Location().Translated(gp_Vec(axis)*(std::min(ends[0],ends[1])-coordinate(cylinder.Location(),axis)));
        const auto oldTool=BRepPrimAPI_MakeCylinder(gp_Ax2(low,axis),radius,height).Shape();
        const auto oldFeature=BooleanGeometry::combine<BRepAlgoAPI_Common>(local.solid,oldTool);
        double mass=0;if(!BRepReconstruction::validSolid(oldFeature,mass)) return unsupported("Current boss material is empty or ambiguous.");
        TopoDS_Shape healed;
        if(!BRepReconstruction::normalize(BooleanGeometry::combine<BRepAlgoAPI_Cut>(local.solid,oldTool),healed,error,true)||
           !BRepReconstruction::equivalent(BooleanGeometry::combine<BRepAlgoAPI_Fuse>(healed,oldFeature),local.solid))
            return unsupported("Removing the current boss did not recover a certified support solid.");
        double expectedWall=0;for(TopExp_Explorer it(oldFeature,TopAbs_FACE);it.More();it.Next())
            if(BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType()==GeomAbs_Cylinder) expectedWall+=faceArea(it.Current());
        if(std::abs(wall-expectedWall)>1.e-8*std::max(1.,wall)) return unsupported("Boss mantle contains interacting or incomplete trims.");
        // Exactly one axial plane must attach to the healed exterior. Both
        // orientations are considered, so reversed cylinder axes work as well.
        TopoDS_Shape supportRegion;double baseZ=0;gp_Dir outward=axis;int attachments=0;
        for(double z:ends) {
            TopoDS_Shape region;auto direction=axis;if(z>std::min(ends[0],ends[1])+tolerance) direction.Reverse();
            for(TopExp_Explorer it(healed,TopAbs_FACE);it.More();it.Next()) {
                const auto face=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(face);
                if(s.GetType()!=GeomAbs_Plane||!s.Plane().Axis().Direction().IsParallel(axis,angular)||std::abs(coordinate(s.Plane().Location(),axis)-z)>tolerance) continue;
                auto n=s.Plane().Axis().Direction();if(face.Orientation()==TopAbs_REVERSED) n.Reverse();
                if(!n.IsEqual(direction,angular)) continue;
                const auto prism=BRepPrimAPI_MakePrism(face,gp_Vec(direction)*height).Shape();
                region=region.IsNull()?prism:BooleanGeometry::combine<BRepAlgoAPI_Fuse>(region,prism);
            }
            if(region.IsNull()) continue;
            GProp_GProps overlap;BRepGProp::VolumeProperties(BooleanGeometry::combine<BRepAlgoAPI_Common>(oldFeature,region),overlap);
            if(overlap.Mass()<=1.e-8) continue;
            ++attachments;supportRegion=region;baseZ=z;outward=direction;
        }
        if(attachments!=1) return unsupported("Boss must attach to one unambiguous planar exterior support.");
        const auto oldTrim=BooleanGeometry::combine<BRepAlgoAPI_Common>(oldTool,supportRegion);
        if(!BRepReconstruction::equivalent(oldFeature,oldTool)&&!BRepReconstruction::equivalent(oldFeature,oldTrim))
            return unsupported("Current boss is neither a full overhang nor the exact trim of its healed support.");
        if(edit) policy=BRepReconstruction::equivalent(oldFeature,oldTool)?BossBoundaryMode::Extend:BossBoundaryMode::Trim;
        const auto newTool=BRepPrimAPI_MakeCylinder(gp_Ax2(low.Translated(delta),axis),newRadius,height).Shape();
        const auto clipped=BooleanGeometry::combine<BRepAlgoAPI_Common>(newTool,supportRegion);
        if(!BRepReconstruction::validSolid(clipped,mass)||mass<=1.e-8)
            return unsupported("Destination boss has no reliable connected support overlap.");
        // Limit the boundary class to one planar side, excluding corner cuts,
        // curved hole boundaries and depth-varying support interactions.
        std::optional<gp_Pln> side;
        for(const auto& shape:{oldTrim,clipped}) for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(face);
            if(s.GetType()==GeomAbs_Cylinder) {if(!sameCylinder(s.Cylinder(),shape.IsSame(oldTrim)?cylinder:gp_Cylinder(gp_Ax3(low.Translated(delta),axis),newRadius),tolerance)) return unsupported("Boss footprint intersects a curved feature boundary.");continue;}
            if(s.GetType()!=GeomAbs_Plane) return unsupported("Boss support has an unsupported boundary surface.");
            if(s.Plane().Axis().Direction().IsParallel(axis,angular)) continue;
            auto n=s.Plane().Axis().Direction();if(face.Orientation()==TopAbs_REVERSED) n.Reverse();
            if(std::abs(n.Dot(axis))>angular||(side&&(!side->Axis().Direction().IsEqual(n,angular)||side->Distance(s.Plane().Location())>tolerance)))
                return unsupported("Boss reconstruction across corners or multiple side supports is not yet certified.");
            side=gp_Pln(s.Plane().Location(),n);
        }
        if(removal) {
            if(!side) return unsupported("Select the cap with an interior boss wall; automatic whole-boss deletion requires a certified boundary crossing.");
            if(!BRepReconstruction::equivalent(BooleanGeometry::combine<BRepAlgoAPI_Cut>(local.solid,healed),oldFeature))
                return unsupported("Delete Face could not independently certify the removed boss material.");
            return finishCylindricalRemoval(local,healed,oldFeature,*removal);
        }
        const bool unchanged=delta.Magnitude()<=tolerance&&std::abs(newRadius-radius)<=tolerance;
        const auto feature=unchanged?oldFeature:policy==BossBoundaryMode::Trim?clipped:newTool;
        const auto candidate=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(healed,feature);
        auto affected=local.patch;affected.insert(affected.end(),local.neighbors.begin(),local.neighbors.end());
        // Only the actual base-plane material interface may acquire new trims.
        for(TopExp_Explorer it(local.solid,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(face);
            if(s.GetType()==GeomAbs_Plane&&s.Plane().Axis().Direction().IsParallel(axis,angular)&&
               std::abs(coordinate(s.Plane().Location(),axis)-baseZ)<=tolerance&&
               faceArea(BooleanGeometry::combine<BRepAlgoAPI_Common>(face,newTool))>tolerance*tolerance&&
               std::none_of(affected.begin(),affected.end(),[&](const auto& f){return f.IsSame(face);})) affected.push_back(face);
        }
        const auto region=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(oldTool,newTool);gp_Trsf identity;BRepReconstruction::Result finished;
        if(!BRepReconstruction::finishLocal(local,candidate,region,affected,{},identity,finished,error)||!manifold(finished.solid)) {
            auto failure=unsupported("Boss reconstruction cannot preserve unrelated material or manifold topology: ");failure.message+=error.toStdString();return failure;
        }
        if(!BRepReconstruction::equivalent(BooleanGeometry::combine<BRepAlgoAPI_Cut>(finished.solid,healed),feature)||
           !BRepReconstruction::equivalent(BooleanGeometry::combine<BRepAlgoAPI_Cut>(finished.solid,feature),healed))
            return unsupported("Rebuilt boss failed independent added-material and support preservation checks.");
        Result result;result.status=Status::Success;result.code=unchanged?Code::Unchanged:Code::Moved;
        result.shape=finished.solid;result.axis=gp_Ax1(low.Translated(delta),axis);result.radius=newRadius;result.height=height;
        if(edit&&edit->attachment) *edit->attachment=gp_Ax1(cylinder.Location().Translated(delta+gp_Vec(axis)*(baseZ-coordinate(cylinder.Location(),axis))),outward);
        result.message=edit?"Boss radius and attachment certified from current cylindrical material.":"Boss translated from its current cylindrical wall and certified planar support.";return result;
    } catch(const Standard_Failure&) {return unsupported("OCCT could not certify boundary boss reconstruction; original geometry was kept.");}
      catch(const std::exception&) {return unsupported("Boundary boss material reconstruction failed; original geometry was kept.");}
}

Result moveLocalBoss(const TopoDS_Shape& source,const TopoDS_Face& selected,
                     const gp_Vec& delta,BossBoundaryMode policy,const Result& refused) {
    if(refused.status!=Status::Unsupported||!std::isfinite(delta.X())||!std::isfinite(delta.Y())||!std::isfinite(delta.Z())) return refused;
    if(policy!=BossBoundaryMode::Trim&&policy!=BossBoundaryMode::Extend) return refused;
    try {
        BRepReconstruction::Neighborhood local;QString error;
        if(!BRepReconstruction::neighborhood(source,selected,local,error)||
           BRepAdaptor_Surface(local.target).GetType()!=GeomAbs_Cylinder||local.neighbors.size()!=2) return refused;
        const double tolerance=10*Precision::Confusion();
        const auto cylinder=BRepAdaptor_Surface(local.target).Cylinder();const double radius=cylinder.Radius();
        const auto axis=cylinder.Axis().Direction();
        if(radius<=100*tolerance||std::abs(delta.Dot(gp_Vec(axis)))>tolerance) return refused;
        TopoDS_Face cap,support;
        for(const auto& face:local.neighbors) {
            const BRepAdaptor_Surface s(face);if(s.GetType()!=GeomAbs_Plane||!s.Plane().Axis().Direction().IsParallel(axis,angular)) return refused;
            TopTools_IndexedMapOfShape wires;TopExp::MapShapes(face,TopAbs_WIRE,wires);
            GProp_GProps p;BRepGProp::SurfaceProperties(face,p);
            if(wires.Extent()==1&&std::abs(p.Mass()-pi*radius*radius)<=tolerance*std::max(1.,radius*radius)&&
               gp_Lin(cylinder.Axis()).Distance(p.CentreOfMass())<=tolerance) {
                if(!cap.IsNull()) return refused;cap=face;
            } else {if(!support.IsNull()) return refused;support=face;}
        }
        if(cap.IsNull()||support.IsNull()) return refused;
        const double baseCoordinate=coordinate(BRepAdaptor_Surface(support).Plane().Location(),axis);
        const double capCoordinate=coordinate(BRepAdaptor_Surface(cap).Plane().Location(),axis);
        const double height=std::abs(capCoordinate-baseCoordinate);
        if(height<=100*tolerance||height>1.e7||radius>1.e7) return refused;
        auto outward=axis;if(capCoordinate<baseCoordinate) outward.Reverse();
        const auto base=cylinder.Location().Translated(gp_Vec(axis)*(baseCoordinate-coordinate(cylinder.Location(),axis)));
        std::array<double,2> lengths{};double area=0;
        for(const auto& edge:local.boundary) {
            const BRepAdaptor_Curve curve(edge);if(curve.GetType()!=GeomAbs_Circle) return refused;
            const auto circle=curve.Circle();const double z=coordinate(circle.Location(),axis);
            const int end=std::abs(z-baseCoordinate)<=tolerance?0:std::abs(z-capCoordinate)<=tolerance?1:-1;
            if(end<0||std::abs(circle.Radius()-radius)>tolerance||gp_Lin(cylinder.Axis()).Distance(circle.Location())>tolerance) return refused;
            GProp_GProps p;BRepGProp::LinearProperties(edge,p);lengths[end]+=p.Mass();
        }
        for(double length:lengths) if(std::abs(length-2*pi*radius)>tolerance*std::max(1.,radius)) return refused;
        for(const auto& face:local.patch) {
            area+=faceArea(face);BRepAdaptor_Surface s(face);
            const double u=(s.FirstUParameter()+s.LastUParameter())/2,v=(s.FirstVParameter()+s.LastVParameter())/2;
            BRepLProp_SLProps p(s,u,v,1,tolerance);if(!p.IsNormalDefined()) return refused;
            auto normal=p.Normal();if(face.Orientation()==TopAbs_REVERSED) normal.Reverse();
            auto radial=gp_Vec(base,s.Value(u,v));radial-=gp_Vec(axis)*radial.Dot(gp_Vec(axis));
            if(normal.Dot(gp_Dir(radial))<.99) return refused;
        }
        if(std::abs(area-2*pi*radius*height)>tolerance*std::max(1.,radius*height)) return refused;
        const double probe=std::min(radius,height)*1.e-4;if(probe<10*tolerance) return refused;
        for(int i=0;i<12;++i) for(double fraction:{.2,.5,.8}) {
            const double angle=2*pi*(i+.37)/12;
            const auto radial=gp_Vec(cylinder.XAxis().Direction())*std::cos(angle)+gp_Vec(cylinder.YAxis().Direction())*std::sin(angle);
            const auto center=base.Translated(gp_Vec(outward)*(height*fraction));
            if(classify(local.solid,center.Translated(radial*(radius-probe)),tolerance)!=TopAbs_IN||
               classify(local.solid,center.Translated(radial*(radius+probe)),tolerance)!=TopAbs_OUT) return refused;
        }
        const auto oldBoss=BRepPrimAPI_MakeCylinder(gp_Ax2(base,outward),radius,height).Shape();
        if(!BRepReconstruction::equivalent(BooleanGeometry::combine<BRepAlgoAPI_Common>(local.solid,oldBoss),oldBoss)) return refused;
        auto removed=local.patch;removed.push_back(cap);
        // Same OCCT closure strategy as Delete Face, certified independently
        // below against the actual cylinder volume instead of any stock pattern.
        BRepAlgoAPI_Defeaturing deletion;deletion.SetShape(local.solid);deletion.SetToFillHistory(true);
        for(const auto& face:removed) deletion.AddFaceToRemove(face);
        deletion.Build();
        if(!deletion.IsDone()||deletion.HasErrors()||deletion.HasWarnings()) return refused;
        for(const auto& face:removed) if(!deletion.IsDeleted(face)) return refused;
        TopoDS_Shape healed;if(!BRepReconstruction::normalize(deletion.Shape(),healed,error)) return refused;
        const auto baseOracle=BooleanGeometry::combine<BRepAlgoAPI_Cut>(local.solid,oldBoss);
        if(!BRepReconstruction::equivalent(healed,baseOracle)) return refused;
        const auto diskInside=[&](const gp_Pnt& center) {
            const double r=radius+100*tolerance;
            const auto wire=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(center,outward),r)).Edge()).Wire();
            const auto disk=BRepBuilderAPI_MakeFace(gp_Pln(center,outward),wire,true).Face();
            double covered=0;
            for(TopExp_Explorer it(healed,TopAbs_FACE);it.More();it.Next()) {
                const auto face=TopoDS::Face(it.Current());
                if(BRepReconstruction::sameAnalyticSupport(support,face)) covered+=faceArea(BooleanGeometry::combine<BRepAlgoAPI_Common>(disk,face));
            }
            return std::abs(covered-pi*r*r)<=1.e-8*std::max(1.,pi*r*r);
        };
        if(!diskInside(base)) return refused;
        const auto destination=base.Translated(delta);
        if(!diskInside(destination))
            return refuse(Status::Unsupported,Code::InteractingFeatures,"Local boss movement requires a fully supported footprint clear of current boundary and feature trims.");
        const auto newBoss=BRepPrimAPI_MakeCylinder(gp_Ax2(destination,outward),radius,height).Shape();
        const auto candidate=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(healed,newBoss);
        const auto oracle=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(baseOracle,newBoss);
        if(!BRepReconstruction::equivalent(candidate,oracle)||
           !BRepReconstruction::equivalent(BooleanGeometry::combine<BRepAlgoAPI_Common>(candidate,newBoss),newBoss))
            return refuse(Status::Failure,Code::InvalidResult,"Local boss material does not match the requested translated cylinder.");
        const auto region=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(oldBoss,newBoss);
        auto affected=removed;affected.push_back(support);gp_Trsf shift;shift.SetTranslation(delta);
        BRepReconstruction::Result finished;
        if(!BRepReconstruction::finishLocal(local,candidate,region,affected,removed,shift,finished,error)||!manifold(finished.solid))
            return refuse(Status::Failure,Code::InvalidResult,"Local boss reconstruction changed protected material or face geometry.");
        // This local class preserves a COMPLETE boss. Generic local acceptance
        // allows affected supports to vanish, but this operation must retain
        // every translated wall/cap region (splits and merges remain valid).
        for(TopExp_Explorer it(newBoss,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(face);
            if(s.GetType()==GeomAbs_Plane&&s.Plane().Distance(destination)<=tolerance) continue; // material interface
            double covered=0;
            for(TopExp_Explorer next(finished.solid,TopAbs_FACE);next.More();next.Next()) {
                const auto finalFace=TopoDS::Face(next.Current());
                if(BRepReconstruction::sameAnalyticSupport(face,finalFace))
                    covered+=faceArea(BooleanGeometry::combine<BRepAlgoAPI_Common>(face,finalFace));
            }
            if(std::abs(covered-faceArea(face))>1.e-8*std::max(1.,faceArea(face)))
                return refuse(Status::Failure,Code::InvalidResult,"Local reconstruction lost part of the translated boss wall or cap.");
        }
        Result result;result.status=Status::Success;result.code=delta.Magnitude()<=tolerance?Code::Unchanged:Code::Moved;
        const auto lowEnd=cylinder.Location().Translated(gp_Vec(axis)*(std::min(baseCoordinate,capCoordinate)-coordinate(cylinder.Location(),axis)));
        result.shape=finished.solid;result.radius=radius;result.height=height;result.axis=gp_Ax1(lowEnd.Translated(delta),axis);
        result.message=policy==BossBoundaryMode::Trim?"Boss translated locally with Trim (fully supported footprint).":"Boss translated locally with Extend (fully supported footprint).";
        return result;
    } catch(const Standard_Failure&) {return refused;}
      catch(const std::exception&) {return refused;}
}

// A planar end can include both stock and the flat cut through a boss. Move
// that support by intersecting the actual finite cylinder with the new plane,
// not by sweeping the old (varying-section) end face or regenerating stock.
Result moveClippedBossPlane(const TopoDS_Shape& source,const TopoDS_Face& selected,double distance) {
    const auto unsupported=refuse(Status::Unsupported,Code::UnsupportedFeature,"No certified planar cut through one cylindrical boss was found.");
    if(!std::isfinite(distance)||distance<=10*Precision::Confusion()) return unsupported;
    try {
        BRepReconstruction::Neighborhood local;QString error;
        if(!BRepReconstruction::neighborhood(source,selected,local,error)||local.patch.size()!=1||BRepAdaptor_Surface(local.target).GetType()!=GeomAbs_Plane) return unsupported;
        auto plane=BRepAdaptor_Surface(local.target).Plane();auto normal=plane.Axis().Direction();
        if(local.target.Orientation()==TopAbs_REVERSED) normal.Reverse();
        TopoDS_Face mantle;
        for(const auto& face:local.neighbors) {
            const BRepAdaptor_Surface s(face);
            if(s.GetType()!=GeomAbs_Cylinder) continue;
            if(!mantle.IsNull()||std::abs(s.Cylinder().Axis().Direction().Dot(normal))>angular) return unsupported;
            mantle=face;
        }
        if(mantle.IsNull()) return unsupported;
        BRepReconstruction::Neighborhood boss;
        if(!BRepReconstruction::neighborhood(local.solid,mantle,boss,error)||boss.patch.size()!=1||boss.neighbors.size()!=3) return unsupported;
        const auto cylinder=BRepAdaptor_Surface(mantle).Cylinder();const auto axis=cylinder.Axis().Direction();
        const double radius=cylinder.Radius(),tolerance=10*Precision::Confusion();std::vector<double> ends;
        for(const auto& face:boss.neighbors) {
            const BRepAdaptor_Surface s(face);if(s.GetType()!=GeomAbs_Plane) return unsupported;
            if(BRepReconstruction::sameAnalyticSupport(face,local.target)) continue;
            if(!s.Plane().Axis().Direction().IsParallel(axis,angular)) return unsupported;
            ends.push_back(coordinate(s.Plane().Location(),axis));
        }
        if(ends.size()!=2||radius<=100*tolerance||radius>1.e7) return unsupported;
        const double low=std::min(ends[0],ends[1]),height=std::abs(ends[0]-ends[1]);
        if(height<=100*tolerance||height>1.e7) return unsupported;
        const auto base=cylinder.Location().Translated(gp_Vec(axis)*(low-coordinate(cylinder.Location(),axis)));
        const auto full=BRepPrimAPI_MakeCylinder(gp_Ax2(base,axis),radius,height).Shape();
        const auto clip=[&](const gp_Pln& p) {
            if(gp_Vec(p.Location(),base).Dot(gp_Vec(normal))<=-radius) return full;
            const auto inside=p.Location().Translated(gp_Vec(normal)*-std::max(1.,radius));
            return BooleanGeometry::combine<BRepAlgoAPI_Common>(full,BRepPrimAPI_MakeHalfSpace(BRepBuilderAPI_MakeFace(p).Face(),inside).Solid());
        };
        const auto oldBoss=clip(plane);
        if(!BRepReconstruction::equivalent(BooleanGeometry::combine<BRepAlgoAPI_Common>(local.solid,full),oldBoss)) return unsupported;
        // The selected cylindrical wall must be exterior boss material.
        BRepAdaptor_Surface s(mantle);const double u=(s.FirstUParameter()+s.LastUParameter())/2,v=(s.FirstVParameter()+s.LastVParameter())/2;
        BRepLProp_SLProps props(s,u,v,1,tolerance);if(!props.IsNormalDefined()) return unsupported;
        auto outward=props.Normal();if(mantle.Orientation()==TopAbs_REVERSED) outward.Reverse();
        auto radial=gp_Vec(base,s.Value(u,v));radial-=gp_Vec(axis)*radial.Dot(gp_Vec(axis));
        if(outward.Dot(gp_Dir(radial))<.99) return unsupported;
        TopoDS_Shape healed;
        if(!BRepReconstruction::normalize(BooleanGeometry::combine<BRepAlgoAPI_Cut>(local.solid,full),healed,error,true)||
           !BRepReconstruction::equivalent(BooleanGeometry::combine<BRepAlgoAPI_Fuse>(healed,oldBoss),local.solid)) return unsupported;
        TopoDS_Face stockEnd;
        for(TopExp_Explorer it(healed,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());
            if(BRepReconstruction::sameAnalyticSupport(local.target,face)) {
                if(!stockEnd.IsNull()) return unsupported;stockEnd=face;
            }
        }
        if(stockEnd.IsNull()) return unsupported;
        BRepReconstruction::Result movedStock;
        if(!BRepReconstruction::movePlanarLocal(healed,stockEnd,distance,movedStock,error)) return unsupported;
        const gp_Vec delta=gp_Vec(normal)*distance;plane.Translate(delta);
        const auto newBoss=clip(plane);
        const auto candidate=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(movedStock.solid,newBoss);
        // A full cylinder plus the exact, independently certified stock change
        // bounds the entire local edit. Unrelated material/surfaces stay fixed.
        const auto stockChange=BooleanGeometry::combine<BRepAlgoAPI_Cut>(movedStock.solid,healed);
        const auto region=BooleanGeometry::combine<BRepAlgoAPI_Fuse>(stockChange,full);
        auto affected=local.neighbors;affected.push_back(local.target);gp_Trsf shift;shift.SetTranslation(delta);
        BRepReconstruction::Result finished;
        if(!BRepReconstruction::finishLocal(local,candidate,region,affected,{local.target},shift,finished,error)||!manifold(finished.solid)||
           !BRepReconstruction::equivalent(BooleanGeometry::combine<BRepAlgoAPI_Common>(finished.solid,full),newBoss)||
           !BRepReconstruction::equivalent(BooleanGeometry::combine<BRepAlgoAPI_Cut>(finished.solid,full),movedStock.solid))
            return refuse(Status::Failure,Code::InvalidResult,"Moving the clipped boss plane failed local material or manifold certification.");
        bool targetPresent=false;
        for(TopExp_Explorer it(finished.solid,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());bool allowed=false;
            const auto translated=TopoDS::Face(BRepBuilderAPI_Transform(local.target,shift,true).Shape());
            if(BRepReconstruction::sameAnalyticSupport(translated,face)) {allowed=true;targetPresent=true;}
            for(TopExp_Explorer old(local.solid,TopAbs_FACE);old.More()&&!allowed;old.Next())
                if(!old.Current().IsSame(local.target)) allowed=BRepReconstruction::sameAnalyticSupport(TopoDS::Face(old.Current()),face);
            if(!allowed) return refuse(Status::Failure,Code::InvalidResult,"Moving the clipped boss plane introduced an unintended surface.");
        }
        if(!targetPresent) return refuse(Status::Failure,Code::InvalidResult,"The moved end plane disappeared.");
        Result result;result.status=Status::Success;result.code=Code::Moved;result.shape=finished.solid;
        result.message="Planar end moved with its cylindrical boss trim reconstructed from current supports.";return result;
    } catch(const Standard_Failure&) {return unsupported;}
      catch(const std::exception&) {return unsupported;}
}
}

CylindricalMoveGeometry::Result CylindricalMoveGeometry::moveThroughHole(
        const TopoDS_Shape& source,const TopoDS_Face& selected,const gp_Vec& delta) {
    const auto result=moveSelected(source,selected,delta,std::nullopt);
    if(result) return result;
    const auto exterior=moveWithExterior(source,selected,delta,result);
    if(exterior) return exterior;
    const auto local=moveLocalHole(source,selected,delta,exterior);
    if(local) return local;
    const auto notch=moveLocalNotch(source,selected,delta,local);
    if(notch) return notch;
    if(selected.IsNull()||BRepAdaptor_Surface(selected).GetType()!=GeomAbs_Cylinder) return notch;
    return reconstructPlanarRimHole(source,selected,delta,BRepAdaptor_Surface(selected).Cylinder().Radius(),notch);
}

bool CylindricalMoveGeometry::removeBoundaryFeature(const TopoDS_Shape& source,const TopoDS_Face& mantle,
        BRepReconstruction::Result& result,QString& error) {
    result={};error.clear();
    const auto refused=refuse(Status::Unsupported,Code::UnsupportedFeature,"Selected cylindrical wall has no certified boundary-feature closure.");
    const auto notch=moveLocalNotch(source,mantle,gp_Vec(),refused,&result);
    if(notch) return true;
    const auto boss=moveBoundaryBoss(source,mantle,gp_Vec(),BossBoundaryMode::Extend,refused,&result);
    if(boss) return true;
    result={};error=QStringLiteral("Cylindrical Delete Face: %1 %2").arg(QString::fromStdString(notch.message),QString::fromStdString(boss.message));return false;
}

static CylindricalMoveGeometry::Result resizeRadiusCertified(
        const TopoDS_Shape& source,const TopoDS_Face& selected,double radius) {
    const auto unsupported=[](const char* message){return refuse(Status::Unsupported,Code::UnsupportedFeature,message);};
    if(!std::isfinite(radius)||radius<=.01||radius>1.e7)
        return refuse(Status::InvalidInput,Code::InvalidDisplacement,"Cylinder radius must be finite and greater than 0.01 mm, within the geometry limit.");
    try {
        BRepReconstruction::Neighborhood local;QString error;
        if(!BRepReconstruction::neighborhood(source,selected,local,error)) {
            auto failure=unsupported("Pull / Resize cannot analyze the selected neighborhood: ");failure.message+=error.toStdString();return failure;
        }
        if(BRepAdaptor_Surface(local.target).GetType()!=GeomAbs_Cylinder||!manifold(local.solid))
            return unsupported("Radial Pull needs a cylindrical member face of one manifold solid.");
        const auto cylinder=BRepAdaptor_Surface(local.target).Cylinder();const auto axis=cylinder.Axis().Direction();
        const double oldRadius=cylinder.Radius(),tolerance=10*Precision::Confusion();std::vector<double> ends;std::vector<gp_Pln> clips;
        for(const auto& face:local.neighbors) {
            const BRepAdaptor_Surface s(face);if(s.GetType()!=GeomAbs_Plane) return unsupported("Radial Pull requires planar cap/support neighbors; blends and curved intersections are unsupported.");
            if(s.Plane().Axis().Direction().IsParallel(axis,angular)) ends.push_back(coordinate(s.Plane().Location(),axis));
            else {
                auto n=s.Plane().Axis().Direction();if(face.Orientation()==TopAbs_REVERSED) n.Reverse();
                if(std::abs(n.Dot(axis))>angular) return unsupported("Sloped cylindrical trims cannot be resized safely.");
                clips.emplace_back(s.Plane().Location(),n);
            }
        }
        if(ends.size()!=2||clips.size()>1||oldRadius<=100*tolerance)
            return unsupported("Radial Pull needs two distinct axial rim planes and at most one planar side cut.");
        const double low=std::min(ends[0],ends[1]),height=std::abs(ends[0]-ends[1]);
        if(height<=100*tolerance||height>1.e7) return unsupported("The selected cylinder has no reliable finite axial span.");
        const auto base=cylinder.Location().Translated(gp_Vec(axis)*(low-coordinate(cylinder.Location(),axis)));
        bool boss=false;bool first=true;
        for(const auto& face:local.patch) {
            const BRepAdaptor_Surface s(face);const double u=(s.FirstUParameter()+s.LastUParameter())/2,v=(s.FirstVParameter()+s.LastVParameter())/2;
            BRepLProp_SLProps p(s,u,v,1,tolerance);if(!p.IsNormalDefined()) return unsupported("Cylinder material side is undefined.");
            auto n=p.Normal();if(face.Orientation()==TopAbs_REVERSED) n.Reverse();
            auto radial=gp_Vec(base,s.Value(u,v));radial-=gp_Vec(axis)*radial.Dot(gp_Vec(axis));
            const double sign=n.Dot(gp_Dir(radial));
            if(std::abs(sign)<.99||(!first&&boss!=(sign>0))) return unsupported("Selected cylindrical patches have inconsistent material orientation.");
            boss=sign>0;first=false;
        }
        if(!boss&&!clips.empty()) return unsupported("Radial Pull of an open or partially trimmed hole is not certified; original geometry was kept.");
        const auto tool=[&](double r){return BRepPrimAPI_MakeCylinder(gp_Ax2(base,axis),r,height).Shape();};
        const auto clipped=[&](const TopoDS_Shape& c,double r) {
            if(clips.empty()) return c;
            const auto& plane=clips.front();const auto n=plane.Axis().Direction();
            if(gp_Vec(plane.Location(),base).Dot(gp_Vec(n))<=-r) return c;
            return BooleanGeometry::combine<BRepAlgoAPI_Common>(c,BRepPrimAPI_MakeHalfSpace(BRepBuilderAPI_MakeFace(plane).Face(),plane.Location().Translated(gp_Vec(n)*-std::max(1.,r))).Solid());
        };
        const auto oldTool=tool(oldRadius),newTool=tool(radius);
        const auto oldFeature=boss?clipped(oldTool,oldRadius):oldTool,newFeature=boss?clipped(newTool,radius):newTool;
        double material=0;if(!BRepReconstruction::validSolid(oldFeature,material)||!BRepReconstruction::validSolid(newFeature,material))
            return unsupported("The requested radius leaves an empty, degenerate or disconnected feature.");
        const auto overlap=BooleanGeometry::combine<BRepAlgoAPI_Common>(local.solid,oldTool);
        if(boss) {
            if(!BRepReconstruction::equivalent(overlap,oldFeature)) return unsupported("Boss material does not match its current cylinder and planar trims.");
        } else {
            GProp_GProps p;BRepGProp::VolumeProperties(overlap,p);
            if(std::abs(p.Mass())>1.e-8*std::max(1.,pi*oldRadius*oldRadius*height)) return unsupported("The selected hole is not an unambiguous complete void.");
        }
        double wall=0;for(const auto& face:local.patch) wall+=faceArea(face);
        double expectedWall=0;for(TopExp_Explorer it(oldFeature,TopAbs_FACE);it.More();it.Next())
            if(BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType()==GeomAbs_Cylinder) expectedWall+=faceArea(it.Current());
        if(std::abs(wall-expectedWall)>1.e-8*std::max(1.,expectedWall)) return unsupported("The selected mantle is incomplete or contains interacting trims.");
        TopoDS_Shape healed;
        const auto restored=boss?BooleanGeometry::combine<BRepAlgoAPI_Cut>(local.solid,oldTool):BooleanGeometry::combine<BRepAlgoAPI_Fuse>(local.solid,oldTool);
        if(!BRepReconstruction::normalize(restored,healed,error,true)) return unsupported("Removing/filling the selected feature did not recover one valid support solid.");
        const auto original=boss?BooleanGeometry::combine<BRepAlgoAPI_Fuse>(healed,oldFeature):BooleanGeometry::combine<BRepAlgoAPI_Cut>(healed,oldTool);
        if(!BRepReconstruction::equivalent(original,local.solid)) return unsupported("Feature reconstruction could not certify the original material independently.");
        if(!boss) for(TopExp_Explorer cap(oldTool,TopAbs_FACE);cap.More();cap.Next()) {
            const auto f=TopoDS::Face(cap.Current());if(BRepAdaptor_Surface(f).GetType()!=GeomAbs_Plane) continue;
            double covered=0;
            for(TopExp_Explorer stock(healed,TopAbs_FACE);stock.More();stock.Next()) {
                const auto support=TopoDS::Face(stock.Current());
                if(BRepReconstruction::sameAnalyticSupport(f,support)) covered+=faceArea(BooleanGeometry::combine<BRepAlgoAPI_Common>(f,support));
            }
            if(std::abs(covered-faceArea(f))>1.e-8*std::max(1.,faceArea(f))) return unsupported("Only complete through-holes closing on two exterior planes support radial Pull.");
        }
        if(boss) {
            int supported=0;
            for(TopExp_Explorer cap(newFeature,TopAbs_FACE);cap.More();cap.Next()) {
                const auto f=TopoDS::Face(cap.Current());const BRepAdaptor_Surface s(f);
                if(s.GetType()!=GeomAbs_Plane||!s.Plane().Axis().Direction().IsParallel(axis,angular)) continue;
                double covered=0;
                for(TopExp_Explorer stock(healed,TopAbs_FACE);stock.More();stock.Next()) {
                    const auto support=TopoDS::Face(stock.Current());
                    if(BRepReconstruction::sameAnalyticSupport(TopoDS::Face(f.Reversed()),support)) covered+=faceArea(BooleanGeometry::combine<BRepAlgoAPI_Common>(f,support));
                }
                if(covered>tolerance) {
                    if(std::abs(covered-faceArea(f))>1.e-8*std::max(1.,faceArea(f))) return unsupported("The resized boss footprint crosses an uncertified support boundary or another feature.");
                    ++supported;
                }
            }
            if(supported!=1) return unsupported("Radial Pull requires one fully supported boss base; overhang/support extensions are unsupported.");
        }
        const auto candidate=boss?BooleanGeometry::combine<BRepAlgoAPI_Fuse>(healed,newFeature):BooleanGeometry::combine<BRepAlgoAPI_Cut>(healed,newTool);
        auto affected=local.patch;affected.insert(affected.end(),local.neighbors.begin(),local.neighbors.end());
        const auto region=radius>oldRadius?newTool:oldTool;gp_Trsf identity;BRepReconstruction::Result finished;
        if(!BRepReconstruction::finishLocal(local,candidate,region,affected,{},identity,finished,error)||!manifold(finished.solid)) {
            auto failure=unsupported("Radial Pull cannot preserve unrelated material or manifold topology: ");failure.message+=error.toStdString();return failure;
        }
        auto resized=cylinder;resized.SetRadius(radius);
        for(TopExp_Explorer it(finished.solid,TopAbs_FACE);it.More();it.Next()) {
            const auto face=TopoDS::Face(it.Current());const BRepAdaptor_Surface s(face);
            bool allowed=s.GetType()==GeomAbs_Cylinder&&sameCylinder(s.Cylinder(),resized,tolerance);
            for(TopExp_Explorer old(local.solid,TopAbs_FACE);old.More()&&!allowed;old.Next()) {
                const auto f=TopoDS::Face(old.Current());
                if(std::none_of(local.patch.begin(),local.patch.end(),[&](const auto& p){return p.IsSame(f);})) allowed=BRepReconstruction::sameAnalyticSupport(f,face);
            }
            if(!allowed) return unsupported("Resizing would introduce an unintended transition surface; original geometry was kept.");
        }
        Result result;result.status=Status::Success;result.code=std::abs(radius-oldRadius)<=tolerance?Code::Unchanged:Code::Moved;
        result.shape=finished.solid;result.axis=gp_Ax1(base,axis);result.radius=radius;result.height=height;
        result.message="Cylinder radius reconstructed from current rims, material and planar trims.";return result;
    } catch(const Standard_Failure&) {return unsupported("OCCT could not certify this radial reconstruction; original geometry was kept.");}
      catch(const std::exception&) {return unsupported("Radial material reconstruction failed; original geometry was kept.");}
}

CylindricalMoveGeometry::Result CylindricalMoveGeometry::resizeRadius(
        const TopoDS_Shape& source,const TopoDS_Face& selected,double radius) {
    const auto result=resizeRadiusCertified(source,selected,radius);
    if(result) return result;
    const BossRadiusEdit edit{radius};
    const auto boss=moveBoundaryBoss(source,selected,gp_Vec(),BossBoundaryMode::Extend,result,nullptr,&edit);
    return boss?boss:reconstructPlanarRimHole(source,selected,gp_Vec(),radius,boss);
}

CylindricalMoveGeometry::Result CylindricalMoveGeometry::bossHeightReference(
        const TopoDS_Shape& source,const TopoDS_Face& cap) {
    const auto unsupported=[](const char* message){return refuse(Status::Unsupported,Code::UnsupportedFeature,message);};
    try {
        BRepReconstruction::Neighborhood local;QString error;
        if(!BRepReconstruction::neighborhood(source,cap,local,error)||BRepAdaptor_Surface(local.target).GetType()!=GeomAbs_Plane)
            return unsupported("Boss height requires a current planar cap of one valid solid.");
        TopoDS_Face mantle;
        for(const auto& neighbor:local.neighbors) if(BRepAdaptor_Surface(neighbor).GetType()==GeomAbs_Cylinder) {
            if(!mantle.IsNull()&&!BRepReconstruction::sameAnalyticSupport(mantle,neighbor))
                return unsupported("Selected cap meets multiple cylindrical supports; a joint height reconstruction is not certified.");
            mantle=neighbor;
        }
        if(mantle.IsNull()) return unsupported("Selected plane has no adjacent cylindrical boss wall.");
        TopoDS_Face original;for(const auto& mapping:local.history) if(mapping.modified.front().IsSame(mantle)) original=mapping.original;
        gp_Ax1 attachment;const BossRadiusEdit edit{std::nullopt,&attachment};
        const auto result=moveBoundaryBoss(source,original,gp_Vec(),BossBoundaryMode::Extend,
            unsupported("The current cylindrical boss support cannot certify a height reference."),nullptr,&edit);
        if(!result) return result;
        const BRepAdaptor_Surface surface(local.target);auto normal=surface.Plane().Axis().Direction();if(local.target.Orientation()==TopAbs_REVERSED) normal.Reverse();
        const auto top=attachment.Location().Translated(gp_Vec(attachment.Direction())*result.height);
        if(!normal.IsEqual(attachment.Direction(),angular)||surface.Plane().Distance(top)>1.e-6)
            return unsupported("Select the exterior boss cap opposite its attachment; support and underside planes are not height handles.");
        const auto tool=BRepPrimAPI_MakeCylinder(gp_Ax2(attachment.Location(),attachment.Direction()),result.radius,result.height).Shape();
        for(const auto& patch:local.patch) if(std::abs(faceArea(BooleanGeometry::combine<BRepAlgoAPI_Common>(patch,tool))-faceArea(patch))>1.e-8*std::max(1.,faceArea(patch)))
            return unsupported("Selected cap includes unrelated planar material outside the cylindrical feature.");
        auto reference=result;reference.axis=attachment;return reference;
    } catch(const Standard_Failure&) {return unsupported("The current cap/wall adjacency could not certify a boss height reference.");}
      catch(const std::exception&) {return unsupported("Boss height material analysis failed; source geometry was kept.");}
}

CylindricalMoveGeometry::Result CylindricalMoveGeometry::resizeBossHeight(
        const TopoDS_Shape& source,const TopoDS_Face& cap,double height) {
    if(!std::isfinite(height)||height<=.01||height>1.e7) return refuse(Status::InvalidInput,Code::InvalidDisplacement,"Boss height must be finite and greater than 0.01 mm within the geometry limit.");
    const auto reference=bossHeightReference(source,cap);if(!reference) return reference;
    if(std::abs(height-reference.height)<=10*Precision::Confusion()) {auto result=reference;result.shape=source;return result;}
    BRepReconstruction::Result rebuilt;QString error;
    if(!BRepReconstruction::movePlanarLocal(source,cap,height-reference.height,rebuilt,error)||!manifold(rebuilt.solid)) {
        auto failure=refuse(Status::Unsupported,Code::InvalidResult,"Boss height cap sweep cannot preserve the surrounding material: ");failure.message+=error.toStdString();return failure;
    }
    auto result=reference;result.shape=rebuilt.solid;result.height=height;result.code=Code::Moved;
    result.message="Boss height reconstructed by sweeping its current cap along the cylindrical axis.";return result;
}

CylindricalMoveGeometry::Result CylindricalMoveGeometry::moveBoss(
        const TopoDS_Shape& source,const TopoDS_Face& selected,const gp_Vec& delta,BossBoundaryMode policy) {
    const auto result=moveSelected(source,selected,delta,policy);
    if(result) return result;
    const auto local=moveLocalBoss(source,selected,delta,policy,result);
    return local?local:moveBoundaryBoss(source,selected,delta,policy,local);
}

CylindricalMoveGeometry::Result CylindricalMoveGeometry::movePlanarPreservingExterior(
        const TopoDS_Shape& source,const TopoDS_Face& plane,double distance) {
    const auto clipped=moveClippedBossPlane(source,plane,distance);
    if(clipped||clipped.status==Status::Failure) return clipped;
    const auto refused=refuse(Status::Unsupported,Code::UnsupportedStock,"No certified unaffected stock slab with exterior features was found.");
    for(TopExp_Explorer it(source,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());
        if(BRepAdaptor_Surface(face).GetType()!=GeomAbs_Cylinder) continue;
        const auto moved=moveWithExterior(source,face,gp_Vec(0,0,0),refused,plane,distance);
        if(moved) return moved;
    }
    return refused;
}
