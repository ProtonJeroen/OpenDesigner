# v0.3 — Boolean Operations

## Workflow

Select two or more parts with Alt+click in the viewport (Ctrl+Alt+click to add
parts), Ctrl+click faces, or multi-select rows in the Structure Browser. Selected
faces identify their owning parts; selecting several faces on one part still
counts as one operand. Hidden browser-only selections remain excluded from the
viewport operand set: show those parts first to include them in an interactive
Boolean.

Choose **Modeling > Boolean**:

- **Unite** combines all selected solids using `BRepAlgoAPI_Fuse`.
- **Subtract...** opens a dialog. Explicitly choose one selected part as the
  **Target** to keep; the dialog lists all other selected parts as the **Tools**.
  Select the intended target and tools before opening the dialog. OK is disabled
  until a target is chosen. Cancel leaves the model unchanged. The tools are
  subtracted sequentially using `BRepAlgoAPI_Cut`.
- **Intersect** retains the volume common to every selected solid using
  `BRepAlgoAPI_Common`.

The operands can be Box, Cylinder, Sphere, Cone or existing GeneralBRep parts,
including earlier Boolean results and parts containing multiple solids. Complete
instance transforms are applied before the operation. The result is one new
GeneralBRep part in world coordinates, with an identity instance transform,
selected automatically. Its initial color comes from the first operand, or the
explicit target for Subtract. Source rows disappear from the browser only after
the operation succeeds.

Move/Rotate gizmos work on the result and use its B-Rep volume centroid. Use the
Structure Browser to rename it, hide/show it, or choose **Color...** from its
context menu. Ctrl+D duplicates the result, preserving geometry, transform and
color. Boolean results can be operands in further Booleans. General face-offset
Pull is not implemented for arbitrary Boolean B-Reps; Box and primitive-specific
Pull commands are not applied to those results.

## Empty results and errors

Every operand and the final result must be a valid OCCT solid with positive
volume. OCCT failures, invalid inputs, fewer than two distinct parts, an absent
Subtract target, empty cuts and intersections, and contacts with no common solid
volume are rejected. The status bar explains the failure and states that the
original parts were kept. An active manipulator drag must be finished first.

Disjoint Unite is supported: one result part can contain several disconnected
solids. The success message reports its solid count. Intersect of disjoint parts
fails without replacing anything. Subtract that removes no target volume is
reported as a no-op and preserves all sources. If some tools intersect and
others do not, a successful Subtract consumes all selected source operands.
All computations use OCCT's standard tolerances; extremely small/sliver or
invalid geometry can be rejected rather than installed.

## Undo, Redo and document identity

A Boolean needs more state than an ordinary Pull dimension. A dedicated
`BooleanChange` stores source part snapshots, the cached result, and previous
part selection. It is referenced by an entry in the existing chronological
modeling undo/redo stack. Pull, transformations and Booleans therefore undo in
the order in which they were committed.

Undo restores the source B-Reps, transforms, primitive parameters, names, colors,
visibility and part selection where possible. Face selections are restored as
whole-part selections; hidden sources remain hidden and are not added to 3D
selection. Redo reapplies the cached result without recomputing the kernel
operation. Unrelated parts created later retain their IDs and state.

Part vector positions are stable IDs for the current application session.
Consumed sources and undone results become inactive slots; their scene objects
are removed and their browser rows hidden. Slots are never erased or renumbered.
`partsChanged` refreshes the browser before selection is synchronized. Visibility,
selection, duplication, transforms and picking exclude inactive parts. This
prevents browser/selection references from addressing the wrong part after
Undo/Redo or chained operations. Snapshots and inactive slots retain geometry in
memory; history compaction and persistent document IDs are future work.

The kernel uses copied world-space shapes and non-destructive algorithms. It
finishes and validates before modifying the document. A Boolean result has no
Box/Cylinder/Sphere/Cone editing flags. Colors and names now belong to CadPart,
so they can be copied and restored together with geometry.

Existing limitations remain: creation, duplication, rename, color and visibility
changes do not independently create undo commands. Boolean Undo still restores
the source states captured at that operation. There is no persistence or
asynchronous/cancellable Boolean processing yet; large operations run on the UI
thread. Windows and macOS have not been tested for this implementation.

## Build and automated verification

The existing OCCT dependencies provide TKBO and TKBool, now linked explicitly.
No additional third-party libraries were added.

```sh
cmake -S . -B build -DOPENDESIGNER_BUILD_TESTS=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

The optional viewport tests require X11/OpenGL (XWayland is supported). A CI host
with Xvfb and software OpenGL can use `xvfb-run -a ctest --test-dir build
--output-on-failure`. The current CI does not run these optional tests.

`boolean-operations` exercises all three operations and exact analytic volumes,
two and three operands, multiple cutting tools, all existing primitives,
GeneralBRep result reuse, transformed operands, cached Redo, mixed transform and
Boolean history, hidden source restoration, chained operations, selection from
browser rows and face owners, explicit target selection/cancel, result color,
visibility and duplication, stable browser IDs, invalid inputs and empty/no-op
results. The three existing Pull/Cone/gizmo tests remain enabled.

## Manual smoke test

1. Create a Box and duplicate it with Ctrl+D. The default 25 mm offset gives an
   overlap. Multi-select the pair in the browser and choose Unite. Check that
   only the result row remains. Undo and Redo, then Undo again.
2. Choose Subtract. Verify OK is initially disabled, choose either part as the
   target, inspect the tools list and confirm. Undo and choose the other target:
   the opposite end should remain.
3. Undo, select both parts and choose Intersect. Only the overlapping portion
   should remain. Verify undo/redo, browser selection and visibility.
4. Repeat with moved/rotated primitives and with an earlier Boolean result.
   Move, rotate, rename, recolor, hide/show and duplicate a result.
5. Try disjoint parts, touching-only parts, a complete subtraction and an
   insufficient selection. Errors must preserve sources and the undo history;
   disjoint Unite should produce one part containing disconnected solids.
