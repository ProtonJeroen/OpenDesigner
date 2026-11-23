#pragma once
// F2.9F.1 generic provisional capability negotiation for a planar Face move.
//
// The session describes the request only by the actual B-Rep, the selected
// Face, the Part placement and the operation parameters. There are no primitive,
// feature-history or file-origin inputs. Negotiation is display-only: it never
// certifies, never produces a FaceChange or P2 receipt, and every result other
// than Outcome::Ready/Failed means "use the existing certified preview".
//
// F2.9F.4: this header is provider-neutral. Providers prove capability through
// the PlanarMoveProvider contract below; planning, sweep, construction and
// validation run here through the shared planar stages (planPlanarSweep /
// sweepPlanarRegion / constructPlanarSweep). Provider selection lives outside
// this header (BRepProvisionalProviders.h); nothing here names a provider.
#include "BRepProvisional.h"
#include "BRepReconstruction.h"
#include "BRepCertification.h"
#include "BRepTiming.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Trsf.hxx>
#include <cmath>
#include <exception>
#include <optional>
#include <array>
#include <algorithm>
#include <initializer_list>

namespace BRepReconstruction::Provisional {
struct PlanarMoveRequest {
    TopoDS_Shape source;   // canonical Part B-Rep (no placement applied)
    TopoDS_Face face;      // selected Face, a sub-shape of source
    gp_Trsf placement;     // Part placement
    gp_Trsf displayed;     // placement of the displayed presentation
    // Operation parameters.
    bool planarFaceMove=false;        // planar Face translation route (not boss/hole/blend)
    bool blendFeatures=false;         // source carries edge-blend features (blend-aware route)
    bool alongOutwardNormal=false;    // direction is the Face's own outward normal: not reversed, no references
    bool invariantCapPolicy=false;    // certified route uses the axial invariant-cap reconstruction policy
};

// Constant time; examines no geometry. Unsupported when the operation or the
// placement lies outside the provisional contract. Otherwise CertifiedOnly:
// the request may be negotiated, but no source has been admitted yet.
inline Admission admitOperation(const PlanarMoveRequest& request) {
    if(!request.planarFaceMove||request.blendFeatures||!request.alongOutwardNormal||!request.invariantCapPolicy
       ||!identityPlacement(request.placement)||!identityPlacement(request.displayed))return Admission::Unsupported;
    return Admission::CertifiedOnly;
}

// F2.9F.2 shared minimum OCCT displayability of a candidate (moved unchanged
// from the Box provider; the provider-specific form is the expectation). One
// pass over Faces, Edges and Vertices, as before. DISPLAY ONLY: this never
// replaces final certification (material, reconstruction, history) at OK.
inline bool displayable(const TopoDS_Shape& shape,const DisplayExpectation& expected) {
    BRepTiming::Scope timer("MoveFace.BoxProvisional.DisplayChecks"); // name kept for measurement continuity
    auto count=[](const TopoDS_Shape& s,TopAbs_ShapeEnum type){TopTools_IndexedMapOfShape m;TopExp::MapShapes(s,type,m);return m.Extent();};
    if(shape.IsNull()||count(shape,TopAbs_SOLID)!=1||count(shape,TopAbs_SHELL)!=1
       ||count(shape,TopAbs_FACE)>expected.maxFaces||count(shape,TopAbs_EDGE)>expected.maxEdges)return false;
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const auto f=TopoDS::Face(it.Current());
        if(expected.planarFaces&&(BRepAdaptor_Surface(f).GetType()!=GeomAbs_Plane||count(f,TopAbs_WIRE)!=1))return false;
        double a,b,c,d;BRepTools::UVBounds(f,a,b,c,d);
        for(double t:{a,b,c,d})if(!std::isfinite(t)||Precision::IsInfinite(t))return false;
        if(a>=b||c>=d)return false;
    }
    for(TopExp_Explorer it(shape,TopAbs_EDGE);it.More();it.Next()) {
        BRepAdaptor_Curve c(TopoDS::Edge(it.Current()));
        if((expected.linearEdges&&c.GetType()!=GeomAbs_Line)||!std::isfinite(c.FirstParameter())||!std::isfinite(c.LastParameter())
           ||Precision::IsInfinite(c.FirstParameter())||Precision::IsInfinite(c.LastParameter())||c.FirstParameter()>=c.LastParameter())return false;
    }
    for(TopExp_Explorer v(shape,TopAbs_VERTEX);v.More();v.Next()) {
        const auto p=BRep_Tool::Pnt(TopoDS::Vertex(v.Current()));
        if(!std::isfinite(p.X())||!std::isfinite(p.Y())||!std::isfinite(p.Z()))return false;
    }
    if(!BRepCertification::withinTolerance(shape,1.e-6)||!BRepCertification::validBRep(shape)
       ||!BRepCertification::closedShells(shape)||(expected.manifoldEdges&&!BRepCertification::manifold(shape)))return false;
    BRepClass3d_SolidClassifier classifier(shape);classifier.PerformInfinitePoint(Precision::Confusion());return classifier.State()==TopAbs_OUT;
}

