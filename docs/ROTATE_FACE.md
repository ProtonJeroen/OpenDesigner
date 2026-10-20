# Rotate Face (v0.3.4)

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

## Supported cases and refusal behavior

The operation accepts one planar face of one valid, closed, single-solid B-Rep
with one shell and plane/cylinder/cone support surfaces. A straight edge
bordering the target face or a projected world reference axis defines the
rotation line. OCCT `BRepOffsetAPI_DraftAngle` rotates the target support and
rebuilds intersections with the existing neighboring surfaces. No revolve,
rotational sweep, bridge face or Boolean volume is used. The result must remain
one valid closed solid, preserve face/edge/vertex counts and per-face wire
connectivity, preserve neighboring analytic supports, and contain the requested
rotated target plane. Candidates involving unsupported topology changes or
self-intersections are refused. The original B-Rep is never modified.

Verified automated geometry includes positive and negative Box rotations,
different straight axes, an edited GeneralBRep and a simple Boolean result where
DraftAngle preserves the existing topology. The narrow vertical transition on
the two-height Boolean Unite fixture is explicitly refused: OCCT cannot rebuild
it without topology changes in the current supported path. Complex Boolean
results, blends, freeform supports, multiple shells/solids and unsupported
neighboring surfaces are refused with an explanation; the original solid and
history remain unchanged. Diagnostics include topology counts, target
wire/edge counts, adjacent surface types and OCCT validation status.

Curved target faces, freeform surfaces, blends/fillets, multiple shells or
solids, multi-face rotation, topology changes and reference axes that do not
lie in the target plane are outside this version. Complex Boolean and
GeneralBRep topology may be rejected conservatively. Computation is synchronous.

Rotate Face uses the existing `CommandSession`, temporary AIS preview and
chronological `PullChange` history with a `FaceChange` snapshot. It does not
rotate the part instance and does not create a second undo stack.
