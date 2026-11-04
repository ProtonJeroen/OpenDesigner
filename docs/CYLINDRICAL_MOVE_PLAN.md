# Cylindrical Move Face — phase 9 boundary reconstruction plan

This supplements Issue #51 and the existing [Move Face](MOVE_FACE.md) contract.
Phase 9A records the architecture; the 9B and 9C sections describe the headless
through-hole and boss operations. Phase 9D integrates them into Move Face;
manual Fedora KDE acceptance and phase 9E remain outstanding.
Phase 9D.1 below expands recognition to independent parallel features in one Part.

## 9A — completed architecture analysis

Analysis of the current branch; all API names below are proposals. No geometry
or UI implementation is included in 9A.

### Reusable components and required separation

| Existing code | Reuse / boundary |
| --- | --- |
| `src/MoveFaceGeometry.{h,cpp}` | Keep planar `reference`/`move` unchanged. Offset-on-face changes radius for a cylinder; it does not laterally move a feature. The signed-volume-change rule also does not apply to relocation. |
| `src/BRepReconstruction.{h,cpp}` | Reuse `validSolid`, `normalize`, `equivalent`, analytic support comparison and deep-copy discipline. `prepare` can preserve/map a selected patch, but is not a complete feature recognizer. Do not reuse plane-specific `finish`, `supports` or `rebuildPlanar` as cylindrical acceptance: they require a target plane and/or retained original supports. |
| `src/BooleanGeometry.h` | Reuse non-destructive `combine` with OCCT Fuse/Cut/Common. Its Boolean validator permits multiple solids/shells; cylindrical edits require the stricter reconstruction validator. Never discard stray solids with `solidResult` to rescue a candidate. |
| `src/DeleteFaceGeometry.cpp` | Reuse the design of edge/face adjacency, periodic-seam exclusion, planar rim certification and independent feature-volume certificates. Helpers are private and require deliberate extraction or a small local equivalent. Generic defeaturing success alone does not establish the intended stock. |
| `src/MoveFace.cpp` | Reuse immutable `faceBefore`, full-solid preview, invalid-preview clearing, bounds update, `GeneralBRep` conversion and `applyFaceState`. Add cylindrical dispatch rather than change planar semantics. |
| `src/SelectionContext.cpp` | Face owners identify the actual Part/member face. Split Move eligibility from Rotate/Align eligibility: currently all three share the planar `supported` flag. Keep preview AIS objects unselectable. |
| `src/CadViewport.h`, `src/CadViewport.cpp` | Reuse `CommandSession`, `FaceChange`, commit/cancel and `restorePullChange` cached snapshots. Current scalar `value`/normal arrow are insufficient for two lateral components; parameter parsing, changed detection and confirmation need cylindrical dispatch. |
| `src/NativeDocument.cpp` and viewport snapshot/restore | Existing BinXCAF local B-Rep, placement and persistent Part ID suffice. No feature history or schema extension is needed. Save excludes previews; Open clears undo history. |

### Topology-based recognition

Start from one selected analytic cylindrical member face on one valid closed
solid with one shell (initial source budget: 64 faces). Deep-copy geometry and
topology before kernel work. Build edge-to-face adjacency on the isolated source;
maintain copy correspondence back to the selected face. Interpret surfaces with
their OCCT locations, independently of the Part instance placement.

Grow only the edge-connected, coaxial, equal-radius mantle component. Internal
edges between its faces cancel; a periodic seam used twice on one face is not a
rim or a second neighbor. Certify that the union covers one full 2π interval at
every axial section between two distinct perpendicular rim planes, without gaps,
overlaps or branches. UV bounds, a single seam, face orientation or two circular
edges alone are insufficient. Verify pcurves/edge geometry and assemble boundary
arcs into exactly two closed circular rims of the same radius; permit harmless
split arcs/faces only when coverage and adjacency remain unique.

- **Through-hole:** both rims border surviving planar exterior faces normal to
  the axis; there is no disk cap. Material-side classification is outside the
  cylinder and void is inside throughout the span. Fill the isolated cylindrical
  void to obtain stock `B`; certify `source = B − C0`, and no material change
  outside the recognized feature. The openings must actually exit the stock.
- **Attached boss:** one rim bounds a planar disk cap, with no inner wires or
  other attached feature, and the other rim is an inner footprint boundary of
  the surviving planar support face. Material is inside the mantle and below
  the footprint; the cap is exterior. Remove the isolated boss volume to obtain
  stock `B`; certify `source = B ∪ C0`. The stock and boss share the full circular
  base interface, with positive stock thickness below it.

Use oriented surface normals AND solid classification on both sides of several
well-separated regular mantle/rim/support points. Refuse conflicting or
tolerance-indeterminate classifications. Certify stock recovery by Boolean
material relations, not historical primitive flags or a bounding box. A standalone
cylinder, blind hole, partial cylinder, countersink, fillet or ambiguous cap/rim
is not one of these initial features. Split source mantles may be recognized;
partially truncated *results* are legitimate, but recognizing them for a later
new command is deferred.

### Minimal proposed geometry API

Add a geometry-only `CylindricalMoveGeometry` adapter, keeping the existing planar
API stable. Declaration sketch (not compiled code):

```cpp
enum class FeatureKind { ThroughHole, Boss };
enum class BossBoundaryMode { Trim, Extend }; // no implicit fallback/default
struct Prepared; // opaque owned immutable source, stock, feature and rim certificate
struct Reference {
    FeatureKind kind;
    gp_Ax2 frame; // origin at first rim center; Z=axis, X/Y=lateral input basis
    double radius, height;
};
bool recognize(const TopoDS_Shape& source, const TopoDS_Face& selected,
               Prepared& prepared, Reference& reference, QString& error);
bool translate(const Prepared& prepared, const gp_Vec& localDelta,
               std::optional<BossBoundaryMode> bossMode,
               BRepReconstruction::Result& result, QString& error);
```

