# Workplanes, Axes and Points (v0.4.0)

Construction geometry is independent direct-modeling document data. Creating an
object evaluates references once, in world coordinates. Only the resulting
analytic frame is retained. Moving, editing or deleting a source Part, Face,
Edge, Vertex or construction object never recomputes the result. There is no
persistent feature history or dependency graph.

## Workflow

Use **Modeling → Construction Geometry → Workplane / Axis / Point**. All creation
methods and Workplane/Axis Move/Rotate are available there. Context menus offer
applicable construction operations on planar/cylindrical Faces and on existing
construction objects. Points also have Edit Coordinates.

A nonmodal parameter panel lists explicit operand roles. Select a named object
or Part subshape in a role's list, or press its **Pick** button and select in the
viewport. Construction rows in the Structure Browser can supply an armed role.
Invalid input leaves the panel open, removes any obsolete preview and explains
why OK is unavailable. An invalid reference pick keeps the command open.
Preselected compatible references fill roles without using the same selected
subshape for two roles. The role fields remain editable.

Blue previews are temporary and non-selectable. **OK/Enter** confirms one shared
Undo step; **Cancel/Esc**, window close or a different tool discards the preview.
Ordinary new viewport selection cancels an unfinished command unless an operand
role or manipulator is being used. Camera orbit/pan/zoom keeps the command open.
Moving and rotating Workplanes/Axes provide orange XYZ arrows/rings and exact
numeric values. Release ends the drag, not the command. Shift bypasses existing
linear/angle snapping. Transformations are calculated from the original frame,
not accumulated over earlier previews. Rotation is around the object's origin
and a chosen world direction; drag a ring to choose a world X/Y/Z direction.

The browser has a Construction Geometry group separate from the existing Part
rows. New objects use independent name sequences **WP1, WP2, …**, **AX1, AX2, …**
and **PT1, PT2, …**. Cancel does not consume a number. Confirmed creation does;
Undo or deletion does not reuse it. The counters survive Save/Open and reset on
New. Existing/custom names remain unchanged. Objects have UUIDs, names, visibility
and a selection lock. Names are not
identifiers and need not be globally unique. Hidden or locked objects remain
manageable in the browser but cannot supply geometry operands. Context menus
provide Show/Hide, Lock/Unlock, Fit Selected and Delete Construction. Rename,
visibility, selection lock, creation, deletion and geometry edits are undoable.

Normal Face/Ctrl and Alt-Part/Ctrl+Alt selection is retained. Clicking a
construction object selects that typed set, with Ctrl for additions/removals.
Workplanes are picked on their boundary/axis edges so a transparent plane does
not take every Face click inside its rectangle. Alt-Part picking ignores
construction presentations. Passive selection does not activate manipulators.
Undo/Redo clears all Part/subshape/construction/browser selection and tools.

## Definitions and units

Lengths use the existing model millimetres; angles use degrees. World XYZ
orientation applies extrinsic world X, then Y, then Z rotations (`Rz * Ry * Rx`).
Frames are right-handed and orthonormal. Workplane local XY is its infinite
plane, local Z its normal. The longer displayed in-plane arrow is +X and the
shorter is +Y. Axes have a meaningful origin and signed unit direction. Points
have a position. Reverse normal/direction is available on new planes and axes.

Viewport names use fixed-size, camera-facing labels: a Workplane label sits
outside its projected lower-left corner, an Axis label at its negative display
endpoint (foot), and a Point label floats above/right of its marker. Labels follow
rename, visibility, preview, Undo/Redo and Open; they cannot intercept selection.
Camera movement only updates their presentation and never dirties the document.
The resolved Qt system UI font family is used explicitly, avoiding an OCCT
generic-font fallback to symbol-only fonts on Fedora KDE.

Displayed Workplanes and Axes are finite aids with a stored half-extent and local
display offset, not bounded mathematical planes/lines. A new Workplane is centered
on the mean selected reference anchors projected onto its support plane; planar
Faces supply their area centroid with instance placement applied once. Source
bounds determine the displayed size with a margin (minimum Workplane half-extent
10 mm). Copies/offsets of Workplanes retain their half-extent. With no references,
the default half-extent is 100 mm. A two-point Axis is centered between its points
and extends beyond each by 10% of their distance, with a minimum 1 mm overrun.
Move/Rotate carries the local display offset with the analytic frame. The
analytic origin remains unchanged: the first Point is still the origin for
distances on a two-point Axis and the first defining Point is still a Workplane
coordinate origin for future sketches. Their presentations do
not enter Part B-Reps, Boolean operations or the ordinary solid Fit All bounds.
With construction-only documents, Fit All uses visible finite display extents;
Fit Selected is also available in the construction context menu.

