# Command sessions and preview (v0.3.2–v0.4.1)

Interactive Move Part (axis gizmo and orange face-normal arrow) and Rotate Part
now have a temporary preview. Select geometry and explicitly choose its Modeling/context action to show a gizmo.
Drag it, release, and optionally
edit the localized numeric value. The preview follows valid numeric edits.
Choose **OK** or press **Enter** to apply it; **Cancel** or **Esc** discards it.
A zero-distance/zero-angle confirmation creates no history entry.

The axis gizmos, snapping (Shift bypass), mouse navigation, numeric units and
v0.3.1 rotation pivots are retained. Rotate gizmos use local axes and the menu
Rotate Part dialog still uses world axes. A group's pivot is the unweighted
average of its world volume centroids; all members receive the same rigid
world transform. A Move stores a rigid instance translation, preserving the
canonical B-Rep and its local primitive dimensions.

Changing a transform tool, selecting something else or starting another
modeling operation cancels the pending transform first. Clicking empty space
therefore discards a preview rather than silently applying it. Undo/Redo also
cancel a pending preview before operating on the existing history. Every successful
Undo or Redo clears all Face/Edge/Part owners, internal Part selection, hover/pick
caches, selection highlights and Structure Browser selection/current item. Cleanup
runs before replacing topology and again after restoration, since restore adapters
can otherwise select the restored operands/result. Toolbar, Edit and Ctrl+Z/Ctrl+Y
use the same actions and policy, also from an active floating parameter editor.
No selection or manipulator is restored by history;
select geometry again explicitly to start the next operation. Camera
orbit, pan, zoom and snapping preferences do not confirm a session.

From v0.3.3, [General Move Face](MOVE_FACE.md) also uses this lifecycle with a
geometry candidate and a separate AIS preview.
v0.3.4 adds [Rotate Face](ROTATE_FACE.md) and [Align Face](ALIGN_FACE.md): both
keep a source-face snapshot, render a separate candidate, and append one
`FaceChange` through the same chronological history on confirmation.

## Current architecture

[Delete Face](DELETE_FACE.md) extends this lifecycle with a parameter-free
`DeleteFace` kind. Immutable source/result snapshots and separate blue candidate
presentations are held per Part. Confirmation requires a valid changed candidate,
not a nonzero distance field; grouped snapshots still form one ordinary history
entry. Cancel, tool switches, window close and history navigation remove every
candidate presentation. Geometry failure signals share a MainWindow popup;
live numeric preview errors stay in the status bar until explicit confirmation.

`CadViewport` owns part slots (`CadPart`), OCCT presentations, picking, parameter
editors and the existing modeling history (`PullChange`, with grouped changes
and optional `BooleanChange`). `MainWindow` owns menus and the Structure Browser;
stable slot indices and selection signals keep browser rows synchronized.
There is still one document/history owner; this change introduces no second
undo manager.

The small nested `CommandSession` is a transaction record. Its lifecycle is:

1. **Selection**: capture operand IDs, explicit part selection and actual OCCT
   selection owners, including face owners.
2. **Parameters**: freeze the direction, rotation pivot and each original transform.
3. **Preview**: calculate candidate transforms from the original state, never
   cumulatively from the previous preview. Only the AIS presentation transforms
   and gizmo display are changed for Move/Rotate. Face edits show a separate
   candidate presentation and hides the original. Canonical shapes, bounds, primitive parameters,
   part transforms, browser selection and both history stacks remain unchanged.
   Move Face retains the original face owners for Cancel while its source
   presentation is hidden.
4. **Confirmed**: write candidate transforms or face geometry and append one ordinary
   `PullChange` (grouped for multiple parts, or carrying `FaceChange` geometry
   snapshots) to the existing history. Only a
   nonzero confirmed command clears Redo.
5. **Cancelled**: restore original presentation transforms and selection owners.
   Nothing needs to be undone in the document. Discard the session record.

The numeric editor and OK/Cancel buttons use this same transaction lifecycle.
During a session they occupy a small raster tool window positioned near the
gizmo, avoiding Qt control painting on the OCCT paint-on-screen surface.
Closing this tool window also cancels the session. Each Move/Rotate drag starts
a session; refine its preview with the numeric field before confirming. Move
Face starts a session at face selection and allows repeated arrow drags.
Invalid numeric input or invalid candidate geometry cannot confirm a command.
The adapters include rigid Move/Rotate, planar Move Face, edge/axis-driven
Rotate Face and planar-reference Align Face. Face rotation uses OCCT
`BRepOffsetAPI_DraftAngle`; Coplanar alignment composes that operation with
the existing `MoveFaceGeometry` offset. Both share its conservative validity,
volume, self-interference and analytic-support policy. Future geometry commands
can extend the candidate state using
`PullChange`'s existing shape/parameter snapshots and validate candidate B-Reps
before confirmation. Session keyboard handling is scoped to the viewport and
its parameter tool window, including when embedded in MainWindow.
Geometry previews should use separate temporary AIS shapes
and keep original face owners valid until commit; merely restoring a transform
is insufficient for a topology-changing command.

