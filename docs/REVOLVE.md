# Revolve — v0.4.8, phases 8D–8E

Activate a visible, unlocked Workplane with **Edit Sketch**, then choose
**3D Modeling → Revolve**, also available in the 3D Modeling toolbar.
Choose **New Solid**, **Add Material** or **Remove Material** in the Operation
selector. All modes use the existing [Revolve geometry kernel](REVOLVE_GEOMETRY.md)
unchanged. New Solid remains the default.

Select one or more certified **Profile regions** in the Command Panel; all are
selected initially. Holes belong to their enclosing region. Construction entities
do not close profiles. Profile analysis follows the same all-or-nothing policy as
Extrude: invalid or unsupported profiles on the source Workplane must be corrected
before starting the command. Selecting a region does not edit source geometry.

Choose the **Rotation axis** from the dropdown, or use **Pick rotation axis** and
click geometry in the viewport. Supported references are:

- Visible, unlocked straight Sketch segments or Infinite Lines on the active
  Workplane, including Construction lines. The Structure Browser can also supply
  a Sketch line while axis picking is active.
- Visible, unlocked Construction Axes, via dropdown, viewport or Structure Browser.
- Straight edges of visible Parts, picked directly in the viewport. The existing
  construction-reference resolver applies the Part instance transform once.

Curves and non-linear geometry are refused. The selected axis must lie in the
profile plane, and the selected regions must occupy the same side of the axis.
The kernel also rejects crossing-axis, pinched and self-intersecting revolutions.
Part-edge axis direction follows the oriented edge reference; Reverse direction
lets the user switch the result without changing that reference. No axis geometry
is created or changed by picking.

Enter an exact signed **Angle (degrees)** with `1 <= abs(angle) <= 360`, including
fractional degrees. Positive follows the axis right-hand rule; **Reverse direction**
negates the entered value, including an already-negative angle. The default is 360°.
Source-region, axis and angle changes rebuild the actual blue 3D preview from the
original source snapshot. Invalid input clears preview and disables OK. The preview
is not selectable, does not mark the document modified and is never saved.

**OK/Enter** creates one GeneralBRep Part in New Solid, or replaces the target
geometry in place in Add/Remove, as one Undo step. **Cancel/Escape**, including
Escape during axis picking, discards the entire command and retains Redo. Escape
while target picking first ends that picker, matching Extrude; panel Cancel always
cancels the entire command. Selection, Active Part and the source
editing Workplane are preserved on confirmation and cancellation; normal picking
and hover behavior resume after closing the command. Source
Sketches and Axes remain independent and unchanged; material modes modify only the
chosen target. There is no persistent feature dependency, temporary tool Part or
new storage schema.

Source model changes and command switches discard the session. Confirmation
rechecks source snapshots and the picked Part's identity, shape and placement or
Construction Axis snapshot. Save/Open stores confirmed B-Rep geometry and sources
through the existing native document paths, preserving the new Part UUID.

The existing Command Panel positioner, drag handle and overflow scrolling are
shared with Extrude. On a small HiDPI viewport, scroll to reach controls and buttons.

## Add / Remove Material

Entering Add or Remove resolves an eligible **Active Part** automatically. Without
one, or if it is hidden/ineligible, click visible Part geometry at the target
prompt. **Pick target part**, the target list and Structure Browser allow explicit
overrides without changing Active Part. Each operation-mode switch resolves Active
Part afresh; an override applies to the current mode. New Solid clears its target
and stops target picking. Axis, signed angle, Reverse and selected regions retain
their values across operation switches; there is no Revolve Blind/Through All mode.

Axis picking and target picking use distinct selection roles. If a target is chosen
before an axis, the command resumes axis picking. **Pick rotation axis** ends target
picking and restores the original target display before requesting the new axis.
Choose a target again with **Pick target part** if one is still missing.

The blue preview is the actual fused or cut target, not the swept tool. Remove
hides the original target presentation while its valid preview is displayed; invalid
input, changing mode/target and cancellation restore the original display. All
parameters rebuild from original source snapshots. Targets are checked by UUID,
slot, visibility, shape and placement before confirmation.

Material application reuses `SketchProfileExtrusion::addMaterial` and the subtraction
stage extracted from Cut Extrude as `SketchProfileExtrusion::removeMaterial`.
Cut Extrude delegates its certified prism to the same removal function. Both use
existing `BooleanGeometry` non-destructive operations, solid extraction,
normalization and certification; there is no second Boolean engine. Target
placement is applied once in world space, with one inverse transform for local
Part storage. `FaceChange` stores complete before/after target snapshots, retaining
UUID, name, material/presentation, transform, visibility and browser slot. Other
Parts and source Sketch/Axis geometry are unchanged. Native Save/Open uses the
existing GeneralBRep path.

Add requires one connected target and a connected result; disconnected and unchanged
additions fail. Remove permits multiple surviving solids in the same Part but
refuses no measurable removal and complete target removal. The kernel's coplanar,
same-side axis restrictions remain. Only one target is supported. Ordinary source
selection/highlights are retained; local face/edge owners on a modified target are
discarded after confirmation because their topology has been replaced. An existing
whole-Part selection follows the same target identity.

## Phase 8D automated validation — 2026-11-01

Incremental builds of only `OpenDesigner` and `OpenDesignerSelectionContextTests`
succeeded in the existing Release `build` directory. Final targeted CTests passed
4/4 in 31.54 seconds with desktop access:

