# Align Face (v0.3.5)

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

## Geometry scope (v0.3.5)

The planar target uses the shared [B-Rep reconstruction layer](BREP_RECONSTRUCTION.md).
Parallel tilts through OCCT DraftAngle; Coplanar composes that with an intersecting
face offset. Redundant Boolean topology is normalized and the target is mapped
through OCCT history before construction. Neither stage adds a sweep or bridge
volume. Each stage checks analytic supports, the requested plane, closure,
positive volume and self-interference. Topological splits/merges are permitted
without relying on equal face/edge/vertex counts.

The two-height Unite riser is verified for Coplanar x=55 and x=65, unchanged
Parallel, and tilted Parallel/Coplanar planes with slopes ±0.1. Tests compare
material occupancy and exact volumes, not just validity. Existing Box and
reference-Part session tests remain enabled; riser transaction tests exercise
both signs, preview/cancel/confirm and cached Undo/Redo.

Freeform/blend supports, multiple shells/solids, loss of original analytic
supports, ambiguous selected-face mapping and failed reconstruction are refused
without document mutation. Parallel aligns the plane, not a prescribed outward
normal direction. Computation remains synchronous and conservative.
Manual Fedora KDE verification of v0.3.5 passed; the user confirmed the tested geometry and live preview workflow.