`Prepared` owns deep copies and source correspondence, never Part/UI pointers;
implement as an opaque value or immutable shared implementation. Failure clears
all outputs. `localDelta` must be finite and perpendicular to the axis within
the documented linear tolerance: reject an axial component rather than silently
project it away. A boss requires an explicit mode, even for an interior move;
a hole requires no boss policy. Zero displacement returns unchanged material and
creates no history. The caller converts world displacements using inverse Part
placement (including scale) and supplies two signed distances in the reference
X/Y frame. Choose a deterministic perpendicular basis once at recognition and
retain it throughout the session; the clicked normal is not a translation axis.

`Result::history` remains support correspondence, permitting empty/multiple
descendants. Track the mantle on the translated cylinder and retained supports
on their source geometry; assign new boundary patches to certified supports.
Never choose an arbitrary target face: `target` is null unless exactly one final
mantle patch exists. No persistent subshape naming or Align chaining is promised.

### Material semantics and supported boundary cases

Let `C0` be the original finite cylinder, `Ct` its exact lateral translation,
and `B` the certified stock recovered above. Initially `B` is an arbitrary rigidly
placed rectangular block certified from its six oriented plane supports, not
an axis-aligned bounds reconstruction. Additional stock classes require their
own certificates. The axis is normal to the block's support/opposite planes.

| Operation | Required material |
| --- | --- |
| Through-hole, interior or boundary crossing | `B − Ct`; exterior support surfaces stay fixed but trims may open into an edge/corner notch, split, merge or disappear. Extend the cutting tool beyond both exit planes for robust Boolean construction; it is tooling, not added material. |
| Boss Trim | `B ∪ (Ct ∩ H)`, where `H` is the lateral stock region bounded by extended physical side supports, unbounded along the boss axis. This clips walls/cap at actual exterior surfaces. |
| Boss Extend, initial block scope | `B ∪ Ct`. Where the translated footprint still overlaps the original support with positive area and a manifold union exists, the exposed portion of the boss base becomes an underside on the support plane. This is the minimal connected extension; no additional support pedestal is implied. |

For Extend cases requiring material below the support plane or a bridge after
complete footprint separation, no unique extension follows from a general B-Rep.
Such cases are explicitly deferred/refused until a separate support-extension
rule and independent oracle are specified in 9C; never silently trim or invent
a bridge. This does not refuse ordinary partial protrusion with a connected
full cylinder. Future certified support reconstruction may add material only
under an explicitly documented rule.

Boundary-crossing holes may change from a closed bore to an open notch if the
result is one valid manifold solid. The Phase 9B requirement supersedes the
original retained-cylinder restriction: a completely escaped tool is accepted
as the recovered stock, with an explicit `HoleExited` diagnostic. Refuse total
removal of stock or disconnected/nonmanifold remaining material.
For Trim require positive boss volume/height and a surviving curved wall;
fully clipped-away bosses are refused. For Extend require the entire `Ct`
material, including its full cap and circumference, and a positive-area base
connection. Exact tangencies causing pinched/nonmanifold topology or uncertain
classification are refused; tangency alone is not automatically an error if
the resulting regular solid is certifiable.

### Acceptance and safe refusal

Normalize only after constructing the requested material, then validate again.
Reuse `validSolid` for BRepCheck, one closed shell/solid, no loose subshapes,
finite positive volume and copied self-interference analysis. Add explicit
manifold edge/vertex neighborhood and positive-area interface certification;
a reported single solid alone is not a material-connectivity certificate.
Never sew an invalid open candidate or accept point/edge-only attachment.

Compare final material with the policy expression using Common/equivalence
(existing relative volume tolerance 1e-8 scaled by at least 1), including both
directional differences where needed. Verify radius, axial direction, height,
translated axis and allowed analytic supports independently. Interior translation
preserves volume; boundary cases follow the expression, not a signed-distance
volume rule. Check that stock and unrelated feature material outside the local
edit region is unchanged. Auxiliary tool/seed faces must not survive as exterior
supports. Legitimate topology changes override no face/edge-count invariant.

Tests must use independent analytical volumes, construction oracles and IN/OUT
samples around old/new footprints and side boundaries, not only replay the same
Boolean algorithm. Tolerances derive from `Precision::Confusion()`, source
subshape tolerances and model scale; document linear/angular/volume thresholds
in 9B and refuse features/clearances comparable to them. Never increase fuzzy
tolerance until a failed operation passes. Material equality within tolerance
is a numerical certificate, not a mathematical proof for arbitrary geometry.

Refuse invalid/nonmember selections, nonanalytic or incomplete features,
multi-solid/cavity inputs, intersecting neighboring features in the edit region,
ambiguous stock/support recovery, unsupported blends/freeform surfaces, invalid
or excessive tolerances, degenerate sizes, failed Booleans, ambiguous histories
and exhausted bounded complexity. Return actionable errors without changing
the original Part, selection snapshot or history. No alternate mode on failure.

### Concrete phase boundaries

- **9A (this change):** code inspection, recognition/material contracts, proposed
  API and phase plan only. No application code, UI, builds or tests.
- **9B:** implement the isolated recognizer and geometry API, tolerance policy,
  certified rectangular stock recovery and through-hole translation, including
  edge/corner crossings and complete exit (expanded by the 9B requirement).
  Headless tests: seam/split mantle coverage, false-positive rejection,
  exact axis/radius/span/volume, located B-Reps, source immutability, zero/invalid
  vectors and independent material oracles. No boss reconstruction or mode UI.
- **9C:** implement interior bosses and both explicit
  boss policies within the certified scope above; add topology-changing geometry,
  material and refusal tests below. Any broader support-extension rule must be
  specified and tested here before it becomes supported.
