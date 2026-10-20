# B-Rep reconstruction — v0.3.6 Advanced Face Topology

OpenDesigner follows the explicit select → command → preview → OK/Cancel workflow
of the locally documented SolidDesigner 2008 v16 reference. This is an independent
analytic implementation, not a complete implementation of that product. See the
[workflow analysis](SOLIDDESIGNER16_WORKFLOWS.md).

## Shared architecture

`BRepReconstruction` remains geometry-only. Move Face and Rotate Face pass a
canonical solid, member target face and requested plane in the B-Rep frame.
Align Face uses these same operations. No primitive parameters, feature creation
history, viewport selection or document metadata enters reconstruction.

The existing offset (`BRepOffset_MakeOffset`) and rotation
(`BRepOffsetAPI_DraftAngle`) paths remain the first construction attempts.
Initial preparation deep-copies topology and geometry and unifies redundant
Boolean seams, protecting the selected patch boundary. `Prepared::history`
composes copy and OCCT same-domain history to identify the selected face without
silently absorbing another selected-plane patch.

Every successful nonzero result now passes **final** normalization as well:

1. Copy the candidate and remove INTERNAL subshapes with
   `BRepTools::RemoveInternals` with connectivity checks enabled, never forced.
2. Use `ShapeUpgrade_UnifySameDomain` to merge safe coincident adjacent surfaces
   and collinear edge segments. Selected edges are no longer protected at this
   stage: the requested plane has already been constructed.
3. Require one valid closed solid before and after cleanup. Verify material
   equivalence through OCCT Common: both original volumes and their common volume
   must agree within the relative volume tolerance (1e-8, scaled by at least 1).
   Equal volume alone would not establish equivalence. No open shell is repaired
   into acceptance, and no neighboring surface is approximated or replaced.

This corrects the previous asymmetry: v0.3.5 normalized the input, while the
intersection builders could introduce new trims afterward without final merging.
The standard staircase already produced eight faces for Move -60 in v0.3.5;
that specific run did not reproduce the user's extra-face observation. A separate
regression deliberately splits a coplanar side with `BRepFeat_SplitShape` and
proves that final cleanup removes the seam without changing material.

## Controlled planar topology reconstruction

When the existing builder cannot construct/accept a result, a bounded planar
material-cell path may reconstruct it. This uses OCCT surface splitting and solid
Booleans, not a second intersection or wire kernel:

1. Require all supports planar, one target-plane patch, at most 12 distinct
   support planes and at most 32 cells after splitting. Input remains bounded to
   64 faces, one valid closed solid/shell. Curved neighbors retain the older
   conservative construction path.
2. `BOPAlgo_Splitter` splits an isolated solid with its support planes. Its Objects
   are material solids; Tools are plane faces, which are excluded from the result.
   Source cell volumes must cover the source volume.
3. Remove artificial target-plane partitions inside existing material by merging
   across that plane when the union is convex. A centroid inside the cell and all
   vertices inside every actual bounding halfspace certify the convex cells.
   Ambiguous/nonconvex internal target partitions are refused. Merge volumes and
   subsequent normalization are checked for material preservation.
4. Record actual cell facets, then **retain physical target-neighbor trims** for
   cells bounded by the selected plane, even if those trims were redundant before
   editing. Derive them from edge/face adjacency of the selected patch, certify
   that its vertices lie on one side of each neighbor plane and that adding them
   removes no source-cell material. A convex single-wire target is required by
   this fallback; ambiguous/concave boundary decomposition is refused. Substitute
   the requested plane only where it bounds material. Do not add all global
   source planes: unrelated bounds must not clip legitimate overhangs.
5. Build those intersections with `BRepPrimAPI_MakeHalfSpace` and
   `BRepAlgoAPI_Common`, then unite them with `BRepAlgoAPI_Fuse`. Empty material
   cells may disappear. A finite auxiliary box bounds computation; any box face
   surviving as a new support causes refusal. It is never retained as model
   geometry. The new path requires the target normal to remain on the original
   side (dot product > 1e-4, approximately a rotation below 90 degrees).
6. Normalize and validate the resulting solid. Unselected supports may disappear,
   split into patches, merge, or expose a necessary reverse-facing patch on the
   same plane, but only in this cell-construction path. For example, an extended
   overhang can expose an underside on the former terrace's plane. New arbitrary
   transition planes and sweep/revolve volumes are never accepted.
7. Check material against the union of the cells' signed halfspace expressions:
   a 9×9×9 grid around the result and probes on either side of face centroids
   must agree with OCCT solid classification (samples near a support are skipped).
   This is additional semantic validation, not a claim that sampling alone proves
   arbitrary B-Rep correctness. Tests separately check exact analytical volumes.

