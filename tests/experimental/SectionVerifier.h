#pragma once
// F2.1 test-only experiment. No production dependency or modeling output.
#include <TopoDS_Wire.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Pln.hxx>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/rational.hpp>
#include <string>
#include <vector>
#include <memory>
namespace SectionExperiment {
using Rational=boost::rational<boost::multiprecision::cpp_int>;
enum class Outcome { Certified, Rejected, Unresolved };
enum class Check { NotChecked, Pass, Fail, Uncertain };
struct PairEvidence {
    size_t first,second;
    Check outcome;
    std::string reason;
    std::vector<Rational> determinants;
    bool operator==(const PairEvidence&) const = default;
};
inline constexpr unsigned LinePolicyVersion=1;
struct ExactPoint {Rational x,y,z;bool operator==(const ExactPoint&) const=default;};
struct EdgeEvidence {
    ExactPoint start,end;
    Rational first,last,guard;
    int startVertex,endVertex;
    bool operator==(const EdgeEvidence&) const=default;
};
struct EvaluationTiming {double preparationMs=0,pairsMs=0;};
struct Consumption {
    Outcome outcome=Outcome::Unresolved;
    bool evidenceConsistent=false;
    size_t verifiedPairs=0,pairCalculations=0;
    std::string reason;
    bool operator==(const Consumption&) const=default;
};
struct Evidence {
    unsigned policyVersion=LinePolicyVersion;
    std::shared_ptr<const void> sourceContext;
    std::string sourceBinding;
    std::vector<EdgeEvidence> edges;
    Outcome outcome=Outcome::Unresolved;
    size_t edgeUses=0,requiredPairs=0,assessedPairs=0,verifiedPairs=0;
    Check planarity=Check::NotChecked,closure=Check::NotChecked,
          regularity=Check::NotChecked,intersection=Check::NotChecked,
          orientation=Check::NotChecked,interior=Check::NotChecked;
    Rational signedTwiceArea=0;
    std::vector<PairEvidence> pairs;
    std::string arithmetic="exact rational predicates on stored binary line data; recorded-tolerance AABB ambiguity guard";
    std::string reason;
    bool operator==(const Evidence&) const = default;
};
class Input {
    TopoDS_Wire wire_;
    std::vector<TopoDS_Edge> uses_;
    gp_Pln plane_;
    std::shared_ptr<const void> context_=std::make_shared<const int>(0);
    std::string binding_;
    friend Evidence verify(const Input&,EvaluationTiming*);
    friend Consumption consumeEvidence(const Input&,const Evidence&,unsigned,EvaluationTiming*);
public:
    // Deep-copy geometry/topology: caller aliases cannot modify this context.
    Input(const TopoDS_Wire&,const std::vector<TopoDS_Edge>& orderedUses,const gp_Pln&);
};
Evidence verify(const Input&,EvaluationTiming* timing=nullptr);
Consumption consumeEvidence(const Input&,const Evidence&,unsigned requiredPolicyVersion=LinePolicyVersion,EvaluationTiming* timing=nullptr);
// Compatibility wrapper around the separate consumer, not producer replay.
bool checkEvidence(const Input&,const Evidence&);
}
