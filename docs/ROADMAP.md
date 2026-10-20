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

OpenDesigner targets Windows, Linux and macOS. This roadmap describes planned work, not finished features.

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

## v0.3.6 — further topology improvements (planned)

- Extend controlled topology reconstruction beyond redundant seams, including
  carefully validated support-disappearance events.
- Broaden coverage for complex Boolean and curved-neighbor configurations with
  independent expected-shape checks and safe refusal where reliability is unproven.
- Keep freeform/blend reconstruction, multi-shell solids and persistent subshape
  naming outside the v0.3.5 support claim; revisit their scope separately.
