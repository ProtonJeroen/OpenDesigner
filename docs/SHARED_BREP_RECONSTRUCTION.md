# Shared B-Rep reconstruction — v0.5.7 Phase 1

Phase 1 extracts support and topology analysis without adding modeling
capabilities. `BRepReconstruction` remains the reconstruction and certification
owner. Existing operation adapters, tolerances, preview and history remain in
place. No document schema or STEP translator is introduced.

## Current shared interface

`BRepTopology.h` provides geometry-only analysis:

- `SupportDescriptor` retains the actual oriented face and its exact OCCT
  surface adaptor. Analytic supports, extrusion basis curves, B-spline carriers,
  carrier transforms and trimmed UV bounds remain accessible. Describing a
  surface does not certify its editability or its material side.
- `TopologyIndex` retains distinct face owners for each edge, plus every oriented
  edge occurrence with its face and wire owner. Periodic seams retain both uses
  even though they have only one distinct adjacent face. Wires retain their
  outer/inner classification. Edge enumeration is not certified wire traversal.
- Original face/edge handles preserve 3D curves, pcurves, continuity information,
  locations, orientations and tolerances for subsequent exact checks. Material
  side must still be derived and certified against the current solid; normals
  or carrier type alone do not prove it.

The index borrows read-only topology. It does not copy, heal, normalize, validate
or filter unsupported surfaces, and must be rebuilt for a changed shape. Callers
remain responsible for isolation and their existing complexity/solid policy.
`neighborhood` indexes its already isolated source. Its analytic patch growth,
boundary ambiguity rules and numerical limits remain unchanged. Blend adjacency
now uses the same index. Shared support comparison still accepts only its
previous plane/cylinder/cone cases with unchanged comparisons and tolerances.

## Fillets and Chamfers

Transition recognition remains in `EdgeBlendFeatures`, outside support identity:

- `recover`: recipe-free separated rectangular-stock recognition, using actual
  adjacent planes and tangent cylinders or equal-distance planar transitions.
- `sharedCircle`, `certifyFillet`, `certifyChamfer`, `recoverConical`: complete
  circular boundaries, coaxial meridian geometry, analytic tangency/equal-distance
  checks and unique conical interpretation.
- `rebuild`, `bind` and `resolve`: sharp-support reconstruction, transition replay,
  material equivalence and correspondence to the current shape. Optional recipes
  are validated against current geometry before use.

These are the reusable recognition/certification paths for future imported
Fillet work. No new Fillet/Chamfer algorithms or broader recognition are added.
The index keeps the adjacency, trims and continuity inputs needed to extend
those certificates later. A cylinder or torus is not automatically a Fillet;
a planar or conical transition is not automatically a Chamfer.

Ordinary Face edits retain their existing geometry routes. Recognizing a Chamfer
feature is not required before attempting a supported Move, Rotate or Delete.
Optional transition recognition supplies dimension editing, sharp deletion and
Keep ON/OFF only where the existing certificate succeeds. Whole-operation
validity/material checks remain authoritative; this is not permission to bypass
an unsupported transition or corrupt its material.

The future acceptance contract is identical geometry-first recognition for
equivalent native and STEP B-Reps, independent of recipes, primitive metadata,
Sketches or construction history. Recipes may accelerate recognition but must
eventually not determine capabilities for equivalent geometry. Current native
recipe coverage is broader than recipe-free circular-cylinder recognition;
Phase 1 preserves that existing limitation rather than claiming parity. Exact
differences in representation, trims, continuity or tolerances must be assessed
explicitly. Unsupported or ambiguous reconstruction remains an atomic refusal.

## Focused compatibility checks

`brep-support-topology` reuses existing synthetic blend fixtures and verifies
distinct adjacency, oriented periodic seam uses, inner wires, pcurve access,
located carriers, immutable input and stale-index clearing. A serialized B-Rep
with no recipes exercises existing Fillet/Chamfer recovery and dimensions; it
does not claim STEP roundtrip validation or general imported-Fillet editing.

