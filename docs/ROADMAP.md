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
