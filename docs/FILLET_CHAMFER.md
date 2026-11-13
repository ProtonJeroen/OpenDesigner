# Fillet and Chamfer — v0.5.2

Choose **3D Modeling > Fillet** or **3D Modeling > Chamfer**, then click one
edge on a visible Part. Enter a positive radius or equal distance in millimeters
in the existing floating command panel. Fractional values retain double precision;
there is no snapping or rounding of the requested treatment. The inherited numeric
editor range is 100000 mm. For scaled instances the world distance is converted
to local distance once; placement is preserved.

The complete candidate solid appears in blue with 25% transparency. Numeric
changes rebuild from the original B-Rep. Invalid geometry or incomplete input
removes the candidate, restores the original presentation, disables OK and reports
the reason in the status bar. Preview never writes canonical document geometry.
OK/Enter confirms one cached FaceChange; Cancel/Escape or a tool switch removes
all preview objects and leaves history, including Redo, intact. Picking returns
to normal Face selection after termination. Selecting another object after the
edge is chosen follows the existing command cancellation convention.

## Geometry and safety

`FilletGeometry::apply` uses `BRepFilletAPI_MakeFillet` with one selected edge and
a positive constant radius. `ChamferGeometry::apply` uses
`BRepFilletAPI_MakeChamfer` with two identical distances and a face obtained from
actual edge-to-face adjacency. Equal distances make the chosen face side
immaterial. Both operate on one deep copy and map the selected topology into it.
No mesh conversion or healing is used; editable transitions use the verified reconstruction recipes below.

`EdgeBlendValidation` checks finite positive input, membership, non-degenerate
edges, two distinct adjacent faces and absence of seam topology. Both paths use
the existing `BRepReconstruction::validSolid` policy before and after construction:
one solid, one closed shell, no stray face/edge/vertex topology, BRepCheck validity,
positive finite volume and isolated self-interference analysis. OCCT must complete
successfully. Exceptions become actionable errors; failures return a null shape.

Candidates reuse `presentFacePreview`, including tessellation and nonselectable
blue AIS display. Confirmation replaces the B-Rep of the same Part slot, marks
it GeneralBRep and records complete before/after snapshots. UUID, appearance,
visibility, placement and browser order use the existing FaceChange lifecycle.
Native `.odc` storage saves the exact resulting B-Rep without a schema version
change. Optional feature-reconstruction recipes are described below. Save during
preview saves confirmed geometry. Undo/Redo restores cached shapes and clears selection.

## Scope and limitations

One selected edge on one Part per command. Ordinary convex box edges and concave
prismatic notch edges are tested. OCCT may extend a blend along a tangent contour;
there is no separate multi-edge/junction control. Oversized treatments, seam,
non-manifold, foreign and degenerate edges are refused. The shared validator
conservatively rejects solids with multiple shells, including enclosed cavities.
Fillet accepts straight edges and complete certified circular contours. Elliptical,
spline and freeform edges are outside this milestone. Variable radii,
asymmetric/angle chamfers, setback corners, face-to-face blends and multi-edge
selection remain future work. Geometry computation is synchronous, like existing
face commands; expensive arbitrary models have no background cancellation.

## Automated verification

`Fillet-geometry` and `Chamfer-geometry` check analytic convex material volume,
concave material addition, invalid sizes/selections, valid closed solid topology
and byte-identical serialized source geometry across success and failure.
`edge-blend-sessions` checks OCCT edge picking on a translated/rotated Part,
fractional input, blue-preview ownership, malformed input cleanup, Escape/Enter,
Cancel/Redo preservation, one-step history, stable identity/metadata/Part slots,
unrelated Part preservation and native Save/Open including confirmed-only Save.

Initial creation validation on Fedora 44, Release build, 2026-11-10:

- `cmake --build build -j 4`: PASS.
- Targeted `Fillet-geometry` and `Chamfer-geometry`: 2/2 PASS.
- Targeted `edge-blend-sessions`, `selection-context`,
  `native-document-sessions`, `command-panel-sessions`: 4/4 PASS (65.00 s).
- `ctest --test-dir build --output-on-failure -j 2 --timeout 300`:
  92/92 PASS, 0 failed, 0 skipped (759.78 s), including all 89 existing tests.
- `git diff --check`: PASS.

Viewport tests required execution outside the filesystem/network sandbox to access
X11/OpenGL. Initial sandboxed viewport attempts aborted before test output; all
four passed with display access. No tests were disabled or assertions weakened.

Initial creation was subsequently accepted manually on Fedora KDE. The new
feature-aware controls below still require manual acceptance; automated viewport
checks do not establish behavior on every compositor, screen or display scale.

