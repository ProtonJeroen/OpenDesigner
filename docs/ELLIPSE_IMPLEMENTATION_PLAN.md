# Exact Ellipse and EllipticalArc support — v0.4.2 phase 6A

Status: phase 6A.1 merged; phase 6A.2 interactive drawing and basic snapping
implemented, 2026-10-24. The baseline analysis below is historical. The latest
approved scope separates drawing (6A.2) from oblique 3D projection (6A.3).

## 1. Investigation baseline and scope

The inspected checkout is `develop` at `1197e76` (interactive projection PR #43
merged). At investigation start there were no tracked modifications; `hole.zip`
was the only untracked entry. It was not opened or changed. This plan is the only
new file. No branch switch, commit, push, build or test execution was performed.

The findings below come from the actual sources and tests, checked against
`SKETCH_EDITOR.md`, `SKETCH_PROJECTION.md`, `NATIVE_DOCUMENTS.md`,
`CONSTRUCTION_GEOMETRY.md`, `COMMAND_SESSIONS.md`, `SELECTION_CONTEXT.md` and
`ROADMAP.md`. No `AGENTS.md`, `ARCHITECTURE.md` or `DECISIONS.md` was found in the
inspected project tree; no ancestor `AGENTS.md` was found either.
Installed OCCT headers identify version 7.9.3.

Distinguish three kinds of statements:

- **Existing implementation:** observed OpenDesigner behavior and contracts.
- **OCCT evidence:** API/source behavior verified against the installed headers
  and upstream OCCT 7.9.3 sources linked below.
- **Proposal:** new OpenDesigner policy, representation or scope. No new claim
  about original SolidDesigner v16 interaction is made here.

Recommended goal: exact analytic ellipse values in the existing Sketch collection,
initially produced by Project 3D Geometry. Support both Construction and Profile
roles in the model, while projection continues to create Construction only.
Interactive ellipse drawing belongs to phase 6A.2, alongside the next useful
ellipse editing/projection integration. General splines, constraints, silhouette
extraction and automatic profile reconstruction remain separate work.

## 2. What the current code supports

| Area | Observed implementation and consequence |
| --- | --- |
| Entity model | `src/SketchGeometry.h/.cpp`: `Geometry` is a value variant of Segment, InfiniteLine, Circle, Arc and ConstructionPoint. Entity UUID, owning Workplane UUID, role, name, visibility and selectability are already independent of source topology. Ellipses are absent. |
| Shape conversion | `Sketch::shape` creates exact OCCT edges/vertices in the owner frame. Its final visitor branch assumes Circle/Arc. `valid` assumes every remaining curved alternative has a radius; `typeName` falls back to Arc. Merely appending types is unsafe. |
| Presentation and selection | `src/SketchTools.cpp`: AIS_Shape from `Sketch::shape`, dashed pink Construction and solid white Profile, blue previews, typed entity selection and visibility/lock filtering. `src/MainWindow.cpp` builds UUID-keyed rows under the owning Workplane using `typeName`. These generic paths can be reused. |
| Workplanes | `src/ConstructionGeometry.h/.cpp`: validated direct, orthogonal `gp_Ax3` frame. Analytic origin differs from presentation extent/displayOffset. Moving the owner carries local XY geometry without rewriting it. |
| Projection | `src/SketchProjection.h/.cpp`: widget-independent vertex, bounded line, parallel circle/arc and actual face-wire projection. Tilted circles fail with NonCircularProjection; native analytic elliptical edges fail with NonlinearEdge. No fitting is attempted. |
| Face boundaries | Every outer/inner wire is traversed and every oriented edge occurrence checked. Projected joins/closure are validated before deduplication. Stored circular Arc endpoints receive an extra check. There is no silhouette or guaranteed closed Profile output. |
| Snapping | `src/SketchSnapping.h/.cpp`: analytic lines/circles/arcs, point targets, contains/intersect/nearest/tangent operations and a screen-pixel catch radius. Circle-specific radial nearest-point and radius-based angle tolerances cannot be reused for ellipses. |
| Overdraw | `src/SketchOverdraw.h/.cpp`: intersections split sources into finite Segment/Arc pieces, with forward/reverse traversal. Only points are filtered out of curve sources. A remaining fallback uses `std::get<InfiniteLine>`; new ellipse values would reach an invalid alternative unless handled explicitly. |
| Construction tools | `src/SketchConstruction.h/.cpp` limits Offset to straight references and Common Tangent to Circle/Arc through explicit predicates. Preserve these guards; an ellipse is not a circular reference. |
| Interactive projection | `src/SketchProjectionTools.cpp` adapts real OCCT owners, freezes the target, previews kernel results and revalidates on OK. `SketchProjectionPanel` is type-neutral. It needs no second command or storage system. |
| History | `CadViewport::SketchChange` stores full before/after Entity vectors. `SketchTools.cpp` confirms once; Cancel discards candidates. `CadViewport.cpp` restores values and clears selection on Undo/Redo. New copyable value types fit this design. |
| Storage | `src/NativeDocument.h/.cpp`: one BinXCAF file. Current capability is v4, with v3 output without points. Numeric tags 0–4 and fixed parameter counts are persisted. Readers reject unknown versions/types and stage values before replacing the document. |

