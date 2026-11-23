// F2.7A/B/C: isolated, read-only shadow experiment. Never used by production.
#include "MoveFaceGeometry.h"
#include "BoxShadowValidator.h"
#include "BRepCertification.h"
#include "BRepTiming.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepFeat_SplitShape.hxx>
#include <BRepTools_ReShape.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <Precision.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Geom2d_Line.hxx>
#include <Geom_Line.hxx>
#include <iomanip>
#include <limits>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shell.hxx>
#include <Standard_Failure.hxx>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace C = BRepCertification;
namespace {
using namespace ExperimentalBoxShadow;
struct Context {
    TopoDS_Shape source;TopoDS_Face selection;double delta;std::string binding;
    std::shared_ptr<const int> identity=std::make_shared<const int>(0);
    explicit Context(double d):source(BRepPrimAPI_MakeBox(40,30,20).Shape()),delta(d) {
        Box b;require(inspect(source,b).verdict==Verdict::Certified,"source eligibility");selection=b.top;binding=fingerprint(source);
    }
};
struct Attempt { TopoDS_Shape candidate;std::shared_ptr<const int> identity;TopoDS_Face selection;double delta;bool completed; };
Attempt execute(const Context& c) {
    BRepBuilderAPI_Copy isolated(c.source,true,false);auto face=TopoDS::Face(isolated.ModifiedShape(c.selection));
    face.Orientation(c.selection.Orientation());QString error;
    auto candidate=MoveFaceGeometry::move(isolated.Shape(),face,c.delta,error,MoveFaceGeometry::ReconstructionPolicy::AxialInvariantCap);
    require(!candidate.IsNull()&&error.isEmpty(),"production reconstruction failed: "+error.toStdString());
    return {candidate,c.identity,c.selection,c.delta,true};
}
Result binding(const Context& c,const Attempt& a) {
    if(!a.completed||a.candidate.IsNull()) return no("operation failed / null result");
    if(a.identity!=c.identity||!a.selection.IsEqual(c.selection)||a.delta!=c.delta||fingerprint(c.source)!=c.binding)
        return unsure("source/selection/parameter identity mismatch");
    if(!std::isfinite(c.delta)||20+c.delta<=0) return no("invalid height");return yes();
}
Result shadow(const Context& c,const Attempt& a) {
    auto r=binding(c,a);if(r.verdict!=Verdict::Certified) return r;
    Box source;r=inspect(c.source,source);if(r.verdict!=Verdict::Certified) return r;
    if(!source.top.IsEqual(c.selection)) return no("selected Face is not top");
    return ExperimentalBoxShadow::candidate(source,a.candidate,c.delta);
}
// Public Move regenerates; it is not a validator for arbitrary injected shapes.
// Independently regenerated full result is the intended-material oracle here.
Result referenceChecks(const Context& c,const Attempt& a,const TopoDS_Shape& ref) {
    auto r=binding(c,a);if(r.verdict!=Verdict::Certified) return r;
    double volume=0;
    if(!C::withinTolerance(a.candidate,1.e-6)||!C::singleSolid(a.candidate,volume)||!C::manifold(a.candidate)) return no("reference candidate safety");
    BRepClass3d_SolidClassifier classifier(a.candidate);classifier.PerformInfinitePoint(Precision::Confusion());
    if(classifier.State()!=TopAbs_OUT) return no("reference orientation");
    if(!C::equivalent(a.candidate,ref)) return no("candidate differs from full production material");
    auto region=BRepPrimAPI_MakePrism(c.selection,gp_Vec(0,0,c.delta),true,true).Shape();
    C::MaterialDifference difference;QString error;
    if(!C::difference(c.source,a.candidate,difference,error)) return no("directional difference Boolean failed");
    auto changed=c.delta>0?difference.added:difference.removed;
    auto unchanged=c.delta>0?difference.removed:difference.added;
    if(!C::equivalent(changed,region)||std::abs(C::volume(unchanged))>1.e-8*24000) return no("directional material policy");
    if(!C::preservedOutside(c.source,a.candidate,region)) return no("protected region");
    C::Correspondence correspondence;
    if(!C::correspond(ref,a.candidate,correspondence,error)||!correspondence.newFaces.empty()) return no("reference correspondence");
    for(const auto& face:correspondence.faces)
        if(face.relation==C::FaceRelation::Removed||face.relation==C::FaceRelation::Modified) return no("changed reference trim");
    return yes();
}
struct Sample { double ms=0;int booleans=0,volume=0,si=0,repeatedVolume=0;double boundaryMs=0; };
Sample sum(Sample a,Sample b) { return {a.ms+b.ms,a.booleans+b.booleans,a.volume+b.volume,a.si+b.si,a.repeatedVolume+b.repeatedVolume,a.boundaryMs+b.boundaryMs}; }
Sample measure(const std::function<void()>& action) {
    BRepTiming::reset();BRepTiming::enabled=BRepTiming::detailed=true;
    auto begin=std::chrono::steady_clock::now();action();Sample s;
    s.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    BRepTiming::enabled=BRepTiming::detailed=false;std::set<int> measured;
    for(const auto& d:BRepTiming::details) {
        if(d.kind.rfind("Boolean.",0)==0) ++s.booleans;
        if(d.kind=="Measure.Volume") {++s.volume;if(!measured.insert(d.a).second) ++s.repeatedVolume;}
        if(d.kind=="Validate.SelfInterference") ++s.si;
    }
    auto guard=BRepTiming::stats.find("Experimental.BoxParameterBoundary");
    if(guard!=BRepTiming::stats.end()) s.boundaryMs=guard->second.inclusive;
    return s;
}
void print(const std::string& label,const std::vector<Sample>& samples) {
    auto median=[&](auto value) {std::vector<double> v;for(auto s:samples)v.push_back(value(s));std::sort(v.begin(),v.end());return v[v.size()/2];};
    std::cout<<"TIMING "<<label<<" median_ms="<<median([](Sample s){return s.ms;})
      <<" Boolean="<<median([](Sample s){return s.booleans;})<<" volume="<<median([](Sample s){return s.volume;})
      <<" boundary_screen_ms="<<median([](Sample s){return s.boundaryMs;})<<" SI="<<median([](Sample s){return s.si;})<<" repeated_exact_volume="<<median([](Sample s){return s.repeatedVolume;})<<'\n';
}
TopoDS_Shape compound(const TopoDS_Shape& a,const TopoDS_Shape& b) {BRep_Builder builder;TopoDS_Compound result;builder.MakeCompound(result);builder.Add(result,a);builder.Add(result,b);return result;}
std::vector<std::pair<std::string,TopoDS_Shape>> negatives(const Context& c,const TopoDS_Shape& good) {
    const double h=20+c.delta;std::vector<std::pair<std::string,TopoDS_Shape>> cases;
    cases.push_back({"unchanged",copy(c.source)});
    cases.push_back({"wrong-direction",BRepPrimAPI_MakeBox(40,30,20-c.delta).Shape()});
    cases.push_back({"wrong-amount",BRepPrimAPI_MakeBox(40,30,h+1).Shape()});
    cases.push_back({"reversed",good.Reversed()});
    cases.push_back({"extra-solid",compound(copy(good),BRepPrimAPI_MakeBox(gp_Pnt(100,100,100),1,1,1).Shape())});
    BRep_Builder builder;auto missing=copy(good);auto shell=TopoDS::Shell(TopExp_Explorer(missing,TopAbs_SHELL).Current());
    shell.Free(true);builder.Remove(shell,TopExp_Explorer(shell,TopAbs_FACE).Current());cases.push_back({"missing-Face",missing});
    auto wrong=copy(good);auto face=TopoDS::Face(TopExp_Explorer(wrong,TopAbs_FACE).Current());auto wire=BRepTools::OuterWire(face);
    auto edgeMap=map(wrong,TopAbs_EDGE);auto owned=map(face,TopAbs_EDGE);TopoDS_Shape replacement;
    for(int i=1;i<=edgeMap.Extent();++i) if(!owned.Contains(edgeMap(i))) {replacement=edgeMap(i);break;}
    wire.Free(true);builder.Remove(wire,TopExp_Explorer(wire,TopAbs_EDGE).Current());builder.Add(wire,replacement);
    cases.push_back({"incorrect-sidewall-connection",wrong});
    BRepAlgoAPI_Cut cut(copy(good),BRepPrimAPI_MakeBox(gp_Pnt(10,10,5),2,2,2).Shape());
    require(cut.IsDone()&&!cut.HasErrors(),"cavity construction");cases.push_back({"unexpected-cavity",cut.Shape()});
    cases.push_back({"equal-volume-different-material",BRepPrimAPI_MakeBox(gp_Pnt(2,0,0),40,30,h).Shape()});
    auto coarse=copy(good);builder.UpdateVertex(TopoDS::Vertex(TopExp_Explorer(coarse,TopAbs_VERTEX).Current()),2.e-6);
    cases.push_back({"excessive-tolerance",coarse});return cases;
}

// F2.7B fixtures are modifications of deep disposable copies. No normalization,
// repair or BRepTools_ReShape is applied by either validator.
TopoDS_Face cap(const TopoDS_Shape& shape) {
    TopoDS_Face result;double z=-INFINITY;
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        auto f=TopoDS::Face(it.Current());BRepAdaptor_Surface surface(f);
        if(surface.GetType()==GeomAbs_Plane&&std::abs(surface.Plane().Axis().Direction().Z())==1.
           &&surface.Plane().Location().Z()>z) {z=surface.Plane().Location().Z();result=f;}
    }
    require(!result.IsNull(),"fixture top cap missing");return result;
}
TopoDS_Face sidewall(const TopoDS_Shape& shape) {
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        auto f=TopoDS::Face(it.Current());BRepAdaptor_Surface surface(f);
        if(surface.GetType()==GeomAbs_Plane&&surface.Plane().Axis().Direction().X()==1.
           &&surface.Plane().Location().X()==40.) return f;
    }
    throw std::runtime_error("fixture X sidewall missing");
}
TopoDS_Face patch(const gp_Pln& plane,const std::vector<gp_Pnt>& points) {
    BRepBuilderAPI_MakePolygon polygon;for(const auto& p:points) polygon.Add(p);polygon.Close();
    BRepBuilderAPI_MakeFace face(plane,polygon.Wire(),true);require(face.IsDone(),"fixture patch");return face.Face();
}
TopoDS_Shape replaceFace(const TopoDS_Shape& shape,const TopoDS_Face& oldFace,TopoDS_Face replacement) {
    replacement.Orientation(oldFace.Orientation());
    BRepTools_ReShape reshape;reshape.Replace(oldFace,replacement);return reshape.Apply(shape);
}
struct Adversary {
    std::string label;TopoDS_Shape candidate;
    Verdict expectedShadow;bool correctCandidate=false;bool pcurve=false;
};
std::vector<Adversary> adversaries(const TopoDS_Shape& good,double delta) {
    std::vector<Adversary> cases;BRep_Builder builder;const double h=20+delta;
    for(int kind=0;kind<5;++kind) {
        auto shape=copy(good);auto face=cap(shape);auto edge=TopoDS::Edge(TopExp_Explorer(face,TopAbs_EDGE).Current());
        double first,last;auto pc=Handle(Geom2d_Line)::DownCast(BRep_Tool::CurveOnSurface(edge,face,first,last));
        require(!pc.IsNull(),"fixture affine pcurve missing");
        if(kind==0) {builder.Range(edge,face,first+1,last-1);builder.SameRange(edge,false);builder.SameParameter(edge,false);}
        if(kind==1) {auto direction=pc->Direction();direction.Reverse();
            builder.UpdateEdge(edge,new Geom2d_Line(pc->Value(first+last),direction),face,BRep_Tool::Tolerance(edge));}
        if(kind==2||kind==4) {auto moved=Handle(Geom2d_Line)::DownCast(pc->Copy());
            moved->Translate(kind==2?gp_Vec2d(1,0):gp_Vec2d(0,0.01));
            builder.UpdateEdge(edge,moved,face,BRep_Tool::Tolerance(edge));}
        if(kind==3) builder.UpdateEdge(edge,Handle(Geom2d_Curve){},face,BRep_Tool::Tolerance(edge));
        const char* labels[]={"pcurve-domain","pcurve-reversed","pcurve-mismatch","pcurve-removed-planar","pcurve-placement"};
        // Removing a stored planar pcurve is legal when OCCT supplies its correct
        // analytic projection. Verify the effective pcurve below, not its absence.
        cases.push_back({labels[kind],shape,kind==3?Verdict::Certified:Verdict::Rejected,kind==3,true});
    }
    auto makeTop=[&](std::string label,const std::vector<gp_Pnt>& points) {
        auto shape=copy(good);auto f=cap(shape);
        cases.push_back({std::move(label),replaceFace(shape,f,patch(BRepAdaptor_Surface(f).Plane(),points)),Verdict::Rejected});
    };
    makeTop("same-plane-triangle",{{0,0,h},{40,0,h},{20,30,h}});
    makeTop("wrong-cap-dimensions",{{0,0,h},{39,0,h},{39,30,h},{0,30,h}});
    makeTop("shifted-cap-trim",{{1,0,h},{41,0,h},{41,30,h},{1,30,h}});
    makeTop("equal-area-different-cap",{{0,0,h},{60,0,h},{60,20,h},{0,20,h}});
    auto wrong=copy(good);auto f=sidewall(wrong);
    cases.push_back({"incorrect-sidewall-trim",replaceFace(wrong,f,patch(BRepAdaptor_Surface(f).Plane(),
       {{40,0,0},{40,30,0},{40,30,h-1},{40,0,h-1}})),Verdict::Rejected});
    auto extra=copy(good);auto shell=TopoDS::Shell(TopExp_Explorer(extra,TopAbs_SHELL).Current());shell.Free(true);
    builder.Add(shell,patch(gp_Pln(gp_Pnt(0,0,h),gp_Dir(0,0,1)),{{10,10,h},{12,10,h},{12,12,h},{10,12,h}}));
    cases.push_back({"additional-bounded-Face",extra,Verdict::Rejected});
    cases.push_back({"valid-equal-volume-wrong-dimensions",BRepPrimAPI_MakeBox(60,20,h).Shape(),Verdict::Rejected});
    auto split=copy(good);BRepFeat_SplitShape splitter(split);
    splitter.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(20,0,h),gp_Pnt(20,30,h)).Edge(),cap(split));splitter.Build();
    require(splitter.IsDone(),"split Face fixture");
    cases.push_back({"valid-split-planar-Face",splitter.Shape(),Verdict::Unresolved,true});
    auto divided=copy(good);auto e=TopoDS::Edge(TopExp_Explorer(divided,TopAbs_EDGE).Current());BRepAdaptor_Curve curve(e);
    auto half=BRepBuilderAPI_MakeEdge(curve.Value(curve.FirstParameter()),curve.Value((curve.FirstParameter()+curve.LastParameter())/2)).Edge();
    BRepFeat_SplitShape edgeSplitter(divided);edgeSplitter.Add(half,e);edgeSplitter.Build();
    require(edgeSplitter.IsDone(),"split collinear Edge fixture");
    cases.push_back({"valid-split-collinear-Edge",edgeSplitter.Shape(),Verdict::Unresolved,true});
    auto cloned=copy(good);require(!cloned.IsSame(good),"copy retained source topology identity");
    require(!map(cloned,TopAbs_EDGE)(1).IsSame(map(good,TopAbs_EDGE)(1)),"copied Edge identity did not change");
    cases.push_back({"valid-copied-topology-identities",cloned,Verdict::Certified,true});return cases;
}
void pcurveDiagnostic(const TopoDS_Shape& shape) {
    auto face=cap(shape);auto edge=TopoDS::Edge(TopExp_Explorer(face,TopAbs_EDGE).Current());
    double first,last;Standard_Boolean stored=false;
    auto pc=BRep_Tool::CurveOnSurface(edge,face,first,last,&stored);BRepAdaptor_Curve curve(edge);BRepAdaptor_Surface surface(face);
    std::cout<<" PC stored="<<stored<<" effective="<<!pc.IsNull()<<" domain="<<first<<','<<last
       <<" edge-domain="<<curve.FirstParameter()<<','<<curve.LastParameter()<<" SameRange="<<BRep_Tool::SameRange(edge)
       <<" SameParameter="<<BRep_Tool::SameParameter(edge);
    if(!pc.IsNull()) {double residual=0;for(double t:{first,last}) {auto uv=pc->Value(t);
        residual=std::max(residual,surface.Value(uv.X(),uv.Y()).Distance(curve.Value(t)));}
        std::cout<<" affine="<<!Handle(Geom2d_Line)::DownCast(pc).IsNull()<<" endpoint_residual="<<residual;}
    std::cout<<'\n';
}
Result safely(const std::function<Result()>& action) {
    try {return action();}catch(const Standard_Failure& e) {return unsure(std::string("OCCT cannot conclude: ")+e.GetMessageString());}
    catch(const std::exception& e) {return unsure(std::string("reference/evaluation cannot conclude: ")+e.what());}
}
struct AdversarialTotals {int cases=0,disagreements=0,falseRefusals=0,referenceFalseAcceptances=0;};
void assess(const Context& context,const Attempt& good,const Adversary& fixture,AdversarialTotals& totals) {
    auto attempt=good;attempt.candidate=fixture.candidate;const auto before=fingerprint(attempt.candidate);
    const auto refSourceBefore=fingerprint(context.source);
    const bool valid=C::validBRep(attempt.candidate);Result s,r;
    auto sc=measure([&]{s=safely([&]{return shadow(context,attempt);});});
    auto rc=measure([&]{r=safely([&]{auto ref=execute(context).candidate;return referenceChecks(context,attempt,ref);});});
    std::cout<<"ADVERSARIAL delta="<<context.delta<<' '<<fixture.label<<" actual_F/E/V="<<map(attempt.candidate,TopAbs_FACE).Extent()
      <<'/'<<map(attempt.candidate,TopAbs_EDGE).Extent()<<'/'<<map(attempt.candidate,TopAbs_VERTEX).Extent()
      <<" root_location_identity="<<attempt.candidate.Location().IsIdentity()<<" OCCT_valid="<<valid
      <<" expected_correct_candidate="<<fixture.correctCandidate<<" shadow="<<name(s.verdict)<<" ("<<s.reason<<") reference="<<name(r.verdict)
      <<" ("<<r.reason<<") shadow_ms="<<sc.ms<<" full_reference_ms="<<rc.ms<<'\n';
    if(fixture.pcurve)pcurveDiagnostic(attempt.candidate);
    require(fingerprint(attempt.candidate)==before&&fingerprint(context.source)==refSourceBefore,"adversarial evaluation mutated geometry");
    require(sc.booleans==0,"shadow unexpectedly invoked a material Boolean");
    ++totals.cases;if(s.verdict!=r.verdict)++totals.disagreements;
    if(!fixture.correctCandidate) {
        require(s.verdict!=Verdict::Certified,"false shadow acceptance: "+fixture.label);
        if(r.verdict==Verdict::Certified)++totals.referenceFalseAcceptances;
    } else {
        require(valid,"supposedly valid representation is not OCCT-valid: "+fixture.label);
        if(s.verdict!=Verdict::Certified) ++totals.falseRefusals;
    }
    require(s.verdict==fixture.expectedShadow,"unexpected shadow classification: "+fixture.label+": "+s.reason);
}
// Only rotations around Z: reference still uses the same axial +Z cap sweep.
// This adds fixture placements, not a general placement-aware shadow validator.
Context placedContext(const Context& base,const gp_Trsf& transform,bool bake) {
    Context result=base;result.identity=std::make_shared<const int>(0);
    if(bake) {BRepBuilderAPI_Transform moved(base.source,transform,true);result.source=moved.Shape();
        result.selection=TopoDS::Face(moved.ModifiedShape(base.selection));result.selection.Orientation(base.selection.Orientation());}
    else {auto location=TopLoc_Location(transform);result.source=base.source.Moved(location);
        result.selection=TopoDS::Face(base.selection.Moved(location));}
    result.binding=fingerprint(result.source);return result;
}
void placedCases(const Context& base,const Attempt& good,AdversarialTotals& totals) {
    gp_Trsf translation;translation.SetTranslation(gp_Vec(7,11,3));
    gp_Trsf rightAngle;rightAngle.SetValues(0,-1,0,7, 1,0,0,11, 0,0,1,3);
    gp_Trsf general;general.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1)),std::acos(-1.)/6.);
    struct Placement {const char* label;gp_Trsf transform;bool bake;Verdict expected;};
    for(const auto& placement:std::vector<Placement>{{"translated-location",translation,false,Verdict::Unresolved},
        {"translated-baked",translation,true,Verdict::Certified},
        {"exact-90-location",rightAngle,false,Verdict::Unresolved},{"exact-90-baked",rightAngle,true,Verdict::Certified},
        {"30-degree-baked",general,true,Verdict::Unresolved}}) {
        auto context=placedContext(base,placement.transform,placement.bake);auto attempt=good;
        attempt.identity=context.identity;attempt.selection=context.selection;
        TopoDS_Shape candidate=placement.bake?BRepBuilderAPI_Transform(good.candidate,placement.transform,true).Shape():
            good.candidate.Moved(TopLoc_Location(placement.transform));
        assess(context,attempt,{placement.label,candidate,placement.expected,true},totals);
    }
}

