# OpenDesigner – standing rules for Claude Code

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