Existing local-neighborhood, face-transform, blend-feature, circular Fillet,
Chamfer and conical tests remain compatibility checks. No UI or history path is
replaced. Later phases need separate proof before expanding constant-section
extrusion support, oblique intersections or topology-changing reconstruction.

Validation on 2026-11-14: incremental builds of the six affected headless
geometry test targets succeeded. Eight focused CTest checks passed (14.14 s):
`brep-support-topology`, `local-neighborhood-move-geometry`,
`face-transform-geometry`, `edge-blend-features`, `circular-fillet-geometry`,
`Fillet-geometry`, `Chamfer-geometry` and `conical-edge-blends`.
`git diff --check` passed. No GUI tests, full suite, application build, STEP
roundtrip or manual desktop acceptance was performed for this extraction.
Private untracked documents were not changed. Phase 1 stops here for review.

## Phase 2 — shared certification and correspondence

`BRepCertification` is a headless utility library built on the current exact
B-Rep, `SupportDescriptor` and `TopologyIndex`. It does not construct edited
models, heal topology, recognize new features or own transactions.

### Extracted checks and callers

| Utility | Contract and migration |
| --- | --- |
| `validBRep`, `closedShells` | Existing OCCT validity and shell closure primitives, shared by direct-edit and Boolean checks. These primitives alone do not establish an eligible solid. |
| `singleSolid` | Existing one-solid/one-shell direct-edit policy, including no loose face/edge/vertex topology, positive volume and self-interference checks. `BRepReconstruction::validSolid` delegates here. |
| `manifold` | Existing cylindrical edge-use and connected vertex-fan check; degenerate edges remain refused. Cylindrical Move/Pull/Delete paths retain this additional policy. It is not newly imposed on other operations. |
| `withinTolerance` | Existing face/edge/vertex scan. Neighborhood preparation, Move's offset result, circular Fillet and sharp blend validation pass their existing `1e-6` limit. No tolerances are capped or repaired. |
| `volume`, `area`, `occupancy` | Existing OCCT property/classification mechanisms; cylindrical point classification delegates here with its original tolerance. Sampling never replaces a material-set certificate. |
| `equivalent` | Existing deep-copy, non-destructive Common comparison of both volumes and their common volume at `1e-8 * max(1, abs(volumes))`. The old public wrapper delegates here, preserving all its callers. |
| `difference` | Non-destructive directional Cuts report removed and added sets separately, preserving their locations instead of reducing them to equal scalar volumes. Failure clears both outputs. |
| `preservedOutside` | Existing Cut-out-edit-region and material-equivalence check extracted from `finishLocal`. The caller must independently certify material semantics inside the region. |
| `sameCarrier`, `coverage`, `areaMatches`, `preservedFace` | Shared carrier comparison and trimmed overlap utilities. Local finishing retains only positive overlaps above `1e-12` and its `1e-8` coverage tolerance. Native Delete retains all carrier matches, its complete overlap sum and its separate `1e-7` coverage tolerance. |

The single-shell direct-edit policy is not substituted for the Boolean/sweep
multi-solid/cavity policy. Boolean spline-aware volume integration is unchanged;
the extracted legacy direct-edit material comparison does not newly certify
general spline solids. Operation-specific signed-volume, feature-material,
halfspace, occupancy, permitted-disappearance, support and Keep rules remain in
their original adapters. Normalization still uses its existing material checks;
these utilities never normalize away a feature.

### Correspondence levels

Carrier comparison preserves the previous plane orientation policy and
unoriented cylindrical axes. It is distinct from equal trimmed regions, material
side, continuity and Fillet/Chamfer identity. Coverage exposes both all matching
carriers (including disjoint patches) and actual positive-area overlaps.

The additional `correspond` API describes geometric region relations:

