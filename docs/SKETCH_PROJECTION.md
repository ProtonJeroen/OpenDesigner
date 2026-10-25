# Exact sketch projection and interactive command — v0.4.2 phases 2–5

Phases 2–4 supply a widget-independent computation module. It projects
vertices, bounded analytic straight edges, full circles, bounded circular arcs
and actual face boundaries onto a chosen Workplane as independent Construction
values. Phase 5 adds the selection/preview session described below, using this
same API. Popup placement and natural panel
sizing are implemented separately in CommandPanelPositioner; this computation
module does not reposition anything.

## Phase 6A.3 integration

Standalone oblique Circle/Arc and analytic OCCT Ellipse/elliptical-arc edges,
also inside outer/inner face wires, produce exact Ellipse/EllipticalArc values
through the same API. The 6A.3a coefficient decomposition, signed traversal,
conditioning and endpoint certificates remain unchanged. Face walks now check
stored elliptical-arc endpoints and closure before deduplication.

6A.3b compares conic supports analytically using the symmetric principal-radius
matrix. Its spectral-norm difference plus center distance bounds geometric
support deviation within the requested tolerance, without unstable axis matching
near circles. The common phase `w=t+axisAngle` handles axis signs, circular-limit
cross-type equivalence, reversed traversal and periodic intervals. Duplicates
are removed first; positive common intervals on equivalent supports fail with
`PartialOverlap`. Complementary arcs, isolated crossings/tangencies and loose
supports are accepted. Hidden/locked Construction references still participate;
Profile/other-owner geometry does not. The temporary ellipse gates are removed.
Ellipse outputs use existing schema v5; older outputs retain v3/v4 selection.
No tessellation, silhouettes, Profile reconstruction, UI session or schema change
is introduced. The older phase-specific restrictions below are historical.

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
checked. An infinite/half-infinite, standalone flagged degenerated, invalid or
unsupported curve is refused. Face-contained certified poles have the explicit
exception described in phase 4 below. Analytic lines, circles and ellipses are
accepted, subject to the numerical checks above. A perpendicular or nearly perpendicular edge whose projected length
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
was no interactive Project command in phase 3 (phase 5 is below), and ellipse/spline
support or associative source relationships remain absent. No new SolidDesigner behavior is
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

## Phase 4: actual face boundaries

The public `project(...)`, Source and Result transaction contract is retained.
Source.shape now also accepts a Part-local `TopoDS_Face`. One face can yield
many independent Segments, Circles and Arcs; ordinary boundary vertices are not
emitted as extra Construction Points. Explicit vertex sources still create
points. No persistent wire groups, source references, new curve type or document
schema are introduced. These are OpenDesigner computation policies; no new
SolidDesigner workflow or behavior is asserted.

### Complete outer and inner wires

The source face must pass `BRepCheck_Analyzer` and have a bounded outer wire.
Every actual topological wire is traversed, including every hole, using cumulative
face/wire/edge locations. `BRepTools::OuterWire` checks that a boundary exists;
it is not a filter that discards inner wires. No hull, bounding box, silhouette,
hidden-line result or triangulation participates in the projection.

Face orientation is normalized to forward for traversal: reversing a selected
face does not change its boundary's geometric set. Each wire keeps its own edge
traversal, including opposite inner-wire orientation. `BRepCheck_Wire::Closed`
checks topological closure. `BRepTools_WireExplorer` supplies connection order;
every visited oriented edge occurrence must match a direct wire child exactly
once. If the explorer stops early on malformed topology, remaining children
cause `InvalidFaceBoundary`, rather than a silently incomplete contour.

Each ordinary edge uses the same analytic vertex/edge core from phases 2–3:
finite straight edges project to Segments, parallel circular edges to Circles or
signed Arcs, and unsupported/oblique curves fail. The existing separate outer
Part transform is applied once after OCCT has consumed the cumulative edge and
curve locations. Surface locations are used only for the face/pole checks; they
are not reapplied to boundary edges.

