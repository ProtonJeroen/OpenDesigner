# OpenDesigner PR #79 — Local AI progress handover

**Snapshot:** 2026-11-23 (documentation synchronization after PR #79 merged into `develop`) | **Worktree:** `/home/Jeroen/OpenDesigner-develop` | **Branch:** `docs/synchronize-after-pr-79` | **Baseline HEAD:** `fb370e5bc7fb0c83064a35e8e03176280504f260` (`origin/develop`, PR #79 squash merge) | **Audience:** Mistral Vibe

> Sections 1-11 are the PR #79 development session family (worktree `/home/Jeroen/OpenDesigner-vibe`,
> branch `ai/vibe-moveface-review` / PR head `feature/v0.5.7-shared-brep-reconstruction`, baseline
> `c50f054`); their findings are preserved unchanged. Section 12 is this documentation task.
> Items marked **verified** were executed in the recorded session. Re-run the commands in section
> 5 (and section 12) before acting on any claim.

## 1. Git and working-tree status (verified)

| Item | Value |
| --- | --- |
| Branch | `ai/vibe-moveface-review` (local); PR #79 head branch is `feature/v0.5.7-shared-brep-reconstruction`, base `develop` @ `98ed414` |
| HEAD | `c50f054e7cdc423cab4f6e5a87d8100d59ed59a1` — "fix(tests): repair PR #79 CI failures and stabilise the Move Face snapshot" (Phase 11 commit; baseline for Phase 11b) |
| Working tree | Phase 11b start: `M docs/LOCAL_AI_PROGRESS.md` only (+10 / −4), i.e. the CI-outcome paragraphs written after the Phase 11 push. All code and test changes from Phases 2–10 are already committed in `c50f054` and are untouched by Phase 11b. After the Phase 11b commit the tree is clean |
| `git diff --check` | clean — no whitespace or conflict markers, including the four new files (checked with `git add -N` intent-to-add). Re-run in Phase 11b on the docs-only diff: exit 0 |
| Commits, pushes, merges | Phase 11: one commit on `ai/vibe-moveface-review`, pushed to `feature/v0.5.7-shared-brep-reconstruction` (fast-forward, no force). Phase 11b: one docs-only commit on top of `c50f054`, pushed to the same branch (fast-forward, no force). No merge, no release, no version bump |

## 2. Task objectives and completion status

| Task | Status |
| --- | --- |
| PR #79 Phase 2 — fix `brep-certification-correspondence` (ctest #8) | **Complete, verified** |
| Permanent technical-handover rule for all future sessions | **Complete** — `AGENTS.md` created at repo root |
| Mandatory task timing for all future development tasks | **Complete** — rule in `AGENTS.md` and `CLAUDE.md`; `docs/LOCAL_AI_TASK_TIMES.csv` created with the first measured row |
| PR #79 Phase 3 — fix the `move-face-drag-initialization` timeout (ctest #141) | **Complete, verified** |
| PR #79 Phase 4 — fix `cylindrical-open-notch-sessions` (ctest #67, "Boss cap cannot preview after notch edits") | **Complete, verified** — harness-only fix |
| PR #79 Phase 5 — fix `move-face-crash03112026` (ctest #68, "Exact crash offset preview failed") | **Complete, verified** — harness-only fix, five call sites |
| PR #79 Phase 6 — fix `cylindrical-move-face-recognition` (ctest #70, "Planar edit after open hole/local boss move failed") | **Complete, verified** — harness-only fix, one call site |
| PR #79 Phase 7 — fix `cylindrical-move-face-boundary` (ctest #71, "Plane z=60 offset=2 Trim=0: ") | **Complete, verified** — two-part fix: four harness awaits (the reported failure) **plus** a production fix in `CadViewport::movePartSnapshot` for an unstable snapshot signature |
| Eliminate blocking save prompts in automated GUI tests (separate task, 3.8) | **Complete, verified** — one shared `MainWindowTest::UnsavedPromptGuard` in `tests/MainWindowTestHelpers.h` replaces three per-file copies; both affected executables run unattended under Xvfb |
| GUI-dependent tests locally | **Unblocked** — `Xvfb`/`xvfb-run` are installed (`/usr/bin/Xvfb`, `/usr/bin/xvfb-run`); the Phase 2 "no X server" limitation is obsolete |
| PR #79 Phase 8 — fix `sloped-planar-opening-move` (ctest #65, silent exit 1) | **Complete, verified** — harness-only fix, one call site; the whole 23-test `OpenDesignerMoveFaceTests` family is now **23 PASS / 0 FAIL** |
| PR #79 `OpenDesignerMoveFaceTests` failures | **Closed locally** — all six originally-failing names plus `sloped-planar-opening-move` now pass; the 23-test family command from section 5 reports 23/23 |
| PR #79 Phase 9 — fix `edge-blend-sessions` (ctest #133, `exact preselection input failed`) | **Complete, verified** — harness-only fix, five call sites; the whole 52-test `OpenDesignerSelectionContextTests` family is now **52 PASS / 0 FAIL** |
| `OpenDesignerSelectionContextTests` remaining failure | **Closed locally** — `edge-blend-sessions` was the sole failure and is fixed; the 52-test family command from section 5 reports 52/52 |
| PR #79 Phase 10 — full regression validation of the whole worktree | **Complete, verified** — all 144 registered tests pass twice: once in the session `nl_NL` locale and once under `LC_ALL=C` (the CI-equivalent locale). 0 FAIL, 0 SKIP, 0 TIMEOUT in both runs. No production or test file was changed by this phase |
| Phase 7 `movePartSnapshot()` review (snapshot stability, genuine-change detection, OCCT flags, 7.6.3 compatibility, performance) | **Complete** — stability and sensitivity confirmed by probe; the seven-flag re-carry is **load-bearing for `Free`** (probe6, verified); OCCT 7.6.3 compilation is **not verified locally** (only 7.9.3 headers exist) and rests on CI |
| PR #79 Phase 11 — commit the reviewed repairs, push to the PR head branch, verify GitHub CI | **Complete, verified** — one commit `14f2d5c..c50f054` pushed fast-forward (no force) to `feature/v0.5.7-shared-brep-reconstruction`; PR #79 head verified unambiguous before pushing; **GitHub CI green on `c50f054`**: run `45063550829`, job `linux`, `conclusion=success`, `100% tests passed, 0 tests failed out of 142`, Build 1314 s / CTests 1757 s, which also closes the OCCT 7.6.3 compilation question for the Phase 7 flag setters |
| PR #79 Phase 11b — commit and push the CI handover documentation | **Complete, verified** — docs-only commit (`docs/LOCAL_AI_PROGRESS.md` + `docs/LOCAL_AI_TASK_TIMES.csv`) on top of `c50f054`, pushed fast-forward to the same PR head branch. No source, test or `CMakeLists.txt` file touched, so no build or test was re-run. PR #79 re-verified after the push: `state=OPEN`, `mergeable=MERGEABLE`, `mergeStateStatus=CLEAN`, head = the new docs SHA |
| `AGENTS.md` / `CLAUDE.md` consistency | **Complete, verified** — `CLAUDE.md` was missing the Mandatory Technical Handover and Handover file conventions sections that `AGENTS.md` carries; appended verbatim, the two files now differ only in their intentional title/intro paragraph |

## 3. Root cause and technical decisions

### 3.1 Phase 2 — `brep-certification-correspondence` (verified)

- Failure point: `tests/BRepCertificationTest.cpp:252`, `check(brep(source)==stamp,"Boolean copies/normalization mutated source")` inside `booleanValidityReuse()`. `check` throws, so the test aborts at the first failed guard.
- `brep()` is `BRepTools::Write` into an `ostringstream`, i.e. full serialization **including per-shape flags**. The observed diff was one line — the SOLID flag line `1100000` → `0100000`, the OCCT `Free` bit flipping `1 → 0`. Geometry, topology, locations and orientations were byte-identical.
- Mechanism: OCCT's `BRep_Builder::Add` clears the `Free` flag of the added shape's shared `TShape`. The test helper `compound()` added the caller's shape directly, so the fixture call `certificate(compound({source}),true,1,2,2)` mutated `source` itself. `TopoDS_Shape` is a handle over a shared `TShape`, hence the fixture's own input changed.
- Attribution: **proven** that `Add` alone flips the flag and that the compound fixture is the first mutating step. **Not demonstrated:** any mutation by `BooleanGeometry::certifiedSolid`, `combine<BRepAlgoAPI_Cut>` or `BRepReconstruction::normalize`. `combine` calls `SetNonDestructive(Standard_True)` (`src/BooleanGeometry.h:61`) and `normalize` copies its candidate first (`src/BRepReconstruction.cpp:485`). No production-code mutation was observed.
- Git history: both the fixture and the guard were introduced in the same commit `14f2d5c`; the test was self-inconsistent as authored.
- Decision: fix the fixture, not the guard. The strict full-serialization guard, the `brep()` helper and the failure message are unchanged, so the guard keeps its teeth — a probe confirmed the same comparison still detects a real `Add` mutation.

### 3.2 Phase 3 — `move-face-drag-initialization` timeout (verified)

- **Symptom.** ctest #141 (`OpenDesignerMoveFaceTests --drag-initialization`, `TIMEOUT 240 RUN_SERIAL TRUE`, registered in `CMakeLists.txt:573` by HEAD commit `14f2d5c`) ran the full 240 s and printed **nothing**. Reproduced locally under Xvfb: the process hangs and `timeout` kills it (exit 124) with zero bytes of output.
- **Root cause, proven by a live backtrace** (`gdb -p <pid> -ex "thread apply all bt"` on the hung process):
  `MoveFaceTest::dragInitialization()` → `QWindow::close()` → `MainWindow::closeEvent(QCloseEvent*)` → `showNewMessageBox` → `QDialog::exec()` → `ppoll`, parked forever.
  `MainWindow::closeEvent` (`src/MainWindow.cpp:719`) calls `maybeSave()` (`src/MainWindow.cpp:586`), which — when `documentDirty_` is true — opens a modal `QMessageBox::warning` ("Unsaved changes", Save/Discard/Cancel, default Save). An unattended run has nobody to click it, so `window.close()` at the end of the first loop iteration never returns.
- **Why the document is dirty.** Every `dragSession()` commits exactly one edit through the Enter key, and part changes call `MainWindow::markDocumentDirty()` (`src/MainWindow.cpp:337–367`), suppressed only while `documentTransition_` is set. The test's own Save/Open step uses viewport-level `exportDocumentSnapshot`/`importDocumentSnapshot` into a `QTemporaryDir`, not `MainWindow::openDocument`, so `documentDirty_` stays true and `documentPath_` stays empty.
- **Why no output.** `std::cout` is block-buffered when stdout is the ctest pipe; the process is killed before the buffer flushes. The two `DRAG_*` lines that had been produced are lost — hence "timed out without any output" in CI. With `stdbuf -oL` the partial output is visible and shows the hang starts right after the second session (`DRAG OFF reopened Box`), i.e. at the first `window.close()`.
- **Attribution.** The test function was added in `46d2f9b` and registered in `14f2d5c`; the unsaved-changes prompt predates both. The test was therefore un-runnable in CI from the moment it was registered. The earlier claim in `docs/FAST_DIRECT_MODELING_CERTIFICATION.md` ("`move-face-drag-initialization` also calls `window.close()`, but it passes unattended, so it does not hit the prompt") is **withdrawn**: it was based on desktop runs where the prompt could be answered by hand.
- **Decision: fix the harness, not production.** The prompt is intended application behaviour. `MainWindow::documentDirty_` is private and `MoveFaceTest` is not a friend (`src/MainWindow.h:15–37`), so the `window.documentDirty_=false; window.close();` idiom used by other session tests is not available here. The same file already solves this for `provisionalAcceptanceTimings` (`tests/MoveFaceTest.cpp:762–769`) by answering the prompt from a `QTimer`; Phase 3 reuses that pattern as a named helper.
- **Why Discard.** `Save` would call `saveDocument()` with an empty `documentPath_` → `saveDocumentAs()` → a modal `QFileDialog`, i.e. the same hang; `Cancel` would make `closeEvent` ignore the close and leave the window open. `Discard` is the only non-blocking answer, and the test's assertions do not depend on MainWindow's dirty state.
- **Not a timeout problem.** The timeout property, the assertions and the tolerances are untouched. Post-fix the test needs ~25 s against its 240 s budget.

### 3.3 Phase 4 — `cylindrical-open-notch-sessions` (verified)

