# Move Face latency investigation — 2026-11-15

Investigation only. No optimizations, caches, weakened certificates or modeling
policy changes were made. All prior uncommitted work and ten private documents
were retained. The added diagnostic scopes are inactive by default.

## Method

Built only `OpenDesignerMoveFaceTests` incrementally with `-j4`, using the existing
Release build and OCCT 7.9.3. Ran:

```
build/OpenDesignerMoveFaceTests --startup-timings
```

The successful run used the existing X11/OpenGL display; the sandboxed GUI launch
aborted before producing output. An initial harness run stopped after Box trial
1 because retained manipulation state/screen coordinates affected the next hit.
The corrected harness resets tools and recomputes projection between trials.
These are benchmark setup changes, not application behavior changes.

Four synthetic exact solids were measured for three trials each: a 100×80×20
Box, a 50/30 radius elliptical extrusion, the existing 30° rotated obround with
radius-10 through-hole, and the existing two-cubic-segment no-hole spline prism.
All heights start at 20 mm. Geometry creation, Part presentation setup, camera
fitting and intertrial cancellation occur outside the timed intervals.

Selection uses actual Qt mouse press/release events at a projected interior cap
point and verifies the selected B-Rep owner. Context-menu construction is timed
separately; no user think time is included. Startup calls the existing Move Face
command with the cap selected. Preview types +15 mm and calls the normal command
parameter update. Selection/startup/preview include synchronous rendering and a
Qt event flush. No startup phase invoked `previewMoveFace`: the initial command
panel starts at zero; the first nonzero preview is a separate stage.

Source serialization is checked after every trial. Timing scopes collect call
counts and inclusive/exclusive wall time on the current thread. Nested times
must not be summed. OCCT worker work is reflected in caller wall time, not
independently traced. Three trials characterize these fixtures, not arbitrary
user documents or all GPU/display configurations.

Raw successful run: `/tmp/opendesigner-move-startup-timings.log`.

## Stage timings

Milliseconds, median of three trials (minimum–maximum):

| Solid | Face selection | Context menu | Command startup | First +15 preview |
| --- | ---: | ---: | ---: | ---: |
| Box | 67.401 (62.802–68.096) | 16.710 (14.241–27.080) | 77.260 (59.130–79.320) | 148.062 (138.031–180.790) |
| Ellipse | 66.829 (62.749–68.776) | 37.476 (34.508–38.011) | 88.511 (72.554–90.780) | 824.247 (747.635–866.373) |
| Obround with hole | 66.835 (65.869–68.489) | 24.871 (23.084–29.787) | 77.111 (70.664–92.365) | 304.971 (292.338–346.517) |
| Spline | 67.461 (64.831–81.059) | 86.732 (86.288–129.076) | 109.532 (100.314–130.002) | 1842.310 (1728.748–1913.746) |

The substantial shape-dependent delay is in nonzero preview reconstruction and
certification. A multi-second delay merely opening Move Face was not reproduced
with these fixtures. Selection is similar for all four and did not invoke solid
certification, correspondence or topology indexing. Startup's geometry cost is
principally `reference -> validSolid -> singleSolid -> BOPAlgo_ArgumentAnalyzer`;
remaining startup time includes UI/arrow display, redraw and event handling.
Those UI portions were not decomposed into individual driver calls.

## Exact cost attribution

The following is **trial 2**, not a mixture of separately calculated medians.
Boolean rows are disjoint timed API buckets. Measurement includes its own input
validity/copy/domain work; integration alone is shown separately.

