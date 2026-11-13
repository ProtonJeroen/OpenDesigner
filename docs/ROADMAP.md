# OpenDesigner roadmap

Versioned sections below are ordered by development history, from early foundations to v0.4.9. The older `v0.20` milestone names are retained as originally recorded. Planned and in-progress items follow separately; proposed milestone numbers and order are not release commitments.

## Cross-cutting principle — Geometry-first, history-independent B-Rep editing

This is a **project-wide architectural requirement**, not only a cylindrical
Move Face feature or a promise limited to primitive shapes.

- [ ] Model and edit solids as their **current boundary representation**:
  faces/surfaces, edges, vertices, shells, topology and material occupancy.
  A box is a closed six-face solid; a simple cylinder consists of its cylindrical
  lateral surface and planar caps. Creation commands may be conveniences,
  but stored primitive labels, Extrude/Revolve history or imported provenance
  must never determine whether a current face is eligible for direct modeling.
- [ ] Support **independent and combined selection/manipulation** of faces
  (including planar and curved faces), and where mathematically meaningful
  edges or face groups, for Move/Rotate/Pull/Resize/Delete/Align. A single
  selected face may reshape adjacent boundaries; groups must be transformed
  coherently, without assuming the entire Part is rigid.
- [ ] Infer direction, axes, constraints and the necessary local geometric
  neighborhood from current B-Rep analytic supports and adjacency. Preserve
  unselected material whenever intended, and reconstruct neighboring
  surfaces/edges and topology when faces split, merge or disappear.
- [ ] Treat a rotated plane, intersected cylinder, Boolean result, arbitrary
  trims or imported STEP solid according to the same geometric rules.
  Surface recognition must be independent of how the geometry arose, while
  operation-specific feasibility is decided by certified result geometry.
- [ ] Preserve Part identity, selections as appropriate, preview, Cancel,
  Undo/Redo and document persistence; check solid validity, connectivity,
  tolerances and material occupancy. Unsupported or ambiguous operations
  must refuse **that operation** with a precise reason, without crashing or
  disabling unrelated editable geometry.
- [ ] Implement the capability incrementally with shared geometric-neighborhood
  and reconstruction utilities and focused regressions. Do not claim that all
  arbitrary B-Reps are already editable; advanced blends, interacting trims,
  multi-face transitions and freeform surfaces require separate validation.

## v0.1 — foundation
- Qt 6 native desktop application
- Open CASCADE 3D viewport
- Dockable structure browser
- Box creation and basic navigation
- Linux CI build

## v0.20 — primitive Pull
- Sphere creation and interactive Pull with absolute diameters are implemented on the sphere feature branch.
- Sphere and cylinder Pull show and accept absolute diameters or cylinder heights, preserving local centers, opposite caps, instance transforms and undo/redo.
- Optional viewport regression tests and manual verification steps: [Sphere Pull](SPHERE_PULL.md).

## v0.20 — cone primitive
- Create Cone adds a parametric GeneralBRep frustum (80/40 mm diameters, 80 mm height).
- Four orange Pull arrows edit absolute bottom/top diameters and height from either end; top diameter zero creates a pointed cone.
- Cone parameters survive transforms, duplication and Pull undo/redo. Equal diameters remain editable through a cylinder B-Rep.
- Transform gizmos use the current B-Rep volume centroid, including after Pull and undo/redo; multipart pivots retain unweighted averaging.
- Regression coverage and manual checks: [Create Cone and Pull](CONE_PULL.md).


## v0.3 — Boolean Operations
- Modeling > Boolean provides Unite, Subtract and Intersect for primitives and GeneralBRep solids in world coordinates.
- Subtract explicitly selects a target and lists the remaining selected parts as tools.
- Validated results replace sources transactionally; cached Boolean commands share the modeling undo/redo history.
- Stable inactive part slots synchronize browser rows and selection across Boolean undo/redo without renumbering IDs.
- Results support transforms, color, visibility, duplication and further Booleans; multi-solid disjoint unions are supported.
- Automated checks and manual smoke steps: [Boolean Operations](BOOLEAN_OPERATIONS.md).
- Follow-up: general face-offset editing, history compaction, persistent IDs and cancellable background operations.

## v0.3.1 — consistent rotation pivot
- Rotate Part and gizmo rotation share B-Rep volume centroid calculation in world coordinates.
- Groups retain the unweighted mean pivot and support menu rotation with grouped Undo/Redo.
- Existing local gizmo axes, world dialog axes and Primitive Pull anchors are preserved.
- Regression coverage and manual checks: [Rotation pivot](ROTATION_PIVOT.md).

## v0.3.2 — command sessions and preview
- Interactive Move and Rotate, including groups, use temporary presentation transforms until OK/Enter.
- Cancel/Esc and tool changes discard the preview and preserve original geometry, transforms and selection.
- Confirmation adds one operation to the existing modeling history; cancellation preserves Undo/Redo.
- At v0.3.2, Primitive Pull, Booleans and modal transform dialogs retain their confirmation behavior; Booleans migrate to sessions in v0.3.3.
- Architecture, future adapters and verification: [Command sessions](COMMAND_SESSIONS.md).

## v0.3.3 — General Move Face
- Modeling > Move Face provides signed planar face offsets with a normal arrow and exact input.
- OCCT intersection offsets rebuild adjacent plane/cylinder/cone boundaries; supported results are validated before confirmation.
- Geometry previews share command sessions; direct edits retain stable part/browser IDs and use the existing Undo/Redo history.
- Edited primitives become GeneralBRep; Undo restores their original primitive state.
- Single-solid, single-wire and unchanged-topology scope, verification and manual checks: [Move Face](MOVE_FACE.md).
- Follow-up: holes/cavities, topology transitions, blends/freeform, multiple faces and asynchronous calculation.

### v0.3.3 — selection and context workflow
- Passive Face/Alt-Part selection retains Ctrl and Ctrl+Alt controls; no selection dropdown or automatic manipulators.
- Viewport context menus dispatch to existing Part operations, supported Move Face and primitive/multi-Box Pull.
- Face selection stays explicit on GeneralBRep/Boolean geometry; incompatible Part tools are disabled and their previews cancelled.
- Move Face and Primitive Pull share orange arrow styling; unavailable face actions are omitted rather than shown as placeholders.
- Capabilities, tests and manual checks: [Selection/context menus](SELECTION_CONTEXT.md).

### v0.3.3 — tool compatibility and Boolean sessions
- Selecting new geometry cancels incompatible previews and removes old manipulators.
- Context/Modeling actions explicitly activate one tool; no combined Move/Rotate set. Normal picking always follows modifiers.
- Boolean sessions start from one explicit Target or request one, capture editable Tools via viewport/browser, and provide Keep Tools (default off), Preview, OK/Enter and Cancel/Esc.
- Preview shares existing OCCT validation and leaves document geometry intact. One confirmation creates one existing Boolean history entry; cancellation creates none.
- Keep Tools preserves tool identities and properties; only Target is consumed into a new stable result slot. Cached Undo/Redo uses the existing chronological history.
- Automated regression coverage includes selection/tool transitions, Target/Tools, both Keep Tools policies and metadata/browser/history preservation. Manual Fedora KDE retest pending.

### v0.3.3 — explicit selection-driven lifecycle
- Passive selection, ordinary right-click menus and optional default Face picking with no prior selection.
- Central tool cleanup on OK/Enter, Cancel/Esc, tool changes, one-shot operations and Undo/Redo; no persistent last-used tool.
- Explicit Primitive Pull / Resize now has rollback snapshots and deferred one-step history; existing geometry and absolute numeric dimensions are retained.
- Pull still uses its live working B-Rep calculation during preview; immutable candidate extraction is a future architecture improvement.
- Regression coverage for passive selection, explicit activation, complete termination and Boolean operand-selection exceptions. Fedora KDE retest pending.

## v0.3.4 — Rotate Face & Align Face
- Rotate Face accepts one planar target, a bordering straight edge or projected world reference axis, signed degrees and an orange rotation ring.
- Align Face keeps Target and Reference separate and supports Coplanar and Parallel against a planar face on the same or another Part.
- OCCT DraftAngle and intersecting planar offset operations rebuild supported adjacent analytic surfaces. Candidates must preserve face/edge/vertex counts, wire connectivity and neighboring supports; no sweep/revolve bridge fallback is used.
- Both adapters use the existing CommandSession preview and geometry snapshot history; part identity, metadata, browser rows and reference Parts are preserved.
- Geometry, selection, preview, cancel, confirmation and history coverage: [Rotate Face](ROTATE_FACE.md), [Align Face](ALIGN_FACE.md).
- Support remains conservative for analytic single-solid topology. Regression coverage includes safe refusal of a narrow Unite transition between overlapping Boxes of different heights, plus direct edits of topology-preserving Box and simple Subtract faces. Fedora KDE manual verification for this refinement is pending.

## v0.3.5 — Advanced B-Rep Reconstruction

- Shared isolated B-Rep normalization, OCCT face history and analytic-support
  acceptance for Move/Rotate/Align Face; redundant Boolean faces/edges may merge.
