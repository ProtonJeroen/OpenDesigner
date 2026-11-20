#pragma once
#include "BoxProvisionalPreview.h"
#include <memory>
#include <optional>

namespace BoxProvisionalExperiment {
// Test-only coordinator. The bridge supplies full production evaluation and P2
// guards; this class neither certifies geometry nor constructs a receipt.
class SessionAdapter {
public:
    SessionAdapter()=default;
    SessionAdapter(const SessionAdapter&)=delete;
    SessionAdapter& operator=(const SessionAdapter&)=delete;
    ~SessionAdapter(){cancel();}
    enum class Phase { Idle, Provisional, Certifying, Certified, Failed, Cancelled };
    struct CertifiedResult {
        TopoDS_Shape shape;
        std::shared_ptr<void> transaction;
    };
    Phase phase() const { return phase_; }
    bool committed() const { return committed_; }
    bool publish(const State& state,quint64 generation) {
        if(committed_||(phase_!=Phase::Idle&&phase_!=Phase::Provisional&&phase_!=Phase::Failed))return false;
        if(!state.displayable||state.source.IsNull()||state.selected.IsNull()||state.candidate.IsNull()
           ||state.partId.isEmpty()||!std::isfinite(state.displacement)||(state.displacement!=15&&state.displacement!=-5)
           ||state.operation!=Operation::MoveFace||state.mode!=Mode::FaceNormal)return fail();
        binding_=Binding{state.partId,state.source,state.candidate,state.selected,state.revision,generation,state.displacement,state.operation,state.mode};
        result_.reset();phase_=Phase::Provisional;return true;
    }
    // Only a successful production bridge may supply CertifiedResult. Status
    // flags or raw displayability are never sufficient evidence here.
    template<class Guard,class FullCertification>
    bool certify(const State& state,quint64 generation,bool inputPending,Guard guard,FullCertification full) {
        if(committed_||phase_!=Phase::Provisional)return false;
        try {
            if(inputPending||!matches(state,generation)||!guard())return fail();
            phase_=Phase::Certifying; // binding_ is frozen through callback
            auto result=full();
            if(phase_!=Phase::Certifying||!binding_||!result||result->shape.IsNull()||!result->transaction||result->shape.IsEqual(binding_->raw)
               ||!matches(state,generation)||!guard())return fail();
            result_=std::move(result);phase_=Phase::Certified;return true;
        } catch(...) {fail();throw;}
    }
    template<class Guard,class Commit>
    bool commit(const State& state,quint64 generation,Guard receiptMatches,Commit commitCertified) {
        if(committed_||phase_!=Phase::Certified||!result_)return false;
        try {
            if(!matches(state,generation)||!receiptMatches(*result_))return fail();
            if(!commitCertified(*result_))return fail();
            committed_=true;binding_.reset();result_.reset();phase_=Phase::Idle;return true;
        } catch(...) {fail();throw;}
    }
    bool fail() {
        if(committed_||phase_==Phase::Cancelled)return false;
        binding_.reset();result_.reset();phase_=Phase::Failed;return false;
    }
    bool cancel() {
        // Synchronous OCCT cannot be interrupted. Never report cancellation of
        // the in-flight callback; cleanup can run once it returns.
        if(committed_||phase_==Phase::Certifying||phase_==Phase::Cancelled)return false;
        binding_.reset();result_.reset();phase_=Phase::Cancelled;return true;
    }
private:
    struct Binding {
        QString part;TopoDS_Shape source,raw;TopoDS_Face face;
        quint64 revision,generation;double displacement;Operation operation;Mode mode;
    };
    bool matches(const State& state,quint64 generation) const {
        return binding_&&state.displayable&&binding_->part==state.partId&&binding_->source.IsEqual(state.source)
            &&binding_->raw.IsEqual(state.candidate)&&binding_->face.IsEqual(state.selected)
            &&binding_->revision==state.revision&&binding_->generation==generation
            &&binding_->displacement==state.displacement&&binding_->operation==state.operation&&binding_->mode==state.mode;
    }
    Phase phase_=Phase::Idle;
    bool committed_=false;
    std::optional<Binding> binding_;
    std::optional<CertifiedResult> result_;
};
}