// F2.9F.2 shared post-construction validation, applied to every provider's
// candidate. Request/candidate consistency (constant time), then the display
// gate for Ready candidates. Failure turns the candidate into Failed with a
// reason and no shape; non-Ready results pass through with no shape. Never
// upgrades a result, never certifies, never copies the B-Rep.
inline Candidate validateCandidate(const PlanarMoveRequest& request,double displacement,Candidate candidate,
                                   const DisplayExpectation& expected) {
    if(candidate.outcome!=Outcome::Ready){candidate.shape.Nullify();return candidate;}
    try {
        if(candidate.admission!=Admission::Provisional||candidate.shape.IsNull()||candidate.shape.IsSame(request.source)
           ||!std::isfinite(displacement)||std::abs(displacement)<1.e-7)
            throw Standard_Failure("Provisional candidate is inconsistent with its request.");
    } catch(const Standard_Failure& failure){candidate.reason=QString::fromUtf8(failure.GetMessageString());
        candidate.outcome=Outcome::Failed;candidate.shape.Nullify();return candidate;}
    // Display gate. A provider that declares fallBackOnFailure degrades to the
    // certified preview (CertifiedOnly); otherwise the existing Failed result.
    try {
        if(displayable(candidate.shape,expected))return candidate;
        candidate.reason=QString::fromUtf8(expected.failure);
    } catch(const Standard_Failure& failure){candidate.reason=QString::fromUtf8(failure.GetMessageString());}
      catch(const std::exception& failure){candidate.reason=QString::fromUtf8(failure.what());}
    candidate.outcome=expected.fallBackOnFailure?Outcome::CertifiedOnly:Outcome::Failed;candidate.shape.Nullify();return candidate;
}

// F2.9F.4 geometry-neutral provider contract: a constant aggregate of plain
// function pointers and data (no virtual dispatch, registry or allocation).
// A provider only PROVES; it never plans, sweeps, constructs or validates.
// The shared layer always binds full evaluation-local source certification
// (SourceAdmission) after a successful source proof, for every provider.
struct ProviderProof { std::array<double,8> scalars{}; }; // provider-owned, opaque to the shared layer
struct PlanarMoveProvider {
    // Exact source-geometry proof for (source, Face); may record scalars it
    // needs later. Must not modify or copy the source.
    bool (*proveSource)(const TopoDS_Shape& source,const TopoDS_Face& face,ProviderProof& proof);
    // Displacement admission beyond the generic finite/non-zero rule.
    bool (*admitsValue)(const ProviderProof& proof,double displacement);
    // Provider binding of the shared plan (constant time).
    bool (*admitsPlan)(const PlanarSweepPlan& plan);
    const char* planRefusal;      // reason when admitsPlan refuses
    DisplayExpectation display;   // candidate form guaranteed by this provider's proof
    // F2.9G.3 optional value-dependent proof on the shared plan, evaluated
    // after admitsPlan and before any sweep. Must use only the plan's isolated
    // copy: no copy, Boolean or certification. nullptr (the Box) = no hook.
    bool (*admitsSweep)(const PlanarSweepPlan& plan,ProviderProof& proof,double displacement) = nullptr;
};

