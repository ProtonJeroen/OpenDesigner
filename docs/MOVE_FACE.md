# General Move Face (v0.3.5)

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

## Supported geometry (v0.3.5)

Planar targets on one closed valid solid/shell, up to 64 faces, with plane,
cylinder and cone supports. Box faces, cylinder/cone caps, simple Subtract
faces and the two-height Unite riser are covered by automated geometry tests.
Both signs of a face offset are supported conditionally on reconstruction.
Cone cap radii follow the existing conical support, rather than primitive Pull.
Located B-Reps and existing part instance transforms remain supported.

The shared [B-Rep reconstruction layer](BREP_RECONSTRUCTION.md) removes redundant
Boolean seams on an isolated copy, maps the target through OCCT history, and
rebuilds intersections with `BRepOffset_MakeOffset` in intersection mode. It
checks the requested plane, outward volume change, original analytic supports,
closure, positive volume and self-interference. Coplanar faces and collinear
edges can merge; edge/vertex counts and per-face wire sizes need not remain
identical. There is no prism, extrude, sweep or bridge-volume fallback.

Support-disappearing collapses, unsupported/freeform surfaces, blends, spheres,
tori, multiple shells/cavities, disconnected solids and ambiguous target history
are refused. A surface type alone does not guarantee every distance succeeds.
No open shell is sewn into acceptance. Failed previews leave source geometry,
part properties, selection snapshots and history intact.

## Verification

Run the CMake build with `OPENDESIGNER_BUILD_TESTS=ON`, all CTests and
`git diff --check`. The move-face suite verifies signed Box/cylinder/cone
cap dimensions and volumes, Subtract geometry, source preservation, transformed
parts, numeric/drag/keyboard input, preview/cancel/commit and cached Undo/Redo.
It also runs Move/Rotate/Align transaction checks on the Unite riser while
preserving name, color, presentation, transform and browser row.
The face-transform-geometry suite independently checks Unite material occupancy
and exact volume, including rotated wedge shapes; see the shared layer document.
Manual Fedora KDE verification of v0.3.5 passed, as confirmed by the user. For repeatable checks, use both signs,
Cancel/Esc, OK/Enter and one Undo/Redo on the documented fixture.
