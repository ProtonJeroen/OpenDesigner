# OpenDesigner — Engineering operations, traceability and acceptance manual

**Document status:** technical handover, snapshot 2026-11-07. Read alongside `LOCAL_AI_HANDOVER.md`, `LOCAL_AI_TECHNICAL_REFERENCE.md`, `MISTRAL_VIBE_START.md` and actual repository code. This manual defines **how to verify** the project rather than treating all historical statements as proven.

## 1. Evidence hierarchy and change control

| Tier | Source | Appropriate use |
|---|---|---|
| A | Current checkout at recorded commit, passing tests with logs | Current implementation and tested behavior |
| B | Current repository feature documentation + matching source | Intended behavior and known limitations |
| C | GitHub PR discussion, CI artifacts and merge commits | Historical decisions and integration history |
| D | Development conversation / user-reported acceptance | Requirements and observed manual experience |
| E | Design proposal / AI hypothesis | Future work only |

Every implementation claim must record a **source path and commit SHA**. If docs and code conflict, flag a discrepancy; do not silently edit either to conceal it. Never report CI, local tests or user acceptance as equivalent evidence. Distinguish a current passing suite from counts aggregated across reruns.

## 2. Repository audit procedure

Start with a safe read-only inventory:

```sh
git status --short --branch
git rev-parse HEAD
git log --date=short --pretty='%h %ad %s' -30
git branch -a --contains HEAD
find src tests docs .github -type f | sort
rg -n 'add_executable|add_library|add_test|find_package|target_link_libraries' CMakeLists.txt
ctest --test-dir build -N
```

Do not assume `build` exists or is current. Do not run `git clean` or `git reset --hard`. For each source class, record header, implementation, dependencies, state owned, callbacks, thread constraints, geometry mutators, exception/error strategy and tests. Identify which public interfaces are actually stable. Do not derive a UML diagram from filenames alone.

Suggested inventory columns: `Subsystem | Class/function | Header | Implementation | Calls | Mutates | Geometry input/output | Tests | Known limits | Confidence`.

## 3. Geometry acceptance: reproducible protocol

For every geometry-changing operation:
1. Record original shape identity, transform, orientation, volume and bounding box.
2. Verify the selected faces/edges belong to the original shape, including transformed placements.
3. Produce candidate on a deep copy or immutable source snapshot.
4. Check finite parameters, OCCT validity, closed solids and no loose topology.
5. Check required solid count and connectivity.
6. Verify material occupancy with an independent oracle or exact Common/difference equivalence; do not accept solely by BRepCheck.
7. Verify unselected regions and external supports remain within feature-specific constraints.
8. Check stable Part identity and expected selection invalidation.
9. Confirm Undo/Redo and native Save/Open roundtrip for representative cases.
10. Record rejected unsupported cases and ensure refusal leaves model unchanged.

**Numerical risk:** OCCT `Precision::Confusion`, vertex tolerances, transformed shapes, large world coordinates and floating-point ULP budgets require explicit bounded behavior. Do not widen a strict `1e-6` material-equivalence tolerance to make a regression green without an independent proof. Test rotated, translated and large-coordinate cases.

**Topology risk:** OCCT can split, merge or reorder faces after Boolean operations. A face index is not a persistent geometric identity. Re-resolve from current analytic supports and adjacency, or explicitly mark topology references invalid. This is especially important for Move Face after repeated edits, imported STEP geometry and native document reload.

## 4. Feature acceptance matrix

| Feature | Positive tests | Negative tests | State/persistence tests |
|---|---|---|---|
| Box/Cylinder/Sphere/Cone | exact dimensions and material | zero, negative, overflow, invalid geometry | Undo/Redo, Save/Open |
| Pull/Resize | current B-Rep recognition, signed exact values | unsupported analytic topology, tolerance | candidate cleanup, Part metadata |
| Move/Rotate Part | one/multiple Parts, shared pivot, transformed solids | locked/hidden, invalid input | grouped history, placement |
| Boolean | Unite/Subtract/Intersect, Keep Tools | no overlap, disconnected invalid target, bad solid | target identity, Undo/Redo |
| Move Face planar | supported plane/cylinder/cone neighborhoods | freeform/ambiguous or material change invalid | exact restored shape |
| Move Face cylindrical | holes, notches, bosses, rotated rims, repeat edits | interacting, blind, blends, invalid trim | Part UUID, Save/Open, sessions |
| Delete Face | certified convex/rim/section cases | partial multi-Part success, unbounded, ambiguous | grouped atomic history |
| Workplane/Sketch | profiles, holes, construction, snaps | duplicate, open, self-intersecting | entity UUID and schema |
| Extrude | New/Add/Remove, signed depth, holes | ellipse profiles, disconnected Add, no-op | source preserved, one Undo |
| Revolve | axis references, signed angles, three modes | crossing axis, pinched region, curve axis | preview, native roundtrip |
| Projection | analytic edges, transformed Parts | collapsed/unsupported curves | no persistent source dependency |
| Native .odc | v1–v5 compatibility and camera | corrupt/future version, read-only paths | atomic replacement, preview excluded |

