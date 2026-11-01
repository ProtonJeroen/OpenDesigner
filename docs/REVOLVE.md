# Revolve — v0.4.8, phase 8D

Activate a visible, unlocked Workplane with **Edit Sketch**, then choose
**3D Modeling → Revolve**, also available in the 3D Modeling toolbar.
This phase supports **New Solid** only. It uses the existing
[Revolve geometry kernel](REVOLVE_GEOMETRY.md) unchanged.

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

**OK/Enter** creates one GeneralBRep Part with its own stable UUID, Structure Browser
entry and one Undo step. **Cancel/Escape**, including Escape during axis picking,
discards the entire command and retains Redo. Selection, Active Part and the source
editing Workplane are preserved on confirmation and cancellation; normal picking
and hover behavior resume after closing the command. Source
Sketches, Axes and Parts remain independent and unchanged. There is no persistent
feature dependency, temporary tool Part, new storage schema or Boolean operation.

Source model changes and command switches discard the session. Confirmation
rechecks source snapshots and the picked Part's identity, shape and placement or
Construction Axis snapshot. Save/Open stores confirmed B-Rep geometry and sources
through the existing native document paths, preserving the new Part UUID.

The existing Command Panel positioner, drag handle and overflow scrolling are
shared with Extrude. On a small HiDPI viewport, scroll to reach controls and buttons.

## Automated validation — 2026-11-01

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

## Manual Fedora KDE acceptance — pending

Manual acceptance follows this phase and is required before starting phase 8E.

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
7. At normal and 200% scaling, verify the 12-pixel viewport anchor, numeric focus,
   dragging, docking/resizing and scrolling to all controls and OK/Cancel. Verify
   ordinary Sketch/Part selection and hover still work after closing Revolve.
