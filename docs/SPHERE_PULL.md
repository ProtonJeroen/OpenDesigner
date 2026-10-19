# Sphere Pull (v0.20)

Create a sphere with **Modeling > Create Sphere**. Select its surface, then
explicitly choose **Primitive Pull / Resize** from its context or Modeling menu
to display the orange radial arrow. Selection alone is passive (v0.3.3).
Drag outwards to enlarge it or inwards to shrink it. The field continuously
shows the new **absolute diameter**, for example `Ø 100,00 mm` on a Dutch system.
After releasing, that diameter remains displayed. Type the desired final
diameter and press Enter: `100,00` sets a 100 mm diameter (50 mm radius),
regardless of the starting size or how far the arrow was dragged. Both a plain
number and the complete displayed `Ø … mm` text are accepted. Use the system
locale's decimal separator. OK/Enter confirms one Undo step and closes the tool; Cancel/Esc restores the
original and closes it. All arrows disappear after either outcome.

Cylinder side-wall Pull uses the same absolute diameter display and input.
Pulling either cylinder cap shows and accepts the **absolute final height**,
for example `95,00 mm`. The opposite cap stays fixed, including on transformed
and duplicated cylinders. A plain height or the displayed `… mm` text can be
submitted.

The diameter must remain greater than 0.02 mm and the cylinder height greater
than 0.01 mm. Face snapping applies to arrow travel; Shift bypasses snapping.
Exact typed dimensions are not snapped. Clicking and releasing the arrow
without moving also opens numeric entry for spheres and cylinders. All round
Pull dimensions use two decimals and the system locale, including the status
bar. Box Pull and transform inputs retain their existing meanings.

A sphere is rebuilt around its original local center. Its instance transform is
preserved, so moving, rotating or duplicating it does not change the meaning of
Pull. Sphere identity and radius are copied during duplication. Pull shares the
cylinder drag, dimension editor, B-Rep snapshot and undo/redo paths. Each drag
and its subsequent exact input form one undo step. Creating and duplicating
parts still use the existing behavior and are not undoable commands.

## Manual regression checks

1. Create a sphere, select it, drag its orange arrow out and in. Its center must
   stay still. Check that the diameter updates during dragging and remains
   unchanged on release. Enter `100,00` (Dutch locale): the result must be a
   100 mm diameter sphere. Enter a smaller diameter in a second operation.
   Check Undo Modeling and Redo Modeling.
2. Enter a zero or negative diameter. The last valid geometry must remain and
   the field must stay editable.
3. Move and rotate the sphere with the existing transform gizmos. Select its surface, explicitly start Resize and repeat Pull and undo/redo.
4. Duplicate the edited sphere with Ctrl+D, move the copy clear of the original
   and resize it. The original must remain unchanged.
5. Repeat with a sphere created after other parts, so its local center is away
   from the origin. Check picking after orbiting the camera and changing zoom.
6. On a cylinder, pull the side wall and enter the desired final diameter.
   Pull the bottom cap and enter the desired final height: its top must stay
   fixed. Check top-cap Pull too (bottom fixed), with exact input and undo/redo.
   Repeat after moving, rotating and duplicating the cylinder.

## Automated integration test

No new test libraries are required. The optional test uses the real Qt/OCCT
viewport and needs a working X11/OpenGL display (XWayland is supported):

```sh
cmake -S . -B build -DOPENDESIGNER_BUILD_TESTS=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

On a Linux CI host with Xvfb and software OpenGL installed, run the last command
under `xvfb-run -a`. The existing CI does not install or run this optional test.

The test covers surface picking and the center-click arrow fallback, outward
and inward mouse drags, absolute diameters and heights, two-decimal localized
text during dragging and after release, plain and decorated numeric input,
Dutch and English decimal separators, invalid dimensions, B-Rep validity and
volume, fixed world centers and opposite caps, transformed and duplicated
spheres/cylinders, repeated undo/redo, and cylinder bottom/top/radial Pull. Camera angles,
platforms and desktop configurations beyond the test setup need manual checks.
