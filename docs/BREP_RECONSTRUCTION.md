# Advanced B-Rep Reconstruction — v0.3.5

The direct-modeling contract follows the locally documented SolidDesigner 2008
v16 workflow: explicitly select a face command, alter existing supports and
retrim their neighbors, preview, then OK or Cancel. This is an independently
implemented analytic subset, not a claim of complete SolidDesigner compatibility.
See [the reference analysis](SOLIDDESIGNER16_WORKFLOWS.md).

## Architecture

`BRepReconstruction` is a geometry-only component shared by Move Face,
Rotate Face and Align Face. It owns preparation, face provenance, analytic
support acceptance and solid validation. Existing `MoveFaceGeometry` and
`FaceTransformGeometry` remain the kernel construction adapters. No viewport,
primitive feature parameters, document metadata or history stack enters the
shared component.

1. The adapter validates membership, planar target, one solid/shell, supported
   analytic surfaces and the 64-face bound. Numeric zero remains a no-op.
2. `prepare` deep-copies topology **and geometry**, then uses
   `ShapeUpgrade_UnifySameDomain` to merge coplanar faces and collinear edges.
   Safe-input mode is enabled. Edges of the copied selected face are protected:
   selecting one coplanar patch must not silently select its neighbor as well.
3. Copy history composed with unification's `BRepTools_History` produces an
   original-face to normalized-face correspondence (`Prepared::history`). Many
   original faces may map to one normalized face. Every original face must map,
   and the target must map unambiguously to one face. Normalization must retain
   validity, closure, volume and centroid. The original geometry never enters a
   mutating kernel operation.
4. Move uses `BRepOffset_MakeOffset`, zero on all unselected faces, intersection
   mode, and a signed offset only on the selected plane. Rotate uses
   `BRepOffsetAPI_DraftAngle` with its neutral plane through the requested axis.
   Its side conventions are tried only on isolated copies; the expected signed
   plane and acceptance checks decide success. Align composes rotation and, for
   Coplanar, offset. These OCCT builders extend/restrict support surfaces and
   reconstruct intersection curves, pcurves, edges, vertices, wires and faces.
5. `validSolid` requires exactly one solid and closed shell, no loose topology,
   BRepCheck validity, positive finite volume and an isolated
   `BOPAlgo_ArgumentAnalyzer` self-interference check. A closed offset shell may
   be oriented and promoted to a solid. Open shells are refused.
6. `supports` checks both directions: every original support, substituting only
   the expected target plane, must remain; every result face must use one of
   those supports. Plane location and outward orientation, cylinder axis/radius,
   and cone axis/apex/semi-angle are compared. New transition surfaces and lost
   supports are refused. Move additionally requires volume change in the requested
   outward/inward direction. Rotate requires the actual requested signed plane.
   Neither global counts nor per-face wire/edge counts are acceptance criteria.
7. Existing CommandSession owns the temporary AIS candidate and FaceChange
   snapshot. Confirmation replaces geometry in the same Part slot and records one
   history entry; cancellation discards candidates. Old owners are discarded on
   commit, retained on Cancel. Cached Undo/Redo restores exact original/result
   topology. Name, color, visibility, presentation, instance transform and browser
   row identity remain outside geometry reconstruction.

The exposed face history describes **normalization**, not persistent naming of
all final edges/faces across arbitrary future edits. Final support correspondence
is checked geometrically; persistent subshape IDs remain future work.

## OCCT choices and limits