| Preview work | Box | Ellipse | Obround-hole | Spline |
| --- | ---: | ---: | ---: | ---: |
| Entire preview phase, ms | 138.031 | 747.635 | 304.971 | 1842.310 |
| Material-equivalence Common Build, ms (calls) | 13.536 (3) | 209.924 (5) | 57.091 (3) | 782.403 (6) |
| Other Common, ms (calls) | 8.001 (8) | 47.895 (16) | 27.648 (12) | 266.710 (16) |
| Cut, ms (calls) | 14.383 (3) | 236.617 (4) | 52.845 (3) | 312.423 (3) |
| Fuse, ms (calls) | 5.337 (1) | 0 (0) | 13.076 (1) | 46.180 (1) |
| Self-interference Perform, ms (calls) | 6.555 (5) | 126.565 (8) | 39.372 (5) | 269.995 (6) |
| All measurement calls, ms (calls) | 27.468 (26) | 60.340 (64) | 45.260 (30) | 104.225 (126) |
| Analytic integration, ms | 2.861 | 6.582 | 10.817 | 0.002 |
| Adaptive integration, ms (calls) | 0 (0) | 37.090 (3) | 0 (0) | 59.798 (125) |
| Topology indexing, ms (calls) | 0.165 (2) | 0.159 (8) | 0.213 (2) | 0.390 (31) |
| Mesh/AIS preview creation, ms | 0.909 | 4.161 | 7.301 | 3.207 |

Boolean work accounts for about 66% of the elliptical preview, 49% of obround
and 76% of spline. The largest individual API buckets are Cut for the ellipse
and material-equivalence Common for obround and spline. This is repeated exact
material/region certification, not tessellation or topology enumeration.

Representative inclusive owners (these overlap rows above):

- Ellipse: `moveEllipticalCap` 652.345 ms; source/result recognition twice
  182.225 ms; `finishLocal` 218.228 ms; protected-region verification 135.779 ms.
- Obround: `movePlanarLocal` 242.601 ms; `finishLocal` 144.965 ms;
  normalization 67.236 ms; protected-region verification 54.421 ms.
- Spline: `movePlanarLocal` 1746.617 ms; complete extrusion certification twice
  235.214 ms; `finishLocal` 673.060 ms; correspondence 13 calls / 263.434 ms;
  protected-region verification 304.052 ms. Its 31 topology indexes cost only
  0.390 ms. Correspondence cost is primarily region Boolean/validation work.

`BRepReconstruction::prepare` was never invoked in these measured paths.
Neighborhood preparation was invoked once for Box/obround and twice for
ellipse/spline previews. There are repeated source validations: reference,
isolated neighborhood, source/result recognition and final normalization all
retain their separate acceptance checks.

Before any edit, ellipse/obround/spline menus call planar `reference` twice:
once for Move/Rotate/Align availability and again through `supportsResize`.
Startup calls it again. Thus the same source receives three independent
single-solid/self-interference checks across menu and startup. Blend resolution
also runs twice in the menu and once at startup; in trial 2 startup it cost only
0.095 / 0.147 / 0.500 / 0.125 ms for Box/ellipse/obround/spline.

Trial 2 startup self-interference Perform was 1.071 / 17.743 / 5.921 / 36.124 ms.
Total property measurement was 1.258 / 0.838 / 2.091 / 2.417 ms. Ellipse and
obround startup used **no adaptive integration**. Spline adaptive integration
was just 0.659 ms at startup. The extra spline proof attempted before elliptical
fallback cost 0.203 ms during preview and did not run at startup.

## Pre-existing behavior versus v0.5.7

Source comparison used the locally available `origin/develop` at `98ed414`
(v0.5.6 base). This is code ancestry evidence, not a separately timed old binary.

Already present there:

- Menu blend recognition and duplicate planar reference checks via
  `populateFaceMenu` and `supportsResize`.
- `startMoveFace`: blend resolution, plane reference, command session setup,
  arrow display and redraw.
- `reference -> validSolid`: full B-Rep validity, volume, deep copy and
  `BOPAlgo_ArgumentAnalyzer::SelfInterMode` / `Perform`.
- Local cap sweeps, Fuse/Cut, independent material Common/equivalence,
  protected-region checks, unaffected Face coverage and certified normalization.
- Explicit preview mesh creation and display through the existing command UI.

v0.5.7 adds/extracts shared TopologyIndex, certification, correspondence and
measurement paths; adds the certified elliptical route and the spline-specific
complete-extrusion/correspondence certificates. Shared measurement also performs
its own input validation. These add measurable preview work, particularly for
spline correspondence and repeated exact material comparisons, but do not
explain the earlier existence of the startup behavior. Obround still takes the
existing local planar route. Ellipse/obround startup never entered spline
adaptive integration, so blaming that code as the original cause is unsupported.

