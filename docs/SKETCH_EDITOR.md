# 2D Modeling / Sketch Editor — v0.4.1 and v0.4.2 foundations

Phase 1 supplies document values, native storage, presentation, browser management
and shared history. Phase 2 adds explicit Workplane editing, interactive and
numeric drawing, analytic snapping and live feedback. Phase 3 adds explicit
Overdraw of bounded Construction portions into independent Profile geometry.
The 2D Construction additions supply offset lines and common tangent lines using
the same values, analytic helpers, viewport and transactions.
Profile validation, 3D profile operations, splines, ellipses and constraints remain
outside this release. General 3D-to-2D projection of vertices, edges and face
boundaries is scheduled for v0.4.2. Existing Point projection creates an
independent world-space 3D construction Point on a plane; it is not a 2D sketch
entity, does not supply sketch snaps and does not follow subsequent Workplane
transforms. No associative projection or feature history is implied.

## Model and ownership

`SketchGeometry.h/.cpp` defines a `std::variant` of exact local-XY segment,
infinite line, circle and circular arc values. Each entity has a UUID, Workplane
UUID, optional name, Construction/Profile role, visibility and selection lock.
An infinite line requires a unit direction and the Construction role. Circles
have positive radii; arcs store a start angle and signed sweep in radians
(counterclockwise positive, less than one revolution). A full revolution is a
Circle. The discriminated representation permits additional curve types later;
schema-v3 type tags 0–3 are fixed and must never be reassigned.

v0.4.2 phase 1 appends `ConstructionPoint { position }` as tag 4, with exact local
XY and the same identity, Workplane ownership and flags. It is an independent
Construction entity, never a Profile curve. Its finite coordinates are validated;
assigning the Profile role is refused. The lightweight `Sketch::Point` coordinate
pair used by analytic helpers remains distinct from this persistent entity type.

The document's entity collection is partitioned by owning Workplane ID, giving
one collection per Workplane and any number of contours. There is no separate
Sketch feature, constraint graph, source geometry reference or feature history.
Role is explicit, independent of the curve type. Future profile extraction must
filter Profile entities before topology construction; Construction entities must
never close or split a machining profile.

World positions use `origin + X*x + Y*y` from the analytic Workplane frame.
`extent` and `displayOffset` never change local coordinates. Moving/rotating a
Workplane reprojects its contents, including temporary blue previews; confirmation
does not rewrite entity coordinates or identities. Hiding/locking a Workplane
also hides/disables viewport selection of its contents without changing their
own flags. Deleting a Workplane deletes its contents in the same reversible
history step. Creation references for the Workplane remain evaluated once.

## Viewport, browser and transactions

`SketchTools.cpp` adapts curves to exact OCCT edges. Profile curves are white
and solid; Construction curves are pink and dashed. Infinite lines are clipped
only for display, using the Workplane extent, and excluded from automatic OCCT
infinite-object fitting. Construction-only Fit All additionally considers finite
display bounds and the drawn curves. Labels and rendering are never storage data.

Local Construction Points use exact OCCT vertices and pink circle-plus markers
with a fixed screen size; temporary point previews are blue. Their analytic
positions follow the owner frame, independent of its display offset. Browser,
viewport selection, visibility, selection locks, owner deletion and mixed 2D/3D
history reuse the same entity path as curves. Hidden/locked owners suppress
point picking; hiding an owner also suppresses its markers.

2D browser rows are children of their owning Workplane, keyed by entity UUID.
They support rename, an eye control, Show/Hide, selection lock, Delete and Fit
Selected. Normal click/Ctrl picks typed 2D sets. Alt Part selection passes through
2D objects. The typed selection resolver does not treat sketch edges as Part
Faces or permit them to enter 3D modeling operations. Hidden/locked entities
remain manageable in the browser.

`previewSketchEntities` validates a complete candidate collection and creates a
separate blue presentation; `finishSketchEdit` confirms or cancels it. The existing
command-session OK/Enter and Cancel/Esc routing is reused. Confirmed changes carry
before/after `SketchChange` values through the existing chronological `PullChange`
history alongside 3D operations. A changed confirmation is one Undo step; a no-op
or Cancel creates none. Undo/Redo clears all selection types and browser selection.
Workplane deletion carries both construction and 2D snapshots in one entry.

Only confirmed entities are saved, even during 2D or Workplane previews. Save/Save
As retain both history stacks and selection. Successful New/Open clears history,
all previews and selection and rebuilds presentations/browser rows. Invalid input
and failed Open do not replace the current document.

## Workplane presets and Construction Axes

Modeling → Construction Geometry → Workplane includes **XY Workplane**,
**XZ Workplane** and **YZ Workplane**, alongside the existing creation methods.
No Part or reference selection is required. The default origin is (0,0,0).
**Offset along normal** moves the origin by the signed distance along the displayed
normal, including after **Reverse normal**. The right-handed preset frames are:

| Preset | Local X | Local Y | Normal |
| --- | --- | --- | --- |
| XY | world +X | world +Y | world +Z |
| XZ | world +X | world +Z | world −Y |
| YZ | world +Y | world +Z | world +X |

These are ordinary Workplanes, with temporary preview, OK/Cancel, one Undo step,
stable identity, numbering and native storage. After OK the Workplane is selected;
**Edit Sketch** activates it immediately. No new persistence type is introduced.