[Same-domain unification](https://occt3d.com/dev/doc/refman/html/class_shape_upgrade___unify_same_domain.html)
removes redundant seams rather than weakening validity checks to accommodate them.
[Intersection offsets](https://occt3d.com/dev/doc/refman/html/class_b_rep_offset___make_offset.html)
and [DraftAngle](https://occt3d.com/dev/doc/refman/html/class_b_rep_offset_a_p_i___draft_angle.html)
already implement support extension, intersection and trimming. They are preferred
to a separate custom plane intersection/wire kernel. Generic `GeomLib` surface
extension, `BOPAlgo_Splitter`, unrestricted sewing and ShapeFix repairs are not
used as fallback: they do not independently determine the intended material cell.
Healing an open or invalid candidate would therefore not prove the requested edit.

Supported targets are planes; all supports must be planes, cylinders or cones.
Cylinder and truncated-cone cap Move cases retain their existing support geometry
and are tested with exact analytical volumes/radii. This does not promise arbitrary
rotation against curved neighbors. No freeform reconstruction is implemented.

Controlled topology changes include removing redundant coplanar Boolean faces and
collinear edge seams, and accepting valid intersection-generated edge/vertex or
trim changes on the same analytic supports. The verified Unite fixture reduces
its original split topology. There is no general support-disappearance algorithm:
collapses that erase a terrace/support, apex crossings, multiple solids/shells,
cavities, blends/freeform, ambiguous target mapping and kernel failures are refused.
A broad new class of face splits/merges is not claimed solely because its result
passes BRepCheck. Computation is synchronous and bounded to small solids.

## Reproducible Unite fixture and evidence

Construct a Box from `(0,0,0)` to `(100,80,50)` and a second Box from
`(60,0,0)` to `(160,80,90)`, then Boolean Unite. Select the vertical riser at
`x=60`, `z=50..90`, `y=0..80` (outward normal -X). Initial volume is
`960000 mm³`. No primitive recognition is used by the face operation.

| Command | Verified parameters | Independent expected geometry |
| --- | --- | --- |
| Move | ±5 and ±25 mm outward | Riser x=60-distance; volume change 3200×distance |
| Rotate | ±8° around a straight Y boundary edge | x(z)=60+tan(angle)×axisY×(z-pivotZ); exact wedge volume |
| Align Coplanar | x=55 and x=65 | Riser reaches reference plane; signed terrace volume change |
| Align Parallel | Parallel reference offset from riser | Original riser plane/material retained |
| Tilted Align | Parallel and Coplanar; slopes ±0.1 | x(z)=60+slope×(z-70), or 55+slope×(z-70) |

`face-transform-geometry` checks B-Rep validity, exact analytical volumes and
independent 3D solid-classifier occupancy throughout and outside the fixture,
including probes 0.05 mm either side of the requested riser. Thus an unwanted
extruded bridge or wrong rotation cannot pass merely by producing a valid solid.
The source B-Rep is serialized before and after successful/refused operations.
Distances +100 and -120 are refused as collapse/crossing cases.

`move-face` additionally exercises all three adapters at both signs through
Preview, Cancel, OK and a single cached Undo/Redo while preserving Part properties,
instance transform and browser row. These tests set the known rotation axis and
Align reference directly; existing selection-context tests cover interactive
axis/reference picking, passive selection and tool termination. Existing Box,
Subtract, cylinder/cone cap and command lifecycle regression suites remain enabled.
The evidence applies to the stated fixture and parameters, not every Boolean
riser, axis, angle, offset or curved support combination. The user has confirmed
all tested functionality on Fedora KDE, including the face geometry and the
preview refinement below.

## Future Extrude, Revolve and Sweep integration

The input contract is a canonical B-Rep, a member face and coordinates in that
B-Rep's frame. The construction history of a solid is irrelevant. Future feature
commands can pass their resulting analytic solids through the same adapters and
shared preparation/acceptance. Their original profile/path must not be regenerated
as a substitute for direct face reconstruction. Outputs with unsupported spline,
torus or blend supports remain refused until a separately validated algorithm is
implemented. Document adapters retain their existing transformation conversion,
primitive invalidation, metadata and transaction responsibilities.

## Validation

```sh
cmake -S . -B build -DOPENDESIGNER_BUILD_TESTS=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
git diff --check
```

Viewport tests require the existing X11/OpenGL display. See README and the command
session documentation for build dependencies and the unchanged interaction model.

## Preview presentation refinement

The user has manually confirmed the geometry of Move, Rotate and Align on Fedora
KDE, including the former Unite-riser failure. The reported remaining visibility
issue belongs to the candidate-to-AIS presentation path, not a change to the
reconstruction acceptance policy. That path now explicitly meshes the isolated
candidate, verifies all face triangulations, installs a non-selectable shaded
blue translucent solid, and updates/invalidates the retained viewer scene. The
source is hidden only after the candidate is ready. Rendering or reconstruction
failure restores the original and prevents confirmation. Replaced, cancelled
and committed candidates are fully removed from the AIS context.

`move-face` verifies rendered pixel coverage for all three riser commands, the
full candidate shape, replacement on input, invalid-input recovery, original
immutability and cleanup using both retained handles and total AIS object counts.
These are OCCT offscreen image checks; native GLX grabs are unusable in this test
environment, and the exact KDE compositor failure was not reproduced here.
The user subsequently confirmed that the Fedora KDE manual tests passed for all
tested functionality, including the visible-preview fix and background Undo/Redo
menu (2026-10-20). Further topology improvements are planned for v0.3.6. See
[Command sessions](COMMAND_SESSIONS.md) for the display contract and evidence.
