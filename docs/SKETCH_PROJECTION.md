# Exact sketch projection core — v0.4.2 phases 2–3

This phase supplies a computation module, not an interactive command. It projects
vertices, bounded analytic straight edges, full circles and bounded circular arcs
onto a chosen Workplane as independent Construction values. Face boundaries and
a Project 3D Geometry selection/preview session remain later work. Popup placement and natural panel
sizing are implemented separately in CommandPanelPositioner; this computation
module does not reposition anything.

## API and transaction boundary

`SketchProjection.h/.cpp` exposes:

```cpp
SketchProjection::Result SketchProjection::project(
    const std::vector<SketchProjection::Source>& sources,
    const Construction::Object& workplane,
    const std::vector<Sketch::Entity>& existing = {},
    double tolerance = 1.e-7);
```

Each Source supplies an exact OCCT subshape, its separate outer Part placement and
an optional diagnostic label. Result contains `success`, independent `additions`
and structured diagnostics. Diagnostics identify the input index/label and, where
applicable, the related input or existing entity ID. They are temporary computation
data; none is written into the resulting 2D entities.

The module uses OCCT and Qt Core value types, without QWidget, AIS, selection,
camera or command-session access. It neither reads nor mutates a document. The
caller supplies the existing 2D collection and later appends additions to an
unchanged before-snapshot for one SketchChange. The existing collection is never
rewritten. New UUIDs are allocated once for returned entities; retain these values
through preview/confirmation rather than recomputing UUIDs on each redraw.

One invalid source or overlap fails the entire request with no additions. All
input sources are checked so multiple source errors can be reported together.
An empty or entirely duplicate batch succeeds with `changed()==false`; it must
not create history or dirty the document. Context errors (invalid Workplane,
tolerance or existing values/IDs) have no source index. Input order determines
which of geometrically duplicate new references is retained.

## Geometry and placement contract

Source.shape is Part-local, including any accumulated OCCT ancestor/subshape and
curve-representation locations. Source.partPlacement is the separate external
Part transform, matching the existing Construction::resolve/viewport convention.
For an already world-space shape, pass identity placement. The API cannot infer
whether a caller has already baked the Part transform into a shape; doing both
would be a caller error.

`BRep_Tool::Pnt` consumes vertex locations. `BRepAdaptor_Curve` consumes edge and
curve locations, including parameter handling. Their evaluated points are then
transformed by the outer Part placement exactly once. Its translation minus the
Workplane origin is evaluated before adding local coordinates, avoiding loss of
small spans at large common placements (regressions include 1e16/1e20 mm).
No rounded absolute-world-point intermediate is required. Workplane local coordinates
are `x=(P-origin)·X` and `y=(P-origin)·Y`. Dropping the normal component is exact
orthogonal projection onto the Workplane's analytic XY plane. Its finite display
extent and display offset do not clip or shift projected geometry.

Vertices become ConstructionPoint values. A finite `GeomAbs_Line` edge becomes
a Segment between its projected parameter endpoints; reversed edge orientation
reverses those endpoints. There is no polygon sampling, tessellation-derived
geometry, curve fitting or geometric approximation. B-Rep validity, 3D curve
availability, placement, finite parameters/coordinates and source length are
checked. An infinite/half-infinite, flagged degenerated, invalid or unsupported curve
is refused. Only analytic lines and circles are accepted; a circular edge must
meet the parallel-plane rules below. A perpendicular or nearly perpendicular edge whose projected length
is no greater than tolerance is refused as CollapsedProjection, rather than
silently converted into a point.

The default linear tolerance is 1e-7 mm; callers may request finite tolerances of
at least 1e-9 mm, consistent with the existing segment model's minimum length.
Local dot products and interval comparisons use long double intermediates;
coordinates must fit the stored double values within the requested tolerance.
This is analytic floating-point CAD geometry, not arbitrary-precision arithmetic.

## Duplicate and overlap policy