// F2.7C fixture metadata is an independent test oracle, never a validator input.
struct ToleranceCase {
    std::string label;TopoDS_Shape candidate;
    double requested=0,actual=0,tolerance=0;
    bool mustRefuse=false,boundary=false,pcurve=false;
};
std::vector<ToleranceCase> toleranceCases(const TopoDS_Shape& good) {
    std::vector<ToleranceCase> cases;BRep_Builder builder;
    auto top=cap(good);auto originalEdge=TopoDS::Edge(TopExp_Explorer(top,TopAbs_EDGE).Current());
    const double tolerance=BRep_Tool::Tolerance(originalEdge);
    require(tolerance==1.e-7,"unexpected fixture edge tolerance");
    struct Residual {const char* label;double value;bool boundary;};
    const std::vector<Residual> residuals={{"below",tolerance/2,false},
       {"boundary-down",std::nextafter(tolerance,0.),true},{"boundary",tolerance,true},
       {"boundary-up",std::nextafter(tolerance,INFINITY),true},{"above",2*tolerance,false}};
    auto shiftedPcurve=[&](double residual,double edgeTolerance,bool sameRange,bool sameParameter) {
        auto shape=copy(good);auto face=cap(shape);auto edge=TopoDS::Edge(TopExp_Explorer(face,TopAbs_EDGE).Current());
        double first,last;auto pc=Handle(Geom2d_Line)::DownCast(BRep_Tool::CurveOnSurface(edge,face,first,last));
        require(!pc.IsNull()&&pc->Location().Y()==0.&&pc->Direction().X()==0.&&std::abs(pc->Direction().Y())==1.,"fixture pcurve oracle axes");
        // UV Y origin is exactly zero: the stored residual is the requested
        // binary double, not a rounded subtraction at a large coordinate.
        auto shifted=Handle(Geom2d_Line)::DownCast(pc->Copy());shifted->Translate(gp_Vec2d(0,residual));
        require(shifted->Location().Y()==residual,"stored residual changed");
        builder.UpdateEdge(edge,shifted,face,edgeTolerance);builder.SameRange(edge,sameRange);builder.SameParameter(edge,sameParameter);
        return shape;
    };
    for(const auto& residual:residuals) {
        const double r=residual.value;
        auto shifted=shiftedPcurve(r,tolerance,true,true);
        cases.push_back({std::string("pcurve-")+residual.label,shifted,r,r,tolerance,r>tolerance&&!residual.boundary,residual.boundary,true});
        auto trimmed=copy(good);auto face=cap(trimmed);auto edge=TopoDS::Edge(TopExp_Explorer(face,TopAbs_EDGE).Current());
        BRepAdaptor_Curve curve(edge);require(curve.FirstParameter()==0.,"fixture trim starts at zero");
        builder.Range(edge,r,curve.LastParameter(),true); // pcurve range is deliberately unchanged, flags remain true
        cases.push_back({std::string("edge-trim-")+residual.label,trimmed,r,r,tolerance,true,residual.boundary,true});
        auto vertexShift=copy(good);face=cap(vertexShift);edge=TopoDS::Edge(TopExp_Explorer(face,TopAbs_EDGE).Current());
        TopoDS_Vertex a,b;TopExp::Vertices(edge,a,b);auto p=BRep_Tool::Pnt(a);
        require(p.Y()==0.,"fixture vertex oracle Y origin");p.SetY(r);builder.UpdateVertex(a,p,BRep_Tool::Tolerance(a));
        cases.push_back({std::string("vertex-to-curve-")+residual.label,vertexShift,r,r,tolerance,true,residual.boundary});
        // Disconnect one endpoint only in the selected Face. Proximity must not
        // substitute for the required actual shared vertex/edge identities.
        auto gap=copy(good);face=cap(gap);edge=TopoDS::Edge(TopExp_Explorer(face,TopAbs_EDGE).Current());
        TopExp::Vertices(edge,a,b);TopoDS_Vertex v;builder.MakeVertex(v,BRep_Tool::Pnt(a).Translated(gp_Vec(0,r,0)),BRep_Tool::Tolerance(a));
        BRepTools_ReShape endpoint;endpoint.Replace(a,v);auto disconnectedEdge=endpoint.Apply(edge);
        BRepTools_ReShape inFace;inFace.Replace(edge,disconnectedEdge);auto disconnectedFace=inFace.Apply(face);
        BRepTools_ReShape inShape;inShape.Replace(face,disconnectedFace);
        cases.push_back({std::string("face-cycle-gap-")+residual.label,inShape.Apply(gap),r,r,tolerance,true,residual.boundary});
        auto detached=copy(good);face=cap(detached);gp_Trsf move;move.SetTranslation(gp_Vec(r,0,0));
        BRepBuilderAPI_Transform movedFace(face,move,true);auto detachedFace=TopoDS::Face(movedFace.Shape());double actualGap=0;
        for(TopExp_Explorer v(face,TopAbs_VERTEX);v.More();v.Next())
            actualGap=std::max(actualGap,BRep_Tool::Pnt(TopoDS::Vertex(v.Current())).Distance(BRep_Tool::Pnt(TopoDS::Vertex(movedFace.ModifiedShape(v.Current())))));
        cases.push_back({std::string("adjacent-Face-gap-")+residual.label,replaceFace(detached,face,detachedFace),r,actualGap,tolerance,true,residual.boundary});
    }
    for(double r:{0.,tolerance/2,2*tolerance}) for(bool range:{false,true}) for(bool parameter:{false,true}) {
        std::string label="flags-r="+std::to_string(int(r/tolerance*2))+"-SR="+std::to_string(range)+"-SP="+std::to_string(parameter);
        cases.push_back({label,shiftedPcurve(r,tolerance,range,parameter),r,r,tolerance,r>tolerance,false,true});
    }
    auto mismatched=copy(good);auto face=cap(mismatched);auto edge=TopoDS::Edge(TopExp_Explorer(face,TopAbs_EDGE).Current());
    double first,last;auto pc=BRep_Tool::CurveOnSurface(edge,face,first,last);
    builder.Range(edge,face,first+1,last+1);builder.SameRange(edge,true);builder.SameParameter(edge,true);
    cases.push_back({"mismatched-domains-both-flags-true",mismatched,1,1,tolerance,true,false,true});
    // Different *valid* range shared by all representations, with vertex
    // parameters updated. Same exact 3D boundary, not just equal edge length.
    auto reparam=copy(good);face=cap(reparam);edge=TopoDS::Edge(TopExp_Explorer(face,TopAbs_EDGE).Current());
    BRepAdaptor_Curve curve(edge);auto line=curve.Line();const double shift=8;
    line.SetLocation(line.Location().Translated(-gp_Vec(line.Direction())*shift));
    builder.UpdateEdge(edge,new Geom_Line(line),BRep_Tool::Tolerance(edge));
    for(TopExp_Explorer f(reparam,TopAbs_FACE);f.More();f.Next()) {
        auto owner=TopoDS::Face(f.Current());if(!map(owner,TopAbs_EDGE).Contains(edge)) continue;
        auto c=Handle(Geom2d_Line)::DownCast(BRep_Tool::CurveOnSurface(edge,owner,first,last));
        auto changed=Handle(Geom2d_Line)::DownCast(c->Copy());changed->Translate(-gp_Vec2d(c->Direction())*shift);
        builder.UpdateEdge(edge,changed,owner,BRep_Tool::Tolerance(edge));
    }
    builder.Range(edge,curve.FirstParameter()+shift,curve.LastParameter()+shift);
    TopoDS_Vertex a,b;auto forward=edge;forward.Orientation(TopAbs_FORWARD);TopExp::Vertices(forward,a,b);
    builder.UpdateVertex(a,curve.FirstParameter()+shift,edge,BRep_Tool::Tolerance(a));
    builder.UpdateVertex(b,curve.LastParameter()+shift,edge,BRep_Tool::Tolerance(b));
    cases.push_back({"valid-shared-reparameterization",reparam,0,0,tolerance,false,false,true});
    auto independent=copy(good);face=cap(independent);edge=TopoDS::Edge(TopExp_Explorer(face,TopAbs_EDGE).Current());
    auto c=Handle(Geom2d_Line)::DownCast(BRep_Tool::CurveOnSurface(edge,face,first,last));
    auto changed=Handle(Geom2d_Line)::DownCast(c->Copy());changed->Translate(-gp_Vec2d(c->Direction())*shift);
    builder.UpdateEdge(edge,changed,face,BRep_Tool::Tolerance(edge));builder.Range(edge,face,first+shift,last+shift);
    builder.SameRange(edge,false);builder.SameParameter(edge,false);
    cases.push_back({"equivalent-independent-pcurve-domain",independent,0,0,tolerance,false,false,true});
    auto reversed=copy(good);face=cap(reversed);edge=TopoDS::Edge(TopExp_Explorer(face,TopAbs_EDGE).Current());
    auto wire=BRepTools::OuterWire(face);wire.Free(true);builder.Remove(wire,edge);builder.Add(wire,edge.Reversed());
    cases.push_back({"inconsistent-Edge-use-orientation",reversed,0,0,tolerance,true});
    cases.push_back({"elevated-tolerance-hides-PC",shiftedPcurve(5*tolerance,1.e-6,true,true),5*tolerance,5*tolerance,1.e-6,false,false,true});
    cases.push_back({"apparently-valid-wrong-displacement",BRepPrimAPI_MakeBox(40,30,35+tolerance/2).Shape(),tolerance/2,(35+tolerance/2)-35,tolerance,true});
    return cases;
}

