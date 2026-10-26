# OpenDesigner roadmap

## v0.1 — foundation
- Qt 6 native desktop application
- Open CASCADE 3D viewport
- Dockable structure browser
- Box creation and basic navigation
- Linux CI build

## Next milestones
- CAD document ownership, persistent save/load and robust selection
- Direct face modeling and workplanes
- Assemblies and 3D copilot
- STEP, IGES, STL
- GLB/glTF and FBX import/export

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
  (114.97 seconds, isolated Xvfb/xcb/software OpenGL), existing 2D/3D
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
