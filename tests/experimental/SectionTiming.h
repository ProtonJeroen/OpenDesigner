#pragma once
// Optional per-call experiment diagnostics; never part of proof decisions.
#include "SectionVerifier.h"
#include <chrono>
namespace SectionExperiment {
class EvaluationMeasurement {
    EvaluationTiming* result_;
    std::chrono::steady_clock::time_point start_,pairStart_;
    bool pairs_=false;
public:
    explicit EvaluationMeasurement(EvaluationTiming* result):result_(result) {
        if(result_){*result_={};start_=std::chrono::steady_clock::now();}
    }
    void beginPairs() {
        if(result_){pairStart_=std::chrono::steady_clock::now();result_->preparationMs=std::chrono::duration<double,std::milli>(pairStart_-start_).count();pairs_=true;}
    }
    ~EvaluationMeasurement() {
        if(!result_)return;
        const auto end=std::chrono::steady_clock::now();
        if(pairs_)result_->pairsMs=std::chrono::duration<double,std::milli>(end-pairStart_).count();
        else result_->preparationMs=std::chrono::duration<double,std::milli>(end-start_).count();
    }
};
}