In Edit Sketch choose **Construction Axes** in the 2D Modeling menu/toolbar or
viewport's 2D Drawing menu. Choose **Local X axis (Y=0)**, **Local Y axis (X=0)**
or **Both**. These create ordinary infinite Construction entities at local origin,
with exact unit directions (1,0)/(0,1), independent UUIDs and Workplane ownership.
Blue is temporary; OK/Enter confirms all missing requested axes as one history
step, while Cancel/Esc creates none. Both does not mean two Undo steps.

Existing infinite Construction lines on the same analytic axes count as duplicates
within the sketch intersection tolerance (1e-7 mm), regardless of name, direction,
origin along the line, visibility or selection lock. A no-op adds no history and
does not dirty the document. It does not unhide/unlock an existing axis. Finite
segments and other Workplanes do not count as the same axis; lines offset by
more than the geometric tolerance are distinct.

Axes use ordinary selection, visibility, snapping, intersections, offset, Overdraw
and schema-v3 Save/Open. Moving/rotating the owner carries their display without
changing local values. For Overdraw, two axes alone provide no bounded span; add
other actual intersections such as a circle or crossing lines. The visual frame
arrows remain presentation aids and are distinct from these editable 2D entities.

## Interactive drawing (phase 2)

Create a Workplane through Modeling → Construction Geometry first. Select it and
choose **Edit Sketch**, or choose it in the **2D Modeling** toolbar. With several
Workplanes present, no implicit choice is made. The active Workplane is bold in
the browser; the toolbar selector switches context and cancels pending geometry.
**Look Normal** aligns the camera to its local XY frame and fits its display.
Activation, navigation and snap settings never dirty the document. **Finish 2D**
returns to ordinary 3D picking. Starting a 3D modeling command also leaves 2D mode;
Alt-click passes through to Part selection when no drawing command is pending.

The toolbar and Modeling → 2D Modeling expose **Line**, **Infinite Construction
Line**, **Circle**, **Arc (center/start/end)** and a Construction geometry toggle.
The floating editor also permits choosing Construction/Profile per element;
infinite lines always have the Construction role. Each command creates one entity:

- Line: pick start and end, or enter start XY followed by length/angle.
- Infinite line: pick two distinct points or enter origin XY and length/angle.
  The length establishes direction only; the stored line is unbounded.
- Circle: pick center and radius point, or enter center XY and radius.
- Arc: pick center, start/radius point, then end direction. Radius stays fixed;
  the last point defines a direction on the support circle. Choose explicit
  **Clockwise (CW)** or **Counterclockwise (CCW)** for mouse and numeric input.
  A positive numeric sweep amount follows that choice; a negative amount selects
  CW for compatibility with signed input. A zero or full-turn sweep is refused;
  use Circle for a full turn. Changing direction with fixed endpoints chooses
  the complementary short/long arc between those same endpoints. The displayed
  signed sweep and live preview follow the chosen direction. Role changes never
  reinterpret or round an already entered numeric sweep.

**Use XY point** accepts absolute Workplane coordinates. **Use length/angle**,
**Use radius/angle** and **Use sweep angle** accept the corresponding numeric
values. Angles are degrees in the editor and radians in the model. Fields accept
localized numbers with nine decimals, positions up to ±1e9 mm and positive
dimensions from 1e-6 to 1e9 mm. Enter in XY accepts a point; Enter in a dimension
accepts polar input. Once the candidate is complete, **OK/Enter** commits one
history step. **Back one point** revises input. **Cancel/Esc** or switching tools
discards all pending points, presentations and feedback. Save excludes them and
retains the current command and history. New/Open clear the editing context.

Blue preview curves and an origin marker show pending geometry, while existing
entities keep their Construction/Profile styling. An orange marker and named
cursor readout identify the current snap. XY and distance/angle/radius readouts
follow the cursor. Middle-button orbit, Shift-middle pan and wheel zoom remain
available during drawing; an edge-on Workplane refuses picks until the view is
usable again. There is no automatic chain of segments or persistent constraint.

## Analytic snapping

`SketchSnapping` is independent of widgets and OCCT rendering. Eligible sources
are visible, unlocked 2D entities on the active Workplane, in either role. Targets
are segment/arc endpoints and midpoints, circle/arc centers, bounded intersections
of lines/segments/circles/arcs, and nearest points on curves. Tangent contacts are
available when drawing a line from an external anchor to a circle/arc; starting
on a circle/arc also permits its tangent direction. This phase does not implement
the reference's dedicated tangent-circle construction commands or 3D B-Rep snaps.

Intersections respect segment and signed-arc domains, including periodic seams.
Tangencies yield one point; disjoint/degenerate curves yield none. Coincident
overlapping curves report coincidence and generate no arbitrary intersection
nodes. The analytic double coordinates are stored directly, without rounding to
screen pixels. Extended intermediates reduce cancellation in intersection math.
Intersection targets are cached for a command; hover-dependent projections and
tangents are calculated per move.

The toolbar **Snapping** toggle and **1–100 px** radius control acquisition in
screen space, independently of zoom; Shift temporarily bypasses it. Named point
targets inside the radius take priority over nearest-curve projections, then
screen distance and deterministic target-kind ordering resolve ties. The final
arc direction snaps only to targets on its fixed support circle. Snapping creates
independent coordinates, never a persistent relation to its source.

