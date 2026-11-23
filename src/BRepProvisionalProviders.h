#pragma once
// The single explicit provisional provider-selection point (F2.9F.4, F2.9G.3).
//
// The shared reconstruction (BRepProvisionalNegotiation.h) receives providers
// as arguments and never names one. This header is the only place that knows
// which providers exist and in which order they are offered. There is no
// registry, scoring or provider-to-provider fallback: the shared layer selects
// the FIRST provider whose cheap source proof holds, builds the one source
// certificate, and runs that provider alone. Order:
//   1. exact Box (unchanged behavior, messages, display policy and counts);
//   2. geometry-independent clear planar sweep (F2.9G.3);
//   otherwise CertifiedOnly -> existing certified preview.
#include "BRepProvisionalNegotiation.h"
#include "ProvisionalBoxPreview.h"
#include "ProvisionalPlanarClearance.h"

namespace BRepReconstruction::Provisional {
inline Candidate constructPlanarMove(const PlanarMoveRequest& request,double displacement,const PlanarMoveProvider** selected=nullptr) {
    return constructPlanarMove(request,{&ProvisionalBoxPreview::provider,&ProvisionalPlanarClearance::provider},displacement,selected);
}
inline Candidate negotiatePlanarMove(const PlanarMoveRequest& request,double displacement) {
    return negotiatePlanarMove(request,{&ProvisionalBoxPreview::provider,&ProvisionalPlanarClearance::provider},displacement);
}
}