## Boolean adapter (v0.3.3)

Unite, Subtract and Intersect now use `CommandSession::Kind::Boolean` and the
same floating controls and lifecycle. The session holds explicit Target/Tools,
Keep Tools and a validated `BooleanChange` candidate. Operand picking is routed
before normal selection cancellation; Structure Browser selection updates Tools.
Preview is a separate non-selectable AIS overlay, with originals left visible
and canonical geometry untouched. Changing roles/tools/policy discards it.
OK commits one existing `BooleanChange`; Cancel discards it and restores the
original owners/part selection. No second undo stack is introduced. See
[Boolean operations](BOOLEAN_OPERATIONS.md) for replacement and Keep Tools policy.

## Central termination and Primitive Pull (v0.3.3)

Selection itself is passive; modifiers determine Face/Part picking without a
mode dropdown. Tool activations are mutually exclusive. Every confirmed or
cancelled session calls `resetModelingTools()` to remove all arrows/rings,
highlights, parameter controls and tool flags. No last-used command remains
armed. One-shot operations cancel old work first. Camera navigation never
confirms a session. Invalid input keeps an explicit editor with Cancel/Esc.

Primitive Pull / Resize now starts explicitly and uses `CommandSession::Kind::Pull`.
Its existing geometry calculation still updates the working B-Rep during preview;
the session captures exact `CadPart` snapshots (including shape, primitive
parameters, transforms and selection owners). Cancel restores those snapshots;
OK caches the final snapshots in existing `FaceChange` history entries, grouped
for multi-face/multi-part Box Pull. Release no longer pushes Undo. No separate
undo stack or duplicate geometry algorithm is used. Unlike Move/Rotate and Move
Face, this adapter temporarily updates canonical working geometry while the
session is active; it is restored exactly on cancellation. A future refactor
can isolate that legacy calculation into a purely immutable candidate builder.

Numeric primitive input retains final diameter/height semantics, including
localized values and the diameter symbol. Box and multi-face Pull retain signed
travel in mm. Snapping is unchanged. Zero-distance confirmation adds no history.

## Modal operations

- Numeric **Rotate Part** and **Move Part Along Face** menu dialogs retain their
  existing modal OK/Cancel behavior and immediate single history entry on OK.
  This version migrates the interactive transform paths first.

## Verification

```sh
cmake -S . -B build -DOPENDESIGNER_BUILD_TESTS=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
git diff --check
```

An X11/OpenGL display is required. `command-sessions` checks presentation-only
preview, canonical shape/metadata preservation, Cancel/OK, invalid input,
keyboard confirmation/cancellation, groups, arbitrary existing rotations and
translations, exact original selection owners, tool switches, and single-step
Undo/Redo. It also verifies that cancellation preserves an existing Redo entry.
The Boolean suite additionally checks unchanged browser rows/selection during
preview and cancellation, and verifies that a Boolean tool switch discards the
pending transform before operand evaluation.
The v0.3.1 `rotation-pivot` suite now compares the gizmo's preview transform before
confirmation and the committed result with the actual Rotate dialog. Existing
Pull, gizmo and Boolean/browser suites remain enabled. The `face-transform-geometry`
suite covers signed rotations, multiple axes, suitable GeneralBRep/Boolean
results and Coplanar/Parallel references. `selection-context` covers explicit
tool activation, preview cancellation, one-step history and reference-Part
preservation.

The user reports that v0.3.2 was manually tested and approved on Fedora KDE.

For repeatable Fedora KDE manual verification, repeat Move and Rotate on a Box, Cone and
Boolean result, both alone and in a mixed group. Release and change the exact
value several times before OK; compare with Cancel/Esc and a tool switch during
the drag and after release. Check that one Undo restores the entire confirmed
group and one Redo reapplies it. Repeat primitive Pull and Boolean operations to
check their existing confirmation behavior. Verify overlay placement and keyboard
focus with your display scaling; other operating systems are not yet manually
verified for this change.

## v0.3.5 shared reconstruction

Move/Rotate/Align Face prepare isolated B-Reps through the shared
[BRepReconstruction](BREP_RECONSTRUCTION.md) component. Redundant Boolean seams
can merge before intersection rebuilding; old face owners remain valid only on
the original preview snapshot and are discarded on confirmation. No session,
selection, manipulator or history behavior changes. The move-face suite now also
checks signed riser transactions that change topology, keeping stable Part and
browser identity across Cancel, OK and one cached Undo/Redo step.

### v0.3.5 visible face previews