Duplicate points coincide within tolerance. Duplicate segments have matching
endpoint pairs in either orientation within tolerance. Compare against accepted
batch values and existing Construction values on the target Workplane, including
hidden or locked values. Other Workplanes and Profile geometry are distinct
semantic objects and are not suppressed. A point on a line remains an independent
point, rather than a duplicate line or a new trim.

Deduplication happens before overlap checks. Thus a no-op cannot introduce a new
conflict merely because existing references already overlap. Nonduplicate
collinear finite intervals that share positive length (including containment)
fail atomically, even for overlaps shorter than the model tolerance. Only interval
arithmetic noise is allowed at touching endpoints. Collinearity is evaluated with
the requested linear tolerance. A finite segment lying on an existing infinite
Construction line also reports overlap; the module does not trim or delete that
line. Crossings and isolated endpoint contacts are allowed. No automatic merging,
splitting, trimming or choice of representative support is performed.

These refusal, duplicate and diagnostic policies are explicit OpenDesigner design
choices. They are not assertions of undocumented SolidDesigner v16 behavior.

## Storage and regression coverage

Results use the phase-1 SketchGeometry collection, Construction role and target
Workplane ID, with default visibility/selectability. They contain no Part ID,
source topology, persistent dependency or projection history. Editing/deleting a
3D source cannot recompute them. Moving the owning Workplane carries the resulting
2D values like all existing sketch geometry.

No schema extension is needed: projected segments, circles and arcs use v3, and documents with
projected Construction Points use v4. Existing v1–v4 reading, unsupported-schema
refusal and atomic storage remain unchanged. See [native storage](NATIVE_DOCUMENTS.md).

`sketch-projection-geometry` runs headlessly with QCoreApplication. It checks
vertices on/off the plane, straight/inclined/perpendicular edges, negative trim
parameters, reversed orientation, transformed Workplanes, independent Part
placements, nested compound/subshape and stored curve locations, explicit
world-space inputs, duplicate/no-op and overlap policies, source-specific batch
errors and source independence. Deliberately misleading polygon data verifies
that tessellation is ignored. Native roundtrips verify independent stable values
and v3 line-only/v4 point storage; the full suite retains v1/v2/v3/v4 migration,
2D tools/sessions and 3D modeling regressions.

Validation on 2026-10-23: complete CMake Release build PASS; all 33 CTests PASS
(207.21 seconds). This includes the new projection regression, phase-1 points,
v1/v2/v3/v4 storage coverage, drawing/Overdraw/Offset/Common Tangent sessions,
selection/history and existing Pull/Move/Rotate/Align/Delete Face and Boolean
regressions. The projection executable also passed with DISPLAY and WAYLAND_DISPLAY
unset. Tracked/new-file whitespace and changed-file artifact/secret-pattern checks
passed. There is no new interactive projection UI to validate in this phase.

## Phase 3: exact circles and circular arcs

The API and source placement contract are unchanged. `BRepAdaptor_Curve::Circle`
supplies the analytic circle with the accumulated OCCT edge/curve locations.
The existing relative-translation calculation projects its center into local XY;
the external Part's absolute uniform scale multiplies its radius exactly once.
No polygon samples, fitting, persistent source links or new document store are
introduced. Circle/Arc already exist in SketchGeometry and schema v3/v4.

A circular source must be parallel to the Workplane, above, below or on it.
The transformed circle X/Y basis is expressed in Workplane X/Y/normal coordinates.
Only 64 double-epsilon of angular roundoff is accepted in its normal components,
independently of the caller's linear tolerance. An oblique circle that would
project to an ellipse, or an edge-on collapsed curve, fails with
`NonCircularProjection`. A large linear tolerance never authorizes flattening it.
Angular roundoff amplified by a very large radius, the projected unit basis metric
and the converted radius must also be representable within the linear tolerance; otherwise the result is `NumericalFailure`.

