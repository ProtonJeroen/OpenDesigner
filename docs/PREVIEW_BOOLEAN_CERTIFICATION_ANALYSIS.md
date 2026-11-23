# P3A — first-preview Boolean certification analysis

Investigation only, 2026-11-15. Branch:
`feature/v0.5.7-shared-brep-reconstruction`. No reconstruction, certification,
tolerance, eligibility, or P2 reuse optimization was implemented in P3A.

## Measurement and tracing

Built only `OpenDesignerMoveFaceTests` and `OpenDesignerBRepCertificationTests`
incrementally with at most `-j4`. The existing Release / OCCT 7.9.3 / X11/OpenGL
scenario now accepts `--preview-certification-timings`. Three trials per solid
exercise the same +15 mm edit, actual selection, preview, confirmation and Undo.
The synthetic solids are Box, ellipse, rotated obround with circular hole, and
the no-hole two-cubic-segment spline extrusion. No private document was loaded.

Raw run: `/tmp/opendesigner-p3a-detail.log`.
[Individual Boolean operations](PREVIEW_BOOLEAN_CERTIFICATION_OPERATIONS.csv)
contains **255 separate timed calls**, including every trial, ordered operands,
results and caller paths; the report does not substitute an aggregate timer for
individual Common/Cut/Fuse measurements.

Tracing is opt-in. Oriented TShape/location identities are assigned diagnostic
IDs within one timing phase only. Pure `BRepBuilderAPI_Copy` provenance is
recorded for roots and Faces; normalization mutation breaks that provenance.
No identities are inferred from matching carriers, equal volumes, successful
material-equivalence checks, or similar dimensions. Registry handles are
released after each timing report and are never consulted by modeling code.
IDs are not persistent subshape identities. Some measurements share copy
provenance while operating on different actual handles: those are distinguished
from literal same-handle repetitions below.

Per-call timers cover Boolean Build, explicit copy constructors, existing raw
B-Rep validity, self-interference Perform, analytic/adaptive integration, NURBS
retry, exact prism creation, normalization, mesh, AIS delivery and event flush.
Existing nested timers cover recognition, topology, correspondence, material
checks and P2 snapshot overhead. The tracer retains temporary handles for the
phase to avoid pointer-reuse ambiguity; this and diagnostic bookkeeping affect
absolute wall time. Timings are observations/upper-bound work estimates, not
promised optimized results. Nested inclusive timings must not be added.

## 1. Exact preview computation stages

Common sequence:

1. P2 checks the source/frame snapshot; resolve numeric value and direction.
2. `MoveFaceGeometry::reference` checks membership and selected plane, validates
   the source solid, measures volume, runs self-interference on an isolated copy,
   and computes the arrow centroid (a separate display-only area integration).
3. `movePlanarLocal` isolates a neighborhood, validates its copy, indexes topology
   and checks the complete applicable invariant-support neighborhood.
4. Build the exact swept cap region. Check region solidity and source/sweep
   occupancy; reconstruct using the existing Fuse/Cut route where applicable.
5. Certify directional material change against that exact region.
6. `finishLocal` validates the edit region, normalizes a copied candidate and
   independently certifies normalization's material preservation. Cut the old
   and normalized solids by the edit region and compare their outside material.
7. Map actual trimmed Faces, preserve unaffected regions, check permitted result
   supports and the complete moved cap. Apply operation-specific final checks.
8. Construct the existing FaceChange, mesh/display the blue candidate, record the
   guarded P2 receipt, redraw and deliver queued UI events.

Ellipse specialization: the local route first refuses the elliptical carrier;
`moveEllipticalCap` then prepares another neighborhood. Source and result each
undergo exact cap/wall/axis/hole recognition, orientation/manifold/tolerance,
analytic section-area × height and complete-prism material checks. Oracle Face
regions are checked in both directions. It constructs a change prism and full
candidate prism, computes **both** directional Cuts, and retains the common
finish and final opposite/translated-cap region checks.

Spline specialization: complete source invariant-extrusion recognition builds a
whole-section witness and checks its material. After the swept region and Fuse,
a complete cap prism must equal the Boolean result. Directional change is
certified, then the common finish runs scoped spline correspondence. Result
support/boundary validation invokes correspondence again; a final copied-result
neighborhood, complete extrusion witness, volume/area and topology checks remain.

Box and obround use the existing local analytic path. Obround's several matching
cylindrical Face bands have **different trims**; testing each is not redundant
merely because the supporting cylinders agree.

## 2. Measured stages

Milliseconds, median of three trials. Parenthesized numbers are call counts,
which were stable across trials.