The legacy construction path still requires all original analytic supports.
Only the internally certified planar-cell path can relax that rule; an arbitrary
valid solid cannot bypass it through the public `finish` API.

## Mapping, validation and transactions

`Result::history` describes final **support correspondence**: empty modified lists
record vanished supports and multiple faces record patches on a support. It is
not persistent subshape naming or exact trim lineage; coplanar source faces can
share descendants. The target maps by the requested oriented plane. `Result::target`
is populated only for a unique final target patch. Align refuses an ambiguous
multi-patch target rather than moving an arbitrary fragment. Complete disappearance
of the requested target plane is refused even if the remaining material is valid.

Every accepted shape must pass BRepCheck, closed-shell/single-solid checks, no
loose topology, positive finite volume and self-interference analysis on a copy.
The planar-cell path rejects degenerate planar edges and negligible faces. Move
also requires volume change in the requested outward/inward direction. Target
position/orientation and result support correspondence are checked independently
of face/edge/vertex counts. Preparation and final normalization both check material
through Common, not just matching counts or centroids.

Existing CommandSession snapshots, blue translucent full-solid previews and orange
manipulators are unchanged. All calculations start from the original source.
Confirmation replaces geometry in the same Part slot and creates one cached
Undo/Redo step. Old owners are discarded on commit; Cancel restores the source
owners. Rotation axes refer to the immutable source snapshot during the session,
not edges that may disappear in the candidate. Name, color, visibility, instance
transform, presentation and Structure Browser identity remain intact. Empty-space
Undo/Redo menus and passive Face/Alt-Part selection retain their behavior.

## Verified cases and independent geometry

The staircase is Unite of Box `(0,0,0)..(100,80,50)` and Box
`(60,0,0)..(160,80,90)`. Its riser is x=60, y=0..80, z=50..90, outward -X;
initial volume is 960000 mm³. Below z=50, material spans x=0..160. Above it,
material spans x=b(z)..160 whenever b(z)<160. The upper region may overhang x=0;
clipping it to the old bounding box would change existing Rotate Face behavior.

| Test | Independently checked result |
| --- | --- |
| Rotate about +Y through (60,0,50), +75° | Reaches the requested plane beyond the former failure; z=90 cap disappears; 7 faces and fewer edges/vertices |
| Same axis, -75° | Preserves the legitimate overhang and 8 necessary faces |
| 60°, -60°, 68.1°, 68.3° | Material probes and analytical volumes across the cap-disappearance threshold near 68.2° |
| Move -60 mm | Riser reaches x=120, beyond the original Boolean overlap seam; 8 faces, no redundant coplanar edges |
| Move +60 mm | Low terrace disappears; target merges into x=0 exterior; 6-face solid |
| Move +100 mm | Riser reaches x=-40; necessary underside appears on z=50; 8 faces |
| Align after +75° reconstruction | Mapped target moves onto an offset reference plane with correct material |
| Side notch in a 160×80×90 Box, x=0..20, z=20..40 through full Y | Move -5 retains two necessary disconnected x=0 patches; Move +20 closes the notch and merges to the original 6-face stock |
| Deliberately split coplanar Unite side | Final cleanup restores 8 faces with Common-confirmed material equivalence |

For a bottom-edge rotation, b(z)=60+tan(angle)×(z-50). Expected volume is the
unchanged lower block plus 80 times the integral of max(160-b(z),0) over z=50..90.
Tests integrate that piecewise-linear width exactly and independently classify
points inside, outside and just beside the requested plane. They also verify
that removed faces/edges/vertices are absent, necessary coplanar patches remain,
final mappings contain only result faces and the original serialized B-Rep is
unchanged. The former -120 mm riser move remains refused because the selected
support disappears completely.

`move-face` exercises both ordinary and larger topology-changing distances/angles
through rendered previews, replacement, invalid-input recovery, Cancel, OK and
one cached Undo/Redo step for all three adapters. It checks source/properties,
browser row and total AIS object counts after cleanup. The existing Box, curved
cap, Subtract, Boolean, Pull and selection suites remain enabled.

## Diagnostic model capture

**File → Export Debug Model...** now captures exact B-Reps, meshes, part metadata,
selection, available command parameters and geometric diagnostics in a ZIP.
The independent `OpenDesignerDebugModel` tool reloads it into isolated snapshots,
compares stored/fresh triangulations and classifies suspect material points.
See [debug model export and investigation](DEBUG_MODEL.md). This is diagnostic
capture only, not v0.3.7 `.odc` Save/Open. Its payloads enabled the exact regression
and geometric correction described below.

