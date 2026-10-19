# Create Cone and Pull (v0.20)

Choose **Modeling > Create Cone** to create a closed Open CASCADE B-Rep with:

| Dimension | Default |
| --- | --- |
| Bottom diameter | 80 mm |
| Top diameter | 40 mm |
| Height | 80 mm |

## Editing

Disable Interactive Move Part, Direct Move Part and Interactive Rotate Part
(including Combined Transform Gizmo), then click any cone face. Four orange
arrows appear:

- The radial arrow at the lower end changes the bottom diameter.
- The radial arrow at the upper end changes the top diameter.
- The axial arrow below the cone changes height while keeping the upper end fixed.
- The axial arrow above the cone changes height while keeping the lower end fixed.

Drag an arrow, then release and type the desired **absolute final dimension**.
Diameters display as `Ø 80,00 mm` and heights as `80,00 mm` on a Dutch system.
Both the complete displayed dimension and a plain number are accepted. Display
and input use the system locale and two decimal places. For example, entering
`100,50` on a diameter arrow sets a 100.50 mm diameter, not a diameter change.
The displayed dimension updates during dragging and is retained on release.
Clicking and releasing an arrow without moving also opens exact input. Esc
keeps the dragged result. Existing face snapping applies to arrow travel;
Shift bypasses snapping, and typed final dimensions are not snapped.

Set the top diameter to **0 mm** for a pointed cone. Its top-diameter arrow
remains available at the apex; use it to return to a truncated cone. Equal
diameters and a top diameter larger than the bottom diameter are supported.
Diameter changes preserve the end centers and height. Height changes preserve
the opposite end plane, both diameters and the instance transform.

Use the existing part selection, Structure Browser, transform gizmos and Ctrl+D
for moving, rotating and duplicating cones. After transforming a cone, disable
transform modes and click its surface to return to Pull. A copied cone retains
its dimensions and is independently editable.

## Implementation and limits

Cones are `GeneralBRep` parts with bottom radius, top radius, height and local
base-offset parameters. Geometry is rebuilt in the original local coordinate
system; the part's instance transform is preserved. Ordinary forms use
`BRepPrimAPI_MakeCone`; equal diameters use `BRepPrimAPI_MakeCylinder` while
retaining the editable cone parameters. No new dependencies are needed.

The existing Cylinder/Sphere Pull drag, dimension editor and undo/redo stacks
are shared. Cone snapshots include the B-Rep, dimensions, bounds, base offset
and instance transform. A drag and its exact input form one undo step. Creating
and duplicating parts retain the existing application behavior: these actions
are not undoable. Box-only face commands reject non-box parts; use the orange
Pull arrows or transform gizmos for cones.

The bottom diameter must exceed 0.02 mm, the top diameter must be nonnegative,
and the height must exceed 0.01 mm. Invalid input preserves the last valid shape
and leaves exact input open. The existing numeric editor supports final input
up to 100000 mm. When an axial arrow points directly toward the camera, orbit
the view to make its projected length draggable. Nearby arrows can overlap in
projection on very short cones; orbit or zoom to distinguish them.

## Transform gizmo center

Move/Rotate gizmos use `BRepGProp::VolumeProperties` on the current canonical
B-Rep. Its local center of mass is transformed once by the instance transform.
The same center drives displayed arrows and rotation arcs, hit testing and the
actual rotation pivot. It is recomputed from current geometry after Pull,
including geometry undo/redo, and applies independently to duplicates.

For a frustum with bottom radius R, top radius r and height h, the distance from
the lower plane to the centroid is:

`h * (R² + 2 R r + 3 r²) / (4 * (R² + R r + r²))`

The default 80/40/80 mm cone therefore has its centroid 31.43 mm above its lower
plane, rather than the 40 mm midpoint of its bounds. A pointed cone uses h/4;
equal radii use h/2. Current Box, Cylinder and Sphere B-Reps use their symmetry
centers. A cylinder's center follows height edits at either cap.

Multipart selection retains the existing unweighted average of selected part
centers, now using their live world-space centroids. It does not use a
volume-weighted group centroid. Shapes without usable volume properties retain
the previous bounds-based placement. Pull retains its original local reference
center and cap/radius anchors, separately from the live transform center.

## Verification

```sh
cmake -S . -B build -DOPENDESIGNER_BUILD_TESTS=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

The tests need an X11/OpenGL display (XWayland is supported). On a CI host with
Xvfb and software OpenGL installed, use `xvfb-run -a ctest --test-dir build
--output-on-failure`. The existing CI does not run these optional tests.

`cone-pull` checks defaults, B-Rep validity and volume, surface and arrow picking,
live absolute diameter/height display and retention on release, exact localized
input, a zero top diameter and recovery, equal and inverted diameters, invalid
input, fixed opposite ends, move/rotation gizmos and undo/redo, duplication,
transformed-copy Pull and a Box Pull regression. `primitive-pull` retains the
Cylinder/Sphere checks. `gizmo-center` independently checks analytical cone
centroids against the computed centers and the actual move/rotation glyph
geometry. It covers pointed/truncated cones, both diameters and height Pull,
transformed duplicates, rotation about the centroid, geometry undo/redo with
gizmos active, symmetry centers for Box/Cylinder/Sphere and the unchanged
multipart pivot policy with grouped rotation undo/redo. Windows, macOS and
other graphics configurations still need manual testing.

For a manual smoke test, create a cone and exercise all four arrows. Change the
top diameter to zero and back; try equal diameters; move, rotate and duplicate
the cone and repeat Pull on the copy. Check Undo Modeling and Redo Modeling at
each stage, and verify the source stays unchanged when editing its copy.