## Offset and common tangent Construction lines

Activate a visible, unlocked Workplane and use **Offset Construction Line** or
**Common Tangent Construction Line** in the 2D Modeling toolbar, Modeling menu or
viewport's 2D Drawing submenu. Both tools always produce the Construction role.
Compatible preselection fills operand roles; otherwise use **Pick**, a named
reference list, or a Structure Browser row while that operand is armed.
Hidden/locked objects, other Workplanes and invalid types cannot supply references.
Multiple curves within the acquisition radius require an explicit list choice.

### Offset Construction Line

Choose a finite Construction/Profile segment or an infinite Construction line.
Projected straight 3D geometry already represented as such a 2D value is equally
eligible; provenance and creation history are ignored. This command does not
itself project or pick raw 3D B-Rep edges.

Enter a **Signed offset** in millimetres. Positive means left of the directed
source in local Workplane XY: `normal = (-direction.y, direction.x)`. Negative
means right. The **Side** list and signed field stay synchronized. Enable
**Choose side with mouse**, point on the desired side, then click to lock it;
the mouse changes the sign only, keeping the exact numeric magnitude. Click does
not commit: **OK/Enter** confirms and **Cancel/Esc** discards the preview.

Choose **Infinite** (default) or **Finite** output. Finite output from a segment
retains its endpoint placement, length and direction, shifted perpendicularly.
Infinite output stores a unit direction and offset origin. Finite output from an
infinite source requires **Use explicit position and length**, a signed start
parameter along the source from its stored origin, and a positive length.
Displaying the default interval fields is insufficient: the checkbox is required.
Display bounds never invent a finite span.

Blue output is temporary; no source moves. Reversing a source reverses its
Left/Right convention. Zero/tolerance-sized offsets, invalid intervals,
non-straight inputs and results that cannot represent the requested position,
length or displacement within tolerance are refused without a stale blue preview.
The fixed linear tolerance is 1e-7 mm. Numeric fields use nine decimals and the
existing ±1e9 mm distance/position range.

### Common Tangent Construction Line

Choose two different Construction circles/arcs; Profile circles/arcs are excluded.
Available lines are green, nonselectable previews. Choose explicitly in the list
or click a uniquely acquired green line. The chosen line is blue, with orange
contact markers. Even one available solution requires explicit choice. Changing
references or Infinite/Finite output clears the choice and prevents stale OK.

- Disjoint circles supply two external and two internal tangents, including
  unequal radii. Intersecting circles supply the two external tangents.
- External contact supplies three infinite tangents; the internal one has
  coincident contacts and no finite segment. Internal contact supplies one
  infinite tangent and no finite span. A finite output always runs exactly from
  contact 1 to contact 2, without arbitrary extension.
- Strictly nested circles and unequal concentric circles have no common tangent.
  Coincident circles and overlapping arcs on a shared circular support have no
  unique discrete solution and are refused. Supports indistinguishable within
  tolerance are refused unless exact shared-support domains resolve them.
- Arc solutions respect both signed trim domains, including endpoints and seams.
  Exact shared-support arcs meeting only at one or two endpoints supply the
  corresponding infinite tangents; disjoint domains supply none. Their zero-length
  finite counterparts are excluded.
- Analytic normal equations use extended intermediates near tangency. Negative
  discriminants are not clamped into invented tangents. Duplicate contacts/line
  directions within tolerance are merged. Invalid input, unresolved precision or
  no domain-valid solution leaves the command open with an explanation.

`SketchConstruction.h/.cpp` supplies widget-independent calculations.
`SketchConstructionTools` and its panel use the existing Sketch session and
before/after `SketchChange`. Operand IDs, alternatives and mouse state are
transient. OK adds one independent entity with a new UUID and one shared Undo
entry. Cancel, tool/owner switches, New and Open remove all panels, overlays and
markers. Camera navigation preserves the command. New/Open reset history and
selection; Undo/Redo clear selection through the existing chronological history.

Save/Save As retain the command/history and serialize confirmed values only.
Both outputs already fit schema v3; no new records or source links are stored.
Later source edit/deletion never recomputes results. New lines immediately supply
ordinary snapping and Overdraw. Overdraw's bounded-piece rules remain in effect,
including its 0/1-cut circle limit. Reference/candidate acquisition reuses the
screen-pixel radius and analytic nearest-point helper. Disabling free-drawing
Snapping or holding Shift does not permit off-reference operands for these tools.

### Reference evidence for these additions

The supplied v16 Getting Started HTML was searched for offset/parallel/tangent
workflows. `construct_geom.html`, `mirror_exercise.html`, `2d_copilot.html`,
`2D_3_commands.html` and `2d_geometry_taskbar.html` were read. Confirmed: finite
and infinite Construction lines; distance/angle/radius feedback; numerical input
and OK; tangent **circle** examples under Tan by 2 / Tan by 3. Those examples
create circles tangent to references, not common tangent lines between circles.
The mirror exercise confirms parallel-line guidance and snapping, but does not
document a standalone offset-line command.

