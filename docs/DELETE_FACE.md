# Delete Face

Select one or more Faces with click / Ctrl+click, then choose **Delete Face**
from the viewport context menu or Modeling. A blue translucent preview shows
the reconstructed solid. **OK/Enter** confirms; **Cancel/Esc**, closing the
panel, selecting other geometry or changing tools discards it. There is no
distance parameter. Normal camera navigation does not confirm the operation.

Faces can belong to multiple visible Parts. All selected regions must succeed:
partial feature removal and partially applying a multi-Part command are refused.
Confirmation creates one existing history entry, grouped when needed. Parts
retain their slots, presentations, browser rows, names, colors, visibility and
instance transforms. Edited primitives become GeneralBRep; Undo restores their
exact original geometry and primitive metadata. Redo uses cached geometry.
Undo/Redo clears all Face/Edge/Part and browser selection, including when invoked
with the Delete Face panel focused. Ordinary Cancel restores original owners.

## Geometry and acceptance

`DeleteFaceGeometry` is geometry-only and operates on deep copies in the canonical
B-Rep frame. Boundary cylindrical features use the certified local reconstruction
described below. Explicit selections use OCCT `BRepAlgoAPI_Defeaturing` to extend
and trim neighboring surfaces. It explicitly checks face membership (OCCT would ignore foreign faces),
deduplicates selection and requires history to report every selected face deleted.
An OCCT warning/error rejects its entire native candidate, even if some regions
succeeded. A separately certified planar step reconstruction may still succeed;
no partial native result is ever installed.

A closed, valid solid alone does not establish the intended result. Acceptance
requires one of the following bounded material certificates:

1. **Convex planar source:** all source vertices must lie inside every source
   oriented support halfspace. Removing the selected supports defines the unique
   retained halfspace intersection. A finite seed bounds computation only; any
   remaining seed support rejects an unbounded intersection. The defeaturing
   result must be Common-equivalent to this oracle. This supports simple convex
   chamfer removal, including rigidly transformed geometry.
2. **Planar-rim features:** selected faces are partitioned by shared-edge
   connectivity. Every external boundary must form a closed planar wire on its
   actual adjacent surviving plane. These original selected faces and rim faces
   must bound a valid feature solid. Overlapping/nested rim caps, overlapping
   selected feature volumes and features partly inside and partly outside the
   original material are refused. A feature entirely inside source material is
   subtracted (boss); a feature entirely outside is united (hole/pocket). Common
   checks the candidate against that exact material oracle.
3. **Planar sections:** when native defeaturing/certification fails, a
   transition plane can carry opposite-facing patches on the same Part. The
   selected step, or the unique retained step axis for a side-facet-only deletion,
   defines the sections. Prismatic supports are parallel or perpendicular to this axis.
   If a previous edit removed the opposite-facing transition, the current B-Rep
   may instead establish a unique axis from parallel/perpendicular supports and
   three to eight distinct axial stations. Multiple possible axes are refused.
   No deleted topology, previous command or stored feature history is required.
   If no prismatic axis exists, a unique family of three to eight parallel station
   planes may instead define sections with oblique top/front supports. More than
   one such axis is refused. This covers the L-shaped transition between a rotated
   low section and a higher section: several actual neighboring supports jointly
   trim the selected patch, rather than requiring it outside each trim separately.
   OCCT splits the immutable source at the axial stations into convex sections;
   both volume coverage and Common material equivalence must match the source.
   A deleted step patch lets its actual neighboring opposite-section support
   trim its owning section. Each imported support must exclude part of the selected
   patch; their combined intersection must remove the entire patch. Other selected
   faces may release only whole section facets. Fixed axial partitions prevent
   material from spreading into unrelated
   sections. Halfspace intersections must be bounded entirely by original
   supports; every selected patch must disappear and every unselected patch must
   retain positive area on its original support. This explicitly permits adjacent
   faces to be shortened, where demanding all their original area would reject
   valid Delete Face operations. Every accepted result is built from these rules;
   an arbitrary closed kernel result cannot bypass them.

The rim faces are verification geometry on existing supports. They are never
installed as arbitrary caps, and cannot rescue a failed defeaturing operation.
The source is not reconstructed from primitive parameters or a stored feature
history. The v0.3.6 target-plane substitution fallback is not used for deletion.

For the first two paths every unselected original face patch must remain covered
by result faces on the same oriented analytic support, checked through surface
Common area. The certified planar-section path permits trimming those patches
while requiring that each still survives. Every result face must map to a
surviving original support. Merely preserving a support or
face count is insufficient. Deleted patches get empty history; an unrelated
coplanar patch may legitimately survive.