- `profile-revolution-sessions`
- `profile-revolution-sessions-hidpi`
- `profile-extrusion-sessions`
- `profile-extrusion-sessions-hidpi`

The new session test covers viewport and dropdown/Browser axis references,
transformed Part edges, region/angle/reverse updates, numerical preview volume,
invalid angles/profiles/axes, stale Part references, nonselectable preview, no
document mutation before confirmation, OK/Enter and Cancel/Escape, Active Part,
native Sketch and local-face selection, restored hover, one-step Undo/Redo, UUID,
Structure Browser, native persistence, command switching and source invalidation.
Normal and HiDPI checks exercise the native viewport anchor and overflow scrolling
without horizontal clipping. Initial runs exposed loss of native Sketch highlight
when selected owners were deactivated; selected Sketch mode 0 now remains active,
and shared selection restoration uses current global owners. No display styles or
geometry checks were changed. `git diff --check` passed. No full suite or stress
tests were run; this does not constitute manual desktop acceptance.

Modified files for phase 8D:

- `src/CadViewport.h`, `src/CadViewport.cpp`, `src/MainWindow.h`, `src/MainWindow.cpp`
- `src/SketchProfileExtrusionTools.cpp`, new `src/SketchProfileRevolutionTools.cpp`
  and `src/ProfileSweepSelection.h`
- `src/SketchDrawing.cpp`, `src/SketchTools.cpp`, `src/ConstructionTools.cpp`
- `CMakeLists.txt`, `tests/SelectionContextTest.cpp`, `tests/ProfileExtrusionSessionTest.cpp`
  and new `tests/ProfileRevolutionSessionTest.cpp`
- This document, `docs/REVOLVE_GEOMETRY.md` and `docs/SHARED_MODELING_ARCHITECTURE.md`

## Phase 8E automated validation — 2026-11-01

Only `OpenDesigner`, `OpenDesignerSelectionContextTests`,
`OpenDesignerProfileRevolutionTests` and `OpenDesignerProfileExtrusionTests` were
built incrementally in the existing Release build directory. All six targeted
tests passed, with no failures:

- `sketch-profile-revolution`, `sketch-profile-extrusion`: 2/2, 2.32 seconds.
- Normal/HiDPI `profile-revolution-sessions` and `profile-extrusion-sessions`:
  4/4, 41.37 seconds with desktop access.

Geometry coverage adds full/partial signed Revolve tool Fuse/Cut, transformed
targets, disconnected/unchanged operations, total removal, split results, invalid
tools/targets/placement and source immutability. Material session coverage adds
Active Part and explicit viewport overrides, target-before-axis picking, axis
repicking after a cut preview, angle/reverse updates, all three mode switches,
stale target placement, untouched canonical geometry before confirmation,
Cancel/Escape and display restoration, native selection preservation, discarded
stale target face owners, UUID/name/color/visibility/placement/browser slot,
one-step Undo/Redo, Redo preservation and native roundtrip. The existing phase 8D
New Solid checks remain unchanged and pass at both scales.

Phase 8E changed `src/SketchProfileExtrusion.h`, `src/SketchProfileExtrusion.cpp`,
`src/SketchProfileExtrusionTools.cpp`, `src/SketchProfileRevolutionTools.cpp`,
`tests/SketchProfileRevolutionTest.cpp`, `tests/ProfileRevolutionSessionTest.cpp`,
`CMakeLists.txt` (link existing reconstruction code into the Revolve geometry test)
and the three Revolve/architecture documents. `git diff --check` passed.
No full CTest suite, unrelated tests or stress tests were run. Interactive Fedora
KDE acceptance remains separate and pending.

## Manual Fedora KDE acceptance — pending

Manual Fedora KDE acceptance remains pending; automated phase 8E checks do not
constitute interactive desktop acceptance.

1. Create a closed profile beside a Construction line or Infinite Line. Start
   Revolve from menu and toolbar; pick the axis, change selected regions and check
   the full preview. Also use a profile boundary line as a supported axis.
2. Check signed 1°, fractional, 90°, 180° and 360° angles and Reverse direction.
   Try holes and disjoint regions without crossing the axis; confirm partial caps
   and full closure visually.
3. Pick a Construction Axis and a straight edge of a moved/rotated Part. Check
   world placement, viewport hover and explicit axis changes via Pick rotation axis.
4. Try zero/out-of-range/non-numeric angles, curved edges, off-plane/crossing axes
   and an empty region selection. Verify the preview clears, OK is disabled and
   canonical geometry stays unchanged. Correct input and recover a valid preview.
5. Check OK/Enter versus panel Cancel/Escape while picking and while previewing.
   Verify original selection/highlights and Active Part survive; one Undo/Redo
   removes/restores the created Part without consuming source geometry.
6. Save/Open a confirmed result with its sources. Switch commands or open a file
   during preview and confirm no temporary geometry or panel remains.
7. In Add/Remove, check eligible/hidden/absent Active Part, direct target picking,
   explicit override and axis repicking. Verify that the preview is the final
   target, Cancel restores it, one Undo/Redo preserves properties/identity, and
   Save/Open restores both Boolean results. Try disconnected Add, unchanged
   operations and total removal; verify safe refusal.
8. At normal and 200% scaling, verify the 12-pixel viewport anchor, numeric focus,
   dragging, docking/resizing and scrolling to all controls and OK/Cancel. Verify
   ordinary Sketch/Part selection and hover still work after closing Revolve.
