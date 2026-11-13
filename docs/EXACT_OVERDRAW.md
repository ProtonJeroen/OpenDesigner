# Exact elliptical and projected Overdraw — v0.5.6

The existing Overdraw command, finite-piece graph, hover choices and Sketch
transaction now accept ellipses, elliptical arcs, interpolating splines and
coefficient B-splines alongside the existing line/circle geometry. No second
curve store, selection model or Overdraw engine is introduced.

Finite endpoints and certified intersections bound pieces. Full circles,
ellipses and periodic splines need at least two distinct boundary points;
uncut full curves are not invented from a single contact. Interpolating spline
fragments and reversed interpolants retain their evaluated exact B-spline
coefficients; there is no re-interpolation or polyline geometry. Original
unchanged forward curves retain their complete values. Reversed traversal
keeps the selected locus, including major arcs and periodic parameter seams.
Periodic coefficient paths split at their existing seam where needed to keep
stored domains inside the canonical knot range.

## Coincidence, partial overlap and identity

Certified common analytic supports and identical B-spline coefficients are
partitioned at their finite endpoints as well as crossing intersections.
Exact coefficient reversal is recognized algebraically. Shared positive
intervals appear once in the graph; ordinary crossings remain separate incident
pieces. Complementary conic arcs are not collapsed merely because endpoints
match. Common-support endpoint contacts are distinguished from positive overlap
by certified parameter domains and geometric residuals.

Construction entities remain unchanged, with their original roles and IDs.
Choosing a new portion produces an independent Profile with a new ID. Existing
Profiles, including hidden/locked Profiles, prevent duplicate covered portions.
A covered choice reuses the canonical Profile unchanged, including its ID,
geometry, role, name, visibility and locking. Partial coverage does not trim an
existing Profile: its full extent remains in the Sketch, as explained in the
Overdraw notice. Blue chosen portions are transient views. A confirmation that
only reuses existing geometry adds no entity and no Undo step. New geometry
still commits in one existing SketchChange. No source links are persisted.

## Projection and profiles

The existing SketchProjection conversion is unchanged. A circle orthogonally
projected onto an oblique Workplane remains an exact ellipse with its analytic
axes, local placement and signed arc traversal. Part/subshape placement and
Workplane transformations are applied by that existing projection path.
Projected ellipses and standalone projected B-splines enter the same Overdraw
graph as other Sketch values; source geometry remains independent.

Supported Overdraw contours use existing Profile Validation and Builder,
including holes and disjoint regions. Rational/higher-degree coefficient
B-splines may be exact Sketch pieces but remain outside the existing Profile
certification scope. Invalid, overlapping, open or uncertifiable contours cannot
be constructed as profiles; Sketch data is not repaired or corrupted.

## Safe refusals and numerical limits

- Differently parameterized or differently represented B-splines are not assumed
  equivalent from sampled proximity. Comparisons without an exact coefficient
  proof may be refused atomically, even when the loci could be equivalent.
- Self-crossing/overlapping coefficient supports are refused, including when
  the crossing is outside a stored trim. Domain-based reuse requires a simple
  support; only the known periodic seam may repeat a support point.
- Near-coincident supports without an exact equivalence proof, unresolved cuts,
  singular intersections and tolerance-bridging node clusters fail atomically.
- Existing analytic Overdraw retains its numerical range. Advanced curves keep
  the shared precision headroom checks. Residual/node checks use the existing
  1e-7 Sketch tolerance; no downstream certification tolerance is loosened.
- A partial reused Profile may extend outside the chosen path and make a later
  contour invalid. Existing Profile Builder diagnostics remain authoritative.

Focused coverage is in `sketch-advanced-overdraw-geometry`, the existing
`sketch-overdraw-geometry`, `sketch-overdraw-sessions`,
`sketch-overdraw-traversal`, and the shared curve-operations regression when its
intersection handling changes. Projection and Profile Builder implementations
are unchanged. Manual Fedora KDE/Wayland acceptance remains pending.

The consolidated [Fedora KDE/Wayland acceptance checklist](V056_KDE_ACCEPTANCE.md)
covers 100% and 200% scaling. Manual acceptance is **pending**; automated
xcb/HiDPI sessions do not certify native desktop behavior.