Candidates use shared final normalization (connectivity-checked internal cleanup
and same-domain unification), with Common-confirmed material equivalence before
and after cleanup. The usual BRepCheck, single closed solid/shell, positive finite
volume, no loose topology and self-interference checks apply. The complete solid
must tessellate before the source presentation is hidden. Multiple Part previews
use the same blue shaded rendering helper as Move/Rotate/Align Face.

## Boundary cylindrical features (phase 9D.7C)

Select one cylindrical wall of a through-notch on one planar exterior side, a
Trim boss, or a complete Extend boss with a partial support footprint. Delete
Face resolves the entire connected cylindrical feature, including split mantle
patches and the boss cap/underside; separate cap selection is unnecessary for
these boundary classes. Nearby holes, bosses and planar faces remain protected.
The blue preview, Cancel, cached Undo/Redo and stable Part identity use the
existing session. Confirmation removes deleted face selection and selects the
edited Part; Undo/Redo clear selection as before.

Previously, native defeaturing and its material oracle required the explicitly
selected region to have closed planar rim loops while preserving every
unselected patch. An open notch has partial circular rims and opening edges
across different planes. A trimmed/overhanging boss also has side or underside
intersections, and selecting its wall alone leaves its cap unselected. These
cases could not meet that certificate.

`CylindricalMoveGeometry::removeBoundaryFeature` reuses current-B-Rep recognition
and material reconstruction from phases 9D.7A/B. For a notch, infer the finite
missing cylinder segment on the material side of the actual exterior plane.
Certify its added material, independent re-cut to the source, axial/side exterior
closure and mantle area. For a boss, remove the current finite cylinder material,
certify the healed support and reassembled source, then verify the old boss is
either the complete cylinder or its exact trim by the healed planar support
prism. This is actual trimmed support geometry, not a stock bounding box.

Return the certified fill/healed body rather than rebuilding a translated
feature. Shared `finishLocal` preserves material outside this minimal removed or
filled volume and unaffected trimmed patches. Require a manifold solid, complete
selected-wall disappearance and mapping of every final face onto an existing
oriented source support. Only the resolved feature and its immediate neighbors
may change. Splitting/merging is allowed. No offset path, creation flags,
primitive dimensions or feature history are used. The existing OCCT defeaturing
and planar-section adapters remain unchanged for their previous selections.

The new automatic resolution takes one cylindrical face per Part. Selecting one
piece of a split mantle resolves the connected patch. General multi-face or
multi-feature selections continue through the legacy adapters and are not
expanded automatically. Interior full bosses still require their cap in the
explicit selection. Corners/multiple exterior sides, blind/depth-varying notches,
sloped/curved rims, blends, interacting or incomplete boss trims, ambiguous
support and geometry outside existing numerical/topology budgets are refused.
No extinct side boundary is inferred from history. Each edited Part must remain
one closed manifold solid; all Parts must succeed before any preview is shown.

Focused commands (native input remains read-only):

```sh
build/OpenDesignerDeleteFaceTests --boundary-geometry
build/OpenDesignerDeleteFaceTests --boundary-session
build/OpenDesignerDeleteFaceTests --boundary-session /absolute/path/document.odc
```

The geometry regression covers imported current geometry, split mantles with
rotated axes, independent analytic volume and occupancy, unrelated features and
atomic corner/blind/interacting/foreign-face refusals. The session regression
checks context activation, blue non-selectable preview, Cancel/OK, cached
Undo/Redo, Part UUID/placement/appearance/browser identity, selection cleanup,
follow-up Move/Delete on remaining features and multi-Part atomic refusal.
Native replay uses the current support/cylinder geometry and independent stock
oracles, with all feature changes confined to the in-memory snapshot. Manual
9D.7B desktop acceptance was reported as 8/8 passed; broader 9E persistence and
manual 9D.7C acceptance remain separate.

Validation: the new boundary geometry and portable session regressions passed,
as did the existing Delete Face geometry regression. The directly affected
Move Face `--notch` and `--repeated-boss-detail` regressions passed after sharing
the removal certificates. Incremental application and affected test builds
passed. Native session replays of `moveCylinderBug.odc` and `crash03112026.odc`
passed all three removal classes and source-file immutability. The current
`moveCylinder.odc` (R21/R23) does not satisfy the native test harness's bounded
axis-aligned box oracle; it returns a fixture diagnostic before Delete Face,
and no passing replay is claimed for that file. Rotated/split geometry is covered
by the portable independent-oracle regression. All three ODC hashes are unchanged
from the start of this phase and remain untracked. No full CTest suite was run.

## Supported scope and failures