- SameRegion: both trimmed areas agree with their exact OCCT Common region.
- Split: complete source coverage by several uniquely owned result regions.
- Merged: several disjoint source regions completely cover one result region.
- Modified: one overlapping region changes its trims.
- Removed/New: no positive-area counterpart; a disjoint Face on the same carrier
  is not silently treated as the same trimmed region.

Overlapping duplicate source/result regions, incomplete split/merge relations,
many-to-many repartitions, empty face sets and unsupported carriers refuse with
an empty output and diagnostic. Current strict carrier support remains planes,
cylinders and cones. This API does not assign persistent IDs or certify material
orientation. Consumers choosing a target still require their operation's material,
orientation and uniqueness proof. Existing commands keep their legacy support
mapping semantics; they are not switched wholesale to this stricter API.

Fillet feature interpretation still requires the existing geometric continuity,
adjacency and replay certificates. Ordinary Chamfer Faces do not need feature
recognition to enter an eligible ordinary Face operation. No new blend algorithm
or native/import capability difference is introduced by certification.

### Focused regression target

`brep-certification-correspondence` covers equal and redistributed material,
directional differences, a missing cavity with equal-volume compensation,
permitted local change, unexpected protected loss, trimmed identities and
disjoint carriers, splits/merges, duplicate and many-to-many ambiguity, unsupported
carriers, primitive/prism/Boolean equivalence, immutable topology and native Part
metadata. It uses synthetic B-Reps and no private document or STEP translator.

The smallest next phase is a bounded constant-section extrusion proof for planar
cap translation, initially an exact elliptical extrusion. Extend recognition,
invariance and trim/material correspondence together; keep oblique rotation,
general B-spline editing, topology transitions and imported-Fillet expansion out
until independently certified.

Phase 2 validation on 2026-11-14: the new certification test initially passed
alone (0.50 s). After finalizing extraction, all eight selected test targets
built with `cmake --build build --target OpenDesignerBRepCertificationTests
OpenDesignerCylindricalMoveTests OpenDesignerFaceTransformTests
OpenDesignerDeleteFaceTests OpenDesignerEdgeBlendFeatureTests
OpenDesignerFilletTests OpenDesignerConicalBlendTests
OpenDesignerProfileExtrusionTests -j4`.

The final focused CTest run used `-j2`; 9/9 passed in 18.35 s:

| Test | Result | Seconds |
| --- | --- | --- |
| `brep-certification-correspondence` | PASS | 0.26 |
| `cylindrical-radius-geometry` | PASS | 6.27 |
| `local-neighborhood-move-geometry` | PASS | 4.51 |
| `face-transform-geometry` | PASS | 13.01 |
| `delete-face-geometry` | PASS | 5.47 |
| `edge-blend-features` | PASS | 0.83 |
| `circular-fillet-geometry` | PASS | 1.27 |
| `conical-edge-blends` | PASS | 2.08 |
| `sketch-profile-extrusion` | PASS | 0.80 |

`git diff --check` passed. No full suite, GUI acceptance, STEP roundtrip or
standalone application build was run. All nine private untracked `.odc`
documents remain unmodified and untracked. No commit, push or PR was created.
Phase 2 stops here for review.

## Phase 3 — axial elliptical cap Move Face

`BRepReconstruction::moveEllipticalCap` is a bounded geometry-only adapter in
`BRepInvariantExtrusion.cpp`. Move Face opts into it after its existing local
planar route. No UI or transaction architecture changes. Rotate, Pull, Delete,
Fillet and Chamfer do not opt in; global `sameCarrier` rules are unchanged.
`MoveFaceGeometry::ReconstructionPolicy` defaults to the existing behavior for
all internal callers. Move Face explicitly opts in through the existing session
record. Pull's two adapters that borrow that session explicitly retain the old
policy, preventing an accidental Pull capability expansion. No UI layout or
persistence schema changes are needed.

### Eligibility and exact geometry