- **Failure point.** `tests/CylindricalMoveFaceSessionTest.cpp:619`, `check(v.commandSession_->valid, "Boss cap cannot preview after notch edits")` (ctest #67). The test had already committed two notch moves; the failure appeared only at the third session, on the circular boss cap at local `z = 115`.
- **Root cause: the test asserted a preview that production deliberately defers.** The cap is a planar face, so `classifyMoveFeature()` returns `Planar` and `CadViewport::scheduleMoveNumericPreview()` (`src/CadViewport.cpp:259`) sets `valid = false`, `state = Parameters`, clears the candidate and schedules the certified preview on `moveNumericTimer_`, a single-shot **300 ms** timer created at `src/CadViewport.cpp:181`. The assertion therefore ran inside the debounce window, before `updateMoveNumericPreview()` had built a candidate. The two earlier sessions in the same test passed because their faces belong to cylindrical ThroughHole/Boss features, which take the synchronous `updateCommandParameter()` path and validate immediately.
- **Why this is a harness bug, not a geometry bug.** The deferred contract is asserted by a passing test: `tests/MoveFaceTest.cpp:274` (`"first typed value was evaluated early"`), and the helper `MoveFaceTest::awaitNumeric()` (`tests/MoveFaceTest.cpp:98`) exists precisely to pump the event loop past the debounce. A temporary diagnostic (since reverted) measured the candidate ~350 ms after `input()`: `valid = true`, `state = Preview`, `value = 1.5`, `geometryError` empty, and `BRepTool::volume(candidate) == BRepTool::volume(current) + π·18²·1` — the boss grew by exactly one part unit, so the previewed solid is geometrically correct.
- **Decision: fix the harness, not production.** Added a local `awaitNumericPreview(CadViewport&)` helper that pumps `QApplication::processEvents()` while `v.pendingMoveNumeric_` is set, bounded by a 60 s `QElapsedTimer`, and called it at the one failing site before the existing assertion. The helper deliberately does **not** assert validity, so call sites that expect an invalid preview keep their own assertion. The original assertion, its tolerance and the surrounding drag/Cancel/Undo/Redo sequence are untouched. `CylindricalMoveFaceSessionTest` is already a `friend struct` of `CadViewport` (`src/CadViewport.h:237`), so `pendingMoveNumeric_` is reachable without widening access.

### 3.5 Phase 5 — `move-face-crash03112026` (verified)

- **Failure point.** `tests/CylindricalMoveFaceSessionTest.cpp:487`, `check(v.commandSession_->valid,"Exact crash offset preview failed")` inside `crashPreview()`, reached through `OpenDesignerMoveFaceTests --crash-planar` (`CMakeLists.txt:329`, `TIMEOUT 120 RUN_SERIAL TRUE`). Reproduced 1 of 1 pre-fix: exit 1, deterministic, all earlier checks in the function (isolated `MoveFaceGeometry::move` sweep over `{.1,1,2,5,10,12.9,13,13.1,20,40}`, the inward-cut refusals, and the **drag** preview at line 484) had already passed.
- **Root cause: the same 300 ms planar numeric debounce as Phase 4, at five call sites.** A temporary diagnostic (since reverted) measured the session state immediately after `input(v,"5")`: `valid=0 state=1(Parameters) feature=0(Planar) pending=y candidate=null geometryError empty`. After pumping the event loop: `valid=1 state=2(Preview) value=5 pending=n candidate non-null volume=798645`. So the value was never evaluated at the assertion, not evaluated wrongly.
- **Why the drag path passed and the typed path did not.** `scheduleMoveNumericPreview()` (`src/CadViewport.cpp:265`) returns `false` — i.e. falls through to the synchronous `updateCommandParameter(false)` — unless `commandSession_->moveFeature==MoveFaceFeature::Planar` **and** `allowEllipticalCapMove` **and** `moveSourceSnapshot` is non-empty. `src/MoveFace.cpp:272` fills `moveSourceSnapshot` only for `Planar && !blendSupport && allowEllipticalCapMove`, so this right-end face at `x=163` is exactly the class production defers. Mouse drag never enters that gate, hence the passing line-484 assertion.
- **Attribution: harness, not production — proven, not assumed.** Three independent facts: (1) the deferral is deliberate, with an explicit status message "Move Face: preview pending. OK/Enter applies the current value." and the OK button enabled at `src/CadViewport.cpp:287`; (2) a **passing** test requires it — `tests/MoveFaceTest.cpp:273` `require(!v.commandSession_->valid&&v.pendingMoveNumeric_,"first typed value was evaluated early")`; (3) the commit path is correct under deferral — `confirmCommandSession()` calls `consumeMoveNumericPreview()` first (`src/CadViewport.cpp:417`). The awaited candidate then passes the test's full geometric certification, so no geometry defect exists on this path.
- **Scope of the fix.** Phase 4's `awaitNumericPreview(v)` helper is reused; five call sites in `crashPreview()` were coupled to the debounce: the `"5"` validity check (487), the `"-5"` rejection check (488), the `"13"` full-cylinder transition (489), the `"5"` whose `candidate` is captured for the "Commit differs from crash preview" comparison (491), and the `"2"` repeated right-end check (493). Fixing only 487 moves the failure to 489 — verified by the diagnostic run, which failed next at "Full-cylinder transition preview failed".
- **Assertion strengthened, not weakened.** `input(v,"-5");check(!valid,…)` previously passed **vacuously** (nothing had been evaluated yet). Awaiting first makes it assert that the inward cut was actually evaluated and refused. No assertion, tolerance, timeout or message was changed.

### 3.6 Phase 6 — `cylindrical-move-face-recognition` (verified)

- **Failure point.** `tests/CylindricalMoveFaceSessionTest.cpp:945`, `check(v.commandSession_->valid,"Planar edit after open hole/local boss move failed")` inside `documentRegression()`, reached through `OpenDesignerMoveFaceTests --cylindrical-document` (`CMakeLists.txt:332`, `TIMEOUT 120 RUN_SERIAL TRUE`). Reproduced 1 of 1 pre-fix: exit 1, deterministic, 10.08 s, with every earlier check in the function already passing (both cylinder inventories, the open-notch commit, both local boss moves, and the planar-capability classification at line 944).
- **Corrected prior claim.** The Phase 5 handover listed this test as "not a plain debounce call site". That was an untested assumption and is **withdrawn**: it is the same 300 ms planar numeric debounce, at one call site.
- **Root cause, measured rather than assumed.** A temporary diagnostic (since reverted) dumped the session state at the assertion: `valid=0 state=1(Parameters) feature=0(Planar) pending=y candidate=null snapEmpty=0 allowEll=1 blendSupport=0`. Every gate of `CadViewport::scheduleMoveNumericPreview()` (`src/CadViewport.cpp:267`) is therefore satisfied — `Planar` + `allowEllipticalCapMove` + non-empty `moveSourceSnapshot` — so the typed `"2"` was **deferred**, not evaluated wrongly. After pumping the event loop: `valid=1 state=2(Preview) value=2 candidate=set geometryError empty` after 368 ms, status "Move Face preview ready. OK/Enter applies; Cancel/Esc discards."
- **Ruled out, each by direct evidence.** *Feature recognition*: line 944 asserts `moveFeature==Planar` and passes; the failure is the next statement. *Stale selection*: `consumeMoveNumericPreview()` (`src/CadViewport.cpp:250`) accepted the pending request — the status became "preview ready", not "Move Face source or selection changed. Cancel and select the Face again.", which `invalidateMoveNumericContext()` would have emitted. *BRep reconstruction*: the awaited candidate is geometrically exact (below).
- **Geometric certification of the awaited candidate** (diagnostic, throwaway): current volume 811146 → candidate 820746, delta exactly 9600 = 2·80·60, the analytic sweep of the right-end face at x=163; `BRepReconstruction::equivalent(candidate, BRepAlgoAPI_Fuse(current, 2 mm slab))` = true; `BRepReconstruction::validSolid` = true; 10 faces, 21 edges, 0 non-manifold edges; seven `BRepClass3d_SolidClassifier` occupancy points confirm the new slab is inside, x=166 outside, and the relocated notch (axis at x=0 after the −54 edit), the boss at (123,36,90) and the rest of the stock are unchanged.
- **Commit path also verified**: OK applied exactly the certified candidate (`IsSame` true), Part identity preserved (persistentId, presentation, name, colour, visibility), undo stack grew by exactly one, Undo restored the base solid (volume 811146), Redo restored the candidate, a final Undo returned to base.
- **Decision: fix the harness, not production.** Phase 4's `awaitNumericPreview(v)` helper is reused; one call added between `input(v,"2")` and the untouched assertion. The helper asserts nothing, so a genuine stale-selection or geometry failure still surfaces through the original `check` with its original message. No assertion, tolerance, timeout or message changed; no production file touched.

### 3.7 Phase 7 — `cylindrical-move-face-boundary` (verified)

- **Failure point.** `tests/CylindricalMoveFaceSessionTest.cpp:722`, inside `boundaryRegression()` reached through `OpenDesignerMoveFaceTests --cylindrical-boundary` (`CMakeLists.txt:333-334`, `TIMEOUT 180 RUN_SERIAL TRUE`). Reproduced 1 of 1 pre-fix: exit 1 in ~5.4 s, message `Plane z=60 offset=2 Trim=0: `.
- **What `Trim=0` means — nothing diagnostic.** The message is `qPrintable(QString("Plane z=%1 offset=%2 Trim=%3: %4").arg(s.Plane().Location().Z()).arg(value).arg(trim).arg(v.commandSession_->geometryError))`. `trim` is the enclosing `for(bool trim : {false,true})` loop variable, printed as `0`/`1`; the trailing empty field is `geometryError`, which is **empty**. So the message says only "the session was not valid", with no error to report — it is not evidence of a geometry rejection.
- **Cause 1 — harness (the reported failure).** The check runs immediately after `input(v,value)`, but `scheduleMoveNumericPreview()` (`src/CadViewport.cpp:267`) deliberately defers Planar + typed-value input: it stores the request, sets `valid=false, state=Parameters` and starts the 300 ms `moveNumericTimer_` (`src/CadViewport.cpp:181`). The candidate is built only when the timer fires and `consumeMoveNumericPreview()` validates. The neighbouring checks in the same function (boss/hole/boundary drags) are synchronous, which is why only this loop failed. Fix: `awaitNumericPreview(v)` at the four planar call sites in this function (lines 722, 735, 737, 763).
- **Cause 2 — genuine production defect, exposed only after the await was added (at value `-2`).** With the preview actually computed, `consumeMoveNumericPreview()` rejected the request and `geometryError` became "Move Face source or selection changed. Cancel and select the Face again." (`src/CadViewport.cpp:262`, emitted from the gate at :275). `moveSourceUnchanged()` (`src/MoveFace.cpp:77`) compares the BIN byte snapshot from `movePartSnapshot()` against the live Part, and that comparison is **not stable for an unedited Part**.
- **Mechanism, measured.** OCCT attaches derived caches to the live `TShape` while previews and display run. Measured on the same `TShape` (`shapeIsSame=1`, identical topology faces=10 edges=22 vertices=14 wires=12, identical mesh census tris=10 nodes=352000368, `p2d=44`): `Locations 0 / Curve2ds 43` at session start → `Locations 2 / Curve2ds 49` after the first preview; raw BIN size 8431 → 9228 (+797). BIN format `VERSION_4` does not serialize triangulation, so the drift is purely Curve2ds/Locations. The byte signature of an untouched Part therefore changes during a session, and the guard reports a phantom selection change.
- **Fix.** `movePartSnapshot()` now serializes a `BRepBuilderAPI_Copy` of the Part shape instead of the live shape. A copy normalizes curve-on-surface caches to exactly one entry per (edge, face) incidence (44) and is stable across the drift (8420 bytes both times). This is a *normalization*, not a relaxation: no tolerance, no `IsSame` shortcut, no fuzzy tolerance, no healing.
- **Why the shape flags are carried over (verified, not assumed).** `tests/MoveFaceTest.cpp:1631-1632` (`experimental-box-provisional-preview`) deliberately asserts that the snapshot is sensitive to the root shape's `Free` flag. Measured: `FLAGS1 F=1 M=1 C=0 O=0 Cl=0 I=0 V=0`; after `BRep_Builder::Add(compound, probe.shape.Moved(loc))` → `F=0` (the attachment mechanism clears `Free` on the shared `TShape`) and `add_binary_equal=0`; after `probe.shape.Free(true)` → `F=1` and the snapshot equals `before` again. In OCCT 7.9.3 `TopoDS_Shape::Free()` and friends delegate to `myTShape`, and BIN writes those root flags, so a fresh copy (default flags) hid the change (`add_binary_equal=1`, test failed). The seven classic flags (`Free Modified Checked Orientable Closed Infinite Convex`) are therefore copied onto the canonical shape. `Locked()` is deliberately **not** carried: it is not present in every OCCT version and CI builds OCCT 7.6.3.
- **Causality proven both ways.** With the copy fix but without the flag carry-over, `--box-provisional` FAILs ("compound attachment mechanism changed", `add_binary_equal=1`); with the production fix reverted, it PASSes. With the copy fix plus carry-over it PASSes (41.1 s).
- **Cost.** `SESSION_IDENTITY` diagnostics: `calls=46 median_ms` 0.033 → 0.134, max 0.114 → 0.385. The copy roughly quadruples per-snapshot cost and stays sub-millisecond; `cylindrical-move-face-boundary` passes in 34.2 s direct / 59.18 s under ctest against its 180 s timeout.

### 3.8 Unattended GUI tests must not block on the save prompt (verified, separate task)

- **Objective.** No automated GUI regression test may park inside the modal "Unsaved changes" prompt; CTest has to run unattended. Production save behaviour is unchanged.
- **Complete prompt inventory** (`src/MainWindow.cpp`): `maybeSave()` at `:586` opens the **only** `Save|Discard|Cancel` box in the application. It is reached from `closeEvent()` (`:719`), `newDocument()` (`:628`) and `openDocument()` (`:639`). Every other production message box is Ok-only: `geometryErrorDialog` (`:38`), "Debug export failed" (`:92`), About (`:284`), "Save failed" (`:603`), "Open failed" (`:646`, `:652`). That unique button set is what identifies the prompt.
- **Why Discard, again measured.** `Save` runs `saveDocument()`, which with an empty `documentPath_` falls into `saveDocumentAs()` and a modal `QFileDialog` — the same hang. `Cancel` makes the guarded call a no-op. `Discard` is the only answer that returns.
- **Centralised.** The Phase 3 local `closeUnsaved()`, the inline `QTimer` in `provisionalAcceptanceTimings()` and the local `discardPrompt()` in `tests/ConstructionSessionTest.cpp` are replaced by one RAII `MainWindowTest::UnsavedPromptGuard` in the new `tests/MainWindowTestHelpers.h`, used at three sites. Each site also asserts `prompts.answered == 1`, so the prompt is now *asserted*, not merely survived — this closes the Phase 3 limitation "the harness answers the prompt rather than asserting on it".
- **Qt behaviour measured, not assumed** (throwaway probe `/tmp/qtpromptprobe/probe.cpp`, Qt 6.11.2, `QT_QPA_PLATFORM=Offscreen`): a `QMessageBox` created with a `QMainWindow` parent reports `parentWidget() == window` in a minimal app, but `parentWidget() == nullptr` for the **production** prompt while it is modal — Qt reparents an open modal `QDialog`. The first guard version scoped on the parent link and hung `move-face-drag-initialization` for the full 240 s; the diagnostic it printed (`parent=0`) is what identified this. The parent link is therefore only checked when Qt keeps it.
- **Scoping rule now in force** (all three must hold): the test declares the document it owns **before** the guarded call; the window must report a dirty document (title ends `" *"`); the box must carry `Save`+`Discard`+`Cancel`. Ownership claim rules: an empty path means "untitled", and the window label must then be exactly `Untitled`; a non-empty path must lie inside the canonical `QDir::tempPath()` root **and** its `fileName()` must equal the window's label (label = window title after the U+2014 em dash, minus the trailing `" *"`). A mismatch throws from the constructor before the guarded call starts, so a real user document is never silently discarded.
- **Refusal path verified to fail fast, not hang.** Temporarily claiming `QString()` at the `ConstructionSessionTest` site (whose window shows `saved-as.odc`) made `construction-sessions` **FAIL in 3.32 s** with `UnsavedPromptGuard: window shows "saved-as.odc"; declare the test-owned path instead of claiming an untitled document`. The site was reverted immediately after.
- **Deliberately untouched.** `tests/NativeDocumentSessionTest.cpp:144-180` verifies Cancel/Save/Discard through its own `answer()` helper and keeps its behaviour. Eight friend tests use `documentDirty_=false; window.close();` (`ActivePartSessionTest:230`, `DeletePartSessionTest:124`, `ProfileExtrusionSessionTest:112/244`, `ProfileRevolutionSessionTest:163/274`, `SketchDisplayBoundsSessionTest:141`, `SketchOrthogonalLineTest:146`, `SketchProjectionSessionTest:358`, `WorkplaneRenderingTest:165`); they assert clean-close semantics rather than prompt handling, so they were left alone. No global message-box suppression was introduced: the guard exists only for the lifetime of one guarded call.
- **Latent hazard, not changed.** `WorkplaneReferenceSessionTest::acceptPopups()` (`:286`, `:299`, `:559`) and `EdgeBlendSessionTest.cpp:202` install `QTimer::singleShot(0, …)` handlers that click *any* open `QMessageBox`; they would mis-answer an unsaved prompt if one were live in that region. None is live today, and changing them is out of scope.

### 3.9 Phase 8 — `sloped-planar-opening-move` (verified)

- **Failure point.** `tests/CylindricalMoveFaceSessionTest.cpp:315`, inside the `start` lambda of `slopedPlane()`: `check(v.commandSession_->valid,qPrintable(v.commandSession_->geometryError))`, reached through `OpenDesignerMoveFaceTests --sloped-plane` (`CMakeLists.txt:321-322`, `TIMEOUT 180 RUN_SERIAL TRUE`). Reproduced 1 of 1 pre-fix: exit 1 in 17.4 s, **stdout+stderr = exactly one byte (`\n`)**.
- **Why the message was empty — proven, not inferred.** The dispatcher's only handler is `catch(const std::exception& error) {std::cerr<<error.what()<<'\\n';return 1;}` (`tests/MoveFaceTest.cpp:2655`). `od -c` on the captured output gave a single `0a`. So `error.what()` was the empty string: the failing `check` was one whose message argument is `qPrintable(...)` of a **still-empty** `QString`. `check` throws `std::runtime_error(message)` (`tests/CylindricalMoveFaceSessionTest.cpp:61`), so an empty message yields an empty `what()`. A real geometry rejection would have filled `geometryError` in — the blank message is itself the tell.
- **Ruled out by construction.** OCCT 7.9.3 `Standard_Failure` derives from `Standard_Transient`, **not** `std::exception` (`/usr/include/opencascade/Standard_Failure.hxx:25`), so an OCCT exception cannot reach that handler; it would terminate the process (exit 134), not exit 1. The Release build has no debug info (`CMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG`, no `.debug_*` sections), which is why a `gdb` backtrace was not the tool of choice here.
- **Locating the site without debug info.** Temporary harness-only instrumentation in the test TU: a `checkDiag(bool,const char*,int)` plus `#define check(v,m) checkDiag((v),(m),__LINE__)`, which printed `CHECK_FAIL line=317 msg=[]`; the file was +2 lines at that point, i.e. original line 315. Both diagnostics were reverted immediately (file restored from `/tmp/pr79p8/CylindricalMoveFaceSessionTest.cpp.bak`, md5 `434e0cbc49887bc7552094cbb483fa4c`, `grep -c "dumpSloped\|checkDiag\|#define check"` = 0 afterwards).
- **State measured at the assertion** (`dumpSloped`, since removed): `valid=0 state=1(Parameters) feature=0(Planar) pending=1 candNull=1 errEmpty=1 allowEll=1 snapEmpty=0 ctxInv=0 blendSupport=0 byPoints=0 readOnly=0 provEnabled=0 phase=0 value=0`. Every gate of `CadViewport::scheduleMoveNumericPreview()` (`src/CadViewport.cpp:267`) is therefore satisfied, so the typed value was **deferred**, not evaluated wrongly. After pumping the event loop: `valid=1 state=2(Preview) pending=0 candNull=0 value=5 candSolid=1`, part volume 1.20237e6 → candidate 1.24675e6.
- **Root cause: the same 300 ms planar numeric debounce as Phases 4–7, at one further call site.** The Phase 7 note that this test "drives `MoveFaceGeometry` directly and does **not** fit the pattern" was **wrong** and is withdrawn. The `start` lambda types a value into a planar face and asserts `valid` inside the debounce window. It is called five times per run (`start(5)`, the `{5,-2,1}` loop, and `start(-1)` after `save(true)`), and the first call failed.
- **Attribution: harness, not production — proven by three facts.** (1) The deferral is deliberate: `scheduleMoveNumericPreview()` sets `valid=false, state=Parameters`, starts the 300 ms `moveNumericTimer_` (`src/CadViewport.cpp:181`) and emits "Move Face: preview pending. OK/Enter applies the current value." with OK enabled. (2) A **passing** test requires it: `tests/MoveFaceTest.cpp:275` `require(!v.commandSession_->valid&&v.pendingMoveNumeric_,"first typed value was evaluated early")`, followed by `awaitNumeric(v)`. (3) The awaited candidate passes the test's own full certification — `verify()` runs `BRepReconstruction::validSolid`, the independent physical-support oracle (volume + `BRepReconstruction::equivalent`), a 10×10×10 `BRepClass3d_SolidClassifier` occupancy grid, the two-use manifold edge census, ellipse-on-plane and ellipse-on-stationary-cylinder checks, and the ≤1e-6 tolerance sweep. All passed.
- **Regression window established from history.** `slopedPlane` was added in `e67e7d4` ("fix: move planar faces around stationary cylindrical openings"), where `scheduleMoveNumericPreview`/`pendingMoveNumeric_` occur **0** times in `src/CadViewport.cpp`; they occur **10** times at HEAD `14f2d5c` and **0** at `14f2d5c^`. So the assertion was correct when written and raced when HEAD introduced the debounce. This also explains `docs/SLOPED_PLANAR_OPENING_MOVE.md` recording the test as passing in 15.75 s.
- **Fix.** One added `awaitNumericPreview(v)` between `input(...)` and the untouched `check`. The Phase 4 helper is reused unchanged. No assertion, message, tolerance or timeout changed; no production file touched. The helper asserts nothing, so a genuine stale-selection or geometry failure still surfaces through the original `check` with `geometryError` populated.
- **Diagnosability gap left open deliberately.** The message at that site is `qPrintable(geometryError)`, which is empty whenever the session is merely pending. Fixing it would change an assertion message, which the task forbade; the recommended follow-up is in section 8.

### 3.10 Phase 9 — `edge-blend-sessions` (verified)

- **Failure point.** `tests/EdgeBlendSessionTest.cpp:471`, `check(v.updateCommandParameter(false),"exact preselection input failed")` inside `preselection()`, reached through `OpenDesignerSelectionContextTests --edge-blends` (`CMakeLists.txt:509`, `TIMEOUT 120`). `runEdgeBlendSessions()` (`:672`) calls `preselection()` first, so this was the first assertion of the executable. Reproduced 1 of 1 pre-fix: exit 1 in 3.70 s, deterministic, message only.
- **Not the 300 ms debounce.** Phases 4–8 were all the planar numeric debounce. This one is a different path: Fillet/Chamfer sessions are **synchronous** — `CadViewport::scheduleMoveNumericPreview()` (`src/CadViewport.cpp:268`) returns `false` unless `moveFeature==Planar`, so `previewCommand(value)` runs inline. No `awaitNumericPreview` belongs here.
- **Decisive experiment.** The *same unmodified binary* run with `LC_ALL=C LANG=C` **passed** (exit 0, "Selection, command lifecycle and document checks passed."), while the session environment (`LANG=nl_NL.UTF-8`, `LC_NUMERIC=nl_NL.UTF-8`) **failed**. The trigger is the process locale, not geometry, selection or timing.
- **Gate measured inside the real test** (temporary harness-only diagnostic, since reverted): `locale=nl_NL text=1.23456789 toDouble_ok=0 parsed=0 hasAcceptable=0 valid=0 value=0 geomErr="Enter a finite positive value in millimeters."`. So `CadViewport::updateCommandParameter()` (`src/CadViewport.cpp:389-390`) rejected the input at the parse gate and `previewEdgeBlend()` was never reached — `geometryError` is the tell, and it is the parse message, not a blend-geometry message.
- **Root cause.** The test hardcodes a locale-specific numeric literal. In `nl_NL` the decimal separator is `,` and `.` is the **group** separator, so `QLocale().toDouble("1.23456789")` fails and `QLineEdit::hasAcceptableInput()` is false (the `PullDimensionValidator` at `src/CadViewport.cpp:110` inherits the system locale). Production is deliberately locale-aware: every numeric read goes through `QLocale().toDouble(dimensionNumber(...))` and every write through `QLocale().toString(...)`.
- **Precision gate ruled out by reading production.** `CadViewport::beginCommandSession` (`src/CadViewport.cpp:307`) already does `validator->setDecimals(kind==Fillet||kind==Chamfer ? 17 : 2)`, so an 8-decimal blend value is legal; the constructor's `QDoubleValidator(-100000.0,100000.0,2,...)` default applies only to non-blend sessions. A standalone Qt probe (`/tmp/pr79p9/qprobe/probe.cpp`, Qt 6.11.2) confirmed the two independent gates: under `LC_ALL=C` the 2-decimal validator rejects `1.23456789` while `toDouble` accepts it, and under `nl_NL` both reject it. For blends the validator gate is lifted to 17, leaving the locale as the only blocker.
- **Attribution: test-harness defect, proven.** Five sites in `tests/EdgeBlendSessionTest.cpp` pass a hardcoded `"d.ddd"` string to `dragDimension_->setText()`, while the same file already uses the locale-aware idiom at `:247`, `:252`, `:298`, `:523`, `:606`, `:656` — e.g. `:523` is `setText(QLocale().toString(1.23456789,'f',8))`, the exact value `:471` intends. The literals are the outliers; production is consistent.
- **Fix.** Replace the five literals with `QLocale().toString(value,'f',N)` where `N` is the literal's decimal count: `:471` (1.23456789, 8), `:402` and `:411` (2.345, 3), `:427` (1.25, 2), `:434` (1.5, 1). Integer literals (`"0"`, `"1"`, `"2"`, `"20"`, …) are locale-invariant and were left alone. No assertion, message, tolerance, timeout or geometry changed; no production file touched. The test now exercises the user's real locale instead of assuming `C`.
- **Cascading sites found by execution, not prediction.** Fixing only `:471` moved the failure to `circular numeric radius failed` (`:402`); the other three decimal literals were then found by `grep 'setText("[0-9]*\.[0-9]'` and fixed in the same pass. All five are in `preselection()` and `circular()`.
- **Move Face family not re-run, by design.** `tests/EdgeBlendSessionTest.cpp` is compiled into exactly one executable (`CMakeLists.txt:502`, `OpenDesignerSelectionContextTests`), and no production source changed in this task, so the conditional 23-test Move Face recheck was not triggered. The Phase 8 result (23 PASS / 0 FAIL) still stands.

### 3.11 Phase 10 — full regression validation and Phase 7 review (verified)

- **Scope.** Build every configured target, then run the **complete registered CTest suite** (144 tests) under Xvfb, twice: once in the session's existing `nl_NL` locale and once under `LC_ALL=C`, which is the locale CI uses. Validation and analysis only — no commit, push, merge, package install, production change or test change.
- **Result: 144 PASS / 0 FAIL / 0 SKIP / 0 TIMEOUT in both runs.** `Total Test time (real)` 1224.08 s (`nl_NL`) and 1138.30 s (`C`); both ctest invocations exited 0. JUnit reports agree: `tests=144 failures=0 skipped=0 disabled=0` in both files. There is therefore **nothing to attribute** to the current changes, to the harness or to an existing defect — requirement 11 has an empty answer, and that is itself the finding.
- **The `C` run is the CI-equivalent check.** `.github/workflows/build.yml` is the only workflow; its test line (line 25) is `xvfb-run -a -s "-screen 0 1280x1024x24 +extension GLX" ctest --test-dir build -C Release --output-on-failure -j2` — same server args, same `-j2`, **no `--timeout`**, default `C`/POSIX locale. The two local runs bracket that invocation on both locale axes and found no difference, so no test is locale-dependent any more (Phase 9 removed the only such case).
- **Timeout coverage gap found (not a failure).** 117 of the 144 registered tests declare a `TIMEOUT` property (min 30 s, median 120 s, max 300 s); **27 declare none** — 17 `sketch-*` geometry names plus `Chamfer-geometry`, `Fillet-geometry`, `brep-support-topology`, `command-panel-positioning`, `command-panel-positioning-hidpi`, `construction-geometry`, `reference-geometry`, `profile-extrusion-sessions`, `profile-extrusion-sessions-hidpi` and `selection-contours`; the full list is in section 7. Locally those were bounded with ctest `--timeout 300`, which is a harness-level bound only: it changes no registered property and no test source. In CI they are **unbounded**, so a hang in one of them would stall the workflow rather than fail it. 39 tests are `RUN_SERIAL`, which is why `-j2` is the ceiling.
- **Slowest tests in the `C` run** (headroom against their budgets, all far inside): `face-history-persistence` 71.89 s, `cylindrical-open-notch-geometry` 67.67 s, `cylindrical-move-face-boundary` 59.22 s, `cylindrical-move-geometry` 46.14 s, `move-face-drag-initialization` 44.90 s, `move-face` 42.41 s, `move-face-clear-sweep-provisional` 40.70 s, `sloped-planar-opening-move` 39.57 s. The Phase 3 timeout fix holds: `move-face-drag-initialization` is now ~45 s against a 120 s budget.
- **Phase 7 review — snapshot stability.** Within one shape state the snapshot is stable: `snapshot bytes=5714/5714 stable=1`, and stable again after meshing (`post-mesh snapshot pair stable=1`). The canonical copy is +24 bytes over the live serialization on this fixture, i.e. the copy is a different but deterministic stream.
- **Phase 7 review — genuine edits are still detected.** Placement (5 mm translate), dimension (box 163 → 164) and radius (r=17 → 18) edits all change the snapshot (`detected=1`), so the copy did not blunt the guard.
- **Phase 7 review — the seven-flag re-carry is load-bearing, and specifically for `Free` (this resolves the earlier open conflict).** `TopoDS_Shape` flag accessors delegate to the shared `TShape` (`TopoDS_Shape.hxx:144-165`), so the question was whether `BRepBuilderAPI_Copy` propagates them. Probe6 reproduced the exact box-provisional scenario: `TopoDS_Builder::Add` into a compound flips `part.Free` 1 → 0, the **live** stream changes, a **raw copy stream does not change** (`copy WITHOUT recarry changed by Add=0`, and `poke Free(false)` gives `copy Free=1`), while the **snapshot with the re-carry does change** (`SNAPSHOT changed by Add=1`). So `BRepBuilderAPI_Copy` resets `Free` to true on the copy and the re-carry is what restores the sensitivity; `Modified`/`Checked`/`Convex` are inherited by the copy, which is why the earlier probe4 (which only poked `Modified`/`Checked`) made the block look redundant. Do not delete those seven lines.
- **Phase 7 review — residual risk: subshape flags are not normalized.** `BRepMesh_IncrementalMesh(part, 0.1, False, 0.5, True)` clears `Checked` on two subshapes of the fixture (`checked=2 → 0`) and the snapshot then differs (`SNAPSHOT changed by mesh=1`) even though the source flags are byte-identical before and after. Attribution of the live-shape drift was measured precisely: `GProp` alone adds nothing; meshing alone changes the live stream by **2 bytes** (`firstDiffByte=4555`); extrema, a classifier pass and a pcurve census add nothing further. So on this synthetic fixture the drift is *flag* churn, not the Curve2ds/Locations cache growth the Phase 7 handover measured inside the app (+797 bytes, 8431 → 9228). Both are real drift of the live shape and the copy normalizes the cache growth; it does **not** normalize the flag churn. Consequence: if tessellation ever runs between the two `movePartSnapshot()` calls in `moveSourceUnchanged()` (`src/MoveFace.cpp:91`, calls at `:96` and `:98`), the guard can report a false "source changed" and refuse a move that did happen to be a no-op. No registered test covers that interleaving — all 144 pass — so this is an **unresolved residual risk, not a demonstrated defect**.
- **Phase 7 review — flag order is correct but fragile.** `TopoDS_TShape::Modified(true)` also clears `Checked` (`TopoDS_TShape.hxx:74`), so `Modified` must precede `Checked`; production does that, and probe confirmed `M/C pair carried=1` and `prod order equals reverse order=1` for the resulting stream. A future reorder of those two lines would silently drop `Checked`.
- **Phase 7 review — performance.** Heavy fixture (400×300×60 box cut by 24 cylinders → 366 subshapes, live BIN 49603 bytes): live serialize median 0.2417 ms, snapshot median 0.9246 ms (~3.8× live); 46 `moveSourceUnchanged()`-style pairs (92 snapshots) cost **84.82 ms**. Light fixture: 46 snapshots median 0.10 ms, 46 pairs 9.2–9.6 ms. Consistent with the Phase 7 in-app measurement (`SESSION_IDENTITY calls=46 median_ms` 0.033 → 0.134). No registered test is within 25 % of its budget because of this, so no performance regression is currently observable in the suite; the cost scales with subshape count and should be re-measured before raising snapshot frequency.
- **Names argument is a non-issue.** `BRepBuilderAPI_Copy(part)` (default `Names=false`) and the repo's usual `(shape, true, false)` produce byte-identical snapshot streams (`names affect BIN stream=0`, 5714/5714), so the Phase 7 call's argument choice is not an inconsistency worth changing.
- **OCCT 7.6.3 compatibility could not be executed locally.** Only `/usr/include/opencascade` 7.9.3 exists here and no fetch/web connector was available. Evidence-based inference: `tests/NativeDocumentTest.cpp:56-57` already calls `Free() Locked() Modified() Checked() Orientable() Closed() Infinite() Convex()` at HEAD, so CI at 7.6.3 already compiles those accessors including `Locked()`; the seven setters Phase 7 uses are therefore expected to compile. **This is an assumption confirmed only by CI, not by this session.**
- **Probe binaries exit 139 (SIGSEGV at teardown)** when linked with the all-`libTK*.so` line; `mini.cpp` exits 0 when linked minimally (`-lTKBin -lTKBinL -lTKPrim -lTKTopAlgo -lTKGeomBase -lTKGeomAlgo -lTKBRep -lTKMath -lTKernel`). That is a link-line artifact of the throwaway probes, **not** a defect in the code under review.

### 3.12 Phase 11 — commit, push and CI validation (verified)

- **Scope.** Review the whole uncommitted diff, commit it as one logical change on `ai/vibe-moveface-review`, push it to the PR #79 head branch without force, then monitor GitHub Actions. No code was written in this phase: `cmake --build build -j4` reported `ninja: no work to do` (0.03 s), i.e. the tree was byte-identical to what Phase 10 validated with 144 PASS / 0 FAIL twice.
- **Every file reviewed before staging.** `git diff --numstat HEAD` gave 7 modified files (105 insertions / 37 deletions) and 4 untracked files. Each hunk was read and mapped to the phase that produced it: `src/MoveFace.cpp` → Phase 7; `tests/BRepCertificationTest.cpp` → Phase 2; `tests/MoveFaceTest.cpp` → Phase 3 + save-prompt task; `tests/CylindricalMoveFaceSessionTest.cpp` → Phases 4–8 (12 `awaitNumericPreview` call sites + the helper); `tests/ConstructionSessionTest.cpp` → save-prompt task; `tests/EdgeBlendSessionTest.cpp` → Phase 9 (5 locale literals); `CLAUDE.md` → task-timing rule. No hunk was unexplained, and no hunk changed an assertion, message, tolerance or timeout.
- **Two hunks in `CylindricalMoveFaceSessionTest.cpp` looked like undocumented locale fixes and are not.** The `QLocale().toString(...,'g',15)` calls in the `slopedPlane` `start` lambda and in the boss-cap site appear on both the `-` and `+` side of the diff: they are pre-existing at `14f2d5c`, and the only change in those lines is the added `awaitNumericPreview(v)`. Verified by printing the full hunks rather than the grep of added lines.
- **Temporary diagnostics: none left.** `git diff HEAD | grep -iE 'SESSION_IDENTITY|checkDiag|dumpSloped|FLAGS[123]|DIAG'` is empty, and the same grep over the four new files matches only the intentional `UnsavedPromptGuard::diagnose()` refusal reporting in `tests/MainWindowTestHelpers.h`. The Phase 7/8/9 diagnostics were already reverted in their own sessions.
- **Generated files and private fixtures: none staged.** `git status --porcelain --ignored=matching` lists exactly the 11 entries above plus `!! build/`, which is ignored. No `.odc` file appears in the diff or in the untracked set.
- **Credentials: none.** `ghp_`, `github_pat_`, `BEGIN * PRIVATE KEY`, `api_key`, `secret`, `password`, `token=` scanned across the tracked diff and all four new files: no match.
- **Instruction-file drift found and fixed.** `diff AGENTS.md CLAUDE.md` showed two differences: the intentional title/intro paragraph, and `CLAUDE.md` missing the whole `## Mandatory Technical Handover` + `### Handover file conventions` block that `AGENTS.md` carries. Since `AGENTS.md` requires both files to change together, the block was appended to `CLAUDE.md` verbatim. After the fix the only remaining difference is the title/intro paragraph.
- **Whitespace.** `git add -N` on the four untracked files, then `git diff --check` and `git diff --check HEAD`: both exit 0, so the new files are covered. No trailing whitespace, tab or CRLF in any of them; no conflict markers anywhere in the diff.
- **PR #79 verified unambiguous before pushing** (`gh pr view 79`): `headRefName = feature/v0.5.7-shared-brep-reconstruction`, `headRefOid = 14f2d5ccedc413ce2f1c1224bd5f0986f13c722b`, `baseRefName = develop` @ `98ed414`, `state = OPEN`, `isDraft = false`, `mergeable = MERGEABLE`. `git fetch` + `git rev-list --left-right --count HEAD...origin/feature/v0.5.7-shared-brep-reconstruction` = `0 0`, so local HEAD equals the PR head exactly and one new commit fast-forwards. The local branch's upstream is that same remote branch. Pushed with an explicit refspec `HEAD:refs/heads/feature/v0.5.7-shared-brep-reconstruction`, no `--force`.
- **The Phase 7 snapshot fix is preserved exactly as reviewed.** `movePartSnapshot()` still serializes `BRepBuilderAPI_Copy(part.shape).Shape()` and still re-carries the seven root flags (`Free Modified Checked Orientable Closed Infinite Convex`) in that order, with `Locked()` deliberately absent for OCCT 7.6.3 compatibility. Nothing in Phase 11 touched `src/MoveFace.cpp`. Do not delete those seven lines and do not reorder `Modified` before `Checked` — see 3.11 probe6 and `TopoDS_TShape.hxx:74`.
- **Residual risk carried into the PR description, not fixed.** `BRepMesh_IncrementalMesh` clears `Checked` on subshapes of the live Part without changing geometry, and `movePartSnapshot()` then produces a different stream (2 differing bytes, `firstDiffByte=4555` on the synthetic fixture). If tessellation ever runs between the two snapshots in `moveSourceUnchanged()` (`src/MoveFace.cpp:96` and `:98`), the guard can report a false "Move Face source or selection changed". No registered test covers that interleaving, all 144 pass, and a fix would be a production change outside this task. The canonical copy normalizes Curve2ds/Locations cache growth (the +797-byte in-app drift) but **not** subshape flag churn.
- **Observed snapshot overhead, carried into the PR description.** Serializing a copy instead of the live shape costs about 3.8–4×: in-app `SESSION_IDENTITY calls=46 median_ms` 0.033 → 0.134 (max 0.114 → 0.385); heavy fixture (366 subshapes, live BIN 49603 bytes) live 0.2417 ms vs snapshot 0.9246 ms median, and 46 `moveSourceUnchanged()`-style pairs cost 84.82 ms. Sub-millisecond per call, no test within 25 % of its budget, cost scales with subshape count.
- **OCCT 7.6.3.** The seven flag setters were never compiled locally (only 7.9.3 headers exist). CI was the first place this was checked; the inference that they compile rested on `tests/NativeDocumentTest.cpp:56-57` already using the same accessors at `14f2d5c`. **Now confirmed by execution** — see 3.13.

### 3.13 Phase 11b — CI outcome recorded in the handover, committed and pushed (verified)

- **Why a second commit exists.** `docs/LOCAL_AI_PROGRESS.md` is part of the Phase 11 commit, so the Actions result for `c50f054` could only be observed *after* that commit was pushed. The CI paragraphs therefore had to be written afterwards and shipped in a follow-up docs-only commit. This closes the loop for a fresh session: the handover on the PR branch now states the CI result instead of deferring it to a session report.
- **Nothing but documentation changed.** `git status --porcelain` at Phase 11b start listed exactly `M docs/LOCAL_AI_PROGRESS.md`; `git diff --stat` = 1 file, 10 insertions / 4 deletions. Every code, test and `CMakeLists.txt` change from Phases 2–10 is already in `c50f054` and was left byte-identical. No build or test command was run, because no compiled file changed — `AGENTS.md` forbids re-running passing tests when relevant sources did not change.
- **GitHub CI on `c50f054` — read from the Actions log, not inferred.** Actions run **`45063550829`**, workflow `Build`, single job `linux`, `headSha c50f054e7cdc423cab4f6e5a87d8100d59ed59a1`, `status=completed`, `conclusion=success`, job wall clock `2026-11-22T19:56:18Z` → `2026-11-22T20:48:04Z` (51 m 46 s). Step results: Set up job 1 s, checkout 1 s, Dependencies 26 s, Configure 2 s, **Build 1314 s success**, **CTests (X11 and software OpenGL) 1757 s success**, Stage Linux executable 0 s, Upload Linux artifact 1 s.
- **Test result: 142/142 PASS.** CTest summary line: `100% tests passed, 0 tests failed out of 142`, `Total Test time (real) = 1754.68 sec`, command `xvfb-run -a -s "-screen 0 1280x1024x24 +extension GLX" ctest --test-dir build -C Release --output-on-failure -j2`. Every test touched by Phases 2–10 passed on CI: `brep-certification-correspondence` 0.99 s, `move-face-drag-initialization` 68.61 s, `cylindrical-move-face-recognition` 14.24 s, `cylindrical-move-face-boundary` 65.72 s, `move-face-crash03112026` 10.80 s, `cylindrical-open-notch-sessions`, `sloped-planar-opening-move`, `edge-blend-sessions` 32.20 s, `construction-sessions` 26.85 s.
- **OCCT 7.6.3 confirmed.** The Dependencies step installed `libocct-foundation-7.6t64`, `libocct-modeling-data-7.6t64`, `libocct-modeling-algorithms-7.6t64`, `libocct-visualization-7.6t64`, `libocct-data-exchange-7.6t64`, `libocct-ocaf-7.6t64`, `libocct-draw-7.6t64` on Ubuntu 24.04, and the Build step compiled `src/MoveFace.cpp` against them. The Phase 7 flag setters therefore compile on 7.6.3; the local probe numbers remain 7.9.3 numbers.
- **CI runs 142 tests, local runs 144 — fully explained, not a gap.** Verified by diffing `ctest --test-dir build -N -C Release` names against the names in the CI log: local-only = `command-panel-cross-screen` (inside `if(Qt6Widgets_VERSION VERSION_GREATER_EQUAL 6.11)`, `CMakeLists.txt:210`, CI Qt is 6.4.2) and `experimental-whole-section-lines` (registered only when Boost.Multiprecision headers are found, `CMakeLists.txt:136-139`, not in the CI dependency list); CI-only = none.
- **Which SHA the CI evidence belongs to.** The green run `45063550829` is for `c50f054`, the code+test commit. Pushing this documentation commit triggered a second Actions run, **`45070445941`**, for the docs-only tip. A fresh session must not read "CI green" as covering an unobserved run: check `gh pr checks 79` for the current tip before relying on either run.
- **The Phase 11b CI wall clock is deliberately *not* in the CSV `test_seconds` field.** That field is defined as the duration of a test command run for the task in this session, and no test command ran. The CI durations are recorded in prose here.

## 4. Files changed and why

| File | Change | Reason |
| --- | --- | --- |
| `tests/BRepCertificationTest.cpp` | `compound()` now adds `BRepBuilderAPI_Copy(s,true,false).Shape()` per child instead of the caller's shape (+3 comment lines) | Stops `BRep_Builder::Add` from clearing the `Free` flag of the caller's shared `TShape`. One hunk, 6 insertions / 1 deletion. |
| `tests/MainWindowTestHelpers.h` (new, untracked, 146 lines) | Namespace `MainWindowTest` with `insideTemporaryDirectory()`, `documentLabel()`, `PromptReport` and the RAII `UnsavedPromptGuard`. | One shared harness for the modal save prompt, replacing three per-file copies. Ownership is validated in the constructor, before the guarded call. |
| `tests/MoveFaceTest.cpp` (11 insertions / 10 deletions) | `#include "MainWindowTestHelpers.h"`; the Phase 3 local `closeUnsaved()` removed; `dragInitialization()` and `provisionalAcceptanceTimings()` now wrap `window.close()` in `UnsavedPromptGuard` and assert `prompts.answered == 1`. | Same fix as Phase 3, centralised and now asserted rather than merely survived. |
| `tests/ConstructionSessionTest.cpp` (7 insertions / 8 deletions) | `#include "MainWindowTestHelpers.h"`; the anonymous-namespace `discardPrompt()` removed; the `window.newDocument()` call wrapped in `UnsavedPromptGuard(window, window.documentPath_, prompts)` plus `check(prompts.answered == 1, …)`. | Second prompt site in the same family; declares the `QTemporaryDir` fixture it owns. |
| `tests/CylindricalMoveFaceSessionTest.cpp` | Phase 4: new `#include <QElapsedTimer>`, a local `awaitNumericPreview(CadViewport&)` helper (pumps `processEvents()` while `pendingMoveNumeric_` is set, 60 s bound), and one added call before the existing boss-cap assertion. Phase 5: five more `awaitNumericPreview(v)` calls in `crashPreview()` (lines 487–491 and 493). Phase 6: one more call in `documentRegression()` at line 945. Phase 7: four more calls in `boundaryRegression()` at lines 722, 735, 737, 763. Phase 8: one more call in the `start` lambda of `slopedPlane()` at line 315. | Harness-only fixes for the 300 ms planar numeric-preview debounce. File diff vs `HEAD`: 21 insertions / 12 deletions, 9 hunks. No assertion, message or tolerance changed. |
| `src/MoveFace.cpp` (Phase 7) | `movePartSnapshot()` serializes `BRepBuilderAPI_Copy(part.shape).Shape()` and carries the seven root shape flags onto the copy before `BinTools::Write`; new `#include <BRepBuilderAPI_Copy.hxx>`. Two hunks, 15 insertions / 1 deletion. | The live-shape byte signature drifts as OCCT attaches Curve2ds/Locations caches during previews, producing a false "Move Face source or selection changed". Serializing a normalized copy makes the signature stable while keeping the flag sensitivity the box-provisional test asserts. |
| `tests/EdgeBlendSessionTest.cpp` (Phase 9, 5 insertions / 5 deletions, 5 hunks) | Five `dragDimension_->setText("<hardcoded decimal>")` calls replaced with `QLocale().toString(value,'f',N)` at lines 402, 411, 427, 434, 471. | The literals are invalid in any locale whose decimal separator is not `.` (e.g. `nl_NL`), so `updateCommandParameter()` rejected them at the parse gate. Matches the locale-aware idiom already used six times in the same file. |
| `AGENTS.md` (new, untracked) | Created: full standing rules carried over verbatim from `CLAUDE.md`, plus the Mandatory Technical Handover section and handover-file conventions. Later in this session: the Mandatory task timing section and the CSV conventions were added, and `docs/LOCAL_AI_TASK_TIMES.csv` was listed under handover-file conventions. | No `AGENTS.md` existed, so Vibe sessions had no repo-root instruction file. |
| `CLAUDE.md` (modified, +84 lines) | Mandatory task timing section and CSV conventions appended after `## Reporting`; in Phase 11 the Mandatory Technical Handover section and handover-file conventions were appended as well, so the file matches `AGENTS.md` apart from its title/intro paragraph | `AGENTS.md` requires that a standing-rule change update both files so they cannot drift. |
| `docs/LOCAL_AI_TASK_TIMES.csv` (new, untracked) | Fixed header plus the first measured row (`docs-task-timing-instructions`) | Required by the task-timing rule; append-only from here on. |
| `docs/LOCAL_AI_PROGRESS.md` | This document. Phase 11b added the CI outcome for `c50f054` (section 3.13), refreshed sections 1/2/4/5/7/8 for the two-commit state, and appended the Phase 11b timing row | Required by the handover rule. |

Phases 2–6 modified no production source. Phase 7 changed one production function, `CadViewport::movePartSnapshot()` in `src/MoveFace.cpp`, after the drift was measured on the live shape. `BooleanGeometry::solidResult` (`src/BooleanGeometry.h:79-88`) does call `builder.Add(compound, solids.Current())` on shapes derived from its input; it is harmless today (multi-solid inputs already have non-free children, single-solid inputs return before `Add`) but is a latent hazard for any future code that `Add`s a caller's top-level shape. Left unchanged — unproven as a defect and out of scope.

## 5. Build and test commands with results (executed)

### Phase 2

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPENDESIGNER_BUILD_TESTS=ON
cmake --build build --target OpenDesignerBRepCertificationTests
QT_QPA_PLATFORM=Offscreen ctest --test-dir build -C Release -R '^brep-certification-correspondence$' --output-on-failure
```

- Before the fix: **FAIL** (0.15 s) — `Boolean copies/normalization mutated source`. Reproduced 2 of 2 runs, deterministic.
- After the fix: **PASS** (0.35 s). Three direct runs of `./build/OpenDesignerBRepCertificationTests` all exited 0 and printed all seven phase lines, including `Boolean invocation-local reuse: … passed`. Only `ROOT_TIMING` millisecond values differ between runs.
- The 13-test batch listed in that run: 12 **PASS**, `move-face-stacked-cylinder-unification` **FAIL** with `X Error … BadWindow` — environmental (no X server then). It now **passes under Xvfb** (see Phase 3), so that item is closed.

### Phase 3 — reproduction (pre-fix build)

```bash
# Xvfb screen spec must be `-screen 0 1600x1200x24`; `-g` is invalid, Xvfb
# refuses to start and Qt aborts in init_platform with SIGABRT and no message.
timeout 150 xvfb-run -a --server-args="-screen 0 1600x1200x24" \
  ./build/OpenDesignerMoveFaceTests --drag-initialization
```

- **HANG reproduced 1 of 1** — exit 124 at 150 s, zero output. `gdb -batch -p <pid> -ex "thread apply all bt"` on the hung process gave the `closeEvent` → `showNewMessageBox` → `QDialog::exec` stack quoted in 3.2.
- The pre-fix build was re-created by reverting only the call site (`closeUnsaved(window)` → `window.close()`) and rebuilding incrementally.

### Phase 3 — after the fix

```bash
cmake --build build --target OpenDesignerMoveFaceTests
xvfb-run -a --server-args="-screen 0 1600x1200x24" \
  ctest --test-dir build -C Release -R '^move-face-drag-initialization$' --output-on-failure
```

- **PASS 25.79 s** (ctest exit 0). Three further direct runs `xvfb-run -a … ./build/OpenDesignerMoveFaceTests --drag-initialization`: **3/3 exit 0**, each printing all eight `DRAG_TIMING`/`DRAG` session lines (OFF/ON × new/reopened × Box/Extrude) and the final success line. `full_reconstructions` is exactly 22 per session with the provisional flag OFF and 5 with it ON in every run — no extra event processing observed.

### Phase 3 — regression batch (whole `OpenDesignerMoveFaceTests` family, 23 registered tests)

```bash
xvfb-run -a --server-args="-screen 0 1600x1200x24" \
  ctest --test-dir build -C Release -j2 \
  -R '^(face-history-persistence|move-face|move-face-numeric-input|move-face-preview-reuse|move-face-startup-validation|spline-move-face-session|obround-move-face-session|move-face-geometry|elliptical-move-face-session|sloped-planar-opening-move|cylindrical-rotated-rim|cylindrical-open-notch-sessions|move-face-crash03112026|cylindrical-move-face-sessions|cylindrical-move-face-recognition|cylindrical-move-face-boundary|cylindrical-move-face-sessions-hidpi|experimental-box-provisional-preview|move-face-provisional-box|move-face-drag-initialization|move-face-provisional-placement|move-face-clear-sweep-provisional|move-face-stacked-cylinder-unification)$' \
  --output-on-failure
```

- **18 PASS / 5 FAIL** in 197.6 s. Failures: `sloped-planar-opening-move`, `cylindrical-open-notch-sessions` ("Boss cap cannot preview after notch edits"), `move-face-crash03112026` ("Exact crash offset preview failed"), `cylindrical-move-face-recognition` ("Planar edit after open hole/local boss move failed"), `cylindrical-move-face-boundary` ("Plane z=60 offset=2 Trim=0: …").
- **Independence proven:** the same five tests were run against the pre-fix build (call site reverted, incremental rebuild) — **0 of 5 passed**, identical messages. They are pre-existing assertion failures in Move Face geometry paths, not hangs, and not caused by this change. They match the "other five failing CI tests" and were deliberately **not** investigated.
- `move-face-stacked-cylinder-unification` (the Phase 2 environment-blocked item) **PASS** 2.64 s under Xvfb.

Tests intentionally **not** run: the full 144-test suite, and every test outside the `OpenDesignerMoveFaceTests` executable (no relevant source changed for them since Phase 2).

### Phase 4 — reproduction (pre-fix build)

```bash
xvfb-run -a --server-args="-screen 0 1600x1200x24" \
  ./build/OpenDesignerMoveFaceTests --open-notch
```

- **FAIL reproduced 3 of 3** — `Assertion failed: Boss cap cannot preview after notch edits`, exit 1, deterministic.
- Temporary diagnostic (reverted): immediately after `input()` the session had `valid = 0`; after pumping `QApplication::processEvents()` past the 300 ms timer, `valid = 1`, `state = 2 (Preview)`, `value = 1.5`, `geometryError` empty, and `BRepTool::volume(candidate) == BRepTool::volume(current) + π·18²·1`.

### Phase 4 — after the fix

```bash
cmake --build build --target OpenDesignerMoveFaceTests
xvfb-run -a --server-args="-screen 0 1600x1200x24" \
  ctest --test-dir build -C Release -R '^cylindrical-open-notch-sessions$' --output-on-failure
```

- **PASS 11.40 s** (ctest exit 0). Three further direct runs `timeout 170 xvfb-run -a … ./build/OpenDesignerMoveFaceTests --open-notch`: **3/3 exit 0**, each ending with "Repeated open-notch viewport, drag, numeric input, Cancel and cached Undo/Redo passed".
- Family re-run with the Phase 3 command above: **19 PASS / 4 FAIL** in 196.09 s, `cylindrical-open-notch-sessions` **PASS** 11.28 s. The four remaining failures carry the identical messages from the Phase 3 batch, so Phase 4 fixed one of the five and changed nothing else. Logs: `/tmp/family_phase4.log`, `/tmp/family_phase4_final.log`.
- `xvfb-run -a … ctest --test-dir build -C Release -R '^(brep-certification-correspondence|move-face-stacked-cylinder-unification)$'`: both **PASS** (0.35 s / 2.58 s).

### Phase 5 — reproduction (pre-fix build)

```bash
timeout 170 xvfb-run -a --server-args="-screen 0 1600x1200x24" \
  ./build/OpenDesignerMoveFaceTests --crash-planar
```

- **FAIL reproduced 1 of 1** — `Exact crash offset preview failed`, exit 1, 6.46 s, deterministic. Output ends after the isolated-offset sweep, i.e. the geometry sweep and the drag preview had already passed.
- Diagnostic (temporary, reverted): the state dump quoted in 3.5. It also showed the next failure after 487 was `Full-cylinder transition preview failed`, which set the five-site scope.

### Phase 5 — after the fix

```bash
cmake --build build --target OpenDesignerMoveFaceTests          # 7.19 s
xvfb-run -a --server-args="-screen 0 1600x1200x24" \
  ctest --test-dir build -C Release -R '^move-face-crash03112026$' --output-on-failure
```

- **PASS** — ctest #68, 6.35 s (exit 0), against its 120 s timeout. Three further direct runs: **3/3 exit 0**, each ending with "Isolated native crash geometry, drag, preview, Cancel and Undo/Redo regression passed" and `Post-replacement face owners: current=9 stale=0`.
- The awaited candidate is certified by the test's own `verify()` lambda: `BRepReconstruction::validSolid`, a manual 2-use manifold edge census, the exact analytic volume `(163+d)·80·60 − π·17²·60 + area·55` to a relative 1e-6, `BRepReconstruction::equivalent` against an independently built oracle, and a 9×5×7 `BRepClass3d_SolidClassifier` occupancy grid. All ran and passed.

### Phase 5 — family and rechecks

- Phase 3/4 family command (23 registered tests): **20 PASS / 3 FAIL** in 210.54 s. `move-face-crash03112026` **PASS** 6.35 s. Remaining failures carry byte-identical messages to the Phase 4 batch: `sloped-planar-opening-move` (14.28 s), `cylindrical-move-face-recognition` ("Planar edit after open hole/local boss move failed", 7.15 s), `cylindrical-move-face-boundary` ("Plane z=60 offset=2 Trim=0: ", 1.93 s). Log: `/tmp/family_phase5.log`.
- `xvfb-run -a … ctest --test-dir build -C Release -R '^(brep-certification-correspondence|move-face-stacked-cylinder-unification)$'`: both **PASS** (0.35 s / 2.62 s).

### Phase 6 — reproduction (pre-fix build)

```bash
xvfb-run -a --server-args="-screen 0 1600x1200x24" \
  ./build/OpenDesignerMoveFaceTests --cylindrical-document
```

- **FAIL reproduced 1 of 1** — `Planar edit after open hole/local boss move failed`, exit 1, 10.08 s, deterministic. Output ended after the two `Native cylinder radius=…` lines and three `Post-replacement face owners: current=10 stale=0` lines, i.e. the whole preceding document sequence had already passed.
- Temporary diagnostic (since reverted) gave the pre/post state dump and the geometric certification quoted in 3.6. Pump time to the certified preview: 366–375 ms across runs, consistent with the 300 ms `moveNumericTimer_` plus evaluation.

### Phase 6 — after the fix

```bash
cmake --build build --target OpenDesignerMoveFaceTests          # 7.17 s
xvfb-run -a --server-args="-screen 0 1600x1200x24" \
  ctest --test-dir build -C Release -R '^cylindrical-move-face-recognition$' --output-on-failure
```

- **PASS** — ctest #70, 7.49 s (exit 0), against its 120 s timeout. Three further direct runs `xvfb-run -a … ./build/OpenDesignerMoveFaceTests --cylindrical-document`: **3/3 exit 0**, each ending with "Two-feature document selection/availability/recognition regression passed" (logs `/tmp/rec_run{1,2,3}.log`).
- `git diff` of the test file after reverting the diagnostic shows exactly six hunks; the Phase 6 hunk is the single added `awaitNumericPreview(v)` at line 945.

### Phase 6 — family and rechecks

- Phase 3/4/5 family command (23 registered tests): **21 PASS / 2 FAIL** in 203.86 s. `cylindrical-move-face-recognition` **PASS** 7.47 s. Remaining failures carry byte-identical messages to the Phase 5 batch: `sloped-planar-opening-move` (14.23 s) and `cylindrical-move-face-boundary` ("Plane z=60 offset=2 Trim=0: ", 1.95 s). Log: `/tmp/family_phase6.log`.
- `xvfb-run -a … ctest --test-dir build -C Release -R '^(brep-certification-correspondence|move-face-stacked-cylinder-unification)$'`: both **PASS** (0.36 s / 2.60 s).

Tests intentionally **not** run: the full 144-test suite and every test outside `OpenDesignerMoveFaceTests` / `OpenDesignerBRepCertificationTests`; no relevant source changed for them.

### Phase 7 — reproduction (pre-fix build)

```bash
timeout 200 xvfb-run -a --server-args="-screen 0 1600x1200x24" \
  ./build/OpenDesignerMoveFaceTests --cylindrical-boundary      # exit 1, ~5.4 s
```

- **FAIL reproduced 1 of 1** — `Plane z=60 offset=2 Trim=0: `, exit 1, deterministic, log `/tmp/boundary_repro.log`. Everything before the loop had already passed (boundary commit, face-owner census, post-boundary planar context activation).
- Temporary diagnostics (all reverted, file restored from `/tmp/CylindricalMoveFaceSessionTest.cpp.bak`, md5 `5b0a77f232ccef0315c3a7f931cea296`): session state at the assertion (`valid=0 state=1(Parameters) feature=0(Planar) pending=y`), and after pumping the event loop (`valid=1` for `2`, then `valid=0` with `geometryError="Move Face source or selection changed. Cancel and select the Face again."` for `-2`). A `FLAGS1/FLAGS2/FLAGS3` probe in `tests/MoveFaceTest.cpp` (removed with `sed -i '/FLAGS1 F=/d;/FLAGS2 F=/d;/FLAGS3 F=/d'`) measured the `Free`-flag behaviour quoted in 3.7.

### Phase 7 — after the fix

```bash
cmake --build build --target OpenDesignerMoveFaceTests          # 7.2 s incremental
timeout 200 xvfb-run -a --server-args="-screen 0 1600x1200x24" \
  ./build/OpenDesignerMoveFaceTests --cylindrical-boundary      # exit 0, 34.2 s
timeout 200 xvfb-run -a --server-args="-screen 0 1600x1200x24" \
  ./build/OpenDesignerMoveFaceTests --box-provisional           # exit 0, 41.1 s
```

- `--cylindrical-boundary`: **exit 0**, ends with "Committed boundary topology/selection regression passed"; two further direct runs **2/2 exit 0** (33.87 s, 34.12 s). Log `/tmp/boundary_fix3.log`.
- `--box-provisional`: **exit 0** with the flag carry-over present. It **FAILs** with the copy fix but without carry-over (`add_binary_equal=1`, "compound attachment mechanism changed") and **PASSes** with the production fix reverted — causality proven in both directions.
- `xvfb-run -a … ctest --test-dir build -C Release -R '^(brep-certification-correspondence|move-face-stacked-cylinder-unification)$' --output-on-failure`: **2/2 PASS** (0.35 s / 2.58 s, 2.93 s total). `OpenDesignerBRepCertificationTests` links only `OpenDesignerCylindricalMoveGeometry` + `Qt6::Gui` (`CMakeLists.txt:141-144`), so it cannot be affected by the `src/MoveFace.cpp` change; it was re-run anyway.

### Phase 7 — family and rechecks

- Phase 3/4/5/6 family command (23 registered tests, `-j2`): **22 PASS / 1 FAIL** in 303.97 s, log `/tmp/family_phase7b.log`. `cylindrical-move-face-boundary` **PASS** 59.18 s (ctest #71, timeout 180), `experimental-box-provisional-preview` **PASS** 3.59 s. Sole failure: `sloped-planar-opening-move` (ctest #65, 14.32 s), out of scope for this phase.
- An intermediate family run with the copy fix but **without** the flag carry-over gave **21 PASS / 2 FAIL** in 231.81 s (`/tmp/family_phase7.log`): `sloped-planar-opening-move` plus `experimental-box-provisional-preview`. That run is what identified the flag requirement.

Tests intentionally **not** run: the full 144-test suite and every test executable outside `OpenDesignerMoveFaceTests` / `OpenDesignerBRepCertificationTests`. `src/MoveFace.cpp` is compiled into several other test executables, so a full-suite run is the recommended next CI-side check once the user allows it.

### Unattended save-prompt task — commands and results (executed)

```bash
cmake --build build --target OpenDesignerMoveFaceTests OpenDesignerSelectionContextTests -j4
xvfb-run -a --server-args="-screen 0 1600x1200x24" \
  ctest --test-dir build -C Release -R '^(move-face-drag-initialization|construction-sessions)$' --output-on-failure
```

- **First guard version FAILED** — `construction-sessions` **PASS** 3.49 s but `move-face-drag-initialization` **TIMEOUT** 240.03 s. The guard's own diagnostic printed `leaving prompt open - modal prompt belongs to another window`, and a temporary pointer dump showed `parent=0`: the scoping rule had assumed `box->parentWidget() == &window`, which Qt does not keep for the production prompt (see 3.8).
- **After the scoping fix: 2/2 PASS** — `construction-sessions` 3.46 s, `move-face-drag-initialization` 23.56 s (27.02 s total) against their 120 s / 240 s budgets.

Refusal path, verified to fail fast rather than hang (temporary edit, reverted immediately):

```bash
# tests/ConstructionSessionTest.cpp:284 claim changed from window.documentPath_ to QString()
cmake --build build --target OpenDesignerSelectionContextTests -j4
xvfb-run -a --server-args="-screen 0 1600x1200x24" \
  ctest --test-dir build -C Release -R '^construction-sessions$'
```

- **FAIL in 3.32 s** (not a timeout), message `UnsavedPromptGuard: window shows "saved-as.odc"; declare the test-owned path instead of claiming an untitled document`. The site was restored to `window.documentPath_` and rebuilt.

Regression, both affected executables under Xvfb at `-j2` (test names extracted from `CMakeLists.txt` per executable):

```bash
xvfb-run -a --server-args="-screen 0 1600x1200x24" \
  ctest --test-dir build -C Release -j2 -R '^(<all 24 OpenDesignerMoveFaceTests names>)$'   # 350.00 s
xvfb-run -a --server-args="-screen 0 1600x1200x24" \
  ctest --test-dir build -C Release -j2 -R '^(<all 52 OpenDesignerSelectionContextTests names>)$'   # 147.61 s
```

- Move Face executable: **22 PASS / 1 FAIL** in 350.00 s. Sole failure `sloped-planar-opening-move` (ctest #65), the known pre-existing failure from Phase 7. 23 of the 24 names matched: `move-face-box-shadow-observation` is gated behind `OPENDESIGNER_ENABLE_BOX_SHADOW` (`CMakeLists.txt:551`, default `OFF`) and is not registered in this build.
- Selection-context executable: **51 PASS / 1 FAIL** in 147.61 s. Sole failure `edge-blend-sessions` (ctest #133, 0.74 s, `exact preselection input failed` at `tests/EdgeBlendSessionTest.cpp:471`).
- **Independence of that failure proven by execution, not assumed:** `tests/ConstructionSessionTest.cpp` was temporarily replaced with its `HEAD` version (fixed copy saved to `/tmp/regression/ConstructionSessionTest.cpp.fixed`), the executable rebuilt, and `edge-blend-sessions` still **failed** identically (0.58 s). The fixed file was then restored and rebuilt, `diff -q` clean. `--edge-blends` dispatches to `runEdgeBlendSessions()` from `tests/SelectionContextTest.cpp:519`, a translation unit this task never touched.

Final confirmation on the restored binary:

```bash
xvfb-run -a --server-args="-screen 0 1600x1200x24" \
  ctest --test-dir build -C Release -R '^(move-face-drag-initialization|construction-sessions|native-document-sessions)$' --output-on-failure
```

- **3/3 PASS** in 30.17 s: `native-document-sessions` 1.19 s (the test that deliberately verifies Cancel/Save/Discard — untouched and still passing), `construction-sessions` 3.65 s, `move-face-drag-initialization` 25.33 s.

Tests intentionally **not** run: the full 144-test suite, and every executable outside the two that compile `MainWindowTestHelpers.h`. No production source changed in this task.

### Phase 8 — `sloped-planar-opening-move`

Reproduce before changing anything (pre-fix binary, HEAD test file):

```bash
xvfb-run -a --server-args="-screen 0 1600x1200x24" ./build/OpenDesignerMoveFaceTests --sloped-plane
# exit 1, 17.4 s; stdout+stderr captured to file = 1 byte (`od -c` shows a single `0a`)
```

Diagnostics (temporary, harness-only, both reverted): `checkDiag` + `#define check` macro printed `CHECK_FAIL line=317 msg=[]` (file was +2 lines ⇒ original line 315); a `dumpSloped` state dump printed the session state listed in 3.9 and exited 0.

```bash
cmake --build build --target OpenDesignerMoveFaceTests -j4   # 7.251 s (macro diag), 7.400 s (dump diag), 7.240 s (final fix) = 21.9 s total
xvfb-run -a --server-args="-screen 0 1600x1200x24" ./build/OpenDesignerMoveFaceTests --sloped-plane   # x3
xvfb-run -a --server-args="-screen 0 1600x1200x24" ctest --test-dir build -C Release -R '^sloped-planar-opening-move$' --output-on-failure
xvfb-run -a --server-args="-screen 0 1600x1200x24" ctest --test-dir build -C Release -j2 -R '^(<all 23 registered MoveFaceTests names>)$' --output-on-failure   # log /tmp/pr79p8/family_phase8.log
xvfb-run -a --server-args="-screen 0 1600x1200x24" ctest --test-dir build -C Release -R '^(brep-certification-correspondence|move-face-stacked-cylinder-unification|move-face-drag-initialization|cylindrical-open-notch-sessions|move-face-crash03112026|cylindrical-move-face-recognition|cylindrical-move-face-boundary)$' --output-on-failure
```

- Direct runs after the fix: **3/3 exit 0** in 25.04 / 24.61 / 24.70 s, each ending `Sloped planar Move Face with stationary cylindrical openings: geometry, repeated signs, numeric sessions, history and persistence passed`.
- ctest single test: **1 PASS / 0 FAIL**, `sloped-planar-opening-move` 22.23 s (against its 180 s budget).
- Full Move Face family: **23 PASS / 0 FAIL** in 312.16 s of test time (315.19 s wall) at `-j2`. `sloped-planar-opening-move` 36.79 s. This closes the family that Phase 7 left at 22 PASS / 1 FAIL.
- The seven named tests in one run: **7 PASS / 0 FAIL** in 84.51 s — `brep-certification-correspondence` 0.35 s, `cylindrical-open-notch-sessions` 11.29 s, `move-face-crash03112026` 6.30 s, `cylindrical-move-face-recognition` 7.54 s, `cylindrical-move-face-boundary` 30.88 s, `move-face-drag-initialization` 25.48 s, `move-face-stacked-cylinder-unification` 2.66 s.
- `edge-blend-sessions` re-run for the record: still **FAIL** in 0.68 s (`exact preselection input failed`, `tests/EdgeBlendSessionTest.cpp:471`). Different executable (`OpenDesignerSelectionContextTests --edge-blends`), untouched by this task, pre-existing.
- Tests intentionally **not** run: the full 144-test suite, and every executable outside `OpenDesignerMoveFaceTests`, `OpenDesignerBRepCertificationTests` and `OpenDesignerSelectionContextTests`. No production source changed in this task.

### Phase 9 — `edge-blend-sessions`

Reproduce before changing anything (pre-fix binary, HEAD test file):

```bash
timeout 170 xvfb-run -a --server-args="-screen 0 1600x1200x24" \
  ./build/OpenDesignerSelectionContextTests --edge-blends      # exit 1, 3.70 s
# Same binary, C locale:
LC_ALL=C LANG=C timeout 170 xvfb-run -a --server-args="-screen 0 1600x1200x24" \
  ./build/OpenDesignerSelectionContextTests --edge-blends      # exit 0
```

- **FAIL reproduced 1 of 1** in the session locale (`LANG=nl_NL.UTF-8`), exit 1, output exactly `exact preselection input failed`. **PASS under `LC_ALL=C`** with the identical binary — the locale is the trigger.
- Temporary harness-only diagnostic at line 471 (since reverted, file restored from `/tmp/pr79p9/EdgeBlendSessionTest.cpp.orig`, md5 `10e838c547e8ab3fd153a18402638b43`, `grep -c DIAG` = 0 afterwards) printed the gate values quoted in 3.10, plus the positive control: `localeText=1,23456789 toDouble_ok=1 hasAcceptable=1 updateCommandParameter=1 valid=1`.
- Standalone Qt probe `/tmp/pr79p9/qprobe/probe.cpp` (Qt 6.11.2, `QT_QPA_PLATFORM=Offscreen`) separated the two gates under both locales, as recorded in 3.10.

```bash
cmake --build build --target OpenDesignerSelectionContextTests -j4   # 5.407 s diag, 5.318 s first fix, 5.377 s final = 16.1 s
for i in 1 2 3; do timeout 170 xvfb-run -a --server-args="-screen 0 1600x1200x24" ./build/OpenDesignerSelectionContextTests --edge-blends; done
LC_ALL=C LANG=C timeout 170 xvfb-run -a --server-args="-screen 0 1600x1200x24" ./build/OpenDesignerSelectionContextTests --edge-blends
xvfb-run -a --server-args="-screen 0 1600x1200x24" ctest --test-dir build -C Release -R '^edge-blend-sessions$' --output-on-failure   # x3
xvfb-run -a --server-args="-screen 0 1600x1200x24" ctest --test-dir build -C Release -j2 -R '^(<all 52 registered OpenDesignerSelectionContextTests names>)$' --output-on-failure   # log /tmp/pr79p9/selctx_full.log
```

- Direct runs after the fix: **3/3 exit 0** in the session locale, plus **1/1 exit 0** under `LC_ALL=C`; each ends `Selection, command lifecycle and document checks passed.`
- ctest single test: **3/3 PASS** — ctest #133 `edge-blend-sessions` 4.22 s, 4.14 s, 4.19 s against its 120 s budget.
- Full `OpenDesignerSelectionContextTests` family (52 registered names, `-j2`): **52 PASS / 0 FAIL** in 146.81 s of test time (149.83 s wall). `edge-blend-sessions` 4.33 s in that run. This closes the family that the save-prompt task left at 51 PASS / 1 FAIL.
- Tests intentionally **not** run: the full 144-test suite, and every executable outside `OpenDesignerSelectionContextTests`. `tests/EdgeBlendSessionTest.cpp` is compiled only into that executable (`CMakeLists.txt:502`) and no production source changed, so the 23-test Move Face family was not re-run.

### Phase 10 — full-suite validation (executed)

Build of **all configured targets** in the existing build directory:

```bash
mkdir -p /tmp/pr79p10
/usr/bin/time -v cmake --build build -j4 > /tmp/pr79p10/build.log 2>&1   # exit 0, 115 steps, 70.88 s
ninja -C build -n                                                        # 0 pending steps afterwards
```

Build: **PASS**, 70.88 s, no compiler warnings or errors.

Run 1 — complete suite in the session's existing `nl_NL` locale:

```bash
cd /home/Jeroen/OpenDesigner-vibe && QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 \
  xvfb-run -a -s "-screen 0 1280x1024x24 +extension GLX" \
  ctest --test-dir build -C Release --output-on-failure -j2 --timeout 300 \
  --output-junit /tmp/pr79p10/full_nlnl_junit.xml > /tmp/pr79p10/full_nlnl.log 2>&1
```

Result: **144 PASS / 0 FAIL / 0 SKIP / 0 TIMEOUT**, `Total Test time (real) = 1224.08 sec`, `FULL_NLNL_EXIT=0`. The child process really was `nl_NL` (`LANG=nl_NL.UTF-8`, `LC_CTYPE=C.UTF-8`).

Run 2 — same command prefixed `env LC_ALL=C LANG=C LANGUAGE=`, writing `/tmp/pr79p10/full_c.log` and `/tmp/pr79p10/full_c_junit.xml` (run as a background process, id `process-83ce50862379-c4a62a81e9f205f5c1c76579`):

Result: **144 PASS / 0 FAIL / 0 SKIP / 0 TIMEOUT**, `Total Test time (real) = 1138.30 sec`, `FULL_C_EXIT=0`. JUnit: `tests=144 failures=0 skipped=0 disabled=0 time=1138`.

Timeout inventory (read from `build/CTestTestfile.cmake`, list in `/tmp/pr79p10/timeouts.txt`): 144 registered tests, 117 with a `TIMEOUT` property (30 / 120 / 300 s), **27 without one**, 39 `RUN_SERIAL`. The 27 were bounded locally by ctest `--timeout 300`; no registered property was altered.

Tests intentionally **not** run: none — the complete registered suite was run, twice.

### Phase 11 — pre-commit review, commit, push (executed)

```bash
git status --porcelain=v1 -b && git worktree list && git remote -v
git diff --numstat HEAD                       # 7 modified files, 105 insertions / 37 deletions
git diff HEAD -- src/MoveFace.cpp tests/BRepCertificationTest.cpp tests/EdgeBlendSessionTest.cpp
git diff HEAD -- tests/CylindricalMoveFaceSessionTest.cpp tests/MoveFaceTest.cpp tests/ConstructionSessionTest.cpp
git status --porcelain --ignored=matching     # 11 entries + !! build/ only
git diff HEAD | grep -inE 'SESSION_IDENTITY|checkDiag|dumpSloped|FLAGS1|FLAGS2|FLAGS3|DIAG|...'   # none
grep -inE 'ghp_|github_pat_|BEGIN [A-Z]* PRIVATE KEY|api_key|secret|password' AGENTS.md tests/MainWindowTestHelpers.h docs/* # none
diff <(sed 's/[[:space:]]*$//' AGENTS.md) <(sed 's/[[:space:]]*$//' CLAUDE.md)   # only the title/intro paragraph
/usr/bin/time -f 'BUILD_SECONDS=%e' cmake --build build -j4    # ninja: no work to do, 0.03 s
git add -N AGENTS.md docs/LOCAL_AI_PROGRESS.md docs/LOCAL_AI_TASK_TIMES.csv tests/MainWindowTestHelpers.h
git diff --check && git diff --check HEAD                     # both exit 0
timeout 90 gh pr view 79 --repo ProtonJeroen/OpenDesigner --json number,title,state,isDraft,headRefName,headRefOid,baseRefName,baseRefOid,mergeable,reviewDecision,url
timeout 60 git fetch origin feature/v0.5.7-shared-brep-reconstruction
git rev-list --left-right --count HEAD...origin/feature/v0.5.7-shared-brep-reconstruction   # 0 0
git push origin HEAD:refs/heads/feature/v0.5.7-shared-brep-reconstruction   # fast-forward, no --force
```

- Build: **PASS**, `ninja: no work to do`, 0.03 s — the working tree was identical to the Phase 10 validated tree, so no test was re-run for a code reason (AGENTS.md: do not rerun passing tests unless relevant sources changed; only Markdown and CSV changed).
- `git diff --check`: **clean** for both the working tree and against `HEAD`, with the four new files covered through intent-to-add.
- PR head verification: `headRefOid` equalled local `HEAD` exactly, so the target was unambiguous and the push was a fast-forward.
- Tests intentionally **not** run in this phase: the full 144-test suite and every individual test. No source file changed after the Phase 10 runs; the only edits were `CLAUDE.md`, `docs/LOCAL_AI_PROGRESS.md` and `docs/LOCAL_AI_TASK_TIMES.csv`. GitHub CI runs the full suite on the pushed SHA.

### Phase 11b — CI evidence and docs-only push (executed)

```bash
git status --porcelain=v1 -b && git log -1 --format='%H %an %s'
git ls-remote origin refs/heads/feature/v0.5.7-shared-brep-reconstruction   # == c50f054
git diff --stat && git diff --check                                            # 1 file, +10/-4, exit 0
python3 -c "import csv;rows=list(csv.reader(open('docs/LOCAL_AI_TASK_TIMES.csv')));print(len(rows),{len(r) for r in rows[1:]})"
/usr/bin/gh run view 45063550829 --json status,conclusion,headSha,displayTitle
/usr/bin/gh run view 45063550829 --json jobs                                    # per-step timings
/usr/bin/gh run view 45063550829 --log | grep -aE "tests passed|Total Test time"
/usr/bin/gh run view 45063550829 --log | grep -aoE "Test *#[0-9]+: [A-Za-z0-9-]+" | ... # CI test names
ctest --test-dir build -N -C Release | grep -oE "^ *Test +#[0-9]+: .*" | ... # local test names, for the 142 vs 144 diff
git add docs/LOCAL_AI_PROGRESS.md docs/LOCAL_AI_TASK_TIMES.csv
git commit -m 'docs: record the green CI result for PR #79 ...'                # docs only
git push origin HEAD:refs/heads/feature/v0.5.7-shared-brep-reconstruction      # fast-forward, no --force
/usr/bin/gh pr view 79 --repo ProtonJeroen/OpenDesigner --json state,headRefOid,mergeable,mergeStateStatus,statusCheckRollup
```

- The Phase 11b commit SHA is the tip of `feature/v0.5.7-shared-brep-reconstruction` after Phase 11; resolve it with `git log -1 --format=%H -- docs/LOCAL_AI_PROGRESS.md` rather than trusting a value written before the commit existed.
- CI queries above are read-only (`gh run view`, `gh pr view`); no CI job was re-triggered, no workflow file changed, no PR merge attempted.
- Build and tests intentionally **not** run in Phase 11b: the diff is two Markdown/CSV files, and `AGENTS.md` forbids re-running passing tests when relevant sources did not change. `build_seconds` and `test_seconds` for this task are therefore `not measured`.

## 6. Probe evidence (throwaway, outside the repo)

Scratchpad probe `probes/copy_independence.cpp`, compiled and linked OCCT-only (`-lTKPrim -lTKTopAlgo -lTKBRep -lTKGeomAlgo -lTKGeomBase -lTKMath -lTKernel`; note `TKernel` has no `.7.9.3` suffix, and there is no `TKIO`/`TKPrimitives` in this build):

| Probe | Result |
| --- | --- |
| Old helper | `source.Free` 1 → 0 after `Add`; `brep(source)` changed |
| Copy helper | `source.Free` stays 1; `brep(source)` unchanged |
| Standalone `BRepBuilderAPI_Copy(source,true,false)` | serialization byte-identical to source (2545 bytes) |
| Compound from copies vs. from originals | byte-identical (2566 bytes); only `IsSame`/`IsEqual` differ |
| Two-shape compound | both sources untouched |

Prior-session probes still valid: `probes/mutation_probe.cpp`, `dump_brep.cpp`, `dump_flags.cpp`, `add_free_flag.cpp`. Phase 3 evidence is the live `gdb` backtrace of the hung process (no probe needed).

Qt probe for 3.8: `/tmp/qtpromptprobe/probe.cpp`, compiled with `pkg-config --cflags --libs Qt6Widgets` (Qt 6.11.2) and run with `QT_QPA_PLATFORM=Offscreen`. A `QMainWindow` subclass whose `closeEvent` calls `QMessageBox::warning(this, …, Save|Discard|Cancel, Save)` gave `parent=win`, `buttons=3`, `save/discard/cancel` all non-null, and `button(Discard)->clicked()` returned answer `8388608` (= `QMessageBox::Discard`). The same parent check against the real `MainWindow` gave `parent=0`, which is the difference that forced the scoping rule in 3.8. Note `QWidget::closeEvent` takes `QCloseEvent*` and is virtual — declaring the override with `QEvent*` silently fails to override and the probe prints nothing.

Qt probe for 3.10: `/tmp/pr79p9/qprobe/probe.cpp` (Qt 6.11.2, `QT_QPA_PLATFORM=Offscreen`) isolated the two gates that `updateCommandParameter()` applies to `dragDimension_`. With `QLineEdit` + `QDoubleValidator(-100000.0,100000.0,2)`: under `LC_ALL=C`, `setText("1.23456789")` gives `toDouble_ok=1 hasAcceptable=0`; under `LC_ALL=LANG=nl_NL.UTF-8`, `setText("1.23456789")` gives `toDouble_ok=0 hasAcceptable=0` while `setText(QLocale().toString(1.23456789,'f',8))` gives `toDouble_ok=1 hasAcceptable=1`. That is the whole failure: the validator's decimal count is a red herring for blends (raised to 17 at `src/CadViewport.cpp:307`), the locale is not.

Phase 10 probes (`/tmp/pr79p10/probe*.cpp`, OCCT 7.9.3, fixture = 163×80×60 box cut by a cylinder r=17 → 7 faces, 15 edges, 42 subshapes, live BIN 5690 bytes):

| Probe | Result |
| --- | --- |
| Drift attribution | `GProp` alone: no drift. `BRepMesh_IncrementalMesh(part,0.1,False,0.5,True)`: drift, `firstDiffByte=4555`, **total differing bytes=2**. Extrema, classifier and a pcurve census add nothing further. Subshape flag census: before mesh `shapes=42 free=1 modified=42 checked=2`, after mesh `checked=0`. |
| Snapshot stability | `snapshot bytes=5714/5714 stable=1`; `post-mesh snapshot pair stable=1` (stable within one shape state); copy stream is +24 bytes vs live |
| Genuine-edit sensitivity | placement (5 mm), dimension (box 164) and radius (r=18) all `detected=1` |
| Flag sensitivity / order | `Free flag sensitivity=1`; `Modified(true)`→`Checked(true)` gives `M=1 C=1`; `Checked(false)`→`Modified(true)` gives `M=1 C=0`; `prod order equals reverse order=1`; `M/C pair carried=1` |
| `Names` argument | `names affect BIN stream=0 (5714/5714)` — `BRepBuilderAPI_Copy(part)` and `(part,true,false)` are byte-identical here |
| probe2 — subshape flags | Source flags identical before/after mesh (`F=1 L=0 M=1 C=0 O=0 Cl=0 I=0 V=0`), raw copy inherits them, but `SNAPSHOT changed by mesh=1` → the top-level re-carry does **not** neutralize subshape-level flag drift |
| probe4 — re-carry redundancy | `recarry_changes_stream=0` for a solid box, open wire, vertex, meshed solid and all four `Modified`/`Checked` combos → the copy already propagates root `Modified`/`Checked` |
| **probe6 — resolves the conflict** | Reproduced the box-provisional scenario with `TopoDS_Builder::Add` into a compound: `after Add: part Free=0`, `live stream changed by Add=1`, **`copy WITHOUT recarry changed by Add=0`**, **`SNAPSHOT with recarry changed by Add=1`**; `poke Free(false)` gives `copy Free=1`. So `BRepBuilderAPI_Copy` resets `Free` and the re-carry is load-bearing exactly for that flag |
| probe5 — performance | Heavy part: 400×300×60 box cut by 24 cylinders → 366 subshapes, live BIN 49603 bytes; live serialize median 0.2417 ms / max 0.2666 ms; snapshot median 0.9246 ms / max 0.9695 ms (~3.8× live); 46 `moveSourceUnchanged`-style pairs (92 snapshots) = **84.82 ms**. Light fixture: 46 snapshots median 0.10 ms, 46 pairs 9.2–9.6 ms |
| Teardown crash | All probe binaries exit 139 (SIGSEGV at teardown) on the all-`libTK*.so` link line; `mini.cpp` exits 0 on a minimal link. Link-line artifact, not a code defect |

Compile line used: `/usr/bin/c++ -O2 -std=gnu++20 -isystem /usr/include/opencascade X.cpp -o X -L/usr/lib64 $LIBS` with `LIBS=$(ls /usr/lib64/libTK*.so | grep -vE 'TKOpenGl|TKIVtk|TKViewerTest|TKOpenGlTest|TKTopTest|TKDraw|TKQADraw|TKToubleshooting' | sed 's#.*/lib##; s#\.so##; s#^#-l#' | tr '\n' ' ')`. API gotchas found on 7.9.3: no `TopExp::Num`; `BRep_Tool::CurveOnSurface` needs the 4-arg form; `BRepGProp::VolumeProperties` needs an lvalue `GProp_GProps`; `BRepMesh_IncrementalMesh` angular deflection 0 is invalid (use 0.5); `BRepPrimAPI_MakeCylinder` needs `gp_Ax2`; `TopoDS::Compound` needs a `TopoDS_Compound&` from `MakeCompound`.

## 7. Remaining problems and known limitations

- **Full suite is green locally (Phase 10, verified).** 144 registered tests, **144 PASS / 0 FAIL / 0 SKIP / 0 TIMEOUT** in both the `nl_NL` run (1224.08 s) and the `LC_ALL=C` run (1138.30 s). No failure, crash or timeout exists to attribute, so requirement 11 has no cases.
- **Residual risk, not a defect: subshape flag churn can still read as a source change (Phase 10, probe2).** `BRepMesh_IncrementalMesh` clears `Checked` on subshapes of the live Part, and `movePartSnapshot()` then produces a different stream even though the source geometry is unchanged. `moveSourceUnchanged()` snapshots twice (`src/MoveFace.cpp:96` and `:98`); if tessellation ever runs between those two calls, the guard reports a false "Move Face source or selection changed" and refuses a move. No registered test exercises that interleaving, so it is **unproven as a live defect** and was deliberately not "fixed" — a fix would need to normalize subshape flags in the snapshot, which is a production change outside this validation task.
- **Do not remove the seven-flag re-carry in `movePartSnapshot()` (Phase 10, probe6).** It looked redundant in probe4 because `BRepBuilderAPI_Copy` inherits `Modified`/`Checked`, but it is **load-bearing for `Free`**: the copy always comes back `Free=true`, so without the re-carry the box-provisional `Add` scenario is invisible to the guard. The `Modified`-before-`Checked` order is also required (`TopoDS_TShape::Modified(true)` clears `Checked`), and that ordering is fragile — a future reorder silently drops `Checked`.
- **CI timeout gap (Phase 10).** 27 of the 144 registered tests declare no `TIMEOUT`, and `.github/workflows/build.yml:25` passes no `--timeout`, so those tests are unbounded in CI. Locally they were bounded with `--timeout 300`. The 27 are: `Chamfer-geometry`, `Fillet-geometry`, `brep-support-topology`, `command-panel-positioning`, `command-panel-positioning-hidpi`, `construction-geometry`, `profile-extrusion-sessions`, `profile-extrusion-sessions-hidpi`, `reference-geometry`, `selection-contours`, and 17 `sketch-*` names (`sketch-circular-projection`, `sketch-closed-profile-validation`, `sketch-construction-geometry`, `sketch-construction-points`, `sketch-cylinder-silhouettes`, `sketch-display-bounds`, `sketch-ellipse-foundation`, `sketch-ellipse-snapping`, `sketch-elliptical-projection`, `sketch-face-projection`, `sketch-overdraw-geometry`, `sketch-profile-extrusion`, `sketch-profile-revolution`, `sketch-profile-wire-face-construction`, `sketch-projection-geometry`, `sketch-snapping`, `sketch-spline-geometry` — note the *session* sketch tests such as `sketch-trim-sessions` **do** have a timeout). Adding `set_tests_properties(... TIMEOUT ...)` for those names is a test-registry change and was out of scope here; it is the recommended next hardening step.
- **OCCT 7.6.3 compilation of the Phase 7 code is unverified locally (Phase 10).** Only 7.9.3 headers exist on this machine and no web/fetch connector was available. The inference that the seven flag setters compile on 7.6.3 rests on `tests/NativeDocumentTest.cpp:56-57` already using the same accessors at HEAD, which CI compiles. **Resolved by CI (Phase 11, verified):** the Build step on `c50f054` succeeded (1314 s) against the `libocct-*-7.6t64` packages installed by the Dependencies step, so the seven flag setters do compile on OCCT 7.6.3. The local 7.9.3 probes remain 7.9.3 numbers. `gh` is installed at `/usr/bin/gh` and the GitHub API is reachable from the session shell, so CI status can be read with `gh pr checks` / `gh run`.
- **CI is green on the Phase 11 SHA (Phase 11, verified from the Actions log after the push).** Actions run `45063550829`, workflow `Build`, single job `linux`, `headSha c50f054e7cdc423cab4f6e5a87d8100d59ed59a1`: `status=completed`, `conclusion=success`, wall clock 19:56:18Z → 20:48:04Z (51 m 46 s). Steps: Dependencies 26 s, Configure 2 s, **Build 1314 s success**, **CTests (X11 and software OpenGL) 1757 s success**, artifact upload 1 s. CTest summary line: `100% tests passed, 0 tests failed out of 142`, `Total Test time (real) = 1754.68 sec`. This is the first execution of the OCCT 7.6.3 check for the Phase 7 flag setters, and it passed. Note the CI count is **142**, not the 144 registered locally, and the difference is fully explained by two conditional registrations, not by a missing test: `command-panel-cross-screen` is inside `if(Qt6Widgets_VERSION VERSION_GREATER_EQUAL 6.11)` (`CMakeLists.txt:210-216`, CI Qt is 6.4.2) and `experimental-whole-section-lines` is only registered when Boost.Multiprecision headers are found (`CMakeLists.txt:136-139`, not installed in the CI dependency list). Verified by diffing `ctest -N -C Release` names against the CI log names: local-only = exactly those two, CI-only = none.
- **The committed snapshot of this file cannot contain its own CI result.** The handover is part of commit `c50f054`, so the Actions outcome for that SHA was only observable after the push. That is why Phase 11b exists: it shipped the CI outcome in a follow-up **docs-only commit** on the same branch. A fresh session reading the PR branch now sees the green result; a session reading `c50f054` itself does not.
- **Local OCCT is 7.9.3, CI is 7.6.3.** Every probe result in section 6 was measured on 7.9.3. Flag-propagation behaviour of `BRepBuilderAPI_Copy` and `BRep_Builder::Add` is stable across these versions in the code read here, but the probe numbers (byte counts, timings) are 7.9.3 numbers.
- **No `OpenDesignerMoveFaceTests` failure remains** (Phase 8): the 23 registered names are **23 PASS / 0 FAIL**. `sloped-planar-opening-move` (ctest #65) was closed by Phase 8; `cylindrical-move-face-boundary` was closed by Phase 7. The only still-failing test known locally is `edge-blend-sessions` in a different executable.
- **Resolved hypothesis (Phase 7).** The Phase 6 note that `cylindrical-move-face-boundary` "uses `cylinderBossBoundary_`/Trim paths and needs its own investigation" was **partly wrong**: the reported failure was the same 300 ms planar numeric debounce at four call sites in `boundaryRegression()`. The truncated message with an empty `geometryError` was the tell — a real geometry rejection would have filled it in. Investigating the *deferred* path was still necessary: once the await was added, a second, genuine production defect appeared.
- **Resolved hypothesis (Phase 6, still current).** The Phase 5 claim that `cylindrical-move-face-recognition` was "not a plain debounce call site" was an assumption and is **withdrawn** — it was the same 300 ms planar numeric debounce at one call site, now fixed. Lesson for the open failures: check the debounce gate before assuming a geometric defect, but prove it with a state dump rather than by pattern-matching.
- **Resolved hypothesis (Phase 5, still current).** The Phase 4 lead that `move-face-crash03112026` shared the debounce pattern is **confirmed** and fixed. The same latent harness race still applies to any future test that types into a planar face and asserts `commandSession_->valid` or reads `commandSession_->candidate` without pumping the event loop; Phases 4/5/6/7 added per-file calls rather than refactoring the shared harness.
- **New production finding (Phase 7, verified).** `CadViewport::movePartSnapshot()` used to serialize the live Part shape, and OCCT mutates that byte signature during a session by attaching Curve2ds/Locations caches to the shared `TShape`. Any future code that (a) serializes a live shape for change detection, or (b) compares `BinTools` output across a preview/display cycle, inherits this defect. The canonical-copy approach is the fix; do not "fix" it by comparing `IsSame` or by loosening tolerances.
- **New cost note (Phase 7).** Every Move Face snapshot now copies the shape: `SESSION_IDENTITY calls=46 median_ms` 0.033 → 0.134 (max 0.114 → 0.385). Sub-millisecond per call, but a future Part with a much larger B-Rep should be re-measured before raising snapshot frequency.
- **Closed (Phase 9).** The `edge-blend-sessions` failure recorded in 3.8 is fixed. Root cause was **not** a numeric-input or `updateCommandParameter` production defect as 3.8 guessed: it was five hardcoded decimal literals in `tests/EdgeBlendSessionTest.cpp` that are invalid in any locale whose decimal separator is not `.`. The session shell is `LANG=nl_NL.UTF-8`, where `QLocale().toDouble("1.23456789")` fails because `.` is the group separator. Lesson for future failures in this repo: **run the same binary under `LC_ALL=C` before assuming a geometry or timing defect** — a locale flip that changes the result identifies a harness literal, not production.
- **New harness rule established (Phase 9).** Any test that types a decimal number into `dragDimension_` (or any `QLineEdit` validated by `PullDimensionValidator`) must build the string with `QLocale().toString(value,'f',N)`, never a `"d.ddd"` literal. Production reads these fields with `QLocale().toDouble(dimensionNumber(...))` and the validator inherits the system locale, so a fixed-locale literal is a latent failure on any non-`.` desktop (the developer's own Fedora KDE shell is `nl_NL`). Integer literals are locale-invariant and safe. Remaining risk: the rule is not enforced by any lint, so a future test can reintroduce it.
- **Locale-sensitive sites elsewhere (not changed).** `tests/CadViewportPullTest.cpp:409` and `tests/MoveFaceTest.cpp:2609` call `QLocale::setDefault(...)` deliberately — English/US and Dutch/Netherlands respectively — so those files already assume control of the locale. No other registered test failed under `nl_NL` in the 52-test family run, so no further literals were found in `OpenDesignerSelectionContextTests`. Executables outside that family were not re-run under `nl_NL` in this task, so a similar literal elsewhere would still be latent.
- **Latent harness hazard (3.8, not changed).** `WorkplaneReferenceSessionTest::acceptPopups()` (`:286`, `:299`, `:559`) and `EdgeBlendSessionTest.cpp:202` install `QTimer::singleShot(0, …)` handlers that click **any** open `QMessageBox`. If an unsaved-changes prompt were ever live in those regions, those handlers would answer it — with the default **Save**, which then opens a modal `QFileDialog`. No prompt is live there today (those tests either keep the document clean or never close it), so nothing was changed. A future hardening step is to give those handlers the same button-set signature check the guard uses.
- **Resolved hypothesis (Phase 8, withdrawn Phase 7 note).** The Phase 7 claim that `sloped-planar-opening-move` "drives `MoveFaceGeometry` directly and does **not** fit the debounce pattern" was **wrong**. It was the same 300 ms planar numeric debounce at one further call site: the `start` lambda of `slopedPlane()`, `tests/CylindricalMoveFaceSessionTest.cpp:315`. The empty `geometryError` in the message was again the tell. Lesson reinforced: the tell is *which* assertion fails and what the state dump shows, not which helper the test happens to call.
- **Diagnosability gap still open (Phase 8, deliberately not fixed).** `check(v.commandSession_->valid, qPrintable(v.commandSession_->geometryError))` yields an **empty** `what()` whenever the session is merely pending, so the harness exits 1 with a single newline. Changing that message would have changed an assertion, which the task forbade. A future hardening step is to give `check` a fallback message when the message argument is empty (see section 8).
- The intermittency recorded in `docs/FAST_DIRECT_MODELING_CERTIFICATION.md` F2.9G.4 ("resumed drag failed", ~3 of 10 runs under `kde-inhibit`) did **not** reproduce in the four post-fix Xvfb runs. That is a small sample, not a fix; it is consistent with the F2.9F.1 finding that the real blocker in those runs was the unanswered close prompt.
- **Superseded (3.8).** The Phase 3 note "the harness answers the prompt rather than asserting on it" no longer applies: `UnsavedPromptGuard` counts what it answers, all three sites assert `prompts.answered == 1`, and an unrecognised modal box is reported on stderr with `refused++` instead of being swallowed. If production changes the prompt's buttons, the guard refuses, prints the box text once, and the test fails as a diagnosable timeout — not a silent pass.
- Compound children in the Phase 2 fixture are no longer identity-equal (`IsSame`/`IsEqual`) to the caller's shapes. All current call sites match geometrically and pass, but a future fixture relying on child identity must know this.
- Certification test runtime rose 0.15 s → 0.35 s from the extra copies; far inside its 60 s timeout.
- No GUI, KDE/Wayland or HiDPI **visual** acceptance was performed; `cylindrical-move-face-sessions-hidpi` passes as a headless Xvfb test only.
- PR #79 CI reported 135 of 142 passing with six failures and one timeout. **Seven** names are now fixed locally: `brep-certification-correspondence`, `move-face-drag-initialization`, `cylindrical-open-notch-sessions`, `move-face-crash03112026`, `cylindrical-move-face-recognition`, `cylindrical-move-face-boundary` (Phases 2–7) and `sloped-planar-opening-move` (Phase 8). The other CI names were **not** transcribed from the Actions log in any session; `edge-blend-sessions` is the only still-reproducing local failure, and the mapping to the CI log remains an assumption.

## 8. Next recommended development step

1. **The full-suite verification recommended by Phase 9 is done (Phase 10, verified).** All 144 registered tests pass under Xvfb in both the session `nl_NL` locale and under `LC_ALL=C`, 0 FAIL / 0 SKIP / 0 TIMEOUT. The worktree at `14f2d5c` plus the current uncommitted diff is therefore **ready to commit and ready for GitHub CI**: the production change in `src/MoveFace.cpp` is linked into several test executables and none of them regressed. Two caveats to carry into the commit message or PR description: the OCCT 7.6.3 compilation of the new flag setters is confirmed only by CI, and the subshape-flag churn in 7 is a known residual risk, not a fixed defect.
   **Phase 11 supersedes the "ready for CI" framing: the commit is pushed and CI is green on `c50f054`** (run `45063550829`, job `linux`, 142/142). **Phase 11b closed the remaining documentation loop**: the CI outcome is now committed on the PR branch, so there is no validation or documentation work left on this diff. The next step is the hardening list in point 2 — starting with (a) registered `TIMEOUT` properties for the 27 tests that lack one, since CI still passes no `--timeout` — and, if the residual subshape-flag risk is to be closed, (b). Merging PR #79 remains a human decision and was explicitly out of scope.
2. **Recommended hardening, in priority order, each a separate task:** (a) give the 27 tests with no `TIMEOUT` property a registered timeout so CI cannot hang on them (`.github/workflows/build.yml:25` passes no `--timeout`); (b) make the snapshot immune to subshape flag churn, or make `moveSourceUnchanged()` compare geometry rather than a byte stream, so tessellation between the two snapshots cannot produce a false "source changed"; (c) promote `awaitNumericPreview` to a shared helper in `tests/CommandPanelTestHelpers.h` — five phases have hit this race, and `tests/MainWindowTestHelpers.h` is the pattern to follow.
2b. Make the harness failure diagnosable: `check` at `tests/CylindricalMoveFaceSessionTest.cpp:61` throws `std::runtime_error(message)` and the dispatcher prints only `error.what()`, so an assertion whose message is `qPrintable(geometryError)` exits 1 with a single newline. Give `check` a fallback message naming the file, line and session state when the message argument is empty. This is a harness-only change and was out of scope for Phase 8 because it edits an assertion.
2c. Harden the two `QTimer::singleShot(0, …)` popup handlers in `WorkplaneReferenceSessionTest::acceptPopups()` and `EdgeBlendSessionTest.cpp:202` with the same button-set signature check `UnsavedPromptGuard` uses, so they can never answer a Save/Discard/Cancel prompt with the default Save.
3. Consider (only if the user approves) promoting `awaitNumericPreview` to a shared helper in `tests/CommandPanelTestHelpers.h` — it now exists in `tests/CylindricalMoveFaceSessionTest.cpp` and a near-identical `awaitNumeric` in `tests/MoveFaceTest.cpp`, and **five** phases have hit this race. The save-prompt harness has already been centralised this way (`tests/MainWindowTestHelpers.h`), so follow that pattern.
3b. Make the harness failure diagnosable: `check` at `tests/CylindricalMoveFaceSessionTest.cpp:61` throws `std::runtime_error(message)` and the dispatcher prints only `error.what()`, so an assertion whose message is `qPrintable(geometryError)` exits 1 with a single newline. Give `check` a fallback message naming the file, line and session state when the message argument is empty. This is a harness-only change and was out of scope for Phase 8 because it edits an assertion.
3c. Harden the two `QTimer::singleShot(0, …)` popup handlers in `WorkplaneReferenceSessionTest::acceptPopups()` and `EdgeBlendSessionTest.cpp:202` with the same button-set signature check `UnsavedPromptGuard` uses, so they can never answer a Save/Discard/Cancel prompt with the default Save.
4. **Done in Phase 11 (verified).** `src/MoveFace.cpp`, `tests/BRepCertificationTest.cpp`, `tests/MoveFaceTest.cpp`, `tests/CylindricalMoveFaceSessionTest.cpp`, `tests/EdgeBlendSessionTest.cpp`, `tests/ConstructionSessionTest.cpp`, `tests/MainWindowTestHelpers.h`, `AGENTS.md`, `CLAUDE.md`, `docs/LOCAL_AI_TASK_TIMES.csv` and this document were committed as one logical change on `ai/vibe-moveface-review` and pushed fast-forward to `feature/v0.5.7-shared-brep-reconstruction` (PR #79 head). No merge, no force-push, no version bump. The next session should work from the pushed SHA, not from `14f2d5c`.
5. From the next session on, every task follows the task-timing rule in section 11: capture the start timestamp before any other work, capture the end timestamp after validation, and append one row to `docs/LOCAL_AI_TASK_TIMES.csv`.

## 9. Environment facts a fresh session needs

| Item | Value |
| --- | --- |
| Worktree | `/home/Jeroen/OpenDesigner-vibe` |
| OCCT | 7.9.3 (headers `/usr/include/opencascade`, libs `/usr/lib64/libTK*.so.7.9.3`) |
| Qt | 6.11.2 (`Qt6::Gui`); `QT_QPA_PLATFORM=Offscreen` for headless, X11 backend `libqxcb.so` present; `tests/MoveFaceTest.cpp:2620` forces `QT_QPA_PLATFORM=xcb` on Linux when `DISPLAY` is set |
| X server for tests | `Xvfb` and `xvfb-run` **are installed**. Correct invocation: `xvfb-run -a --server-args="-screen 0 1600x1200x24" <cmd>`. `-g WxHxD` is **invalid** for `Xvfb` and makes the server fail to start, which shows up as a Qt `init_platform` SIGABRT with no message |
| Test registration | `cylindrical-open-notch-sessions` → `OpenDesignerMoveFaceTests --open-notch`, `CMakeLists.txt:325-326` (`TIMEOUT 120 RUN_SERIAL TRUE`). Do not confuse it with `cylindrical-open-notch-geometry` → `OpenDesignerCylindricalMoveTests --notch` (`CMakeLists.txt:152-153`), which passes |
| Debugging a hung test | `gdb -batch -ex "set debuginfod enabled off" -p <pid> -ex "thread apply all bt" <binary>`; find the pid with `pgrep -af "<binary> --<mode>"` and skip the `timeout`/`xvfb-run` wrappers |
| Compiler | GCC 16.2.1, `/usr/bin/c++`, `CMAKE_CXX_STANDARD=20` in `CMakeLists.txt` (not 17) |
| Build | CMake 4.3.0 + Ninja 1.13.2, `CMAKE_BUILD_TYPE=Release`; `OPENDESIGNER_BUILD_TESTS=ON` required (`CMakeLists.txt:98`) |
| CI | `.github/workflows/build.yml:18` sets the same option; line 25 runs `xvfb-run … ctest --test-dir build -C Release --output-on-failure -j2` |
| Probe compile flags | `cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` then `build/compile_commands.json`, or `ninja -t commands <target> \| tail -1` |
| Locale | The Vibe session shell is `LANG=nl_NL.UTF-8`, `LC_NUMERIC=nl_NL.UTF-8` — decimal separator `,`, group separator `.`. `QLocale()` in Qt inherits it, so `QLocale().toDouble("1.23456789")` returns 0 and `QDoubleValidator::validate("1.23456789")` returns `No`. CI (`build.yml:25`) runs under the default `C`/`POSIX` locale, so a locale-dependent test failure does **not** reproduce in CI. Check `env | grep -iE '^(LANG\|LC_)'` before debugging any numeric-input test, and re-run under `LC_ALL=C` as the first differential |

## 10. Restrictions carried forward

- No changes outside this worktree.
- No commits, pushes or merges without explicit user approval.
- No weakening of tests, assertions, tolerances or timeouts; the strict BRep serialization comparison and the 240 s drag timeout stay as they were.
- `AGENTS.md` Mandatory Technical Handover applies: update this file before the final report of every development task.
- `AGENTS.md` Mandatory task timing applies: record start and end per task and append one row to `docs/LOCAL_AI_TASK_TIMES.csv`. No estimated or invented historical timings.

## 11. Task timing workflow (added this session)

`AGENTS.md` and `CLAUDE.md` now carry a **Mandatory task timing** rule: every future
development task is timed and appended to `docs/LOCAL_AI_TASK_TIMES.csv`. The rule adds
measurement only — it changes no build flag, test selection, tolerance or timeout, and
it does not replace the Reporting or Handover rules.

Workflow a fresh session follows:

1. Capture the **start** timestamp as the first action of the task, before any other
   tool call or file change — `run_typescript` with `new Date().toISOString()`. Keep
   the value in the session context so it survives context compaction.
2. Implement and validate as before (incremental build, directly relevant tests).
3. Capture the **end** timestamp after implementation and validation, immediately
   before writing the row.
4. Compute `duration_seconds` as the wall-clock difference `end - start`.
5. Append exactly one row to `docs/LOCAL_AI_TASK_TIMES.csv`. Append-only: never
   rewrite, reorder, deduplicate or delete existing rows, and never add a row for a
   task that was not timed in its own session.

Header, in this order:
`task_id,task_title,start_utc,end_utc,duration_seconds,result,build_seconds,test_seconds,context_compactions,notes`.

| Field | Rule |
| --- | --- |
| `start_utc`, `end_utc` | ISO-8601 UTC with milliseconds, recorded inside the task's own session |
| `duration_seconds` | `end - start` in seconds; never a subjective estimate |
| `result` | `pass`, `fail`, `blocked` or `not measured` |
| `build_seconds`, `test_seconds` | measured duration of the build and test commands run for that task, not a sum of everything that ran |
| `context_compactions` | number of context compactions observed during the task, when the session reports it |
| any uncaptured value | the literal string `not measured` |

Hard constraint: **no estimated or invented historical timings.** A task whose start
was never recorded keeps `not measured` for `start_utc`, `end_utc` and
`duration_seconds`; failed and blocked tasks still get a row, with `result` set
accordingly.

First row, measured in this session: `docs-task-timing-instructions`,
`2026-11-21T22:03:37.038Z` → `2026-11-21T22:07:28.799Z`, **231.761 s**, result
`pass`, `build_seconds` and `test_seconds` `not measured` (docs-only change, no
build or test executed), `context_compactions` 0. `git diff --check` was run as the
validation step and reported no whitespace or conflict-marker errors.

Phase 5 row, measured in its own session: `pr79-phase5-move-face-crash03112026`,
`2026-11-21T22:13:49.866Z` → `2026-11-21T22:27:34.570Z`, **824.704 s**, result
`pass`, `build_seconds` 14.4 (two incremental builds: 7.21 s diagnostic + 7.19 s
fix), `test_seconds` 235.6 (repro 6.46 + post-fix direct 9.30 + ctest #68 6.36 +
23-test family 210.54 + two rechecks 2.97), `context_compactions` 0.

Phase 6 row, measured in its own session: `pr79-phase6-cylindrical-move-face-recognition`,
`2026-11-22T02:17:46.335Z` → `2026-11-22T02:39:43.645Z`, **1317.310 s**, result
`pass`, `build_seconds` 17.6 (three timed incremental builds: 7.26 s diagnostic +
3.21 s failed-compile + 7.17 s fix; one intermediate diagnostic build was not
timed), `test_seconds` 224.4 (pre-fix repro 10.08 + ctest #70 7.49 + 23-test
family 203.86 + two rechecks 2.96; the three post-fix direct runs and the
diagnostic runs were not timed separately), `context_compactions` 0.

Phase 8 row, measured in its own session: `pr79-phase8-sloped-planar-opening-move`,
`2026-11-22T12:23:55.319Z` → `2026-11-22T13:07:09.868Z`, **2594.549 s**, result
`pass`, `build_seconds` 21.9 (three timed incremental builds: 7.251 s macro
diagnostic + 7.400 s state-dump diagnostic + 7.240 s fix), `test_seconds` 562.6
(pre-fix repro 17.407 + two diagnostic runs 17.271 and 24.871 + three post-fix
direct runs 25.04 / 24.61 / 24.70 + ctest single test 25.26 + 23-test family
315.19 + 7-named-test run 87.53 + `edge-blend-sessions` recheck 0.68),
`context_compactions` 1.

Phase 10 row, measured in its own session: `pr79-phase10-full-regression-validation`,
`2026-11-22T17:09:24.686Z` → `2026-11-22T18:15:31.428Z`, **3966.742 s**, result
`pass`, `build_seconds` 70.88 (one timed build of all configured targets),
`test_seconds` 2362.38 (the two full-suite runs: 1224.08 s in `nl_NL` + 1138.30 s
under `LC_ALL=C`), `context_compactions` 1. Validation only: no source file was
changed, so the row's `notes` carry the whole result set.

Phase 11 row, measured in its own session: `pr79-phase11-commit-and-ci-validation`,
`2026-11-22T19:40:03.891Z` → `2026-11-22T19:52:31.930Z`, **748.039 s**, result
`pass`, `build_seconds` 0.03 (one incremental build check, `ninja: no work to do`),
`test_seconds` `not measured` — no test command ran in this phase because no source
file changed after the Phase 10 runs; only `CLAUDE.md` and the two handover files
changed. GitHub Actions ran the full suite on the pushed SHA; its wall-clock duration
is outside this row because the end timestamp was recorded immediately before the
row was written, and the handover is part of the same commit that had to be pushed
to trigger CI. CI itself is measured separately and is green: run `45063550829`,
job `linux`, 51 m 46 s wall clock, Build 1314 s, CTests 1757 s, `100% tests passed,
0 tests failed out of 142`. The CI wall clock is **not** written into the CSV row's
`test_seconds` field, because that field is defined as the duration of a test command
run for the task in this session, and no such command ran.

Phase 11b row, measured in its own session: `pr79-phase11b-ci-handover-documentation`,
`2026-11-22T20:59:44.985Z` → `2026-11-22T21:06:53.379Z`, **428.394 s**, result
`pass`, `build_seconds` and `test_seconds` both `not measured` — the diff is two
documentation files, so no build and no test command was run, and `context_compactions`
0. The CI durations quoted in 3.13 (Build 1314 s, CTest 1757 s) are **GitHub Actions**
durations read from the Actions log and are deliberately not entered in either timing
field.

**Measurement honesty for the Phase 11b row.** Its `end_utc` was captured when the
documentation content and its validation were complete, immediately before the row was
written, as the rule requires. Two documentation-only commits resulted from the task:
`64b7e7e` carried the row itself, and a later commit added the "which SHA the CI evidence
belongs to" bullet. The extra wall clock after `end_utc` is **not** back-filled into
`duration_seconds` — the CSV is append-only and the recorded interval is the one that was
actually measured.

## 12. Documentation synchronization after PR #79 (2026-11-23)

### 12.1 Git and working-tree status (verified in this session)

| Item | Value |
| --- | --- |
| Worktree | `/home/Jeroen/OpenDesigner-develop` |
| Branch | `docs/synchronize-after-pr-79`, created from `origin/develop` |
| Baseline HEAD | `fb370e5bc7fb0c83064a35e8e03176280504f260` — PR #79 squash merge, single parent `98ed414`, `mergedAt 2026-11-23T12:11:51Z` |
| PR #79 head | `63ff9a6b35188af14d709015bdb1d7791261dc74` (`gh pr view 79 --json headRefOid`) |
| Working tree at review time | 13 modified documentation files, +252 / −58; no `src/`, `tests/`, `CMakeLists.txt` or `.github/` change |
| `git diff --check` | exit 0 — no whitespace errors or conflict markers |

### 12.2 Task objective and completion status

Documentation only: remove stale version statements, eliminate contradictions between the
project-level instruction files and the docs, describe the shared B-Rep reconstruction
architecture delivered by PR #79, keep every still-open v0.5.7/v0.5.8 roadmap item unchecked,
and check tolerances, test rules and CI statements for mutual consistency. **Complete** as a
docs change; no code, build or test work was in scope.

### 12.3 Contradictions found and resolved

| File | Stale or contradictory claim | Resolution |
| --- | --- | --- |
| `AGENTS.md` / `CLAUDE.md` | "v0.5.2 (current), branch `feature/v0.5.2-conical-edge-blends`" | Replaced with `develop` HEAD `fb370e5`, the v0.5.0-v0.5.6 PR list, v0.5.7 merged-but-unreleased, v0.5.8 planned |
| `README.md` | Status line at v0.5.5; "Final build/test results ... v0.4.9 integration review" | v0.5.6 last release + v0.5.7 merged-unreleased; the "final results" pointer now points at `docs/LOCAL_AI_PROGRESS.md` |
| `docs/LOCAL_AI_HANDOVER.md` | Snapshot `2026-11-07 / develop v0.4.9`; v0.4.9 build graph and reading order; PR #68 merge state listed as unknown | Snapshot `2026-11-23 / fb370e5`; reading order and module list updated; PR #68 recorded as merged (`60d4bccc`); timeline extended to v0.5.7 |
| `docs/LOCAL_AI_TECHNICAL_REFERENCE.md` | v0.4.9 snapshot presented as current; PR #68 marked **VERIFY** | Kept as a v0.4.9-anchored annex with an explicit anchoring note; PR #68 marked resolved |
| `docs/MISTRAL_VIBE_START.md`, `docs/LOCAL_AI_ENGINEERING_OPERATIONS.md` | "Initial assignment"/"Priority 0" still aimed at v0.4.9 and PR #68 reconciliation | Rewritten to the current baseline: the `rotation-pivot` CI timeout and the open v0.5.7 acceptance gates |
| `docs/NATIVE_DOCUMENTS.md`, `docs/SKETCH_EDITOR.md`, `docs/SKETCH_PROJECTION.md`, `docs/WORKPLANE_REFERENCES.md` | "Current v0.5.5 scope and limits" | Version-neutral pointer listing v0.5.5, v0.5.6 and v0.5.7 scope documents |
| `docs/ROADMAP.md` | No record of the merged PR #79 state; CI counts unexplained | New `### Merged state on develop (PR #79, 2026-11-23)` section with delivered items, CI evidence and the test-registry reconciliation |

Feature documents that state their own milestone baseline (`docs/CROSS_SCREEN_HIDPI.md`,
`docs/DELETE_FACE.md`, `docs/MOVE_FACE.md`, `docs/ADVANCED_SKETCH_CURVES.md`) were left
untouched: those are historical scope records, not current-status claims.

### 12.4 Technical decisions

- Version truth source: `project(OpenDesigner VERSION 0.5.6)` in `CMakeLists.txt` plus the
  `git log` PR annotations. `AGENTS.md` remains canonical and `CLAUDE.md` is kept byte-identical
  except for its title/intro, as the standing rule requires.
- The shared B-Rep architecture is documented by reference to the existing implementations
  (`BRepTopology`, `BRepCertification`, `BRepMeasurement`, `BRepInvariantSupport`,
  `BRepInvariantExtrusion`, `BRepTiming`, the provisional headers) and to
  `docs/SHARED_BREP_RECONSTRUCTION.md`; no new parallel description was invented.
- `docs/LOCAL_AI_TECHNICAL_REFERENCE.md` was annotated rather than rewritten: its OCCT/Qt
  call-level notes are still useful, only its status claims needed a boundary.
- `.github/workflows/build.yml` was **not** edited (CI configuration is outside this task),
  even though its artifact `README.txt` still says "OpenDesigner v0.5.6 ... Linux prototype"
  (line 31). Reported as an open item instead.

### 12.5 Commands executed in this session (verified)

```sh
git status --short --branch
git log --oneline -20 origin/develop
git log -1 --format=%B fb370e5
git log -1 --format="%H %ci %s" 60d4bcc
git diff --check            # exit 0
git diff --stat             # 13 files, +252 / -58
gh pr view 79 --json headRefOid,mergeCommit,mergedAt,state
gh pr checks 79             # linux pass 54m14s, run 45071533024
gh run view 45071533024 --log | grep -E "tests passed|rotation-pivot"
                              # 100% tests passed, 0 tests failed out of 142; #51 Passed 58.35 sec
gh run view 45172009344 --log-failed | grep -E "rotation-pivot|tests passed"
                              # 99% tests passed, 1 tests failed out of 142; #51 ***Timeout 60.03 sec
grep -c "add_test(" CMakeLists.txt          # 144
perl -0777 ... CMakeLists.txt               # 145 names, 118 with TIMEOUT, 27 without
```

**Not executed:** no `cmake` configure, no build, no `ctest`. `build_seconds` and
`test_seconds` in the CSV row are therefore `not measured`. The 144/142/145 figures above
come from static `CMakeLists.txt` analysis plus the two Actions logs, not from a local run.

### 12.6 Findings a fresh session needs

- The `develop` CI failure is a timing flake, proven by `git diff --name-only fb370e5 63ff9a6`
  being empty plus the 58.35 s / 60.03 s pair. Do not "fix" it by relaxing the assertion.
- Test registry reconciled: 144 `add_test(` calls, `foreach(blend Fillet Chamfer)` adds a
  second name (`Fillet-geometry`, `Chamfer-geometry`), so 145 names exist. Three are
  configure-time gated out of CI: `command-panel-cross-screen` (Qt >= 6.11, line 211),
  `experimental-whole-section-lines` (Boost.Multiprecision, line 130) and
  `move-face-box-shadow-observation` (`OPENDESIGNER_ENABLE_BOX_SHADOW`, default `OFF`, line
  550). CI therefore runs 142. This reconciles with the Phase 10 inventory taken from
  `build/CTestTestfile.cmake` (144 registered, 117 with `TIMEOUT`, 27 without): that local
  configuration had the Box observer test off and both version-gated tests otherwise present.
- 118 of the 145 names set `TIMEOUT`; 27 do not and inherit CTest's 1500 s default.
  `rotation-pivot`'s 60 s ceiling comes from the shared `set_tests_properties` list at
  `CMakeLists.txt:295`.
- Tolerances are mutually consistent between code and docs: `1.e-6` reconstruction/topology
  ceiling (src/BRepInvariantSupport.cpp:122, src/BRepCertification.cpp:123), `1e-7` Sketch
  (src/SketchSpline.cpp:42), `1e-8` relative coverage (src/BRepSweepClearance.h:32),
  `1e-12` adaptive integration (src/BRepMeasurement.cpp:103), and the `2e-6` clearance margin
  documented as twice the ceiling.
- The OCCT gate `stackedCylinderMergeSupported` (`OCC_VERSION_HEX >= 0x070900`,
  src/BRepReconstruction.cpp:244) means a locally green seam-merge result is not evidence for
  the CI toolchain and vice versa.

### 12.7 Remaining problems and open uncertainties

- `develop` CI is red on the `rotation-pivot` timeout; unresolved by design here.
- 27 tests have no `TIMEOUT`; whether that is intentional is a test-registry question.
- The CI artifact text still advertises v0.5.6 (`.github/workflows/build.yml:31`).
- F2.9G.5A legacy `.odc` compatibility is untested; Fedora KDE manual acceptance for the
  v0.5.7 pipeline is pending.
- Residual risk from the PR #79 merge message still open: `BRepMesh_IncrementalMesh` clears
  `Checked` on subshapes, so tessellation between the two `moveSourceUnchanged()` snapshots
  can still read as a source change.
- All v0.5.7/v0.5.8 acceptance and release gates in `docs/ROADMAP.md` remain unchecked.

### 12.8 Exact next recommended step

On a code branch (not this docs branch): decide the `rotation-pivot` remedy — re-run CI first,
and only if it reproduces, raise the shared `TIMEOUT 60` group at `CMakeLists.txt:295` or
split `rotation-pivot` into its own registration with a measured budget. Then take the first
open v0.5.7 acceptance gate: legacy `.odc` compatibility for F2.9G.5A on a copy of a private
document opened from a temporary directory.