## Fedora KDE Rotate +30° → Move +49 mm correction (2026-10-20)

The user supplied `preview-debug.zip` and `result-debug.zip` and confirmed that
Rotate +30° is correct; the triangle first appears on Move +49 mm of the oblique
face. Exact payloads are retained under
[`tests/fixtures/rotate30-move49`](../tests/fixtures/rotate30-move49/README.md).
The regression replays the rotation, verifies it against the supplied rotated
B-Rep, then edits that exact B-Rep rather than a synthetic approximate model.

**Diagnosis: actual extra solid material**, not a harmless dividing edge or an
AIS triangulation artifact. The recorded result is BRepCheck-valid, closed and
free of detected self-interference, but classifies (175,-20,50) as IN. Fresh
triangulation preserves that material. Recorded volume is 1156871.7334342287 mm³;
the correctly bounded result is 1091890.5249039864 mm³. The unwanted wing adds
64981.2085302423 mm³ and extends to y=-47.44486372867095, outside the real y=0
front support. Preview and confirmed output contain the same faulty material.

**Cause:** support-arrangement splitting at the low terrace's z=43 plane creates
an upper high-section cell whose old oblique top already implies y>=0. Because
the front plane is then not an actual facet of that individual cell, the earlier
cell rules omitted it. Moving the oblique plane outward invalidates that implicit
bound: the cell extends through the real front and exposes a spurious underside
on z=43. Final unification/healing cannot remove such valid-but-wrong material,
and a validator derived from the same incomplete rules accepted it.

**Correction:** carry the selected face's physical neighboring trims into every
cell that uses its plane. They remain constraints even when redundant before the
edit. The source convex-cell vertices must satisfy these constraints, so this
adds no clipping to the original material; it prevents an artificial partition
from redefining the outside boundary after movement. Constraints are local to the
selected face, not arbitrary bounding-box clamps. OCCT still performs intersections,
trimming, union and same-domain cleanup. No fixture coordinates or primitive
history enter production code, and no triangular-face deletion heuristic is used.

The exact regression verifies the new 9-face/21-edge/14-vertex closed solid through
an independently built bounded prism/union oracle, analytical volume, Common
material equivalence, dense samples around the former wing and both sides of
the real front/low terrace, exact +49 mm plane, no coplanar seams, final target
mapping, Align after editing, preserved input data and a rigidly transformed
copy. It compares against the supplied faulty result as a negative control.
The viewport regression covers visible full-solid preview, replacement, Cancel,
Enter, one Undo step, Redo, AIS cleanup and stable Part metadata/browser references.
The previous 18 high-section investigation cases remain enabled as additional
coverage. Existing riser ±75°/overhang tests ensure unrelated global bounds do
not start clipping legitimate extensions.

The corrected exact case passes automated checks. Manual Fedora KDE retesting
of this correction is still required; general freeform topology support is not
claimed.

## Remaining limits and future solids

This is not unrestricted freeform topology editing. Curved targets, blend/spline/
torus supports, cavities/multiple shells, disconnected results, lost targets,
ambiguous Align targets, concave/holed or conflicting target-neighbor trim
decompositions in the planar fallback, excessive cell complexity and failed or degenerate
intersections remain refused. No general curved-neighbor support-disappearance
algorithm is implemented. Near-tangent or unbounded cell results can be refused;
there is no general guarantee for every angle, direction or Boolean arrangement.
Calculation remains synchronous. Persistent edge/face IDs are future work.

Solids from future Extrude, Revolve and Sweep commands can use the same canonical
B-Rep contract independently of feature history when their surfaces meet these
limits. Profile/path regeneration is not a substitute for direct reconstruction.

```sh
cmake -S . -B build -DOPENDESIGNER_BUILD_TESTS=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
git diff --check
```

Viewport tests need X11/OpenGL; rendered pixel checks use OCCT offscreen images.
The user approved v0.3.5 on Fedora KDE. Manual v0.3.6 testing found the
protrusion defect diagnosed and corrected above using the exact debug payloads.
Release approval and a manual retest of that correction remain outstanding.

OCCT references: [Splitter](https://occt3d.com/dev/doc/refman/html/class_b_o_p_algo___splitter.html),
[HalfSpace](https://occt3d.com/dev/doc/refman/html/class_b_rep_prim_a_p_i___make_half_space.html),
[UnifySameDomain](https://occt3d.com/dev/doc/refman/html/class_shape_upgrade___unify_same_domain.html).
