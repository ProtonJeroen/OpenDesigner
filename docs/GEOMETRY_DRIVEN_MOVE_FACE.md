# Phase 9D.4 — geometry-driven direct modeling foundation

Capability is determined from the current B-Rep, never primitive flags, feature
creation history, Part kind or native/import provenance. This applies equally
to primitives, sweeps, Booleans, earlier direct edits and imported solids whose
actual surfaces/topology satisfy an operation's certificate.

## Existing components and limitations

Move Face uses OCCT intersection offsets; Rotate uses DraftAngle and analytic
surface correspondence. The shared planar material-cell reconstruction permits
certified support disappearance/splitting. Delete Face uses OCCT defeaturing,
edge/face adjacency, planar rim closure and independent feature-material oracles.
Boolean helpers isolate inputs and use non-destructive Fuse/Cut/Common. Shared
normalization verifies material equivalence and one valid closed solid, including
self-interference checks. Sessions already retain immutable source snapshots.

Cylindrical Move Face has additional rectangular-stock, complete-feature and
global decomposition limits. The 9D.3 exterior-preservation path protects boundary
bosses during hole/side-plane edits, but the reverse case (an unchanged complete
boss beside an open boundary hole) still fails whole-Part recognition. These are
reconstruction-policy limits, not feature-history requirements or selection bugs.

## Incremental design

Add geometry-only neighborhood analysis to `BRepReconstruction`: isolate/map the
source, grow the connected selected analytic support patch, collect actual boundary
edges and adjacent faces, and reject ambiguous/nonmanifold adjacency. Analysis
does not require rectangular stock or a count/pattern of unrelated cylinders.
It does not by itself promise an available reconstruction algorithm.

An operation then supplies its affected/moved faces, an explicit changed-material
region and a candidate. Local acceptance independently checks material outside
that region and trimmed surface coverage of every unaffected face; output mapping
records actual surviving/merged/split patches. Final normalization, closed-solid
validation and operation-specific material certificates remain mandatory. A local
declaration alone cannot certify arbitrary material inside the edit region.

## First additional topology class

Move a complete cylindrical boss with one full planar disk cap and one planar
support after a different hole becomes a side notch. Use actual mantle/rim
adjacency, oriented normals and material classification; remove mantle+cap with
OCCT defeaturing, independently verify the exact removed cylinder, and preserve
the resulting nonrectangular support/material without regenerating the Part.

This first local path requires the original and translated base disks, with a
tolerance collar, to lie completely inside the actual healed planar support face.
Thus it introduces no ambiguous bridge, support extension or interaction with a
hole/notch/other boss. Trim and Extend coincide in this certified interior case;
the existing boundary-policy paths remain in place for their supported classes.
Repeated local edits reanalyze only the new B-Rep. No feature IDs/history are saved.
Acceptance also requires the entire translated mantle and cap to survive as
actual boundary surface regions, allowing splits/merges without absorbing the
boss into other material.

## Focused verification

Incremental application, geometry and session builds passed. All eight focused
Move Face/cylindrical geometry and session regressions passed. After the final
complete-wall/cap guard, the local neighborhood test and native-reader replay of
both `moveCylinder.odc` and `moveCylinderBug.odc` passed again. These cover repeated
boss edits after a hole becomes a notch, hole/planar edits after boss boundary
crossing, material occupancy, immutable input, selection refresh and safe refusal.
Both supplied files retain their original SHA-256 hashes and remain untracked.
No full CTest suite was run.

## Later topology classes

Repeated partial/overhanging boss movement across one planar side is now covered
by [phase 9D.7B](REPEATED_BOUNDARY_BOSS_MOVE.md). Contact-changing planar support edits,
local Trim/Extend across general curved/concave support boundaries, blind or
cross-drilled holes, blends and freeform reconstruction need additional local
material/trim certificates. Existing numerical and complexity budgets remain
safety limits, not feature recognition or provenance rules.
