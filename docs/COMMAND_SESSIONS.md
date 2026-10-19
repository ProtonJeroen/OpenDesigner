# Command sessions and preview (v0.3.2)

Interactive Move Part (axis gizmo and orange face-normal arrow) and Rotate Part
now have a temporary preview. Drag an existing gizmo, release, and optionally
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
cancel a pending preview before operating on the existing history. Camera
orbit, pan, zoom and snapping preferences do not confirm a session.

## Current architecture

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
   and gizmo display are changed. Canonical shapes, bounds, primitive parameters,
   part transforms, selection and both history stacks remain unchanged.
4. **Confirmed**: write candidate instance transforms and append one ordinary
   `PullChange` (grouped for multiple parts) to the existing history. Only a
   nonzero confirmed command clears Redo.
5. **Cancelled**: restore original presentation transforms and selection owners.
   Nothing needs to be undone in the document. Discard the session record.

The numeric editor and OK/Cancel buttons use this same transaction lifecycle.
During a session they occupy a small raster tool window positioned near the
gizmo, avoiding Qt control painting on the OCCT paint-on-screen surface.
Closing this tool window also cancels the session. Each drag starts a session;
refine its preview with the numeric field before confirming.
Invalid numeric input cannot confirm a command. The current adapters are rigid
Move and Rotate; future geometry commands can extend the candidate state using
`PullChange`'s existing shape/parameter snapshots and validate candidate B-Reps
before confirmation. Geometry previews should use separate temporary AIS shapes
and keep original face owners valid until commit; merely restoring a transform
is insufficient for a topology-changing command.

## Operations not migrated yet

- **Primitive Pull and Box face Pull** keep their previous behavior: dragging
  changes the actual geometry; release records a history entry. Exact entry
  adjusts that entry. Esc after release closes the editor and keeps the result.
  A future Pull adapter must keep candidate shapes and primitive parameters
  separate until OK, with stable original geometry/selection for cancellation.
- **Booleans** keep their previous behavior: Unite/Intersect execute immediately;
  Subtract asks for a target and executes when its dialog is accepted. Results
  are validated before transactional replacement and cached for Redo. A future
  Boolean adapter can preview the validated result and operand visibility, then
  create the existing `BooleanChange` only on OK. Tool switching first discards
  any outstanding Move/Rotate preview.
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
Pull, gizmo and Boolean/browser suites remain enabled.

The user reports that v0.3.2 was manually tested and approved on Fedora KDE.

For repeatable Fedora KDE manual verification, repeat Move and Rotate on a Box, Cone and
Boolean result, both alone and in a mixed group. Release and change the exact
value several times before OK; compare with Cancel/Esc and a tool switch during
the drag and after release. Check that one Undo restores the entire confirmed
group and one Redo reapplies it. Repeat primitive Pull and Boolean operations to
check their existing confirmation behavior. Verify overlay placement and keyboard
focus with your display scaling; other operating systems are not yet manually
verified for this change.
