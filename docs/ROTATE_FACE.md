# Rotate Face (v0.3.6)

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

## Supported cases and refusal behavior (v0.3.6)

Rotate Face keeps the existing OCCT DraftAngle construction and adds the shared
[planar material-cell reconstruction path](BREP_RECONSTRUCTION.md) when that
builder cannot reliably cross a topology event. Final normalization merges safe
coplanar faces and collinear segments, with Common-confirmed material equivalence.
The expected signed target plane, valid closed single solid, analytic supports,
self-interference and intended material distribution remain acceptance conditions.
No sweep/revolve bridge is created.

The Unite staircase riser now reaches **+75° about +Y through (60,0,50)**, beyond
the v0.3.5 failure where the z=90 cap must disappear. Seven necessary faces remain,
with obsolete cap edges/vertices removed. Negative rotations preserve the existing
overhang behavior: -75° retains eight necessary faces rather than clipping the
result to the original bounding box. Independent material probes and exact
piecewise-linear volume integration cover ±60°, ±75°, 68.1° and 68.3° around the
cap-disappearance threshold. Ordinary signed Box rotations remain tested.

The new path requires all-planar supports, one target-plane patch, at most 12
planes and 32 source cells, and a target normal remaining on its original side
(approximately below 90°). The numeric command still accepts finite angles below
179° in magnitude for the existing builder; that is not a promise that all such
angles can be rebuilt. Curved targets, unsupported surfaces, ambiguous partitions,
unbounded/degenerate intersections and complete target disappearance are refused.

Final target mapping follows the requested plane after unification, rather than
an obsolete edge or face index. CommandSession still owns the immutable source,
orange ring, blue full-solid preview, Cancel/OK and one cached history entry.
The larger-angle riser cases also run through preview/cleanup/Undo/Redo tests.
Manual Fedora KDE verification of v0.3.6 is pending.

The exact Fedora +30° rotation about +X through (125,80,60) remains
material-equivalent to the user's accepted rotated B-Rep. A regression then moves
its oblique target +49 mm and verifies physical neighbor trimming, rather than
accepting a valid solid with an unintended triangular wing. See the
[exact fixture](../tests/fixtures/rotate30-move49/README.md).
