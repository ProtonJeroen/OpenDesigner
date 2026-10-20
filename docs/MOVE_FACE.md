# General Move Face (v0.3.6)

Choose **Modeling > Move Face**, then click one planar face. A preselected face
can also be used, including through the viewport Face context menu. The visible
Faces selection mode keeps Boolean/GeneralBRep faces selectable even with part
transform tools enabled; see [Selection/context menus](SELECTION_CONTEXT.md). The **orange** arrow points along the face's outward normal.
Drag it or type a signed displacement in millimeters: positive moves outward,
negative inward. This is a relative displacement, not an absolute primitive
height or diameter. Existing linear snapping and Shift bypass apply.

A translucent solid previews the result. **OK/Enter** confirms once;
**Cancel/Esc**, closing the parameter tool window or switching modeling tools
abandons the candidate. Re-grabbing the orange arrow continues the same session.
An invalid distance restores the original display, disables OK and cannot be
confirmed with Enter. Correct the value or cancel. A zero offset adds no history.
A normal near the camera direction may not be draggable; exact input remains
available, or orbit the camera first.

Move Face changes geometry in the existing part slot. Move Part translates the
whole part; Primitive Pull edits recognized primitive parameters. They remain
separate operations. After a nonzero Move Face, the edited part becomes a
GeneralBRep and loses primitive Pull eligibility. This prevents later Box bounds
rebuilding or primitive regeneration from overwriting the edited topology.
Undo restores the original shape, kind and primitive parameters; Redo restores
the cached direct-edit result. Names, colors, visibility, presentation identity
and Structure Browser row IDs are preserved. Confirmation replaces obsolete face
owners with a whole-part selection; Cancel restores the original face owners.

## Supported geometry (v0.3.6)

Planar targets on one closed valid solid/shell, up to 64 faces, with plane,
cylinder and cone supports. Box faces, cylinder/cone caps, simple Subtract
faces and the two-height Unite riser are covered by automated geometry tests.
Both signs of a face offset are supported conditionally on reconstruction.
Cone cap radii follow the existing conical support, rather than primitive Pull.
Located B-Reps and existing part instance transforms remain supported.

The shared [B-Rep reconstruction layer](BREP_RECONSTRUCTION.md) normalizes both
input and output. OCCT same-domain merging and connectivity-checked internal
subshape cleanup remove redundant final trims. Common confirms that cleanup
preserves material; disconnected coplanar patches remain separate when necessary.

The usual intersection-offset path keeps original analytic supports. When it
fails, bounded all-planar material-cell reconstruction can accept controlled
support disappearance and newly exposed patches on existing planes. It uses
OCCT Splitter, halfspace Common and Fuse, not an extruded bridge volume. The
requested target plane must survive (possibly merged with another exterior face)
and volume must change in the requested direction.

On the two-height Unite fixture, Move -60 reaches x=120 with 8 faces; +60 removes
the low terrace and produces 6 faces; +100 reaches x=-40 and exposes the necessary
underside on z=50 with 8 faces. A notched Box preserves two disconnected coplanar
wall patches when deepened, and merges to 6 faces when its notch closes. Tests
check material distribution as well as exact volumes and topology.

The new path requires one target-plane patch, at most 12 distinct planes and 32
source cells. Complex nonconvex partitions, lost targets, multiple shells/solids,
freeform/blend surfaces and unreliable intersections remain refused. Cylinder/
cone cap edits keep the conservative existing construction path; there is no new
general curved-surface topology algorithm. Failed previews leave document geometry
and history intact. See the shared architecture for the exact scope and limits.

## Verification

Run the CMake build with `OPENDESIGNER_BUILD_TESTS=ON`, all CTests and
`git diff --check`. The move-face suite verifies signed Box/cylinder/cone
cap dimensions and volumes, Subtract geometry, source preservation, transformed
parts, numeric/drag/keyboard input, preview/cancel/commit and cached Undo/Redo.
It also runs Move/Rotate/Align transaction checks on the Unite riser while
preserving name, color, presentation, transform and browser row.
The face-transform-geometry suite independently checks Unite material occupancy
and exact volume, including rotated wedge shapes; see the shared layer document.
Manual Fedora KDE verification of v0.3.6 is pending. For repeatable checks, use both signs,
Cancel/Esc, OK/Enter and one Undo/Redo on the documented fixture.

## Exact oblique-face regression: Rotate +30° then Move +49 mm

The supplied Fedora debug B-Rep now moves the selected oblique plane +49 mm
without extending a triangular solid wing through the real front face. The
reconstruction retains neighboring physical trim supports even where source-cell
splitting made them temporarily redundant. The low terrace remains unchanged;
the front/side faces extend to the requested plane. Correct volume is
1091890.5249039864 mm³ (recorded faulty result: 1156871.7334342287 mm³).

Exact fixture provenance, material samples and the independent oracle are in
[the fixture documentation](../tests/fixtures/rotate30-move49/README.md).
Visible preview, Cancel, Enter, Undo/Redo and Part identity are tested on this
same B-Rep. The bounded planar fallback now explicitly refuses concave, holed or
conflicting target-neighbor decompositions; existing native analytic construction
paths retain their conservative support rules. Manual Fedora KDE retest pending.