The current solid must have one closed, outward-oriented shell, two planar caps
perpendicular to one unique extrusion axis, one exact elliptical extrusion wall,
and optionally one coaxial cylindrical through-hole. Caps need one full analytic
elliptical outer edge; the optional inner wire needs one full circle strictly
inside the ellipse. The complete topology is bounded to three or four Faces.
There are no stock, Sketch, primitive, feature-recipe or creation-history inputs.

`SupportDescriptor` and `TopologyIndex` supply carriers, cap/wire ownership and
actual adjacency. The opt-in `BRepCertification::ellipticExtrusion` proof requires
`GeomAbs_SurfaceOfExtrusion` with an exact analytic ellipse basis and extrusion
direction parallel to its plane normal. OCCT's transformed basis/direction access
preserves B-Rep locations. Equivalent axial offsets of the basis ellipse and
unoriented parameter axes compare by their actual infinite extrusion carrier.
This does not classify the elliptical wall as a cylinder. General rational
B-spline equivalents, split boundaries and arbitrary ruled surfaces are not
accepted by this bounded proof.

The shared vector request rejects non-axial displacement. The existing Move
Face reference controls still resolve the signed normal offset; they do not
introduce an oblique translation of the cap. Existing numeric/reference and
instance-placement conventions remain unchanged.

### Reconstruction and certification

1. Reuse `neighborhood` to isolate the current B-Rep and selected cap.
2. Derive the opposite cap, analytic ellipse, optional circle and height from
   current geometry. Certify their orientations, adjacency and exact carriers.
3. Independently check cap areas and source volume against
   `pi * (majorRadius * minorRadius - holeRadius^2) * height`. Require material
   equivalence and bidirectional trimmed-region coverage with the exact prism of
   the current opposite cap. This excludes missing/extra regions and features.
4. Construct the new solid with OCCT's exact prism of that certified current
   cross-section, and construct the intended added/removed axial region from the
   selected cap. No Sketch or original extrusion is replayed. Inner-wire support
   uses the same prism builder; no additional hole reconstruction algorithm.
5. Use `BRepCertification::difference` to verify the directional material change
   and reject material change in the other direction. Reuse `finishLocal` for
   normalization, outside-region preservation and unaffected Face coverage.
6. Re-certify the final solid, shell orientation, manifold edge uses, topology
   tolerances, analytic volume, full cross-section and material distribution.
   Verify the opposite cap and exact outer/hole carriers remain unchanged.
   Add only the independently certified elliptical-wall correspondence to the
   existing source-to-result mapping; no persistent subshape IDs are invented.

Length/tolerance policies retain the existing `1e-6` topology ceiling, `1e-8`
angular tests and relative area/material checks. Zero/collapsed/inverted heights,
unsupported trims, non-axial requests and uncertifiable results fail atomically.
Equal volume alone is never used as material equivalence. The core certificate
compares exact material sets; regression occupancy probes provide extra
independent evidence. No tessellation enters reconstruction or certification.

### Synthetic regression cases

`elliptical-move-face-geometry` starts with major/minor radii 50/30 mm and height
20 mm. It checks +15 mm to height 35 and -5 mm to height 15. The solid case was
implemented and passed before adding the optional central R10 hole.

| Case | Initial volume | Volume after +15 mm |
| --- | --- | --- |
| Solid ellipse | `30000*pi` mm³ | `52500*pi` mm³ |
| Ellipse with central R10 through-hole | `28000*pi` mm³ | `49000*pi` mm³ |

Two independent source routes are exercised: exact profile-wire/face prism and
explicit ellipse-extrusion surface with independent cap Faces assembled into a
closed shell. The latter uses an axially displaced basis parameter origin; its
hole variant uses independent cylinder subtraction. Tests check exact material,
analytic volume, elliptical/cylindrical supports, both caps, occupancy, located
B-Reps, source correspondence, raw B-Rep roundtrip, non-axial and collapsed
refusals, and source immutability. No private document is used.