No precise offset-line or circle-to-circle common-tangent-line command sequence
was found in this reference set. Named operand lists/Pick, signed Left/Right and
mouse-lock choices, explicit finite intervals, default Infinite output,
green alternatives/blue choice, contact handling and refusal policy above are
OpenDesigner decisions implementing the approved request. They are not claimed
as undocumented SolidDesigner behavior. Proprietary reference assets, models,
text and code are not copied into the repository.

## Overdraw (phase 3)

With a visible, unlocked Workplane active, choose **Overdraw** in the 2D Modeling
toolbar, Modeling → 2D Modeling, or the viewport's 2D Drawing menu. It is disabled
outside 2D editing. Output is always **Geometry / Profile**, independently of the
drawing toolbar's Construction toggle.

1. Pick a meeting point or finite Construction endpoint. Orange marks the proposed
   start; a blue marker tracks the current endpoint after selection.
2. Point along a connected, unchosen portion. A unique candidate is green. Click
   it, or press **Choose green piece**, to add a blue temporary profile and advance
   to its other endpoint. No complete route is selected automatically.
3. Where several portions are within the pixel radius, none is guessed. Explicitly
   choose one in the list, preview it green, then click/Choose. This also applies
   to starting nodes that overlap on screen when zoomed out. Moving farther along
   a portion can resolve the screen ambiguity.
4. **Back one piece / Backspace** removes the last choice and restores its starting
   endpoint. Repeat to correct an earlier choice. **New start** retains the chosen
   portions and allows another chain or contour in the same transaction.
5. **OK/Enter** creates all chosen Profile entities in one shared Undo step.
   **Cancel/Esc**, closing the tool window or switching Workplane/tools discards
   every pending choice. An empty/error command cannot confirm. Save retains the
   command and stores only previously confirmed geometry; Open/New clear it.

`SketchOverdraw` builds a transient graph from visible, selectable Construction
entities on the active Workplane only. Profile entities and other Workplanes do
not supply cuts. It reuses `Sketch::intersect` and analytic nearest-point picking
from phase 2. Cuts come from actual intersections/tangent contacts and natural
finite endpoints. Sorted parameters split segments, infinite lines, circles and
signed arcs. Source circles produce separate circular arcs; source arcs retain
their support and trim domain. Traversing backwards reverses segment endpoints
or signed sweep, without changing the curve. The graph and picking source IDs
exist only during the command, not in `.odc` or the resulting entities.

The same directed candidate is now used for hit testing, green preview, candidate
labels and blue Profile output, starting at the current graph endpoint. A reverse
Arc uses `start + sweep` and `-sweep`: it retains the absolute angular extent and
support circle instead of selecting the complementary arc. The graph is
undirected; either endpoint can begin traversal, independently of source CW/CCW.
Mixed-orientation arcs and reversed lines still connect at the shared analytic
nodes. Multiple possible portions still require explicit choice.

`SketchOverdrawTools` adapts that graph to the existing Sketch command session and
before/after collection. Nonselectable green hover and blue chosen AIS overlays
are distinct from immutable source presentations; the browser changes only after
confirmation. Source geometry remains independent and unchanged. Later editing
or deletion of a Construction source never changes a confirmed Profile entity.
Workplane transforms still carry both roles through their common local frame.

### Boundaries and refusal policy

- Infinite lines yield only intervals between two different actual cut points.
  Unbounded tails are excluded. Display extents and Workplane borders never
  become endpoints. Two crossing infinite lines alone therefore yield no pieces.
- Finite segments and arcs can use their natural endpoints. Circle cuts are
  periodic: the seam is not an artificial boundary. A circle with fewer than two
  distinct contacts/intersections cannot yield a bounded non-full-turn Arc, and
  is excluded with an explanation. An isolated circle or two circles touching at
  a single point alone therefore cannot be overdrawn in this phase.
- Line/circle and circle/circle tangencies are real single cut points, not two
  almost-equal intersections. With additional boundaries they split usable
  portions normally. Signed, seam-crossing arcs respect their exact trim domains.
- Coincident/overlapping eligible Construction sources make the command
  noncommittable, with a request to hide/remove redundant sources. No arbitrary
  overlap ownership or fabricated endpoints are chosen. Touching but disjoint
  collinear segments or same-support arcs are permitted.
- Model acquisition uses a fixed linear tolerance of 1e-7 mm. Coincident cuts are
  merged; tolerance-sized/invalid pieces are excluded. Invalid sources or duplicate
  source IDs refuse the graph transactionally. Each candidate can be chosen once
  per command. Different circle portions between the same two nodes stay distinct.
- The configured 1–100 px radius controls node/piece acquisition across zoom
  levels. Overdraw still picks exact boundaries when the free-drawing Snapping
  toggle is off; it does not support off-curve anchors or Shift bypass.

The command creates independent portions, not a certified closed machining loop.
It does not validate final closure, self-intersection, branches, tangent loops or
overlap with already existing Profile geometry. Repeating Overdraw can create
overlapping profiles; closed-profile validation remains a later phase. No spline,
ellipse, persistent constraint or feature-history dependency is introduced.

## Native schema v3

This section describes the v0.4.1 curve format. The v0.4.2 point extension below
retains it for documents without points and adds schema v4 when necessary.

The single BinXCAF `.odc` file uses `OpenDesigner.RootAssembly.v3` and schema
integer 3, preserving the previous exact Part geometry, camera and construction
records. A required `OpenDesigner.Sketch.v1` metadata group contains one collection
per owning Workplane, with per-entity IDs, names, flags, fixed curve type tags,
original collection order and exact double parameters in OCAF attributes.
No viewport tessellation or feature history is stored.

