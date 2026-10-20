# Align Face (v0.3.6)

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

## Geometry scope (v0.3.6)

Align Face shares [B-Rep reconstruction](BREP_RECONSTRUCTION.md) with Move and
Rotate Face. It benefits from final coplanar/edge unification and the bounded
planar material-cell fallback for controlled topology events. Cleanup must retain
Common-confirmed material equivalence; constructing a valid solid alone is not
enough. The reference Part remains read-only.

After rotation/unification, Coplanar uses the mapped final target patch for its
offset. Ambiguous multiple target patches are refused rather than moving an
arbitrary fragment. A regression first removes the Unite staircase's high cap
with +75° Rotate, then aligns its surviving mapped riser to an offset reference
plane and checks exact volume and material occupancy. Existing Parallel/Coplanar
and external-reference workflows remain tested. The larger signed riser sessions
also test reference planes at x=0 and x=120, including terrace disappearance.

The new cell path is all-planar and bounded to 12 distinct supports and 32 cells;
target rotation must remain on the original side. Freeform/blend geometry,
multiple shells/solids, lost targets, ambiguous partitions and unreliable results
remain refused. The conservative plane/cylinder/cone construction path remains
available where no new topology algorithm is needed. Calculation is synchronous.
All previews, orange manipulators, Cancel/OK and cached Undo/Redo behavior remain.
Manual Fedora KDE verification of v0.3.6 is pending.

The exact Fedora Rotate +30° / Move +49 mm fixture also verifies a subsequent
coplanar Align using the final mapped oblique face. It shares the correction that
retains physical target-neighbor trims across material-cell reconstruction.
Concave/holed or conflicting neighbor-trim decompositions remain refused by the
planar fallback. See [the diagnosis](BREP_RECONSTRUCTION.md).