Before deduplication, adjacent projected endpoints and final wire closure must
agree within the caller's linear tolerance (default 1e-7 mm). Persisted Arc
endpoints are checked too. A valid B-Rep with looser OCCT vertex/edge tolerances
cannot authorize a larger 2D gap. There is no endpoint snapping, curve extension
or repair to force a wire shut. A planar face projecting edge-on to a line is
refused; circular tilt and ordinary point-collapsing edges retain their previous
refusal rules. Display extent/offset do not limit the analytic projection.

### Seam and degenerated edges

Seam occurrences are real boundary occurrences and are traversed, not mistaken
for additional unrelated contours. Geometrically identical projected pieces
(including reversed occurrences) then use the common duplicate rules. For
example, a conical frustum's two seam occurrences produce one radial segment
alongside its two projected circles. No silhouette or replacement outer loop is
invented. Results are independent construction references, not a reconstructed
profile: on periodic surfaces the boundary walk can retrace a seam, and the
deduplicated geometric set can contain branches or open seam curves. Closure is
verified on the complete original wire walk before removing duplicate references.

A degenerated flag or equal endpoint vertices alone is insufficient proof of
collapse: a full nonzero circle also has coincident endpoints. A flagged edge is
omitted only when all of these analytic checks pass:

- It has no 3D curve and has a finite line p-curve with exactly constant V.
- Its raw surface is a supported analytic sphere or cone. The whole constant-V
  curve is at a spherical pole (`R*cos(V)=0`) or a conical apex
  (`Rref+V*sin(semiAngle)=0`) within angular/arithmetic roundoff.
- Twice the residual circle radius, including surface and Part uniform scale,
  fits the requested linear tolerance. Its two endpoint occurrences coincide
  with that analytic pole within the same placed tolerance.
- The pole still connects the neighboring projected boundary pieces and closes
  the original wire; it does not excuse a missing piece or an empty wire.

The whole p-curve image is certified analytically; samples are not used to infer
collapse. A successful omission emits non-error `DegenerateSkipped`, with face
wire/edge context, and creates no extra point. All other flagged edges fail as
`DegenerateEdge` (or `InvalidBRep` if the source is already invalid). In particular,
generic surfaces of revolution/freeform poles, degenerated flags on nonzero 3D
curves and unverifiable p-curves are conservatively refused. Ordinary nonzero
edges that merely project to points, including cylindrical seam lines viewed
along the cylinder axis, are refused as `CollapsedProjection`; the pole exception
does not silently remove them.

### Batches, diagnostics and storage

All face pieces, explicit vertices/edges and other selected faces enter the same
deduplication/overlap pass. Shared edges, repeated faces, opposite traversal,
distinct topology with equal projected curves and existing hidden/locked
Construction geometry cannot create duplicate references. Profile geometry and
other Workplanes remain distinct. Full duplicates are skipped; nonduplicate
collinear/cocircular interval overlaps fail without trimming or merging.
Isolated contacts/crossings remain allowed. Near-equality uses the existing
linear tolerance, not a new face-size-relative tolerance.

Diagnostics retain the selected input's index/label. Face-piece errors,
duplicate/overlap diagnostics and certified omissions additionally name the
one-based wire/edge occurrence in their message. One unsupported inner edge,
open wire, unsafe degenerate or overlap refuses the entire mixed batch with no
additions. Entirely duplicate batches remain unchanged successes. Returned
UUID/Construction/Workplane values can be appended in one future SketchChange;
Cancel/history/selection/UI are not accessed by this module. They use native v3
for curves only and v4 when points are present; v1–v4 readers are unchanged.