- **9D:** integrate cylindrical selection/dispatch, explicit lateral reference
  and direction angle plus signed distance (a full lateral displacement),
  manipulator, boss policy and matching full-solid live preview in the existing
  Move Face Command Panel. Preserve planar Rotate/Align gating.
- **9E:** complete history/identity/persistence regressions, negative transaction
  tests and manual acceptance. No native schema change or persisted undo stack.

Future test homes: a dedicated headless cylindrical geometry suite, existing
`tests/MoveFaceTest.cpp`, `tests/FaceTransformTest.cpp`,
`tests/BooleanOperationsTest.cpp`, `tests/NativeDocumentTest.cpp` and
`tests/NativeDocumentSessionTest.cpp`. These are planned checks, not 9A results.

## 9B — implemented headless through-hole geometry

`src/CylindricalMoveGeometry.{h,cpp}` provides the single stateless entry point
`moveThroughHole(source, selectedMantle, displacement)`. To keep this phase's API
small, recognition and translation share one call; the proposed reusable
`Prepared` session above remains deferred; 9C adds an explicit boss policy. The public API uses
only OCCT and the C++ standard library. The library links Qt Core for existing
Boolean/reconstruction helpers, with no Widgets or visualization dependency.

`Result` carries a nullable shape, translated axis, radius/span, a `Status`
(`Success`, `Unsupported`, `InvalidInput`, `Failure`), a machine-readable `Code`
and a descriptive message. Success distinguishes `Moved`, `Unchanged` and
`HoleExited`. Every refusal returns a null shape; no partial result or source
mutation is exposed. Subshape history is deferred to command integration.

Recognition isolates the source on a deep copy, certifies one manifold solid,
collects coaxial cylindrical mantle patches and verifies circular rim adjacency
on opposite axial stock planes. Internal split edges and periodic seams are
excluded from rims. Both rim lengths and total mantle area must cover the full
cylinder. Oriented mantle normals and radial IN/OUT probes distinguish a void
from a boss. Six oriented orthogonal physical plane supports define an oriented
stock oracle. Material equivalence to stock minus the original cylinder certifies
coverage and absence of other features; no primitive metadata or bounding box is
used. This also accepts mantle patches split into axial bands or angular patches.

The operation fuses in the original cylinder, normalizes and certifies the filled
stock against that oracle, then cuts the translated cylinder using an axially
extended tool. Final validation checks one closed shell/solid, self-interference,
two edge uses (including seam uses), connected vertex fans, material equivalence
to the finite-cylinder expression, and only original planes/translated-cylinder
supports. Open notches, corner crossings, disappearing cylinders and healed old
footprints are permitted. Point contacts, pinched tangent walls and corner cuts
that isolate another solid are refused.

Numerical scope is intentionally conservative: linear tolerance is
`10 * Precision::Confusion()` (1e-6 mm), angular tolerance 1e-8 radians;
source subshape tolerances above the linear limit are refused. Radius, stock
thickness and original side clearances must exceed 100 linear tolerances.
Final planar support correspondence uses the existing shared helper's 1e-5 mm
and 1e-7 rad thresholds; translated cylinder correspondence uses the stricter
linear/angular thresholds above.
Mantle area/rim length comparisons scale this linear tolerance by feature size;
material equivalence uses the existing 1e-8 relative volume rule. IN/OUT probes
are 1e-4 of the smallest radius/span/stock scale and must exceed 10 linear
tolerances. Source complexity is bounded to 64 faces and 256 edges/vertices,
dimensions to 1e7 mm and plane coordinates to 1e9 mm. No fuzzy tolerance is raised
on failure. Axis-parallel displacement components exceeding the linear tolerance
are invalid input; permitted numerical residue is not projected away.

Unsupported inputs include blind holes, partial source mantles/notches, standalone
cylinders, bosses, multiple holes, nonrectangular stock, cavities and blends.
Zero displacement is recognized and certified as unchanged material; a caller
must avoid adding history for `Unchanged`. Fully exited holes produce the filled
stock and cannot be selected as holes in a subsequent new command. These limits
do not constrain a later Trim/Extend builder to retain the original topology.

Headless coverage is in `tests/CylindricalMoveGeometryTest.cpp`, registered as
`cylindrical-move-geometry`. It checks analytic volumes and sampled material
occupancy for interior, positive/negative edge, circular-segment and corner cuts,
complete exit, zero moves, rigidly transformed and located solids. It exercises
split mantle recognition, source serialization preservation, geometry/axis
dimensions and atomic refusals for invalid vectors, nonmember/planar targets,
blind/partial holes, multiple features, bosses and disconnected/pinched results.

Verification: the new headless target builds; `cylindrical-move-geometry`,
`face-transform-geometry` and `delete-face-geometry` pass. `git diff --check`
passes. No UI, command history or native document behavior is changed in 9B.

## 9C — implemented headless boss geometry

`moveBoss(source, selectedMantle, displacement, BossBoundaryMode)` reuses 9B's
isolated geometry path, tolerances, stock recovery, mantle connectivity and rim
certificates, material comparison, normalization and manifold checks. Policy has
no default: callers must supply `Trim` or `Extend`; invalid enum values produce
`InvalidInput / InvalidPolicy`. No UI, Part IDs, history or file storage enter the
API. Through-hole behavior remains covered by the same cylindrical test target.

Recognition requires outward-facing complete cylindrical mantle patches, one
adjacent planar disk cap with a single wire, and the opposite complete rim on
one planar supporting face. It identifies the cap from topology, area and axis
placement, then excludes it from the stock's six oriented support planes. The
cap lies beyond the stock face, with positive stock thickness below the full
original footprint. Radial material classification must be IN inside the boss
and OUT outside. Source material must equal `B ∪ C0`, and cutting out `C0` must
recover the certified rectangular stock exactly within the shared material
tolerance. Split axial bands/angular mantle patches are accepted when all rim,
adjacency, normal and material certificates agree. No creation metadata is used.