## Fedora KDE acceptance checklist

1. Launch `build/OpenDesigner`, create a Box, choose **3D Modeling > Fillet**,
   then click a visible convex edge. Enter `2.345` mm (use the locale decimal
   separator); verify a complete translucent blue round and unchanged browser row.
2. Change the radius repeatedly. Enter zero, a negative number, incomplete input
   and an oversized radius; verify the original returns and OK is unavailable.
   Correct the value, then Cancel. Repeat and press Escape while editing the field.
3. Confirm using OK; verify one Undo restores the box and one Redo restores the
   fillet. Verify name, color, visibility, Active Part and placement remain correct.
4. Repeat with **Chamfer**, using an equal distance of `2.345` mm; inspect both
   adjacent faces. Repeat cancellation, invalid input and one-step Undo/Redo.
5. Rotate/translate a Part and repeat both operations. Check placement and exact
   world-space treatment size. Test a concave notch edge when available.
6. Save during preview and reopen a copy: only confirmed geometry should exist.
   Save each confirmed treatment, reopen and inspect the solid and Part identity.
7. Switch tools during edge picking and preview; invoke Undo/Redo during preview;
   verify no blue objects, stale edge highlights, gizmos or panels remain.
8. Repeat at normal and 200% display scale, checking numeric focus, panel placement,
   typing and OK/Cancel visibility. Camera orbit/pan/zoom must preserve the session.

## Feature-aware direct modeling

The initial creation workflow above was manually accepted on Fedora KDE (including
normal/200% display scaling). Feature editing and Keep behavior below require the
new manual acceptance checks; the earlier 92-test result describes the initial
creation implementation, not validation of these subsequent changes.

### Confirmed regression causes

`OpenDesignerEdgeBlendFeatureTests --baseline` reproduces the old geometry routes
using current member faces on a 100 x 80 x 60 Box, an R5 vertical Fillet and an
opposite 3 mm Chamfer:

- Box and Fillet face/edge/vertex maximum tolerances: `1e-7`.
- After Chamfer: faces and edges remain `1e-7`, vertices reach `1e-4`.
- Calling OCCT's symmetric chamfer builder directly also creates `1e-4` vertices;
  this is already present in the kernel result, before viewport selection,
  snapshots or native serialization. Changing to the symmetric API is not a fix.
- The legacy ordinary planar Move route then refuses its source at the existing
  `1e-6` neighborhood tolerance check. A Fillet-only cap can move in that route.
- Delete Face's hole/boss and removable-region certificates cannot establish the
  sharp intersection of these transition supports. A cylinder surface alone
  cannot distinguish a Fillet from a hole or boss.
- GeneralBRep Pull dispatch previously offered planar faces only for recognized
  primitives or supported boss caps; it had no transition-dimension adapter.

No tolerance is clamped, relaxed or arbitrarily overwritten. The shared
`BRepReconstruction` validators and their existing limits are unchanged.

### Certification and persistent information

`EdgeBlendFeatures` stores a sharp local B-Rep, feature IDs, type, absolute size,
and pairs of supporting planes (normal and signed plane offset). This is an
optional reconstruction recipe, not an authoritative history tree. No face
indices, edge indices or OCCT pointer values are serialized.

New supported creations record their sharp supports. To certify a recipe after
Open or topology replacement, replay the transitions using the OCCT kernels and
require full material equivalence with the actual current B-Rep. Bind the current
faces by geometric carrier correspondence only after that certificate succeeds.
Face handles are then cached solely for that certified shape; stale member faces
are refused. Generic topology replacement clears a recipe whose certificate no
longer matches. Invalid persisted information can be discarded and independently
recognized again, rather than reviving stale supports.

For imported/older B-Reps without recipes, recognition is deliberately narrower:
rectangular stock with six distinct axis-aligned local planar supports, plus
separated cylindrical Fillets tangent to two perpendicular side planes or planar
equal-distance Chamfers on those planes. Distances, tangency, actual neighbors,
sharp closure and complete reconstructed material are checked. An ordinary
cylinder, arbitrary inclined plane, unequal chamfer or ambiguous/split surface
is not enough to expose Edit. A rotated/translated Part instance retains its
local coordinate system; arbitrarily rotated imported B-Reps without recipes
are outside this recognition scope.

Optional native metadata `OpenDesignerEdgeBlendRecipeV1` stores the exact sharp
B-Rep and geometric descriptors. Older readers can ignore it. The current edited
B-Rep remains authoritative. Snapshots carry recipes through Undo/Redo and Part
duplication; feature IDs are scoped to their owning Part. Instance transforms
remain outside the local B-Rep. Save during preview includes only the confirmed
recipe and solid.

### Edit, Pull and Keep

