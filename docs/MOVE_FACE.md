# General Move Face (v0.3.3)

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

## Supported geometry

This first implementation accepts exactly one planar face on one valid, closed
solid with one shell and no extra loose faces, edges or vertices. Every face in the solid must have exactly one wire, and
all supporting surfaces must be **planes, cylinders or cones** (maximum 64 faces).
The selected face may have planar, cylindrical or conical neighbours.

Verified cases include:

- Box faces, with straight planar neighbours.
- The top and bottom circular caps of Cylinder, with its cylindrical side fixed.
- The top and bottom caps of a truncated Cone, with its conical side fixed.
  Extending a cap changes its radius according to that fixed cone surface;
  this differs deliberately from primitive height Pull.
- Concave planar Boolean solids, such as an L-shaped extrusion, and simple
  repository Boolean Subtract results.
- GeneralBRep shapes with a shape location and parts with existing instance
  rotation/translation. The world-space distance is converted to local units,
  including the instance's uniform scale.

Acceptance is conditional on OCCT successfully rebuilding the intersection
curves and on all validation checks. A supported surface type does not promise
that every possible distance is valid. Large inward moves, crossing the opposite
cap, moving past a cone apex, removing a face or splitting/merging topology are
refused. Face, edge and vertex counts must remain unchanged.

Curved selected faces, spheres, tori, blends, fillets, B-spline/freeform surfaces,
multi-wire faces (including through-hole caps), multiple shells/cavities, open
shells and multi-solid parts are outside this version's scope. The conservative
surface/wire policy applies to the entire solid, even when an unsupported face
is distant from the selection. Simultaneous movement of several faces or parts
is not provided. Arbitrary reference directions and face rotation remain future
work. Calculations run synchronously for these bounded small solids.

## Geometry and transaction architecture

`MoveFaceGeometry` is independent of viewport picking and document mutation.
It uses OCCT `BRepOffset_MakeOffset` with zero offset for unselected faces,
`SetOffsetOnFace` for the selected plane and intersection joins. Adjacent support
surfaces stay fixed; their boundaries are rebuilt by OCCT. This is not a part
translation, prism added to the old face or a Box-bounds reconstruction.
The API is documented in the [OCCT reference](https://occt3d.com/dev/doc/refman/html/class_b_rep_offset___make_offset.html).

Offset construction operates on a deep copy of topology and geometry. Input
and result checks include BRep validity, self-interference analysis on isolated
copies, a single solid/shell and positive finite volume. A closed shell returned
by OCCT can be promoted to a solid and oriented; open shells are never sewn or
healed into acceptance. Each nonzero result must change volume in the requested
direction, preserve topology counts, and retain every unselected analytic
support surface. The selected support plane must move by exactly the requested
normal displacement. Exceptions or failed checks return a refusal, not a
partially updated document.

The existing `CommandSession` gains a MoveFace adapter and a separate temporary
AIS presentation. The original presentation is hidden while a valid candidate
is shown, while canonical shapes, part transforms, metadata and history remain
unchanged. No document or browser rows are created for previews.
A confirmed edit stores a `FaceChange` geometry snapshot in the existing
`PullChange` history. Restoration copies geometry/primitive state into the same
slot, without reverting names, colors or visibility changed independently.
There is no second Undo stack. Existing Move/Rotate sessions, Pull and Boolean
confirmation semantics remain as described in [Command sessions](COMMAND_SESSIONS.md).

## Verification and Fedora KDE checks

```sh
cmake -S . -B build -DOPENDESIGNER_BUILD_TESTS=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
git diff --check
```

The `move-face` suite checks analytical positive/negative cap volumes, fixed
opposite caps, source B-Rep preservation, a concave Boolean, located geometry,
unsupported faces, collapse, numeric preview, confirmation/cancellation,
primitive metadata restoration, existing world transforms, menu/viewport
selection, arrow dragging, Enter/Esc, tool switches, stable browser selection
and cached Undo/Redo. All previous suites remain enabled.

For manual Fedora KDE verification:

1. Move a Box top and side face both inward and outward. Change exact input
   several times before OK. Confirm that the opposite face stays fixed.
2. Repeat on both Cylinder and Cone caps. Check that Cone radii follow the
   original conical side surface, unlike Primitive Pull.
3. Rotate and move the part first; repeat on a Boolean result. Confirm stable
   names/colors/rows, and use one Undo/Redo for the edit.
4. Try a curved face, a through-hole cap and an excessive inward distance.
   Confirm refusal, then enter a valid value or cancel.
5. Compare Cancel/Esc and a tool switch before/after dragging. Check selection,
   visibility and original geometry, then exercise Move/Rotate/Pull/Booleans.

The new workflow still needs the user's manual KDE validation. Other operating
systems and display configurations have not been manually verified here.