After removal of the original boss, Trim computes `B ∪ (Ct ∩ H)`. The finite
representation of `H` is a prism bounded laterally by the original certified
stock side planes and axially by the boss rim planes; its caps cannot crop any
intended boss material. It is constructed in the stock support frame, never
from an axis-aligned bounding box. New planar truncated walls may merge with
stock sides. Extend computes `B ∪ Ct`, retaining the full translated cylinder;
the part outside the support footprint has an underside on the original support
plane. Reverse-facing patches on that plane are permitted, and all final planes
must correspond to original stock or cap supports. No bridge or pedestal is
invented, and neither policy falls back to the other.

Both policies require reliable positive-area overlap with the original support:
disk-to-rectangle penetration must exceed 100 linear tolerances, and the clipped
prism intersection must be a certified positive-volume solid with area greater
than 10000 squared linear tolerances. Tangent line/point contact, complete exit,
fully trimmed-away bosses and ambiguous tiny overlaps return
`Failure / DisconnectedBoss` with no result. Final acceptance additionally checks
the requested material expression, unchanged stock, a surviving cylindrical wall
and a valid single manifold solid. Extend independently requires the entire
translated cylinder to be contained in the result. Radius, axial span and
direction remain unchanged; Trim may shorten the surviving circular arcs.

Initial limitations remain rectangular stock, a full original circular footprint,
one boss with a unique disk cap and support face, no blends or other features.
Recognizing an already overhanging/truncated boss for a new command, split caps
or ambiguous support faces, nonrectangular support reconstruction and disconnected
extensions are deferred. Input B-Reps remain untouched on success and refusal.

Focused tests in `tests/CylindricalMoveGeometryTest.cpp` compare interior Trim
and Extend by material equivalence; verify half-circle, circular-segment and
corner Trim volumes; and verify full cap/mantle area for Extend overhangs. They
sample analytic material occupancy below/above the support, at old/new footprints
and outside stock sides, and check exact volumes, dimensions, valid connectivity
and serialized source immutability. Cases include negative movement, rotations,
located solids, split mantles, zero moves, complete exit/tangency refusals,
invalid directions/policies and unsupported sources. Only the affected geometry
target and relevant `cylindrical-move-geometry` CTest are needed for this phase.

Verification: incremental build of `OpenDesignerCylindricalMoveTests` passed;
`ctest --test-dir build -R '^cylindrical-move-geometry$' --output-on-failure`
passed (including existing through-hole regressions). `git diff --check` passed.
No GUI tests or complete CTest suite were run for 9C.

### Reconstruction scope for later expansion

Recognize the complete attached boss, its cap, original footprint, supporting
material and physical exterior boundary supports from the source B-Rep, without
requiring primitive history. Close the old footprint before reconstructing the
translated feature. Resolve boundaries in the Part frame, including placed Parts.

Specify and implement two separate candidate builders:

- **Trim:** clip the translated boss against the extended original exterior
  boundary surfaces. Reconstruct adjacent faces and close the solid; a circular
  wall or cap can become truncated, with planar wall patches where the exterior
  supports are planar. Preserve the cylinder's analytic radius and axis on its
  surviving curved patches. Do not clamp against the axis-aligned bounding box.
- **Extend:** retain the entire translated cylinder, including full circumference,
  cap, radius and height, even beyond the original Part boundary. Reconstruct or
  extend supporting material to keep it connected. First certify an unambiguous
  extension of the original support region and its exterior supports; do not
  invent an arbitrary bridge. Refuse cases where the required extension cannot
  be determined uniquely. Define the support-material construction and its
  independent material oracle before accepting each new geometry class.

Allow faces/edges to split, merge, appear or disappear; unchanged topology counts
are not acceptance criteria. Keep unrelated features and material unchanged
outside the certified reconstruction region. Neither builder may fall back to
the other policy on failure.

Begin with a circular boss on a planar rectangular block. Use independent
analytic intersections/unions and material classification to certify expected
volume and occupancy for moves wholly inside, crossing one edge, crossing a
corner, and reaching or passing tangency. Compare both modes at identical
translations: Trim has truncated material at exterior supports; Extend retains
the complete cylinder and the specified support extension. Include moves with
no remaining original footprint overlap: accept only when that mode's certified
construction remains valid and connected; otherwise refuse. Cover both signs,
rotated/located Parts and tolerance-scale perturbations. Add nonrectangular
support cases only with explicit boundary orientation and material certificates.

Validate BRepCheck, closed manifold shells, one positive-volume solid, absence
of loose topology/self-interference, and volumetric material connectivity.
Reject point/edge-only contact, slivers or ambiguous boundary classification.
Check expected material as well as validity; a valid but semantically wrong solid
must fail. Verify the original serialized B-Rep is unchanged after success or
refusal. Existing hole behavior remains independently covered.

## 9D — interactive Move Face integration

The existing **3D Modeling → Move Face** command dispatches planar faces to the
unchanged planar geometry adapter and cylindrical mantles to the unchanged 9B/9C
APIs. The existing typed face context menu offers cylindrical Move Face after
zero-displacement geometry certification. Rotate/Align remain planar-only. The
selected member face identifies the single target Part; an unrelated Active Part
does not override it. No new modeling command, geometry API, history framework
or file schema is introduced.

Cylindrical sessions begin with **Choose lateral reference…** and no arrow or
confirmable candidate. The user chooses Part X, Y or Z projected perpendicular
to the cylinder axis; references parallel to the axis are disabled. An exact
direction-angle field rotates that projection in the lateral plane using the
right-hand rule about the displayed cylinder axis in Part coordinates. The
orange arrow then defines positive movement; drag it or type a signed distance
in mm in the existing distance editor. Angle zero and the chosen reference are
explicit, never an arbitrary basis derived from the cylinder axis. This polar
input spans both lateral degrees of freedom without adding two distance editors.

