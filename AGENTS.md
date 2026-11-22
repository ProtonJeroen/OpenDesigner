# OpenDesigner – standing rules for AI coding agents

`AGENTS.md` is the canonical, vendor-neutral instruction file for this repository. It is loaded
automatically by Mistral Vibe and by other agent CLIs. `CLAUDE.md` carries the same standing rules
for Claude Code; when a standing rule changes, update both files in the same commit so they cannot
drift.

## Project
OpenDesigner is an open-source, cross-platform direct-modeling CAD application in C++20, Qt6 and
Open CASCADE (OCCT), inspired by CoCreate SolidDesigner / OneSpace Modeling.
Repository: ProtonJeroen/OpenDesigner. Primary project reference: `docs/ROADMAP.md` on `develop`.

## Version status
- v0.5.1 is complete: PR #71 merged into `develop` (merge commit `fabd9be9f47ff68eb9ae0928b733a450a3c9db68`).
- v0.5.2 (current): conical Fillet & Chamfer support, branch `feature/v0.5.2-conical-edge-blends`.
- v0.5.3 (next): Workplane, Axis, Point, Sketch and reference foundation (previously planned as v0.5.2).
- Later: exact ellipse/spline curves and projection, Sweep with 3D spline paths, Loft, broader Align
  and Assemblies, packaging. Task-specific instructions come from the prompt of each session.

## Architecture principles (non-negotiable)
- History-independent, geometry-first B-Rep editing. The current B-Rep (faces, edges, vertices,
  adjacency, analytic supports, material occupancy) is the geometric truth. No dependency on
  primitive origin or feature history.
- In-place edits preserve Part UUID, name, colour, visibility, placement and browser position.
- Build candidates on an isolated copy and validate topology, volume, material equivalence and
  numeric tolerances before committing.
- Preview is temporary and non-selectable. OK/Enter commits exactly one Undo step;
  Cancel/Escape commits none. Save writes only confirmed geometry.
- Uncertain or unsupported cases are refused specifically and atomically, without model mutation.
- Keep the 1e-6 sharp-reconstruction tolerance. Never relax tolerances or weaken assertions to make
  tests pass. No uncontrolled shape healing.
- Reuse existing modules (`FilletGeometry`, `EdgeBlendFeatures`, Move/Delete Face geometry,
  `NativeDocument`) rather than introducing parallel engines or persistence formats.

## Git rules
- Never `git clean`, `git reset --hard`, force checkout or force push.
- Never merge PRs, commit directly to `develop`/`main`, or change branch protection.
- Pushing the current feature branch and creating/updating a PR targeting `develop` is allowed
  when the session prompt asks for it.
- Small, logical commits on the feature branch.

## Private files
- Untracked or private `.odc` documents (including `fillet.odc`) must never be modified, moved or
  committed. To use one as a fixture, copy it to a temporary directory and open only the copy.
- Repository tests use synthetic fixtures built in code.

## Build and test limits
- Incremental builds, maximum `-j4`. Preserve existing build directories; no clean rebuild unless
  technically necessary.
- CTest maximum `-j2`. Run only directly relevant tests locally; no full CTest suite locally.
- Do not rerun passing tests unless relevant sources changed.
- CI (Ubuntu 24.04) uses OCCT 7.6.3 and Qt 6.4.2. Newer APIs need version guards or a fallback.

## Reporting
Every report states: branch and SHA; exact build and test commands with PASS/FAIL counts;
tests intentionally not run; changed files; remaining manual (GUI, Fedora KDE/Wayland, HiDPI)
checks; unsupported or safely refused cases. Never claim a test passed that was not run in the
current session.

## Mandatory task timing (all development tasks)

Every development task is timed. Timings are recorded, never reconstructed.

- Record the **start** timestamp as the first action of the task, before any other
  tool call or file change. In a Mistral Vibe session, capture it with `run_typescript`
  (`new Date().toISOString()`) and keep the value in the session context so it survives
  context compaction.
- Record the **end** timestamp after implementation and validation are complete,
  immediately before writing the task's row.
- Duration is the actual wall-clock difference between the two recorded timestamps,
  in seconds. Never derive it from a subjective impression of how long the work took.
- Append exactly one row per completed task to `docs/LOCAL_AI_TASK_TIMES.csv`.
  Append only: never rewrite, reorder or delete existing rows, and never add a row
  for a task that was not timed in its own session.
- Required fields per row: task identifier, start timestamp, end timestamp,
  duration, result, build time, test time and context compactions.
- Write the literal string `not measured` for any value that was not captured.
  Never estimate, interpolate or invent a historical timing: a task whose start
  was not recorded keeps `not measured` for start, end and duration.
- Build time and test time are the measured durations of the build and test
  commands run for the task, not a sum of everything that happened to run.
- Context compactions is the number of context compactions observed during the
  task when the session reports it; otherwise `not measured`.
- Failed and blocked tasks still get a row, with `result` set accordingly.
- Timing is measurement only. It never changes build flags, test selection,
  tolerances or timeouts, and it does not replace the reporting rules above.

### Task-time CSV conventions

- `docs/LOCAL_AI_TASK_TIMES.csv` header, in this order:
  `task_id,task_title,start_utc,end_utc,duration_seconds,result,build_seconds,test_seconds,context_compactions,notes`.
- Timestamps are ISO-8601 UTC with milliseconds, for example
  `2026-11-21T22:03:37.038Z`.
- `result` is one of `pass`, `fail`, `blocked`, `not measured`.
- Quote a field only when it contains a comma, a double quote or a newline;
  escape inner double quotes by doubling them.
- One row per task, appended at the end of the file. Do not sort, deduplicate
  or add a summary row.

## Mandatory Technical Handover (all development tasks)

At the end of every development task, update `docs/LOCAL_AI_PROGRESS.md` BEFORE presenting the
final report.

The handover must contain:

- Current branch, commit and working-tree status.
- Task objective and completion status.
- Root cause and technical decisions.
- Files changed and why.
- Actual build and test commands with PASS/FAIL results.
- Remaining problems and known limitations.
- Exact next recommended development step.
- Important findings required by a fresh AI session.

Rules:

- Preserve relevant findings from previous tasks.
- Keep the document concise and avoid duplication.
- Distinguish verified results from assumptions.
- Never claim tests passed unless executed.
- Never weaken or disable tests to make them pass.
- Do not commit or push without explicit permission.
- Do not modify files outside the current worktree.
- If a task fails or is blocked, document the actual status and blocker.
- A task is not complete until the handover has been updated.

### Handover file conventions

- `docs/LOCAL_AI_PROGRESS.md` — the per-task handover described above. One snapshot per session
  family: state the snapshot date, worktree path, branch, baseline commit and whether the claims
  were verified by execution. Update it in place; do not append an unbounded log.
- `docs/LOCAL_AI_TASK_TIMES.csv` — append-only log of measured task timings, one
  row per task that was timed in its own session. See "Mandatory task timing";
  unmeasured fields stay `not measured`.
- `docs/LOCAL_AI_HANDOVER.md` — long-lived navigation and engineering handover. An index, not an
  alternative truth source.
- Checked-out source, tests and `CMakeLists.txt` are authoritative. A handover claim is only as
  good as the command that produced it; re-run the recorded reproduction commands before acting
  on any claim.