### Schema v4 and point isolation (v0.4.2 phase 1)

Current snapshots support v4 in memory. Native Save writes v4 if any local
Construction Point exists, otherwise v3. Readers accept v1/v2/v3/v4 and upgrade
the snapshot capability without changing coordinates. Tag 4 stores exactly two
double values (X/Y) in the existing sketch metadata layout under a v4 root.
Tags 0–3, ownership and flags are unchanged. Explicit v3-with-points saves, tag-4
records in v3 files, Profile points, non-finite coordinates and future schemas
are refused before file/document replacement. See [Native documents](NATIVE_DOCUMENTS.md).

Visible, selectable points on the active Workplane supply exact strong snap
targets labelled **Construction point**, using the existing screen-pixel radius.
Existing endpoint/midpoint/center/intersection/tangent tie priorities are retained;
nearest-curve targets remain weaker. Owner visibility/locking is enforced by the
editing context, and entity visibility/locking by snap acquisition. A line snapped
to a point stores independent exact coordinates; deleting the point does not
delete or drive that line.

Points do not become curve intersections, tangent/offset references, Overdraw
sources or cuts. Overdraw skips them; Offset/Common Tangent refuse them. They
cannot be Profile geometry or close a contour. This phase exposes point values
through the existing candidate/SketchChange API and native files; no interactive
point-creation tool or Project 3D Geometry command is added yet. Projection and its
selection/session UI remain later phases. These are OpenDesigner design choices,
not additional claims about SolidDesigner v16 behavior.

### Camera capture without Parts

Empty, Workplane-only and 2D-only documents are valid native documents. No fake
Part or solid is needed. A GUI regression reproduced the previous failure on a
new XY Workplane after Look Normal: OCCT FitAll of a plane normal to the view set
Eye and Center both to `(0,0,0)`, while keeping scale 210 and an independent valid
Direction. The existing camera record derives its direction from Eye/Center;
that collapsed pair could not represent the actual view and was correctly refused
by storage validation. Earlier storage tests supplied a valid camera explicitly
and did not exercise this fit/viewport transition.

`CadViewport::stabilizeViewCamera` now keeps a representable positive Eye/Center
separation after fit operations, using OCCT's retained direction and preserving
center, up vector and orthographic scale. Clipping is refitted normally. Snapshot
capture also canonicalizes a camera copy as a safeguard for raw/external OCCT
fits, without modifying the live view or changing document selection/history.
Serialization remains strict: malformed camera metadata with no recoverable
direction is still refused. The schema and camera record format are unchanged.

The Arc model likewise needs no new flag/schema: center, radius, start angle and
signed sweep already encode exact CW/CCW geometry. `directedArcSweep` computes
the requested periodic extent; `reversedArc` reverses traversal without changing
the selected short/long locus. The viewport no longer opens the legacy 3D numeric
panel on a Sketch mouse release. Drawing, Overdraw and Construction-line tools
keep their own controls and keyboard focus.

Readers accept schema v1 and v2, initializing an empty 2D collection. Writers may
emit legacy schemas only when their unsupported collections are empty. Unknown
root schemas, unknown curve types, missing/duplicate groups, orphan ownership,
duplicate IDs, invalid role/visibility/lock flags, invalid record sizes and
degenerate/non-finite geometry are refused. Existing staged loading and atomic
`QSaveFile` replacement remain in use. Older applications refuse the v3 marker
instead of silently discarding sketches.

## Reference boundary and later phases

SolidDesigner/Modeling v16's local Getting Started documentation is the workflow
reference. `construct_geom.html` confirms finite/infinite construction lines,
arcs/circles, Cross and angle-line exercises, and tangent-circle construction
examples. `2d_copilot.html` confirms cursor distance/angle/radius information,
blue origin marks and orange snap highlighting. `2d_customize.html` confirms
customizable copilot settings, including midpoints and circle extremes. These
pages do not fully specify arc gestures, snap precedence or numeric editor
behavior. Circle-extreme snaps and dedicated tangent-circle tools are not
implemented in this phase.

The existing OpenDesigner architecture supplies Workplane frames, independent
typed entities, command sessions, shared history and native storage. Explicit
Edit Sketch/Finish 2D, toolbar placement, center/start/end arcs, numeric polar
input, Back, screen-pixel acquisition and target precedence are OpenDesigner
implementation choices. They are not claims about undocumented original behavior.

For phase 3, `contour.html` confirms **Contour → Overdraw**, **Geometry** output,
starting at a Construction meeting point, pointing/clicking portions, green
proposed and blue chosen geometry, and OK producing solid white profile geometry.
`2D_types.html` separates Construction, Contour and machinable 2D geometry.
`profiles1.html` requires closed profiles for later extrusion; `profiles6.html`
identifies gaps, overlaps, branches, intersections and tangencies as invalid
machining profiles. Overdraw here does not claim to perform that later validation.

Source retention and independent results are approved OpenDesigner choices,
not proven v16 behavior. Natural-endpoint starts, explicit ambiguity lists,
Back/New start, fixed tolerances, the overlap refusal policy and the requirement
for two distinct circle cuts are also OpenDesigner implementation choices. The
supplied introductory reference does not establish those exact rules. No copied
proprietary text, assets, course models or source files are included.