Right-click a certified transition face to choose **Edit Fillet** or **Edit
Chamfer**. The editor shows its existing absolute radius/distance in world mm.
Move Face or Pull on a recognized transition uses the same dimension editor;
it never translates the transition surface to approximate a size change.

Creation and editing share the existing orange Pull arrow and drag handling.
The arrow changes the actual absolute feature dimension, synchronizes numeric
input, and rebuilds a blue complete-solid preview. Identical/subpixel redundant
pointer changes skip reconstruction. Numeric input retains full double precision.
Cancel/Escape, tool changes and history remove the arrow and preview.

**Keep Fillet / Chamfer** defaults ON for every Move Face, Rotate Face and
Pull/Resize operation. On supported ordinary planar faces:

- ON: edit the certified sharp supporting solid, update the selected supporting
  plane, then reconstruct transitions with their original radii/distances.
- OFF: identify dependent transitions by actual face adjacency, including end
  caps; omit only these transitions, perform the sharp face edit, and reconstruct
  unaffected transitions. No open shell or partially deleted Part is committed.
- One complete candidate is previewed and one FaceChange is committed. A failure
  refuses the entire operation. Cancel restores the exact source and recipe.

Ordinary planar Pull on a Part with certified transitions uses the same signed
face-offset adapter as Move Face. Existing primitive Pull conventions remain in
place for ordinary unfeatured Parts. Supporting-cap rotation is certified by
sharp reconstruction and preserves the transition dimensions. Existing direct
rotation of a planar Chamfer uses the established Rotate Face geometry route;
when it ceases to be an equal-distance Chamfer, its old recipe is invalidated
instead of displaying a false Edit dimension. Keep is explicitly disabled with
an explanatory tooltip for this legacy face-rotation mode; dimension-preserving
Keep ON/OFF requires a supporting face. This is a supported-scope boundary, not
a switch to approximate equal-distance reconstruction.

Delete Face on a recognized transition restores its sharp supporting intersection
and replays all remaining transitions. Mixed transition/ordinary-face deletion is
refused atomically by this adapter. Ordinary deletions continue through the
existing Delete Face certificates; arbitrary stock-face removal is not promised.

### Supported boundary and refusal policy

Known recipes require a valid planar sharp solid whose face/edge/vertex
tolerances meet the unchanged `1e-6` limit. Each reconstructed transition must
produce one cylindrical Fillet face or one planar Chamfer face on a unique
straight sharp edge. Up to eight separated transitions are supported; transition
faces may not meet at an edge or vertex. Each rebuilt solid passes the existing
blend completion, validity, closure, positive-volume and self-interference checks.
OCCT's conservative Chamfer vertex tolerances are retained; strict sharp-face
editing operates on the independently certified low-tolerance support solid,
never by loosening the legacy neighborhood check on those vertices.

Missing/consumed/split/curved support edges, interacting junctions, unsupported
surface types, lost supports, failed kernel operations and invalid dimensions
are refused. Supported general single-edge creation remains available through
the original kernel path when a recipe cannot be certified, but it does not
advertise unsupported feature editing. There is no speculative freeform healing,
mesh approximation, multi-edge corner algorithm or generalized selection system.

### Targeted validation — feature-awareness update (2026-11-11)

Affected Release targets were built incrementally with at most four jobs:
`OpenDesigner`, `OpenDesignerSelectionContextTests`,
`OpenDesignerEdgeBlendFeatureTests`, and `OpenDesignerNativeDocumentTests`.

Seven unique targeted CTests passed, with no tests disabled or assertions weakened:

- `edge-blend-features`: analytic material checks for single/mixed transitions,
  radius/distance editing, Keep ON/OFF, cap rotation, sharp deletion, persisted
  support recertification, exact source preservation and safe rejection of stale,
  interacting, invalid-size and stray-topology inputs.
- `edge-blend-feature-sessions`: actual command/menu lifecycle, legacy B-Reps
  without metadata, both orange manipulators, exact values, Keep failure recovery
  and commit/Undo/Redo, planar Pull, cap/direct-Chamfer rotation, Delete Face,
  edited-dimension Save/Open, Part transforms and independent duplicate editing.
- `edge-blend-sessions`: existing creation, typed picking, invalid input,
  cancellation and persistence, extended to verify creation dragging/release.
- `selection-context`, `native-document-sessions`, `command-panel-sessions`.
- `native-document-roundtrip`.

The six-test viewport/feature batch passed in 71.60 seconds. The feature-specific
checks were subsequently rerun for the final source-topology guard and explicit
legacy-rotation control boundary: geometry PASS (0.85 seconds), viewport PASS
(8.51 seconds). All seven unique targeted tests are green; none were skipped. The source-preservation assertion is retained;
invalid compound fixtures use an isolated copy because OCCT's compound builder
freezes child topology. Native roundtrip passed separately (0.54 seconds).

