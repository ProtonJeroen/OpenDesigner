# v0.4.9 final integration review (Phase 9F)

Review baseline: `74b70eb` on `feature/v0.4.9-move-cylindrical-face`.
This review prepares the existing scope for publication; it does not add geometry
capabilities or change the roadmap.

## Architecture and scope

Cylindrical Move Face, boundary feature deletion and radial/height Pull derive
capability from current analytic surfaces, connected face patches, adjacency,
physical rim/support planes and material occupancy. Kernel work uses isolated
B-Reps. Local reconstruction validates closed manifold material, preserved
unaffected trimmed regions and source/result correspondence. Conservative
reconstruction budgets and safe refusals remain part of the prototype's scope.

Move Face supports explicit lateral references and angles; boss movement exposes
Trim/Extend. Repeated notch and supported boundary-boss edits reanalyze the current
geometry. A historical Trim boundary is not retained after it disappears.
Radial Pull and supported boss height Pull use certified local reconstruction,
without retrying the unsafe global cylindrical offset path. The planar dispatcher
also refuses uncertified transverse cylinder intersections before unsafe offset.

## Acceptance and document lifecycle

The user reported 47/47 manual desktop checks passed across the recent geometry
and crash-fix phases, including acceptance of Phases 9D.7A–9D.7D. These are reported
manual results, not a new manual acceptance run performed during Phase 9F.
Historical phase documents retain their phase-specific scope and fixture hashes.

[Phase 9E](FACE_HISTORY_PERSISTENCE.md) verified the four current private native
documents using temporary output files. It covered cached Undo/Redo, Part ID,
placement, color, visibility, Active Part and Structure Browser identity, preview
cleanup, confirmed-only Save during previews, and fresh B-Rep editing after Open.
Reopened topology counts, solid validity, volume, Boolean material equivalence and
sampled occupancy matched the confirmed states. Failed/cancelled edits preserved
the confirmed material. Those private-file passes are retained; they are not
replayed as unchanged focused tests in this review.

History remains session-local. `.odc` stores current B-Reps and document/Part
metadata, not feature recipes or Undo stacks. Save preserves live history and
selection. Open clears history, selection and tools and reconstructs presentations.

## Unresolved geometry capabilities

- **Delete Face on the sloped cylindrical opening in `hole2.odc` is unresolved.**
  It is unsupported both before and after serialization. Phase 9E checks the same
  refusal on both B-Reps and verifies failure leaves model/history unchanged.
  Move Face and radial Pull on supported rotated planar rims do work. A passing
  persistence test does not mean this Delete Face capability is complete.
- Uncertified boss corner/multiple-side crossings, sloped or curved boss trims, blends and
  interacting features outside the current material certificates remain unsupported.
- Complex/disconnected/annular boss attachments and oblique or nonconstant-section
  boss caps are not general height/radius reconstruction capabilities.
- Separate independent cylindrical features, mixed wall/cap absolute parameters,
  and general multi-Part cylindrical Pull are not composed into one joint edit.
  Connected split fragments of one certified support are supported.
- General freeform, blind or nonconvex closures outside the local certificates,
  degenerate/tangent/disconnected requests and exceeded numerical/topology budgets
  remain diagnostic refusals. These restrictions do not imply geometric impossibility.

See [rotated rims](ROTATED_RIM_CYLINDER.md),
[boundary bosses](REPEATED_BOUNDARY_BOSS_MOVE.md),
[Delete Face](DELETE_FACE.md), and [Pull/Resize](PULL_RESIZE_GAPS.md) for details.

## Release validation