`sketch-face-projection` exercises rectangles, disks, one/multiple holes, mixed
line/arc contours, opposite wire/face orientation, periodic arc limits, independent
Part/Workplane and nested OCCT placements, large common translations, arbitrary
linear projections, repeated/shared/seam edges, certified and unverified poles,
unsupported inner curves, point/line collapse and strict projected closure despite
loose B-Rep tolerances. Mixed atomic requests, source independence, stable
identities, snapping/Overdraw and exact v3/v4 roundtrips complement the existing
vertex/line/circle/arc and native v1/v2 migration regressions.

Phase 4 added no GUI command or panel. General ellipses/splines, silhouettes,
hidden-line removal, mesh/polyline approximations, automatic contour repair and
associative source history remain unsupported. A curved face is accepted only
when all its actual boundary occurrences meet these explicit analytic rules;
support for its surface type does not imply silhouette projection.

Phase-4 validation: complete CMake Release build PASS; all 38 CTests PASS
(85.43 seconds) on isolated Xvfb with xcb/software OpenGL. All three projection
suites also pass with DISPLAY and WAYLAND_DISPLAY unset (0.16 seconds). Existing
native v1–v4, selection/history, command sessions, 2D tools and 3D modeling
regressions are included. `git diff --check` and explicit reviewed-file
artifact/secret-pattern checks pass. No production UI or environment-specific
test workaround was introduced in this phase.

## Phase 5: interactive Project 3D Geometry

Start **Edit Sketch** on a visible, unlocked Workplane. Use **Modeling → 2D
Modeling → Project 3D Geometry**, the same action in the **2D Modeling** toolbar,
or **2D Drawing → Project 3D Geometry** in the viewport context menu. The menu/
toolbar action is disabled without an active target; a direct invocation reports
the required Workplane rather than silently choosing a different plane.

`SketchProjectionPanel` is a nonmodal Qt Tool window managed exclusively by
CommandPanelPositioner (viewport upper-right, 12 logical pixels). It shows the
target name, selection mode, selected source list/count, resulting entity count,
live status and kernel diagnostic details. Use **Remove selected sources** or
**Clear sources**, **OK** or **Cancel**. Its natural form uses the existing
scroll/clamp/drag behavior at small sizes and HiDPI. Selection/preview changes
never call show/move/adjustSize to reanchor a manually placed panel.

### Temporary source picking

`CommandSession::Kind::Sketch` carries an optional `SketchProjectionState` alongside
the same before/after `SketchChange` used by other 2D commands. The state freezes
the Workplane value and holds transient operand records: Part slot/stable ID,
current local root/subshape, outer placement and actual OCCT selected owner.
These records are only session validation data and are never persisted.

Normal Sketch editing deactivates Part picking. This session temporarily
activates `AIS_Shape::SelectionMode` for **Vertex**, **Edge**, **Face**, or all three
in **Mixed** on visible Parts. Existing Workplane, 2D and preview presentations are
deactivated as source pickers. Preview values remain visible in blue; confirmed
2D geometry remains visible in its normal color. Middle-button orbit, Shift+
middle pan and wheel zoom retain the existing navigation.

Click replaces the source set, **Ctrl+click** adds/removes. OCCT owners and its
existing cyan highlighting are authoritative: session operands are captured from
that selection, not from a second independently highlighted selection system.
Membership is checked against the current Part's local topology. Repeated or
reversed occurrences of the same Part subshape become one selected source;
different topology/Parts can remain selected while the kernel deduplicates their
projected geometric results. Whole Parts and 2D entities are not source operands.
Use the viewport to pick subshapes; the Structure Browser does not contain a new
edge/vertex hierarchy. The pane's list and diagnostics identify each source by
its current list number, Part and type.

A mode change retains only sources eligible for the new mode, rebuilds the
corresponding OCCT picker and recomputes the preview. **Mixed** retains all three
supported source types. Clear/removal update selected owners, preview and counts
together. A projection-specific context menu offers Clear, Cancel and Fit All
without reconfiguring the active source picker as an ordinary face-edit command.

### Preview, confirmation and cancellation