| Work | Box | Ellipse | Obround-hole | Spline |
| --- | ---: | ---: | ---: | ---: |
| **First preview total** | **188.364** | **908.846** | **416.405** | **1624.373** |
| Total Boolean Build wall time | 62.200 (15) | 587.831 (25) | 197.333 (19) | 1231.328 (26) |
| Material-equivalence Common | 19.657 (3) | 235.731 (5) | 65.337 (3) | 690.806 (6) |
| Other individual Commons | 15.497 (8) | 55.241 (16) | 35.505 (12) | 222.255 (16) |
| Individual Cuts | 21.352 (3) | 296.859 (4) | 68.852 (3) | 263.489 (3) |
| Individual Fuses | 5.889 (1) | — | 20.598 (1) | 46.281 (1) |
| Raw B-Rep validity checks | 44.032 (33) | 24.287 (75) | 64.002 (37) | 44.846 (158) |
| Self-interference Perform | 11.052 (5) | 167.152 (8) | 64.965 (5) | 242.127 (6) |
| Explicit application B-Rep copies | 6.201 (24) | 3.158 (35) | 7.306 (24) | 8.534 (181) |
| Exact prism constructors | 0.203 (1) | 0.297 (4) | 0.145 (1) | 0.470 (4) |
| RemoveInternals | 0.043 | 0.007 | 0.045 | 0.008 |
| UnifySameDomain Build | 2.251 | 0.098 | 0.948 | 0.144 |
| TopologyIndex construction | 0.210 (2) | 0.214 (8) | 0.176 (2) | 0.321 (31) |
| Preview mesh | 0.818 | 4.821 | 6.116 | 3.455 |
| Preview AIS display/redisplay | 0.109 | 0.427 | 0.283 | 0.399 |
| Preview redraw wrapper | 17.859 | 19.064 | 12.146 | 13.028 |
| Final Qt event flush | 28.610 | 25.243 | 23.920 | 18.280 |

Boolean total is the median of each trial's total, not the sum of independent
row medians. Explicit copy counts exclude copies internal to OCCT Build/Prism.
The raw validity counts include the **pre-existing** analyzer inside every
measurement call: for example, spline has 32 outer checks plus 126 measurement
checks, not 158 newly introduced checks.

Total ranges: Box 177.751–306.226 ms; ellipse 880.192–968.225 ms; obround
312.195–431.275 ms; spline 1602.932–1733.852 ms. These are not a performance
comparison against P2 or evidence of a new regression/speedup.

Selected **inclusive** certification stages, overlapping the table above:

| Stage | Box | Ellipse | Obround-hole | Spline |
| --- | ---: | ---: | ---: | ---: |
| Initial reference/source validation | 7.646 | 32.528 | 16.498 | 52.841 |
| Neighborhood preparation (all calls) | 6.004 | 48.396 (2) | 19.343 | 83.265 (2) |
| Ellipse source/result recognition | — | 197.486 (2) | — | — |
| Spline complete extrusion witnesses | — | — | — | 214.076 (2) |
| Invariant spline Face proofs | — | 0.345 (failed ellipse probe) | — | 16.281 (24) |
| Explicit both-direction material difference | — | 173.553 | — | — |
| Normalize including its certificates | 37.964 | 86.942 | 81.252 | 173.911 |
| Protected-region verification | 27.397 | 179.808 | 63.732 | 259.863 |
| Scoped correspondence | — | — | — | 218.074 (13) |
| Entire common finish | 88.787 | 285.312 | 203.452 | 567.062 |

The ellipse's known-refused local attempt costs 24.926 ms before the 794.533 ms
elliptical route. Actual prism construction and unification are tiny compared
with the certificates around them.

## 3. Integration counts and cost

| Measurement work | Box | Ellipse | Obround-hole | Spline |
| --- | ---: | ---: | ---: | ---: |
| Volume measurement calls | 17 | 27 | 17 | 30 |
| Area measurement calls | 9 | 37 | 13 | 96 |
| Analytic volume integrations, ms (count) | 3.952 (17) | 4.968 (24) | 13.305 (17) | 0.002 (1) |
| Analytic area integrations, ms (count) | 0.485 (9) | 2.508 (37) | 1.254 (13) | — |
| Adaptive volume GK, ms (count) | — | 49.687 (3) | — | 21.517 (29) |
| Adaptive area, ms (count) | — | — | — | 23.538 (96) |
| Full volume measurement wrapper, ms | 37.581 | 68.846 | 57.194 | 44.749 |
| Full area measurement wrapper, ms | 3.408 | 8.983 | 4.918 | 46.304 |