The full CTest suite and expensive unrelated cylindrical geometry regressions
were deliberately not run for this update. The earlier 92-test result above is
historical evidence for initial creation only. New KDE feature-awareness manual
acceptance remains pending.

### DellJV manual feature-awareness acceptance

1. Make a Box with separated R5 and 3 mm vertical transitions. Select each face
   normally; right-click Edit and verify the current value. Try exact fractional
   input, orange dragging, invalid input, Cancel and Escape.
2. Move a shared cap outward 20 mm with Keep ON. Confirm both dimensions remain
   5/3 mm. Undo/Redo once. Repeat with Keep OFF and verify both sharp corners.
3. Move a side supporting only the Fillet with Keep OFF. Verify the Fillet is
   removed while the opposite Chamfer remains 3 mm. Cancel, then repeat with ON.
4. Repeat ordinary planar Pull/Resize and a small supporting-cap Rotate with both
   policies. Check each new panel defaults ON. On a transition face, Pull/Move
   must change its absolute dimension rather than translate the face.
5. Delete either transition face. Inspect sharp closure and preservation of the
   other transition before OK; check Cancel and one-step Undo/Redo.
6. Repeat on a rotated/translated Part, then duplicate it and edit the copy.
   Save during preview and after confirmation; Open and edit both recognized
   features again. Check Part identity, appearance and browser order.
7. Check safe refusal of touching transitions and missing/ambiguous supports,
   and repeat the new controls at normal/200% KDE display scale.

## v0.5.1 circular-edge Fillets

The same constant-radius fillet engine handles cylinder top/bottom rims, circular
hole entrances/exits and circular boss feet. Circular curves are identified by
`BRepAdaptor_Curve`, not tessellation. Coincident circular segments must form a
valid closed wire with total sweep 2π; every member must have two distinct
adjacent faces and pass seam/degeneracy checks. Partial arcs and ambiguous or
incomplete circular contours are refused. All operations use isolated source
copies and the existing closed-solid, volume and self-interference validation.

Editable recipes support a planar face and one cylindrical carrier. They retain
geometric plane/cylinder references, sharp stock and the stable feature UUID;
face handles are transient caches. Replay must reproduce equivalent material
before loaded metadata is accepted. A unique generated toroidal transition is
bound to current topology, allowing **Edit Fillet**, absolute numeric radius,
Undo/Redo, native Save/Open and independent Part duplication. Ordinary cylinders
and arbitrary toroidal modeling faces do not become features based on type alone.
Imported circular transitions without recipes deliberately have no Edit Fillet.
Split supporting faces or multiple transition faces are not yet editable; the
kernel may create a valid contour without attaching an editable recipe.

Creation and Edit reuse the orange Pull arrow and blue whole-solid preview.
Dragging changes the radius parameter, including on rotated/translated Parts;
instance scale converts between local and world dimensions. For circular Edit,
the arrow lies at the torus major circle and points along its axis, avoiding the
zero radial vector at a full torus face's center of mass. Creation uses a point
on the selected circular contour and the supporting planar face normal.

**Keep ON/OFF boundary:** planar cap translation through Move Face or Pull is
supported when the cap is perpendicular to every circular recipe carrier and the
existing sharp-source engine certifies the offset. Keep ON replays the same
radii; Keep OFF removes only transitions adjacent to the selected cap before
replay. Separated unaffected transitions remain. The material-certified
normalization already used by reconstruction merges cylindrical sweep bands;
strict sharp-solid validation and the `1e-6` tolerance ceiling still apply.
Circular support rotation, cylindrical-wall lateral Move and radial Pull with
retained circular transitions remain unsupported and report a reason. The tested
top through-hole rim Keep-ON cap edit remains refused when replay cannot find a
unique support edge. Split or ambiguous carriers, invalid offsets and oversized
rounds are refused without changing document geometry. Straight-edge behavior is unchanged.

**Delete Face:** selecting a recognized circular Fillet restores its certified
sharp intersection by replay, preserving other separated recipe transitions.
This is atomic and uses existing preview/history handling. Imported toroidal
faces without certified recipes have no guaranteed sharp restoration.

### DellJV / Fedora KDE manual circular acceptance

1. Create a cylinder; Fillet its top and bottom separately with `2.345` mm.
   Inspect the complete blue candidate; drag the orange arrow at several view
   angles, then test malformed/zero/negative/oversized input and recovery.