| Workplane method | Meaning |
| --- | --- |
| Three points | First point is origin, first-to-second is +X; the third determines +Z. |
| Normal to axis through point | Axis direction is normal; origin is the selected point. |
| Through axis and point | Contains the axis and point; +X follows the axis. An on-axis point is ambiguous. |
| Planar face | Copies the analytic support frame, including instance placement and oriented normal. |
| Midplane Workplane | Halfway between two distinct parallel planes, or one of the two angle bisectors of intersecting planes. Faces, Workplanes and mixed pairs are accepted. |
| Offset | Keeps orientation and moves origin by signed normal distance. |
| Edge and face | Plane through a straight edge, perpendicular to the selected plane; the edge direction must be parallel to that plane. |
| Edge/axis and angle | Plane through that line, rotated from the orientation of an explicit zero plane. The line must be parallel to the zero plane. Its origin is the line origin; the zero plane need not contain the line. |
| Parallel through point | Reference orientation, selected point as origin. |
| XYZ and orientation | World origin and world X/Y/Z rotation angles. |
| Move / Rotate | Rigid edit of the existing Workplane's own frame. |

| Axis method | Meaning |
| --- | --- |
| Two points | Origin at first point; direction towards second. |
| Two Workplanes / planar Faces | Unique line of intersection of their infinite support planes. |
| Straight edge | Supporting line, respecting edge orientation. |
| Cylindrical face | Analytic cylinder axis, including placement, independent of creation history. |
| Point and direction | Point as origin; nonzero world direction is normalized. |
| Normal to Workplane | Normal line through Workplane origin; Move can relocate it. |
| Move / Rotate | Rigid edit of the existing Axis. |

| Point method | Meaning |
| --- | --- |
| XYZ / Vertex | Explicit coordinates or exact located vertex position. |
| Edge midpoint | Half the arc length of a finite open edge, not the average curve parameter. Closed edges are refused because they have no unique midpoint. |
| On axis | Signed distance from the axis origin. |
| Axis and Workplane | Unique intersection of the infinite line and plane. |
| Two axes | Actual intersection within OCCT positional tolerance; skew axes are refused. |
| Center of circle | Center of an analytic circular edge, including an arc. |
| Offset | Point plus a world XYZ vector. |
| Projection | Orthogonal projection onto a Workplane/plane. |

Parallel/coincident planes, parallel/coincident/skew axes, zero directions,
coincident/collinear defining points and invalid numeric input produce no new
object and no Undo entry. A closed edge can use Center of circle when applicable.
For a plane intersection, the Axis origin is the point on the intersection
line closest to the first plane origin.

Geometry type is resolved from the current B-Rep via OCCT adaptors, not Part
names, primitive flags or modeling history. Reference placements are applied once.

## Midplane Workplane

Choose **Modeling → Construction Geometry → Workplane → Midplane Workplane**,
or select two planar Faces / two Workplanes and use **Midplane Workplane** in
their context menu. The two explicit roles also allow a Face and a Workplane to
be combined. All sources are evaluated once; the result is an ordinary,
independent Workplane with the existing preview, Cancel, Undo/Redo and `.odc`
storage behavior.

For parallel planes, the result is exactly halfway along their common normal,
independent of opposite Face normals or tangential differences between their
origins. Coincident planes are refused. The default **Between planes / smaller
angle** solution is the only available geometric midplane for parallel planes;
requesting the other solution reports an error and removes the stale preview.

For intersecting planes (such as the sides of a wedge), the result contains their
intersection line and bisects their angle. There are two perpendicular bisector
planes. **Solution** explicitly selects the smaller-angle bisector or the other
bisector; a 90° pair has two equally valid 45° solutions. An obtuse wedge can
require the other solution. **Reverse normal / direction** only flips the chosen
plane's orientation; it does not select the other bisector. Unit-normal signed
plane equations determine equidistance, with signs normalized independently of
Face orientation. The origin is the average source origins projected onto the
selected bisector; a nearly parallel default therefore does not escape to a
remote intersection line. Display extent never limits the source support planes.

Geometry regressions check parallel/opposed normals, 60° wedges and 30° results,
both equidistance loci, normal reversal, operand order, translated wedges,
perpendicular and nearly parallel planes, large rigid placements, and safe
refusal. Session regressions cover selected Faces, Workplane and mixed operands,
solution switching/recovery, Cancel, single-step Undo/Redo, native round-trips
and source deletion independence. No schema change is required beyond v2.

## Architecture

- `ConstructionGeometry.h/.cpp`: value objects, method/role definitions, analytic
  resolver and pure candidate calculations. No widgets or AIS ownership.