## Verification

`sketch-geometry-storage` checks analytic types, local/world coordinates, signed
arcs, precision, invalid geometry, multiple Workplanes, every supported role/type,
exact metadata roundtrips, schema-v1/v2 migration, real malformed/future BinXCAF
records and failure preservation. `sketch-sessions` exercises temporary previews,
Esc, browser/viewport picking, role styling, visibility/locks, Workplane transforms
and deletion, mixed 2D/3D history, Save/Save As, failed import, Open/New and cleanup.
Existing modeling and document CTests remain enabled.

`sketch-snapping` adds symmetric analytic intersection tests, tangent and
degenerate/coincident cases, trimmed signed arcs, large/near-parallel coordinates,
source eligibility and pixel-radius acquisition across zoom levels.
`sketch-drawing` drives the real GUI actions, fields, keyboard and mouse events:
all supported types/roles, exact numeric values, projected Workplane picking,
snap markers at multiple zoom levels, Shift bypass, Cancel/Undo/Redo, Workplane
switches, edge-on recovery, Save/roundtrip and Open/New cleanup.

GUI suites require an X11/OpenGL session. Automated GUI events do not establish
native Fedora KDE visual/focus behavior. Manually check toolbar availability with
your window size/scaling, localized numeric entry, blue previews and orange snap
markers; draw both roles on rotated Workplanes, navigate during a command, switch
Workplanes, then Cancel/OK/Undo/Redo and Save/Open. Repeat Move/Rotate/Delete Face
and Boolean commands after Finish 2D.

Validation on 2026-10-21: the complete CMake Release build succeeded and all
22 CTests passed (183.77 seconds), including the new snapping/drawing suites and
the existing modeling/document regressions. The drawing suite also checks camera
navigation without confirmation/dirty state, fixed-radius arc feedback and normal
selection restoration after Open from an active Workplane. `git diff --check` and
whitespace checks of all new source/documentation files passed. Native Fedora KDE
visual/focus validation of phase 2 remains a manual check.

Before phase 3 the user reported all eight phase-2 GUI practice checks passing on
Fedora KDE. That validates the earlier drawing workflow, not the new Overdraw UI.

`sketch-overdraw-geometry` checks line/circle/arc splitting, true tangent contacts,
infinite-line bounds, negative/seam-crossing arcs, coincidence/degenerate refusal,
separate circle portions, ambiguous acquisition, pixel distances, numerical
precision, source independence and exact schema-v3 roundtrips.
`sketch-overdraw-sessions` drives the actual actions, viewport events and controls:
green/blue offscreen rendering, explicit ambiguous piece/start selection, Back and
New start, immutable sources, one-step history, mixed 3D Move history, Save during
preview, failed/successful Open, source edit/delete independence and cleanup.

Phase-3 validation on 2026-10-23: the complete CMake Release build succeeded;
all 24 CTests passed in the final full run (189.49 seconds), including both new
Overdraw suites and the existing drawing, construction, storage, Boolean,
Move/Rotate Face, Delete Face, selection and history regressions. Offscreen
viewport rendering verified green candidate and blue chosen curves. The expanded
GUI test's zoom fixture was corrected before this run. A construction-session
timeout in an earlier run did not recur in either the focused or final full run.
`git diff --check` and whitespace checks of new source/documentation files passed.
The user subsequently confirmed the latest nine Fedora KDE GUI regressions on
2026-10-23. The checklists below are retained as repeatable procedures.

Manual Fedora KDE checks for phase 3:

1. Draw a finite Construction line through a Construction circle. Start at a
   crossing, hover/select one arc and the connecting straight portion. Check
   green/blue colors and white Profile output after OK; dashed sources must remain.
2. Use two intersecting circles. At their crossing explicitly choose a proposed
   arc from the list, or move along it. Verify that the other arcs are not selected.
3. Pick a wrong piece, use Back/Backspace, then New start for another chain.
   Cancel the whole command, repeat and confirm; check one Undo/Redo step and
   complete selection cleanup.
4. Try a tangent line/circle and tangent circles with no other boundaries; verify
   the explanations. Add another intersecting line and retry. Test coincident
   sources and two crossing infinite lines without artificial Workplane cuts.
5. Repeat on a moved/rotated Workplane, at different zoom levels and KDE scaling.
   Save during selection, Open the saved file, switch Workplanes and use New.
   Confirm that only accepted profiles survive. Recheck ordinary drawing and 3D
   Move/Rotate/Delete Face and Boolean commands after leaving 2D mode.

Construction-line extension regression coverage:

`sketch-construction-geometry` checks positive/negative offsets, finite/infinite
sources and outputs, explicit intervals, directed parallelism, rotated frames,
precision refusal, every external/internal tangent, unequal radii, touching,
intersecting/nested/concentric/coincident circles, signed/endpoint/shared-support
arc domains, duplicate solutions, operand reversal, large translated/rotated
coordinates, exact schema-v3 roundtrips, snapping and use as Overdraw sources.
`sketch-construction-sessions` drives real actions, fields, browser/viewport picks,
mouse side locking, explicit solutions, green/blue rendering, invalid preview
removal, OK/Cancel/Enter/Esc, source independence, Undo/Redo/selection, confirmed
Save during previews, Save As, failed/successful Open, tool/Workplane switching,
New and complete cleanup.

