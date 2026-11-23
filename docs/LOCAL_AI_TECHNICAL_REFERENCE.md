# OpenDesigner — Deep technical reference annex
**Snapshot:** 2026-11-07 · **Reference:** `develop` v0.4.9 · **Companion:** [Local AI Handover](LOCAL_AI_HANDOVER.md) · **Agent startup:** [Mistral Vibe](MISTRAL_VIBE_START.md)

> **Anchoring note (2026-11-23):** this annex is a **v0.4.9-anchored** API and contract reference.
> Its OCCT/Qt call-level notes remain useful, but its version, target and merge-state statements
> are historical. The current baseline is `develop` `fb370e5` (project version `0.5.6`, v0.5.7
> merged by PR #79 but unreleased); read `docs/SHARED_BREP_RECONSTRUCTION.md` for the v0.5.7
> shared B-Rep module contracts and `docs/LOCAL_AI_PROGRESS.md` for the current handover.
> Do not treat the v0.4.9 build graph below as current.

This annex expands the engineering handover with **source-anchored implementation contracts**. It is deliberately conservative: where an exact call graph, class signature, merge state or manual acceptance result has not been inspected, it is marked **VERIFY**. Source and tests always outrank this prose.

## 1. Verified build graph and entry points

The inspected `CMakeLists.txt` declares `project(OpenDesigner VERSION 0.4.9 LANGUAGES CXX)`, C++20, Qt6 Widgets >=6.4, OpenCASCADE and LibArchive. On Fedora, VTK CommonCore/IOImage may need discovery before OCCT. The main executable compiles `main.cpp`, `MainWindow`, `CadViewport`, `BooleanOperations`, `BRepReconstruction`, `MoveFaceGeometry`, `MoveFace`, `FaceOperations`, `FaceTransformGeometry`, `SelectionContext` and `NativeDocument`. Treat the grouping as an observed build dependency, not proof of architectural purity.

| Target | Principal files | Responsibility / boundary |
|---|---|---|
| OpenDesigner | MainWindow, CadViewport, SelectionContext, NativeDocument | UI, interactive session, document/view integration |
| OpenDesignerCommandPanels | CommandPanelPositioner | Qt window/viewport panel positioning |
| OpenDesignerConstruction | SketchGeometry, SketchSnapping, SketchProjection, SketchProfileValidation/Builder/Extrusion/Revolution | Construction and profile geometry; inspect for Qt-free portions |
| OpenDesignerCylindricalMoveGeometry | CylindricalMoveGeometry, BRepReconstruction, MoveFaceGeometry | Headless certified cylindrical reconstruction; Qt Core only |
| OpenDesignerDeleteFaceGeometry | DeleteFaceGeometry, PlanarFaceDeletion | Geometry-only face removal, conservative certificates |
| OpenDesignerDebug | DebugModel | Diagnostic archive model |
| OpenDesignerDebugModel | DebugModelInspector | Isolated inspection CLI |

**Architecture review task:** generate a reproducible `rg`-based symbol inventory and include only symbols actually found in the checked-out commit. Inspect the interfaces of `CadViewport`, `MainWindow`, `NativeDocument`, `SelectionContext`, `SketchProfileExtrusion`, `SketchProfileRevolution`, `CylindricalMoveGeometry` and `DeleteFaceGeometry`. Record ownership, mutability, exception boundaries and test coverage.

## 2. Fundamental geometric invariants

The project's explicit cross-cutting requirement in `docs/ROADMAP.md` is **geometry-first, history-independent B-Rep editing**. Primitive labels and historical feature trees must not determine editability. Infer analytic supports, adjacency and current material occupancy from the actual current shape. Apply transforms exactly once. For an accepted candidate, certify geometry validity, closed/manifold topology, expected connectivity, tolerance bounds and the intended material change. A BRepCheck-valid solid is necessary but not sufficient to prove the user-intended edit.

**Material equivalence:** use geometric Boolean Common/volume/occupancy certificates as appropriate, not merely face count or mesh appearance. Same-domain normalization may remove redundant seams only if it preserves material. Refuse ambiguous or unsupported topology rather than accepting arbitrary OCCT output.

**Identity:** a face edit changes the B-Rep of the existing Part. Preserve Part UUID, name, color, visibility, placement and browser slot. Undo/Redo must restore complete snapshots, not regenerate from obsolete primitive parameters. Local face owners can be invalidated after topology replacement; whole-Part identity persists.

**Source safety:** never mutate input geometry before a candidate is accepted; preserve independent Workplanes and Sketch entities during Extrude/Revolve. Where a preview temporarily uses live geometry (historical Pull path), snapshot capture must substitute the last confirmed geometry when saving.

## 3. Commands, previews and history

Read `docs/COMMAND_SESSIONS.md` before editing the UI. Explicit command activation is required; passive click selects Face, Alt+click selects Part, Ctrl extends/toggles. An operation has a parameter/picking phase, a blue translucent preview, OK/Enter confirmation and Cancel/Esc abort. No leftover AIS geometry, gizmos, stale highlights or floating panels may survive termination. A confirmed operation is one chronological Undo step; cancellation creates none and must retain Redo.

**State transition matrix:**

| Event | Canonical model | Temporary preview | History |
|---|---|---|---|
| Start command | unchanged | optional | unchanged |
| Valid parameter change | unchanged | replace candidate | unchanged |
| Invalid parameter | unchanged | clear candidate | unchanged |
| Cancel/Escape | unchanged | remove | unchanged |
| OK/Enter | atomic accepted candidate | remove | append one entry |
| Save during preview | serialize confirmed state | remain live | unchanged |
| New/Open | replace/reset document | remove | clear |
| Undo/Redo | restore cached state | terminate | move history cursor |

Exception: Escape can first leave a dedicated target picker in some Extrude/Revolve flows. Read the command-specific documentation; do not impose a generic Escape behavior that breaks it.

## 4. Extrude: detailed implementation contract

**Source:** `docs/EXTRUDE_PROFILE.md` (v0.4.8). Activate visible/unlocked Workplane with Edit Sketch, choose **3D Modeling → Extrude**. All Profile entities on that Workplane participate, including hidden/locked Profiles; Construction entities never close profiles. The source remains independent.

**Modes:** New Solid creates one GeneralBRep Part (possibly compound of disconnected material regions); Add Material targets an eligible Active Part or explicitly picked visible Part, preserving its identity; Remove Material offers Blind or Through All and Reverse direction. Add requires connected target and connected result. Remove may leave multiple surviving solids but refuses no-op and complete target removal.

**Kernel:** `SketchProfileBuilder` produces certified planar faces; `SketchProfileExtrusion` uses `BRepPrimAPI_MakePrism`. Validate positive finite volume, orientation, solid count and expected area × depth. Distance is signed for New/Add, positive Blind for Remove. The document records a maximum of 1e9 mm and a minimum greater than twice certified vertex tolerance, plus floating-point/OCCT resolution checks. These are feature-specific limits; never generalize blindly.

**Boolean path:** shared `BooleanGeometry` Fuse/Cut on deep copies; apply target world placement once and inverse transform once for local storage. Do not create a transient tool Part in the committed document. `BooleanChange` handles new Part history; `FaceChange` handles in-place target replacement.

**Unsupported:** ellipse/elliptical-arc Profile extrusion, automatic healing, multi-target material operations, tapered/symmetric/path extrusion, multi-solid Add targets. A disabled action or refusal is not a bug without a changed scope decision.

**Evidence:** `docs/EXTRUDE_PROFILE.md` reports user-confirmed Fedora KDE Unified Extrude 7/7 on 2026-11-01. It also reports 35/35 combined Extrude/Revolve manual checks. These are user-reported, not machine-proven by this handover.

## 5. Revolve: detailed implementation contract

**Source:** `docs/REVOLVE.md` (v0.4.8). Edit Sketch → **3D Modeling → Revolve**. Choose Profile regions; holes belong to their enclosing region. Select a straight Sketch line/Construction Axis/Part edge as the rotation axis, with explicit picking and transforms. Axis must be in the profile plane; regions on one side, without crossing/pinching/self-intersection. No curve axis.

Angle is signed, `1 <= abs(angle) <= 360` degrees, fractional values supported; Reverse direction negates the signed angle. Blue preview is rebuilt from original source snapshots, not cumulatively from previous preview geometry. Modes New Solid/Add/Remove share the Extrude material application kernel, preserving target identity and source sketches. No persistent feature dependency is stored.

**Test references:** `sketch-profile-revolution`, `profile-revolution-sessions`, `profile-revolution-sessions-hidpi`; `docs/REVOLVE.md` records targeted automated checks and 13/13 + 15/15 user-reported manual acceptance for New Solid and Add/Remove.

## 6. Cylindrical Move Face: certified current-B-Rep operations

**Source:** `docs/MOVE_FACE.md` (v0.4.9). One selected complete circular through-hole, supported open notch or attached boss can move laterally. Explicit Part X/Y/Z reference projected into lateral plane, direction angle and signed millimetres; no automatic lateral direction. Boss **Extend** keeps a full circular cylinder and valid overhang; **Trim** clips against original support sides. Through-holes/notches have no policy selector.

Supported cases include selected independent parallel features in certified rectangular stock, repeated trimmed/overhanging boss moves across one planar boundary, open-notch moves and restoration, and oblique planar rims with elliptical intersection curves. A separated complete through-hole must not erase unrelated exterior boss material. Recognize features from current B-Rep, including split cylindrical walls, not a remembered feature state.

**Refusal cases:** blind features, blends, interacting/touching features, unsupported corner/multiple-side/curved trims, disconnected boss material and ambiguous reconstruction. Work remains synchronous. Read `docs/REPEATED_BOUNDARY_BOSS_MOVE.md`, `docs/OPEN_CYLINDRICAL_NOTCH_MOVE.md`, `docs/ROTATED_RIM_CYLINDER.md`, `docs/GEOMETRY_DRIVEN_MOVE_FACE.md`.

**Regression families:** `cylindrical-move-geometry`, `cylindrical-repeated-boss-imported`, `cylindrical-repeated-boss-geometry`, `cylindrical-open-notch-geometry`, `cylindrical-pull-gap-geometry`, `cylindrical-radius-geometry`, `cylindrical-independent-features`, `cylindrical-boundary-follow-up-geometry`, `local-neighborhood-move-geometry`, `cylindrical-move-face-sessions`. Check exact current CTest list.

## 7. Delete Face: material-oracle acceptance

**Source:** `docs/DELETE_FACE.md`. Multi-face/multi-Part deletion is atomic: all selected regions succeed or none are installed. `DeleteFaceGeometry` deep-copies the canonical B-Rep and uses `BRepAlgoAPI_Defeaturing` when eligible. It requires every selected face to disappear and rejects OCCT warnings/errors unless an independently certified alternative path succeeds.

Certified families include: (1) convex planar halfspace reconstruction; (2) planar-rim feature filling/removal with closed material oracle; (3) bounded prismatic/planar-section reconstruction, including selected L-shaped transitions; (4) current-B-Rep boundary cylindrical feature removal via `CylindricalMoveGeometry::removeBoundaryFeature`. Final normalization must preserve material and unaffected supports. No arbitrary caps or unrestricted freeform healing.

Tests must check exact material/volume, unselected face support survival, selected patch disappearance, manifold closure, preview, Cancel, one-step history and native persistence. See `docs/SLOPED_OPENING_DELETE_FACE.md` for certified inclined rim extension.

## 8. Native documents: persistence and migration

**Source:** `docs/NATIVE_DOCUMENTS.md`. Single `.odc` BinXCAF/XCAF file, exact local B-Rep and metadata. Root assembly contains Part components; nested assembly authoring remains future work. Camera state is persisted but camera navigation alone does not dirty document. `QSaveFile` atomic replacement follows BinXCAF staging; failed Save/Open must leave current document intact. Open may stage read-only source in system temp.

**Schema:** reader supports v1–v5. Writes v3 for baseline 2D geometry, v4 when local Construction Points require it, v5 when Ellipse/EllipticalArc is present. Ellipse stores center, major/minor radii, principal axis; EllipticalArc adds start parameter and signed sweep (eccentric parameter, not polar angle). Optional Active Part UUID metadata is additive. Future or malformed versions must be refused rather than silently down-converted.

**Lifecycle:** Save/Save As preserve selection/history and exclude unconfirmed preview; New/Open reset selection, tools and session history. Undo/Redo history is not serialized. Verify tests `native-document-roundtrip`, `face-history-persistence` and UI document sessions.

## 9. Sketch geometry and projection

Read `docs/SKETCH_EDITOR.md`, `docs/SKETCH_PROJECTION.md`, `docs/GUI_TEST_PLAN.md`. Workplanes and construction axes/points have stable identity and local coordinates. Projection of 3D vertices/straight edges and certified circular/elliptical edges is analytic; no tessellation-based approximation. Apply Part placement exactly once, then orthogonally project into Workplane coordinates. Reject collapsed edges and unsupported oblique curves explicitly. Projected sources are independent; later source movement does not create persistent parametric dependencies.

**Precision:** the projection document records default linear tolerance 1e-7 mm and a permitted minimum 1e-9 mm. Avoid catastrophic cancellation at very large placements; source implementation uses relative-to-Workplane calculations.

**Manual GUI plan:** `docs/GUI_TEST_PLAN.md` contains A–H checks for vertex/edge/mixed projection, circular arcs, holes/duplicates, cancellation, viewport panel, HiDPI and regression interactions. The inspected document labels those checks **not yet executed**. A later version may supersede it; verify current acceptance before marking passed.

## 10. Command panels and display systems

The native panel should anchor to viewport top-right with approximately 12 logical px margin, support dragging and scrolling when viewport is small, and remain usable at normal/200% scale. Native WM frame coordinates, Qt logical coordinates and multi-screen DPR differ under Fedora KDE Wayland/XWayland. `docs/V049_INTEGRATION_REVIEW.md` states real cross-screen HiDPI acceptance remains pending, despite synthetic fixture corrections. Do not treat a test adjustment as a production fix. GUI focus tests may need `RUN_SERIAL` because native focus is a shared desktop resource.

## 11. Test taxonomy and evidence discipline

| Layer | Examples | What it proves | What it does not prove |
|---|---|---|---|
| Headless geometry | sketch-profile-extrusion, sketch-profile-revolution, cylindrical-move-geometry | Certified geometry for covered fixtures | Native GUI behavior |
| Viewport/session | profile-extrusion-sessions, move-face, command-sessions | Preview, selection, history and ownership | All real window managers |
| Persistence | native-document-roundtrip, face-history-persistence | Confirmed B-Rep/metadata roundtrip | History persisted across Open |
| GUI positioning | command-panel-positioning, -hidpi, cross-screen | Specific test backend/monitor fixture | Every physical multi-monitor layout |
| Manual KDE | user-recorded checklists | Observed real desktop workflow | Other OS, all arbitrary B-Reps |
| CI Release | GitHub Actions Linux + Xvfb | Reproducible configured CI scope | Windows/macOS release readiness |

The v0.4.9 integration review records 86 cases passing **across full and targeted reruns**, after corrections to test expectations/fixture; it does not claim one clean full suite. Historical PR #68 reportedly had 88/88 CI green; its merge status is **resolved** — merged 2026-11-07
as merge commit `60d4bccc16efef762f01f099a1a850151e8cb020`. The README reports 47/47 manual checks,
explicitly user-reported.

**Required test record:** git SHA, build type, OS/Qt/OCCT versions, exact command, number passed/failed/skipped, GUI backend, test fixture provenance, and whether results were obtained in one run or combined across reruns.

## 12. Historical evolution and design rationale

| Era | Core decision | Why it matters today |
|---|---|---|
| v0.1 | Qt6 + OCCT viewport and browser | Native CAD application foundation |
| v0.20 | Primitive Pull, exact dimensions, sphere/cone | User-facing direct interaction baseline |
| v0.3 | Boolean target/tools and stable Part slots | Consistent ownership/history |
| v0.3.2 | Explicit sessions, blue preview and atomic confirmation | Cross-tool interaction contract |
| v0.3.3–0.3.6 | General face edits and certified B-Rep reconstruction | Geometry-first scope and safety |
| v0.3.7 | Exact native `.odc` storage | No dependency on feature-history replay |
| v0.4.0+ | Construction geometry and 2D sketch system | Independent geometry references |
| v0.4.8 | Unified Extrude/Revolve modes | Shared Boolean/target identity semantics |
| v0.4.9 | Cylindrical Move Face and persistent regression coverage | Expanded certified analytic topology |

Do not assume historical milestone labels are linear branch ancestry. Use `git log --graph`, merge commits and PR descriptions to reconstruct exact lineage.

## 13. Roadmap and feature readiness criteria

**Assemblies:** single-file hierarchy, optional externally referenced components, structure browser tree, per-component placement, stable identity and cycle prevention. Geometry alignment by vertex, axis, face and point-to-point sketch references must define explicit target/reference roles, coordinate frames, preview, cancellation and undo. Distinguish rigid component transforms from B-Rep face deformation.

**Materials:** density and visual material metadata with units, inheritance/override policy, native serialization and export mapping. No silent change to mass properties or identity.

**File interoperability:** STEP and other CAD formats, GLB/glTF and FBX with unit/axis conversion, exact-versus-mesh fidelity disclosure, hierarchy and material policy. Avoid claiming FBX can preserve exact OCCT B-Rep. Network file access must retain atomic save and safe failure.

**Cross-platform/touch:** Windows/macOS build/test qualification, tablet UI and a platform-independent Settings toggle for touch interaction. Do not assume Qt Widgets desktop code can be deployed unchanged on mobile.

**Unscheduled:** threads and broad freeform surface support require separate design approval. No invented release commitments.

## 14. Local AI operating model

Target hardware: Lenovo RTX 3070 8 GB VRAM, 48 GB RAM, Fedora KDE. Ollama + Mistral Vibe + Qwen3-Coder is the planned offline stack; verify installed model name/quantization/context locally. Keep context bounded: read this annex only when relevant; retrieve a few source files and tests for the current task, summarize findings into a session checkpoint. Long overnight execution is acceptable only within approved scope. No cloud APIs; MCP deferred.

**Daily startup:** check status/branch/HEAD; inspect untracked private fixtures; read the task's exact acceptance tests; run baseline targeted tests; implement small commits; record results. **Stop conditions:** destructive Git operation, uncertain material semantics, failure affecting unrelated Parts, test nondeterminism, or need for proprietary/private data.

## 15. Source-traceable checklist for future deep audit

- [ ] Capture exact `develop` and documentation branch SHAs.
- [ ] Generate a complete `src/` and `tests/` inventory from the same SHA.
- [ ] Extract public C++ class/function declarations with file:line references.
- [ ] Trace `CadViewport` command ownership and all `FaceChange`/`BooleanChange` entry points.
- [ ] Map each geometry certificate to focused tests and expected failure modes.
- [ ] Verify native v1–v5 compatibility tests and sample document fixtures.
- [ ] Inspect workflow YAML and confirm Linux/Windows/macOS CI coverage.
- [x] Reconcile PR #68 and all open PRs with `develop`. (PR #68 merged as `60d4bccc`; PRs #70-#79
  are merged through `fb370e5`; re-check open PRs against the current HEAD before acting.)
- [ ] Execute the full suite on the target Fedora host and record exact results.
- [ ] Perform pending physical HiDPI/multi-monitor and projection GUI acceptance.
- [ ] Obtain explicit approval before scheduling the next implementation milestone.

## 16. Evidence and limitations

**Directly inspected repository docs:** `README.md`, `CMakeLists.txt`, `docs/ROADMAP.md`, `docs/EXTRUDE_PROFILE.md`, `docs/REVOLVE.md`, `docs/NATIVE_DOCUMENTS.md`, `docs/MOVE_FACE.md`, `docs/DELETE_FACE.md`, `docs/SKETCH_PROJECTION.md`, `docs/V049_INTEGRATION_REVIEW.md`, `docs/FACE_HISTORY_PERSISTENCE.md`, `docs/GUI_TEST_PLAN.md`. The first handover was based on a smaller subset; this annex extends it. Exact class signatures, exhaustive code graph, live CI and current local uncommitted state have **not** been independently audited. The documents are a technically grounded handover and an explicit audit backlog, not a claim of complete reverse engineering.