The current CMake configuration includes projection, storage, drawing, Overdraw,
construction and 3D session regressions. Earlier milestone results in the docs
are historical; this investigation does not assert a new passing test run.

## 3. Proposed exact local geometry

Append two alternatives, preserving all existing variant/tag meanings:

| Type | Proposed value fields | Persisted tag / parameters |
| --- | --- | --- |
| Ellipse | center XY, majorRadius, minorRadius, axisAngle | 5 / `[cx, cy, a, b, axisAngle]` |
| EllipticalArc | Ellipse support, startParameter, sweepParameter | 6 / the same five support values followed by start and signed sweep |

Retain the existing Entity fields. Do not store OCCT handles, source IDs,
projection matrices, dependencies or sampled points as document geometry.
Use an explicit persisted-tag mapping in NativeDocument rather than extending
an implicit fallback from `geometry.index()`; old tags must remain 0–4.

Define `u=(cos(axisAngle), sin(axisAngle))`, `v=(-u.y, u.x)` and
`E(t)=center + a*cos(t)*u + b*sin(t)*v`. This is a right-handed local frame.
The parameter is the ellipse parameter (eccentric angle), **not** the polar angle
of the position, not arc length and not a Workplane rotation. Positive sweep is
CCW in Workplane XY. `atan2(localY, localX)` alone is wrong for ellipse trimming;
use `atan2(localY/b, localX/a)` in the ellipse frame.

Proposed invariants and shared helpers:

- Finite center, radii, orientation and parameters; `a >= b > 1e-9`, matching
  the current minimum-radius scale. Projection applies its stricter requested
  tolerance and representability checks as well.
- A full ellipse has its own type. An arc has nonzero signed sweep with
  `1e-12 < abs(sweep) < 2*pi`; a nearly full arc remains an arc.
- Canonical creation can use axisAngle in `[0, pi)`. Changing the axis sign
  requires shifting the arc parameter by pi. Swapping radii requires changing
  the parameter frame, not just exchanging two numbers.
- Permit equal radii as a valid ellipse value to avoid singular frame logic at
  the circular limit. Do not coerce genuinely oblique results to Circle merely
  because radii differ by less than a user tolerance. Keep the established
  certified parallel-circle path and verify near-circular ellipse results.
- Readers validate without normalizing or rewriting persisted doubles. Value
  equality and Save/Open roundtrips must preserve confirmed parameters exactly.
  Geometric equivalence is a separate tolerance-aware calculation.
- Add shared evaluate, derivative, endpoints, inverse parameter/domain,
  reversal and supported-curve predicates. Reverse an EllipticalArc by moving
  start to `start+sweep` and negating sweep; never choose the complementary arc.
- Replace generic visitor fallbacks with explicit alternatives and an exhaustive
  compile-time failure for future unsupported types.

Construct world-space `gp_Elips`/`Geom_Ellipse` from the local center, rotated
owner X/Y axes and radii. Use `BRepBuilderAPI_MakeEdge` for the full ellipse and
trimmed parameter range; reverse negative-sweep edge orientation just as Arc
does today. AIS may discretize for display/picking; those samples must never
replace analytic document values.

## 4. OCCT projection evidence and recommended algorithm

### Verified OCCT behavior

