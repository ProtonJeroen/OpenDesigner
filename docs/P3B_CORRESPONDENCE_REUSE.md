# P3B — roadmap clarification and scoped Face-pair overlap reuse

2026-11-15, `feature/v0.5.7-shared-brep-reconstruction`.

## Roadmap update first

Read ROADMAP, shared B-Rep/reconstruction/modeling architecture, and the blend
scope documentation before editing. Updated ROADMAP before application changes:

- Current v0.5.7 ledger: completed topology/support, certification/correspondence,
  measurement, accepted ellipse/obround/no-hole spline cap Move, P1/P2 and P3A;
  P3B implementation evidence with sign-off pending; one- and two-hole spline
  work and release/manual/CI gates remain pending.
- Current unsupported carrier/basis/trim/motion cases and atomic refusal rules.
- Existing straight/circular/conical Fillet/Chamfer scopes separated from pending
  elliptical/spline-edge, complex, variable-radius and advanced Chamfer work.
- Imported STEP recognition and history-independent Edit/Delete goals require
  exact geometry, continuity, trims, neighbors and material proof; optional
  recipes cannot establish parity. Chamfers retain ordinary Face eligibility
  with optional specialized feature recognition. Blended Move/Rotate/Pull/Delete
  regression expansion remains open.
- Foundation and future references explicitly retain Workplanes/Axes/Points,
  Sketch/projection, bounded ellipse/2D-spline scopes versus pending 3D paths,
  Sweep before Loft, v0.5.8 advanced Hole Editing, assemblies/alignment,
  STEP/IGES/STL then GLB/glTF/FBX, Linux/Windows/macOS and iPadOS/Android technical
  builds, and optional cross-platform touch with conventional controls retained.
