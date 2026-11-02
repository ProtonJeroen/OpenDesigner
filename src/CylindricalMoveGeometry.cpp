#include "CylindricalMoveGeometry.h"
#include "BooleanGeometry.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
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
#include <optional>
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
}

CylindricalMoveGeometry::Result CylindricalMoveGeometry::moveThroughHole(
        const TopoDS_Shape& source,const TopoDS_Face& selected,const gp_Vec& delta) {
    return moveSelected(source,selected,delta,std::nullopt);
}

CylindricalMoveGeometry::Result CylindricalMoveGeometry::moveBoss(
        const TopoDS_Shape& source,const TopoDS_Face& selected,const gp_Vec& delta,BossBoundaryMode policy) {
    return moveSelected(source,selected,delta,policy);
}
