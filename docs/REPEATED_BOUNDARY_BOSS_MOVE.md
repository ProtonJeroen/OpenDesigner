# Phase 9D.7B — repeated boundary boss movement

Move Face can select a current trimmed cylindrical boss or a complete cylinder
with an overhanging base and move it again across one planar exterior side.
Supported moves include farther outward, along the side, and back inward to a
fully supported boss. Axis, diameter and height remain unchanged. Trim clips
against the current support; Extend keeps the complete cylinder with a positive
connected attachment. Switching policies on a later move is supported. A zero
move used for activation preserves the exact current material under either mode.

## Current geometry and certificates

The fallback follows the existing complete-boss paths in `moveBoss`. It reuses
`BRepReconstruction::neighborhood`, `normalize`, `validSolid`, `equivalent` and
`finishLocal`, plus the existing manifold validator and Boolean utilities.

Grow the connected cylindrical patch, including split mantles. Require outward
material normals, planar neighbors and two distinct axial end planes. Intersect
the actual solid with the finite cylinder, then remove that volume. Certify one
valid healed support solid, exact source reassembly, and matching cylindrical
wall area. Find the unique attachment plane by extruding outward-oriented healed
exterior faces at each rim. This uses actual trimmed planar faces, retaining
holes and nonrectangular boundaries; it is not a stock bounding-box proxy.

The old boss must equal either the complete cylinder or its exact intersection
with this support prism. The destination footprint must have a valid connected
volume overlap. Both footprints may meet only one shared planar side boundary;
curved hole walls or additional side planes cause refusal. Fuse the full or
trimmed translated cylinder according to the explicitly chosen policy.

`finishLocal` independently protects material outside the old/new cylinder region
and every unaffected trimmed face. Only the selected neighborhood and actual
base-plane regions intersected by the destination cylinder may change. Validate
one manifold solid, exact added feature material and the unchanged healed
support with independent differences. No OCCT offset path, primitive metadata,
feature identifiers, creation flags or remembered Trim/Extend state is used.
The existing session supplies current face owners, blue previews, numeric input,
Cancel, cached Undo/Redo, and Part/browser/presentation identity.

## Focused validation

- `--repeated-boss`: full → Trim/Extend crossing → farther outward → inward →
  full restoration → reopen, with rotated placement, another boss and a hole.
  Check independent analytic volume, exact material oracle, source immutability,
  axis/radius/height, and detached/corner/hole/axial-motion refusals.
- `--repeated-boss-detail`: imported current partial/full geometry, split mantles,
  reversed cylinder axes, nonrectangular support, both policy transitions,
  zero-move identity and independent sampled occupancy.
- `--cylindrical-boundary`: existing boundary session expanded with repeated boss
  drag/Escape, localized numeric confirmations, outward/inward/full restoration,
  cached Undo/Redo and fresh selection owners. Existing hole/planar edit and Part
  identity checks remain. An optional native file is read without writing it;
  replay recognizes the actual current cylinder and exterior planes, previews it
  directly, then tests both policies against an independent box/cylinder oracle.
  A single-Part native file gets an unrelated Part only in the in-memory fixture.

Verification on November 4: incremental application and both affected test
targets built successfully. Both new geometry modes passed, as did the directly
affected `--local`, `--boundary` and `--independent` geometry modes. The expanded
portable boundary session passed. Native boundary replays of `moveCylinder.odc`
and `moveCylinderBug.odc` passed both policies, including the actual loaded boss
preview and source-file immutability. The replay harness now reads the current
support dimensions (the first file currently has one Part and a 196 mm wide
support) and samples visible planar points to avoid an occluded center pick.
No unchanged passing suite was rerun after the final implementation; later
changes were additional test coverage, harness fixes and documentation.

The original native files remain private and untracked. No full CTest suite,
push, merge or PR is part of this phase.

## Remaining limits

The new fallback requires one finite constant-radius boss, planar axial rims,
one unambiguous planar attachment and at most one shared planar side across its
source/destination footprints. It refuses corners/multiple side supports, curved
or sloped support boundaries, blends, depth-varying trims, interacting features,
disconnected/degenerate support overlap, and geometry outside existing numerical
or topology budgets. Existing earlier paths retain their established scope.
There is no reconstruction of vanished boundaries from history. Moving the
selected boss axially or changing its radius/height is a separate operation;
radial Pull limitations are unchanged. Manual desktop acceptance and broader
phase 9E Save/Open lifecycle checks remain pending.