- Two overlapping Boxes of unequal height: signed riser Move and edge-axis Rotate,
  plus Coplanar/Parallel Align verified against analytical material occupancy and
  volumes. Existing transaction/identity tests also cover these topology changes.
- Plane/cylinder/cone support policy remains; no freeform, bridge volumes,
  unrestricted face disappearance or multi-shell reconstruction.
- Architecture, exact fixture, tested parameters, OCCT choices and future
  Extrude/Revolve/Sweep integration: [B-Rep reconstruction](BREP_RECONSTRUCTION.md).
- Follow-up: support-disappearing topology events, broader curved-neighbor
  reconstruction, cancellation of kernel work and persistent subshape IDs.
- Live previews explicitly tessellate the isolated candidate, use blue translucent
  shaded AIS geometry and refresh the retained viewer scene; invalid input and
  termination remove obsolete presentations.
- Empty-space context menus share the Edit/toolbar Undo and Redo actions with
  accurate availability, while existing selection and camera workflows remain.
- Manual Fedora KDE tests passed; the user confirmed all tested v0.3.5
  functionality, including geometry, live previews and context menus (2026-10-20).

## v0.3.6 — Advanced Face Topology

- Every accepted face edit now receives final same-domain normalization and
  connectivity-checked internal cleanup, with material equivalence checked by
  OCCT Common before and after normalization.
- Bounded all-planar material-cell reconstruction uses OCCT Splitter, halfspaces,
  Common and Fuse to support controlled disappearing supports and new patches on
  existing planes. Artificial target-plane partitions inside material are removed
  before editing; nonadjacent supports do not clip legitimate overhangs.
- Verified Unite-riser +75° rotation removes the former blocking high cap;
  negative rotations preserve existing overhang behavior. Move crosses the original
  overlap seam, removes a terrace at +60 mm and exposes an underside at +100 mm.
- Regressions check analytical volumes, material probes, absence of redundant
  seams, preservation of disconnected coplanar patches, final target mapping,
  Align after reconstruction and preview/Cancel/OK/Undo/Redo cleanup.
- Limits remain explicit: one target patch, up to 12 distinct planes and 32 cells
  for the new path; no unrestricted curved/freeform/multi-shell reconstruction,
  lost-target editing or persistent subshape naming. Computation is synchronous.
- File → Export Debug Model captures exact geometry/meshes, metadata, selection
  and available command parameters. An isolated inspector supports roundtrip
  checks, stored/fresh mesh comparison, wireframe and world-space material probes.
  This is diagnostic ZIP capture, not v0.3.7 `.odc` Save/Open.
- The exact supplied Rotate +30° → oblique Move +49 mm case now retains physical
  target-neighbor trims that were previously lost as redundant cell constraints.
  This removes a real 64981.2085 mm³ triangular wing; it is not a mesh-only fix.
  Exact B-Rep, independent material oracle, mapping and full preview/history
  regressions are included. Concave/ambiguous target trim decompositions remain
  refused by the planar fallback. See [B-Rep reconstruction](BREP_RECONSTRUCTION.md).

Remaining work includes broader complex Boolean/curved-neighbor topology events,
nonconvex cell partitions, persistent subshape IDs and cancellable kernel work.

### v0.3.6 — Delete Face and final integration

- Multi-Face / multi-Part deletion uses OCCT defeaturing, all-or-nothing preview
  and one cached history entry while retaining Part identity.
- Independent material certificates cover planar-rim analytic features and
  bounded convex planar support removal; ambiguous or unsupported closures fail
  safely. No arbitrary capping fallback is used.
- Bounded prismatic step reconstruction handles the supplied Unite/Rotate small
  transition triangle and front strip, including actual neighbor shortening.
  Exact user B-Reps, independent profile/volume oracles and history tests guard
  this extension; separate Parts must be united first.
- Successive deletions infer a unique section axis from the current B-Rep even
  after an earlier deletion removed the opposite-facing transition. Both orders
  and the exact exported intermediate are regression-tested with two-step history.
- Planar sections may also have oblique top/front supports when a unique family
  of station planes is available. The exact debug5 L-shaped transition is covered
  by profile/volume oracles, transformed geometry, preview/history and safe-refusal
  regressions for unbounded, ambiguous or lost-unselected-face cases.
- Shared geometry error popups cover Delete/Move/Rotate/Align Face, Boolean and
  Pull. Undo/Redo retains the existing complete selection cleanup.
- Unite/Subtract/Intersect normalize the final B-Rep before preview/history.
  Redundant same-surface divisions disappear with Common-confirmed material
  equivalence; real steps, disconnected solids and cavity shells remain intact.
- Geometry and session regressions are provided. Scope and manual Fedora KDE
  checks: [Delete Face](DELETE_FACE.md). General freeform/blend reconstruction
  remains future work.
- The user confirmed the supplied successive Delete Face cases, Unite seam
  cleanup and the inclined L-shaped deletion on 2026-10-20. These are targeted
  desktop confirmations, not a claim of exhaustive platform validation.
- v0.3.6 implementation is complete within the documented analytic scope.
  The full CMake build and the v0.3.6 regression CTests cover geometry, blue preview, transactions,
  uniform popups, diagnostic roundtrips and cached history. Windows/macOS validation,
  unrestricted freeform reconstruction and packaged distribution remain future work.
## v0.3.7 — Native documents

- File → New/Open/Save/Save As and Ctrl+N/Ctrl+O/Ctrl+S use the native `.odc`
  document format.
- BinXCAF/XCAF stores exact local B-Reps for multiple Parts in a root assembly,
  including stable IDs, names, colors, visibility and placement.
- Camera state is stored on Save without making ordinary camera navigation dirty.
- Unsaved changes are shown in the title and protected by New/Open/close prompts;
  failed loads and saves leave the current document intact.
- New and successful Open reset selection, previews, manipulators and Undo/Redo;
  Save and Save As preserve them.
- Round-trip coverage verifies multiple Parts, exact geometry metrics, metadata,
  transforms, camera state and malformed-file refusal. See
  [Native documents](NATIVE_DOCUMENTS.md).
- Pull capabilities are derived from the current B-Rep, independent of creation
  history and stored primitive flags. Complete supported analytic primitives
  require validity and material-equivalence certification; general cylindrical
  walls use face offsets. Save/Open regressions exercise visible cone/hole
  handles, manipulation and Cancel with missing flags; cut cones are not
  rebuilt as whole primitives. Activation redraws immediately and hole arrows
  remain visible over surrounding material.

- Atomic replacement uses Qt's safe-save transaction after BinXCAF completes;
  failed writes remove staging files and leave existing documents intact. Open
  stages a read-only source in the system temporary directory and rejects wrong
  formats before entering the OCCT reader.
- UI regressions verify Save/Save As history and selection preservation,
  confirmed-state saving during Pull previews, camera-only Save, dirty prompts,
  failed operations, full browser rebuilding and New/Open history/tool reset.
- The user confirmed Native Save/Open and the cone/hole Pull interaction on
  Fedora KDE on 2026-10-21. v0.3.7 is complete within schema-v1 scope: one root
  assembly, no persisted Undo/Redo or feature history, synchronous storage and
  existing documented modeling limits. Nested assemblies, Windows/macOS runtime
  validation and packaged distribution remain future work.

OpenDesigner targets Windows, Linux and macOS. Version sections record implemented
work and its limits; Next milestones and follow-up items describe planned work.

## v0.4.0 — Workplanes, Axes & Points

- [x] Context-menu Fit All without selection and Fit Selected for Faces, Parts and
  construction objects; background access preserves the existing selection.

- [x] Independent WP/AX/PT numbering, camera-facing viewport name labels,
  selection-centered Workplane displays and two-point Axis overrun; saved display
  offsets keep analytic reference origins unchanged.


- [x] Independent construction objects with stable UUIDs, names, visibility and
  selection locks; references are evaluated once with no persistent dependencies.
- [x] Workplane, Axis and Point creation methods, explicit operand roles, analytic
  B-Rep recognition and safe refusal of ambiguous/degenerate definitions.
- [x] Midplane Workplane accepts two planar Faces, Workplanes or a mixed pair:
  halfway between parallel planes or an explicitly chosen angle bisector for
  wedge-like geometry. Normal reversal is independent of bisector choice.
- [x] Workplane/Axis Move and Rotate with orange manipulators, numeric input,
  temporary blue preview and the existing OK/Enter and Cancel/Esc lifecycle.
- [x] Typed browser and viewport selection, context actions and one chronological
  Undo/Redo history shared with existing solid operations.
- [x] Single-file `.odc` schema v2 stores construction values and reads v0.3.7 v1
  documents. Save retains sessions/history; New/Open rebuild both object groups.
- [x] Geometry, metadata/migration and command/document regressions accompany the
  implementation. Fedora KDE manual verification completed by the user.
- [x] Workplane coordinate frames prepare for a future Sketch Editor; no sketches,
  associative constraints or feature history are introduced.
- Methods, definitions, storage and limits: [Construction geometry](CONSTRUCTION_GEOMETRY.md).