The investigation stops here. No certification was removed or reused through a
new cache, and no optimization was implemented. Further work must distinguish
any reported slower real-document opening delay from the preview bottleneck
reproduced here before changing acceptance or lifecycle behavior.

## Performance P1 — remove redundant synchronous menu validation (2026-11-15)

### Change and safety boundary

The optimization is confined to `populateFaceMenu`:

1. For one planar Face, compute `MoveFaceGeometry::reference` once for
   Move/Rotate/Align availability. Do not call `moveFaceReference` on a planar
   failure: it delegates to the identical reference check for that Face.
2. A successful reference already implies the existing `supportsResize` result.
   That function either recognizes a primitive and returns true, or falls back
   to the same planar reference and returns true. Reuse this **Boolean decision
   within the current synchronous function** rather than repeating blend,
   primitive and source validation merely to determine action visibility.
3. Failed reference and nonplanar/multiple selections retain the existing full
   resize eligibility path. All actual command entry points are unchanged.

There is no validation object, persistent cache, revision key or certificate
stored on a Part, QAction or session. Menu availability is not authorization to
accept geometry. Every action invokes the current command entry point, which
revalidates the then-current source and member Face. Ordinary startup has one
required source validation, not two: P1 deliberately retains it. A menu may stay
open while geometry changes, so carrying its result into a later command start
would require an additional revision/immutability proof.

Source tolerances, manifold/self-interference checks and all reconstruction,
material occupancy, Common/Cut/Fuse, correspondence, preview and confirmation
acceptance paths are unchanged. Box menu construction no longer needs an unused
primitive-equivalence probe once the shared planar availability decision is
known; the primitive recognizer and its material certificate remain unchanged
at actual command invocation. No editing capability was added.

P1 changed files:

- `src/SelectionContext.cpp`: the local availability short circuits above.
- `src/CadViewport.cpp`: diagnostic scopes for confirmation and session close.
- `tests/MoveFaceTest.cpp`: confirmation timing with one-step Undo between trials;
  focused eligibility/source-revision/stale-action regression.
- `CMakeLists.txt`: register `move-face-startup-validation` on the existing target.
- This report: P1 measurements and safety rationale.

### Fresh before/after comparison

The previous investigation did not measure confirmation, so P1 captured a fresh
baseline with identical extended instrumentation **before** changing eligibility.
Same existing Release build, OCCT 7.9.3, synthetic fixtures and X11/OpenGL display;
three trials per solid in each run. Geometry setup and Undo restoration occur
outside the timed stages. Confirmation invokes the actual OK path, verifies one
new transaction, and restores the exact source with Undo before the next trial.
Only the necessary test targets were built, at `-j4`.

Raw complete runs:

- `/tmp/opendesigner-p1-before.log`
- `/tmp/opendesigner-p1-after.log`

Each cell below is the median in milliseconds, **before → after**:

| Solid | Context menu | Command startup | First +15 preview | Confirmation |
| --- | ---: | ---: | ---: | ---: |
| Box | 20.562 → 4.117 | 57.956 → 63.226 | 137.735 → 137.906 | 238.918 → 194.763 |
| Ellipse | 58.848 → 31.915 | 95.877 → 80.118 | 789.565 → 631.239 | 806.465 → 740.157 |
| Obround-hole | 73.919 → 11.176 | 72.180 → 68.299 | 279.427 → 289.039 | 349.923 → 359.719 |
| Spline | 132.096 → 76.316 | 108.629 → 89.103 | 2076.103 → 1745.954 | 1925.965 → 1967.754 |

Ranges (minimum–maximum milliseconds), to expose variability:

| Solid/stage | Before | After |
| --- | ---: | ---: |
| Box menu | 16.604–23.571 | 3.389–6.404 |
| Box startup | 57.405–89.168 | 60.576–72.824 |
| Box preview | 135.083–161.431 | 127.507–138.353 |
| Box confirmation | 188.087–256.174 | 189.366–213.744 |
| Ellipse menu | 50.490–99.027 | 30.085–34.982 |
| Ellipse startup | 69.906–138.148 | 67.118–111.643 |
| Ellipse preview | 782.721–1112.122 | 630.124–661.192 |
| Ellipse confirmation | 761.396–1012.509 | 710.682–752.371 |
| Obround menu | 47.515–74.052 | 10.691–20.537 |
| Obround startup | 64.611–74.558 | 60.634–77.489 |
| Obround preview | 256.086–318.465 | 275.506–297.090 |
| Obround confirmation | 347.120–372.600 | 335.127–364.098 |
| Spline menu | 108.693–193.323 | 42.669–83.357 |
| Spline startup | 85.059–121.510 | 86.395–145.980 |
| Spline preview | 1960.925–2651.150 | 1638.599–3008.096 |
| Spline confirmation | 1895.963–1960.291 | 1864.473–2060.926 |

