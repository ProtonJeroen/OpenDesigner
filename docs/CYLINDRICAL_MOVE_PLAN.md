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
