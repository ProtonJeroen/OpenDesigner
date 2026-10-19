# v0.3 — Boolean Operations

## Workflow

Select one visible Part as **Target**, then right-click **Boolean > Unite,
Subtract or Intersect**. The Modeling menu starts the same command session;
without preselection, select a Target after starting it. With multiple selected
parts, the first resolved operand becomes the initial Target and the others
become Tools. Always inspect the displayed roles; **Choose Target** lets you
change the Target explicitly.

The floating command panel displays the Target and each Tool with name and
stable part number. After choosing a Target, click visible parts in the viewport
to toggle them in the Tools list. Structure Browser multi-selection replaces the
Tools list, excluding the Target; Ctrl selects/removes browser rows. The panel's
**Remove selected Tools** button also removes tools without leaving the session.
The Target cannot simultaneously be a Tool. Hidden or inactive parts are never
accepted by the interactive operand selector. Operand picking during this session is Parts; ordinary operand clicks do not
terminate it. After OK/Cancel, normal Face/Alt-Part picking resumes.

**Keep Tools** is off by default. **Preview** computes a temporary orange,
transparent result over the original operands. Original parts remain visible and
pickable, so tools can still be added/removed; changing operands or Keep Tools
invalidates the previous preview. **OK/Enter** computes and validates the current
candidate and commits exactly one history entry. **Cancel/Esc**, closing the panel,
selecting a different modeling tool discards the session without
changing document geometry or creating history. Esc/Enter also work while the
Structure Browser has focus. Computation is synchronous on the UI thread.

- **Unite** combines Target and Tools using `BRepAlgoAPI_Fuse`.
- **Subtract** computes **Target minus Tools**, sequentially using `BRepAlgoAPI_Cut`.
- **Intersect** retains their common volume using `BRepAlgoAPI_Common`.

The operands can be Box, Cylinder, Sphere, Cone or GeneralBRep parts, including
earlier Boolean results and parts containing multiple solids. Complete instance
transforms are applied before the operation. Preview, interactive confirmation
and the direct geometry API share one kernel calculation and validation path.

### Result and Keep Tools policy

The result is one new GeneralBRep part in world coordinates with identity
instance transform, a generated operation name and the **Target's color**. It is
selected after confirmation. Stable slots are never renumbered:

- **Keep Tools off:** Target and all Tools become inactive; one new result slot
  replaces the operands, retaining the existing replacement semantics.
- **Keep Tools on:** only the Target becomes inactive; one new result slot
  replaces it. Every Tool retains its original slot/identity, B-Rep, transform,
  name, color, visibility and Structure Browser reference. The Target's old ID
  is retained as an inactive history source, rather than reassigned to new topology.

Undo restores consumed source snapshots and previous part selection. Redo uses
the cached result and the same result ID. Kept Tools are never removed or
restored by this command. Preview and cancellation create no rows or part slots,
and do not clear an existing Redo stack.

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
browser rows and face owners, explicit Target/Tools sessions, Keep Tools on/off, preview/cancel, result color,
visibility and duplication, stable browser IDs, invalid inputs and empty/no-op
results. The three existing Pull/Cone/gizmo tests remain enabled.

## Manual smoke test

1. Create a Box and duplicate it with Ctrl+D. Select only the original; start
   Subtract from its viewport context menu. Verify the Target label and empty
   Tools list. Click the duplicate, Preview and Cancel/Esc. Geometry, metadata,
   browser references and history must be unchanged.
2. Repeat; add/remove Tools with viewport clicks, browser Ctrl-selection and the
   list's Remove button. Change Target. Verify Target minus Tools and inspect
   the preview before OK. Undo and Redo should each take one step.
3. Repeat all three operations with Keep Tools off and on. With Keep Tools on,
   only Target is replaced; verify tool names, colors, visibility, transforms and
   browser IDs through Undo/Redo.
4. Start from the Modeling menu with no selection. Select Target, then Tools.
   Test Enter/Esc with focus in the viewport, command panel and Structure Browser.
5. Repeat using transformed primitives, GeneralBRep and an earlier Boolean result.
   Try disjoint Intersect, complete Subtract, hidden parts and no-op Subtract.
   Invalid previews must leave original geometry and history unchanged.
6. Start Move or Rotate, preview a change, then start Boolean. The transform must
   cancel first. Cancel Boolean and verify the original selection and geometry.