- Corrected obsolete v0.5.6 candidate status from locally available merged
  `origin/develop` commit `98ed414` (#78); did not infer unrecorded manual/CI passes.

Every original version heading identifier and checklist capability line was
checked to remain present (normalizing only checkbox state). No milestone was
renumbered, silently removed, postponed or reassigned. Single-edge menu creation
is marked complete without closing the broader multi-edge/advanced checklist.

**Decisions still open:** the original v0.5.7 all-operation mixed-profile target
is broader than current scoped Move Face delivery. Retain the outstanding
requirements; any narrower release or carry-over requires explicit approval.
Advanced blend increments have no agreed release number: scope/version them
explicitly without displacing v0.5.8 or Sweep-before-Loft unilaterally. Preserving
existing holes during cap motion is v0.5.7; independently editing holes remains
v0.5.8. Spline-hole implementation has not begun.

## Exact code change and guarantees

`BRepCertification.cpp` owns a private OverlapTable on the stack of **one**
`correspondImpl` invocation. Its entries retain ordered original Face handles and
a successfully calculated overlap area. A lookup requires `IsEqual` for both
Faces: same TShape, location and orientation, with immutable current trim data
for that synchronous invocation. The matcher/copy/measurement policy is fixed
for the invocation. A/B and B/A are distinct; matching geometric carriers,
control coefficients, dimensions or volumes are never keys.

The first query executes the existing Common path, including spline operand
isolation, Boolean status/error checks and BRepMeasurement area certification.
Only after success is the area stored. Exceptions propagate to the original
atomic mapping/error path; no failed operation is installed as a default zero.
A successful zero-area contact can be reused. The table is destroyed on return
or refusal; no persistent, session-wide or cross-invocation cache exists.

The same table serves both ambiguity scans, source coverage, target coverage
and SameRegion evaluation. Every matching, coverage, area comparison,
owner-count, split/merge, ambiguity, removal/new-region and refusal predicate
remains in place. Only their identical overlap calculation is reused.
Standalone `coverage`/`sameTrimmedRegion` calls have no table and keep their
previous behavior. Material Common/Cut/Fuse, reconstruction, normalization,
topology tolerance and precision policies, P2 receipt/invalidation, and the
existing UI/history/persistence architecture are unchanged. Equal volume/area
alone is never substituted for actual material/region intersection.

Changed files for this assignment:

- `docs/ROADMAP.md`.
- `src/BRepCertification.cpp` (only application optimization).
- `tests/InvariantSplineSupportTest.cpp` (existing target, focused additions).
- This report and `P3B_PREVIEW_BOOLEAN_OPERATIONS.csv`.

## Focused validation

Incrementally built only OpenDesignerInvariantSplineTests,
OpenDesignerBRepCertificationTests, OpenDesignerSplineMoveTests and
OpenDesignerMoveFaceTests, maximum `-j4`. CTest used `-j2`:

| Test | Result | Seconds |
| --- | --- | ---: |
| `invariant-spline-support-correspondence` | PASS | 0.67 |
| `brep-certification-correspondence` | PASS | 0.35 |
| `spline-move-face-geometry` | PASS | 7.34 |
| `spline-move-face-session` | PASS | 8.09 |
| `move-face-preview-reuse` | PASS | 21.02 |

The existing tests retain trimmed-region, split/merge, ambiguity, material,
source-independent geometry, unsupported carrier/trim, source immutability,
world/Part placement, preview, Undo/Redo and persistence coverage.

New focused cases verify one Common and two reuse hits for an extended spline
Face pair; fresh evaluation in the next invocation; zero-area contact and
separate A/B versus B/A queries; distinct locations on the same TShape; reversed
Faces and different oriented query identities; small positive overlap; atomic
ambiguity refusal after an overlap is calculated, followed by a fresh successful
invocation. Source and target serialization must remain unchanged. The final
orientation-identity case was added after the first pass; only that changed
headless target was rebuilt/rerun. No application code changed after that pass.

No new CTest target, full suite, unrelated command test, CI run or private document
fixture. `git diff --check` passed.

## Measurements and actual Common counts

Before: preserved complete approved P3A run,
`/tmp/opendesigner-p3a-detail.log`. After:
`/tmp/opendesigner-p3b-detail.log`. Same detailed scenario, Release / OCCT 7.9.3,
three trials per synthetic solid, X11/OpenGL. Baseline was verified rather than
rerun unnecessarily. [Individual after-change Boolean calls](P3B_PREVIEW_BOOLEAN_OPERATIONS.csv)
contains 231 records, compared with 255 in the P3A artifact.

For the **tested no-hole two-cubic spline extrusion**, +15 mm axial Move:

| Metric per preview | Before | After |
| --- | ---: | ---: |
| Preview median, ms | 1624.373 | 1482.236 |
| Preview min–max, ms | 1602.932–1733.852 | 1459.409–1515.081 |
| Total Booleans | 26 | 18 |
| Total Commons | 22 | 14 |
| Face/set Commons outside material-equivalence helper | 16 | 8 |
| Repeated ordered Commons within correspondence invocation | 8 | 0 |
| Table reuse hits | 0 | 8 |
| All-preview repeated ordered pairs, including separate invocations | 10 | 2 |
| Independent material-equivalence Commons | 6 | 6 |
| Cuts / Fuses | 3 / 1 | 3 / 1 |
| Single-solid / self-interference checks | 6 / 6 | 6 / 6 |
| Explicit application copies | 181 | 157 |
| Volume / area measurements | 30 / 96 | 30 / 88 |
| GK volume / adaptive area integrations | 29 / 96 | 29 / 88 |

All counts were identical across the three trials. Two cross-invocation repeats
remain deliberately: each call establishes its own immutable source/result
context. No sharing with the later, separate boundary-validation invocation.

Median timed work, milliseconds:

| Work | Before | After |
| --- | ---: | ---: |
| Face/set Commons | 222.255 | 105.403 |
| Independent material Commons | 690.806 | 679.161 |
| Cuts | 263.489 | 257.169 |
| Fuse | 46.281 | 41.220 |

The sampled spline median is 142.137 ms lower (about 8.8%). This is an observed
three-trial result, not a universal or fixed improvement. The deterministic gain
is eight eliminated redundant Commons and their associated isolation copies and
area integrations. Full source/region/result certification still runs.

Controls were also recorded:

| Solid | Preview median before → after, ms | Boolean count before → after | Reuse hits |
| --- | ---: | ---: | ---: |
| Box | 188.364 → 146.909 | 15 → 15 | 0 |
| Ellipse | 908.846 → 637.706 | 25 → 25 | 0 |
| Obround-hole | 416.405 → 264.513 | 19 → 19 | 0 |

Their lower timings are **not attributed to P3B**: their work/call counts did not
change. Run-to-run host/load/allocator/display variation limits causal claims
about all of the spline wall-time difference too. No extrapolation to other
profiles, curve representations, hole counts, transforms or platforms.

All 12 after-change confirmations retained exactly one P2 reuse and zero Move
Face reconstruction calls. Timing trials passed source immutability and exact
source Undo checks.

## Remaining work and stop

The tested spline still performs six independent material Commons, three Cuts,
one Fuse and six self-interference checks. Those dominate the residual preview;
no evidence here permits removing them. Ellipse Booleans remain untouched.
Repeated property/source validation could be investigated next with a separately
approved immutable per-evaluation proof and compatibility plan; do not infer safe
reuse from equal volume, matching carriers or a passed tolerant equivalence.

Recommended next action is review/sign-off of P3B and its roadmap scope. Any next
performance investigation, release validation/CI, or spline one-/two-hole
implementation needs its own approval. All ten private .odc files, other user data
and prior uncommitted work remain preserved. No reset, clean, commit, push, PR or
merge. This assignment stops here.