Construction-line validation on 2026-10-23: the complete CMake Release build
succeeded and all 26 CTests passed (168.68 seconds) in the final full run.
The geometry suite includes sub-tolerance shared-support arc overlaps, which must
not be mistaken for unique endpoint contacts. The GUI suite verifies green/blue
preview rendering as well as transactions and file lifecycle. Existing Overdraw,
drawing, storage, construction, selection and 3D regressions passed.
`git diff --check` and whitespace checks of new source/documentation files passed.
The user subsequently confirmed the latest nine Fedora KDE GUI regressions on
2026-10-23, covering the Construction-line additions and reported bugfixes.

Storage/Arc/traversal bugfix regression coverage:

- `native-partless-documents` exercises a fresh empty GUI document, a new XY
  Workplane with Look Normal, Construction lines/circles and Profile-only 2D
  contents without any Part. It covers actual Save/Open, browser rebuild,
  history/selection policy, camera orientation/zoom, Fit All and New. A raw OCCT
  planar fit checks camera-copy recovery without live-view mutation; an invalid
  metadata snapshot is still refused.
- `sketch-arc-direction` checks CW/CCW, short/long and seam-crossing arcs on a
  rotated Workplane, exact numeric radius/start/sweep, fixed endpoints during
  direction changes, role changes, real mouse releases, preview edge geometry,
  Cancel, Undo/Redo and exact `.odc` roundtrips.
- `sketch-overdraw-traversal` follows an arc/line/arc chain in both directions,
  using opposite source orientations and a 0° seam. It compares directed green
  and blue OCCT edges, candidate labels and connected Profile endpoints, including
  a reversed large arc, Backspace/New start/Cancel/OK, Undo/Redo and roundtrip.
  The analytic Overdraw suite also checks short/long signed arcs from both ends.

Bugfix validation on 2026-10-23: the complete CMake Release build succeeded and
all 29 CTests passed (195.20 seconds) in the final full run. This includes the
three new GUI regression suites, expanded analytic traversal tests, earlier
Offset/Common Tangent and Overdraw workflows, and existing 2D/3D, storage,
selection and history regressions. `git diff --check` and whitespace checks of
new source/documentation files passed. The camera failure was first reproduced
against the old code with a normal XY Workplane view; the same test passes after
the fix. The user confirmed all nine latest GUI regression tests on Fedora KDE
on 2026-10-23. This confirmation predates the final axes/presets additions.

Manual Fedora KDE rechecks for these bugfixes:

1. Save/Open a fresh empty document; then repeat with only an XY Workplane after
   Look Normal and Fit All. Add Construction lines/circles, then test a document
   containing only Profile 2D geometry on its Workplane. Check camera framing,
   browser contents and that no Part appears.
2. Draw Construction arcs with both CW/CCW choices using mouse and positive
   numeric sweeps. Test 350°→10° and 10°→350°, small/large arcs, negative numeric
   input, direction changes before OK and a rotated Workplane. The endpoints
   stay fixed when changing direction; verify the complementary sweep readout.
3. Start Overdraw from either end of those arcs. Follow connected lines and arcs
   with opposite source directions, checking green/blue previews, route labels,
   short/long extent and explicit ambiguity choices. Use Back/Backspace, New start,
   Cancel and OK, followed by Undo/Redo and Save/Open. No legacy 3D numeric panel
   should appear or steal focus during any 2D mouse click.

Manual Fedora KDE checks for the Construction-line additions:

1. On a rotated Workplane, offset a finite Profile line by positive/negative
   values and Left/Right. Enable mouse side selection and click to lock it; the
   entered magnitude must remain unchanged. Check Infinite and Finite output,
   localized numeric entry, preview, Cancel and one-step Undo/Redo.
2. Offset an infinite Construction line to Finite. Confirm that OK stays disabled
   until explicit positioning is enabled; enter a negative start and exact length.
   Pick references both in the viewport and via an armed browser role.
3. Use two disjoint unequal Construction circles. Check four green alternatives,
   explicit external/internal solution choices, blue output and orange contacts.
   Repeat for Finite output and limited CW/CCW Construction arcs.
4. Try intersecting, externally/internally tangent, nested, concentric and
   coincident circles. Verify the explanations and the Infinite-only solutions
   at shared contacts. Test same-support arcs meeting only at endpoints.
5. At different zoom/KDE scaling levels, choose ambiguous references/solutions
   from their lists. Navigate, switch Workplanes/tools, Save during a preview,
   Open/New, and verify that temporary output never survives. Modify/delete
   sources and confirm accepted lines stay independent.
6. Use the accepted lines for snapping and Overdraw. Recheck ordinary drawing,
   Overdraw correction/Cancel, native Save/Open and existing 3D operations after
   Finish 2D. Toolbar overflow actions must remain reachable through its menu.

## Release axes/presets validation

