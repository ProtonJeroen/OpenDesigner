# OpenDesigner — Technical development handover
**Snapshot:** 2026-11-23 | **Baseline:** `develop` `fb370e5` (v0.5.6 released, v0.5.7 merged by PR #79 but unreleased) | **Audience:** Mistral Vibe / Qwen3-Coder local agent

> This is a navigation and engineering handover, **not** a replacement for source inspection. The code and tests on the checked-out commit are authoritative. Historical conversations and older milestone names are not proof of current behavior. Never silently mark planned features implemented.

## 1. Mission, license, reference
OpenDesigner is an open-source cross-platform **history-independent, direct-modeling CAD** application inspired by CoCreate SolidDesigner / OneSpace Modeling 15.50 and version 16.0 (2008). It aims to reproduce useful interaction workflows with modern improvements, not to clone proprietary code or assets. License: **GPL-3.0-only** (check `LICENSE`). Linux is the currently validated desktop target; Windows/macOS are targets, not certified releases. iPadOS and Android tablet support and a platform-neutral touch-mode setting are roadmap aspirations.

**Non-negotiable modeling invariant:** edit the *current B-Rep*, not primitive creation labels, feature histories or provenance. Recognize analytic geometry from faces/edges/adjacency. A Boolean, imported or transformed shape should be evaluated by geometry and a certified material/validity result. Refuse unsupported operations precisely and non-destructively; never silently approximate geometry.

## 2. Authoritative reading order
1. `AGENTS.md` and `CLAUDE.md` — standing rules, architecture principles, tolerance policy,
   build/test limits, reporting and mandatory timing rules. Keep the two in sync.
2. `README.md` — present product scope and limitations.
3. `docs/ROADMAP.md` — version status, the merged-state section for PR #79, and the open
   v0.5.7/v0.5.8 acceptance gates.
4. `docs/SHARED_BREP_RECONSTRUCTION.md` — the v0.5.7 shared B-Rep architecture: module
   ownership, certification pipeline, provisional pipeline, F2.9G.5A seam rule.
5. `CMakeLists.txt` — exact build targets, dependencies and test registration.
6. `docs/LOCAL_AI_PROGRESS.md` — current per-task handover snapshot and history.
7. `docs/LOCAL_AI_TASK_TIMES.csv` — append-only measured task timings.
8. `docs/FAST_DIRECT_MODELING_CERTIFICATION.md` — the certification ledger, including F2.9G.5A
   and the PR #79 test-harness repairs.
9. `docs/LOCAL_AI_ENGINEERING_OPERATIONS.md`, `docs/MISTRAL_VIBE_START.md`,
   `docs/LOCAL_AI_TECHNICAL_REFERENCE.md` (anchored at v0.4.9 — see its header note).
10. `docs/COMMAND_SESSIONS.md`, `docs/SELECTION_CONTEXT.md`, `docs/NATIVE_DOCUMENTS.md`,
   `docs/BREP_RECONSTRUCTION.md`, `docs/MOVE_FACE.md`, `docs/DELETE_FACE.md`,
   `docs/EXTRUDE_PROFILE.md`, `docs/REVOLVE.md`, `docs/SKETCH_EDITOR.md`,
   `docs/SKETCH_PROJECTION.md`, `docs/FACE_HISTORY_PERSISTENCE.md`,
   `docs/CROSS_SCREEN_HIDPI.md`, `docs/GUI_TEST_PLAN.md`,
   `docs/V049_INTEGRATION_REVIEW.md` and `docs/V056_KDE_ACCEPTANCE.md` — feature-specific
   detail; read before converting a roadmap bullet into an implementation claim.
11. Current `src/`, `tests/`, `.github/workflows/`, Git history and PR status.

Some previously discussed root-level files (`ARCHITECTURE.md`, `DECISIONS.md`, `CODEX_HANDOFF.md`)
were not found at the repository root of `develop`; only `AGENTS.md`, `CLAUDE.md` and `README.md`
exist there. Do not assume they exist; discover files before citing.

## 3. Build architecture (observed in CMake)
C++20, CMake >=3.21, Qt6 Widgets >=6.4, Open CASCADE, LibArchive. Fedora OCCT builds may require VTK CommonCore/IOImage targets to be found before OpenCASCADE. Main executable `OpenDesigner` includes `src/main.cpp`, `MainWindow`, `CadViewport`, `BooleanOperations`, `BRepReconstruction`, `MoveFaceGeometry`, `MoveFace`, `FaceOperations`, `FaceTransformGeometry`, `SelectionContext`, `NativeDocument`.

Dedicated libraries/targets:
- `OpenDesignerCommandPanels`: `CommandPanelPositioner`.
- `OpenDesignerBRepTopology` (CMakeLists.txt:33): `BRepTopology` — topology indexing,
  adjacency and analytic-support lookup shared by every direct-modeling command.
- `OpenDesignerBRepCertification` (CMakeLists.txt:36): `BRepCertification`, `BRepMeasurement`
  and `BRepInvariantSupport` — validity, volume/material certificates and the
  `BRepReconstruction::SupportDescriptor` / `TopologyIndex` implementations.
- `OpenDesignerConstruction`: `ReferenceGeometry`, `SketchDisplayBounds`,
  `ConstructionGeometry`, `SketchGeometry`, `SketchSpline`, `SketchSnapping`,
  `SketchOverdraw`, `SketchConstruction`, `SketchProjection` and the profile
  validation/build/extrude/revolve interfaces declared further down the file.
- `OpenDesignerCylindricalMoveGeometry`: headless geometry with Qt Core, no Qt Widgets/visualization
  dependency; also compiles `BRepReconstruction.cpp` and `BRepInvariantExtrusion.cpp`
  (CMakeLists.txt:77-78).
- `OpenDesignerDeleteFaceGeometry`: Delete Face and planar deletion geometry.
- `OpenDesignerEdgeBlends` (CMakeLists.txt:483): `FilletGeometry`, `ChamferGeometry`,
  `EdgeBlendFeatures`, `FaceTransformGeometry`.
- `OpenDesignerDebug`, `OpenDesignerDebugModel`: diagnostic model capture and inspection.

Header-only v0.5.7 additions with no own CMake target: `BRepTiming.h`,
`BRepMeasurementPrivate.h`, and the provisional pipeline
(`BRepProvisional.h`, `BRepProvisionalNegotiation.h`, `BRepProvisionalProviders.h`,
`ProvisionalBoxPreview.h`, `ProvisionalPlanarClearance.h`, `BRepSweepClearance.h`,
`BRepSweepConstruction.h`).

These are **build groupings**, not a claim of fully isolated architectural layers. Inspect headers/implementations to trace ownership and mutation boundaries. The large `CadViewport` and `MainWindow` translation units deserve special care when changing command lifecycle.

## 4. Functional inventory: implemented within certified scope
**3D and interaction:** Box/Cylinder/Sphere/Cone, direct Pull with exact dimensions, Move/Rotate Part, duplication, volume-centroid pivot, passive Face/Alt-Part selection with Ctrl modifiers, Structure Browser, visibility, material/color, orbit/pan/zoom, explicit command activation, manipulators, blue candidate preview, orange handles, one-step history, Boolean Unite/Subtract/Intersect, Keep Tools policy.

**Direct face edits:** Move Face on supported planar and cylindrical features, Rotate Face, Align Face (Coplanar/Parallel), Delete Face with conservative analytic reconstruction, material-preserving same-domain normalization and bounded planar-cell fallback. Cylindrical holes/bosses support lateral movement and explicit Trim/Extend policies in certified cases. Not arbitrary freeform reconstruction.

**2D / construction:** Workplanes, Axes, Points, sketch lines/circles/arcs, ellipse/elliptical arc foundation, analytic snapping, Overdraw, projection of eligible 3D vertices/edges/faces, profile validation and wire/face building, Extrude and Revolve with New Solid/Add Material/Remove Material where supported. A CMake source entry alone is not proof that Sweep is user-facing.

**Persistence:** native `.odc` BinXCAF/XCAF exact B-Rep, Part metadata, placement and camera; documented schema v1–v5 reader compatibility and version-dependent writing. Save while a preview is active must serialize *confirmed* geometry. Undo/Redo is session-local and is not persisted. Root assembly storage is not equivalent to completed nested-assembly authoring.

**Diagnostics:** ZIP debug-model export and isolated inspector; separate from `.odc` documents.

## 5. Invariants for every command
- A candidate must pass OCCT validity, expected solid/connectivity checks, topology/material occupancy certificates and operation-specific tolerances.
- The document and unrelated Parts must remain unchanged on invalid input, refusal, Cancel or failed preview.
- Explicit OK/Enter commits one chronological history transaction; Cancel/Esc commits none; Undo/Redo restores exact cached states.
- Selection, active Part, names, IDs, placements, color, visibility and browser rows must be preserved according to command semantics.
- Preview presentation must not be mistaken for canonical document geometry; clear all temporary AIS and command UI on termination.
- New/Open reset sessions/history/selection; Save/Save As preserve current session and write confirmed state.
- A topology-changing operation must not rely on primitive metadata or stale subshape IDs.
- Never broaden certified geometry scope by removing safety checks simply to pass a test.

## 6. Geometry and regression lessons
**OCCT and material equivalence:** after Boolean/face edits, remove redundant seams only when material equivalence and connectivity are certified. Shared B-Rep reconstruction has bounded planar-cell logic; unrestricted blends/freeform/complex trims remain open. Geometry and occupancy must be tested independently of triangulation.

**OCCT version behaviour differs and must be gated, not assumed.** Stacked-cylinder seam merging
is compiled only when `OCC_VERSION_HEX >= 0x070900` (`stackedCylinderMergeSupported`,
src/BRepReconstruction.cpp:244): CI's OCCT 7.6.3 keeps the previous guarded normalization
because `ShapeUpgrade_UnifySameDomain::IntUnifyFaces` loops there, while local OCCT 7.9.x merges.
Carrier identity for a shared seam is proven with 64-ulp axis/location/radius comparison, the
material side through `cylinderMaterialSide` rather than raw `TopoDS_Face` orientation, and
`stackedCylinderSeams` (src/BRepReconstruction.cpp:438). A locally green seam-merge result is
therefore not evidence for the CI toolchain, and vice versa.

**Cylindrical edits:** preserve trim edges and analytic support; use certified split-result fallback when unification is unsafe. Keep strict tolerance policy, including the historically used `1e-6` bound in affected offset cases; verify current code before relying on a numeric threshold. Test boundary holes, open notches, rotated rims, boss Trim/Extend and repeated edits.

**Qt/KDE:** native command-panel coordinates can differ from QWidget logical coordinates, especially under Wayland/XWayland and multi-monitor HiDPI. Distinguish test-fixture placement fixes from production cross-screen correctness. `docs/V049_INTEGRATION_REVIEW.md` explicitly leaves real cross-screen acceptance pending.

**Private fixtures:** locally supplied `.odc` crash/geometry cases may be untracked and confidential. Never commit, delete, alter or publish them. Use generated fixtures or explicit opt-in paths for regression replay.

## 7. Build and verification
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DOPENDESIGNER_BUILD_TESTS=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
```
GUI integration tests require a working X11/OpenGL desktop or a suitable Xvfb/software-GL environment. Do not assume `QT_QPA_PLATFORM=offscreen` works for Open CASCADE viewer tests. Use targeted CTest regex first, then full suite; report command, environment, pass/fail counts and skipped cases. CI green is not a substitute for Fedora KDE manual UI acceptance. `README.md` notes Windows/macOS not CI-validated. Review the current workflows before claiming cross-platform support.

**Baseline evidence:** historical v0.4.9 figures in `docs/V049_INTEGRATION_REVIEW.md` describe an
initial full suite with failures, corrected targeted reruns, and 86 cases with passing results
*across full and targeted runs*, not one clean full-suite pass. Its 47/47 manual checks are
*user-reported*. PR #68 is **verified merged** (2026-11-07, merge commit
`60d4bccc16efef762f01f099a1a850151e8cb020`); its 88/88 figure belongs to that PR's own run.
Never combine these figures as if they were the same run.

Current baseline evidence for PR #79, read from GitHub Actions (not re-executed locally in the
documentation session that recorded it): PR head `63ff9a6` run `45071533024` was green with
`100% tests passed, 0 tests failed out of 142`; `develop` `fb370e5` run `45172009344` failed
with `141/142` because `rotation-pivot` hit its 60 s `TIMEOUT` at 60.03 s. The two trees are
byte-identical (`git diff --name-only fb370e5 63ff9a6` is empty), so the failure is a timing
flake, not a merge regression. `CMakeLists.txt` registers 144 tests; CI runs 142 because
`command-panel-cross-screen` (Qt >= 6.11) and `experimental-whole-section-lines`
(Boost.Multiprecision) are gated out.

## 8. Historical development timeline (high level)
- v0.1: Qt/OCCT viewer, browser, box and Linux CI.
- v0.20: primitives, Pull, sphere and cone workflows.
- v0.3–v0.3.2: Boolean operations, pivot, explicit command sessions.
- v0.3.3–v0.3.6: face move/rotate/align/delete, B-Rep reconstruction, diagnostic export and topology-safe workflows.
- v0.3.7: native `.odc` storage.
- v0.4.0+: construction objects, sketch editor, projection, profile validation/build, Extrude/Revolve, elliptical geometry and expanded regression coverage.
- v0.4.9: cylindrical Move Face, geometry and crash fixes, persistence/history validation,
  cross-screen HiDPI qualifications.
- v0.5.0-v0.5.2 (PRs #70-#72): feature-aware Fillet and Chamfer direct editing, circular Fillets
  with intelligent selection and cap-editing recovery, conical Fillet and Chamfer.
- v0.5.3-v0.5.5 (PRs #74-#76): Workplane and reference foundation; Show/Hide Workplanes and
  Move Face by Points; Advanced Sketch Curves.
- v0.5.6 (PR #78): interactive Trim, Extend and exact curve Overdraw. **v0.5.6 is the last
  released version** (`project(OpenDesigner VERSION 0.5.6)`).
- v0.5.7 (PR #79, merged 2026-11-23 as `fb370e5`, unreleased): shared B-Rep reconstruction
  modules, F2.9G.5A stacked-cylinder seam unification, provisional pipeline, test-harness
  repairs.
- v0.5.7 (PR #79, merged 2026-11-23 as `fb370e5`, unreleased): shared B-Rep reconstruction
  modules, F2.9G.5A stacked-cylinder seam unification, provisional pipeline, test-harness
  repairs.

Read milestone-specific documents before converting this timeline into detailed release claims. Old milestone labels in `docs/ROADMAP.md` are historical, not semantic versioning guarantees.

## 9. Roadmap: separate from implemented features
Priority areas to validate and plan: persistent subshape identity; broader history-independent curved/complex B-Rep edits; cancellable expensive OCCT operations; reliable cross-monitor UI; assemblies with hierarchical single-file structure and optional external components; geometric assembly alignment by vertex/axis/face/edge and point-to-point sketch references; materials with visual properties and density; standard parts; network file access; STEP and other CAD import/export plus GLB/glTF and FBX; Windows/macOS validation and packaging; platform-independent touch-mode option and tablet UIs. Thread modeling is **not an approved scheduled milestone**. Check current roadmap before assigning phase/version or claiming work done.

## 10. Local AI environment and resource strategy
Intended offline workflow: Lenovo Legion RTX 3070 8 GB VRAM, 48 GB RAM, Fedora KDE, Ollama + Mistral Vibe with Qwen3-Coder, VS Code; Dell laptop may be a development client. No cloud Codex/Claude/API dependency. Overnight work is acceptable; limited VRAM/context means read **relevant files only**, use small task scopes and persist compact checkpoints. MCP integration is deferred until local workflow stabilizes. Exact model tag/quantization, context size and inference performance require local verification; do not invent settings.

## 11. Autonomous development contract
1. Read `MISTRAL_VIBE_START.md`, then `git status --short --branch`, `git log -1`, `README.md`, relevant roadmap and feature docs.
2. Check for untracked/private files and active work; never reset/clean/delete them.
3. Identify a single task and acceptance criteria; list affected files and failure risks.
4. Build baseline or run targeted tests **before** changes when feasible.
5. Implement a small, reviewable change, keeping geometric certification and command lifecycle.
6. Run focused tests, full suite when appropriate, and record manual GUI steps not yet executed.
7. Produce a session report: commit SHA, changed files, reasoning, test commands/results, limitations and next action.
8. Work on a dedicated feature branch; do not push protected branches, merge PRs, rewrite history, or publish private fixtures without explicit approval.
9. On uncertain topology/material semantics, stop and document choices rather than guess.
10. At handoff, leave a clean or precisely described working tree, with no unsupported success claims.

## 12. Immediate next investigation
`docs/LOCAL_AI_PROGRESS.md` is the current snapshot; read its last section first. As of 2026-11-28
`origin/develop` HEAD is `020a6e4` (PR #82, legacy `.odc` compatibility for F2.9G.5A); the
shared-schema-predicate refactor is merged there. The import-side follow-up is on `refactor/native-document-import-schema-gate` (`6bd589f` + `8daebc5` +
`3227bac`) as **PR #83**, CI green at 144/144 on OCCT 7.6.3, mergeable but not merged — merging PRs is
not authorized by the standing rules.
Open items in priority order: (1) the v0.5.7 acceptance and release gates still open in
`docs/ROADMAP.md`, including the Fedora KDE manual rounds for the shared pipeline; (2) the v0.5.8
Advanced Hole Editing track (circular cylindrical hole recognition, then Resize, Move and Delete
from the current B-Rep only); (3) the registered tests that declare no `TIMEOUT` — recount from
`CMakeLists.txt`, do not trust a cached count; (4) the stale `v0.5.6` product string in the CI
artifact README text. The `rotation-pivot` 60 s CI timeout is resolved: `CMakeLists.txt:298`
sets `TIMEOUT 120` with the measured 58.35 s / 60.03 s evidence in the comment above it.
No implementation work is authorized merely by reading this handover.

## 13. Provenance / confidence
**Repository-observed (2026-11-23):** `AGENTS.md`, `CLAUDE.md`, `README.md`, `CMakeLists.txt`,
`docs/ROADMAP.md`, `docs/SHARED_BREP_RECONSTRUCTION.md`, `docs/LOCAL_AI_HANDOVER.md`,
`docs/LOCAL_AI_PROGRESS.md`, `docs/LOCAL_AI_TASK_TIMES.csv` and `.github/workflows/build.yml`
on `develop` `fb370e5` were inspected while synchronizing the documentation after PR #79.
PR #68 merge status, PR #79 head/merge SHAs and the CI pass/fail counts were read from Git and
GitHub Actions in that session. **Not re-executed:** no C++ build or CTest run was performed in
the documentation session; the local full-suite figures quoted in `docs/LOCAL_AI_PROGRESS.md`
come from earlier development sessions. **Unverified:** Fedora KDE manual acceptance for the
v0.5.7 pipeline on legacy `.odc` documents, real multi-monitor behaviour, and whether the
`rotation-pivot` timeout reproduces on a re-run. Treat these as tasks for direct repository,
CI and local inspection, not as facts.
