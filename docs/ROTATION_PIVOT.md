# Consistent rotation pivot (v0.3.1)

Both Rotate Part and the interactive rotation gizmo use the Open CASCADE
volume centroid of the current canonical B-Rep, including its shape location,
followed by the complete instance transform exactly once. Multiple selected
parts rotate together about the unweighted average of their world centroids.
Shapes without usable volume properties retain the existing bounds fallback.
Pull continues to use its separate primitive reference centers and end anchors.

The controls retain their existing axis conventions: the gizmo follows the
first selected part's local orientation; Rotate Part offers world X/Y/Z.
For an already rotated part, choose the same physical world axis when comparing
the two commands. An equal axis label alone does not imply equal axes.
The menu accepts a selected face, a part, or a group of parts; a group rotation
is one Undo Modeling step. The angle dialog and gizmo exact-angle editor retain
the existing controls and units.

For interactive preview and confirmation behavior from v0.3.2 onward, see
[Command sessions](COMMAND_SESSIONS.md). The pivot and axis conventions above remain in effect.

## Automated verification

```sh
cmake -S . -B build -DOPENDESIGNER_BUILD_TESTS=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

An X11/OpenGL display is required. `rotation-pivot` checks Box, Cylinder, Sphere,
Cone, a located non-primitive GeneralBRep, a Boolean Subtract result and a mixed
group, with and without existing rotation/translation. It compares complete
transforms with an independently calculated centroid/rotation, exercises gizmo
angle application, release and exact entry, automates the real Rotate dialog,
and checks Undo/Redo for both paths. Pointer hit-testing and glyph placement
remain covered by `gizmo-center` and `cone-pull`. Existing Pull and Boolean suites
also cover Move, selection and Structure Browser synchronization.

## Fedora KDE manual checks

The user reports that the v0.3.1 manual tests passed on Fedora KDE and that
part and group rotations work correctly (2026-10-19).

- Create each primitive, especially an asymmetric Cone, then rotate via the
  gizmo and undo. Use Rotate Part with the same physical axis and angle; compare.
- Move and rotate the part first, repeat on a Boolean result and a mixed group.
  Check Undo/Redo, selection highlight and Structure Browser rows throughout.
- Pull both ends and diameters of Cylinder/Cone; verify the opposite end stays
  fixed. Repeat after rotation and on a duplicate. Check Box and Sphere Pull.
- Verify Move, Alt/Ctrl+Alt selection, browser selection and show/hide as before.

The historical [SolidDesigner 16 workflow analysis](SOLIDDESIGNER16_WORKFLOWS.md)
describes the earlier mismatch. It is retained as research context; this change
resolves that pivot mismatch without introducing reference picking or face rotation.
No proprietary reference files are included.
