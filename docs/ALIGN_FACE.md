# Align Face (v0.3.4)

Select one planar target Face and choose **Align Face** from its context menu or
from **Modeling**. Then select a planar reference Face, on the same Part or a
different visible Part. The session panel labels the Target and Reference in
the status message. The reference Part is only read; the operation edits the
target's geometry in its existing part slot.

Choose one of two modes before confirming:

- **Parallel** rotates the target plane until it is parallel to the reference
  plane while keeping the target face centroid stationary. Its signed distance
  from the reference plane can therefore change as it tilts.
- **Coplanar** also moves the target plane onto the reference plane.

Both modes use a temporary shaded preview. **OK/Enter** commits one geometry
change and one Undo step. **Cancel/Esc**, closing the panel, or choosing another
tool discards it. Undo/Redo restores the target geometry from cached snapshots;
target identity, name, color, visibility and browser row remain stable. Reference
geometry and metadata are never changed.

## Geometry scope

The target must be one planar face of a valid closed single-solid B-Rep with one
shell and plane/cylinder/cone support surfaces. Target tilt uses
`BRepOffsetAPI_DraftAngle`; Coplanar then uses the shared
`MoveFaceGeometry`/`BRepOffset_MakeOffset` path. Neither operation creates a
sweep, revolve or bridge volume. Both must preserve the existing face/edge/
vertex counts, per-face wire connectivity and neighboring analytic supports,
while reconstructing only the target plane and trim boundaries. A valid closed
solid and self-interference checks are also required. Simple planar Boxes,
edited analytic GeneralBRep and Boolean faces are supported only when OCCT can
rebuild them without changing that topology.

Freeform faces, blends/fillets, multiple shells or solids, unsupported analytic
neighbours, topology-changing candidates and invalid or self-intersecting
results are refused without changing the source model. In particular, the
narrow riser of the two-height Boolean Unite regression is refused because its
adjacent edge splits are not reconstructed reliably. Align Face uses the same
topology-preserving geometry validation and diagnostics as Move Face and Rotate
Face. It does not attempt generic shell sewing or unrestricted reconstruction.
Parallel is geometric plane alignment; it does not aim a particular face
normal direction. Alignment computations run synchronously. Automated coverage
checks Coplanar to a reference plane, Parallel without moving the target offset,
tilted parallel alignment and preservation of an external reference Part.

Align Face is an adapter on the shared `CommandSession` and existing modeling
history, not a separate transaction or boolean/part-transform operation.
