#pragma once
#include "SectionCapCorrespondence.h"
namespace SectionExperiment {
struct DomainCell {
    Rational left,right,low,high;
    bool operator==(const DomainCell&) const=default;
};
struct FaceDomainEvidence {
    std::shared_ptr<const void> context;
    std::string binding;
    int faceOrientation=0,wireOrientation=0;
    bool naturalRestriction=false;
    std::vector<Rational> breaks;
    std::vector<DomainCell> cells;
    Rational area=0;
    Outcome proposalOutcome=Outcome::Unresolved;
    std::string reason;
};
struct FaceDomainVerdict {
    Outcome nominalDomain=Outcome::Unresolved;
    Outcome actualDomain=Outcome::Unresolved; // No formal OCCT all-point semantics claim.
    size_t cells=0,kernelChecks=0;
    double verificationMs=0,bindingVerificationMs=0,domainChecksMs=0;
    std::string reason;
};
struct FaceDomainAccess;
FaceDomainEvidence produceFaceDomain(const CapInput&,const CapClaim&);
FaceDomainVerdict consumeFaceDomain(const CapInput&,const CapClaim&,const FaceDomainEvidence&);
}