`GeomProjLib::ProjectOnPlane` accepts a plane, projection direction and
KeepParametrization flag. `ProjLib_ProjectOnPlane` also offers a normal-direction
constructor. In OCCT 7.9.3 its circle/ellipse branch projects the source basis;
without parameter preservation it derives canonical conic axes. With parameter
preservation, nonorthogonal projected basis directions can trigger approximation.
Degenerate/numerically unsuitable cases can also use its B-spline fallback.
Near-equal radii may be classified as Circle using OCCT's own tolerance.
Consequently, calling the general API does **not** guarantee our exact analytic
ellipse contract. See the [7.9.3 implementation](https://github.com/Open-Cascade-SAS/OCCT/blob/V7_9_3/src/ProjLib/ProjLib_ProjectOnPlane.cxx#L611)
and [wrapper](https://github.com/Open-Cascade-SAS/OCCT/blob/V7_9_3/src/GeomProjLib/GeomProjLib.cxx#L289).

The public [ProjectOnPlane contract](https://dev.opencascade.org/doc/refman/html/class_geom_proj_lib.html)
describes parameter preservation; the online manual may be newer than the
installed library. `Geom2d_Ellipse.hxx` locally confirms the center/axes/radii,
periodic cosine/sine parameterization used in this proposal.

### Proposed OpenDesigner computation

Keep using `BRepAdaptor_Curve` as the authoritative exact source, with accumulated
OCCT locations already applied. Reuse `local` and `localDirection` so the outer
Part transform is applied once and common translations cancel before small
coordinates are added. Never compose a placed shape and apply Part placement a
second time. Preserve support for uniform scale and reflections already accepted
by `gp_Trsf`; no new nonuniform Part transform is proposed.

Derivation for a circle, independently of the general projection API:

1. Write its exact placed curve as `C + R*(U*cos(t)+V*sin(t))`. Project the center
   into Workplane local XY. Form the two projected radius columns `p` and `q`
   using the linear part of the same placement/frame calculation.
2. Form `A=[p q]`. The projected curve is exactly
   `c + A*[cos(t), sin(t)]`. A nondegenerate oblique circle is therefore an
   ellipse; for orthogonal projection its radii are `R` and
   `R*abs(sourceNormal dot targetNormal)` after uniform placement scale.
3. Compute a stable 2x2 singular-value/principal-axis decomposition. Use scaled
   arithmetic, long-double intermediates and a stable small-singular-value
   calculation (for example `abs(det(A))/largestSingularValue`) rather than
   subtracting nearly equal eigenvalues. Build a direct canonical ellipse basis
   `B=[a*u b*v]`.
4. Recover the orthogonal phase map `Q=B^-1*A`. Its determinant gives the sign
   `s`, and its first column gives phase `phi`: the new ellipse parameter is
   `tNew=s*t+phi`. Verify orthogonality/reconstruction, including sign choices.
5. Retain the exact source trim interval and edge orientation. Forward edges
   start at the original first parameter; reversed edges start at last. Set
   `startNew=s*startSource+phi` and
   `sweepNew=s*(last-first)*(reversed ? -1 : 1)`. Normalize only start, retaining
   signed unwrapped sweep. Full period classification uses the source analytic
   period, not shared endpoint topology or distance between endpoints.
6. Verify stored center/basis conversion and angle rounding. Bound the residual
   of the reconstructed cosine/sine coefficients over the entire curve, not
   just its endpoints. Independently compare both stored arc endpoints against
   projected B-Rep endpoints. Refuse when the requested tolerance cannot be met.

Native `GeomAbs_Ellipse` edges can reuse this algorithm: use source major/minor
radii for the two columns instead of a common R. This is needed for actual
elliptical face boundaries, not only tilted circular ones. Reject unrecognized
Bezier/BSpline approximations rather than inferring an ellipse from appearance.

Rank-one/zero projection, tolerance-sized minor radius, invalid intervals,
nonfinite values or excessive conditioning/rounding error fail with explicit
CollapsedProjection/NumericalFailure diagnostics. Do not replace edge-on conics
with lines or points. An ellipse having one very small axis must not disappear
silently from a mixed batch.

Recommendation: use the direct analytic coefficient method as the production
path to preserve existing large-translation precision and parameter semantics.
Use OCCT conic projection with parameter preservation disabled as an independent
test oracle for well-conditioned cases, inspecting the actual result type and
rejecting any approximate fallback. Endpoint-only conversion through the general
API is insufficient to prove which arc route was retained.

## 5. Face boundaries, duplicates and transaction semantics

Extend the existing edge classifier to accept analytic Ellipse as well as Circle.
Keep all face/wire validation, actual inner contours, oriented occurrence checks,
seams and certified pole handling. A tilted disk can then yield an Ellipse; a
tilted annulus yields two. Mixed line/circle/ellipse boundaries must use the same
edge conversion, with no independent face fitting path.

Generalize stored-endpoint checks from Arc to every bounded curve, including
EllipticalArc. Verify adjacent joins and complete projected closure before any
deduplication. One bad inner edge rejects the entire selected batch with source,
wire and edge diagnostics. Preserve the planar-face edge-on guard. Nonplanar
faces still mean projection of actual boundaries, never silhouettes or a promise
of a simple, closed material region after seam suppression.

Extend duplicate/overlap logic **before enabling conic output in the GUI**:

- Compare elliptical support independently of axis sign, periodic start,
  traversal direction and equivalent parameter frames. Near-circular support
  comparison must not rely solely on axisAngle; use the symmetric conic/basis
  representation with conditioning-aware geometric residual checks.
- Map both arc domains into a shared support parameter frame before comparing
  intervals, including seam splitting. Duplicate reversed arcs are no-ops;
  complementary arcs are distinct; full ellipse versus proper arc is overlap.
- Equal-radius Ellipse and Circle need explicit cross-type equivalence/domain
  handling, rather than allowing duplicate circular supports under two tags.
- Preserve existing policy: compare Construction objects on the target owner,
  including hidden/locked references; Profile and other owners are distinct.
  Deduplicate first, then reject nonzero shared intervals without trimming.
  Crossing/tangent isolated contacts are not partial overlaps.
- Entirely duplicate batches stay successful no-ops. Any source/overlap error
  leaves additions empty. Confirmed entities remain independent after source
  edits/deletion; one successful batch remains one SketchChange.

## 6. Sketch-tool integration and safe scope

| Consumer | Required behavior |
| --- | --- |
| Rendering/browser | Reuse AIS edge presentation, colors, stable IDs, locks and Workplane hierarchy. Add correct type labels and exact transformed bounds. No separate ellipse entity registry. |
| Basic snaps | Center, EllipticalArc endpoints and parameter midpoint; retain pixel catch radius and hidden/locked/owner filtering. Parameter midpoint follows the existing Arc convention; it is not equal arc-length midpoint. |
| Contains/nearest | Evaluate the implicit/support equation and trimmed parameter domain. Use ellipse-frame scaled coordinates. Nearest must minimize Euclidean distance, not radial scaling; include trimmed endpoints. OCCT `Geom2dAPI_ProjectPointOnCurve` supplies extrema candidates; check failure and all eligible minima. |
| Intersections | Line/ellipse, circle/ellipse and ellipse/ellipse, with arc-domain filtering, tangencies, coincidence and periodic duplicates. Reuse OCCT analytic conic intersectors where certified; distinguish actual shared support from tolerance-near tangential segments. Never use screen polylines. |
| Tangent snapping | Solve on the analytic ellipse and filter the arc domain. Direction at an on-curve anchor uses the derivative, not the circle radial formula. This does not add an ellipse Common Tangent construction command. |
| Overdraw | Initially explicit capability gating with a clear notice for unsupported conics, preserving valid existing line/circle workflows. Do not let an ellipse enter the InfiniteLine fallback or silently become circular. Full ellipse Overdraw is a separately testable step below. |
| Offset/Common Tangent | Keep straight-only Offset and circle/arc-only Common Tangent predicates; add refusal tests for both new types. Do not claim an offset ellipse is generally another ellipse. |
| Drawing | Existing Line/Circle/Arc tools keep their behavior. Phase 6A.2 must add independent Ellipse and Elliptical Arc commands using the approved flow below, not only display projected ellipses. No new drawing tool is implemented in 6A.1. |
| Projection session | Reuse current kernel API, transient owners, stable preview candidates, revalidation, all-or-nothing OK and Cancel. Update diagnostics/help text; no new command-session kind. |
| History/document lifecycle | Copyable values in before/after snapshots; unchanged dirty semantics. Save excludes previews and keeps history/selection; New/Open reset sessions/history. Failed Open/Save preserves current state. |

For geometric queries, exact means an analytic stored curve evaluated within a
declared floating-point tolerance. Numerical root solving on that curve is
acceptable; visual fitting and sampled model curves are not. Nearest and
intersection failure must not be reported as a fabricated valid solution.
Review `Geom2dAPI_InterCurveCurve`/`Geom2dInt_GInter` point **and segment** results;
the installed header documents tolerance-sized tangential intersection segments.
Do not classify every such segment as a genuine coincident-ellipse overlap.

Proposed full Overdraw extension, after query support: cut Ellipse into bounded
EllipticalArc pieces at actual intersections; seed bounded arcs with endpoints;
sort cuts by signed ellipse parameter and filter domains. Require at least two
distinct cuts for a full ellipse, matching current full-circle policy. Preserve
minor/major routes, reverse traversal, explicit ambiguous candidate selection,
green hover/blue choices, Back/New start/Cancel, one-step confirmation and source
preservation. Extend `directedPiece`, hit testing and candidate descriptions.
Do not use radius-times-angle as ellipse arc length for tolerances; use bounded
speed/residual checks. This extension needs its own acceptance gate.

## 7. Native storage: propose schema v5, keep v1–v4 readers

New geometry cannot safely be placed under v3/v4: the existing readers explicitly
reject tags above their supported maximum. Do not reuse old tags, discretize an
ellipse or omit it to retain a legacy root marker.

Recommended single-file BinXCAF policy:

| Content of a current-capability snapshot | Written schema |
| --- | --- |
| Existing content without 2D points or elliptical entities | v3, as today |
| At least one ConstructionPoint, no elliptical entities | v4 |
| At least one Ellipse or EllipticalArc, with or without points | v5 |

Retain explicit supported legacy requests and their existing validation rules;
requests below v5 containing ellipses must fail before touching the destination.
Centralize required-schema calculation so storage and viewport import checks
agree. Removing the last ellipse can permit a later v3/v4 save without loss.

Introduce a v5 root marker/version integer and in-memory capability 5; preserve
recognition of every v1–v4 marker. v1/v2 initialize an empty sketch collection,
v3 loads original curves, v4 additionally loads points. Upgrade only in-memory
capability on load, retaining values/IDs/owner frames unchanged. Continue rejecting
unknown future roots/versions, unknown tags, inconsistent markers, wrong array
sizes, invalid radii/domains/roles, nonfinite values and orphan/duplicate IDs.
Use existing sketch group metadata and 5/7-double records for tags 5/6. The older
application will safely refuse a v5 root; it cannot read new ellipses.

Keep atomic QSaveFile replacement, staging, confirmed-only snapshots and
transactional load. Verify the actual root integer/marker in tests: loaded
`schemaVersion` is the current capability, not the file's original disk schema.

## 8. File-by-file change inventory for later implementation

These are planned edits, not edits made by this research task.

| Existing files | Planned change or explicit review boundary |
| --- | --- |
| `src/SketchGeometry.h`, `src/SketchGeometry.cpp` | Append types, explicit visitors, validation, names, evaluation/domain/reversal helpers and exact OCCT edge conversion. |
| `src/NativeDocument.h`, `src/NativeDocument.cpp` | Capability/root v5, old v4 marker retained, explicit tags/array sizes, conditional output schema, downgrade guards and staged validation. |
| `src/SketchProjection.h`, `src/SketchProjection.cpp` | Analytic conic classifier/conversion, phase mapping, numerical certificates, face endpoint checks, support/domain duplicates and overlap, revised diagnostics/contracts. |
| `src/SketchSnapping.h`, `src/SketchSnapping.cpp` | Elliptic contains/parameter/nearest/intersections/tangents and snap targets, without changing pixel priorities. Add explicit query-failure reporting if the current API cannot distinguish failure from no intersection. |
| `src/SketchOverdraw.h`, `src/SketchOverdraw.cpp` | Mandatory safe type gating first; later EllipticalArc graph pieces, reversal, domain cuts and failure propagation. |
| `src/SketchOverdrawTools.cpp` | Elliptical candidate labels, parameter/sweep descriptions and regression review of green/blue preview and hit picking. Required for full ellipse Overdraw. |
| `src/SketchConstruction.cpp`, `src/SketchConstructionTools.cpp` | Audit every `get`/capability path; retain explicit straight/circular eligibility. Only change where new-type safety/diagnostics require it. |
| `src/SketchTools.cpp` | Audit generic AIS, selected IDs, preview, delete and snapshots; adjust only where ellipse-specific presentation or naming is required. |
| `src/CadViewport.cpp`, `src/CadViewport.h` | Update snapshot import schema/content guards; reuse SketchChange and existing source-selection state. No second history manager. Header changes only if needed for shared query diagnostics. |
| `src/SketchProjectionTools.cpp`, `src/SketchProjectionPanel.cpp` | Review revalidation/value comparisons; revise supported-type messages if needed. Panel layout and positioning remain unchanged. |
| `src/SketchDrawing.cpp`, `src/SketchDrawingPanel.h/.cpp`, `src/MainWindow.cpp` | Review all type assumptions and source eligibility; keep existing drawing tools. Browser type names already flow through SketchGeometry. No new ellipse drawing actions in the foundation. |
| `src/ConstructionGeometry.h/.cpp` | Reuse validated owner frames; no new construction-object type or persistent reference required. |
| `CMakeLists.txt` | Register focused conic/storage/query/session tests; existing OCCT dependencies already include TKG2d/TKGeomBase/TKGeomAlgo. Verify linking when concrete APIs are selected. |
| Existing `tests/Sketch*Test.cpp`, `tests/NativeDocument*Test.cpp`, `tests/SelectionContextTest.cpp` | Extend tests below and replace ellipse-as-unsupported fixtures with genuinely unsupported geometry. |
| `docs/SKETCH_EDITOR.md`, `docs/SKETCH_PROJECTION.md`, `docs/NATIVE_DOCUMENTS.md`, `docs/ROADMAP.md`, `docs/GUI_TEST_PLAN.md`, `README.md` | After implementation, document exact supported types, schema compatibility, tool capability limits and actual validation results. Do not mark planned work complete now. |

Proposed new files: `tests/SketchEllipseTest.cpp` and
`tests/SketchEllipticalProjectionTest.cpp`. A small shared
`src/SketchConics.h/.cpp` is justified if evaluation, OCCT conversion and canonical
support/domain logic would otherwise be duplicated across projection, snapping
and Overdraw. It must remain a pure geometry helper, not another document store.

## 9. Regression and acceptance tests

### Model, rendering and queries

- Exact local values, both roles, stable IDs, owner validation, nonfinite/zero/
  negative/swapped radii and invalid sweeps; no variant fallthrough.
- Full ellipse, positive/negative EllipticalArc, small/large spans, arbitrary
  principal-axis rotation, axis sign/phase equivalence and 0/2pi crossings.
- Evaluate known endpoints and derivatives; reversal twice restores the curve;
  distinguish ellipse parameter midpoint from polar/equal-length midpoint.
- BRepAdaptor confirms exact `GeomAbs_Ellipse` edges, trims and orientation;
  moved/rotated Workplane gives expected world positions without local rewrites.
- AIS/browser selection, rename, hide/lock, owner hide/lock/delete and restoration,
  Fit Selected/All, typed 3D isolation, normal/200% display scaling.
- Line/ellipse intersections (0/1/2), circle/ellipse and ellipse/ellipse
  (including four crossings), tangency, shared support, disjoint/overlapping
  arc domains, seam contacts and near-tangent numerical failures.
- Nearest at center/axes/off-axis and trimmed endpoints; ellipse tangents from
  outside/on/inside, domain filtering, zoom-independent pixel catch distance,
  hidden/locked filtering and existing snap priorities.
- Offset/Common Tangent safely refuse elliptic inputs. Overdraw either reports
  the documented capability limit safely or passes its complete new conic suite;
  no bad_variant_access or false line/circle interpretation is acceptable.

### Projection and faces

- Circle tilted 30/45/60 degrees; check `a=R`, `b=R*abs(cos(tilt))` and the
  complete coefficient identity, not just visually plausible axes.
- Project the same curve with different source basis rotations, nonzero seam,
  reversed edge, reflected/scaled Part, reversed target normal and nested OCCT
  locations; include large common 1e16/1e20 translations.
- Circular and native elliptical arcs: CW/CCW, short/major/near-full intervals,
  seam crossing and start at a non-principal parameter. Check interior points,
  endpoints and traversal; no accidental complementary arc.
- Parallel-circle behavior unchanged; almost-parallel ellipses not flattened by
  a large linear tolerance. Edge-on, near-collapse, tiny/huge radii and badly
  conditioned conversion rejected safely where the precision contract fails.
- Tilted disk/annulus, native elliptical face/hole, several inner wires and mixed
  line/circular/elliptical arc contours; verify every edge occurrence and stored
  join/closure. One unsupported inner boundary rejects the whole batch.
- Shared/reversed/seam duplicates, equivalent ellipse axes/domains, containment,
  tiny positive overlaps, endpoint contacts, isolated crossings and duplicate-only
  no-op. Include cross-type Circle/equal-radius-Ellipse cases.
- Mixed vertex/line/circle/ellipse/face sources across Parts; independent values
  after source deletion/change, no source references and no source modification.

### Storage and sessions

- Exact v5 parameter/UUID/role/name/flags/owner roundtrip for both new types,
  mixed old types/points, multiple Workplanes, Parts and camera.
- Empty and 2D-only documents. Inspect actual disk versions for automatic
  v3/v4/v5 selection and removal of the last new type.
- Real v1/v2/v3/v4 fixtures load with unchanged content; current-capability
  snapshots and explicit legacy requests obey supported-content guards.
- Malformed tag/record size, old root with new tag, mismatched marker/version,
  future v6, invalid domains and downgrade refusal; failed Save/Open leaves
  original file/document unchanged.
- Projection preview blue, stable result IDs, one OK/one Undo, Redo with same
  IDs; Cancel/Escape/window close without partial geometry or dirty change.
- Save/Save As during preview excludes candidates, retaining selection/history;
  New/Open resets commands/selection/previews/history. Undo/Redo clears selection.
- If ellipse Overdraw is included: forward/reverse traversal, mixed Arc and
  EllipticalArc directions, major/minor alternatives, explicit ambiguity,
  Back/Backspace, New start, Cancel, independent Profile output and one-step
  Undo/Redo with native roundtrip.

In `SketchCircularProjectionTest.cpp`, preserve parallel-circle tests and replace
only eligible tilted-circle refusal assertions with exact ellipse checks; keep
collapse/numerical refusal tests. `SketchFaceProjectionTest.cpp` currently uses
elliptical outer/inner contours as unsupported fixtures; turn supported cases
into positive tests and retain atomic errors using a genuine unsupported curve.
`SketchProjectionSessionTest.cpp` likewise uses an elliptical prism as its error
fixture. Audit `SketchProjectionTest.cpp` and hard-coded schema-4 assertions in
point/circular/face/session tests; do not simply weaken them to accept anything.

Later implementation acceptance: configure a full Release build with
`OPENDESIGNER_BUILD_TESTS=ON`, build all targets, run **all** CTests with
`--output-on-failure` under the supported OpenGL display setup, and run
`git diff --check`. Preserve existing Move/Rotate/Align/Delete Face, Boolean,
Pull, selection, camera, command-panel, drawing and native-document regressions.
Run the new pure geometry tests without a display as well. Fedora KDE manual
checks must cover tilted holes/arcs, exact numerical snaps, curve picking on a
solid face, 200% scaling, Cancel/Undo/Redo and Save/Open; include Overdraw only
when that optional extension is actually implemented.

## 10. Recommended small implementation stages and decisions

1. **Foundation and safe consumers:** add the two value types, shared helpers,
   exact OCCT rendering, explicit visitor/curve-tool guards and schema v5 with
   v1–v4 migration. Prove value/storage/history/presentation tests first.
2. **6A.2 independent drawing and reference geometry:** implement Ellipse and
   Elliptical Arc workflows below, basic snaps, Euclidean nearest and analytic
   line/ellipse intersections. Keep conic/conic intersections and elliptic tangent
   snaps explicit outstanding 6A.2 items until safely implemented.
3. **6A.3 projection core:** analytic circle/ellipse conversion, signed parameter
   mapping, numerical certificates, face contours and support/domain duplicate
   rules. Keep the widget-independent API and existing unsupported fixtures until
   this separate phase is implemented. No 3D projection changes belong to 6A.2.
4. **Optional full ellipse Overdraw:** implement independent Profile pieces and
   both traversal directions after query correctness is established. Until
   approved and tested, expose the limit explicitly and keep safe gating.
5. **Release verification/documentation:** complete Release/CTest/GUI matrix,
   record actual results and update scope/compatibility documentation.

Decisions and remaining scope:

- Approved for 6A.1: **schema v5 only when elliptical entities require it**,
  retaining v3/v4 output for old content and full read compatibility with v1–v4.
- Approved for 6A.1: principal-axis values and signed ellipse-parameter sweep.
  Native analytic `GeomAbs_Ellipse` projection and elliptical face holes remain
  part of the following projection implementation, not the foundation.
- Elliptic snapping is required for the usable drawing/projection milestone in
  6A.2. Full Overdraw remains an explicit following scope decision, with safe
  guards from day one.
- Independent ellipse drawing is required in 6A.2. Ellipse Common Tangent/Offset
  commands remain outside this foundation and require separate scope.

## 11. Approved 6A.2 independent drawing requirements

The Sketch Editor must support independent creation of both types, without any
projected 3D source or history dependency. Match the existing Circle/Arc command
sessions and parameter-panel conventions:

- **Ellipse:** choose center, determine the major axis (direction and radius),
  then determine the minor radius perpendicular to that axis.
- **Elliptical Arc:** determine center and axes, then choose start and end on
  the exact support ellipse, with an explicit CW/CCW direction. Preserve the
  intended short/large route and seam crossings through signed parameter sweep.
- Both tools expose exact numeric input for center, axis direction/radii and
  start/end parameters or their unambiguous on-curve positions. An input must
  not silently replace the ellipse parameter with a polar angle.
- Provide live previews, snapping, clear acquisition steps and existing
  OK/Cancel/Enter/Escape behavior. Invalid radii or ambiguous choices must not
  create partial geometry; do not silently swap major/minor axes after a start
  point is established.
- Support both Profile and Construction roles; generic selection/browser,
  visibility/locks, one-step Undo/Redo and exact Save/Open use the same entities
  and schema v5. No parallel drawing store or persistent constraints.
- Test mouse and numeric workflows, CW/CCW, rotated Workplanes, small/large and
  seam-crossing arcs, Cancel at every step, snaps, stable IDs and native roundtrip.

These are explicit OpenDesigner requirements, not newly inferred v16 behavior.
Phase 6A.1 was merged before the separate drawing branch was supplied. Its
values/storage/rendering remain the sole geometry/document representation.

## 12. Phase 6A.1 delivery boundary

Implemented: appended Ellipse (tag 5) and EllipticalArc (tag 6), local evaluation,
derivative, inverse support parameter, signed domain/endpoints/reversal, strict
validation, exact OCCT edges and generic AIS/browser/history integration. Existing
tags/UUIDs are unchanged. Schema v5 stores 5/7-double records; readers retain
v1–v4 compatibility. Current saves choose v3 without new types/points, v4 with
points and v5 with ellipses. Illegal downgrade/import requests fail transactionally.

Scope guards: elliptic query/snapping and Overdraw are not enabled yet. Overdraw
excludes these sources with an explicit notice while retaining old line/circle
workflows. Offset/Common Tangent retain their original reference eligibility.
Projection still refuses elliptical/oblique sources. A nonempty projection
request against existing Construction ellipses on its target is conservatively
refused until shared-support comparison is implemented; Profile ellipses, other
owners and empty requests are unaffected. Hiding/locking such a reference does
not bypass this safety rule. No drawing buttons are added in 6A.1.

Validation: full CMake Release build and 43/43 CTests PASS (94.54 seconds) on
isolated Xvfb 1280×1024/xcb/software OpenGL. The new pure geometry/storage test
also passes without a display. The earlier active-KDE run passed 38/43, failing
five existing strict panel-position assertions; those pass on the isolated
CI-style display. No positioning code was changed. See SKETCH_EDITOR.md for
the environment distinction and session coverage.
No new Fedora KDE manual confirmation is implied by automated session tests.
The existing local `hole.zip` remains outside all changes and commits.

## 13. Phase 6A.2 implementation and remaining work

`SketchEllipseDrawing.cpp` extends the existing Sketch command session with
center → major endpoint → perpendicular minor radius → (arc only) start → end.
DrawTool alternatives are appended; document type tags/UUIDs and schema v5 are
unchanged. Both roles are available in menu, toolbar and context menu. Exact
support parameters are stored directly for numeric input instead of reconstructing
them from rounded screen coordinates or input display text.

The existing SketchDrawingPanel presents only the current step's fields, then
all relevant fields during complete preview. Numeric edits update the blue
preview live and own their pending values until accepted or an explicit mouse
click. Automatic cursor feedback cannot overwrite them. Complete edits replace
only fields actually edited, preserving unedited parameter bits. CW/CCW chooses
the complementary path with the same endpoints; signed numeric sweep directly
selects direction. Zero/full-turn sweeps and invalid radii fail without swapping
axes. Back revisits each acquisition step. Enter/Escape, role changes, history,
Save/Open, tool/Workplane switching and panel clamping reuse existing adapters.

Basic snapping supports ellipse/arc centers, arc endpoints, parametric midpoint
and Euclidean nearest points on complete/trimmed ellipses. The OCCT
Geom2dAPI_ProjectPointOnCurve extrema solver runs on the analytic local conic;
all eligible extrema and trimmed endpoints are compared. Center/circular-limit
degeneracy has an explicit analytic treatment. No radial nearest approximation,
polyline sampling or curve fitting becomes application/model geometry.

Line/segment versus ellipse/elliptical arc intersections use a normalized
principal-frame quadratic with finite/domain filtering, tangent deduplication
and geometric residual checks. Unsupported conic/conic combinations report
`IntersectionKind::Unsupported`; invalid/nonfinite calculations can report
`NumericalFailure`. They are never reported as a fabricated crossing.

**Remaining 6A.2 snapping work:** circle/ellipse, ellipse/ellipse (including
coincident supports/domain overlaps), and ellipse tangent snaps. The existing
general conic intersector may return tolerance-sized tangential segments; these
must not be mistaken for true shared supports. A complete robust mapping to the
application's overlap/error contract requires separate tests. Full snapping is
not claimed. Existing line/circle snapping and priorities/Shift bypass remain.

3D ellipse projection stays disabled for 6A.3, including the conservative guard
against existing Construction ellipses in the projection target. Full elliptic
Overdraw, ellipse Offset/Common Tangent commands, dynamic Workplane bounds,
associative constraints and SpaceNavigator are outside this delivery.