2. Cancel with Escape; repeat and confirm with Enter. Check one-step Undo/Redo.
3. Right-click the new round and choose Edit Fillet. Check the current absolute
   radius, numeric edits and orange dragging. Confirm ordinary face selection.
4. Repeat on a through-hole entrance/exit and a fused boss base. Inspect material
   removal at rims/holes and addition at the concave boss base.
5. Save/Open; edit again. Duplicate and transform the Part; edit the duplicate
   independently. Check scale-aware world radius where applicable.
6. Move/Pull each planar cap with Keep ON and OFF. Check the retained radius,
   removal of affected rounds only, and preserved opposite rim. Attempt circular
   wall Move/Pull, support Rotate and a collapsing offset; verify a specific
   refusal and unchanged geometry/history. Delete the recognized round and
   inspect the closed sharp solid; Undo/Redo its restoration.
7. Check partial/split/unsupported imported topology fails safely or remains an
   ordinary selectable solid without an editable recipe. Repeat a straight-edge
   Fillet and Chamfer to check their existing workflow.

Manual KDE/Wayland acceptance remains pending; automated session checks do not
certify every viewing angle, compositor or display scaling setting.

### v0.5.1 initial targeted validation — 2026-11-11 (before recovery)

Incremental Release builds succeeded for `OpenDesigner`,
`OpenDesignerFilletTests`, `OpenDesignerChamferTests`,
`OpenDesignerEdgeBlendFeatureTests`, `OpenDesignerSelectionContextTests` and
`OpenDesignerNativeDocumentTests` with Ninja (`-j4`). CTest used `-j2` and exact
names only:

| Test | Result | Seconds |
| --- | --- | ---: |
| circular-fillet-geometry | PASS | 0.44 |
| Fillet-geometry | PASS | 0.11 |
| Chamfer-geometry | PASS | 0.09 |
| edge-blend-features | PASS | 1.42 |
| edge-blend-sessions | PASS | 6.69 |
| edge-blend-feature-sessions | PASS | 8.76 |
| native-document-roundtrip | PASS | 0.17 |
| native-document-sessions | PASS | 3.01 |

All eight targeted tests pass. The circular test was rerun alone after diagnosing
an unsuitable split fixture: OCCT's shape-divider introduced 1e-5 edge tolerances.
It now uses fused exact half-cylinders and verifies complete-contour volume and
byte-identical input. Production tolerances and assertions were not relaxed.
The circular geometry test also verifies analytic volumes at both outer rims,
both hole rims and a boss foot; recipe edits/roundtrips, tamper rejection,
separated rim preservation, sharp deletion, Keep refusal, invalid radii, seam,
degenerate and incomplete contour rejection. Circular session coverage includes
typed picking, orange dragging, transformed placement, numeric edits, preview,
Cancel, Undo/Redo, native Save/Open, duplication, Keep refusal and Delete Face.

The full CTest suite, expensive cylindrical Move Face regressions and manual
KDE/Wayland acceptance were deliberately not run for this milestone.

### Selection-first command startup

Fillet and Chamfer automatically reuse exactly one currently selected,
membership-validated Edge from any shared command entry point. This initializes
the existing numeric panel, orange manipulator and live preview; no second click
is needed. Fillet accepts its supported straight and circular edges. Chamfer
uses its existing geometry validation; an unsupported edge cannot produce a
confirmable invalid preview.

No selection, Face selection and Part selection retain Edge-picking mode.
Multi-edge, mixed and stale sets report that the set is unsupported and allow
an explicit single Edge pick. They never silently use the first member.
Escape leaves geometry/history unchanged; confirmation uses one Undo/Redo step.


### Direct-modeling recovery investigation — 2026-11-11

The primary fixture was the existing, untracked repository-root `fillet.odc`.
It is read only and is never saved over or included in Git. SHA-256:
`4b3640115549a00f73f4d472047e1c3bc04b823a139e298cf50379aa64978eb7`.
Deterministic OCCT fixtures reproduce equivalent material for both reported
Parts, verified against the original file in the optional document regression.
These generated fixtures are also tested independently of private user data.

| Part | Actual surfaces and metadata | Cause before recovery |
| --- | --- | --- |
| Cylinder 2 | Radius 40, height 80; cylindrical wall, two planar caps, top torus major/minor radii 20/20; certified version-2 recipe and sharp cylinder | `d8ddaa5` introduced a blanket circular-support edit refusal, including the unaffected bottom cap. Sharp cap sweeps also retain split cylindrical bands that make carrier lookup ambiguous without certified normalization. |
| Cone 3 | Original cone radius 40 to 20 over height 80, ordinary cone slope -0.25; top conical equal-distance Chamfer 8; bottom toroidal Fillet radius 11 (major radius approximately 25.91146); two planar caps; no recipe or stored sharp source | Existing recipe recovery only recognizes rectangular stock, and replay supports planes/cylinders, not cones. Primitive Pull correctly rejects a five-face blended cone. Move Face accepts planar targets but cannot certify a torus-containing global offset or a local sweep with changing conical cross section. Delete Face rejects unrecognized toroidal surfaces. |