Verdict expectedToleranceShadow(const ToleranceCase& fixture) {
    const auto& label=fixture.label;
    if(label.rfind("pcurve-boundary",0)==0) return Verdict::Unresolved;
    if(label.rfind("vertex-to-curve-",0)==0&&label!="vertex-to-curve-above") return Verdict::Unresolved;
    if(label=="pcurve-below"||label=="valid-shared-reparameterization"||label=="elevated-tolerance-hides-PC"
       ||label=="flags-r=0-SR=1-SP=1"||label=="flags-r=1-SR=1-SP=1") return Verdict::Certified;
    return Verdict::Rejected;
}
struct ToleranceTotals {int cases=0,disagreements=0,unsafe=0,boundaryAccepted=0;};
void toleranceTests(const Context& context,const Attempt& good,ToleranceTotals& totals) {
    for(const auto& fixture:toleranceCases(good.candidate)) {
        auto attempt=good;attempt.candidate=fixture.candidate;const auto before=fingerprint(attempt.candidate);
        Result s,r;const bool valid=C::validBRep(attempt.candidate);
        auto sc=measure([&]{s=safely([&]{return shadow(context,attempt);});});
        auto rc=measure([&]{r=safely([&]{auto reference=execute(context).candidate;return referenceChecks(context,attempt,reference);});});
        std::cout<<std::setprecision(17)<<"TOLERANCE "<<fixture.label<<" requested="<<fixture.requested<<" actual_oracle="<<fixture.actual
          <<" permitted="<<fixture.tolerance<<" boundary="<<fixture.boundary<<" OCCT_valid="<<valid
          <<" shadow="<<name(s.verdict)<<" ("<<s.reason<<") reference="<<name(r.verdict)<<" ("<<r.reason<<") shadow_ms="<<sc.ms<<" reference_ms="<<rc.ms<<'\n';
        if(fixture.pcurve)pcurveDiagnostic(attempt.candidate);
        if(s.verdict==Verdict::Certified&&fixture.mustRefuse)++totals.unsafe;
        if(s.verdict==Verdict::Certified&&fixture.boundary)++totals.boundaryAccepted;
        if(s.verdict!=r.verdict)++totals.disagreements;++totals.cases;
        require(s.verdict==expectedToleranceShadow(fixture),"unexpected tolerance outcome: "+fixture.label+": "+s.reason);
        require(fingerprint(attempt.candidate)==before&&fingerprint(context.source)==context.binding,"tolerance test modified geometry");
        require(sc.booleans==0,"tolerance shadow invoked material Boolean");
    }
}
}
int main() {
    try {
        int tested=0,disagreements=0;AdversarialTotals adversarial;ToleranceTotals tolerances;
        for(double delta:{15.,-5.}) {
            Context context(delta);Attempt good;std::vector<Sample> generation,shadows,regeneration,comparisons,referenceTotals,validationDifferences;
            for(int trial=0;trial<3;++trial) {
                generation.push_back(measure([&]{good=execute(context);}));const auto before=fingerprint(good.candidate);
                Result s,r;TopoDS_Shape ref;
                shadows.push_back(measure([&]{s=shadow(context,good);}));
                regeneration.push_back(measure([&]{ref=execute(context).candidate;}));
                const auto refBefore=fingerprint(ref);
                comparisons.push_back(measure([&]{r=referenceChecks(context,good,ref);}));
                referenceTotals.push_back(sum(regeneration.back(),comparisons.back()));
                validationDifferences.push_back({comparisons.back().ms-shadows.back().ms});
                require(fingerprint(ref)==refBefore,"reference mutation");
                require(s.verdict==Verdict::Certified,"positive shadow: "+s.reason);
                require(r.verdict==Verdict::Certified,"positive reference: "+r.reason);
                require(fingerprint(context.source)==context.binding&&fingerprint(good.candidate)==before,"positive source/candidate mutation");
                require(std::abs(C::volume(good.candidate)-40*30*(20+delta))<1.e-7,"independent positive volume");
                ++tested;
            }
            std::string label=std::to_string(int(delta));print(label+" production-generation-full",generation);print(label+" shadow",shadows);
            print(label+" full-reference-regeneration",regeneration);print(label+" reference-comparison",comparisons);
            print(label+" total-full-reference",referenceTotals);print(label+" incremental-comparison-minus-shadow",validationDifferences);
            for(const auto& [label,candidate]:negatives(context,good.candidate)) {
                Attempt bad=good;bad.candidate=candidate;const auto before=fingerprint(candidate);Result s,r;
                auto sCost=measure([&]{s=shadow(context,bad);});TopoDS_Shape ref;
                auto regenerate=measure([&]{ref=execute(context).candidate;});
                auto rCost=measure([&]{r=referenceChecks(context,bad,ref);});
                std::cout<<"CASE delta="<<delta<<' '<<label<<" shadow="<<name(s.verdict)<<" ("<<s.reason<<") reference="<<name(r.verdict)<<" ("<<r.reason<<") shadow_ms="<<sCost.ms<<" reference_ms="<<regenerate.ms+rCost.ms<<'\n';
                if(s.verdict!=r.verdict) ++disagreements;
                require(s.verdict!=Verdict::Certified&&r.verdict!=Verdict::Certified,"false acceptance: "+label);
                require(fingerprint(context.source)==context.binding&&fingerprint(candidate)==before,"negative source/candidate mutation: "+label);++tested;
            }
            for(int tamper=0;tamper<5;++tamper) {
                auto bad=good;
                if(tamper==0)bad.completed=false;
                if(tamper==1)bad.identity=std::make_shared<const int>(0);
                if(tamper==2)bad.selection=TopoDS::Face(context.selection.Reversed());
                if(tamper==3)bad.delta+=1;
                if(tamper==4)bad.candidate.Nullify();
                auto ref=execute(context).candidate;
                auto s=shadow(context,bad),r=referenceChecks(context,bad,ref);
                std::cout<<"BINDING delta="<<delta<<" tamper="<<tamper<<" shadow="<<name(s.verdict)<<" reference="<<name(r.verdict)<<" reason="<<s.reason<<'\n';
                if(s.verdict!=r.verdict)++disagreements;
                require(s.verdict!=Verdict::Certified&&r.verdict!=Verdict::Certified,"binding/failure accepted");++tested;
            }
            for(const auto& fixture:adversaries(good.candidate,delta)) assess(context,good,fixture,adversarial);
            if(delta==15.) {placedCases(context,good,adversarial);toleranceTests(context,good,tolerances);}
            Context changed=context;changed.source=BRepPrimAPI_MakeBox(40,31,20).Shape();
            auto ref=execute(context).candidate;auto s=shadow(changed,good),r=referenceChecks(changed,good,ref);
            require(s.verdict!=Verdict::Certified&&r.verdict!=Verdict::Certified,"edited source accepted stale binding");
            std::cout<<"BINDING edited-source shadow="<<name(s.verdict)<<" reference="<<name(r.verdict)<<'\n';++tested;
            require(fingerprint(context.source)==context.binding,"final source mutation");
        }
        std::cout<<"ADVERSARIAL SUMMARY cases="<<adversarial.cases<<" verdict_disagreements="<<adversarial.disagreements
           <<" conservative_false_refusals="<<adversarial.falseRefusals<<" shadow_false_acceptances=0 reference_incorrect_acceptances="<<adversarial.referenceFalseAcceptances<<'\n';
        std::cout<<"TOLERANCE SUMMARY cases="<<tolerances.cases<<" disagreements="<<tolerances.disagreements<<" unsafe_acceptances="<<tolerances.unsafe<<" boundary_acceptances="<<tolerances.boundaryAccepted<<'\n';
        require(tolerances.unsafe==0&&tolerances.boundaryAccepted==0,"unsafe or numerically ambiguous tolerance acceptance");
        std::cout<<"BASE PASS cases="<<tested<<" verdict_disagreements="<<disagreements<<'\n';
        std::cout<<"PASS total_cases="<<tested+adversarial.cases+tolerances.cases<<" shadow_false_acceptances=0; no production receipt or acceptance modified\n";
        return 0;
    } catch(const Standard_Failure& e) {std::cerr<<"OCCT failure: "<<e.GetMessageString()<<'\n';}
      catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';}
    return 1;
}
