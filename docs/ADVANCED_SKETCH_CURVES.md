# v0.5.5 Advanced Sketch Curves

This release extends the existing independent Sketch values, Workplane frames,
reference resolver, native document and shared Profile Builder. No curve stores
an associative link to a source Part. Confirmed B-Rep geometry remains authoritative.

## Supported workflow

- Draw exact ellipses and signed elliptical arcs using the existing drawing tools.
  Select one on the active Workplane and choose **2D Modeling → Edit Curve / Trim
  Parameters** to edit center, radii, axis angle and arc start/signed sweep.
  Parameters are eccentric angles, not polar angles. A full ellipse remains a
  separate type; an arc never changes to its complementary route.
- Choose **2D Modeling → Interpolating Spline**. Enter local Workplane X/Y
  coordinates in the table. Insert adds a point after the selected row; Remove
  removes that row; editing a cell moves its through-point. The Construction
  geometry toggle determines the new entity's role. Edit Curve edits an existing
  spline while retaining its ID, role, name, visibility and selection flags.
- Open splines pass through at least two points. Optional start/end tangent
  vectors use `X, Y`; blank means automatic. Explicit vector magnitudes are
  respected. Closed splines need at least three points and use periodic C1
  closure. Do not repeat the first point at the end. Periodic seam tangents are
  derived; explicit endpoint tangents in closed mode are refused.
- Preview closes the coordinate editor and shows the existing temporary,
  non-selectable blue Sketch preview. The command panel's OK/Enter commits one
  Undo step. Cancel/Escape commits none. Editor Cancel changes no geometry.
  Save includes only previously confirmed values. Undo/Redo retains exact values
  and IDs. **Duplicate Selected Curves** previews independent copies with new
  IDs and identical exact values; coincident Profile copies remain invalid until
  edited into a valid contour. Moving the owning Workplane preserves local
  coordinates; no new transform or history engine is introduced.
- Ellipse center, positive major/minor vertices and arc start/end/midpoint use
  `Sketch::PointReference`. Spline through-points have indexed references, plus
  start/end. The resolver re-evaluates all of them after Workplane movement.
  Ellipse major axes and spline start/end tangents supply line-like references
  through the existing direction resolver and picking path. Hidden/locked/stale
  objects remain unavailable. Through-point indices describe current point
  order; references are evaluated values, not persistent constraints.

## Exact profiles and solids

The shared validator and Profile Builder accept finite segments, circular arcs,
full circles, ellipses, elliptical arcs and cubic interpolating splines. Supported
mixed contours, holes and disjoint regions use the existing even-odd nesting
policy. Elliptical area is analytic. Interpolating spline area integrates Green's
formula over exact cubic knot spans with the three-point Gaussian rule, exact
for the resulting polynomial integrand; no tessellation defines model geometry.

Spline self-intersections and curve-pair contacts are checked with OCCT's exact
curve intersector. The periodic seam is accepted only within floating-point
roundoff at its known endpoint parameters and with a geometric residual check.
Open contours, branches, overlap, crossings and numerically ambiguous contacts
are refused atomically. Intersections/containment which cannot be certified are
reported as unsupported, never treated as disjoint.

Construction still independently checks shared endpoint vertices, closure,
orientation, source edge count, planar face validity, nesting, area and the
existing vertex tolerance (default 1e-7 mm). No tolerance is relaxed and no shape
healing is added. Extrude and Revolve use these same certified faces and their
existing volume, solid-count and interference checks. Shared volume measurement
uses Gauss–Kronrod integration with knot spans for spline/Bézier surfaces and
sweeps based on spline/Bézier curves. Analytic solids retain the established
integration path so repeated direct-modeling validation remains efficient.
Source-face area and centroid measurement uses adaptive integration. This improves integration accuracy without
relaxing the geometric acceptance tolerances. Revolve retains its
in-plane axis, same-side and no pinched-solid restrictions.

## Projection scope

Project 3D Geometry retains existing exact line/circle/arc/ellipse/elliptical-arc
conversion and analytic face-boundary support. It additionally accepts a finite
standalone OCCT B-spline edge, including rational and trimmed curves. Orthogonal
projection changes its poles by an affine map and retains degree, weights,
knots, multiplicities, periodicity, interval and traversal. Part and subshape
locations are applied exactly once. Point collapses are refused.