`elliptical-move-face-session` exercises both variants through the existing
command, Face-normal reference, visible blue preview, Cancel, confirmation,
cached Undo/Redo, Part identity/appearance/placement, preview exclusion from saved
state, native Save/Open and shortening the reopened current B-Rep. The existing
Pull adapter is also checked to retain its previous elliptical-cap refusal;
headless checks verify the default internal geometry policy stays unchanged.
The existing
Move geometry assertions are separately selectable as `move-face-geometry` to
avoid rerunning unrelated desktop workflows.

No STEP importer or translator acceptance is claimed. Equivalent current exact
B-Reps meeting these support/topology criteria use the same eligibility path.
Oblique motion/rotation, split cap/wall sets, multiple/eccentric holes, unrelated
features, general spline/freeform carriers and blend expansion remain outside
this phase. The smallest follow-up is synthetic obround-cap/central-hole coverage
on a rotated Workplane, checking the existing plane/cylinder invariant route
before adding any new reconstruction capability.

Final Phase 3 validation on 2026-11-14: incremental builds of
`OpenDesignerEllipticalMoveTests`, `OpenDesignerMoveFaceTests`,
`OpenDesignerCylindricalMoveTests` and `OpenDesignerBRepCertificationTests`
passed using `-j4`. After adding the explicit Move-only policy, the final CTest
run used `-j2`; 5/5 passed in 29.05 s:

| Test | Result | Seconds |
| --- | --- | --- |
| `elliptical-move-face-geometry` | PASS | 20.03 |
| `move-face-geometry` | PASS | 1.19 |
| `local-neighborhood-move-geometry` | PASS | 4.56 |
| `brep-certification-correspondence` | PASS | 0.26 |
| `elliptical-move-face-session` | PASS | 9.02 |

The initial solid-only geometry check passed before the hole extension. An
earlier test failure identified a reversed-normal error in the bottom-cap test
oracle; correcting that fixture retained the orientation certificate. A first
combined run passed before scope review found the shared Pull entry point. The
final run above includes explicit Pull/default-policy refusal assertions.
`git diff --check` passed. No full suite, STEP translator, standalone application
build or manual Fedora/HiDPI acceptance was run. All nine private untracked
documents remain untouched. No commit, push, PR or merge was performed.
Phase 3 stops here for review.

## Phase 5B.1 — shared spline-aware numerical properties

`BRepMeasurement` consolidates OCCT volume and surface-property integration.
It consumes only current exact B-Reps and returns properties, OCCT's estimated
relative integration error and the selected integration mode, or an explicit
diagnostic with cleared outputs. It has no feature/history/recipe inputs and
does not decide whether an operation is geometrically supported.

### Numerical policy and reuse

- Analytic carriers with analytic trims retain the existing non-tessellated
  `BRepGProp` integration path. Automatic selection also examines edge/pcurve
  geometry, so a planar cap with spline boundaries selects adaptive integration.
- Spline/Bezier carriers, spline sweeps and spline trims use OCCT facilities:
  `VolumePropertiesGK` with knot-span integration, and adaptive
  `SurfaceProperties`. Both request `1e-12` and refuse a negative, nonfinite or
  larger estimated relative error. Returned nonzero properties need finite
  centroids. Existing area/material acceptance thresholds are not relaxed.
- A periodic-spline compatibility case initially reported `4.2974e-10` error
  for its swept representation. GK's surface-span option applies to B-spline
  Faces. One bounded retry exposes spline sweeps as exact NURBS on a disposable
  OCCT copy, then integrates their spans at the same `1e-12` requirement. This
  reuses `BRepBuilderAPI_NurbsConvert`; it introduces no fitted geometry or
  numerical integration engine. Converted topology never reaches the model,
  correspondence, preview or persistence. An unsuccessful retry still refuses.
- Volume measurement includes solid material only: valid empty sets and
  lower-dimensional Boolean contacts measure zero. A missing/null B-Rep or an
  invalid/open solid is refused. This zero-volume support does not turn an empty
  result into a valid positive solid. Multiple solids and cavity shells can be
  measured while the direct-edit single-shell policy remains separate.
