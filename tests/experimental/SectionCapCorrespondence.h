#pragma once
#include "SectionVerifier.h"
#include <TopoDS_Face.hxx>
namespace SectionExperiment {
struct CapClaim {
    std::shared_ptr<const void> context;
    std::string binding;
    std::vector<Rational> frame; // world O,U,V,N coefficients
    std::vector<size_t> order;   // section occurrence -> actual cap occurrence
    std::shared_ptr<const Input> section;
    Evidence proof;
};
// Certified here means extraction obligations only; consumeCap must still
// independently certify the section and its complete actual-cap correspondence.
struct CapExtraction {
    Outcome outcome=Outcome::Unresolved;
    std::string reason;
    CapClaim claim;
    std::vector<EdgeEvidence> actualEdges;
    size_t faceIndex=0,edgeCount=0;
    std::vector<std::pair<size_t,size_t>> adjacentFaces;
};
struct CapConsumption {
    Outcome outcome=Outcome::Unresolved;
    std::string reason;
    size_t matchedEdges=0;
    double sectionConsumptionMs=0,correspondenceMs=0;
};
class CapInput {
    TopoDS_Shape source_;
    TopoDS_Face face_;
    bool member_=false;
    bool allowReversed_=false;
    std::shared_ptr<const void> context_=std::make_shared<const int>(0);
    std::string binding_;
    friend struct FaceDomainAccess;
    friend CapExtraction extractCap(const CapInput&);
    friend CapConsumption consumeCap(const CapInput&,const CapClaim&);
public:
    CapInput(const TopoDS_Shape&,const TopoDS_Face&,bool allowReversedCarrier=false);
};
CapExtraction extractCap(const CapInput&);
CapConsumption consumeCap(const CapInput&,const CapClaim&);
}
