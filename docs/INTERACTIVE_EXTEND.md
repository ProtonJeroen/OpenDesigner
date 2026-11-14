# Interactive Extend — v0.5.6

Activate a visible, unlocked Sketch Workplane, then choose **2D Modeling → Extend**
(or Extend on the 2D toolbar). Click near the source endpoint to extend. A blue
marker identifies the selected endpoint; click a separate boundary curve next.
The original geometry remains displayed until a valid blue preview is ready.
OK/Enter confirms one SketchChange. Cancel/Escape discards the operation and
preserves Redo. One Extend is permitted per confirmation. Source ID, name, role
and flags are retained; the selected boundary is never changed.

Picking uses the same native AIS detection and Sketch eligibility rules as
Trim. Clicks in the middle of a source do not choose an endpoint. Multiple
curves or endpoints under the pointer require a clearer pick or zoom. Both
source and boundary must be visible and unlocked on the active Workplane.
Changing the model, hiding the Workplane or switching tools cancels the pending
operation. Save stores confirmed values only.

## Exact geometry and safe refusals

- Segments extend along their original straight support, at either end.
- Circular and elliptical arcs extend along their exact conic supports, retaining
  axis placement and signed traversal. Periodic parameter seams are preserved.
  An extension cannot pass through the opposite endpoint or create a full turn.
- Trimmed coefficient B-splines can reveal more of their **existing knot domain**.
  Poles, weights, knots, multiplicities and reversal remain unchanged, including
  rational supports. Periodic seam hits use the alias on the selected side,
  without extending past the canonical knot-domain limit.
- Interpolating spline extrapolation and B-spline extrapolation beyond existing
  knots are refused explicitly. No new through-points, fitting or polylines are
  introduced. Complete closed curves and infinite lines are not Extend sources;
  infinite lines may supply boundaries.

Only certified crossings beyond the selected endpoint are considered. Existing
finite-domain intersections and intersections on the opposite extension side
are ignored. Boundary domains remain finite where stored as finite curves.
Exactly one valid crossing must remain. Multiple solutions are refused even
when one is nearer; select a finite boundary portion with one crossing instead.
Tangencies, coincident/overlapping supports, unresolvable geometry and ambiguous
contacts fail atomically, with no candidate or history change.

The existing 1e-7 Sketch tolerance and downstream certification limits remain
unchanged. UI preview is separate from confirmed document values.

## Profile compatibility

Extend produces a valid Sketch curve, which can still belong to an open or
invalid Profile. The preview message distinguishes these cases from a closed
Profile passing topology validation. Profile Builder retains responsibility for
wire/face certification and refuses invalid contours. Extend does not broaden Profile Validation or
Builder policy. In particular, rational or higher-degree
coefficient B-splines may be exact Sketch extensions while remaining unsupported
for Profile construction under the [Trim profile restrictions](INTERACTIVE_TRIM.md).

## Focused validation and Fedora KDE acceptance

`sketch-extend-geometry` checks independent coordinates/parameters, finite-domain
filtering, reversal, periodic seams, unchanged B-spline coefficients, safe
refusals and existing profile certification. `sketch-extend-sessions` and its
HiDPI variant exercise real clicks and the existing transaction lifecycle.
Native KDE acceptance remains manual; repeat at normal and 200% scaling:

1. Extend each end of a line to an explicitly chosen Construction boundary.
   Include a nearer unselected boundary; confirm only the selected one is used.
2. Check source-endpoint marker, boundary hover, blue preview, OK/Enter,
   Cancel/Escape and one-step Undo/Redo. A second preview click must not chain
   another operation. Save during preview, then confirm and Save/Open a copy.
3. Extend clockwise circular/elliptical arcs, including across a parameter seam.
   Inspect fixed endpoints, support placement and retained signed traversal.
4. Select a circle yielding two forward intersections, a tangent, an overlapping
   support and a boundary missing the available support: expect explicit refusal.
5. Trim a spline, then extend a coefficient fragment within its original knot
   domain. Try extending beyond the domain and extrapolating an original
   interpolating spline: expect precise refusal.
6. Repeat on a moved/rotated Workplane. Hide/lock sources and boundaries; switch
   tools or hide Workplanes during preview. Check cancellation and no stale
   blue geometry. Close a supported contour and use the existing Profile Builder;
   an open/invalid Profile must not be presented as a certified solid profile.

The consolidated [Fedora KDE/Wayland acceptance checklist](V056_KDE_ACCEPTANCE.md)
covers 100% and 200% scaling. Manual acceptance is **pending**; automated
xcb/HiDPI sessions do not certify native desktop behavior.