- `ConstructionPanel.h/.cpp`: parameter widgets and callbacks only.
- `ConstructionTools.cpp`: viewport/picking/presentation adapter and command
  implementation. `CommandSession::Kind::Construction` uses the existing
  lifecycle, keyboard routing and tool cancellation.
- `ConstructionChange`: before/after value snapshots carried by the existing
  chronological `PullChange` Undo/Redo stacks alongside face and Boolean edits.
- `NativeDocumentSnapshot::constructions`: confirmed values only, independent of
  presentations, source references and Undo data.
- `MainWindow`: typed browser routing and existing document dirty/save prompts.

The existing CadViewport remains the single runtime document/history owner.
Construction values and presentation handles are separate collections; no
second Part store or undo manager is introduced. The persisted Workplane frame
provides an origin, +X/+Y and normal for a future Sketch Editor. Sketch entities,
constraints, profile ownership and sketch editing are not implemented here.

## Storage and document lifecycle

Native schema v2 stores construction metadata using standard BinXCAF/OCAF
attributes in the same `.odc` file. Objects are not fake XCAF solid components.
The root uses `OpenDesigner.RootAssembly.v2` and an explicit schema integer;
construction labels store UUID, type, name, origin, normal, +X, display extent,
visibility and selection lock. Optional `OpenDesigner.Presentation.v1` child
metadata stores the local display offset. The construction group stores the three
name counters. Readers accept earlier v2 records without these extensions: display
offsets default to zero and counters are inferred from existing WP/AX/PT names.
Malformed counters or presentation metadata are refused. IDs are unique across
Parts and constructions.

The reader accepts v0.3.7 schema v1, upgrading its snapshot in memory with an
empty construction collection. New documents save as v2. The separate v2 root
marker makes the old v0.3.7 application refuse new files instead of silently
removing construction data on a subsequent Save. Future schemas, invalid frame
vectors, duplicate identities and malformed flags are refused transactionally.

Save/Save As preserve selection, active previews and Undo/Redo; only confirmed
objects are saved. Camera navigation does not mark the document dirty. New and
successful Open remove all old presentations, command panels, manipulators,
selection and history, and rebuild both browser groups. Failed Open/Save leaves
the current document intact. Existing temporary staging and QSaveFile atomic
replacement remain in use.

## Verification and limits

`construction-geometry` covers every method, analytic expected positions and
directions, transformed references, arc-length midpoint, source independence,
and degenerate/ambiguous input refusal. `native-document-roundtrip` includes all
three types, Unicode names, frames, extents, visibility/lock, Parts and camera,
v1 migration and real malformed BinXCAF metadata. `construction-sessions` covers
real Face/Vertex picks, explicit browser operands, preview/Cancel, orange handle
dragging, properties/deletion, mixed history, Save/Save As, confirmed-state Save,
real Open/New and cleanup. Existing modeling and document suites remain enabled.

```sh
cmake -S . -B build -DOPENDESIGNER_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
git diff --check
```

GUI suites need X11/OpenGL. Geometry calculations and native storage remain
synchronous. There are no nested assembly-local construction coordinates,
associative updates, closed-edge midpoint conventions, arbitrary nonplanar
Workplanes, automatic surface fitting, Sketch Editor or persistent subshape IDs.
Fedora KDE manual validation of this new workflow is still pending; the earlier
v0.3.7 user approval does not establish v0.4.0 interaction quality.

SolidDesigner v16 is a workflow reference only. This implementation contains no
proprietary code, reference assets, course models or copied UI resources.

Local validation on 2026-10-21: the complete Release CMake build succeeded and all
18 CTests passed (148.23 seconds), including the existing modeling regressions.
`git diff --check` passed. The OCCT viewport capture was inspected for the new
construction presentations. This does not replace the pending Fedora KDE manual
workflow verification or Windows/macOS validation.

Midplane extension validation on 2026-10-21: complete CMake build and all 18
CTests passed (130.49 seconds), including the new bisector, context-menu,
preview/history and native round-trip checks. `git diff --check` passed.

Naming/presentation extension validation on 2026-10-21: complete Release CMake
build and all 18 CTests passed (136.09 seconds). Coverage includes independent
WP/AX/PT sequences, Cancel/Undo/delete/Open/New numbering behavior, display
centering with transformed Faces, long/short/reversed two-point Axis overrun,
Move/Rotate display offsets, labels/visibility/non-selectability/camera refresh,
native offset/counter round-trips, early-v2 defaults and malformed metadata.
Visual inspection then identified a symbol-font fallback on KDE. After selecting
the Qt-resolved UI font explicitly, the application and selection/session test
executable were rebuilt; `construction-sessions` passed again (13.23 seconds),
and viewport captures confirmed readable WP1, AX1, PT1 and PT2 labels.
`git diff --check` passed. Manual Fedora KDE workflow verification remains pending.