Exact coefficient duplicates (including reversed occurrences) are reused.
Point references and curves with disjoint certified pole-hull bounds can coexist.
Potentially overlapping B-spline comparisons without identical coefficients are
refused specifically. B-spline face boundaries are refused with wire/edge
context; project a standalone edge instead. No curve is replaced by a fitted
interpolant or polyline.

## Deliberate limits

- Projected coefficient B-splines are exact Construction references. They support
  display, endpoint/nearest picking, Save/Open and Sketch history. Their Profile
  certification and control-pole editing are not supported in v0.5.5. Changing
  one to Profile produces a specific refusal before any solid is committed.
- General spline/conic intersection snaps, elliptic tangent snaps and automatic
  intersection-based ellipse/spline Overdraw remain unsupported. The existing
  line/circle Overdraw engine stays guarded. Elliptical arcs can be trimmed by
  editing their exact parameter interval; splines can be edited through points.
- No associative projections, constraints, 3D spline paths, Sweep or Loft.
- At most 10000 spline points/poles. Consecutive interpolation points must be
  separated by more than 1e-7 mm. Unresolvable coordinates, invalid vectors,
  unsupported contacts and unresolved interpolation fail without model mutation.

## Native storage

In-memory capability is schema v6. A document containing a spline writes v6;
otherwise existing v3/v4/v5 selection remains unchanged. Tags 0–6 retain their
meaning. Tag 7 stores closure, through-point count, tangent presence/vectors and
XY through-points. Tag 8 stores exact B-spline degree, periodic/reversed flags,
trim interval, pole/knot counts, XY poles/weights and knots/multiplicities.
Readers validate counts, flags, finite values and kernel construction before
replacing the live document. Unknown schemas/tags are rejected. No private test
fixtures or untracked `.odc` documents are used.

## Automated validation

`sketch-spline-geometry` checks interpolation, directed tangent magnitudes,
periodic C1 closure, invalid inputs, point references, bounds, mixed-profile area
against an independent polynomial integral, Extrude volume, closed spline face
certification, exact rational edge projection and native round trips.
`sketch-advanced-curve-sessions` and its HiDPI variant exercise the actual point
editor, Preview/Cancel/OK, one-step history, stable IDs and native persistence.
Existing profile tests add exact rotated ellipse/arc area, holes, arbitrary
Workplane frames and elliptic Revolve volume. Existing projection, ellipse,
reference and native-document regressions remain applicable. Projection panel
placement sessions run serially on the shared X display, retaining their full
assertions and timeouts, as the existing command-panel positioning tests do.

## Manual Fedora KDE / Wayland acceptance (pending)

Run each at 100% and `QT_SCALE_FACTOR=2`; automated tests do not certify these
manual desktop checks.

1. Activate an oblique Workplane; create an ellipse and clockwise elliptical
   arc. Edit radii/rotation/start/sweep. Confirm exact route, blue Preview,
   Cancel, one-step OK, Undo/Redo, browser names and stable selection.
2. Create an open spline, insert a point, move it, remove it, and specify distinct
   start/end tangent vectors. Verify curve passage, tangent direction and
   Workplane frame growth. Cancel both editor and preview, then confirm.
3. Create a closed periodic spline without a repeated endpoint. Inspect the
   smooth seam; verify repeated endpoints and closed-mode end tangents refuse.
4. Close an open spline with a line; Extrude. Revolve a profile wholly on one
   side of an in-plane axis. Check preview, volume, OK, Undo/Redo. Try crossing,
   open, overlapping, branching and pinched profiles: no model mutation.
5. Build a rectangle with an elliptical hole; Extrude and inspect the hole.
   Test major/minor elliptic arc routes, negative sweep and a rotated Workplane.
6. Project a placed standalone rational B-spline edge. Verify exact trimmed
   endpoints and reversal, duplicate reuse and specific overlapping-reference /
   B-spline-face refusals. Move or delete the source: projection stays independent.
7. Pick ellipse vertices/major direction and spline through-points/end tangents
   for construction geometry and Move Face references. Move the Workplane and
   re-evaluate. Hide/lock the source and verify refusal.
8. Save with a pending edit: reopen a copy and confirm only committed values.
   Save confirmed spline geometry, reopen, edit points again and Undo/Redo.
9. Check browser role/visibility/lock behavior, Workplane Show/Hide, menu text,
   numeric/table focus, HiDPI picking and no leftover preview pixels.