Directions originate in the Part's B-Rep frame, then transform to a normalized
world direction for the arrow and scalar world-mm displacement. Inverse Part
placement converts the complete vector back to the geometry frame, including
scale; no instance translation is applied to vectors. Numeric angle, distance,
reference and policy changes rebuild from `faceBefore`, never the last preview.
Dragging uses the existing arrow hit testing, mm snapping and Shift bypass.

Bosses visibly default to **Extend — full round boss** on each new command. The
**Trim — original stock boundaries** choice remains selected throughout that
session, including invalid displacement and direction changes. Through-holes
hide the policy selector. Failed geometry clears the candidate and restores
the original display, disables OK, reports the geometry diagnostic and cannot
confirm an earlier candidate. Missing/invalid directions are also refused.

The full reconstructed B-Rep uses the existing blue translucent, unselectable
AIS preview and original Part placement. The orange arrow remains unselectable
and in the topmost layer. Existing CommandSession confirmation/cancellation,
Escape/Enter routing and selection-owner restoration are reused. All controls
live in the existing raster Qt Tool panel managed by CommandPanelPositioner;
preview updates do not reset its placement or create a second tool window.

`tests/CylindricalMoveFaceSessionTest.cpp` runs through the affected Move Face
session target with `--cylindrical`. Coverage includes typed context-menu gating,
explicit references, angle/distance updates, actual reconstructed material,
visible blue previews, invalid-input recovery, both policies, source immutability,
basic Cancel/Escape/commit, Part placement/scale, real arrow dragging, panel bounds
and Active Part precedence. CTest registers normal and `QT_SCALE_FACTOR=2` runs.
Existing planar Move Face tests remain enabled because shared dispatch/session
code changes. Detailed Undo/Redo, Save/Open and lifecycle matrices remain 9E;
manual Fedora KDE acceptance must follow 9D before that phase.

Verification: incremental builds of `OpenDesigner` and `OpenDesignerMoveFaceTests`
passed. The focused `cylindrical-move-face-sessions`, its HiDPI variant and the
existing `move-face` CTests passed. No full CTest suite was run. Geometry APIs
remain unchanged; manual Fedora KDE acceptance is still required.

### Native document diagnosis at 097d922 (superseded by 9D.1)

The supplied file (SHA-256
`3fcabc46f01ee3d79a15a4ee3b8ae589c563d632a7efd889d0a8812abf5cae47`)
loads through `NativeDocumentStorage::load` as two visible Parts:

| Part | Actual B-Rep material |
| --- | --- |
| Unite 5 | Rectangular stock `(0,0,0)..(163,80,60)`, through-hole radius 17 at `(54,36)` over z=0..60, boss radius 18 at `(121,36)` over z=60..115; identity placement, 9 faces, one valid solid |
| Part 6 (Active Part) | Separate 100×80×60 box; local x=625..725, translated -402 along X, so world x=223..323; no cylindrical faces |

Unite 5 has volume 783907.964473723 mm³. Its two mantles are complete circular
features, but their **combination in one Part** fails the 9B/9C single-feature
contract. Recognition refuses the other cylinder before stock reconstruction;
simply ignoring it would also invalidate the source-material certificate and
risk losing unselected material. Separately reconstructed hole-only and boss-only
oracles satisfy the existing rules. Supporting their combination needs a further
certified feature-preservation design, not weaker validation.

Viewport face detection and typed membership resolve both cylinders to Unite 5;
the unrelated Active Part does not override the target. The 3D Modeling action
is enabled and reaches the same safe geometry refusal. Previously the context
menu silently removed Move Face on recognition failure. Unsupported cylinders
now retain a disabled Move Face action with the refusal in its tooltip/status
tip, and geometry diagnostics identify the additional cylindrical feature.
That commit fixed the missing explanation. Phase 9D.1 below removes this
single-feature-per-Part limitation with additional material/geometry certificates.

`cylindrical-move-face-recognition` tests a portable material-equivalent version
of this document. To replay the exact private document without adding it to Git,
run `OpenDesignerMoveFaceTests --cylindrical-document /path/to/moveCylinder.odc`.
That path uses the native reader, checks the independent material oracle and
real viewport detection, typed selection, menu/command-first activation, unchanged
source/history and unchanged input bytes. No supplied binary is stored in Git.

Verification: incremental application/session/geometry targets built; the exact
native-file replay passed for both cylindrical faces with byte preservation.
`cylindrical-move-geometry`, `cylindrical-move-face-sessions` and
`cylindrical-move-face-recognition` passed. `git diff --check` passed; no full
CTest suite was run.

### Preview acceptance contract

Expose **Trim / Extend** in the existing Move Face Command Panel for applicable
boss reconstruction. Keep the selected policy explicit and stable as the boss
crosses a boundary, numeric values change or the manipulator is dragged. Never
switch policies silently. Unsupported geometry produces a mode-specific refusal.

Rebuild each preview from the original snapshot and the selected policy, using
the same builder and validation as confirmation. Show the full reconstructed
solid, including truncated walls or extended supporting material. A mode change
invalidates the previous candidate; an invalid candidate disables confirmation
and clears/restores the preview according to the existing invalid-input behavior.
Do not leave a stale valid preview confirmable.

Test numeric and drag input across boundary thresholds, repeated mode changes,
invalid-to-valid recovery, Cancel/Esc, OK/Enter and preview cleanup. Compare the
preview B-Rep/material to the committed result for each mode, in addition to
rendered checks. Confirm the UI cannot commit a candidate from another policy
or an earlier displacement. Check existing planar and hole Move Face sessions.

## 9D.1 — independent cylindrical features in one Part

The geometry entry points and Move Face session infrastructure are reused.
`moveSelected` retains the original single-feature path, with a bounded
independent-feature path when other cylindrical supports prevent its stock
certificate. It does not ignore unrelated cylindrical surfaces or weaken the
original solid, radius/span, stock or material-side checks.