Full circles require a complete analytic parameter period
(`last == first + period`), independently of whether the endpoint vertices share
topological identity. A nonzero seam parameter and reversed edge orientation
are supported. A merely near-full interval remains an Arc, even when its endpoint
gap is smaller than the model tolerance. An interval that is neither a complete
period nor shorter than one turn fails with `UnsupportedCircularSpan`.
Radius and arc length must exceed the requested linear tolerance, and arcs must
satisfy the existing model's minimum absolute sweep of 1e-12 radians. Invalid
B-Rep, non-finite values, degenerated flags and unsupported orientations are refused.

An Arc stores center/radius, a normalized local start angle and signed sweep.
The determinant of the projected circle basis gives local handedness. Reversed
OCCT edges start at the last parameter and negate traversal; reversing the target
normal or reflecting the Part can also reverse local CW/CCW. The original parameter
span is retained, so 90/180/270-degree arcs and 0/360 crossings cannot accidentally
switch to their complementary route. Both converted endpoints are checked against
exact OCCT evaluations within the requested tolerance. There is no new orientation
flag or change to the existing signed-sweep data model.

### Circular duplicates and overlaps

Circular supports coincide when center distance and radius difference are within
the existing linear tolerance. Full circles ignore seam/orientation for duplicate
detection. Arcs additionally compare their canonical CCW domain start and absolute
sweep using arc-length tolerance. Reversing traversal leaves that domain unchanged;
opposite semicircles or a small arc and its large complement are not duplicates.
A bounded arc never duplicates a complete circle.

Nonduplicate arcs on a coincident support are split into at most two analytical
angular intervals at the periodic seam. Any positive shared angular interval above
64 double-epsilon times one turn (capped at linear tolerance divided by radius)
is refused, even if shorter than the model's linear tolerance. That angular
allowance handles parameter/trigonometric arithmetic noise at shared endpoints; it does not authorize trimming. Circle/arc containment
and all other cocircular containment likewise fail as `PartialOverlap`. Endpoint
contacts, isolated crossings and circles with genuinely different supports remain
allowed. Hidden/locked Construction references on the target Workplane participate
in these checks; Profile geometry and other Workplanes remain distinct.

Duplicate checks still precede overlaps. One invalid source or unsafe overlap
empties the entire result, with source-specific diagnostics. Mixed vertex/line/
circle/arc requests return independent UUID-bearing values for one future
SketchChange transaction; a wholly duplicate batch remains an unchanged success.

### Phase-3 coverage and remaining scope

`sketch-circular-projection` covers parallel and displaced circles, CW/CCW
90/180/270-degree arcs, reversed sources/normals, periodic boundaries, full versus
near-full intervals, independent Part/Workplane placements, nested OCCT and stored
curve locations, reflection/uniform scale and large common translations. It also
covers duplicate/no-op, overlap/containment/contact, degeneracies, oblique/edge-on
refusal, mixed atomic batches, UUID/role/ownership and source independence. Native
v3/v4 roundtrips and existing snapping/tangent/Overdraw compatibility use the same
ordinary SketchGeometry values. The previous projection suite now uses an ellipse
as its unsupported-curve fixture; circular edges are intentionally supported.

The full regression suite retains v1/v2 migration and v3/v4 storage, selection,
command sessions, Undo/Redo, panel sizing and existing 3D modeling coverage. There
is still no interactive Project command, face-boundary projection, ellipse/spline
support or associative source relationship. No new SolidDesigner behavior is
claimed; these are explicit OpenDesigner computation and safety policies.

Phase-3 validation: complete CMake Release build PASS; all 37 CTests PASS
(84.91 seconds) on isolated Xvfb with xcb/software OpenGL. The new circular
projection executable also passes with DISPLAY and WAYLAND_DISPLAY unset.
Existing v1–v4 storage, 2D tools/history, panel sizing/HiDPI and 3D regressions
are included. `git diff --check` and reviewed-file artifact/secret-pattern checks
pass. Earlier runs on the active KDE desktop failed existing panel focus/window
position assertions; those unchanged tests pass in the isolated X11 environment.
The construction-session test file chooser now explicitly enters its directory
and filename: a debugger trace identified a stale Save As name causing an
unattended overwrite prompt. The test asserts the resulting document path;
product dialogs and panel behavior are unchanged.
