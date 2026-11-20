#pragma once
// F2.9E.2 shared provisional candidate contract. Geometry-only, no session,
// document or renderer types. A Candidate is display-only evidence that OCCT
// construction and the minimum display gate succeeded for one exact input. It
// is NOT a reconstruction or material certificate: there is deliberately no
// certified state, and no conversion to FaceChange, P2 receipt or document
// geometry exists. Only the existing full production path may commit.
#include <QString>
#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>

namespace BRepReconstruction::Provisional {
// Value-independent capability of a source/selection/operation (shared
// reconstruction interface, F2.9E.3+). Anything but Provisional keeps the
// existing certified preview.
enum class Admission { Provisional, CertifiedOnly, Unsupported };
// Per-input construction result. CertifiedOnly: outside the provisional
// guarantee, use the certified preview. Failed: construction or display gate
// failed; reason is reported and nothing becomes committable.
enum class Outcome { Ready, CertifiedOnly, Failed };
// F2.9F.2: the candidate form a provider's construction guarantees, declared as
// data so the shared display gate applies it within its own single traversal.
// Expectations only restrict; the generic checks always run. Display-only: this
// is never a material, reconstruction or document certificate.
struct DisplayExpectation {
    bool planarFaces = false;   // every Face planar with exactly one wire
    bool linearEdges = false;   // every Edge a line
    bool manifoldEdges = false; // BRepCertification::manifold (not imposed on every solid)
    int maxFaces = 64;
    int maxEdges = 256;
    const char* failure = "Provisional candidate failed display checks.";
    // F2.9G.3: when true, a display-gate failure yields CertifiedOnly (the
    // existing certified preview) instead of Failed. Consistency failures and
    // construction exceptions are never converted. The Box keeps false.
    bool fallBackOnFailure = false;
};
struct Candidate {
    // F2.9F.1: value-independent negotiation result for this source/Face/
    // operation. Provisional only when a provider admitted the actual source;
    // Outcome then reports the value-dependent result.
    Admission admission = Admission::CertifiedOnly;
    Outcome outcome = Outcome::CertifiedOnly;
    TopoDS_Shape shape; // isolated, owned graph; non-null only when Ready
    QString reason;     // diagnostic for Failed
};
// F2.9D.4 placement rule (moved here unchanged in F2.9F.1; provider-neutral):
// judged on the actual matrix, not only the OCCT form label. Native Open
// restores an exact identity Part placement through gp_Trsf::SetValues, which
// always labels it gp_CompoundTrsf. Exact equality only (no tolerance): unit,
// positive scale; every rotation/scale entry exactly 1 or 0; translation
// exactly 0. Any other form keeps the certified route. Constant time.
inline bool identityPlacement(const gp_Trsf& placement) {
    const auto form=placement.Form();
    if((form!=gp_Identity&&form!=gp_CompoundTrsf)||placement.IsNegative()||placement.ScaleFactor()!=1.)return false;
    for(int row=1;row<=3;++row)for(int column=1;column<=4;++column)
        if(placement.Value(row,column)!=(row==column?1.:0.))return false;
    return true;
}
}
