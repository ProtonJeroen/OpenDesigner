# Phase 9D.5 — boss boundary follow-up

## Diagnosis before the reconstruction change

Native-reader replay of `moveCylinderBug.odc` at `2f0b522` confirms valid
single-shell results for both boss policies, current AIS face owners, preserved
Part identity and successful unchanged-hole/right-side-plane edits after OK.
The reported blanket loss of command availability is not reproduced by that
replay; the menu action is not disabled for a whole Part by boss recognition.

Expanded per-face replay exposes the previous regression's coverage gap:
planar reference/selection succeeds on every plane, but the stock top/bottom,
boss cap, and Extend underside cannot reconstruct a +2 mm edit. The primary
`BRepOffset_MakeOffset` route returns no completed shape (kernel status 0).
The exterior-slab fallback only handles side planes recovered from a complete
hole; the final planar-cell fallback requires every source support to be planar.
That last refusal overwrites the earlier useful diagnostic. Three unaffected
side planes still reconstruct. Thus the reproducible defect is missing local
planar reconstruction, not invalid accepted boss material or stale session state.

The existing tests checked only the right-hand stock plane, missing the axial
faces. No validity or unsupported-boss check should be relaxed to address this.

## Bounded correction

Reuse local neighborhood analysis and acceptance for a planar patch whose
adjacent surfaces are invariant along its normal: parallel-axis cylinders and
planes containing the movement direction. Sweep the actual trimmed face, not a
stock proxy, and add/remove exactly that material. Certify original occupancy,
translated target coverage, adjacent supports and protected outside material.
General tilted/curved intersections and contact changes remain separate cases.

## Implemented certificates

`BRepReconstruction::movePlanarLocal` uses the isolated neighborhood and the
normal-oriented actual face sweep. Addition must encounter no existing material;
removal must be wholly contained in existing material. The actual change must
equal that sweep. `finishLocal` certifies material outside it and all unaffected
trimmed faces. The complete translated target must survive, and every output
support must match an original support or that target. Cylindrical material-side
orientation is also checked, preventing extension past a cap from inventing an
oppositely oriented cylindrical cavity.

Cylinder surface identity now compares an unoriented axis line and radius;
`IsCoaxial` had incorrectly treated OCCT's reversed parameter axes as different
supports. Physical hole/boss classification and oriented normals remain checked
separately. Local normalization may keep a fresh copy of certified split faces
when same-domain merging fails; the unmerged copy must pass the same closed-solid,
self-interference and independent Common/material checks. Invalid unifications
are never accepted. Other normalization callers retain their existing default.

The next-hole regression exposed a second global dependency: after increasing
stock height beside an overhang, the physical hole-rim slab also includes some
boss overhang and cannot equal rectangular stock. A final local hole fallback
now recognizes the complete connected mantle and two circular rims, planar
neighbors, inward normal and radial material occupancy. The old finite cylinder
must be void; filling it must add exactly that cylinder and close both real rim
disks on exterior planes. Subtract the translated finite tool and use the same
local preservation certificate. Unrelated features cannot disappear or change
their trimmed boundary surfaces. Boundary notches/complete hole exit remain
legitimate; ambiguous interactions, disconnected results and unexpected supports
are refused. No stock proxy, Part metadata or stored operation history is used.

## Scope and limitations

Both original boss policies remain explicit and unchanged. Unaffected side and
bottom planes, stock top and boss cap now reconstruct for the tested signed
offsets after both policies; Extend's underside and stock front also work.
Unchanged complete holes and separate fully supported complete bosses remain
editable. Tests include hole edits after the newly reconstructed planar results.
The Trim front plane merged with the truncated boss wall has a non-invariant
curved neighbor and is safely refused by the local planar path. Repeated lateral
movement of the truncated/overhanging selected boss still needs a broader contact
reconstruction algorithm. This work does not add arbitrary boundary-boss editing
on general nonrectangular supports, tilted/curved planar intersections, blends,
partial hole recognition or unrestricted B-Rep editing.

The command remains face-specific: recognizing/refusing the modified boss does
not govern another face's action. No selection, Part replacement, persistence or
history infrastructure was changed. The replay now checks every planar context
action, signed axial previews, a confirmed bottom edit, repeated activation,
Undo/Redo, unchanged-hole edits, another Part, UUID/placement/appearance/browser
identity and source/file immutability. It runs from either native supplied file
and has a portable geometric equivalent; neither binary is a Git fixture.

## Verification

Incremental application, cylindrical geometry/session and shared face-transform
targets built successfully. All nine focused tests passed: cylindrical geometry,
independent features, boundary follow-up geometry, local neighborhoods, planar
Move Face, cylindrical sessions, native recognition, committed boundary sessions
and face-transform geometry (165 seconds total). Native-reader boundary replay
also passed separately for each supplied `.odc`, exercising both policies.
Source serialization checks, independent material oracles/classification and
closed manifold B-Rep checks passed. Both file SHA-256 hashes remain unchanged;
neither file is staged. `git diff --check` passes. No full CTest suite was run.