The v0.4.0 scope is complete. The documented limits remain intentional follow-up
work: nested assemblies, associative or persistent parametric dependencies,
non-planar Workplanes, persistent subshape references, arbitrary surface fitting
and the future Sketch Editor are not part of this release.

## v0.4.1 — 2D Modeling / Sketch Editor

- [x] Exact local-XY segments, infinite Construction lines, circles and signed
  circular arcs; stable UUIDs, Workplane ownership, Construction/Profile roles,
  names, visibility and selection locks. Extensible curve representation.
- [x] One 2D collection per Workplane, independently editable document values
  without persistent constraints or source/feature history.
- [x] Native schema v3 stores every 2D value in the same BinXCAF `.odc`; v1/v2
  documents load with an empty 2D collection. Unsupported schemas/types and
  malformed ownership/geometry are rejected transactionally.
- [x] Exact OCCT edge presentation distinguishes dashed Construction from solid
  Profile geometry; browser child rows and typed viewport selection preserve
  existing 3D picking, including Alt Part selection.
- [x] Workplane frames carry confirmed and previewed 2D geometry without rewriting
  local coordinates. Workplane deletion includes contents in one history entry.
- [x] Programmatic create/edit/delete candidates use the existing OK/Cancel
  session and chronological 3D Undo/Redo history; only confirmed values are saved.
- [x] Geometry/storage and GUI/session regressions cover precision, migration,
  transforms, selection, mixed history, Save/Open/New and failure preservation.
- [x] Explicit active Workplane, Look Normal and Finish 2D; visible toolbar and
  Workplane switching with isolated 2D picking.
- [x] Interactive/numeric finite lines, infinite Construction lines, circles and
  center/start/end arcs in Construction/Profile roles, live preview, one-element
  OK/Cancel transactions and shared Undo/Redo.
- [x] Analytic endpoint/midpoint/center/intersection and line-to-circle/arc tangent
  snapping, nearest-curve projection, screen-pixel radius and Shift bypass.
- [x] Full Release build and 22/22 CTests pass (2026-10-21), including numeric and
  mouse-driven drawing, zoom-independent snap acquisition, cancellation, mixed
  history, native roundtrips and existing 3D regressions. The user subsequently
  confirmed all eight phase-2 practice tests on Fedora KDE.
- [x] Explicit Overdraw from bounded Construction line/circle/arc portions into
  independent Profile segments/arcs, retaining sources, green/blue feedback,
  explicit ambiguity choices, Back/New start, shared transactions and schema v3.
- [x] Safe boundaries/refusal: no unbounded tails or Workplane-border cuts,
  no fabricated full-turn arcs from 0/1-cut circles; overlapping sources refuse
  the command. Tangencies and signed/trimmed arcs use phase-2 analytic geometry.
- [x] Phase-3 full Release build and 24/24 CTests pass (2026-10-23, 189.49 seconds),
  including analytic Overdraw, real GUI events/rendering, transactions, native
  roundtrips and existing 2D/3D regressions; diff/whitespace checks pass.
- [x] The user confirmed the nine latest Fedora KDE GUI regressions on
  2026-10-23, including the reported storage/Arc/Overdraw fixes.
- [x] Offset Construction Line from finite Construction/Profile and infinite
  Construction references; signed exact distance, numeric/mouse side choice,
  Infinite/Finite output and explicit finite positioning for infinite sources.
- [x] Analytic external/internal common tangent Construction lines for circles
  and domain-valid arcs; explicit solution choice, contact previews, finite
  contact-to-contact spans, infinite shared-contact solutions and safe refusal.
- [x] Both tools reuse Sketch sessions, shared Undo/Redo, schema v3,
  browser/viewport references, snapping and ordinary Overdraw sources. Sources
  remain independent; reference evidence and OpenDesigner choices are separated.
- [x] Construction-line extension: full Release build and 26/26 CTests pass
  (2026-10-23, 168.68 seconds), including analytic offset/tangent cases,
  transactions/rendering, native roundtrips, Overdraw and existing 2D/3D tests.
  Diff and new-file whitespace checks pass.
- [x] The latest user-confirmed GUI regressions cover the Construction-line
  additions; the earlier checklist is retained as a repeatable test procedure.
- [x] Storage/Arc/Overdraw bugfixes: planar orthographic camera capture without
  Parts; explicit CW/CCW mouse/numeric arcs; directed forward/reverse previews,
  labels and Profile traversal; no legacy 3D editor on 2D mouse releases.
  Full Release build and 29/29 CTests pass (2026-10-23, 195.20 seconds), including
  the new partless-document, arc-direction and Overdraw-traversal regressions.
- [x] Fedora KDE recheck of the three reported bugs passed, per the user’s
  nine-test confirmation.
- [x] Construction Axes: real local-X/local-Y infinite Construction lines, single
  or Both, duplicate prevention, shared OK/Cancel/Undo/Redo and schema-v3 storage.
- [x] Explicit XY/XZ/YZ Workplane presets with right-handed frames and optional
  signed normal offset, reusing Workplane creation even without Parts.
- [x] GitHub Actions builds Release with tests enabled and runs CTests under
  Xvfb/software OpenGL before uploading the executable.
- [x] Final release validation (2026-10-23): full CMake Release build, all
  30/30 CTests (214.47 seconds), tracked/new-file whitespace checks, backward
  compatibility and existing 3D regressions pass. No artifact/secret findings.
- [x] The user confirmed all nine additional Fedora KDE GUI tests, including
  axes and presets: 18/18 GUI regressions passed in total. The repeatable
  procedures remain in `SKETCH_EDITOR.md`.
- [ ] Closed-profile analysis and later 3D profile operations. Splines, ellipses
  and other curves remain future work.

Phases 1–3 and the release axes/presets are implemented. Full 3D-to-2D projection
is deferred to v0.4.2; existing Point projection only creates a 3D construction
Point. Overdraw does not certify closed/extrudable profiles
or detect overlaps with existing Profiles; those remain future work.
Dedicated tangent-circle construction and circle-extreme snaps are
also not implemented. Read the [model, GUI, storage and scope](SKETCH_EDITOR.md).
No proprietary reference files
or copied implementation are included.

## v0.4.2 — Project 3D Geometry foundations

### Phase 6A: exact ellipse extension

- [x] 6A.1: append exact Ellipse/EllipticalArc values without renumbering old
  types or changing UUIDs. Local evaluation/derivatives, inverse parameter,
  signed domain/endpoints/reversal, validation and exact OCCT presentation.
- [x] 6A.1: reuse typed selection, Structure Browser and SketchChange history;
  safely gate unsupported curve tools and retain existing 2D/3D behavior.
- [x] 6A.1: schema v5 for ellipses; v1–v4 reading; conditional v3/v4 output for
  old content; strict malformed/future/downgrade refusal and atomic file lifecycle.
- [x] 6A.1 validation (2026-10-24): full CMake Release build and 43/43 CTests
  pass (94.54 seconds) with isolated Xvfb 1280×1024, xcb and software OpenGL,
  including normal/200% conic sessions and existing 2D/3D regressions. New pure
  geometry/storage tests also pass without a display; diff/safety checks pass.
- [x] 6A.2: **independent drawing tools**, not only projected ellipse display.
  Ellipse: center → major axis → minor axis. Elliptical Arc: center/axes → start
  → end with explicit CW/CCW. Both roles, numeric input, preview, history and save.
- [x] 6A.2 basic snapping: ellipse/arc centers, arc endpoints/parametric midpoint,
  Euclidean nearest on complete/trimmed ellipses and analytic line/ellipse
  intersections. Respect domains, tangent degeneracy, locks, pixels and Shift.
- [ ] Remaining 6A.2 snapping: circle/ellipse and ellipse/ellipse intersections,
  coincident-support handling and elliptic tangent snaps. Full conic snapping
  is not yet claimed; unsupported combinations report an explicit status.
- [x] 6A.3: exact oblique circle/ellipse and face-boundary projection, including
  parameter mapping and shared-support duplicate/overlap comparisons. Uses the
  existing Project 3D Geometry session and native schema v5.
- [x] 6A.4: exact finite tangent generators for complete analytically cylindrical
  mantles with certified rectangular UV trims. Reuses Construction Segments,
  projection preview/history/storage and seam deduplication. Complex/partial
  trims and degenerate views are explicitly refused; general silhouettes remain
  outside this limited extension.
- [ ] Full elliptic Overdraw remains separately gated pending its graph/domain,
  reverse traversal and ambiguity tests. Ellipse Offset/Common Tangent commands
  are outside this foundation.

See [ellipse plan and delivery boundary](ELLIPSE_IMPLEMENTATION_PLAN.md).
No new Fedora KDE manual approval is implied; automated results are recorded in
the phase-6A.1 PR. Earlier milestone results below retain their historical scope.

### Phase 6B: Workplane presentation (Issue #48)

- [x] 6B.1: derived rectangular local bounds from all finite Sketch geometry,
  including analytic circular/elliptical arc extrema, per-side 10% padding,
  20 mm minimum spans and legacy empty-Workplane base extent. Outside infinite
  lines enlarge the frame by perpendicular offset, ignoring tangential origins.