Cone 3's transition classification is supported by material equivalence with an
independently constructed cone, top Chamfer and bottom Fillet. Its ordinary cone
wall is **not** classified as a Chamfer based on its surface type. Neither
transition is exposed as editable without a certified reconstruction recipe.
This is an existing geometry/recognition boundary, not evidence of an OCCT
kernel defect or a regression in selection/highlighting.

Reproduction covered every actual Face after Part selection, both command
initialization paths, Keep state, feature resolution and raw geometry errors.
Face ownership and topology membership remained valid; no stale owner or HLR
presentation regression was found. Automated viewport picks also return from
Part to the actual cylinder cap and conical wall. Historical user clicks cannot
be inferred from a saved document: a browser Part selection still requires an
explicit Face pick for Face commands, and never chooses an arbitrary Face.

Planar Pull on a nonprimitive solid now reuses Move Face when no existing
primitive/boss-height Pull applies. This restores command targeting and exposes
the actual geometric failure on Cone 3's caps. Existing absolute boss-height
Pull semantics are preserved. Ordinary complete cones retain diameter/height
Pull and planar-cap Move. Conical through-hole Delete still restores certified
stock. Ordinary conical-wall Move, Cone 3 transition editing/deletion and cap
offsets remain unsupported; failures keep geometry/history unchanged and provide
specific diagnostics. Conical Face menus allow invoking Pull/Move to obtain the
reason rather than silently hiding both commands.

Run the portable fixture with `ctest --test-dir build -R
'^direct-modeling-recovery$' --output-on-failure -j1`.

Since the synthetic-fixture conversion (after v0.5.2),
`direct-modeling-recovery-document` no longer depends on the private
`fillet.odc` and is always registered. It builds the v0.5.1 fixture in code
(Cylinder 2: radius 40, height 80, certified 20 mm top Fillet recipe; Cone 3:
R40 at z=0 to R20 at z=80, 8 mm top Chamfer, 11 mm bottom Fillet, no recipe),
saves it through `NativeDocumentStorage` to a temporary `.odc`, reopens it and
runs every recovery assertion on the reopened document. Run it with
`LC_ALL=C ctest --test-dir build -R '^direct-modeling-recovery-document$'
--output-on-failure -j1`.

An optional local probe runs when `OPENDESIGNER_PRIVATE_FIXTURE` points to a
`.odc` file at test run time. The file is copied to a temporary directory and
only the copy is opened; the original's SHA-256 is verified unchanged. When both
Parts match the v0.5.1 fixture the full recovery assertions also run on the
copy; otherwise the probe reports which Part diverged and skips them. CI never
sets the variable. The former `OPENDESIGNER_REGRESSION_DOCUMENT` CMake option is
removed.

Coverage includes both caps × Pull/Move × Keep ON/OFF, independent material
oracles, B-Rep validity/tolerances, source immutability, preserved separated rims,
Edit/Delete Fillet, failure/Cancel, Undo/Redo, edited-recipe Save/Open, real
picking, ordinary cone diameter Pull and cap Move, nonprimitive conical-hole cap Pull,
conical-hole Delete, and safe
refusal on all five Cone 3 Faces. The user accepted all five selection/highlighting checks before this recovery.
Picking and highlighting implementations are unchanged; recheck the manual
items below after this geometry change.


Fedora KDE recovery acceptance:

1. Open the original document. Select Cylinder 2 in the browser, then click a cap;
   confirm the Face menu (not Part menu). Pull and Move both caps by 1 mm with
   Keep ON/OFF; inspect the radius and the unaffected opposite rim.
2. Cancel, confirm, Undo/Redo. Edit its Fillet and Delete it; Undo. Save As to a
   separate temporary file and reopen; confirm the retained Fillet is editable.
3. Pick Cone 3's ordinary wall, top transition, bottom round and both caps.
   Confirm specific unsupported-operation messages and unchanged geometry.
4. Recheck automatic Edge/Face picking, delayed Part promotion, Alt/Ctrl,
   Edge-preselected Fillet/Chamfer, Face-only silhouettes, Part design edges,
   cyan hover/light-blue selection and orange radius direction.


Recovery validation (incremental Release/Ninja builds, at most `-j4`; exact
CTest names, at most `-j2`; no full suite or unrelated cylindrical Move tests):