Menu measurements are lower for all three trials of each solid and the work
reduction is deterministic. Do not interpret the wall-time ratios as guaranteed
speedup factors: these are small sequential samples on a variable-load display
host, not a controlled statistical benchmark. No startup, preview or confirmation
speedup is claimed. Their algorithms and measured validation counts are unchanged;
changes in those medians, including the lower ellipse preview, are not attributed
to P1. Confirmation still reconstructs/certifies the preview again.

### Expensive-call counts

Counts were identical across all three trials within each run. For each successful
planar menu, `singleSolid`, `validBRep` and self-interference `Perform` each change
from **2 to 1**; blend resolution also changes from **2 to 1**.

| Solid | Menu self-interference | Startup self-interference | Preview self-interference | Confirmation self-interference |
| --- | ---: | ---: | ---: | ---: |
| Box | 2 → 1 | 1 → 1 | 5 → 5 | 5 → 5 |
| Ellipse | 2 → 1 | 1 → 1 | 8 → 8 | 8 → 8 |
| Obround-hole | 2 → 1 | 1 → 1 | 5 → 5 | 5 → 5 |
| Spline | 2 → 1 | 1 → 1 | 6 → 6 | 6 → 6 |

Menu property measurements change from 5 to 1 for Box and 2 to 1 for the other
solids. The Box menu's primitive-recognition Common changes from 1 to 0. Preview
and confirmation material-equivalence Common counts remain respectively
3 / 5 / 3 / 6 for Box / ellipse / obround / spline. No material certificate
was weakened or skipped in a modeling operation.

### Focused validation

Ran only the selected tests with CTest `-j2`:

| Test | Result | Seconds |
| --- | --- | ---: |
| `move-face-startup-validation` | PASS | 2.14 |
| `elliptical-move-face-session` | PASS | 8.41 |
| `obround-move-face-session` | PASS | 2.65 |
| `spline-move-face-session` | PASS | 9.58 |
| `brep-certification-correspondence` | PASS | 0.40 |

The new regression compares menu availability to the unchanged standalone resize
policy on all four fixtures. It verifies independent startup self-interference
validation, invalid/open-solid refusal after changing the same Part, subsequent
valid revision acceptance, and preservation of source, identity and history.
It also creates a menu for a valid solid, replaces the same Part's source by an
open Face, and triggers the **old QAction**: startup refuses the current invalid
source. This final case was added after the first five-test pass; only the new
regression was rerun. The other four cases had no later source changes.

Both complete timing runs passed source-immutability, real Face-selection,
preview, confirmation transaction and exact-source Undo assertions.
`git diff --check` passed. No full suite, private-document fixture, commit, push,
PR or merge was used. All ten private untracked documents and prior work remain
untouched.

P1 stops here. The smallest next performance step is to investigate a
session-owned unchanged-preview commit proof: exact source, parameters and live
reference revision guards would be required before deciding whether confirmation
may reuse a previously certified candidate. No such reuse is implemented in P1,
and no Boolean/numerical optimization is proposed without separate approval.

## Performance P2 — session-local certified preview reuse (2026-11-15)

### What the preview proves

`previewMoveFace` first obtains an accepted geometry result through the existing
MoveFaceGeometry / reconstruction / independent material certification paths.
Only then does it create `FaceChange{before,after}` and display an AIS wrapper.
The display wrapper itself is not a certificate. P2 records a receipt only after
the geometry route succeeds, the nonzero candidate is the FaceChange's exact
result, presentation succeeds and the source is still unchanged.

