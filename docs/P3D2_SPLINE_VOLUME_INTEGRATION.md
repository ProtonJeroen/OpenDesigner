# P3D.2 — direct knot-aware spline volume integration

2026-11-15. Changes are limited to BRepMeasurement's numerical-path selection,
focused measurement regressions and this report. Existing reconstruction,
correspondence, source/result policies and P1/P2/P3B/P3D.1 remain intact.

## Cause and existing implementation

BRepMeasurement already copied exact spline geometry, called
`BRepGProp::VolumePropertiesGK(...,1e-12,false,true,true)`, and checked the
returned error. Only a finite estimate above 1e-12 on a spline sweep triggered
an exact integration-copy `BRepBuilderAPI_NurbsConvert` followed by the same GK
call. No converted geometry was returned to the model.

OCCT's `IsUseSpan` option applies to **B-spline Faces** (installed
`BRepGProp.hxx` documentation), not to the internal basis knots of a
Geom_SurfaceOfLinearExtrusion carrier. The expensive first pass therefore did
not expose those basis spans through that option. It spent substantial adaptive
work and still failed the requested estimate before the existing conversion
made knot-aware integration available.

The actual private source's first-pass estimate was 7.417967541286779e-11.
Its existing converted retry and the controlled direct converted pass both
returned 1.524533190606979e-15, with identical mass and centroid. The old first
pass was not accepted as sufficiently precise.

## Stage A — controlled comparison before production selection changed

A temporary headless diagnostic reused existing fixtures, the current shared
measurement API and the exact existing OCCT copy/conversion/GK sequence. No
alternative numerical integrator, model refit or application behavior was added
for this comparison. Three trials per source; all inputs remained immutable.
Four synthetic sources additionally had independent analytic/polynomial volume
oracles. Current versus direct-result mass was compared at 1e-12 relative and
centroid at 1e-9 absolute.

Median source-measurement times, milliseconds:

| Source | Existing | Span-first trial | Decision |
| --- | ---: | ---: | --- |
| Box | 1.125 | 1.452 | Keep analytic path; trial uses that same path |
| Ellipse | 0.904 | 0.783 | Keep analytic path; trial uses that same path |
| Rotated obround with hole | 2.131 | 2.210 | Keep analytic path; trial uses that same path |
| Single-span cubic fixture | 1.234 | 8.495 | Do not convert first |
| Private Part 2 | 1342.206 | 59.445 | Convert eligible multi-span sweeps first |

The private source went from two attempts/one retry to one attempt. Its
converted mass and centroid agreed exactly with the established retry result
in all three trials. For the simple spline, both error estimates were below
1e-12 (1.151e-15 versus 2.017e-15), but conversion cost more; its mass difference
was only 1.64e-11 mm³ and centroid difference 3.97e-15 mm. This evidence ruled
out blanket conversion of all spline-containing shapes.

Before preview timings for all four synthetic scenarios were recorded with the
unchanged integration choice. The approved, preserved P3D.1 three-trial private
preview log is the before baseline; it was verified rather than unnecessarily
rerun for another 37-second preview sequence. Stage A also directly measured
the private source three times as above. Production selection changed only
after the controlled numerical comparison passed.

## Which of the 26 readings repeat?

The preserved private preview has 26 adaptive volume readings and one analytic
zero-material reading. Instrumented oriented input identity and explicit copy
provenance establish:

- 17 distinct exact oriented input identities for those 26 adaptive readings.
- 16 groups after known exact-copy provenance; this is not carrier matching or
  deduplication by volume.
- The same edit-region graph is read four times; one candidate graph three
  times; one normalized result graph three times plus two readings of its
  explicit isolated copy; one original neighborhood graph twice. The remaining
  twelve identities are read once.

The actual existing Common/difference certificates, rather than equal masses,
establish three equivalent-material families:

| Material family | Readings | Distinct input identities |
| --- | ---: | ---: |
| Source/protected source material and witness/Common results | 7 | 6 |
| Intended added region and directional difference/Common results | 6 | 3 |
| Final candidate, normalization/result witnesses and Commons | 13 | 8 |

Some objects have identical material but different topology/trims/provenance.
Identity and material-family classification must remain distinct. P3D.2 does
**not** reuse any measurements or remove independent checks across these
objects or layers. There is no persistent, global or per-operation property
cache added in this phase.

## Stage B — narrow implementation

Changed files:

- `src/BRepMeasurement.cpp` only for production behavior.
- `tests/SplineMeasurementTest.cpp` in the existing target.
- This report. No CMake, UI, session, reconstruction or correspondence changes.