| Test | Result | Seconds |
| --- | --- | ---: |
| direct-modeling-recovery-document | PASS | 23.79 |
| direct-modeling-recovery | PASS | 23.58 |
| circular-fillet-geometry | PASS | 2.18 |
| edge-blend-sessions | PASS | 13.27 |
| cone-pull | PASS | 13.30 |
| edge-blend-feature-sessions | PASS | 13.84 |
| edge-blend-features | PASS | 1.92 |
| selection-context | PASS | 32.69 |
| selection-contours | PASS | 0.41 |
| automatic-selection | PASS | 5.07 |
| automatic-selection-hidpi | PASS | 5.24 |

All eleven pass across focused runs. The final five changed/failed cases passed
in 39.25 seconds. The older cone Pull helper assumed a repeated click after
Cancel retained Face selection; each independent case now clears selection
before picking, preserving the accepted second-click promotion. Older circular
Keep-refusal assertions now distinguish certified cap translations from the
still-refused transverse/ambiguous cases and check valid geometric results.
No geometry assertion or tolerance check was weakened. Application and directly
affected test targets build successfully; `git diff --check` passes. The
original document's SHA-256 remains unchanged. Manual recovery acceptance has
not been performed by the agent.

## v0.5.2 conical Fillets and Chamfers

Priority decision (2026-11-12): v0.5.2 delivers conical edge blends; the
Workplane, Axis, Point, Sketch and reference foundation moves to v0.5.3.

### Supported conical transitions

A *conical solid* is a frustum: one body cone (`GeomAbs_Cone`) between two
planar caps perpendicular to its axis. Each cap/body circular edge may carry at
most one transition:

| Transition | Blend face | Creation | Recipe-free recognition |
| --- | --- | --- | --- |
| Fillet, constant radius | coaxial torus | yes | yes |
| Chamfer, equal distance | coaxial cone band | yes | yes |

`EdgeBlendFeatures` recipes now hold an optional `gp_Cone` support next to the
existing plane/cylinder supports (one curved support per transition). Creation
on a plane/cone circular edge requires a cap perpendicular to a coaxial cone.
`FilletGeometry`/`ChamferGeometry` are unchanged kernel paths; no new engine.

### Certification criteria (analytic, no topology indices)

All tests use `1e-6` mm (lengths) and `1e-8` (directions/angles), in the
meridian frame (axial `t`, radius `ρ`) of the body cone.

**Fillet between plane and cone.** The blend face is a torus whose axis
coincides with the body cone axis; its minor radius `r` is the Fillet radius;
its tube-centre circle lies at distance `r` from the cap plane *and* from the
body cone generator; the face is bounded by exactly two complete circles
(sweep 2π) — one shared with the cap at the foot of the normal onto the plane,
one shared with the body at the foot of the normal onto the generator (G1
tangency on both sides); the extended cap plane and body cone intersect in one
circle of positive radius (the sharp edge).

**Chamfer between plane and cone.** The band is a cone coaxial with the body
with a different half-angle (slope in the meridian frame); it is bounded by two
complete circles, one shared with the cap and one with the body, each lying on
both of its carriers; the extended cap plane and body cone meet in a unique
sharp circle of radius `ρs > 0`; the distances from that sharp circle along both
supports are equal within `1e-6`.

**Distance convention.** For an equal-distance Chamfer at a non-right angle,
the Chamfer distance is measured *along each supporting face* from the
reconstructed sharp edge: radially in the cap plane (`|ρs − ρcap|`) and along the
body cone generator (`hypot(Δt, Δρ)`). This matches OCCT's
`BRepFilletAPI_MakeChamfer::Add(d, d, edge, face)` on plane/cone edges and is
verified analytically in `conical-edge-blends` (Cone 3: R20 sharp circle, cap
boundary R12, body boundary 8 mm down the generator).

**Interpretation and ambiguity rule.** Recognition orders the faces as a chain
`cap – [blend] – body – [blend] – cap` and tries every cone as body. An
interpretation survives only when each blend certifies as above *and* the
replayed sharp frustum plus recipes is material-equivalent
(`BRepReconstruction::equivalent`) to the current B-Rep. Exactly one surviving
interpretation is accepted; zero or several means ordinary geometry with a
diagnostic (`No certified conical transition: …` / `Ambiguous conical
geometry …`). A stepped cone with unequal distances, or a symmetric double cone
where either cone reads as the other's Chamfer, is therefore refused; the body
cone itself is never a Chamfer.

### Sharp reconstruction

