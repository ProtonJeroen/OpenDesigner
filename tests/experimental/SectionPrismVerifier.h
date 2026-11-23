#pragma once
#include "SectionCapCorrespondence.h"
#include <array>
namespace SectionExperiment {
struct PrismTimes {double capExtractionMs=0,capCertificationMs=0,wallExtractionMs=0,wallCorrespondenceMs=0,verificationMs=0;};
struct WallEvidence {
    size_t face=0,topEdge=0,bottomEdge=0;
    std::array<size_t,2> lateralEdges{};
    bool operator==(const WallEvidence&) const=default;
};
struct PrismEvidence {
    std::shared_ptr<const void> context;
    std::string binding;
    Outcome proposalOutcome=Outcome::Unresolved;
    std::string reason;
    size_t top=0,bottom=0,edges=0,vertices=0,edgeUses=0;
    Rational height=0;
    std::vector<CapClaim> faces;
    std::vector<size_t> bottomByTop;
    std::vector<WallEvidence> walls;
    PrismTimes times;
};
struct PrismVerdict {
    Outcome boundary=Outcome::Unresolved;
    Outcome material=Outcome::Unresolved; // Never a replacement material certificate.
    std::string reason;
    size_t caps=0,walls=0,edges=0,edgeUses=0,vertices=0;
    double verificationMs=0;
};
struct PrismDomainEvidence;
struct PrismDomainVerdict;
class PrismInput {
    TopoDS_Shape source_;
    size_t top_=0;
    Rational requestedHeight_;
    std::shared_ptr<const void> context_=std::make_shared<const int>(0);
    std::string binding_;
    std::vector<std::shared_ptr<const CapInput>> faceInputs_;
    friend PrismDomainEvidence producePrismDomains(const PrismInput&);
    friend PrismDomainVerdict consumePrismDomains(const PrismInput&,const PrismDomainEvidence&);
    friend PrismEvidence producePrism(const PrismInput&);
    friend PrismVerdict consumePrism(const PrismInput&,const PrismEvidence&);
public:
    PrismInput(const TopoDS_Shape&,const TopoDS_Face& top,Rational requestedHeight=20);
};
PrismEvidence producePrism(const PrismInput&);
PrismVerdict consumePrism(const PrismInput&,const PrismEvidence&);
}