- [x] 6B.1: visually clip infinite Construction Lines to the actual rectangle;
  refresh frames/labels for previews, Cancel, history, transforms and Open.
  Analytical origins, geometry, native schema and styles remain unchanged.
- [x] Issue #50: independent one-click/numeric Horizontal and Vertical Infinite
  Construction Line tools in the menu, toolbar and context menu, with H/V icons.
  Reuses exact local InfiniteLine geometry, forced Construction role, dynamic
  bounds, snapping, preview/history and native storage without a schema change.
- [x] 6B.2: one open rectangular Workplane ring, exactly 15 mm wide outside the
  existing margin, transparency 0.5, active green/inactive light gray, subtle
  corner labels and no default X/Y arrows. Explicit manipulators remain.
- [x] 6B.2: complete transient candidate sets determine display bounds before
  blue AIS construction; immediate outside-frame drawing/Offset/projection
  previews, consistent foreground visibility and clipping to the outer ring.
  Cancel/history/Open restore derived presentation without schema/model changes.
- [x] 6B.2: analytical ring topology/width/area/opening tests and normal/200%
  rendered preview, active-state, picking and lifecycle regressions.
- [x] 6B.2 validation (2026-10-26): full CMake Release build, 55/55 CTests
  (116.03 seconds, isolated Xvfb/xcb/software OpenGL), existing 2D/3D
  regressions and diff/whitespace checks pass.