The new selector uses only current exact B-Rep geometry. At least one Face must
have an exact linear-extrusion carrier with a nonrational, nonperiodic B-spline
basis of degree at most 3, and an internal knot **strictly inside that Face's
trimmed U domain**. An internal knot outside the actual trim is insufficient.
This is a measured numerical opportunity, not a new modeling-eligibility rule
or a universal performance guarantee for every possible multi-span profile.

For that case, after the existing source validity, closed-volume extraction,
finite-domain and isolated-copy preparation:

1. Reuse the existing exact NURBS conversion on the disposable integration copy.
2. Require valid converted topology and finite bounded domains.
3. Run the existing span-aware GK routine at exactly 1e-12.
4. Require finite nonnegative error estimate at or below 1e-12, finite mass and
   a finite centroid for nonzero mass.
5. If the direct conversion/integration is unsuccessful, clear its temporary
   properties and run the original unconverted-first/retry/refusal path. A
   failed direct attempt is never an empty-set certificate. The original
   retry is retained, including its validity/domain/precision checks.

Analytic geometry, single-span sweeps, generic B-spline surfaces, other sweep
representations and out-of-scope bases keep their existing numerical path.
Generic B-spline Faces already expose spans through the existing GK option.
Rational/periodic/higher-degree/revolved geometry is not newly refused by this
selector: it remains on its prior measurement path and policies. No surface or
curve is canonicalized, healed, fitted or returned from integration to modeling.

Surface-property integration, adaptive area precision, analytic integration,
empty difference/contact handling, original source validity and operation-specific
positive-solid rules remain unchanged. Numerical error diagnostics were added
as disabled-by-default timing events. No public API was widened.

## Independent correctness and focused tests

Built only affected existing targets, incremental maximum `-j4`; CTest `-j2`.
No full suite. Focused results:

| CTest | Result | seconds |
| --- | --- | ---: |
| spline-geometric-certification | PASS | 9.13 |
| brep-certification-correspondence | PASS | 0.43 |
| spline-move-face-geometry | PASS | 11.49 |
| move-face-preview-reuse | PASS | 32.03 |
| spline-move-face-session | PASS | 11.63 |
| boolean-operations | PASS | 72.83 |

The measurement target was rerun after adding an explicit equal-volume/disjoint
**multi-span** material assertion; no application code changed for that addition.
Existing tests cover Box/Cylinder analytic oracles, exact cubic polynomial area,
volume and centroid, rotated and trimmed spline geometry, two holes, an internal
cavity, split/normalized Faces, equivalent construction routes, disjoint equal
volumes, empty Cut/Common/contact geometry, source immutability and invalid/open/
unbounded refusal. Empty sets remain valid zero measurements while positive-solid
validators still refuse them. Source and result material differences remain
independent Common/Cut certificates.

New path assertions verify:

- Single-span cubic stays on one unconverted GK attempt, without conversion.
- Two-span polynomial extrusion uses one direct GK attempt, no initial GK/retry.
- A trimmed interval inside just one span keeps the old path even though its
  underlying basis has another knot outside that interval.
- Polynomial volume 26100 mm³, and trimmed-span volume 10920.96 mm³, plus
  centroid agreement with independently invoked OCCT integration-copy spans.
- Multi-span two-hole profile and independently cut holes have equivalent
  material: volume 26100-500π mm³. An additional internal cavity subtracts 120
  mm³ and is not equivalent to the uncavitated model.
- A translated multi-span solid retains volume but fails material equivalence.
- Returned estimates are finite and within 1e-12; invalid inputs clear output
  and return failure rather than valid zero material.

No observed direct conversion failed in these accepted cases, so the new
emergency legacy-fallback branch was not forced by a test injection. The
existing precision-refusal guard is retained explicitly. The controlled actual
source's insufficient first estimate was never used as the accepted result.

## Actual private model acceptance

Read-only native load, Part 2 only; three existing GUI-session trials. All pass:

- Height 50 → 65 mm, positive certified volume approximately 2140981.0377 mm³,
  independently compared with measured section area × 65 at 1e-8 relative.
- Valid closed manifold correctly oriented result, existing 1e-6 topology ceiling.
- Original spline degrees, knots, multiplicities and coefficients retained
  modulo the certified axial carrier-origin shift; no refit.
