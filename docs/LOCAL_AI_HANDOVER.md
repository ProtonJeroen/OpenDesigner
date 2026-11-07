# OpenDesigner — Technical development handover
**Snapshot:** 2026-11-07 | **Baseline:** `develop` v0.4.9 | **Audience:** Mistral Vibe / Qwen3-Coder local agent

> This is a navigation and engineering handover, **not** a replacement for source inspection. The code and tests on the checked-out commit are authoritative. Historical conversations and older milestone names are not proof of current behavior. Never silently mark planned features implemented.

## 1. Mission, license, reference
OpenDesigner is an open-source cross-platform **history-independent, direct-modeling CAD** application inspired by CoCreate SolidDesigner / OneSpace Modeling 15.50 and version 16.0 (2008). It aims to reproduce useful interaction workflows with modern improvements, not to clone proprietary code or assets. License: **GPL-3.0-only** (check `LICENSE`). Linux is the currently validated desktop target; Windows/macOS are targets, not certified releases. iPadOS and Android tablet support and a platform-neutral touch-mode setting are roadmap aspirations.

**Non-negotiable modeling invariant:** edit the *current B-Rep*, not primitive creation labels, feature histories or provenance. Recognize analytic geometry from faces/edges/adjacency. A Boolean, imported or transformed shape should be evaluated by geometry and a certified material/validity result. Refuse unsupported operations precisely and non-destructively; never silently approximate geometry.

## 2. Authoritative reading order
1. `README.md` — present product scope and limitations.
2. `docs/ROADMAP.md` — detailed implemented milestones, planned work and geometry-first requirements.
3. `CMakeLists.txt` — exact build targets, dependencies and test registration.
4. `docs/V049_INTEGRATION_REVIEW.md` — v0.4.9 validation, caveats.
5. `docs/COMMAND_SESSIONS.md`, `docs/SELECTION_CONTEXT.md`, `docs/NATIVE_DOCUMENTS.md`.
6. `docs/BREP_RECONSTRUCTION.md`, `docs/MOVE_FACE.md`, `docs/DELETE_FACE.md`, `docs/PULL_RESIZE_GAPS.md`.
7. `docs/EXTRUDE_PROFILE.md`, `docs/REVOLVE.md`, `docs/SKETCH_EDITOR.md`, `docs/SKETCH_PROJECTION.md`.
8. `docs/FACE_HISTORY_PERSISTENCE.md`, `docs/CROSS_SCREEN_HIDPI.md`, `docs/GUI_TEST_PLAN.md`.
9. `docs/SOLIDDESIGNER16_WORKFLOWS.md` — reference workflow analysis without proprietary manual assets.
10. Current `src/`, `tests/`, `.github/workflows/`, Git history and PR status.

Some previously discussed root-level files (`ARCHITECTURE.md`, `DECISIONS.md`, `CODEX_HANDOFF.md`) were not found at the inspected root of `develop`. Do not assume they exist; discover files before citing.

## 3. Build architecture (observed in CMake)
C++20, CMake >=3.21, Qt6 Widgets >=6.4, Open CASCADE, LibArchive. Fedora OCCT builds may require VTK CommonCore/IOImage targets to be found before OpenCASCADE. Main executable `OpenDesigner` includes `src/main.cpp`, `MainWindow`, `CadViewport`, `BooleanOperations`, `BRepReconstruction`, `MoveFaceGeometry`, `MoveFace`, `FaceOperations`, `FaceTransformGeometry`, `SelectionContext`, `NativeDocument`.

Dedicated libraries/targets:
- `OpenDesignerCommandPanels`: `CommandPanelPositioner`.
- `OpenDesignerConstruction`: `SketchDisplayBounds`, `ConstructionGeometry`, `SketchGeometry`, `SketchSnapping`, `SketchOverdraw`, `SketchConstruction`, `SketchProjection`, `SketchProfileValidation`, `SketchProfileBuilder`, `SketchProfileExtrusion`, `SketchProfileRevolution` and declared sweep interfaces.
- `OpenDesignerCylindricalMoveGeometry`: headless geometry with Qt Core, no Qt Widgets/visualization dependency.
- `OpenDesignerDeleteFaceGeometry`: Delete Face and planar deletion geometry.
- `OpenDesignerDebug`, `OpenDesignerDebugModel`: diagnostic model capture and inspection.

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

**Baseline evidence:** `README.md` identifies v0.4.9; its 47/47 manual checks are *user-reported*. `docs/V049_INTEGRATION_REVIEW.md` describes an initial full suite with failures, corrected targeted reruns, and 86 cases with passing results *across full and targeted runs*, not one clean full-suite pass. A separate earlier PR #68 was reported green with 88/88; check its actual merge and branch relevance. Never combine these figures as if they were the same run.

## 8. Historical development timeline (high level)
- v0.1: Qt/OCCT viewer, browser, box and Linux CI.
- v0.20: primitives, Pull, sphere and cone workflows.
- v0.3–v0.3.2: Boolean operations, pivot, explicit command sessions.
- v0.3.3–v0.3.6: face move/rotate/align/delete, B-Rep reconstruction, diagnostic export and topology-safe workflows.
- v0.3.7: native `.odc` storage.
- v0.4.0+: construction objects, sketch editor, projection, profile validation/build, Extrude/Revolve, elliptical geometry and expanded regression coverage.
- v0.4.9: cylindrical Move Face, geometry and crash fixes, persistence/history validation, cross-screen HiDPI qualifications.

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
Verify actual `develop` HEAD, branch divergence and PR #68 merge; read v0.4.9 integration review; inspect current CTest registration; confirm any pending KDE manual tests; compare roadmap planned sections with implementation; then choose the next small issue. No implementation work is authorized merely by reading this handover.

## 13. Provenance / confidence
**Repository-observed (2026-11-07):** `README.md`, `CMakeLists.txt`, `docs/ROADMAP.md`, `docs/V049_INTEGRATION_REVIEW.md`, `docs/FACE_HISTORY_PERSISTENCE.md`, `docs/GUI_TEST_PLAN.md` on `develop` were inspected in preparing this handover. **Historical context:** design preferences and prior milestones from development conversations. **Unverified:** exact current HEAD SHA, PR #68 merged status, current local Fedora working-tree state, complete class-level call graph and real multi-monitor acceptance. Treat these as tasks for direct repository/local inspection, not as facts.