Configuration: Release, all targets, `OPENDESIGNER_BUILD_TESTS=ON`, Ninja,
GCC 16.2.1, Qt 6.11.2 and Open CASCADE 7.9.3 on Linux.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DOPENDESIGNER_BUILD_TESTS=ON
cmake --build build -j2
QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 ctest --test-dir build -C Release --output-on-failure
```

Validation completed on 2026-11-05:

- Complete Release build: passed, all 225 scheduled steps, with no compiler
  warnings or errors in the build log.
- Full CTest suite ran **once**: **82/86 passed**, four failed, **1008.58 seconds**.
- `cylindrical-move-geometry` retained a Phase 9B refusal expectation for a current
  overhanging boss supported since 9D.7B. Replace it with independent Boolean
  material, analytic volume, sampled occupancy and input-immutability checks for
  both policies. Its targeted rerun passed (39.97 seconds).
- Normal/HiDPI `cylindrical-move-face-sessions` expected the obsolete diagnostic
  “Unsupported cylinder”. Require no session plus a meaningful reconstruction
  refusal for the recognized surface. Targeted reruns passed (5.87/5.52 seconds).
- `sketch-projection-sessions-hidpi` reproduced a QWidget/native-frame mismatch
  across the desktop's logical screen gap: at DPR 2 the primary screen is
  `(0,0,1280,540)`, while the second starts at `(2560,0)`. Native and QWidget
  coordinates differed by 1280 logical pixels. This positioning code predates v0.4.9.
  Place the HiDPI multi-screen test fixture on the primary screen and wait for its
  native frame before testing the panel. Keep all original placement, navigation,
  scrolling and cleanup assertions. The targeted rerun passed (18.37 seconds).
- Only affected test executables were rebuilt after these test corrections.
  The first targeted batch passed the three corrected geometry/session cases;
  the unchanged projection case reproduced its placement failure. After the
  fixture correction, only that projection case was rerun. No second full suite
  or unrelated passing focused tests ran.
- **All 86 cases have passing results across the full and targeted runs.** This
  is not a claim that the initial full suite passed without corrections.
  Native document sessions, round-trip storage, Part/Active Part identity,
  history/preview lifecycle, boundary Delete Face, rotated rims, Pull and the
  crash regression all passed within the full run.
Logs are retained locally in `build/phase9f-configure.log`,
`build/phase9f-build.log` and `build/phase9f-ctest.log`. Targeted results are in
`build/phase9f-rerun.log` and `build/phase9f-projection-rerun.log`; the screen probe
is recorded in `build/phase9f-screens.log`.

Publication labels previously still identified v0.4.8. This review corrects the
CMake project, application version/window title, staged artifact description and
README to v0.4.9. No modeling behavior is changed by those metadata corrections.
The regression fixes change test expectations/fixture placement, not geometry or
production command-panel algorithms.

## Release readiness and platform qualification

The Linux Release build and all tested cases are validated for the documented
prototype scope. No new production geometry, history or persistence fix was
required. The unresolved sloped-opening Delete Face capability remains explicit.

**Cross-screen HiDPI panel placement remains a platform limitation on this
Qt 6.11.2/xcb desktop.** Fixing the fixture does not fix or certify production
cross-screen placement. Single-screen HiDPI placement assertions pass; the review
does not claim seamless movement through logical screen gaps. Publication should
retain this qualification alongside the geometry limits. Windows/macOS and a
standalone bundled distribution remain outside this Linux prototype validation.
No release was published, pushed or merged by this review.

## Private fixture baseline

All four files were untracked at review start. The hashes below describe the
current supplied files; they do not replace historical hashes from earlier
acceptance captures. They were verified byte-identical and untracked after validation.

| File | SHA-256 |
| --- | --- |
| `moveCylinder.odc` | `d04452e1c11fcb9fa1cb877903514e3f50c027ced1cebbc0ca583bc71863d465` |
| `moveCylinderBug.odc` | `a2033d919c2f962e2e95248d4d0c22b6bb2c77da49dd943b39729015103f9ac6` |
| `crash03112026.odc` | `9aec713fa3f46925e6e2a13c516b4bbe71c86b0a37f22d42fc61547a7dad61c4` |
| `hole2.odc` | `80d421dd8a5370f446cfd1a2478ceb73380db0b686b58b616dcc5d14260ea5ee` |