- Unique wall correspondence, unchanged bottom cap and unchanged source B-Rep.
- Correct added region, no removed material, protected region preserved. These
  are the same independent material oracles used by the before acceptance run;
  no before/after B-Rep byte-identity claim replaces material certification.
- Part identity and placement unchanged; one-step Undo/Redo; all three OK actions
  use the P2 certified preview with no repeated numerical/Boolean certification.
- Source file SHA-256 unchanged; all ten private documents retained. No private
  geometry, coordinates or document copies are added to repository fixtures.

All 78 direct estimates across the three previews lie between
1.38315882e-15 and 1.52453319e-15. No direct fallback or retry runs on this model.
The independent material equivalence and topology policies remain unchanged.

## Before/after performance

Three trials per full-preview scenario. Release/current OCCT, same existing GUI
timing framework. Private before values are the approved P3D.1 log; after uses
the identical read-only document-session scenario. Raw derived timing logs stay
outside the repository in /tmp, not as private geometry fixtures.

Private Part 2, median milliseconds:

| Stage | Before | After |
| --- | ---: | ---: |
| Move eligibility | 13.800 | 14.337 |
| Command initialization | 157.885 | 158.016 |
| Fully certified first preview | 37333.100 | 4368.160 |
| All volume measurement work | 33883.593 | 843.275 |
| Initial unconverted GK volume work | 33100.441 | 0 |
| Knot-aware GK (old retry / new direct) | 541.632 | 588.629 |
| Exact integration-copy conversions | 156.298 | 167.010 |
| Boolean build kernels, all 18, median summed time | 2634.372 | 2697.368 |
| Six material-equivalence checks, inclusive | 25421.900 | 2482.430 |
| Self-interference checks, five | 600.667 | 624.040 |
| Confirmation, certified-preview reuse | 110.124 | 130.021 |

Inclusive certificate/measurement rows overlap; do not add them together.
Preview trials before: 37333.100, 37210.400, 37971.700 ms. After: 4418.100,
4368.160, 4349.770 ms. The sampled private median is 8.55× faster (about 88.3%
lower), with no extrapolation to untested geometry or platforms. Eligibility,
startup and confirmation have unchanged work policies; no improvement is claimed
for their small/run-dependent differences.

Per preview, 27 volume measurements remain. Integration attempts change from
53 (26 initial GK + 26 retries + one analytic) to 27 (26 direct GK + one analytic).
Conversions remain 26; retries fall 26→0. Individual old initial GK calls across
three previews: min/median/max 1186.032/1274.591/1963.246 ms. New direct calls:
6.040/7.516/100.741 ms. These are different material/input shapes; the per-call
median is not the average cost or a source-only comparison.

Every preview retains six material Commons, eight other Face/set Commons,
three Cuts, one Fuse, five self-interference checks, and eight P3B overlap reuse
hits. Every confirmation retains one P2 reuse and zero reconstruction/volume/
Boolean calls. The savings come from removing unnecessary initial integrations,
not from removing independent material evidence.

Non-selected synthetic controls, first-preview medians (ms):

| Solid | Before | After | New direct calls |
| --- | ---: | ---: | ---: |
| Box | 234.046 | 225.176 | 0 |
| Ellipse | 845.179 | 962.570 | 0 |
| Rotated obround-hole | 370.429 | 433.867 | 0 |
| Single-span spline | 1692.736 | 2148.702 | 0 |

The slower observed control medians are reported, not hidden or described as
improvements. Their integration/reconstruction/Boolean call counts and numerical
paths are unchanged. For example, the single-span spline's independent material
Common build time rose 807.393→994.440 ms and Cuts 314.836→398.191 ms, outside
the new selection branch; GK work rose 26.355→34.800 ms with the same 26 calls.
These runs do not isolate host variation from minor selector/diagnostic overhead,
and do not establish a universal control-performance claim. No unchanged control
was repeatedly benchmarked to select a more favorable result.

## Remaining work and stop

The private preview is still seconds, mainly unchanged Boolean material work
(~2.7 s), volume properties (~0.84 s), and self-interference (~0.62 s). Do not
remove those independent checks merely because equivalent results recur.

If more performance is required, the smallest next investigation is bounded
reuse of duplicate property readings on an explicitly owned immutable graph,
with matching numerical policy, within one evaluation only. First measure the
remaining repeated-reading cost and prove ownership/invalidation; do not invent
cross-session identity, skip Common checks or add a global cache. No such reuse
is implemented here. Further numerical/Boolean/session optimization needs
separate approval.

P3D.2 stops here. No commit, push, PR, merge, reset or clean.
