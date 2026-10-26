# OpenDesigner roadmap

**Start here:** [Full product roadmap and planned features](#full-product-roadmap-planned-features) lists all agreed feature areas, current gaps, tentative ordering and longer-term platforms. The version sections further down retain implementation history and test evidence.

## v0.1 — foundation
- Qt 6 native desktop application
- Open CASCADE 3D viewport
- Dockable structure browser
- Box creation and basic navigation
- Linux CI build

## Next milestones
- CAD document ownership, persistent save/load and robust selection
- Direct face modeling and workplanes
- After Workplanes and Sketches: move cylindrical faces (holes and bosses)
- Assemblies and 3D copilot
- STEP, IGES, STL
- GLB/glTF and FBX import/export

## Full product roadmap (planned features)

This is the **consolidated OpenDesigner feature plan**, updated **2026-10-26**.
The version sections below document work already implemented or being tested;
this section also tracks the longer-term wishlist previously agreed for the
product. It is a **planning document, not a claim that every item exists**, and
does not assign delivery dates.

**Product direction:** open-source GPL-3.0 3D *direct modeling*, inspired in
workflow by **CoCreate SolidDesigner/Modeling 2008 v16.0**. Editing must not
require a parametric feature-history tree. Implement independently in
Qt 6 / C++20 / Open CASCADE, without distributing proprietary CoCreate assets.
Keep existing desktop workflows while expanding to other input devices.

**Status key:** `[x]` implemented in the stated supported scope;
`[ ]` planned/not yet complete; `[?]` candidate or design decision requiring
later prioritization. Partial support is described explicitly rather than
marked complete. Priorities are *relative*, and phases may overlap.

### Milestone overview and dependencies

| Order | Milestone | State / scope |
| --- | --- | --- |
| Foundation (v0.1–v0.4.2) | Desktop CAD viewport, primitives, direct edits, Booleans, native documents, Workplanes, Sketch basics and projection | Implemented with documented limitations |
| **Next: 6B.2** | Authentic SolidDesigner Workplane border and correct live blue Construction preview | **Planned**, [Issue #48](https://github.com/ProtonJeroen/OpenDesigner/issues/48) |
| After Workplane UI | Finish usable closed 2D profiles, curve snapping and Sketch operations | **Planned / partially implemented** |
| After Workplanes and Sketches | Lateral Move Cylindrical Face: move holes **and** attached cylindrical bosses | **Planned**, [Issue #51](https://github.com/ProtonJeroen/OpenDesigner/issues/51) |
| Solid-from-sketch | Extrude, Revolve and Sweep using validated 2D profiles | **Planned** |
| Advanced direct modeling | Broader face/topology editing, curved neighbors and finish operations | **Planned**, some individual operations already supported |
| Assemblies & native documents | True hierarchy, occurrences, geometry-driven Part/Assembly Align and Sketch point-to-point positioning | **Planned**, single-file multi-Part root already supported; [Issue #56](https://github.com/ProtonJeroen/OpenDesigner/issues/56) |
| Interoperability | STEP, IGES, STL, GLB/glTF and FBX | **Planned**, no general-purpose production importer/exporter claimed |
| **UI/UX productization (release prerequisite)** | Replace prototype interface with coherent, intuitive desktop UX, design system, discoverable commands and user-tested workflows | **Planned before broad multiplatform rollout**, [Issue #55](https://github.com/ProtonJeroen/OpenDesigner/issues/55) |
| Multiplatform & input | Windows/macOS builds, Linux maturity, optional touch across platforms, iPadOS and Android tablets | **Planned after UI/UX readiness for broad releases**; technical CI exploration may run in parallel |
| Stable 1.0 | Reliable tested CAD workflows, cross-platform packages, documentation and compatibility | **Release goal; no date committed** |

The order above defines **dependencies**, not a rigid promise that all features
within one stream must finish before work in another. Issue-specific PRs and CI
remain the authority for actual implementation status.

### 1. Workplanes and 2D Sketch (highest near-term priority)

- [x] Independent Workplanes, Axes and Points; XY/XZ/YZ presets, editing,
  transforms and native storage.
- [x] 2D Profile and Construction primitives: lines, infinite lines, circles,
  arcs, ellipses/elliptical arcs and local construction points; numeric drawing,
  basic snapping, Overdraw, offsets and supported common tangents.
- [x] Independent **Horizontal** and **Vertical Infinite Construction Line**
  commands with one-click or numeric local Y/X offset, alongside the unrestricted
  two-point line. Landed via [PR #53](https://github.com/ProtonJeroen/OpenDesigner/pull/53);
  linked [Issue #50](https://github.com/ProtonJeroen/OpenDesigner/issues/50).
- [x] **6B.1:** dynamically sized rectangular Workplane bounds and infinite-line
  clipping; lines outside the prior bounds enlarge the frame by perpendicular
  offset. Landed via [PR #49](https://github.com/ProtonJeroen/OpenDesigner/pull/49).
- [ ] **6B.2:** CoCreate-like Workplane as a **15 mm wide** rectangular ring in
  local model units, with **50% opacity**, **active green / inactive light grey**;
  **no interior face at all**. Preserve see-through visibility, picking and
  snapping. Include 10% free interior geometry margin **plus 15 mm frame width
  on every outside edge**; keep the opening nondegenerate and clip infinite
  lines to the **outermost** edge. Subtle WP label; no large idle XYZ arrows.
- [ ] **6B.2 live-preview fix:** blue *temporary* Construction geometry
  **must be visible before OK**, even when it falls outside the earlier
  Workplane bounds. The frame and blue AIS geometry must share the same
  temporary enlarged rectangle during drawing, projection and offset,
  including numeric changes. **Cancel** restores the previous scene/history;
  **OK** must not make previously hidden geometry suddenly appear.
  Cover finite and infinite lines, circles, projected contours and
  rotated Workplanes; see [Issue #48](https://github.com/ProtonJeroen/OpenDesigner/issues/48).
- [ ] Complete missing analytic **circle–ellipse and ellipse–ellipse
  intersections**, coincident-support cases and elliptic tangent snapping.
  Respect exact domains, locks, degeneracies and snap priorities.
- [ ] Extend **elliptic Overdraw**, including bounded arcs, graph/domain
  handling, reverse traversal and safe ambiguity refusal. Extend applicable
  offset/tangent tools to further supported conic combinations.
- [ ] **Closed-profile analysis and validation:** connected wires, holes/islands,
  self-intersections, overlaps, orientations and exact tolerances. Independent
  Profile geometry should be reliably usable by future 3D solid commands.
- [ ] Expand 2D creation and editing where needed, including an interactive
  Construction Point workflow and reliable multiple-entity edit/selection.
- [?] Parametric 2D dimensions/constraints and additional curve types
  (e.g. splines) are *later design candidates*, **not** prerequisites for
  history-free direct modeling or the current Sketch deliverables.

### 2. Create 3D solids from Workplane profiles

- [ ] **Extrude / Pull Profile:** make or remove solid material from a validated
  closed 2D contour; explicit direction/distance, add/subtract choices, numeric
  input, live preview and safe intersections. Support profiles with inner holes.
- [ ] **Revolve:** rotate a validated 2D section around a selected
  Workplane/Construction axis or geometric reference; exact angle/full turn.
- [ ] **Sweep:** sweep a profile along an eligible path with controlled
  orientation, failure checks and geometric fidelity.
- [ ] Integrate all three with multi-Part document ownership, existing
  Boolean operations, transformations, Undo/Redo and native Save/Open.
  Output should be real analytic B-Rep solids, not display-only mesh bodies.
- [?] Loft, more complex profile treatments, associative dependencies and a
  persistent parametric feature-history workflow are **possible extensions**,
  not committed requirements for the direct-modeling core.

### 3. Direct solid modeling and exact geometry

- [x] Box, Cylinder, Sphere and Cone primitives with available Pull/Resize
  controls; Move/Rotate Part, multi-selection, duplication, exact input,
  Unite/Subtract/Intersect and supported planar Move/Rotate/Align/Delete Face.
  These are real foundations, **not unrestricted arbitrary B-Rep editing**.
- [ ] **Move Cylindrical Face laterally** after Workplanes and Sketches:
  reposition an entire **inner cylindrical hole** or an **outer cylindrical
  boss attached to a planar face**. Keep diameter, direction and depth/height,
  reconstruct openings/support face/connected solid and preserve the rest
  of the Part. Start with simple through-holes and attached bosses; later blind
  holes and complex Booleans. [Issue #51](https://github.com/ProtonJeroen/OpenDesigner/issues/51).
- [ ] Support a broader set of direct **Move / Rotate / Align / Delete Face**
  operations across curved neighboring surfaces, multiple selected faces,
  internal cavities and topology transitions. Provide controlled safe refusal
  rather than broken solids; maintain an accurate Part/face selection mapping.
- [ ] Extend exact manipulation of cylinders, cones and other supported
  analytic B-Reps beyond the primitive-specific Pull commands. A moved hole
  must be geometrically different from merely resizing its diameter.
- [ ] Add or broaden classic finishing operations:
  **fillet/blend**, **chamfer**, **shell/thickness**, and suitable **draft/offset
  faces**. These are *planned functional areas*; detailed UX/OCCT acceptance
  criteria and order still need individual issues before coding.
- [ ] Improve transformations with freely positionable pivots and reference
  axes, exact constraints, multi-face and multi-Part operations, reliable
  snapping and 3D CoPilot-style manipulators, while keeping explicit commands
  and OK/Cancel behavior.
- [ ] Grow topology-safe edits from simple analytic one-solid cases to more
  complex Boolean results and accepted multi-solid structures, with exact B-Rep
  validity/material checks and clear feedback for unsupported cases.

### 4. Structure Browser, documents and assemblies

- [x] Native **single-file `.odc`** Save/Open holds several Parts, Workplanes
  and Sketches; existing Structure Browser covers names, color, visibility
  and selection. The present root is **not yet a full nested assembly model**.
- [ ] True **assemblies and subassemblies**, with hierarchical structure,
  nesting, per-component transforms, selection, grouping and visibility;
  keep a single document capable of holding the **entire assembly**.
- [ ] Optionally store or reference **individual Parts/subassemblies externally**,
  but **never require** external files for a normal multi-Part assembly.
- [ ] Reliable part duplication/instances and component reuse, assembly-level
  placement, reference frames and browser operations. Preserve independent
  geometry versus shared occurrence semantics explicitly.
- [ ] **Geometry-driven Move/Align Part and Position Assembly:** choose a
  **source Part or (sub)assembly** and a fixed **target reference**, then place
  the source using exact **vertex-to-vertex / point-to-point** translation,
  **axis-to-axis** (including cylindrical/Construction/Sketch axes), and
  **face-to-face or face-to-Workplane** coincident/parallel alignment. Offer
  explicit flip/orientation and signed offsets; do not guess any remaining
  degrees of freedom. Distinguish these **rigid object transforms** from
  topology-changing **Move Face / Align Face** operations.
- [ ] **Sketch-guided component positioning:** use Construction Points,
  line intersections, endpoints, centers of circles, and projected 3D points
  **on any transformed Workplane** as exact 3D placement destinations. Support
  e.g. **move a selected Part vertex onto a chosen Sketch point**, with correct
  local-to-world coordinate conversion and snapping, plus optional numerical
  displacement.
- [ ] **Assembly-level alignment:** transform a complete assembly or selected
  group rigidly while preserving all relative placements and geometry of its
  child Parts; do not move each child's vertices independently. Support
  mixed reference types (Part/Face/Edge/Vertex/Construction/Sketch) when
  geometrically defined, and allow position plus explicit orientation references.
- [ ] **Predictable placement interaction:** source vs target selection,
  visible geometry/snap feedback, blue **whole-Part/Assembly preview**,
  exact numeric mm/degrees, OK/Cancel and one Undo/Redo action, native Save/Open,
  lock/visibility checks and safe refusal on ambiguous references. Begin
  with **one-time direct positioning**; persistent assembly mates/constraints
  remain an optional later design decision, not a prerequisite.
  See [Issue #56: geometry-based Part and Assembly alignment](https://github.com/ProtonJeroen/OpenDesigner/issues/56).
- [ ] Structure Browser improvements: search/filter, active component/workplane
  context, typed multi-selection, drag/drop or reorder where safe, and coherent
  model/viewport state.
- [ ] Document evolution: schema migration/backward compatibility,
  scalable large-model handling, recoverable errors and safe undo/redo boundaries.
  No requirement to invent a SolidDesigner-compatible proprietary document format.
- [?] Full persistent **assembly constraints/mates** that maintain relationships
  after later changes, and external-link update semantics, need separate design.
  These are distinct from the planned **one-time geometric Align/Move** in #56
  and are not already implemented.

### 5. Import/export and downstream 3D workflows

- [ ] **STEP** import and export as the priority exact B-Rep interchange route
  for other CAD tools (including Fusion 360 exports); handle multiple
  Parts/assembly hierarchy and placements where supported.
  Imported STEP is geometry, **not a recovered Fusion 360 feature tree**.
- [ ] **IGES** import/export for applicable exact surface/curve exchange,
  with explicit limitations and healing.
- [ ] **STL** import/export for triangle-mesh/3D-print workflows; keep mesh
  exchange distinct from editable exact B-Reps.
- [ ] **GLB / glTF** import/export for Blender, visualization, real-time engines
  and VR-oriented workflows, preserving feasible component hierarchy,
  transforms and material appearance.
- [ ] **FBX** import/export for content pipelines where legally/technically
  supportable, with documented mesh/material/animation limitations.
- [ ] Clear unit/axis/scale conventions, color/material transfer, import
  diagnostics, file-type capability notices and round-trip regression fixtures.
- [?] Additional neutral/legacy formats and automated conversions can be
  prioritized once the core list is dependable; no unapproved format support
  is implied.

### 6. CoCreate v16-style user experience and input

- [x] Viewport orbit/pan/zoom, typed selection, context-sensitive direct
  actions, manipulators and shared operation preview/OK/Cancel/Undo/Redo.
- [ ] Continue converging on the **CoCreate Modeling 2008 v16.0 workflows**:
  explicit action and operand selection, stable reference picking, 3D CoPilot
  placement, edge/face/part selections, reliable numeric fields and predictable
  contextual menus. Keep OpenDesigner independent and modern.
- [ ] Expand edge-based references and multi-face selections where the
  geometry kernel can perform the corresponding exact modeling command.
- [ ] **3Dconnexion / SpaceNavigator / SpaceMouse** 6-DoF navigation,
  especially Linux `spacenavd`, with configurable axis mapping, sensitivity
  and natural unrestricted rotation; retain keyboard/mouse navigation.
- [ ] **Platform-independent touch mode in Settings**: opt-in touch controls on
  **Linux, Windows, macOS, iPadOS and Android**. Larger hit targets,
  touch-friendly manipulators/selection, gestures, on-screen numeric fields,
  plus mouse/keyboard when available. Desktop controls must remain usable.
- [ ] **Input choices** (desktop, touch or hybrid) and custom shortcuts/
  navigation mappings without forcing users into a touch-specific UI.
  Support precision stylus/Apple Pencil interaction on compatible tablets.
- [ ] Accessible, responsive GUI; HiDPI support, dock/panel layouts,
  translations/localized numbers and consistent error presentation.
- [?] Extensive skin/theme customization is lower-priority than reliable
  modeling and input; optional after core workflows.

### 7. Professional, intuitive UI/UX (required before multiplatform rollout)

**Motivation:** The current interface is still visibly a **prototype**, even
though the CAD geometry and commands are functional. Before broad Windows/macOS
distribution and the planned iPadOS/Android tablet products, make the desktop
UI **clean, visually consistent and intuitive for a new user**. Treat this as
a separate product milestone and release-readiness gate, **not merely styling
or an optional polish task**. Track details in
[Issue #55](https://github.com/ProtonJeroen/OpenDesigner/issues/55).

- [ ] **Audit the whole desktop UI:** viewport, Structure Browser, Sketch and
  Modeling toolbars, File/Construction menus, context menus, floating command
  panels, selection feedback, manipulators, dialogs and saved window layout.
  Document confusing interactions, duplicate controls and prototype elements.
- [ ] **User-tested information architecture:** a clear, predictable grouping
  for 2D Sketch, direct 3D Modeling, Construction Geometry, Assemblies,
  documents and View. Context-sensitive tools, recognizable icons, meaningful
  labels/tooltips and a manageable number of visible actions.
- [ ] **Unified command workflow:** make the currently active mode/selection,
  operand requirements, live preview and confirm/cancel actions obvious.
  Preserve both preselect→command and command→select workflows where appropriate;
  consistently use blue Preview, OK/Enter, Cancel/Esc and Undo/Redo. Show
  errors/unsupported operations understandably, without hidden state changes.
- [ ] **Coherent design system:** modern, professional Qt styling for colors,
  typography, iconography, spacing, focus/hover/active/disabled states and
  high-contrast visual hierarchy. Review proposed layouts and UI mockups
  **before** choosing a final theme or implementing a large visual rewrite.
- [ ] **Structure Browser and contextual properties:** present Parts,
  Workplanes, Sketches and future assembly hierarchy legibly, with clear
  visibility, lock, active context and selection states; keep browser and
  viewport synchronized.
- [ ] **Responsive, configurable desktop workspace:** sensible toolbar
  defaults, logically docked/rearrangeable panels, coherent context menus
  and an uncluttered viewport. Retain mouse/keyboard power-user workflows,
  configurable shortcuts and numeric precision.
- [ ] **Accessibility and readiness:** good keyboard focus, tooltips,
  legible contrast, DPI scaling (100%/200%), localized numeric entry,
  small-screen handling and stable panel positions. Prepare shared UI/state
  components for later optional touch/pen support **without forcing touch
  controls on desktop users**.
- [ ] **Iterative implementation and validation:** first UX inventory and
  design prototypes, then small reviewable UI PRs, then practical beginner and
  expert workflows on Fedora KDE. No unnecessary change to B-Rep operations,
  analytic model values, native `.odc` files or already tested CAD semantics.
- [ ] **Multiplatform UI gate:** *before broad release/distribution* for
  Windows, macOS, iPadOS and Android, validate a clean, consistent workflow
  with users and record accessibility/HiDPI regressions. Early technical
  portability experiments and CI can proceed **in parallel**; the gate is
  for supported product experience, **not** an arbitrary ban on builds.

**Acceptance examples:** someone new can create a Part, start and finish a
Sketch, select a Face/Part, use Move/Pull/Boolean, Undo/Redo and Save/Open
without discovering undocumented state; an experienced user keeps precise
keyboard/mouse and 3D-mouse access. Detailed tasks and review rules belong to
Issue #55, not implementation assumptions in this broad roadmap.

### 8. Platforms, builds and distribution

- [x] **Linux desktop** development and GitHub Actions Release build/tests;
  current development and native GUI verification primarily use Fedora KDE.
- [ ] Native **Windows** and **macOS** builds and regressions, with shared
  C++/Qt/OCCT modeling logic and appropriate platform-specific GUI packaging.
  **Broad public platform rollout requires the preceding UI/UX milestone (#55);
  early builds and CI portability tests may be developed in parallel.**
- [ ] **iPadOS** tablet application with full modeling-capable touch UI, not
  merely a passive file viewer; support stylus and common document exchange.
- [ ] **Android tablets** with the same cross-platform touch and CAD-core
  goals. Both tablet ports are planned **after desktop/core stabilization**;
  feasibility and packaging must be validated, not presumed.
- [ ] Reproducible CI for supported platforms; geometry correctness, headless
  tests and manual GUI/device testing on relevant native systems.
- [ ] Installable and updateable distributions, with packaging channels chosen
  per OS (e.g. Linux packages/Flatpak, Windows installer/portable, macOS
  application). Actual formats, certificates/store requirements and release
  obligations require later decisions.
- [?] Steam Deck/handheld optimized controls and other device classes may be
  explored as extensions, not launch commitments.

### 9. Quality, performance, openness and extensibility

- [ ] Keep B-Rep validity, material equivalence where appropriate,
  safe rollback, stable identity and no silent data loss as acceptance gates.
  Add unit, property, GUI/session, regression and real-model import tests.
- [ ] Performance for large Parts and assemblies: responsive selection,
  viewport tessellation/caching, memory management and later cancellable
  long-running kernel operations.
- [ ] Improve native save compatibility, error recovery, diagnostics,
  automated testing, packaging and user-facing documentation/tutorials.
- [ ] Maintain **GPL-3.0**, independent implementation and a clear
  contributor workflow, with GitHub branches, reviewed PRs and CI. Do not
  redistribute proprietary CoCreate source, manuals, icons or assets.
- [?] Scripting/API, plugins, automation and CAD copilot-assisted workflows
  are **future extensibility candidates**. Define secure APIs and meaningful
  use cases before promising an implementation.
- [ ] **Stable v1.0 goal:** essential direct edits, robust native documents,
  core profile-to-solid workflows, dependable import/export, **the validated
  intuitive UI/UX milestone (#55)**, supported desktop packages and tested
  interaction behavior. This is an aspiration without a
  fixed release date or claim that mobile builds precede v1.0.

### How to maintain this plan

- Update this consolidated section when a feature is approved, completed,
  descoped or postponed. Link the relevant GitHub issue/PR and keep checked
  boxes aligned with *merged and tested* capability, not merely a draft branch.
- Track large themes here; write exact geometry/UI acceptance cases in smaller
  issues and technical design documents. Avoid marking a partially supported
  geometry class as universally implemented.
- Preserve the detailed historical release notes below rather than replacing
  their validation results with an optimistic product-level checklist.

---

## Planned after Workplanes & Sketches — Move cylindrical faces (holes & bosses, Issue #51)

- [ ] **Move Cylindrical Face:** extend direct face editing beyond existing
  planar Move Face and cylinder Pull/Resize. Translate a selected **internal
  cylindrical face (hole)** or **external cylindrical face (boss / cylindrical
  protrusion on a planar face)** laterally, perpendicular to its own axis, by
  manipulator or exact numeric input; preserve diameter, direction and height.
- [ ] **Reposition holes:** move the complete hole axis/inner wall and openings
  without changing the exterior Part or other features. Rebuild the adjacent
  planar faces and any affected edges to obtain a valid B-Rep solid.
- [ ] **Reposition bosses:** move a complete **attached cylinder on a face**,
  including its cylindrical mantle, cap and footprint on the support face, so
  the support face closes at the old location and opens at the new one. The
  cylinder remains attached to the Part, not a detached separate solid.
- [ ] Start with exact and tractable examples: a complete circular **through-hole
  in a block** and a complete circular **boss on a planar block face**.
  Subsequently consider blind holes, multiple features and more complex analytic
  Boolean B-Reps. Refuse ambiguous trims, collisions or invalid transitions
  atomically and preserve the original solid.
- [ ] Reuse CoCreate SolidDesigner/Modeling 2008 v16.0 selection/workflow as a
  reference, OpenDesigner's live preview, exact numeric input, OK/Cancel,
  Undo/Redo, native Save/Open and Part-placement handling.
- [ ] Add tests for moved holes **and** moved attached bosses, exact geometry,
  connected/manifold solids, unaffected faces, transformed Parts, safe refusal
  and GUI/history regressions. Plan this **after** Workplane (6B) and Sketch
  improvements, not as part of the current v0.4.2 delivery.

See [planned cylindrical-face move: holes and bosses](https://github.com/ProtonJeroen/OpenDesigner/issues/51).

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
- [ ] 6B.2: SolidDesigner-style borders, front/back/active colors and grid.
  These remain outside 6B.1; Issue #48 is not fully closed by this delivery.

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