`construction-geometry` checks each preset's exact local axes, handedness, zero,
positive/negative offset and reversed normal. `sketch-construction-geometry`
checks canonical axes, individual/Both choices, geometric duplicate detection
including reversed/displaced/hidden/locked sources and separate owners, analytic
snaps at their intersection and bounded Overdraw pieces with a circle.
`sketch-axes-workplane-presets` drives the real actions, numeric preset offset,
OK/Cancel/Enter/Esc, Both as one transaction, selection cleanup, repeat no-ops,
individual missing axes, rotated OCCT line presentation, browser rows, schema-v3
roundtrip, Save history, Open/New cleanup and existing 3D Move history.
Existing schema-v1/v2 migration and 3D suites remain enabled. CI now configures
Release with OPENDESIGNER_BUILD_TESTS=ON and runs all tests in build/ under Xvfb
and software OpenGL; the hosted run still needs verification when a PR is made.

Additional Fedora KDE GUI checks (now confirmed by the user as part of the
nine additional successful tests; 18/18 GUI regressions passed in total):

1. In an empty document create XY, XZ and YZ Workplanes at zero and positive/negative
   offsets. Check the normal/reverse option, local-axis orientation, blue preview,
   Cancel/OK, selection after creation and immediate Edit Sketch/Look Normal.
2. On each Workplane choose Local X, Local Y and Both. Check blue preview,
   Cancel/OK and one-step Undo/Redo for Both. Repeat the command and verify no
   duplicates, extra history or dirty state; also repeat with an axis hidden/locked.
3. Move/rotate a Workplane with axes, snap a line to their origin/intersections,
   add a Construction circle and Overdraw bounded pieces using the axes. Check
   browser/viewport selection and Save/Open, then Finish 2D and ordinary 3D editing.

Final release validation on 2026-10-23: the complete CMake Release build succeeded
and all 30 CTests passed (214.47 seconds). The focused new-feature run also passed
3/3 tests. This includes legacy v1/v2 migration, v3 exact roundtrips, empty/2D-only
storage, existing Move/Rotate/Align/Delete Face, Boolean, selection and history
regressions. Tracked and new-file whitespace checks passed; the changed/new source,
test, configuration and documentation files contained no detected binaries,
temporary artifacts, conflict markers or recognizable secrets. The local branch
was compared with freshly fetched origin/develop; no source files were staged.
The user subsequently confirmed all nine additional GUI tests on Fedora KDE,
including axes and presets: 18/18 GUI regressions passed in total. The hosted
GitHub CI run is checked separately on the PR. General 3D-to-2D projection is
outside this release by the approved scope decision.

## v0.4.2 phase-1 verification

The subsequent phase-2 `SketchProjection` module computes exact orthogonal vertex
and bounded analytic straight-edge projections. It returns independent local
Construction Points/segments and per-source diagnostics, without changing model
or command state. Invalid input/overlap blocks the entire batch; duplicates give
a no-op when no new geometry remains. Storage remains v3/v4 and no interactive
Project command is added. See [projection API and policies](SKETCH_PROJECTION.md).
Existing 3D Point projection remains a separate world-space construction operation.

Validation on 2026-10-23: the complete CMake Release build succeeded and all
32 CTests passed (185.87 seconds), including both new point suites, legacy
schema migration and existing 2D/3D modeling regressions. Tracked/new-file
whitespace checks passed; no binary, conflict-marker or recognizable-secret
findings occurred in changed files. Native Fedora KDE checks below remain pending.

`sketch-construction-points` verifies exact local/world point values on translated
and rotated frames, invalid Profile/non-finite points, pixel-radius snapping and
existing snap tie priorities, hidden/locked/other-owner filtering, curve-only
rejection and unchanged Overdraw graphs even for points lying on curves. Real
BinXCAF tests check v4 point roundtrips, actual v3 output without points, explicit
legacy downgrade refusal, v1/v2/v3 migration and malformed/future-record refusal
without replacing an existing document.

`sketch-point-sessions` drives point creation through the existing preview API,
Cancel/OK, point selection by mouse/browser, rename, visibility/locks and owner
transforms/deletion. Rendered markers keep their pixel size at three zoom levels.
Drawing snaps to exact point values; removing the point leaves the line
independent. Save/Save As preserve selection and both history stacks, pending
points are excluded, and Open/New reset the session and rebuild the browser.
Existing 3D Move Undo/Redo is exercised in the same document.

There is no interactive point-creation button in this foundation phase. Generate
a repeatable manual fixture in the ignored build directory with:

```sh
build/OpenDesignerSketchPointTests --fixture build/construction-points-demo.odc
```

Fedora KDE recheck:

1. Open that fixture with the new executable, select WP1 and Edit Sketch/Look
   Normal. Check pink point markers and browser rows; zoom in/out and change KDE
   scaling. Markers should retain screen size and remain selectable.
2. Select a point by click/Ctrl-click and via the browser. Rename, hide/show and
   toggle selection lock; repeat with its owning Workplane. Move/rotate WP1 and
   verify points follow it. Use Undo/Redo and verify selection is cleared.
3. Draw a line starting near a point at different zoom levels. Check the orange
   **Construction point** snap feedback and exact acquisition; hidden/locked
   points must not snap. Cancel must leave no line; OK adds one history step.
4. Save/Save As/Open, then delete/Undo a point and delete/Undo its owner. Accepted
   lines stay independent, while owner deletion includes its points. Check
   Offset/Common Tangent and Overdraw still operate on curves, then Finish 2D
   and recheck ordinary solid editing. No Project 3D Geometry command is expected.