The sharp source of a recognized Part is the frustum built from the two cap
planes and the body cone carrier (`BRepPrimAPI_MakeCone`, radii taken from the
carrier at the cap positions). Strict sharp validation now admits conical faces
alongside planes and cylinders and keeps the `1e-6` tolerance ceiling. Replay
binds the unique toroidal Fillet face or conical Chamfer band; other faces are
never transitions.

### Editing behaviour

- **Edit Fillet / Edit Chamfer** from the context menu, Pull on the transition
  face, or Move Face on it: absolute numeric value, orange Pull arrow, blue
  preview, OK/Enter (one Undo step), Cancel/Escape (none). For a Fillet the
  arrow sits outside the material and points toward the torus (inward increases
  the radius, outward decreases it). For a conical Chamfer band the arrow is
  anchored on the band at its UV midpoint along the outward normal (the centre
  of mass of a full band lies on the axis); otherwise the existing Chamfer
  manipulator behaviour is unchanged.
- **Delete Face** on a recognized transition restores the sharp cap/cone edge by
  replay and keeps the other transition (identity and dimension).
- **Save/Open:** recipe version 3 (see `NATIVE_DOCUMENTS.md`); revalidated by
  replay on Open. A recipe-free Cone 3 is recognized again from geometry.
- **Duplicate:** follows the existing v0.5.1 convention — the Part receives a new
  Part UUID and an independent copy of the recipe (feature IDs are Part-scoped
  and copied unchanged); the copy is revalidated against its own geometry and
  edits do not touch the source.
- Transformed Parts work in Part coordinates; recognition also certifies tilted
  axes (tested with a rotated + translated replica).

### Keep ON/OFF boundary

Move Face and Pull of a planar cap translate the cap along the cone axis on the
sharp frustum (the cone carrier is unchanged), normalize, then replay.
**Keep ON** preserves the Fillet radius and Chamfer distance on both caps.
**Keep OFF** removes only the transition adjacent to the moved cap and keeps the
other one. Both directions (outward/inward) are tested against independent
kernel constructions.

### Known unsupported cases (safe, atomic refusals)

- **Rotate Face** of a cap with a circular (cylindrical/conical) transition:
  rotation breaks coaxiality, which is the certification basis — refused with
  “rotation breaks coaxiality”, Keep ON and OFF.
- Pull/Resize, Move Face and Delete Face of the **body cone** with recognized
  transitions: refused with a diagnostic; geometry and history unchanged.
- Pointed cones (one cap), more than one blend face per cap, interacting
  transitions, cylinder bodies without a recipe (v0.5.1 behaviour retained),
  conical *holes* with blends, unequal-distance Chamfers, and Chamfers on
  plane/cylinder rims (still created by the uncertified kernel path without an
  editable recipe).

### Automated verification (v0.5.2)

- `conical-edge-blends` (`tests/ConicalEdgeBlendTest.cpp`): synthetic Cone 3
  replica; ordinary cone refusal; Fillet/Chamfer creation; 8 mm / 11 mm
  recovery; independent edits; Delete Fillet/Chamfer; cap Move ±5 mm for both
  caps with Keep ON/OFF against kernel oracles; unrelated-transition
  preservation; recipe v3 persistence and tampering; source immutability;
  tilted and duplicated geometry; stepped, ambiguous and unequal-distance
  refusal; Rotate refusal.
- `direct-modeling-recovery` (session level, synthetic Cone 3): body-cone
  refusals, context-menu Edit Fillet/Chamfer with exact input, Fillet arrow
  direction, Cancel/OK/Undo/Redo, Pull and Move of both caps with Keep ON/OFF,
  Delete Fillet/Chamfer, Rotate refusal, Save/Open (recipe v3) and Duplicate.

### Manual acceptance (Fedora KDE/Wayland, 100% and 200% scaling)

1. Open a **copy** of `fillet.odc`; select `Cone 3`. Right-click the top band:
   *Edit Chamfer* shows `8`; right-click the bottom round: *Edit Fillet* shows `11`.
2. Edit Chamfer to 6 and Fillet to 9 with typed values; inspect the blue preview,
   drag the orange arrow (Fillet: inward grows), Escape cancels, Enter commits;
   check one-step Undo/Redo for each.
3. Delete Face on the band, then on the round: sharp edges return, the other
   transition stays; Undo/Redo.
4. Pull and Move Face on each cap by +3 and −3 mm with Keep ON (dimensions kept)
   and Keep OFF (only the adjacent transition removed).
5. Rotate Face on a cap: a clear refusal, no geometry change.
6. Pull/Move/Delete the body cone: a clear refusal, no geometry change.
7. Save As a new file, reopen, repeat step 1 with the edited values; Duplicate
   the Part and edit the copy independently.
8. Repeat steps 1–2 with `QT_SCALE_FACTOR=2`; arrow and panel remain usable.