- [ ] Native Fedora KDE acceptance of 6B.2 visuals and preview behavior; see
  [checklist](SKETCH_EDITOR.md#phase-6b2-open-workplane-border-and-immediate-blue-previews).
  Grid, separate front/back colors and visual Settings are outside this fixed
  6B.2 delivery and remain future options.

### Completed phases 1–5

- [x] Phase 1: independent local 2D Construction Points with stable UUIDs,
  Workplane ownership, exact XY, names, visibility and selection locks. Points
  cannot be Profile curves or enter curve-only tools.
- [x] Fixed-screen-size markers, typed viewport/browser selection, exact point
  snapping and shared SketchChange OK/Cancel/Undo/Redo. Owner transforms carry
  markers without rewriting local coordinates; owner deletion includes points.
- [x] Single-file native schema v4 for documents containing points; current
  documents without points retain v3 output. Readers accept v1/v2/v3/v4;
  incompatible downgrade requests and malformed/future records fail safely.
- [x] Analytic/storage and GUI/session regressions cover point values, zoom,
  selection/locks, curve isolation, history, file lifecycle and legacy migration.
- [x] Phase-1 validation (2026-10-23): complete Release build, 32/32 CTests
  (185.87 seconds), existing 2D/3D regressions and diff/whitespace checks pass.
- [ ] Native Fedora KDE visual/focus verification of the new point markers and
  snapping. Existing v0.4.1 user confirmations do not cover this addition.

- [x] Phase 2: standalone exact vertex/analytic-straight-edge projection core,
  independent Construction results and source diagnostics, one-time nested
  subshape/Part placements and arbitrary valid Workplane frames.
- [x] Atomic batch refusal, orientation-independent deduplication, no-op batches
  and explicit finite/infinite Construction-line overlap refusal without trimming.
  No interactive Project command or popup-positioning changes are introduced.
  See [projection API, policies and tests](SKETCH_PROJECTION.md).
- [x] Phase-2 validation (2026-10-23): complete Release build, 33/33 CTests
  (207.21 seconds), headless projection tests, v1–v4 compatibility and existing
  2D/3D regressions pass; diff/new-file whitespace and safety checks pass.

- [x] Project 3D vertices, edges and face boundaries into independent local 2D
  references on a Workplane, with explicit supported curve types and safe refusal.
  This is outside the approved v0.4.1 release scope. Existing 3D Point projection
  remains available and does not create 2D sketch entities or dependencies.
  Phases 1–2 add no interactive point-creation or Project 3D Geometry command.
  Phases 3–4 add circular/arc edges and actual face boundaries to the computation
  core. Phase 5 adds the interactive selection/session UI below.

- [x] Phase 3: analytic circular edges parallel to the target Workplane project
  to independent Construction Circles/Arcs. Preserve signed traversal, reversed
  edges/normals and composed placements; refuse oblique elliptical projections.
  Circular duplicate/domain-overlap checks retain all-or-nothing batch behavior.
  See [circular projection rules and tests](SKETCH_PROJECTION.md#phase-3-exact-circles-and-circular-arcs).

- [x] Phase 4: project every actual outer/inner wire of a valid bounded face,
  including multiple holes and mixed straight/circular boundaries. Verify full
  wire traversal and projected closure before shared/reversed/seam deduplication.
  Certify spherical/conical collapsed pole edges before omitting them; refuse
  unsupported, unverified or collapsed ordinary edges atomically. Preserve
  independent Construction values, Part/subshape placements and schemas v1–v4.
  This is boundary geometry, not a silhouette or a convex hull, and adds no UI.
  See [face-boundary rules and limitations](SKETCH_PROJECTION.md#phase-4-actual-face-boundaries).
- [x] Phase-4 validation: complete Release build and 38/38 CTests pass
  (85.43 seconds), including existing 2D/3D/history and v1–v4 storage regressions.
  All three projection suites also pass without a display; diff and reviewed-file
  safety checks pass. That milestone did not include the phase-5 UI.

- [x] Phase 5: Project 3D Geometry in 2D Modeling menu/toolbar/context, requiring
  an active visible/unlocked Workplane. One existing Sketch command session owns
  temporary Vertex/Edge/Face/Mixed OCCT selection, source list/clear/remove,
  live blue preview and kernel diagnostics. OK validates again and commits one
  SketchChange; Cancel/Escape/close discard, restore picking and safely retained
  Sketch selection. Duplicate-only confirmation creates no history/dirty change.
- [x] Projection panel uses CommandPanelPositioner at viewport upper-right +12;
  viewport navigation, dragging, resize/docking, small-view scrolling and HiDPI
  reuse existing mechanisms. Save excludes preview while preserving the session;
  successful Open/New, target/source invalidation and accepted close clean up.
  Exact curve/point values keep native v3/v4 output and v1–v4 reading.
- [ ] Phase-5 native Fedora KDE GUI verification and explicit merge permission.
  The existing v0.4.1 confirmations do not cover this command. Follow
  [tests A–H](GUI_TEST_PLAN.md); record results before merging the UI PR.
- [x] Phase-5 automated validation: complete Release build, 40/40 CTests
  (92.48 seconds) under isolated Xvfb/xcb/software OpenGL, including normal/200%
  projection sessions and existing 2D/3D/history/v1–v4 regressions. Both new UI
  tests also pass at the CI display size 1280×1024 (6.43 seconds). Diff and
  explicit changed/new-file safety checks pass; no GUI confirmation is implied.

### UI: Command Pop-up Positioning

- [x] Separate UI helper after the phase-2 geometric projection core, without
  coupling to projection math, document schemas or history changes.
- [x] Default nonmodal command/parameter panels to the CAD viewport's upper-right
  corner with approximately 12 logical pixels of margin. Use viewport coordinates,
  not the application window or screen; keep the central modeling/picking area free.
- [x] Keep panels draggable. Preserve a user's moved position during the command;
  clamp to viewport bounds on viewport resize, panel content-size changes and
  viewport/window movement. Keep controls reachable on small viewports.
- [x] Centralize placement for 2D and 3D command panels, retaining selection,
  previews, numeric focus, OK/Cancel and Undo/Redo. Exclude genuine modal dialogs
  and ordinary cursor context menus from this placement policy.
- [x] Automated placement, drag/resize/scrolling, HiDPI and actual CAD-panel
  integration regressions accompany the helper.
- [x] Measure natural content dimensions independently of cached outer window
  hints and viewport constraints. Regress complete Move Face, 2D Drawing and
  From planar face forms, dynamic prompts, font changes and shrink/restore;
  enable scrolling only when the natural form genuinely exceeds the viewport.
- [x] Validation (2026-10-23): complete Release build and 36/36 CTests pass
  (192.77 seconds), including strengthened panel-sizing tests and existing
  2D/3D/document regressions; diff and file checks pass.
- [ ] Complete the native Fedora KDE GUI checklist in
  [Command sessions: popup positioning](COMMAND_SESSIONS.md#command-panel-positioning).

The implementation and repeatable manual checks are recorded there. Native KDE
decoration/focus and mixed-monitor scaling verification remain manual checks.

## v0.4.3 — Closed Profile analysis (6C.1 / 6C.2)

- [x] Headless Profile-only analysis on one Workplane: ordered exact Segment/Arc
  traversal, CW/CCW, full Circles, analytic area and intersections.
- [x] Even-odd nesting: material regions with direct holes, islands and multiple
  disconnected regions; atomic refusal of open, overlapping, branching,
  crossing, touching or numerically ambiguous boundaries.
- [x] Source IDs and values remain unchanged; no healing or document/schema changes.
- [x] Explicit construction-readiness diagnostics: tolerance-connected analysis
  does **not** certify an exact closed OCCT wire.
- [x] Extrude/Add/Cut GUI, delivered through the unified v0.4.8 Extrude command.
- [ ] Ellipse/EllipticalArc profiles.

See [closed profile validation](CLOSED_PROFILES.md) for output and limitations.

## v0.4.4 — OCCT Wire & Face Construction (6D.0)

- [x] Headless `SketchProfileBuilder`: exact Segment/Arc/Circle wires and planar
  faces on arbitrary Workplanes, including holes, islands and disjoint regions.
- [x] Explicit bounded vertex policy capped by recorded Sketch tolerance;
  independent OCCT connectivity, closure, orientation, validity, plane and area
  checks. No healing or source-coordinate changes; atomic failure diagnostics.
- [x] Source entity/contour/region references and effective tolerance reporting;
  focused headless construction regressions. `.odc` and GUI remain unchanged.
- [x] 6D.1: **Extrude Profile → New Solid / Add Material** with exact signed
  depth, blue preview, target selection, one-step history and native storage.
  Certified prism solids/compounds; shared Boolean normalization; stable target
  identity/local placement. Source Workplanes and Sketches remain independent.
- [x] Headless extrusion/fusion and normal/200% viewport-session regressions.
- [x] The former 6D.1 desktop gate is superseded by v0.4.8 Unified Extrude
  acceptance (7/7 reported passed); see [Extrude](EXTRUDE_PROFILE.md).
- [x] Profile Cut (6D.2), now the unified Extrude Remove Material mode.
- [ ] Advanced extrusion options and elliptical profiles.

See [closure policy and construction checks](CLOSED_PROFILES.md).

## v0.4.5 — Cut Extrude

- In-place Blind and directional Through All subtraction reuse certified Profile
  faces, extrusion prisms and the shared OCCT Boolean utilities.
- Command Panel region/target selection, reverse direction and live result preview;
  cancellation preserves the target and confirmation records one Undo operation.
- Part identity, properties, placement, browser slot and native persistence retained.
- Numerical geometry and session regressions extend existing extrusion tests,
  including HiDPI panel placement. See [Cut Extrude](CUT_EXTRUDE.md).

## v0.4.6 — Active Part and viewport target picking

- Stable document-level Active Part independent of selection, with optional native
  metadata, green browser indicator and native AIS silhouette (boundary fallback on older OCCT).
- Browser and geometry context activation/deactivation; shared eligible-target
  resolution and viewport picking integrated with Cut Extrude.
- Temporary target changes preserve activation; geometry history keeps valid IDs.
- See [architecture, workflow and pending desktop acceptance](ACTIVE_PART.md).
- Older-OCCT boundary fallback and absent nested assembly UI remain limitations.

## v0.4.7 — Delete Part

- Single/multiple whole-Part deletion from Delete and viewport/browser menus.
- One atomic history operation preserves stable identities, properties and order
  on Undo; hidden browser Parts are supported.
- Local subshape selections, text editing and active tools are protected.
- Reuses existing AIS cleanup, Active Part lifecycle and native persistence.
- See [workflow, architecture and pending desktop acceptance](DELETE_PART.md).

## v0.4.8 — Unified Extrude & Revolve

- [x] Phases 8A–8E: shared certified Profile Builder, sweep results, material
  application, target resolution, Command Panel sessions, preview and history.
- [x] One **Extrude** and one **Revolve** action in 3D Modeling, each shared by
  menu and toolbar. Both offer **New Solid**, **Add Material**, **Remove Material**;
  the separate Cut Extrude UI is replaced by Extrude → Remove Material.
- [x] Add/Remove use an eligible Active Part or direct viewport target picking;
  explicit overrides retain Active Part. New Solid ignores Active Part.
- [x] Extrude retains signed distance and Remove's Blind/Through All/Reverse.
  Revolve supports signed 1°–360° sweeps, Reverse and supported linear axes.
- [x] Actual result previews, safe Cancel/Escape, one-step Undo/Redo, stable Part
  identity/properties/placement/browser slot and native Save/Open.
- [x] Phase 8E targeted automated checks: **6/6 passed** (geometry and normal/HiDPI
  sessions). No full-suite rerun is required for the final publication review.
- [x] User-reported manual Fedora KDE acceptance, recorded 2026-11-01:
  **Unified Extrude 7/7**, **Revolve New Solid 13/13**, **Revolve Add/Remove 15/15**;
  **35/35 total passed**.
- [x] Phase 8F: integration and documentation review; release labels aligned to
  v0.4.8. Ready for publication within this documented scope; publication itself
  remains a separate action.

Limits remain: Segment/Arc/Circle profiles only; one target; Add requires a
connected target and result; Remove refuses unchanged or empty material results
but can leave multiple solids in one Part. Revolve requires a coplanar axis and
same-side profiles, refusing crossing/pinched/self-intersecting sweeps. No healing,
advanced extrusion options, persistent feature links or Revolve Through All mode.

See [Extrude](EXTRUDE_PROFILE.md), [Revolve](REVOLVE.md),
[Revolve geometry](REVOLVE_GEOMETRY.md) and
[shared architecture](SHARED_MODELING_ARCHITECTURE.md).

## v0.4.9 — Cylindrical Move Face and certified boundary editing

- [x] Geometry-first cylindrical hole/boss movement from current analytic
  supports, connected face patches, adjacency and material occupancy, independent
  of creation metadata or feature history. Explicit lateral directions/angles,
  manipulator or numeric input and boss **Trim / Extend** reuse Command Panels.
- [x] Certified repeated open-notch/boundary-boss moves, independent features and
  local planar-neighborhood reconstruction preserve unrelated material. See
  [Move Face](MOVE_FACE.md), [local reconstruction](GEOMETRY_DRIVEN_MOVE_FACE.md),
  [open notches](OPEN_CYLINDRICAL_NOTCH_MOVE.md),
  [boundary follow-up](BOSS_BOUNDARY_FOLLOW_UP.md) and
  [repeated boundary bosses](REPEATED_BOUNDARY_BOSS_MOVE.md).
- [x] Certified diameter/height Pull and boundary Delete Face derive capability
  from current geometry; unsafe global cylindrical-offset paths are avoided.
  See [post-Trim Pull](PULL_AFTER_BOSS_TRIM.md),
  [Pull/Resize scope](PULL_RESIZE_GAPS.md), [Delete Face](DELETE_FACE.md) and the
  [reproduced planar/cylindrical crash correction](CRASH_03112026.md).
- [x] Phase 9F.1: Delete Face on certified sloped planar-rim cylindrical openings.
  Phase 9F.1B: normal movement of a certified planar face containing openings
  recomputes elliptical boundaries while cylinder axes/radii stay fixed. See
  [sloped-opening deletion](SLOPED_OPENING_DELETE_FACE.md),
  [planar opening movement](SLOPED_PLANAR_OPENING_MOVE.md) and
  [rotated-rim movement/Pull](ROTATED_RIM_CYLINDER.md).
- [x] Shared live preview, numeric input, Cancel, one-step cached Undo/Redo,
  Part identity/appearance/placement/browser continuity and native Save/Open.
  Phase 9E verifies confirmed-only Save and fresh editing after Open; history
  remains session-local. See [history/persistence](FACE_HISTORY_PERSISTENCE.md).
- [x] Existing integration validation: complete Release build passed; the single
  full CTest run initially passed **82/86**, followed by targeted corrections and
  passing results for **all 86 cases across those runs**. This is not one clean
  full-suite pass on every later commit. User-reported geometry/crash-phase
  desktop acceptance: **47/47 passed**. See the
  [integration review](V049_INTEGRATION_REVIEW.md) for dated scope and limitations.
- [x] Phase 9F.1B documented **5/5** focused passes. Phase 9F.2 corrects production
  panel positioning for asynchronous native/widget geometry and screen/DPR
  changes, with **8/8** focused passes, synthetic **100%, 125%, 150%, 200%** Qt
  screens and available single-monitor KDE/xcb verification. See
  [cross-screen HiDPI results](CROSS_SCREEN_HIDPI.md). These are existing results;
  roadmap conflict resolution does not rerun builds, tests or manual acceptance.
- [ ] Physical mixed-DPI multi-monitor acceptance: real KWin boundary/spanning
  behavior, decoration timing, focus, scale changes and hotplug remain pending.
  Native Wayland positioning, Windows/macOS and a standalone bundled distribution
  are not certified by this phase.

Implementation is complete within the documented analytic certificates; broader
blind/unproved closures, interacting or freeform geometry and reconstruction
outside numerical/topology budgets remain safe diagnostic refusals. Advanced
joint Pull/Resize and boss-transition extensions remain planned below.

### Original cylindrical-face requirements (Issue #51)

The following retains the original Workplane/Sketch-to-9A–9E rollout requirements,
including the boundary-policy design. These historical requirements are not a
claim of unrestricted implementation; the delivered scope and acceptance are
recorded above. Blind holes and uncertified analytic/Boolean extensions remain
future work.

- **Move Cylindrical Face:** extend direct face editing beyond existing
  planar Move Face and cylinder Pull/Resize. Translate a selected **internal
  cylindrical face (hole)** or **external cylindrical face (boss / cylindrical
  protrusion on a planar face)** laterally, perpendicular to its own axis, by
  manipulator or exact numeric input; preserve diameter, direction and height.
- **Reposition holes:** move the complete hole axis/inner wall and openings
  without changing the exterior Part or other features. Rebuild the adjacent
  planar faces and any affected edges to obtain a valid B-Rep solid.
- **Reposition bosses:** move a complete **attached cylinder on a face**,
  including its cylindrical mantle, cap and footprint on the support face, so
  the support face closes at the old location and opens at the new one. The
  cylinder remains attached to the Part, not a detached separate solid.
- **Boss boundary policy:** offer explicit **Trim / Extend** in the existing
  Move Face Command Panel. Trim clips against extended original exterior
  boundary surfaces; Extend preserves the complete translated cylinder and
  reconstructs connected supporting material. Allow legitimate topology changes,
  validate B-Rep and material connectivity, and refuse ambiguous/invalid results
  atomically without switching modes. Plan geometry, live preview and transaction/
  Save/Open tests in **9C–9E**, with no implementation during architecture **9A**;
  see the [phase 9 boundary reconstruction plan](CYLINDRICAL_MOVE_PLAN.md).
- Start with exact and tractable examples: a complete circular **through-hole
  in a block** and a complete circular **boss on a planar block face**.
  Subsequently consider blind holes, multiple features and more complex analytic
  Boolean B-Reps. Refuse ambiguous trims, collisions or invalid transitions
  atomically and preserve the original solid.
- Reuse CoCreate SolidDesigner/Modeling 2008 v16.0 selection/workflow as a
  reference, OpenDesigner's live preview, exact numeric input, OK/Cancel,
  Undo/Redo, native Save/Open and Part-placement handling.
- Add tests for moved holes **and** moved attached bosses, exact geometry,
  connected/manifold solids, unaffected faces, transformed Parts, safe refusal
  and GUI/history regressions. Plan this **after** Workplane (6B) and Sketch
  improvements, not as part of the current v0.4.2 delivery.

See [planned cylindrical-face move: holes and bosses](https://github.com/ProtonJeroen/OpenDesigner/issues/51).

## Planned — Advanced geometry-driven Pull/Resize and multi-face reconstruction

These are **known remaining capability gaps after v0.4.9 phase 9D.7D**, not
completed functionality. Apply the project-wide geometry-first, history-independent
B-Rep principle: recognize all faces from their current surfaces and topology
regardless of creation method, trims or neighboring geometry.

- [ ] **Independent-feature joint Pull:** allow compatible faces belonging to
  multiple distinct features on one or more Parts to be selected and resized
  together with an explicit, coherent operation policy; preserve each feature's
  geometric relationships and reject incompatible or ambiguous combinations safely.
- [ ] **Mixed cap/wall joint Pull:** support coordinated Pull/Resize of planar
  cylinder caps and cylindrical mantles in one selection, with well-defined
  diameter-versus-height behavior, direction, reference geometry and result
  reconstruction. Existing joint selection of split faces belonging to the
  same cap must continue to work.
- [ ] **Complex boss attachments:** reconstruct bosses supported by multiple,
  intersecting or otherwise complex neighboring faces rather than requiring
  simple planar stock interfaces. Preserve unrelated material and allow valid
  topology changes, with explicit refusal for uncertifiable cases.
- [ ] **Nonconstant-section boss caps and transitions:** extend support beyond
  constant-section cylindrical boss cap patterns; infer the actual geometric
  supports and boundaries and handle valid varying/oblique transitions without
  requiring primitive history or circular rim assumptions.
- [ ] Reuse shared B-Rep neighborhood, topology correspondence and independent
  material/volume certification for Move/Rotate/Pull/Resize/Delete. Preserve
  preview, numeric input, Cancel, Undo/Redo, Part identity and Save/Open.
  Add targeted geometry/session regressions and manual Fedora KDE acceptance
  for each independently scoped extension; avoid duplicating algorithms.

## Priority — Workplane, reference and Sketch geometry foundations (v0.5.3)

Priority decision (2026-11-12): **v0.5.2 = conical edge blends** (see below);
this Workplane, Axis, Point, Sketch and reference foundation moves to **v0.5.3**.

Agreed priority (2026-11-11): finish the current Fillet/Chamfer
acceptance first, then strengthen these foundations **before Sweep, Loft and
broader geometric alignment**. This is planned work, not an implementation claim.
Every new modeling tool must explicitly assess applicable Workplane, Axis, Point,
Sketch and projected-geometry references during design and acceptance.

- [x] Complete the planned Workplane activation, frame visibility, minimum
  extent and Move/Rotate preview improvements below. Keep camera alignment an
  explicit action independent of Workplane activation. (v0.5.3)
- [x] Share consistent target/reference picking and snapping for construction
  objects, Sketch entities and solid geometry. Support points, endpoints,
  centers and unambiguous intersections/relevant curve points; state each
  operation's supported reference types explicitly. (v0.5.3: `References`
  resolver and `CadViewport::pickReference`, used by Move Face direction and
  construction role picking; see [Workplane references](WORKPLANE_REFERENCES.md).
  Other commands, e.g. Rotate/Align Face axes and Revolve axes, still use their
  own pickers and migrate later.)
- [x] Resolve directions, axes and positions in world coordinates after Part
  and Workplane transforms, including references from other Parts/Workplanes.
  Integrate the planned Move Face geometric-direction controls below. (v0.5.3)
- [ ] Extend Sketch geometry to **ellipses and elliptical arcs**, including
  editing/trimming and downstream profile construction; existing ellipse
  drawing alone does not establish complete modeling support.
- [ ] Preserve **exact projected curves** where representable, including
  elliptical and spline/B-spline results from solid edges. Define supported
  projection modes and clear diagnostics for ambiguous/degenerate results.
  Do not silently replace exact curves with polyline approximations.
- [ ] Add **2D multipoint interpolating splines** on a Workplane: initially
  curves passing through supplied points, with point insertion, movement and
  removal, and open/closed modes. Define closure and tangent behavior.
  Control-point B-spline editing is a separately scoped follow-up.
- [ ] Extend trimming, connectivity, closure/self-intersection diagnostics and
  the shared certified Profile Builder for supported mixed contours containing
  lines, arcs, circles, ellipses/elliptical arcs and spline curves, including
  holes and disjoint regions where certified.
- [ ] Carry supported curves through Extrude and Revolve and subsequently
  Sweep/Loft; display-only support is insufficient. Document operation-specific
  limits and refuse unsupported profiles atomically with useful diagnostics.
- [ ] Preserve exact geometry, reference coordinates and editability through
  duplication, transforms, native Save/Open and Undo/Redo, with focused
  geometry/session regressions and manual acceptance.
- [x] Evaluate geometric references at operation time without introducing
  persistent parametric dependencies; associativity is a separate future scope.
  (v0.5.3: stale/deleted/hidden references are refused, never rebound.)

## Next milestones
- v0.5.2 conical Fillet/Chamfer: manual Fedora KDE/Wayland acceptance (FILLET_CHAMFER.md)
- v0.5.3: Workplane, reference and Sketch geometry foundations above (implemented;
  manual Fedora KDE/Wayland acceptance pending, see WORKPLANE_REFERENCES.md)
- v0.5.4: view-level Show/Hide Workplanes and Move Face by Points (implemented;
  manual Fedora KDE/Wayland acceptance pending, see WORKPLANE_REFERENCES.md and
  MOVE_FACE.md)
- v0.5.5: exact ellipse/elliptical arc Sketch modeling, exact projection and 2D
  interpolating splines on the v0.5.3 reference model (moved from v0.5.4)
- Sweep (including 3D spline paths), then Loft, after the foundation gates
- Broader cylindrical-face movement (holes and bosses) beyond the certified v0.4.9 scope
- Assemblies and 3D copilot
- STEP, IGES, STL
- GLB/glTF and FBX import/export

## Planned — Move Face direction from geometric references

- [x] Extend **3D Modeling > Move Face** with an explicit **Direction** reference
  selector. Preserve the current face-normal/manipulator direction as the default.
- [x] Allow direction from a selected **Face** (its normal, with explicit
  orientation), a straight **Edge** (tangent/axis), a **Sketch line**
  (line direction), an **Axis** or a **Workplane** normal, independent of how
  the target Part was created.
- [x] Allow **Point-to-Point** direction from two picked Sketch points;
  calculate the signed 3D vector from the first point to the second. Support
  construction points, Part vertices and snapped Sketch points where their
  positions are unambiguous.
- [x] Support **Reverse Direction**, signed exact distance, and live arrow/blue
  preview. Keep the **target Face** separate from the **direction reference**;
  references may belong to another Part or Workplane without editing them.
- [x] Resolve references in world coordinates, respecting Part and Workplane
  transforms; avoid reliance on primitive metadata or feature history.
  Reject zero-length vectors, ambiguous curved-face normals/curved-edge tangents
  and unsupported reconstruction with clear diagnostics.
- [x] Preserve existing OK/Cancel, Undo/Redo, Part identity and native Save/Open;
  test transformed geometry, all reference types, reverse/signed input,
  reference independence and safe refusal. (v0.5.3, planar Faces.)
- [x] **Move Face by Points**: displacement = full vector from a picked start
  point A to an end point B (Vertex, Point, Sketch point, center,
  intersection), no numeric input, no Reverse; a planar Face moves by
  `(B − A)·n`, so with A on the Face it lands in the plane through B. Coincident,
  parallel, stale/deleted/hidden and unsupported cases refused atomically.
  (v0.5.4, `move-face-by-points-sessions`.)
- [ ] Direction references for cylindrical hole/notch/boss walls (currently
  refused; they keep the v0.4.9 lateral reference controls).

## Planned — View display modes and Workplane activation workflow

- [x] Add **View > Wireframe** and **View > Shaded** display modes, with
  corresponding toolbar actions to switch the current 3D viewport mode.
  Keep the chosen display mode independent of Part selection and editing tools.
- [x] Add a **Show/Hide 3D Geometry** toggle to the View menu and toolbar.
  Temporarily hide or show all 3D solid geometry without changing individual
  Part visibility flags or deleting geometry. Keep Sketches, Workplanes and
  construction references available while solids are hidden.
- [x] Change **Edit Sketch** to activate the Sketch's Workplane (WP) and enter
  the sketch-editing context **without automatically rotating the camera**.
  Preserve the current camera position, orientation and zoom.
- [x] Provide a separate explicit **Look at Workplane / Normal to WP** view
  action, so users may choose when to align the camera to the Workplane.
  Activating a WP and rotating the view must be independent operations.
- [x] Preserve viewport mode, temporary 3D-geometry visibility and user-chosen
  camera orientation across appropriate tool changes; define any Save/Open
  persistence behavior explicitly (view modes are not saved). Selection/picking
  respects hidden solids. Fedora KDE consistency is part of manual acceptance.
- [x] Add focused viewport, Sketch session and UI regressions, including
  switching Wireframe/Shaded, toggling 3D solids and Edit Sketch without
  camera rotation (`workplane-reference-sessions`, `sketch-drawing`).
- [x] Add a **Show/Hide Workplanes** view toggle next to Show/Hide 3D Geometry:
  Workplane frames, rings and their Sketches are not drawn, pickable or snap
  targets; Axes/Points/solids unaffected; flags, `.odc` and Undo untouched;
  activation, Edit Sketch and new Workplanes show them again. (v0.5.4,
  `workplane-visibility-sessions`.)

## Planned — Workplane frame visibility and minimum extent

- [x] **Fix WP Move/Rotate preview:** the Workplane frame/ring must remain
  visible throughout translation and rotation previews, including active and
  inactive appearance, without flicker or disappearing behind the manipulator.
  Keep the preview aligned with the transformed WP and restore the original on Cancel.
- [x] **Minimum Workplane frame extent:** enforce at least **100 × 100 mm** in
  Workplane-local coordinates, centered around the content-derived frame when
  smaller. Larger Sketch contents must continue to expand the frame dynamically;
  never clip them or change underlying Sketch/Workplane geometry.
- [x] **Reduce active green frame ring width from 15 mm to 10 mm**; keep its
  open-ring styling and existing transparency unless separately changed.
  Apply consistent geometry to active/inactive and transformed previews.
- [x] Keep these as presentation-only changes; preserve picking, transforms,
  Undo/Redo, active-WP state and native Save/Open. Add focused normal/HiDPI
  viewport checks for small/empty WPs, Move/Rotate previews and Cancel. (v0.5.3)

## Planned — Sweep, Loft, and Materials & Colors

These are **future milestones, not yet implemented**. Proposed numbering and order
are provisional and should be agreed before implementation. The shared
`ModelingOperation` contract from v0.4.8 (New Solid, Add Material, Remove
Material), existing certified Profile Builder, OCCT Boolean utilities,
Command Panels, target picking and history should be reused rather than
duplicated.

### Sweep — profile along a path (after Sketch/reference foundations)

- [ ] **3D Modeling > Sweep**: sweep one certified, closed 2D profile along a
  selectable continuous path; start with lines, circular arcs and connected
  segment/arc paths, including transformed Sketch/workplane references.
- [ ] Support **New Solid**, **Add Material** and **Remove Material** with the same
  operation selection and eligible Active Part / explicit viewport target override
  semantics as Extrude and Revolve.
- [ ] Add **3D multipoint interpolating spline paths** through spatial points
  not restricted to one Workplane. Support creating/editing path points and
  using construction/Sketch/solid references where unambiguous; persist exact
  curves through transforms, duplication, Save/Open and Undo/Redo.
- [ ] Support both planar and spatial Sweep paths, including certified spline/
  B-spline paths, in addition to the initial line/arc rollout. Explicitly stage
  delivery; 3D spline support is required for the completed Sweep milestone.
- [ ] Define deterministic section orientation along the path, path direction,
  closed-path handling, corner transitions and self-intersection refusal before
  implementation. For 3D paths, specify the moving frame and predictable
  profile rotation/twist behavior, including inflections and near-zero curvature;
  reject undefined frames or offer an explicit, documented reference.
  Reject invalid/non-manifold or ambiguous results atomically.
- [ ] Live result preview; exact inputs where applicable; OK/Cancel/Escape,
  one-step Undo/Redo, stable target Part identity/properties and native Save/Open.
- [ ] Focused kernel, session, Boolean, history and persistence tests; native
  Fedora KDE acceptance before publication.
- [ ] Future extensions: controlled twist, variable scale and guide rails,
  separately scoped rather than promised in the initial milestone.

### Loft — solid through multiple sections (after Sweep)

- [ ] **3D Modeling > Loft**: create a valid solid between at least two ordered,
  certified closed Sketch profiles on different Workplanes; support more than
  two sections when geometrically valid.
- [ ] Support **New Solid**, **Add Material** and **Remove Material**, reusing
  existing target selection, Boolean result validation and history.
- [ ] Define section ordering, orientation, matching/correspondence of vertices
  and edges, periodic seams and transitions between different profile shapes.
  Support circular-to-rectangular and other mixed-section examples only where
  OCCT can produce a certified valid solid; do not silently repair ambiguity.
- [ ] Live result preview, stable target identity, safe Cancel/Escape,
  Undo/Redo and native Save/Open.
- [ ] Focused geometry, session, Boolean and persistence regressions; native
  Fedora KDE acceptance before publication.
- [ ] Future extensions: guide rails, tangent/continuity constraints and
  advanced surface lofts.

### Fillet & Chamfer — v0.5.0 creation and feature-aware editing

Single-edge constant-radius Fillet and equal-distance Chamfer are implemented
through the existing command panel, blue previews and FaceChange history.
Separated transitions now support certified geometric recognition, Edit commands,
orange dimension dragging, sharp Delete Face restoration and Keep ON/OFF during
supported planar Move/Pull and supporting-cap Rotate. Optional native recipes
retain sharp support geometry and are checked against the current B-Rep.
Initial creation was manually accepted on Fedora KDE; new feature-aware controls
still require manual acceptance. Targeted tests only were run for this update.
See [implementation, limitations and Fedora KDE checklist](FILLET_CHAMFER.md).
The broader multi-edge milestone below remains future work.

- [ ] Add separate **3D Modeling > Fillet** and **3D Modeling > Chamfer**
  commands, accessible from toolbar and appropriate edge context menus.
- [ ] **Fillet (round)**: apply a constant-radius blend to one or multiple
  selected solid edges; support internal/concave and external/convex edges
  where the resulting B-Rep is valid. Provide exact radius input (mm).
- [ ] **Chamfer (bevel)**: bevel one or multiple solid edges with exact
  equal-distance input initially; later add asymmetric two-distance and
  distance-angle modes, with an unambiguous face-side convention.
- [ ] Reuse typed Edge selection and allow multi-edge selection on the same
  Part; explicitly handle or reject adjacent-edge junctions, tangencies,
  short edges and overlapping treatments without corrupting geometry.
- [ ] Show a live, non-destructive blue result preview with OK/Cancel/Escape;
  preserve Part UUID, appearance, placement, browser order, single-command
  Undo/Redo and native Save/Open.
- [ ] Reuse OCCT B-Rep fillet/chamfer algorithms and existing shape validation
  and normalization. Refuse failed, degenerate or non-manifold results safely,
  leaving the original Part unchanged.
- [ ] Cover box edges, internal/outer edges, multiple edges, transformed Parts,
  valid/invalid radii or distances, edge junctions, preview cleanup,
  Undo/Redo and persistence with focused headless and session tests.
- [ ] Stage development for efficiency: **(A)** constant-radius single-edge
  Fillet, **(B)** equal-distance single-edge Chamfer, **(C)** multi-edge/junction
  handling and expanded modes, followed by manual Fedora KDE acceptance.
- [ ] Advanced variable-radius fillets, face-to-face blends, setback corners,
  and complex freeform transitions remain future work; no blanket claim that
  all OCCT-valid geometry is editable.

### Materials & Colors — Part and Face appearance (future milestone)

- [ ] **Part-level appearance**: edit an individual Part's default color and
  visual finish through the Structure Browser, viewport context menu and/or a
  properties panel. Existing Part color and native persistence are the starting
  point; preserve backward compatibility.
- [ ] **Part-level engineering material**: choose a built-in material (e.g.
  steel, stainless steel, aluminum, plastics) or define a custom material with
  density and appearance attributes. Compute volume and mass from the certified
  solid and material density, with explicit units.
- [ ] **Per-Face appearance overrides**: select one or multiple Faces and assign
  a color and/or surface finish independent of the Part's engineering material.
  Unassigned Faces inherit the Part appearance. Provide **Reset to Part
  Appearance**; an appearance override must not silently change mass/density.
- [ ] Support color, transparency, metallic/roughness and other feasible OCCT
  viewport properties consistently, while distinguishing engineering material
  from display appearance.
- [ ] Ensure Undo/Redo, duplication, transforms, visibility, Structure Browser
  updates and native Save/Open preserve both Part-level and Face-level values.
- [ ] Define a **topology-change policy** for Extrude, Revolve, Boolean and
  Move/Rotate/Align/Delete Face: keep a Face override only where mapping to the
  resulting Face is reliably established; new or ambiguous Faces inherit the
  Part appearance instead of misassigning a stored color. Test split/merged/
  deleted Faces, Undo/Redo and Save/Open.
- [ ] Implement incrementally: **(A)** Part colors/material library and mass,
  **(B)** Face overrides/inheritance/reset, **(C)** topology-safe transfer.
  Focused tests and manual Fedora KDE acceptance at each stage.

### Related future milestones (provisional ordering)

- [ ] **Broader Move Cylindrical Face reconstruction:** extend the certified
  v0.4.9 hole/boss implementation to the remaining Issue #51 cases described above.
- [ ] **Assemblies and Structure Browser**:
  hierarchical single-file Parts/subassemblies with optional externally stored
  components and explicit assembly-level transforms.
- [ ] **Geometric Align**: align Parts and assemblies using
  Face-to-Face, Axis-to-Axis, Vertex-to-Vertex and Point-to-Point references,
  including points derived from Sketch/Workplane geometry.
- [ ] **CAD exchange**: STEP/IGES/STL, followed by GLB/glTF
  and FBX import/export, with documented geometry/material/assembly fidelity.
- [ ] **Standard Parts library** and wider technical material presets; evaluate
  library integration with Assemblies and Materials & Colors.
- [ ] **Platform-independent Touch Mode** in Settings for desktop and future
  iPadOS/Android tablet targets; keep conventional desktop controls available.
- [ ] **3D copilot/local AI** as a later, separately scoped milestone.

## Planned — Platform builds, packaging and tablet validation

Agreed targets (2026-11-11); these are planned deliverables, not certified builds.

- [ ] Use shared CMake presets and pinned compatible Qt/OCCT dependencies for
  Linux x86_64, Windows x64 and macOS Apple Silicon ARM64 (local Mac M1).
  Establish native build/test workflows and automated platform artifacts.
- [ ] Package Linux AppImage, Windows portable ZIP and macOS app/DMG with
  required libraries/plugins/resources. Select an older supported Linux build
  baseline and publish tested distro/graphics/runtime requirements; do not
  imply universal AppImage compatibility. Intel macOS is a later target.
- [ ] Test Linux releases on Ubuntu, Fedora KDE and Debian; separately validate
  relevant X11/Wayland, graphics and HiDPI behavior. Add clean-machine Windows
  and macOS installation/runtime checks.
- [ ] After desktop build foundations, create iPadOS and Android proof builds
  for startup, viewport, model opening/saving and basic modeling. Validate
  mobile Qt/OCCT builds and graphics compatibility.
- [ ] **Mouse and keyboard are acceptable for initial iPadOS/Android testing**;
  resolve platform input shortcuts, pointer behavior and file access as needed.
  Touch/stylus UI optimization is not a prerequisite for these technical builds.
- [ ] Later implement platform-independent Touch Mode in Settings and optimized
  tablet/stylus interaction, retaining conventional mouse/keyboard controls.

### v0.5.1 — circular-edge Fillets

- [x] Extend the existing constant-radius OCCT engine to complete circular contours.
- [x] Plane/cylinder recipes for outer rims, through-hole rims and boss feet;
  unique toroidal transition recognition, absolute Edit Fillet and sharp deletion.
- [x] Reuse orange radius Pull, blue preview, native recipes and history.
- [x] Certified perpendicular planar cap Move/Pull with circular recipes and Keep ON/OFF; preserve unaffected transitions.
- [ ] Circular cylindrical-wall radial/lateral edits and supporting-face Rotate.
- [ ] Imported circular transition recovery without native recipes.
- [ ] Editable split supporting/transition face sets and general analytic carriers.
- [ ] Manual DellJV Fedora KDE/Wayland acceptance (see FILLET_CHAMFER.md).

Elliptical, spline and freeform fillets remain future work. Circular contour
creation does not imply arbitrary curved support reconstruction is available.

### v0.5.2 — conical edge blends

- [x] Analytic certification of plane/cone Fillets (coaxial torus, G1 on full
  circles) and equal-distance Chamfers (coaxial cone band, distance measured
  along each support from the sharp edge); ambiguity rule and body-cone guard.
- [x] Recipe-free recognition of conical solids (two planar caps, one body cone,
  one transition per cap) with sharp frustum replay and material equivalence.
- [x] Conical Fillet and Chamfer creation through existing kernel paths; recipe
  `cones` support persisted as edge-blend recipe version 3 (document schema unchanged).
- [x] Edit Fillet / Edit Chamfer, Delete with sharp reconstruction, Undo/Redo,
  Save/Open, Duplicate (v0.5.1 recipe-copy convention).
- [x] Planar cap Pull/Move with Keep ON/OFF; unrelated transitions preserved.
- [x] Safe refusals: cap Rotate (coaxiality), body-cone Pull/Move/Delete,
  stepped/ambiguous/unequal geometry.
- [ ] Manual DellJV Fedora KDE/Wayland acceptance (normal and HiDPI).
- [ ] Optional/future: cap rotation with circular transitions, pointed cones,
  conical holes with blends, plane/cylinder rim Chamfer recipes, recipe-free
  cylinder recognition.

### v0.5.3 — Workplane and reference foundation

See [Workplane references](WORKPLANE_REFERENCES.md) for the design.

- [x] Synthetic `direct-modeling-recovery-document` fixture, no private `.odc`
  dependency (separate PR, work package 0).
- [x] Camera decoupling: Workplane activation / Edit Sketch never move the
  camera; explicit **View → Look at Workplane** (+ 2D toolbar and context menu).
- [x] Workplane frame: 10 mm ring, 100 × 100 mm minimum opening, visible blue
  frame during Move/Rotate previews.
- [x] Axes and Points: Axis from conical faces, Axis normal to a Face/Workplane
  at a point, Point from Sketch point/center/intersection and Axis from Sketch
  line through shared reference picking.
- [x] Unified reference resolution (`References`), picking and snapping for
  Part, Face, Edge, Vertex, Axis, Workplane, Point, Sketch line and Sketch
  points (endpoint, midpoint, center, construction point, intersection), with
  stale/hidden/deleted refusal.
- [x] Move Face direction from Edge, Sketch line, Axis, Face, Workplane and
  Point to Point, with Reverse and exact distance (planar Faces).
- [x] View → Wireframe / Shaded and Show/Hide 3D Geometry (view-level, unsaved).
- [ ] Manual DellJV Fedora KDE/Wayland acceptance at 100 % and 200 %
  ([checklist](WORKPLANE_REFERENCES.md#10-manual-acceptance-fedora-kdewayland-100--and-qt_scale_factor2)).
- [ ] Open: reference picking for Rotate/Align Face and Revolve axes through the
  shared picker; direction references for cylindrical walls; solid-edge
  intersection points; persistent (associative) references remain out of scope.

### v0.5.4 — Show/Hide Workplanes and Move Face by Points

- [x] **View → Show Workplanes** toggle (menu and 3D toolbar): hides Workplane
  frames, active rings, labels and all Sketches on Workplanes for drawing,
  picking, snapping and reference resolution; Axes, Points and solids stay.
  View state only (no flags, `.odc`, Undo or dirty change); exact restore,
  individually hidden objects stay hidden; independent of Show/Hide 3D Geometry
  and Wireframe/Shaded. Activating a Workplane, Edit Sketch and creating or
  editing a Workplane switch it back on
  ([details](WORKPLANE_REFERENCES.md#56-showhide-workplanes-v054)).
- [x] **Move Face by Points**: A → B displacement through the shared reference
  picker/resolver and the certified planar Move Face path with offset
  `(B − A)·n`; read-only `|AB|` and normal offset; picking again replaces B,
  *Pick start point again* restarts A; one Undo step on OK
  ([details](MOVE_FACE.md#move-face-by-points-v054)).
- [ ] Manual DellJV Fedora KDE/Wayland acceptance at 100 % and 200 %
  (WORKPLANE_REFERENCES.md §10 items 8–12, MOVE_FACE.md By Points checklist).
- Moved to **v0.5.5**: exact ellipse/elliptical arc Sketch modeling, exact
  projection and 2D interpolating splines.
