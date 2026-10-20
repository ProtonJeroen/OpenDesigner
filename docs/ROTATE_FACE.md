# Rotate Face (v0.3.5)

Select one planar Face and choose **Rotate Face** from its context menu or from
**Modeling**. Choose a straight boundary edge or a projected World X, Y or Z
reference axis. World axes are projected into the selected plane; an axis normal
to that plane is rejected. The orange ring marks the axis and pivot. Drag the
ring or enter a signed angle in degrees. Positive and negative values follow
the right-hand direction of the displayed axis. Existing angle snapping applies
to a drag; hold Shift to bypass it. Numeric entry is exact.

The translucent candidate is a preview. **OK/Enter** commits one operation;
**Cancel/Esc**, closing the parameter panel, or switching tools discards it.
One Undo restores the original shape and primitive parameters; Redo restores the
cached result. The part slot, name, color, visibility and Structure Browser row
remain stable. A directly edited primitive becomes GeneralBRep, as it does for
Move Face. The reference edge is selected again for each new Rotate Face command.

## Supported cases and refusal behavior (v0.3.5)

Planar targets on one closed analytic solid use shared
[B-Rep reconstruction](BREP_RECONSTRUCTION.md). OCCT same-domain unification
removes redundant Boolean seams before `BRepOffsetAPI_DraftAngle` rebuilds the
rotated plane and adjacent trims. The requested signed plane, preserved
analytic supports, solid closure, volume and self-interference are checked.
No revolve, sweep or bridge geometry is generated. Counts need not remain equal.

Verified cases include signed Box rotations around different straight axes,
edited GeneralBRep, simple Boolean faces, and the two-height Unite riser at
±8 degrees around a horizontal boundary edge. The riser test checks exact wedge
volume and analytical material occupancy, including points immediately beside
the requested plane. This does not establish support for every edge or angle.

Curved targets, freeform/blend supports, multiple shells/solids, disappearing
support surfaces, ambiguous target mapping, self-intersections and failed kernel
reconstruction are refused. The axis must lie in the target plane; angles must
be finite with magnitude below 179 degrees. All calculations use isolated copies.

Rotate Face retains the existing CommandSession, separate AIS preview and
FaceChange snapshot history. The geometry tests and move-face transaction tests
cover the riser; selection-context covers the existing axis selection workflow.
Manual Fedora KDE verification of v0.3.5 passed; the user confirmed the tested geometry and live preview workflow.
