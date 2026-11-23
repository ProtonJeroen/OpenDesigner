#include "BoxShadowObservation.h"
#include "BoxShadowValidator.h"
#include <cstdlib>
#include <iostream>
#include <iomanip>
namespace BoxShadowObservation {
namespace E=ExperimentalBoxShadow;
namespace {
thread_local Record latest;
thread_local unsigned completed=0;
bool enabled() {const char* value=std::getenv("OPENDESIGNER_BOX_SHADOW");return value&&std::string(value)=="1";}
// Only a cheap structural filter, not source validity or acceptance evidence.
bool prefilter(const TopoDS_Shape& source,const TopoDS_Face& selected) {
    if(source.IsNull()||selected.IsNull()||!source.Location().IsIdentity())return false;
    auto faces=E::map(source,TopAbs_FACE),edges=E::map(source,TopAbs_EDGE);
    if(faces.Extent()!=6||edges.Extent()!=12||E::map(source,TopAbs_SOLID).Extent()!=1
       ||E::map(source,TopAbs_SHELL).Extent()!=1||!faces.Contains(selected))return false;
    for(int i=1;i<=faces.Extent();++i) {
        auto face=TopoDS::Face(faces(i));BRepAdaptor_Surface s(face);
        if(!face.Location().IsIdentity()||s.GetType()!=GeomAbs_Plane||E::map(face,TopAbs_WIRE).Extent()!=1)return false;
        const auto n=s.Plane().Axis().Direction();int axes=0;
        for(int j=1;j<=3;++j) {if(std::abs(n.Coord(j))==1.)++axes;else if(n.Coord(j)!=0.)return false;}
        if(axes!=1)return false;
    }
    for(int i=1;i<=edges.Extent();++i) {
        auto edge=TopoDS::Edge(edges(i));if(!edge.Location().IsIdentity()||BRepAdaptor_Curve(edge).GetType()!=GeomAbs_Line)return false;
    }
    // Keep F2.7's top +Z cap scope. No arbitrary Face/placement support added.
    BRepAdaptor_Surface s(selected);auto n=s.Plane().Axis().Direction();
    if(selected.Orientation()==TopAbs_REVERSED)n.Reverse();
    if(n.Z()!=1.)return false;
    double high=-INFINITY;for(int i=1;i<=faces.Extent();++i) {
        BRepAdaptor_Surface a(TopoDS::Face(faces(i)));if(std::abs(a.Plane().Axis().Direction().Z())==1.)high=std::max(high,a.Plane().Location().Z());
    }
    return s.Plane().Location().Z()==high;
}
double elapsed(BRepTiming::Clock::time_point start) {return std::chrono::duration<double,std::milli>(BRepTiming::Clock::now()-start).count();}
}
struct Trace::State {
    bool previousEnabled=false,previousDetailed=false,owner=false;
    BRepTiming::Clock::time_point start;
    std::map<std::string,BRepTiming::Stats> before,production;
    std::vector<BRepTiming::Detail> events;
    Record record;std::string binding;TopoDS_Shape source;TopoDS_Face selected;
    int sourceId=0,candidateId=0;size_t eventBegin=0;
    ~State() noexcept {
        BRepTiming::enabled=previousEnabled;BRepTiming::detailed=previousDetailed;
        if(owner)BRepTiming::reset();
    }
};
Trace::Trace(const TopoDS_Shape& source,const TopoDS_Face& selected,bool operationScope,std::chrono::steady_clock::time_point started) noexcept {
    try {
        if(!operationScope||!enabled()||!prefilter(source,selected))return;
        auto prepared=std::make_unique<State>();
        prepared->previousEnabled=BRepTiming::enabled;prepared->previousDetailed=BRepTiming::detailed;
        prepared->owner=!BRepTiming::enabled&&BRepTiming::stack.empty();
        prepared->start=started;
        if(prepared->owner)BRepTiming::reset();
        prepared->before=BRepTiming::stats;prepared->eventBegin=BRepTiming::details.size();prepared->binding=E::fingerprint(source);
        prepared->source=source;prepared->selected=selected;
        BRepTiming::enabled=true;BRepTiming::detailed=true;
        prepared->sourceId=BRepTiming::shapeId(source);state=std::move(prepared);
    } catch(...) {} // No observer failure may change command behavior.
}
void Trace::observe(const TopoDS_Shape& source,const TopoDS_Face& selected,double delta,
                   const TopoDS_Shape& result,bool sourceBindingCurrent) noexcept {
    if(!state)return;
    const auto start=BRepTiming::Clock::now();auto& s=*state;
    struct Finish {Record& record;BRepTiming::Clock::time_point start;~Finish(){record.observerMs=elapsed(start);}} finish{s.record,start};
    BRepTiming::Clock::time_point validateStart{};
    try {
        // Freeze production timings before any observer copies/validation.
        for(const auto& [name,value]:BRepTiming::stats) {
            const auto before=s.before.find(name);const auto old=before==s.before.end()?BRepTiming::Stats{}:before->second;
            s.production[name]={value.calls-old.calls,value.inclusive-old.inclusive,value.self-old.self};
        }
        s.events.assign(BRepTiming::details.begin()+s.eventBegin,BRepTiming::details.end());s.candidateId=BRepTiming::shapeId(result);
        auto geometry=s.production.find("MoveFaceGeometry.MoveFaceGeometry::move");
        if(geometry!=s.production.end())s.record.productionMs=geometry->second.inclusive;
        s.record.productionAccepted=!result.IsNull();
        if(!sourceBindingCurrent||!source.IsEqual(s.source)||!selected.IsEqual(s.selected)||E::fingerprint(source)!=s.binding) {
            s.record.reason="source/selection/revision binding unavailable or changed";return;
        }
        BRepBuilderAPI_Copy isolated(source,true,false);auto target=TopoDS::Face(isolated.ModifiedShape(selected));target.Orientation(selected.Orientation());
        TopoDS_Shape candidate;if(!result.IsNull())candidate=BRepBuilderAPI_Copy(result,true,false).Shape();
        validateStart=BRepTiming::Clock::now();E::Box box;
        auto sourceCheck=E::inspect(isolated.Shape(),box);
        if(sourceCheck.verdict!=E::Verdict::Certified||!target.IsEqual(box.top)) {
            s.record.reason="strict source eligibility: "+sourceCheck.reason;s.record.shadowMs=elapsed(validateStart);return;
        }
        s.record.eligible=true;
        const char* inject=std::getenv("OPENDESIGNER_BOX_SHADOW_TEST_FAULT");
        if(inject&&std::string(inject)=="throw")throw std::runtime_error("injected observer failure");
        auto verdict=inject&&std::string(inject)=="unresolved"?E::unsure("injected Unresolved observer result"):
            E::candidate(box,candidate,delta);
        s.record.shadowMs=elapsed(validateStart);s.record.shadow=E::name(verdict.verdict);s.record.reason=verdict.reason;
        s.record.disagreement=(verdict.verdict==E::Verdict::Certified)!=s.record.productionAccepted;
    } catch(...) {
        try {s.record.shadow="Unresolved";s.record.reason="observer exception; production result retained";
            s.record.disagreement=s.record.productionAccepted;
            if(validateStart!=BRepTiming::Clock::time_point{})s.record.shadowMs=elapsed(validateStart);
        } catch(...) {} // Even failure reporting cannot escape.
    }
}
Trace::~Trace() noexcept {
    if(!state)return;
    auto& s=*state;s.record.previewMs=elapsed(s.start);
    try {
        latest=s.record;++completed;
        std::cerr<<std::setprecision(9)<<"BOX_SHADOW production="<<(s.record.productionAccepted?"Accepted":"Refused")
          <<" eligible="<<s.record.eligible<<" shadow="<<s.record.shadow<<" disagreement="<<s.record.disagreement
          <<" reason="<<s.record.reason<<" production_ms="<<s.record.productionMs<<" shadow_ms="<<s.record.shadowMs
          <<" observer_ms="<<s.record.observerMs<<" preview_ms="<<s.record.previewMs<<'\n';
        // Scope rows are inclusive and self/exclusive; NEVER sum inclusive rows.
        for(const auto& [name,value]:s.production) if(value.calls)
            std::cerr<<"BOX_STAGE production "<<name<<" calls="<<value.calls
               <<" inclusive_ms="<<value.inclusive<<" exclusive_ms="<<value.self<<'\n';
        for(const auto& [name,value]:BRepTiming::stats) {
            const auto before=s.before.find(name);const auto old=before==s.before.end()?BRepTiming::Stats{}:before->second;
            const auto prod=s.production.find(name);const auto p=prod==s.production.end()?BRepTiming::Stats{}:prod->second;
            if(value.calls>old.calls+p.calls)std::cerr<<"BOX_STAGE observer-and-delivery "<<name<<" calls="<<value.calls-old.calls-p.calls
               <<" inclusive_ms="<<value.inclusive-old.inclusive-p.inclusive<<" exclusive_ms="<<value.self-old.self-p.self<<'\n';
        }
        std::set<int> measuredVolumes,measuredAreas;unsigned repeatedVolumes=0,repeatedAreas=0;
        for(const auto& event:s.events) {
            if(event.kind=="Measure.Volume"&&!measuredVolumes.insert(event.a).second)++repeatedVolumes;
            if(event.kind=="Measure.Area"&&!measuredAreas.insert(event.a).second)++repeatedAreas;
            const char* role=event.ao==s.sourceId?"source-origin":event.a==s.candidateId?"returned-candidate":"intermediate/unattributed";
            std::cerr<<"BOX_EVENT "<<event.kind<<" role="<<role<<" a="<<event.a<<" origin="<<event.ao
              <<" b="<<event.b<<" result="<<event.result<<" ms="<<event.ms<<" path="<<event.path<<'\n';
        }
        std::cerr<<"BOX_REPEATS exact_identity_volume="<<repeatedVolumes<<" exact_identity_area="<<repeatedAreas
          <<" note=identity-is-not-an-immutable-revision-proof\n";
    } catch(...) {} // Logging is also strictly observational.
}
Record last() {return latest;}
unsigned count() noexcept {return completed;}
}