Every source selection change calls the unchanged `SketchProjection::project(...)`
with the frozen Workplane and original confirmed 2D collection. The owner's
Part-local shape and separate outer Part transform follow the existing contract;
`SelectedShape()` is not used to accidentally bake the AIS placement twice.
This adapter implements no projection math. It supports the exact types and
conservative refusal rules from phases 2–4, including all inner wires.

Successful additions have separate, nonselectable blue AIS presentations on the
target Workplane, drawn topmost to remain legible on a solid face. Points use the
existing fixed-screen-size marker. Unchanged geometry keeps its preview UUID
across recalculation. A failed batch displays the kernel's source/wire/edge
diagnostics, clears all additions and disables OK; no partial result is presented
as valid. The pane distinguishes selected source count from deduplicated output
count. A fully duplicate valid batch can close with OK without making history or
dirtying the document.

OK/Enter rechecks Workplane and source identity, membership, visibility and
placement, then calls the same kernel again and compares with the displayed
candidate. Changed/in-place edited source geometry cancels rather than silently
committing a different result. Valid unchanged additions retain the preview IDs
and append as one `SketchChange`. One Undo removes the whole projection, one Redo
restores the same values/IDs; the original confirmed 2D collection stays intact.
Normal Sketch picking is restored and confirmed history clears all selection as
before. The Structure Browser is rebuilt through `sketchEntitiesChanged`.

Cancel, Escape and panel close discard all preview and source owners. Safely
pickable pre-command Sketch selection is restored on Cancel/no-op, with no model,
dirty-state or history change. Start of another command cancels first. Target
change/deactivation/deletion, source invalidation, New, successful Open and
accepted document close clean up the session. A refused close or failed Open
keeps the current document intact. Save/Save As capture only confirmed values
and current camera; pending projection remains active with its selection/history.
Results are ordinary independent Construction values: no source links or feature
history. Curves use schema v3, points cause v4, and existing v1–v4 reading remains.

### Reference evidence and validation scope

The supplied v16 Getting Started pages `part_modify_ex1a1.html` (explicit grouped
edge selection), `part_modify_ex34.html` (Preview then OK) and `wp_on_face.html`
(Workplane as a 2D profile context) support those general workflow concepts. They
do not establish this Project command's exact pane, mode labels, shortcuts,
cancel semantics or automatic live preview. Those are explicit OpenDesigner
choices aligned with its current selection and command-session architecture.
No proprietary text/assets are copied into the repository.

`sketch-projection-sessions` and its HiDPI run use real viewport mouse picks and
OCCT owners to cover mode filtering, multi-Part placement, reversed duplicates,
highlighting, point/line/circle/arc/hole preview, atomic failure diagnostics,
stable UUIDs, no-op/OK/Cancel/close, browser rebuild, source/target invalidation,
Save/Save As/Open, shared history and panel drag/resize/docking. Existing geometry
and v1–v4 tests remain mandatory. The source immutability assertion compares exact
B-Rep curves/surfaces/locations/topological connections without mesh caches;
XCAF's `Free` bookkeeping flag is normalized for that test, not changed in the model.

Native Fedora KDE visual/focus/mixed-monitor verification is pending in
[GUI tests A–H](GUI_TEST_PLAN.md). No ellipses/splines, silhouettes, hidden-line
removal, automatic contour repair or associative updates are introduced. On
periodic faces the projected boundary set can contain open/retraced seam curves;
it is not certified as a closed or extrudable Profile.

Phase-5 automated validation: complete CMake Release build PASS; all 40 CTests
PASS (92.48 seconds) on isolated Xvfb with Qt xcb/software OpenGL. Both new session
tests also pass on a 1280×1024 display matching CI (6.43 seconds), including the
200% HiDPI run. Existing geometric projection, v1–v4 storage, command sessions,
selection/history, 2D tools and 3D modeling regressions pass. `git diff --check`
and explicit changed/new text-file artifact/secret-pattern checks pass. Native
Fedora KDE GUI approval remains pending in the linked plan.