Recognition builds edge/face adjacency on an isolated source copy and grows
each mantle only through connected equal-radius coaxial cylinder faces.
Periodic seams/internal split edges are excluded from boundary rims. Each group
must have two complete concentric circular rims on perpendicular planar
neighbors, the complete mantle area and a definite oriented material side.
IN/OUT probes classify both radial sides of every feature in the actual source
before decomposition, as well as in its later isolated certificate.
Bosses require an adjacent full disk cap. Feature volume comes from these actual
rim coordinates and analytic cylinder supports, not creation metadata.

For this bounded scope, axes must be parallel and all source feature footprints
must be separated by more than 100 linear tolerances. Footprint overlap/tangency
is refused even when features occupy different axial spans; this prevents
ambiguous hole/boss support interactions. Up to eight independent features are
accepted within the existing 64-face/256-edge/vertex limits. Original features
must be complete, with footprints inside the rectangular stock.

The candidate decomposition fills holes and removes bosses to recover stock.
Each feature alone is reconstructed on that stock and passed through the
existing single-feature operation at zero displacement. This certifies the
rectangular stock, complete cap/support/rim geometry and radial IN/OUT material
sides independently for every feature. Original mantle/cap trimmed regions must
match their isolated certificates. Reapplying all signed feature volumes must
reproduce the entire original Part by Common material equivalence.

The selected destination footprint must also clear every unselected feature.
The original operation constructs the selected result with the requested Trim/
Extend policy (or hole boundary crossing/exit). Actual editing starts from the
original Part: fill only the old selected hole and cut the new one, or remove
only the old selected boss and fuse its translated/clipped material. Rebuilding
the other features is used only as an alternate material oracle, not as the
production replacement. Final normalization, one-solid/manifold certification
and equivalence to that oracle remain mandatory. Each unselected mantle/cap
region must survive as a Common-equivalent trimmed surface on the same oriented
support, with unchanged total mantle area. Split/merged face counts are allowed.

An interacting source returns an unsupported diagnostic; an interacting
destination returns `Failure / InteractingFeatures`. All refusals return null
geometry and preserve the original source. Independence is evaluated at the
source and requested destination, not along an animated/swept travel path.
Conservative refusals remain for nonparallel/cross-drilled features, blind holes,
coaxial stacked/interacting features, blends, split caps, nonrectangular stock,
partially truncated/overhanging source features and ambiguous decompositions.

The native reader regression now recognizes and previews both the R17 hole and
R18 boss in the supplied Unite 5. It checks actual viewport/context-menu and
command-first activation, material at old/new footprints, preserved unrelated
features, boss Trim/Extend at the stock boundary, collision refusal, cancellation
and unchanged document bytes. The portable equivalent remains the default
`cylindrical-move-face-recognition` test; the optional private `.odc` path is never
committed. `cylindrical-independent-features` adds headless hole+boss, two-hole,
two-boss, both selected features, boundary/exit, exact occupancy, surface/solid,
located/split-mantle and atomic source/destination interaction regressions.

Verification: incremental builds of the application, cylindrical geometry and
Move Face session targets passed. The four focused CTests
`cylindrical-move-geometry`, `cylindrical-independent-features`,
`cylindrical-move-face-sessions` and `cylindrical-move-face-recognition` passed.
The exact native `moveCylinder.odc` replay also passed for both selected features,
including Trim/Extend and interaction refusal, with the original SHA-256 intact.
`git diff --check` passed. No full suite was run; the supplied binary stays
untracked and unchanged.

## 9D.3 — edits after a confirmed boss boundary crossing

The exact supplied `moveCylinderBug.odc` (SHA-256
`a2033d919c2f962e2e95248d4d0c22b6bb2c77da49dd943b39729015103f9ac6`)
loads through the native reader. Unite 5 is one valid closed solid, 10 faces,
identity placement, persistent ID `slot-4`, volume 822307.964473723 mm³. Its stock spans
`(-8,0,0)..(163,80,60)`, its R17 hole is at `(54,48)` over z=0..60, and its full
R18 boss is at `(98,7)` over z=60..115, overhanging y=0 by 11 mm. Part 6 is a
separate valid box (Active Part), translated -402 in X; it remains independent.

Investigation distinguishes capability from lifecycle:

- Every actual planar face passes `MoveFaceGeometry::reference` independently
  of cylindrical recognition. The 3D Modeling action and typed face selection
  remain available. Real picks are checked against the intended current face;
  a screen ray occluded by the boss must not be mistaken for a planar pick.
- After actual OK for Extend and Trim, the stable Part presentation contains the
  new B-Rep. Every computed face owner is a member of that new shape; there are
  no obsolete owners. UUID, placement and presentation identity are preserved.
  Undo/Redo also expose only current faces. No capability cache or stale session
  was found; no AIS/session refresh workaround is introduced.
- Geometry is the failure point. The multi-feature recognizer requires every
  cylinder to meet the original complete, interior-footprint certificates, so
  an unselected boundary boss poisons recognition of an unchanged complete hole.
  Separately, OCCT's global intersection-offset builder returns no candidate for
  the valid saved overhang configuration. The all-planar cell fallback cannot
  accept its cylinders. Thus planar activation is valid but nonzero previews
  fail; this is not a broken planar-selection eligibility check.

The correction isolates only the unaffected stock edit using a complete inward
cylindrical hole's two physical rim planes. Its connected mantle, full circular
rims, oriented material side and actual planar neighbors are certified first.
Cuts with those exact planes partition the source into a stock slab and exterior
material. The slab must pass the original geometry operation's complete stock/
feature/material rules. The exterior must contain cylindrical geometry, bound
valid positive solids and recompose with the slab to the exact source material.
No bounding-box crop, arbitrary bridge or selected-boss recognition bypass is
used. Original input validity, tolerance and complexity limits still apply.