- Adaptive inputs require finite nondegenerate trimmed domains. Source topology
  remains untouched; temporary material containers use copied children because
  OCCT attachment can change a child's Free flag. Adaptive integration also
  operates on copies. No source tolerance is clamped or healed.

The shared implementation replaces BooleanGeometry's duplicated spline-volume
selection, BRepCertification's scalar area/volume and positive-solid numerical
calls, and BRepReconstruction's direct area/volume calls, including normalization
centroids and protected-region checks. Profile Builder and Profile Extrusion
retain their previous explicit adaptive-area policy through the shared API.
Numerical refusals during extrusion now preserve the integration diagnostic.
Existing polynomial Profile validation remains an independent area oracle.

The historical direct-edit integration caveat recorded in Phases 2 and 5A is
superseded by this common numerical path. Surface recognition and operation
eligibility are unchanged: in particular, spline Move Face remains refused by
the existing adjacent-support gate. No freeform editing capability is added.

### Independent regression evidence

One new target, `spline-geometric-certification`, covers:

- Box and cylinder volume/area; valid empty Common/Cut and retained face contacts.
- An exact cubic arch with `x=60t`, `y=20+12t-6t^2-6t^3`, area `1350` mm²,
  20 mm prism volume `27000` mm³ and independent centroid `x=452/15` mm.
- A two-span cubic with a C0 knot, area `675+630=1305` mm² and volume `26100` mm³.
- Two circular holes of radii 3 and 4: area `1350-25*pi` and volume
  `27000-500*pi`; an internal cavity independently subtracts another 120 mm³.
- Equivalent profile-hole and Boolean-hole routes, split and normalized topology,
  located geometry, unequal occupancy at equal volume, and unchanged sources.
- An exact rational quarter-cylinder B-spline surface trimmed to height 10 mm,
  with independently known area `100*pi` mm².
- Unbounded spline area, null input and invalid open-solid refusals, including
  cleared failure outputs; spline Move Face remains unsupported.

Final validation on 2026-11-14: incremental builds used `-j4`. Only the new target,
directly affected certification/spline tests and one headless Boolean Add/Cut
regression target ran. The final CTest invocation used `-j2`: 4/4 passed in 1.98 s.

| Test | Result | Seconds |
| --- | --- | --- |
| `spline-geometric-certification` | PASS | 0.55 |
| `brep-certification-correspondence` | PASS | 0.41 |
| `sketch-spline-geometry` | PASS | 1.43 |
| `sketch-profile-extrusion` | PASS | 1.50 |

`git diff --check` passed. No full suite, GUI acceptance, STEP importer,
standalone application build, commit, push, PR or merge was performed. All
private untracked documents remain untouched. OCCT error estimates are numerical
evidence, not a blanket proof for every pathological spline; independent
material/section checks and existing topology tolerances remain necessary.

The smallest Phase 5B.2 scope is bounded invariant-support recognition and
correspondence for exact low-degree nonrational spline sweeps, starting with a
no-hole axial cap edit. Two holes and rotated geometry follow only after that
case certifies. General B-spline-surface editing and oblique reconstruction
remain outside that scope. Phase 5B.1 stops here for approval.

## Phase 5B.2 — bounded invariant spline supports (2026-11-15)

The interrupted session had added the support proof and opt-in correspondence
implementation, but no focused test target or verified Phase 5B.2 results.
Resume work completed the synthetic target, conservative proof guards, source
isolation for spline Face Common operations, and this validation record.

`SupportDescriptor::invariantSpline` now returns an `InvariantSplineSupport`
proof using the current exact B-Rep and optional `TopologyIndex`. It requires:

- An exact `Geom_SurfaceOfLinearExtrusion` with a nonperiodic, nonrational
  B-spline basis of degree at most 3 and a positive rigid location.
