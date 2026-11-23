#pragma once
// Opt-in investigation diagnostics only. No geometry, caching or acceptance changes.
#include <chrono>
#include <cstdlib>
#include <map>
#include <ostream>
#include <string>
#include <vector>
#include <TopoDS_Shape.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <TopExp_Explorer.hxx>
#include <limits>
#include <iomanip>
namespace BRepTiming {
using Clock=std::chrono::steady_clock;
struct Stats {unsigned calls=0;double inclusive=0,self=0;};
struct Frame {const char* name;Clock::time_point start;double children=0;unsigned id=0;};
inline thread_local bool enabled=false;
inline thread_local bool detailed=false;
inline thread_local unsigned sequence=0,frameSequence=0;
struct Detail {unsigned id;std::string kind,path;int a=0,ao=0,b=0,bo=0,result=0,ro=0;double ms=0,value=0;int mode=-1;};
inline thread_local std::vector<TopoDS_Shape> shapes;
inline thread_local std::vector<int> origins;
inline thread_local std::vector<Detail> details;
inline int shapeId(const TopoDS_Shape& shape) {
    if(shape.IsNull()) return 0;
    for(size_t i=0;i<shapes.size();++i) if(shapes[i].IsEqual(shape)) return int(i+1);
    shapes.push_back(shape);origins.push_back(int(shapes.size()));return int(shapes.size());
}
inline void copyOrigin(const TopoDS_Shape& from,BRepBuilderAPI_Copy& copy) {
    if(!enabled||!detailed||!copy.IsDone()) return;
    const auto link=[&](const TopoDS_Shape& a,const TopoDS_Shape& b) {
        const int source=shapeId(a),target=shapeId(b);if(source&&target) origins[target-1]=origins[source-1];
    };
    link(from,copy.Shape());
    for(TopExp_Explorer f(from,TopAbs_FACE);f.More();f.Next()) {
        try {
            auto target=copy.ModifiedShape(f.Current());if(target.IsNull()) continue;
            target.Orientation(f.Current().Orientation());link(f.Current(),target);
        } catch(...) {} // An unavailable diagnostic mapping must not affect modeling.
    }
}
// RemoveInternals can mutate a copied graph. Do not infer its later identity
// from copy provenance. No geometry or validation result is reused by this tracer.
inline void forgetCopy(const TopoDS_Shape& shape) {
    if(!enabled||!detailed) return;
    const int id=shapeId(shape);origins[id-1]=id;
    for(TopExp_Explorer f(shape,TopAbs_FACE);f.More();f.Next()) {const int child=shapeId(f.Current());origins[child-1]=child;}
}
inline thread_local std::map<std::string,Stats> stats;
inline thread_local std::vector<Frame> stack;
class Scope {
    bool active;
public:
    explicit Scope(const char* name):active(enabled) {if(active) stack.push_back({name,Clock::now(),0,++frameSequence});}
    ~Scope() {
        if(!active) return;
        const auto end=Clock::now();const auto frame=stack.back();stack.pop_back();
        const double ms=std::chrono::duration<double,std::milli>(end-frame.start).count();
        auto& entry=stats[frame.name];++entry.calls;entry.inclusive+=ms;entry.self+=ms-frame.children;
        if(!stack.empty()) stack.back().children+=ms;
    }
};
class Event {
    bool active=false,finished=false;Detail entry{};Clock::time_point start;
public:
    Event(std::string kind,const TopoDS_Shape& a={},const TopoDS_Shape& b={}) :active(enabled&&detailed) {
        if(!active) return;
        entry.id=++sequence;entry.kind=std::move(kind);entry.a=shapeId(a);entry.b=shapeId(b);
        entry.ao=entry.a?origins[entry.a-1]:0;entry.bo=entry.b?origins[entry.b-1]:0;
        for(const auto& f:stack) {if(!entry.path.empty()) entry.path+='>';entry.path+=std::string(f.name)+"#"+std::to_string(f.id);}
        start=Clock::now();
    }
    void finish(const TopoDS_Shape& result={},double value=std::numeric_limits<double>::quiet_NaN(),int mode=-1) {
        if(!active||finished) return;
        const auto end=Clock::now();entry.ms=std::chrono::duration<double,std::milli>(end-start).count();
        entry.result=shapeId(result);entry.ro=entry.result?origins[entry.result-1]:0;entry.value=value;entry.mode=mode;
        details.push_back(std::move(entry));finished=true;
    }
    ~Event() {finish();}
};
inline void reset() {stats.clear();details.clear();shapes.clear();origins.clear();sequence=frameSequence=0;}
inline void report(std::ostream& out,const std::string& label) {
    const auto precision=out.precision();
    out<<"TIMING "<<label<<"\nname,calls,inclusive_ms,self_ms\n";
    for(const auto& [name,s]:stats) out<<name<<','<<s.calls<<','<<s.inclusive<<','<<s.self<<'\n';
    for(const auto& d:details)
        out<<"DETAIL|"<<d.id<<'|'<<d.kind<<'|'<<d.a<<'|'<<d.ao<<'|'<<d.b<<'|'<<d.bo<<'|'<<d.result<<'|'<<d.ro
           <<'|'<<std::setprecision(9)<<d.ms<<'|'<<d.value<<'|'<<d.mode<<'|'<<d.path<<'\n';
    out.precision(precision);out.flush();
    // Investigation records live for one synchronous timing phase only.
    details.clear();shapes.clear();origins.clear();
}
}