Move Face, Rotate Face and Align Face share `presentFacePreview` and
`redrawFacePreview`. A validated candidate is tessellated on its isolated B-Rep;
every face must have triangles before the source presentation is hidden. The
new AIS object has the existing instance transform, explicit shaded mode,
25% transparency and a distinct blue preview color. Orange manipulators remain
unchanged. Display uses selection mode -1, so temporary solids cannot become
operands. The whole reconstructed solid is shown, not a moved face or transition
volume. No document geometry or Part color changes during preview.

Each input replaces and removes the previous AIS object. The retained viewer
scene is explicitly redisplayed, updated and invalidated before redraw; OK and
Cancel also refresh the scene after removing the candidate. Invalid geometry,
failed tessellation and incomplete numeric input remove the old candidate,
restore the original, disable OK and report an error. Corrected Rotate input can
be retried without choosing its axis again. Confirmation still records one
FaceChange; Cancel records none. All candidate and manipulator objects are
removed from the AIS context on termination.

The earlier face-preview path used the source material and implicit AIS meshing
and scene updates. Reconstructed deep copies have no inherited display mesh;
B-Rep validity and a non-null AIS handle did not establish visible shaded output.
The revised path makes that display contract explicit. The exact disappearing
preview reported on Fedora KDE was not reproduced in this test environment;
OCCT offscreen rendering already showed the former candidate, while native GLX
window grabs were black even for the ordinary viewport. Offscreen pixel tests
are not a native-screen proof. The user subsequently completed the Fedora KDE
manual retest successfully and confirmed all tested functionality, including
visible previews, cleanup and the Undo/Redo context menu (2026-10-20).

The move-face suite now renders Box and Unite-riser previews and compares images
with and without the candidate. It checks substantial blue shaded coverage,
triangulation of every face, original visibility, display mode, transparency,
replacement, invalid-input recovery and AIS object counts after Cancel/OK for
all three commands. Existing real drag, numeric, Enter/Esc and selection tests
remain enabled. Kernel shape/volume validation is unchanged.

## v0.4.0 construction adapter

`CommandSession::Kind::Construction` keeps typed reference values and a separate
analytic candidate/presentation. The nonmodal ConstructionPanel edits roles and
parameters, while viewport picking is routed to an explicitly armed operand.
Move/Rotate arrows/rings update the candidate from the original frame. Release
keeps the session open; OK creates one ConstructionChange in the existing
chronological stacks, Cancel creates none. Property and deletion edits use the
same history. History navigation clears construction/browser selection as well
as Parts and subshapes. Save excludes pending candidates and retains the session.
See [Construction geometry](CONSTRUCTION_GEOMETRY.md).

## v0.4.1 2D adapter

`CommandSession::Kind::Sketch` holds an immutable before/after 2D collection and
separate nonselectable blue presentations. Programmatic tools call
`previewSketchEntities`; interactive tools add a transient point/cursor state and
`SketchDrawingPanel`, using the same before/after transaction. OK/Enter confirms one `SketchChange` in the existing
history, while Cancel/Esc discards the candidate. Unchanged candidate confirmations do
not create history. Save uses only confirmed entity values. Construction edits
preview their Workplane contents using the candidate frame; Cancel restores the
original presentation. Construction deletion snapshots its owned 2D contents in
the same entry. No second undo manager is introduced. See
[Sketch Editor](SKETCH_EDITOR.md). During incomplete drawing, Enter accepts XY or
polar input according to the focused field; once complete it confirms. Back edits
the temporary points, and Cancel/Esc leaves no entity. Workplane/tool switches
cancel the transaction; camera navigation does not. Save retains the pending
command but serializes confirmed values only. New/Open also leave 2D editing.

Phase 3 adds an `overdraw` state to the same Sketch session: an evaluated candidate
graph, current endpoint, explicit hover alternatives and an ordered list of
chosen Profile values. Green hover and blue chosen overlays never enter document
selection or the persisted collection. OK commits all portions in one existing
`SketchChange`; Back/Backspace and New start only edit the temporary command.
An empty or invalid Overdraw cannot confirm. Cancel and central termination also
remove its panel, green overlay and endpoint markers. The original Construction
entities are never edited or referenced persistently by the resulting profiles.

Offset and common tangent lines use a `lineConstruction` state within that same
Sketch session. It holds only transient operand/solution picking and one candidate
Construction entity. Numeric edits, side changes and explicit tangent choices
update a blue preview; other tangent alternatives are green nonselectable
overlays. Armed Pick roles accept browser or analytically acquired viewport
references without ending the command. Missing finite intervals, ambiguous
solutions and failed calculations cannot confirm; obsolete previews are removed.
Enter interprets pending numeric fields before confirmation. OK adds one existing
`SketchChange`; Cancel and central cleanup remove the panel, alternatives and
markers. Save snapshots exclude pending results. No extra geometry store, history
manager or persistent source relation is introduced.