The receipt is consumed only by OK on an ordinary planar **Face-normal Move
Face**. It does not apply to reference-driven/By Points directions, cylindrical
feature moves, blend-support edits, or the Pull adapter (its Move-only permission
is false). Those retain full confirmation, including live reference resolution.
This intentionally avoids claiming unproven reference or Fillet recipe identity.
No reconstruction, Boolean or numerical acceptance algorithm changes.

### Exact source and candidate guards

OCCT `IsSame`/`IsEqual` alone cannot prove source immutability: an aliased TShape
can be edited in place. P2 combines exact root/Face identity with session-local
**binary content snapshots**, not hashes or approximate geometry comparison:

- `BinTools::Write`, current binary format, stores exact B-Rep carrier/curve,
  trim, topology, location and tolerance data. Display triangulation is excluded;
  no serialization output is loaded back into modeling. No private data is logged.
- QDataStream records Part identity, name, color, visibility/activity, geometry
  kind and primitive parameters, all bounds, canonical placement and its form/
  scale, plus the actual AIS source placement. Source snapshots explicitly use
  double precision. These metadata fields are guards, not geometry evidence.
- The selected oriented Face, source root and source presentation must remain
  the same objects in the same context. Source snapshot content must match both
  the current Part and the session's original Part snapshot.
- Typed numeric input must parse and pass the existing validator; its exact
  value must match the preview's value. Direction, normal, pivot, reverse flag,
  Move-only reconstruction permission and Keep option must match the receipt.
- Current selection owners and Part selection must match the captured selection.
- The candidate, FaceChange pointer, before-state source snapshot and after-state
  snapshot must still match. The rendered wrapper must still own that candidate.

The receipt is not a persistent revision identifier or a general B-Rep
mathematical-equivalence checker. Distinct roots or changed representations are
not accepted merely because volume, dimensions or appearance agree. The guards
run synchronously on the existing GUI thread, with no event-loop yield before
`closeCommandSession(true)` consumes the existing certified FaceChange.

`applyFaceState` explicitly invalidates a live receipt for the target Part and
marks that session's source as revised, **even if identical geometry is restored**.
This covers source revision events without introducing global revision caches.
In-place tolerance changes that keep the same root are caught by binary content
comparison. Undo/Redo already cancel the current command before changing source
state. The receipt and snapshots die with Cancel, restart or session close and
are absent from native persistence.

A preview attempt clears the previous receipt first; failed certification or
invalid numeric input cannot retain a success. Direction/reference changes also
clear the receipt. A changed input, Face/selection, candidate or option takes the
full confirmation path; uncertain/unavailable proof never grants reuse. A known
source revision or changed source/frame refuses the stale operation and requires
Cancel/reselection, rather than rebuilding against an obsolete `faceBefore`.
An unavailable initial snapshot disables reuse and preserves full confirmation.
Future additions to relevant Part/session state must be included in these guards
or remain excluded from reuse.

### Changed files

- `src/CadViewport.h`: receipt and source guards owned by CommandSession only.
- `src/MoveFace.cpp`: exact snapshots, receipt recording/reuse guards, stale-source
  refusal and source-revision invalidation in applyFaceState.
- `src/CadViewport.cpp`: reuse before the existing full OK path; failed numeric
  input discards the receipt. Existing commit/history code is reused unchanged.
- `src/ReferencePicking.cpp`: clear receipt when changing Move Face direction.
- `tests/MoveFaceTest.cpp`, `CMakeLists.txt`: one focused preview-reuse scenario.
- This report: P2 safety rationale and measured results.

### Focused regressions

Incremental build targets: `OpenDesignerMoveFaceTests` and
`OpenDesignerBRepCertificationTests`, maximum `-j4`. Only selected CTests ran,
with `-j2`; no full suite or standalone application build.

| Test | Result | Seconds |
| --- | --- | ---: |
| `move-face-preview-reuse` | PASS | 23.30 |
| `move-face-startup-validation` | PASS | 2.21 |
| `elliptical-move-face-session` | PASS | 8.65 |
| `obround-move-face-session` | PASS | 2.48 |
| `spline-move-face-session` | PASS | 9.43 |
| `brep-certification-correspondence` | PASS | 0.42 |