One additional reference-centroid area integration per solid is display work,
not independent material certification. No NURBS conversion/retry was needed in
these particular trials; the existing retry remains required for other supported
numerical cases and is untouched. Spline adaptive numerical integration is about
45 ms here, while Boolean Build is about 1231 ms. Numerical precision is not the
main bottleneck, and reducing it is not proposed.

## 4. Proven repeats and their limits

Counts below are **excess calls after the first identical query** within the
preview. Ordered operands are used even for Common. Literal handle identity and
unchanged pure-copy provenance are reported separately; neither is a reusable
certificate by itself if later mutations or policy changes are possible.

| Repeated work | Box | Ellipse | Obround-hole | Spline |
| --- | ---: | ---: | ---: | ---: |
| Same ordered Boolean pair, including pure copies | 0 | 1 | 0 | 10 |
| Of those, literally same operand handles | 0 | 1 | 0 | 0 |
| Same measurement/property/policy, including pure copies | 6 | 20 | 6 | 84 |
| Of those, literally same measurement input | 5 | 18 | 5 | 79 |
| Repeated raw validity query, including pure copies | 13 | 31 | 13 | 117 |
| Repeated self-interference target, including pure copies | 1 | 4 | 1 | 2 |

The raw self-interference algorithm always receives another isolated copy; its
repeat count describes copy-equivalent targets, not a literally reused solver
operand. Validation versus copying/normalization must retain their own safety
contracts; the table is not permission to drop a validator.

**Spline Face intersections:** there are two source/result side-Face pairs.
Each is intersected three times inside one correspondence call: source coverage,
target coverage, and SameRegion classification. The same pairs are encountered
again by the separate boundary-validation layer, for six Commons per pair.

- Ten repeats across the complete preview cost 133.695 / 143.476 / 148.088 ms
  in the three trials.
- Eight repeats **within individual correspondence calls** cost 105.656 /
  111.834 / 121.364 ms. These require no cross-layer cache or symmetry shortcut.
- The checks using the results are distinct; the duplicated work is recomputing
  their identical intersection, not performing the coverage/region predicates.

**Ellipse:** the opposite-cap intersection is repeated between common-finish
coverage and the final SameRegion check, costing only 0.391–0.773 ms. Additional
forward/reverse oracle intersections exist. A∩B and B∩A have equal mathematical
area, but OCCT output orientation, tolerance and parameterization cannot simply
be assumed interchangeable. Those swapped queries are **not counted** as proven
ordered-result duplicates. Both directional region-preservation predicates must
remain even if a future symmetric-area proof permits sharing their input data.

**Measurements:** duplicate full measurement work costs 11.658–22.505 ms for
Box, 9.511–9.958 ms for ellipse, 17.039–24.879 ms for obround, and
48.579–56.901 ms for spline. In spline, 71 excess area calls and 13 excess volume
calls are observed. Within correspondence invocations alone, 46 excess area
queries cost 18.922–22.241 ms, including calls inside support-proof helpers;
a small local area table would not automatically capture every one of those.
These wrapper times include nested validity/copy work: do not add the separate
validity-repeat estimate to them.

**Protected material:** the old-outside Cut, new-outside Cut and their Common
have different operands. No identical protected-region Boolean was found.
Normalization equivalence, directional delta equivalence, initial whole-section
witness and final whole-section witness also have different operands. Success
in one does not make the others duplicates, particularly with nonzero tolerances.

## 5. Safe opportunities, ranked

1. **First P3B: one successful ordered-pair overlap-area result per
   correspondence invocation.** Use the existing isolated OCCT Common and shared
   measurement once; supply its result to source coverage, target coverage and
   SameRegion checks. Keep every gate, ambiguity test, area comparison and
   source-to-result classification. Key by actual oriented trimmed Faces in
   that immutable call and fixed matching/copy policy, never carrier alone.
   No failed/uncertifiable result may be cached as zero.

   On the spline fixture this changes 26 → 18 total Booleans, removing eight
   redundant Face Commons. Measured kernel work is about **0.11 s**, roughly
   seven percent of first-preview time, plus the associated copies and area
   work. This is a work estimate, not measured optimized latency. The other
   three Move Face routes do not use this correspondence path, so no ellipse,
   Box or obround gain is promised from this first change.