These are **required coverage families**, not a statement that every cell currently has an automated test.

## 5. Session/UI regression protocol

Every command should be exercised through: start with no selection; start with eligible preselection; hover; parameter change; invalid input; drag; exact numeric entry; Enter; OK; Escape; Cancel; change tool; Save during preview; New/Open; Undo/Redo; Part visibility changes; transform; HiDPI; keyboard focus. Verify document dirty flag and Active Part separately from viewport appearance.

**Manual Fedora KDE checklist:** normal DPI; 200% scale; Wayland; XWayland where applicable; physical multi-monitor with differing DPR; panel anchored inside viewport after move/resize; no offscreen controls; browser selection consistency; SpaceNavigator when installed. Record a screenshot or reproducible coordinates for each failure. Never infer cross-screen success from synthetic unit tests.

## 6. Persistence and compatibility protocol

Native `.odc` must store exact B-Rep, not triangulation. Test roundtrip of transformed Parts, colors, names, visibility, camera, Workplanes, construction entities, sketch curves and active Part metadata. Test schema v1–v5 where fixtures exist. Save As and Save on a read-only source must have predictable, atomic behavior. On corrupt/future-version input, retain the old in-memory document. Saving during a command must exclude candidate previews. Undo/Redo must **not** be expected after reopening because history is session-local.

A future nested-assembly format must preserve compatibility and stable Part UUIDs; the current root assembly container alone does not prove hierarchical assembly editing exists.

## 7. Branch, PR and CI policy

1. Record base SHA and clean/dirty state before task.
2. Use `feature/<scope>`, `fix/<scope>` or `docs/<scope>`; do not rewrite user branches.
3. Commit focused source + matching tests + documentation, not private `.odc` samples.
4. Run `git diff --check` and targeted tests before pushing.
5. PR description must include intent, technical design, unsupported scope, test commands, environment, evidence and manual gates.
6. CI green permits review, not automatic merge when manual acceptance is pending.
7. Merge only with explicit user approval. Never change branch protection.
8. For a release, verify current workflow coverage; do not call Windows/macOS validated based on Linux CI.

**Session log template:**

```text
Date / environment:
Base branch and SHA:
Task and acceptance:
Working tree before:
Changed files:
Geometry or UI invariants:
Targeted tests:
Full tests:
Manual checks:
CI run URL:
Known limitations:
Branch/commit/PR:
Working tree after:
Next safe action:
```

## 8. Prioritized technical debt and next steps

**Priority 0: evidence and safety.** Reconcile current `develop` with PR #68; capture exact SHA; run fresh full CTest on Fedora; check pending v0.4.9 manual acceptance, including physical HiDPI and cylindrical Move Face session/persistence coverage. This is verification work, not proof that the code is broken.

**Priority 1: reliability.** Regression-hardening for transformed/large-coordinate geometry; certified repeated face edits; topology identity after Boolean; atomic multi-Part commands; native document schema compatibility; command-panel focus and cleanup.

**Priority 2: architecture.** Reduce coupling in `CadViewport`/`MainWindow` only when tests prove command behavior; isolate geometry services; document contracts and data ownership; preserve headless testing. Avoid a speculative wholesale rewrite.

**Priority 3: roadmap planning.** Design hierarchical assemblies and geometry-based alignment first with coordinate-frame/identity invariants. Then materials/density, standard parts, file interoperability and touch/mobile UX. Thread creation remains unscheduled.

## 9. Offline AI autonomy and checkpoints

Use Ollama/Mistral Vibe/Qwen3-Coder with the actual installed model/quantization verified locally. RTX 3070 8GB + 48GB RAM requires selective retrieval: one feature document, relevant headers/sources and a handful of tests per iteration. Write a short checkpoint after each validated step; do not leave an unbounded conversational context running overnight.

A checkpoint must contain task ID, SHA, branch, current phase, last successful command, failing command, affected files, unresolved design questions and next command. Resume from the checkpoint **after checking Git state**. Never claim an overnight task completed if the terminal process was interrupted or test logs are missing.

## 10. Source references for technical claims

Primary: `README.md`, `CMakeLists.txt`, `docs/ROADMAP.md`, `docs/V049_INTEGRATION_REVIEW.md`, `docs/NATIVE_DOCUMENTS.md`, `docs/EXTRUDE_PROFILE.md`, `docs/REVOLVE.md`, `docs/MOVE_FACE.md`, `docs/DELETE_FACE.md`, `docs/SKETCH_PROJECTION.md`, `docs/COMMAND_SESSIONS.md`, `docs/CROSS_SCREEN_HIDPI.md`, `docs/GUI_TEST_PLAN.md`. Further geometry-specific docs are linked from these pages.

**Not yet verified in this handover:** complete symbol-level call graph; all test names against current `ctest -N`; PR #68 merge; Fedora working-tree state; actual multi-monitor acceptance; live Windows/macOS builds. These remain explicit audit items, not concealed assumptions.