For a hole edit, both original and translated through-tools must have certified
positive clearance from all exterior material. Only the old hole is filled and
the new tool subtracted from the original Part. For a planar edit, the original
patch must survive isolation unchanged; only a stock side plane perpendicular
to the slab axis is accepted, with all exterior material strictly inside both
old and requested plane halfspaces. Conservative analytic bounds certify this
clearance, including curved extrema absent from B-Rep seam vertices; the bounds
never define/crop geometry. The unchanged planar operation rebuilds the slab,
then its signed added/removed material is applied locally to the original Part.
The offset builder receives the certified single solid inside any Boolean
compound wrapper.

Both paths require final normalization, BRepCheck, one closed manifold solid,
self-interference checks and equivalence to the independently recomposed edited
slab plus original exterior. Common verifies all exterior material is retained;
unpartitioned exterior face regions must survive on their original supports.
Collision/tangency, lost connections, ambiguous partitions and changed protected
surfaces fail atomically. An unsupported moved boss remains a feature-level
refusal; it does not disable a separated hole or certified unaffected side plane.
This fallback requires a complete through-hole to establish the physical stock
slab. Editing interface/feature-touching planes, repeated movement of partial/
overhanging bosses and unrestricted freeform reconstruction remain unsupported.

Focused regressions reproduce actual OK for both modes, then real planar picks,
nonzero planar/hole previews, repeated supported edits, explicit boss refusal,
Cancel, cached Undo/Redo, face-owner membership, stable UUID/presentation/placement
and another Part's activation. The optional `--cylindrical-boundary /path/to/file`
path uses the supplied native document without saving it. Portable equivalent
geometry is registered as `cylindrical-move-face-boundary`; headless
`cylindrical-boundary-follow-up-geometry` checks independent material/occupancy,
both planar signs, hole boundary crossing/exit, preserved exterior geometry and
safe collision refusal. No supplied binary is committed.

Verification: incremental builds of `OpenDesigner`, `OpenDesignerMoveFaceTests`
and `OpenDesignerCylindricalMoveTests` passed. Seven targeted CTests passed:
`cylindrical-move-geometry`, `cylindrical-independent-features`,
`cylindrical-boundary-follow-up-geometry`, `move-face`,
`cylindrical-move-face-sessions`, `cylindrical-move-face-recognition` and
`cylindrical-move-face-boundary`. The exact native bug-document replay also
passed for both confirmations and subsequent edits, with zero stale face owners
and the original SHA-256 unchanged. `git diff --check` passed. No full suite or
unrelated tests were run.

## 9D.4 — geometry-driven neighborhood foundation

See [strategy, component review and extension boundaries](GEOMETRY_DRIVEN_MOVE_FACE.md).
Face capability is evaluated from the current B-Rep and topology regardless of
whether its geometry originated in a primitive, sweep, Boolean, direct edit or
STEP import. No creator flags, feature tree or stored feature IDs enter this work.

`BRepReconstruction::neighborhood` now isolates the source with explicit face
copy correspondence, grows the connected selected analytic support patch and
collects actual boundary edges and adjacent faces. Its checks concern source
validity, tolerance, complexity and unambiguous adjacency; rectangular stock and
global cylindrical feature counts are not analysis prerequisites. Neighborhood
analysis does not claim every analyzed face has a certified reconstruction path.

`finishLocal` normalizes a candidate, verifies material equivalence outside a
caller-certified changed-material region and requires every unaffected trimmed
face region to survive. It returns geometric old/new correspondence, allowing
multiple descendants or disappearance of affected faces. This is current topology
mapping, not feature history or persistent naming. Operation-specific inside-region
material checks remain mandatory; a declared local region is not a safety bypass.

The first new reconstruction path moves an unchanged complete boss after a
through-hole has opened into a boundary notch. It certifies the current mantle,
full circular rims, disk cap, planar support and radial material sides, then uses
OCCT defeaturing as Delete Face does. Removal must independently equal subtracting
the exact finite boss cylinder from the original shape. Original and translated
base disks with a tolerance collar must lie entirely inside the actual healed
support face, including all of its current hole/notch trims. Re-fusion must equal
the direct Cut/Fuse material oracle and contain the entire translated boss.
Neighborhood-based outside-material and unaffected-surface checks then certify
the result. There is no rectangular stock reconstruction or global feature-count
limit on this local path; existing source complexity budgets remain enforced.

Trim and Extend agree for this fully supported local footprint. Existing boundary
paths retain their tested truncation/overhang behavior. Contact-changing moves,
partial/overhanging selected bosses, split caps, blends/freeform reconstruction,
multi-shell solids and cases lacking a unique local closure need later operation
certificates and are refused safely. Unrelated geometry is not regenerated or
deleted to make a pattern match. Repeated edits analyze the new B-Rep again.

`local-neighborhood-move-geometry` tests repeated boss moves beside a boundary
notch, both policies, exact material occupancy, B-Rep validity, unchanged input,
result-face mapping, rejection of outside-region material changes and interacting
destinations, and a boss beside nine unrelated holes (beyond the old global count
limit). The native document/session regression now opens the R17 hole in
`moveCylinder.odc` to a notch, confirms repeated local R18 boss moves, verifies
both policies/material preservation, and then previews an ordinary planar edit.
The opposite sequence (hole/plane edits after boss boundary crossing) remains in
the 9D.3 regression. Supplied native files remain private, unchanged and untracked.

Verification: incremental application/session/geometry builds and all eight
focused Move Face/cylindrical tests passed. The final complete-wall/cap survival
guard was rechecked with the local neighborhood test and native-reader replay of
both supplied documents. No full CTest suite was run.

## 9D.5 — post-boss boundary reconstruction

See [diagnosis, local certificates and exact limitations](BOSS_BOUNDARY_FOLLOW_UP.md).
Native replay did not reproduce a whole-Part action disable: all planes recognize,
the right side and unchanged hole already edit, and AIS owners are current. The
expanded regression reproduced axial planar preview failures after both policies:
the primary OCCT offset returns no shape, while existing fallbacks require a
recoverable rectangular slab or an entirely planar Part. Earlier tests checked
only the right-hand side plane, hiding this reconstruction gap.

