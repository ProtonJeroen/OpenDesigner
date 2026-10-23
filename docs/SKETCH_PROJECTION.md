# Exact sketch projection core — v0.4.2 phase 2

This phase supplies a computation module, not an interactive command. It projects
vertices and bounded analytic straight edges onto a chosen Workplane as independent
Construction values. Circular edges, arcs, face boundaries and a Project 3D Geometry
selection/preview session remain later work. Popup placement and natural panel
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
checked. An infinite/half-infinite, flagged degenerated, invalid or curved edge
is refused. A perpendicular or nearly perpendicular edge whose projected length
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

No schema extension is needed: projected segments use v3, and documents with
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
