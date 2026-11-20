#include "SectionPrismDomains.h"
#include <algorithm>
#include <chrono>
#include <set>
namespace SectionExperiment {
namespace {
using Vec=std::array<Rational,3>;using Clock=std::chrono::steady_clock;
Vec frame(const CapClaim& c,int i){if(c.frame.size()!=12)throw std::runtime_error("missing Face frame");return {c.frame.at(i*3),c.frame.at(i*3+1),c.frame.at(i*3+2)};}
Vec scale(Vec a,const Rational& s){for(auto& x:a)x*=s;return a;}
Vec cross(const Vec& a,const Vec& b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
Rational dot(const Vec& a,const Vec& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
Vec tangent(const CapClaim& cap,const EdgeEvidence& e){const auto u=frame(cap,1),v=frame(cap,2);Vec r;for(int i=0;i<3;++i)r[i]=u[i]*(e.end.x-e.start.x)+v[i]*(e.end.y-e.start.y);return r;}
double ms(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
}
PrismDomainEvidence producePrismDomains(const PrismInput& input) {
    PrismDomainEvidence e;e.boundary=producePrism(input);const auto start=Clock::now();
    try {
        if(e.boundary.proposalOutcome!=Outcome::Certified)throw std::runtime_error("boundary proposal incomplete");
        bool complete=true;std::string missing;
        for(size_t f=0;f<e.boundary.faces.size();++f) {
            e.domains.push_back(produceFaceDomain(*input.faceInputs_.at(f),e.boundary.faces[f]));
            if(e.domains.back().proposalOutcome!=Outcome::Certified){complete=false;missing=e.domains.back().reason;}
            const auto n=frame(e.boundary.faces[f],3);
            e.sides.push_back({f,f==e.boundary.top?FaceRole::Top:f==e.boundary.bottom?FaceRole::Bottom:FaceRole::Wall,n,scale(n,-1),n,true});
        }
        e.proposalOutcome=complete?Outcome::Certified:Outcome::Unresolved;e.reason=complete?"domain and oriented-side witnesses proposed":missing;
    }catch(const std::exception& x){e.reason=x.what();}
    e.domainProductionMs=ms(start);return e;
}
PrismDomainVerdict consumePrismDomains(const PrismInput& input,const PrismDomainEvidence& e) {
    PrismDomainVerdict r;const auto start=Clock::now();auto addedStart=start;
    const auto finish=[&](const std::string& why,Outcome o=Outcome::Unresolved){r.reason=why;r.outcome=o;r.totalMs=ms(start);r.independentAddedMs=ms(addedStart);return r;};
    try {
        const auto boundary=consumePrism(input,e.boundary);r.baselineMs=boundary.verificationMs;addedStart=Clock::now();
        if(boundary.boundary!=Outcome::Certified)return finish("complete boundary refused: "+boundary.reason,boundary.boundary);
        const size_t count=e.boundary.faces.size();
        if(e.domains.size()!=count||e.sides.size()!=count)return finish("domain/side witness inventory incomplete");
        for(size_t f=0;f<count;++f) {
            const auto d=consumeFaceDomain(*input.faceInputs_.at(f),e.boundary.faces[f],e.domains[f]);
            r.faceDomainChecksMs+=d.domainChecksMs;r.repeatedBindingMs+=d.bindingVerificationMs;
            if(d.nominalDomain!=Outcome::Certified)return finish("Face domain refused: "+d.reason,d.nominalDomain);
            ++r.faces;r.cells+=d.cells;r.kernelChecks+=d.kernelChecks;
        }
        r.nominalDomains=Outcome::Certified;
        const auto sideStart=Clock::now();std::set<size_t> seen;
        const auto direction=frame(e.boundary.faces.at(e.boundary.top),3);
        for(size_t f=0;f<count;++f) {
            const auto& side=e.sides[f];const auto actualNormal=frame(e.boundary.faces[f],3);
            if(side.face!=f||!seen.insert(f).second||side.normal!=actualNormal||!side.negativeSideIsInterior
               ||side.inward!=scale(actualNormal,-1)||side.outward!=actualNormal
               ||dot(actualNormal,side.inward)>=0||dot(actualNormal,side.outward)<=0) {
                r.materialSideChecksMs=ms(sideStart);return finish("material-side evidence contradicts actual oriented Face");
            }
            Vec expected{};FaceRole role;
            if(f==e.boundary.top){expected=direction;role=FaceRole::Top;}
            else if(f==e.boundary.bottom){expected=scale(direction,-1);role=FaceRole::Bottom;}
            else {
                role=FaceRole::Wall;size_t index=e.boundary.walls.size();
                for(size_t i=0;i<e.boundary.walls.size();++i)if(e.boundary.walls[i].face==f){if(index!=e.boundary.walls.size())throw std::runtime_error("ambiguous wall role");index=i;}
                if(index==e.boundary.walls.size())throw std::runtime_error("unowned wall role");
                expected=cross(tangent(e.boundary.faces[e.boundary.top],e.boundary.faces[e.boundary.top].proof.edges.at(index)),direction);
            }
            if(side.role!=role||cross(expected,actualNormal)!=Vec{}||dot(expected,actualNormal)<=0) {
                r.materialSideChecksMs=ms(sideStart);return finish("Face side disagrees with section/extrusion orientation");
            }
        }
        r.materialSideChecksMs=ms(sideStart);r.materialSides=Outcome::Certified;
        // Exact nominal trim domains and constant local material-side normals
        // do not prove every OCCT point/solid interpretation or tolerance band.
        return finish("nominal domains and local material sides verified; all-point Face/solid interpretation Unresolved");
    }catch(const std::exception&){return finish("domain/side evidence or arithmetic incomplete");}
}
}