2. **Local successful area/property results for unchanged inputs.** Keep exact
   property kind, analytic/adaptive policy, domain/precision evidence and input
   immutability. `requireUnambiguous` currently evaluates `area(a)` twice in one
   expression; classification requests it again. Retain all finite/positive,
   region and material comparisons while avoiding identical integrations.
   The 49–57 ms spline duplicate-wrapper estimate is an upper bound requiring
   broader plumbing; it overlaps validation time and cannot be blindly added
   to other savings. Do not introduce a global measurement cache.

3. **Validated source context within one preview, only after a separate proof.**
   Ellipse validates copy-equivalent source four times; repeated targets across
   all source/result checks account for about 82–88 ms of self-interference.
   Spline repeated targets account for 77–97 ms. To reuse such evidence one must
   establish exact copy/immutability and context guarantees, preserve validation
   of newly reconstructed/normalized geometry, and retain membership, ownership,
   closure, manifold and tolerance checks. This is larger than the recommended
   first P3B and is not an unconditional safe saving.

4. **Earlier deterministic route refusal.** A cheap validated-neighborhood
   carrier check could avoid the known-failed ellipse local attempt (~25 ms).
   It must examine the selected neighborhood, not globally dispatch every solid
   containing an unrelated ellipse, and preserve fallback/unsupported behavior.
   This improves refusal/routing work, not material certification itself.

5. **Topology/proof sharing and copies are lower priority.** The spline's 31
   indices total only 0.321 ms. Recognition receipts also depend on adjacency
   context (a Face-only index differs from a whole-solid index), trim domains,
   orientations and material side. Never reuse a bare carrier/control-pole match
   as a trimmed-region or adjacency proof. All 181 explicit spline copies total
   only 8.534 ms. Some isolate OCCT calls known to change ownership/cache flags;
   removing them risks source immutability and P2 guards for a small benefit.
   Caching an already successful query avoids its copies safely; removing the
   copies around a first required query is a different, unproven change.

No large ellipse speedup is justified by proven duplicate ordered Booleans:
its expensive material Commons/Cuts are mostly distinct independent evidence.
Equal volume, zero measured overlap volume, or a previously passed tolerant
material-equivalence comparison must not substitute for those exact set checks.

## 6. Certification risks and required compatibility

- Preserve `1e-6` topology ceiling, existing `1e-8` material/region policies,
  `1e-12` adaptive integration precision and source validity requirements.
- Do not merge keys for same planes/cylinders/spline carriers with different
  wires, holes, orientations, locations or trims. Split/merged/removed/new and
  ambiguous relations must retain existing behavior.
- Do not treat tolerant material equivalence as exact identity or assume
  transitivity of several tolerant checks.
- Keep valid empty/contact differences distinct from positive-solid acceptance;
  cached area/volume zero is not a solid certificate.
- Preserve Boolean error/refusal handling and numerical precision diagnostics;
  cache only a successfully established result under identical policies.
- Keep independent area×height/analytic oracles, directional material change,
  protected regions, normalization material checks and final support checks.
- Preserve first-query isolation copies. Maintain P2 receipt creation only after
  full certification and its exact source/candidate/frame/input invalidation.

A P3B implementation should start with the local ordered overlap table and
focused split/merge/ambiguity, changed-trim, reversed/location, zero-area contact,
near-tolerance and source-immutability compatibility cases, plus the existing
four-solid timing/reuse scenario. Do not expand general spline recognition or
editing capabilities. Measure actual after-change gains before broadening scope.

## 7. Validation, changes and stop

All 12 detailed timing trials completed, with exactly one P2 reuse and zero
reconstruction calls during each confirmation. Source snapshots and exact-source
Undo checks passed. Only these focused CTests ran with `-j2`:

| Test | Result | Seconds |
| --- | --- | ---: |
| `move-face-preview-reuse` | PASS | 21.21 |
| `move-face-startup-validation` | PASS | 2.20 |
| `elliptical-move-face-session` | PASS | 10.38 |
| `obround-move-face-session` | PASS | 3.41 |
| `spline-move-face-session` | PASS | 16.16 |
| `brep-certification-correspondence` | PASS | 0.42 |

Instrumentation files changed: `BRepTiming.h`, `BooleanGeometry.h`,
`BRepCertification.cpp`, `BRepMeasurement.cpp`, `BRepReconstruction.cpp`,
`BRepInvariantExtrusion.cpp`, `MoveFaceGeometry.cpp`, `MoveFace.cpp`, and
`tests/MoveFaceTest.cpp`. Added this report and the per-operation CSV. No new
CTest target, optimization, persistent modeling cache or numerical engine.
`git diff --check` passed. No full suite, commit, push, PR or merge. All ten
private documents and other user data remain untouched. P3A stops here.
