# Cut Extrude (v0.4.5)

Activate a visible, unlocked Workplane with Edit Sketch, then choose **3D Modeling →
Cut Extrude**. Select one or more certified Profile regions in the panel and choose
one visible target Part from the list or viewport/Structure Browser. All regions
on the active Workplane are selected initially; holes belong to their enclosing
region and remain uncut.

Choose **Blind** and a positive exact depth in millimeters, or **Through All**.
The default direction follows the Workplane normal; **Reverse direction** cuts
against it. Through All starts at the Workplane and reaches beyond the furthest
projection of the transformed target bounds in that direction, with a tolerance
and size-based margin. It does not cut material behind the Workplane.

The blue, nonselectable preview shows the resulting solid while temporarily hiding
the original presentation. OK commits one operation; Cancel/Escape restores the
original display and selection without changing geometry, history or the document.
The target keeps its identity, name, color/material properties, placement and
Structure Browser slot. Undo/Redo and native Save/Open use the existing B-Rep paths.
A cut can leave multiple solids inside the same Part; it never creates another Part.

Cut uses the existing Profile Builder and prism engine followed by non-destructive
OCCT subtraction and solid validation. Unsupported/open/intersecting profiles are
refused without healing. Empty cuts, complete target removal, invalid results and
kernel failures leave the target unchanged. Existing coordinate/tolerance and
1e9 mm prism-distance certification limits still apply.

Automated coverage is included in `sketch-profile-extrusion` and
`profile-extrusion-sessions` (also run at scale factor 2). These check numerical
volume, closed/valid topology, holes, multiple regions, both directions, transformed
targets, safe refusal, preview lifecycle, identity, history and native persistence.
Manual Fedora KDE interaction remains a desktop acceptance check.

Validation on 2026-10-30: CMake configure and full build succeeded with tests
enabled. All three targeted extrusion CTests passed, followed by the complete
60/60 CTest suite (503.58 seconds). Qt/OpenGL session tests required desktop
access outside the filesystem sandbox; sandboxed startup aborted without
application diagnostics. Normal and HiDPI panel/session tests passed with desktop
access. Interactive manual workflow acceptance is separate from these tests.
