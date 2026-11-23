#pragma once
#include "SectionPrismVerifier.h"
#include "SectionFaceDomain.h"
namespace SectionExperiment {
enum class FaceRole {Top,Bottom,Wall};
struct MaterialSideEvidence {
    size_t face=0;
    FaceRole role=FaceRole::Wall;
    std::array<Rational,3> normal{},inward{},outward{};
    bool negativeSideIsInterior=true;
};
struct PrismDomainEvidence {
    PrismEvidence boundary;
    std::vector<FaceDomainEvidence> domains;
    std::vector<MaterialSideEvidence> sides;
    Outcome proposalOutcome=Outcome::Unresolved;
    double domainProductionMs=0;
    std::string reason;
};
struct PrismDomainVerdict {
    Outcome outcome=Outcome::Unresolved,nominalDomains=Outcome::Unresolved,
            actualDomains=Outcome::Unresolved,materialSides=Outcome::Unresolved,material=Outcome::Unresolved;
    size_t faces=0,cells=0,kernelChecks=0;
    double baselineMs=0,faceDomainChecksMs=0,repeatedBindingMs=0,materialSideChecksMs=0,independentAddedMs=0,totalMs=0;
    std::string reason;
};
PrismDomainEvidence producePrismDomains(const PrismInput&);
PrismDomainVerdict consumePrismDomains(const PrismInput&,const PrismDomainEvidence&);
}