// F2.9F.3 shared provisional reconstruction: one evaluation, in the former
// provider order (proof, source certificate, value, plan, plan binding, sweep,
// construct). Returns an UNVALIDATED candidate; negotiatePlanarMove validates.
// The source certificate is evaluation-local evidence owned by this scope
// (constructed in place, non-copyable/non-movable) and is handed to the plan,
// so the source is certified exactly once. Exceptions become Failed.
// F2.9G.3: providers are tried in the given order at the PROOF stage only: the
// first whose proveSource holds is selected (cheap, before any certificate).
// The shared layer then builds the single evaluation-local source certificate
// and runs the selected provider alone; a selected provider's later refusal is
// CertifiedOnly (certified preview) and never falls through to another
// provider. nullptr entries are skipped; with no usable provider the result is
// CertifiedOnly without examining geometry. `selected` receives the provider.
inline Candidate constructPlanarMove(const PlanarMoveRequest& request,std::initializer_list<const PlanarMoveProvider*> providers,
                                     double displacement,const PlanarMoveProvider** selected=nullptr) {
    Candidate candidate;if(selected)*selected=nullptr;
    if(admitOperation(request)==Admission::Unsupported){candidate.admission=Admission::Unsupported;return candidate;}
    if(std::none_of(providers.begin(),providers.end(),[](const PlanarMoveProvider* p){return p!=nullptr;}))return candidate;
    try {
        ProviderProof proof;std::optional<SourceAdmission> admitted;const PlanarMoveProvider* provider=nullptr;
        {   BRepTiming::Scope eligibility("MoveFace.BoxProvisional.Eligibility"); // name kept for measurement continuity
            for(const auto* candidateProvider:providers) {
                if(!candidateProvider)continue;
                proof=ProviderProof{};
                if(candidateProvider->proveSource(request.source,request.face,proof)){provider=candidateProvider;break;}
            }
            if(!provider)return candidate;
            admitted.emplace(request.source,request.face);
        }
        if(selected)*selected=provider;
        if(!admitted->valid())return candidate;
        candidate.admission=Admission::Provisional;
        if(!std::isfinite(displacement)||!provider->admitsValue(proof,displacement)||std::abs(displacement)<1.e-7)return candidate;
        PlanarSweepPlan plan;
        if(!planPlanarSweep(request.source,request.face,false,plan,candidate.reason,&*admitted))return candidate;
        // Generic request invariant first, then the provider's binding. The
        // generic rule is unreachable today (allowSpline=false never yields a
        // spline plan), so the observable message is unchanged.
        if(!planMatchesRequest(plan,false)){candidate.reason=QStringLiteral("Shared plan differs from the provisional request.");return candidate;}
        if(!provider->admitsPlan(plan)){candidate.reason=QString::fromUtf8(provider->planRefusal);return candidate;}
        if(provider->admitsSweep&&!provider->admitsSweep(plan,proof,displacement))
            {candidate.reason=QStringLiteral("Provisional sweep admission is not proven for this displacement.");return candidate;}
        PlanarSweep sweep;
        {   BRepTiming::Scope timer("MoveFace.BoxProvisional.Construction");
            sweepPlanarRegion(plan,displacement,sweep);
            constructPlanarSweep(plan,displacement,sweep);
        }
        candidate.outcome=Outcome::Ready;candidate.shape=sweep.candidate();candidate.reason.clear();return candidate;
    } catch(const Standard_Failure& failure){candidate.reason=QString::fromUtf8(failure.GetMessageString());}
      catch(const std::exception& failure){candidate.reason=QString::fromUtf8(failure.what());}
    candidate.outcome=Outcome::Failed;candidate.shape.Nullify();return candidate;
}
// F2.9F.4 single-provider form (nullptr = no provider).
inline Candidate constructPlanarMove(const PlanarMoveRequest& request,const PlanarMoveProvider* provider,double displacement) {
    return constructPlanarMove(request,{provider},displacement);
}

// Value-dependent negotiation. Candidate.admission is Provisional only when a
// selected provider proved the actual source and Face and the shared source
// certificate holds; Candidate.outcome is then Ready, CertifiedOnly (value or
// proof outside the guarantee) or Failed. Every Ready candidate passes
// validateCandidate with the SELECTED provider's declared display form.
inline Candidate negotiatePlanarMove(const PlanarMoveRequest& request,std::initializer_list<const PlanarMoveProvider*> providers,double displacement) {
    const PlanarMoveProvider* selected=nullptr;
    auto candidate=constructPlanarMove(request,providers,displacement,&selected);
    return validateCandidate(request,displacement,std::move(candidate),selected?selected->display:DisplayExpectation{});
}
inline Candidate negotiatePlanarMove(const PlanarMoveRequest& request,const PlanarMoveProvider* provider,double displacement) {
    return negotiatePlanarMove(request,{provider},displacement);
}
}