Add local constant-section planar reconstruction from the actual trimmed face,
and a complete-hole local fill/cut certificate where a later planar edit makes
the old rectangular hole-rim slab assumption fail. Both reuse neighborhood copy
mapping, protected-material/face checks, non-destructive Booleans and strict solid
certification. Recognize cylinder supports independently of parameter-axis sign;
retain certified split faces if optional same-domain unification fails. Preserve
explicit boss policy semantics and refuse non-invariant curved intersections or
unsupported selected partial/overhanging bosses per face. No stock engine or
feature-history dependency is added.

Verification: incremental application, geometry/session and shared face-transform
builds passed; all nine focused regressions passed. Separate native-reader replay
of both supplied documents passed both policies and post-confirmation edits.
Binary files remain unchanged/untracked. No full CTest suite was run.

## 9D.6 — exact crash document and changing boss trim

See [exact reproduction, coredump and certificates](CRASH_03112026.md).
The actual private native file has a merged right-end stock/boss cut face at
x=163. Its +5 mm preview reproducibly SIGSEGVs in OCCT 7.9's offset intersections,
matching the original coredump. The prior constant-section local path refuses
this changing circular trim and, being dispatched after offset, cannot protect it.

Run certified local paths first. Reconstruct this outward planar cut from the
actual finite boss cylinder and original/moved plane halfspaces; move its healed
support with existing local planar reconstruction. Certify both material
partitions and protected neighborhoods without a stock proxy. If a transverse
cylinder contact cannot be reconstructed, refuse before the unsafe offset path,
including across coplanar seams. Preserve normal editing on other supported faces.

The dedicated isolated-process crash regression uses a portable native-storage
equivalent and accepts the original private path for exact replay. It checks
analytic occupancy/volume, topology transition to a full cylinder, actual arrow
drag, preview lifecycle, cached history and source/file immutability. Inward and
multi-clip/interacting cases outside the new certificate remain safe refusals.

Verification: incremental application and affected targets built; all six focused
geometry/session regressions passed. Exact-file replay in an isolated GDB process
also passed with an assertion that the unsafe offset routine was never entered.
The original native file remains unchanged and untracked. No full CTest suite ran.

## 9D.6B — Pull/Resize after a boss Trim

See [newest coredump, radial reconstruction and limits](PULL_AFTER_BOSS_TRIM.md).
The newest crash is in `applyCylindricalFacePull` during drag preview, independently
of planar Move Face dispatch. A valid-solid/surface-type check had admitted a
partial trimmed mantle to OCCT's global offset intersections, which SIGSEGV.

Add current-B-Rep radial reconstruction using existing neighborhood and material
certificates. Keep actual axial/cut planes fixed, preserve unrelated surfaces,
require supported boss footprints or complete exterior hole rims, and validate
the final manifold solid. Certify capability at activation; every preview starts
from the immutable source. No offset fallback or feature-history inference is
permitted. Radius/diameter editing is added for the certified classes; general
height Pull and unprovable support extensions remain outside this hotfix.

The focused headless radial tests and isolated post-Trim Pull session cover
material occupancy/volume, p-curves, topology changes, repeated edits, safe
refusal, source identity, Cancel and cached Undo/Redo. Native replay also checks
that the original document is unchanged and the unsafe offset routine is absent
from the executed path. Supplied binary documents remain private/untracked.

Verification: incremental application and affected geometry/session builds and
all seven focused regressions passed. Final numeric confirmation and native-file
GDB replay passed; the unsafe offset routine was never entered. Original file
hashes are unchanged. No full CTest suite was run.

## 9D.7A — repeated open-notch Move Face

See [recognition, local material certificates and tests](OPEN_CYLINDRICAL_NOTCH_MOVE.md).
Recognize inward partial cylindrical patches from current rims, generatrices,
two axial exterior planes and one planar side support. Infer and certify the
missing cylindrical material on that support's material side, heal only that
volume, and subtract the translated cylinder. No original feature position,
primitive metadata, stock proxy or stored boundary policy is needed.

Certify actual source/fill/re-cut material, destination through-span, outside
material and unaffected trimmed surfaces using existing local reconstruction.
Permit only actual planar boundary regions intersected by the new cutting tool
to join the affected set, enabling complete holes to reopen on nonrectangular
supports. Revalidate the destination neighborhood, allowing complete restoration,
one-sided notches on another edge, face splits/merges and complete exit. Corners,
depth variation and interacting features remain atomic diagnostic refusals.

Reuse existing sessions and selection with explicit lateral references, live
reconstructed previews and cached Undo/Redo. New focused headless and session
regressions cover repeated directions, imported/split topology, rotated/scaled
placements, unaffected features, numeric input, dragging and Escape. Native
replays use the supplied documents read-only. No offset fallback is introduced.

## 9E — transactions, persistence and acceptance

Confirm once into the same Part slot with one cached Undo/Redo step. Preserve
Part identity, name, appearance, visibility, placement, presentation and browser
references. Undo restores the exact source; Redo restores the validated result
without rebuilding under a different policy. Cancel and every refusal leave
document geometry and history unchanged.

For both policies, round-trip native Save/Open and compare geometry, material
occupancy, connectivity, placement and Part metadata. Test Save/Open after
Undo/Redo as supported by the existing document history contract; do not assume
undo stacks persist across reopening. Require preview/commit agreement and
final B-Rep validation before replacement. Exercise edge/corner crossings,
tangency and disconnected/ambiguous constructions through the GUI and document
layer. Include unrelated Parts/features and regression coverage for existing
Move/Rotate/Align operations. Manual acceptance must demonstrate both modes on
the same boundary-crossing move and safe refusal without source changes.
