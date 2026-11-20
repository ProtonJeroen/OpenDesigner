#pragma once
// Development diagnostics only. No candidate/result is returned to production.
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <memory>
#include <chrono>
#include <string>
namespace BoxShadowObservation {
struct Record {
    bool eligible=false,productionAccepted=false,disagreement=false;
    std::string shadow="Bypassed",reason;
    double previewMs=0,productionMs=0,shadowMs=0,observerMs=0;
};
// Metrics only, no retained B-Rep or revision cache. Used by the focused test.
Record last();
unsigned count() noexcept;
class Trace {
    struct State;std::unique_ptr<State> state;
public:
    Trace(const TopoDS_Shape& source,const TopoDS_Face& selected,bool operationScope,std::chrono::steady_clock::time_point started) noexcept;
    ~Trace() noexcept;
    void observe(const TopoDS_Shape& source,const TopoDS_Face& selected,double delta,
                 const TopoDS_Shape& result,bool sourceBindingCurrent) noexcept;
};
}