- A valid Face within the existing `1e-6` topology ceiling, finite U/V domains,
  and one four-edge rectangular wire of exact isoparametric line pcurves.
- An exactly segmented basis whose control poles prove section planarity
  perpendicular to the extrusion axis within `Precision::Confusion()`. Strictly
  positive chord projections of all consecutive control-pole differences prove
  regularity and injectivity across continuous knot spans, without sampling.
- A curved basis; collinear bases await analytic-plane canonicalization.

The proof records isolated full/trimmed basis geometry, axis, current parameter
intervals, section planes, oriented edge occurrences and actual neighboring
Faces, including perpendicular planar cap neighbors when present. Shared
`BRepMeasurement` certifies positive area. The carrier's mathematical axial
continuation is unbounded, but this is not permission for a finite material
edit, collision clearance or arbitrary numerical extension distance.

`BRepCertification::correspondInvariantSpline` is explicit opt-in. Existing
analytic correspondence callers and modeling gates remain unchanged. The new
matcher accepts one directly comparable spline-carrier family per call, using
normalized knots, multiplicities and projected control coefficients, including
basis reversal, affine parameter scaling and axial basis-origin shifts. This
carrier proof alone never establishes region identity: exact OCCT Common and
shared area measurements distinguish SameRegion, Modified trims, Split, Merged,
Removed and New regions. Duplicate overlap and ambiguous many-to-many/partial
relations refuse atomically. Common operands are deep copies to preserve source
ownership flags as well as geometry. No persistent subshape identity is invented.

Generic `Geom_BSplineSurface` carriers, including exact NURBS conversions of
sweeps, are explicitly unsupported pending a separate equivalence proof. So are
rational, periodic, higher-degree, nonplanar, nonmonotone or discontinuous bases;
oblique/inconsistent trims, seams, split boundary edges and holes within a side
Face; nonrigid locations; and knot-inserted/degree-elevated or unrelated carrier
families. This is a conservative initial subset, not general spline equivalence.

Focused validation used incremental builds of
`OpenDesignerInvariantSplineTests` and `OpenDesignerBRepCertificationTests`
with `-j4`, then CTest with `-j2`:

| Test | Result | Seconds |
| --- | --- | --- |
| `invariant-spline-support-correspondence` | PASS | 0.83 |
| `brep-certification-correspondence` | PASS | 0.51 |

The new target covers bounded/partial domains, rigid baked and located rotation,
Face/edge and basis reversal, cap adjacency, changed trims, split/merge,
removed/new regions, ambiguity, unsupported representations, excessive topology
tolerance, and source immutability. It also checks that legacy correspondence
and `movePlanarLocal` still refuse spline supports. No Move Face routing or other
operation acceptance policy changed. No full suite or GUI testing was run.
All private untracked documents remain untouched (ten were present on resume).

Phase 5B.2 stops here. The smallest proposed Phase 5B.3 is an explicitly gated,
no-hole spline-extrusion axial cap edit using the existing prism reconstruction,
a complete cap/side adjacency certificate and independent material checks.
It requires separate approval; multiple holes and broader representations remain
later work. This supersedes the earlier suggestion to include a cap edit in
Phase 5B.2: this phase contains infrastructure only.

## Phase 5B.3 — certified no-hole spline cap Move (2026-11-15)

Move Face now explicitly opts into bounded axial motion of a complete certified
spline extrusion. `MoveFaceGeometry::ReconstructionPolicy::AxialInvariantCap`
combines the existing elliptical permission with the new spline permission.
The default policy and all other command adapters remain unchanged; the
historical Move-only session flag is reused. Spline failures do not retry global
offset or planar-stock reconstruction. Oblique command references are refused
rather than projected onto the cap normal for spline geometry.