The new scenario tests all four fixture classes with rotated/translated Part
placement. Immediate OK must commit the **same candidate handle** with no repeated
reconstruction, single-solid certification or material Common. Independent full
uncached Move Face results must remain materially equivalent. Identity, metadata,
placement, one-step Undo/Redo, native round trips, saved source during preview,
Cancel/restart and source immutability are checked.

Additional Box cases exercise changed displacement, reference mode, selected Face,
selection owner, Keep and reconstruction options; edited source and an identical
applyFaceState revision; canonical and display-only placement changes; in-place
vertex tolerance mutation with unchanged root; missing receipt, collapsed height
and invalid numeric input. Fallback must reconstruct independently or refuse
without committing. Live Undo/Redo cannot retain or revive a receipt.

### Before/after measurement

Baseline is the preserved, complete **P1-after** run with the identical timing
scenario (`/tmp/opendesigner-p1-after.log`). Its three trials per solid already
include confirmation; no unverifiable baseline was inferred or rerun needlessly.
The P2-after run is `/tmp/opendesigner-p2-after.log`, also three trials per solid,
same Release build, OCCT 7.9.3 and X11/OpenGL test environment.

Milliseconds, medians **before → after**. Sequence total is the median of paired
preview-plus-confirmation durations within each trial, not the sum of two
independent medians. User think time and diagnostic output between stages are
excluded.

| Solid | First preview | Confirmation | Preview + confirmation |
| --- | ---: | ---: | ---: |
| Box | 137.906 → 141.450 | 194.763 → 95.958 | 332.669 → 249.274 |
| Ellipse | 631.239 → 645.016 | 740.157 → 92.321 | 1382.495 → 739.376 |
| Obround-hole | 289.039 → 268.722 | 359.719 → 95.231 | 648.758 → 359.810 |
| Spline | 1745.954 → 1606.716 | 1967.754 → 94.076 | 3699.525 → 1700.722 |

Confirmation min–max ranges, before / after, milliseconds:

- Box: 189.366–213.744 / 91.805–132.881.
- Ellipse: 710.682–752.371 / 91.755–94.360.
- Obround-hole: 335.127–364.098 / 91.088–97.092.
- Spline: 1864.473–2060.926 / 94.006–96.467.

No first-preview speedup is claimed. It runs the same reconstruction and all the
same certification calls, with added snapshot guards. Snapshot time in trial 2
was 0.229–0.665 ms total during preview and 0.203–1.648 ms during confirmation.
Three sequential trials are not a broad statistical performance guarantee; the
confirmation work elimination is additionally verified by exact call counts.

### Reconstruction and certification call counts

Counts are identical across all three trials. First preview remains one
`previewMoveFace` and one `MoveFaceGeometry::move` for every solid. Its single-solid
and self-interference counts remain 5 / 8 / 5 / 6, and material-equivalence Common
counts remain 3 / 5 / 3 / 6 for Box / ellipse / obround / spline respectively.

At confirmation:

| Solid | Move geometry calls | Single-solid / self-interference | Material Common | Property measurements |
| --- | ---: | ---: | ---: | ---: |
| Box | 1 → 0 | 5 → 0 | 3 → 0 | 26 → 0 |
| Ellipse | 1 → 0 | 8 → 0 | 5 → 0 | 64 → 0 |
| Obround-hole | 1 → 0 | 5 → 0 | 3 → 0 | 30 → 0 |
| Spline | 1 → 0 | 6 → 0 | 6 → 0 | 126 → 0 |

All twelve P2 confirmation trials recorded exactly one certified reuse. These
zero counts mean reuse of the already certified result, not removed validation
from reconstruction or weakened material correctness.

The remaining confirmation cost is the existing close/commit/presentation path:
trial 2 `closeCommandSession` was 88.868–91.998 ms, including 24.685–27.522 ms
inside `redrawFacePreview`. Further UI/driver attribution requires finer scopes;
P2 does not claim an individual driver bottleneck. Initial preview Boolean
certification remains the principal geometry cost.

P2 stops here. The smallest next optimization investigation is finer profiling
of commit-time presentation, selection reactivation and redraw, before removing
any redundant display work. Reference-aware or blend-aware receipt reuse needs
its own identity/immutability proof and is not enabled here. No new editing
capability, precision/tolerance change, private document use, commit, push, PR
or merge occurred. All ten private documents and other user data remain preserved.