The input is one valid solid with one shell per edited Part, at most 64 Faces,
with plane, cylinder and cone supports. The convex certificate permits at most
12 retained planar face constraints; section reconstruction permits at most
12 distinct planes and eight axial stations, with exactly one convex section per
interval. Feature rims are limited to eight wires per
connected selection. Typical cases are cylindrical/conical through holes, complete
blind holes/pockets, complete bosses and convex planar chamfers. Incomplete
features, lost unselected patches, open/unbounded closures, unsupported surfaces,
internal cavities/multiple shells and uncertified or ambiguous intersections are
refused. Prismatic transitions between touching sections must first belong to
the same united Part: choose both Parts, **Unite → Preview → OK**, then reselect
the resulting patches. Separate Parts cannot borrow each other's surfaces.
This is conservative analytic feature removal, not unrestricted freeform
or fillet reconstruction. Kernel computation remains synchronous.

An explicit failure produces a shared **Geometry operation failed** popup with
the command name, reason and assurance that source geometry was retained. The
same route handles Move/Rotate/Align Face, Boolean and Pull geometry failures.
Live numeric previews use the status bar; explicit confirmation opens the popup,
so typing does not open a dialog on every keystroke. Invalid numeric syntax stays
an input-validation message. Boolean multi-solid results retain their existing
semantics. Failed Pull geometry retains the most recent validated preview; Cancel restores
the original session snapshot.

Debug export records all deleted face indices against their original Part
snapshots and includes each Part's source and preview B-Rep. Confirmed grouped
operations include each original Part snapshot. Indices are diagnostic snapshot
indices, not persistent subshape names.

## Verification

`delete-face-geometry` covers through/blind holes, cylindrical and planar bosses,
conical holes, multiple features, duplicate/reversed selection order, a convex
chamfer, rigid transforms and safe refusal of incomplete, foreign, open,
unsupported and unbounded cases. It checks source serialization, exact stock
material equivalence, independent occupancy probes and deleted-face mappings.

`delete-face-session` covers actual context activation, blue rendered coverage,
tessellation, non-selectable previews, Cancel/Esc/window-close, OK/Enter, cached
Undo/Redo, selection cleanup, properties and stable presentation/browser identity,
multi-Part atomicity, debug roundtrips and common error popups for every adapter.
The session suite needs X11/OpenGL. Run all existing suites as well:

The [exact Unite/Rotate transition fixture](../tests/fixtures/delete-unite-rotate/README.md)
also checks deletion of the small transition triangle and front strip. Removing
the triangle trims the low top/front against the inclined support; removing the
strip extends that support down to the original bottom. Independent profile
prisms, analytical volume, material probes, transformed coordinates and actual
Unite/Delete preview/Cancel/Undo/Redo guard this topology change.

Sequential deletion is also covered in both orders, including the exact B-Rep
exported after the first face removal. After each confirmation the next face is
resolved against the new topology. Two Undo steps restore the intermediate and
original solids; two Redo steps restore their cached results with empty selection.

The exact debug5 L-shaped step also covers oblique section supports, their later
crossings with the retained high top/front, independent profile-prism material
equivalence, analytical volume and occupancy probes, rigidly transformed geometry,
selection orientation resolved against the current solid, safe refusal of a
missing bottom, competing section axes or loss of an unselected top, and
preview/Cancel/OK/cached history with a transformed Part instance.

```sh
cmake -S . -B build -DOPENDESIGNER_BUILD_TESTS=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
git diff --check
```

The user confirmed the supplied sequential, Unite seam and inclined L-step cases
on 2026-10-20. For broader desktop verification, exercise these examples with
Ctrl selection, transformed Parts, both confirmation/cancellation paths and
history from Edit, toolbar and keyboard. Check visible previews and popup focus
at the desktop's actual scaling. Offscreen rendered checks are not a native
screen proof.


## v0.4.9 integration status

Phase 9F.1 adds Delete Face for the current sloped cylindrical opening in
`hole2.odc`, including its elliptical rims, by consuming the shared certified
planar-rim fill before re-cutting. It preserves unrelated geometry, preview,
Cancel, cached history and Part identity across native Save/Open. See
[sloped-opening deletion](SLOPED_OPENING_DELETE_FACE.md) for the exact reproduction,
independent oracles and limits. The old safe refusal was a geometry capability gap,
not a persistence failure; it is now required to succeed in the history regression.

Nonplanar/uncertified interacting rims, unproved closures and outward sloped boss
trims remain outside this extension. See [Phase 9E](FACE_HISTORY_PERSISTENCE.md) and
the historical [Phase 9F review](V049_INTEGRATION_REVIEW.md) for earlier validation.