`movePlanarLocal` retains its existing default behavior. Its explicit spline
opt-in requires one unsplit planar cap without holes, exactly one opposite cap,
and a complete invariant boundary. Every Face is examined: spline walls must
pass Phase 5B.2's exact bounded support proof with a common axis perpendicular
to the selected cap. Perpendicular planar walls can accompany spline walls;
other carriers refuse. The isolated source must be manifold and materially
equivalent to the current cap swept to the opposite cap. Independent integrated
source volume must equal section area times oriented height. Collapse/inversion
is refused before reconstruction. Split axial Face bands are not rejected
merely for failing to touch both caps individually; their complete material
union must still pass the section sweep certificate and correspondence checks.

Reconstruction uses the existing `BRepPrimAPI_MakePrism` machinery on the exact
current trimmed cap, without curve fitting, Sketch replay or recipe inputs.
The local cap sweep supplies the edit region and Boolean Fuse/Cut result.
OCCT Cut was observed to replace line pcurves by uncertified spline pcurves,
and Fuse can retain separate wall bands. For the certified complete extrusion,
a translated-cap prism over the new full height supplies exact support/trim
representations; it is accepted only when materially equivalent to that Boolean
result. This is reuse of the existing exact cap-prism path, not a new spline
modeling engine or a relaxation of trim recognition.

Existing checks independently require the directional material difference to
equal the edit sweep and material outside it to remain unchanged. Shared
normalization, solid validity, manifold/orientation, `1e-6` topology ceiling,
area/volume integration and material equivalence remain mandatory. Final
section/height and complete source-sweep certificates are repeated on the result.
Per-wall correspondence invokes Phase 5B.2's scoped matcher separately for each
carrier family; ordinary analytic mappings retain their previous path. No stable
subshape identities are invented. Opposite cap and unchanged regions are
preserved geometrically, not by claiming persistent OCCT Face handles.

### Focused evidence

The new synthetic fixture closes two exact nonrational cubic B-spline segments.
Its coordinates satisfy `x=60t`, `y=20 +/- (12t+6t²-18t³)`, so direct polynomial
integration gives section area `420 mm²`, independently of OCCT integration.

| Quantity | Independent expected value | Result |
| --- | --- | --- |
| Initial height/volume | 20 mm / 8400 mm³ | PASS |
| +15 mm Move height/volume | 35 mm / 14700 mm³ | PASS |
| -5 mm Move height/volume | 15 mm / 6300 mm³ | PASS |

The geometry regression verifies exact translated cap poles, knots,
multiplicities, parameter trims and oriented edge uses; side supports and Face
correspondence; opposite cap; occupancy and protected material; directional
added/removed material; source immutability; collapse/inversion, selected-wall,
hole and generic spline-representation refusals. No fixed Face count is imposed.

Incremental builds used at most `-j4`; CTest used `-j2`. Final results for each
affected target (unchanged targets were not repeatedly run):

| Test | Result | Seconds |
| --- | --- | --- |
| `spline-move-face-geometry` | PASS | 7.98 |
| `invariant-spline-support-correspondence` | PASS | 2.44 |
| `brep-certification-correspondence` | PASS | 1.05 |
| `spline-move-face-session` | PASS | 8.95 |

The session scenario uses rotated/translated Part placement, existing Face
selection and reference input, visible blue preview, oblique-reference refusal,
Cancel, OK, one Undo/Redo transaction, identity/appearance preservation and native
Save/Open followed by shortening. Pull is explicitly checked to remain refused.
No other command or full suite was exercised. No private documents were used.

Holes, periodic/rational/higher-degree or otherwise unproven spline supports,
generic tensor-product spline carriers, nonrectangular trims, split cap patches,
non-axial edits and uncertifiable numerical results remain refusals. This proves
the tested bounded representation, not arbitrary freeform editing or general
STEP representation equivalence. Existing material tolerances are unchanged.

Phase 5B.3 stops here. The smallest next Phase 5B.4 is investigation/test-first
extension to one circular through-hole, preserving the same complete-boundary,
exact support, directional material and source-immutability certificates.
Multiple holes require separate evidence before broadening eligibility.
